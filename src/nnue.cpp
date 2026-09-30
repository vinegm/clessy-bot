#include "nnue.hpp"

#include "logger.hpp"

#include <cstring>
#include <fstream>
#include <memory>

namespace {
// At most one network is loaded per process, and it has no business being
// visible in the header.
std::unique_ptr<NNUE::Network> network;
} // namespace

void NNUE::accumulate(const Position &pos, Color perspective, int32_t (&accumulator)[HIDDEN_SIZE]) {
  for (int neuron = 0; neuron < HIDDEN_SIZE; neuron++) {
    accumulator[neuron] = network->feature_bias[neuron];
  }

  for (int square = 0; square < 64; square++) {
    Piece piece = pos.lookup_table[square];
    if (piece == NO_PIECE) continue;

    int feature =
        feature_index(perspective, decode_color(piece), decode_type(piece), Square(square));

    const int16_t *weights = network->feature_weights[feature];
    for (int neuron = 0; neuron < HIDDEN_SIZE; neuron++) {
      accumulator[neuron] += weights[neuron];
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

  auto net = std::make_unique<Network>();
  file.read(reinterpret_cast<char *>(net->feature_weights), sizeof(net->feature_weights));
  file.read(reinterpret_cast<char *>(net->feature_bias), sizeof(net->feature_bias));
  file.read(reinterpret_cast<char *>(net->output_weights), sizeof(net->output_weights));
  file.read(reinterpret_cast<char *>(&net->output_bias), sizeof(net->output_bias));
  if (!file) {
    Logger::error("nnue: '", path, "' has a valid header but truncated weights");
    return false;
  }

  network = std::move(net);
  Logger::info("nnue: loaded '", path, "'");
  return true;
}

bool NNUE::is_loaded() { return network != nullptr; }

int NNUE::evaluate(const Position &pos) {
  Color stm = pos.to_move;

  int32_t stm_accumulator[HIDDEN_SIZE];
  int32_t nstm_accumulator[HIDDEN_SIZE];
  accumulate(pos, stm, stm_accumulator);
  accumulate(pos, opposite_color(stm), nstm_accumulator);

  int64_t sum = network->output_bias;
  for (int neuron = 0; neuron < HIDDEN_SIZE; neuron++) {
    sum += crelu(stm_accumulator[neuron]) * network->output_weights[neuron];
    sum += crelu(nstm_accumulator[neuron]) * network->output_weights[HIDDEN_SIZE + neuron];
  }

  // NOTE: truncates toward zero, like clessy_nnue/inference.py
  return static_cast<int>(sum * SCALE / (static_cast<int64_t>(QA) * QB));
}
