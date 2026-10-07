#include "nnue.hpp"

#include "logger.hpp"
#include "position.hpp"

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
    int32_t *values = accumulator.values[perspective];
    for (int neuron = 0; neuron < HIDDEN_SIZE; neuron++) {
      values[neuron] += weights[neuron];
    }
  }
}

void NNUE::remove_feature(Accumulator &accumulator, Color color, PieceType type, Square square) {
  for (int perspective = 0; perspective < 2; perspective++) {
    int feature = feature_index(Color(perspective), color, type, square);

    const int16_t *weights = network->feature_weights[feature];
    int32_t *values = accumulator.values[perspective];
    for (int neuron = 0; neuron < HIDDEN_SIZE; neuron++) {
      values[neuron] -= weights[neuron];
    }
  }
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
  Accumulator &accumulator = pos.accumulator();
  if (accumulator.generation != network_generation) refresh(pos, accumulator);

  const int32_t *ours = accumulator.values[pos.to_move];
  const int32_t *theirs = accumulator.values[opposite_color(pos.to_move)];

  int64_t sum = network->output_bias;
  for (int neuron = 0; neuron < HIDDEN_SIZE; neuron++) {
    sum += static_cast<int64_t>(crelu(ours[neuron])) * network->output_weights[neuron];
    sum +=
        static_cast<int64_t>(crelu(theirs[neuron])) * network->output_weights[HIDDEN_SIZE + neuron];
  }

  // NOTE: truncates toward zero, like clessy_nnue/inference.py
  return static_cast<int>(sum * SCALE / (static_cast<int64_t>(QA) * QB));
}
