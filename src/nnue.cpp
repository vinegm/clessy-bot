#include "nnue.hpp"

#include "logger.hpp"
#include "position.hpp"

#include <algorithm>
#include <fstream>

void NNUE::refresh(const Position &pos, Accumulator &accumulator) {
  for (auto &perspective : accumulator.values) {
    for (int neuron = 0; neuron < HIDDEN_SIZE; neuron++) {
      perspective[neuron] = network->feature_bias[neuron];
    }
  }

  for (int square = 0; square < 64; square++) {
    Piece piece = pos.lookup_table[square];
    if (piece == NO_PIECE) continue;

    add_feature(accumulator, decode_color(piece), decode_type(piece), Square(square));
  }

  accumulator.generation = network_generation;
}

void NNUE::add_feature(Accumulator &accumulator, Color color, PieceType type, Square square) {
  for (int perspective = 0; perspective < 2; perspective++) {
    int feature = feature_index(Color(perspective), color, type, square);

    const int16_t *weights = network->feature_weights[feature];
    int16_t *values = accumulator.values[perspective];
    for (int neuron = 0; neuron < HIDDEN_SIZE; neuron++) {
      values[neuron] += weights[neuron];
    }
  }
}

void NNUE::remove_feature(Accumulator &accumulator, Color color, PieceType type, Square square) {
  for (int perspective = 0; perspective < 2; perspective++) {
    int feature = feature_index(Color(perspective), color, type, square);

    const int16_t *weights = network->feature_weights[feature];
    int16_t *values = accumulator.values[perspective];
    for (int neuron = 0; neuron < HIDDEN_SIZE; neuron++) {
      values[neuron] -= weights[neuron];
    }
  }
}

// NOTE: one feature added and one removed, every quiet move and the bulk of the rest,
// in a single pass instead of a copy and two read-modify-write passes over the child
void NNUE::update(
    const Accumulator &parent,
    Accumulator &child,
    const FeatureChange &added,
    const FeatureChange &removed
) {
  for (int perspective = 0; perspective < 2; perspective++) {
    const int16_t *add =
        network->feature_weights
            [feature_index(Color(perspective), added.color, added.type, added.square)];
    const int16_t *sub =
        network->feature_weights
            [feature_index(Color(perspective), removed.color, removed.type, removed.square)];

    const int16_t *from = parent.values[perspective];
    int16_t *to = child.values[perspective];
    for (int neuron = 0; neuron < HIDDEN_SIZE; neuron++) {
      to[neuron] = static_cast<int16_t>(from[neuron] + add[neuron] - sub[neuron]);
    }
  }
}

void NNUE::AccumulatorStack::push(const Position &pos) {
  // NOTE: the board still shows the parent, the only moment it can be refreshed. A
  // search root is never evaluated itself, so without this no ply under it would
  // have a parent to build from and every evaluation would refresh from scratch
  Accumulator &parent = entries[top];
  if (network && parent.generation != network_generation) refresh(pos, parent);

  top++;
  if (top == entries.size()) entries.emplace_back();

  Accumulator &entry = entries[top];
  entry.generation = 0;
  entry.added_count = 0;
  entry.removed_count = 0;
}

void NNUE::AccumulatorStack::advance() {
  const Accumulator &parent = entries[top - 1];
  Accumulator &child = entries[top];

  // NOTE: nothing to build from, evaluate refreshes this ply if it ever gets there
  if (!parent.is_computed()) return;

  int first_add = 0;
  int first_remove = 0;
  if (child.added_count > 0 && child.removed_count > 0) {
    update(parent, child, child.added[0], child.removed[0]);
    first_add = 1;
    first_remove = 1;
  } else {
    std::copy(&parent.values[0][0], &parent.values[0][0] + 2 * HIDDEN_SIZE, &child.values[0][0]);
  }

  for (int i = first_add; i < child.added_count; i++) {
    const FeatureChange &change = child.added[i];
    add_feature(child, change.color, change.type, change.square);
  }

  for (int i = first_remove; i < child.removed_count; i++) {
    const FeatureChange &change = child.removed[i];
    remove_feature(child, change.color, change.type, change.square);
  }

  child.generation = parent.generation;
}

bool NNUE::load(const std::string &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    Logger::error("nnue: cannot open '", path, "'");
    return false;
  }

  uint32_t header[4];
  file.read(reinterpret_cast<char *>(header), sizeof(header));
  if (!file) {
    Logger::error("nnue: '", path, "' is too short to hold a header");
    return false;
  }

  if (header[0] != MAGIC) {
    Logger::error("nnue: '", path, "' is not a Clessy network (bad magic)");
    return false;
  }

  if (header[1] != VERSION) {
    Logger::error("nnue: '", path, "' is version ", header[1], ", expected ", VERSION);
    return false;
  }

  if (header[2] != NUM_FEATURES || header[3] != HIDDEN_SIZE) {
    Logger::error(
        "nnue: '",
        path,
        "' is ",
        header[2],
        "x",
        header[3],
        ", expected ",
        NUM_FEATURES,
        "x",
        HIDDEN_SIZE
    );
    return false;
  }

  auto loaded = std::make_unique<Network>();
  file.read(reinterpret_cast<char *>(loaded->feature_weights), sizeof(loaded->feature_weights));
  file.read(reinterpret_cast<char *>(loaded->feature_bias), sizeof(loaded->feature_bias));
  file.read(reinterpret_cast<char *>(loaded->output_weights), sizeof(loaded->output_weights));
  file.read(reinterpret_cast<char *>(&loaded->output_bias), sizeof(loaded->output_bias));
  if (!file) {
    Logger::error("nnue: '", path, "' has a valid header but truncated weights");
    return false;
  }

  network = std::move(loaded);
  network_generation++;
  Logger::info("nnue: loaded '", path, "'");
  return true;
}

bool NNUE::is_loaded() { return network != nullptr; }

int NNUE::evaluate(const Position &pos) {
  Accumulator &accumulator = pos.accumulators().current();
  if (accumulator.generation != network_generation) refresh(pos, accumulator);

  const int16_t *ours = accumulator.values[pos.to_move];
  const int16_t *theirs = accumulator.values[opposite_color(pos.to_move)];

  // NOTE: int32 holds the dot, it vectorizes where int64 does not. Output weights are
  // clamped to 127, so 512 terms of at most 255 * 127 stay under 16.6M
  int32_t dot = 0;
  for (int neuron = 0; neuron < HIDDEN_SIZE; neuron++) {
    dot += crelu(ours[neuron]) * network->output_weights[neuron];
    dot += crelu(theirs[neuron]) * network->output_weights[HIDDEN_SIZE + neuron];
  }

  int64_t sum = static_cast<int64_t>(dot) + network->output_bias;

  // NOTE: truncates toward zero, like clessy_nnue/inference.py
  return static_cast<int>(sum * SCALE / (static_cast<int64_t>(QA) * QB));
}
