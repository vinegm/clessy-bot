#include "nnue.hpp"

#include "logger.hpp"

#include <fstream>
#include <memory>

namespace {
// singleton network instance, since the NNUE
// is global and immutable after load
std::unique_ptr<NNUE::Network> network;
} // namespace

void NNUE::accumulate(const Position &pos, Color perspective, int32_t (&accumulator)[L1]) {
  for (int neuron = 0; neuron < L1; neuron++) {
    accumulator[neuron] = network->ft_bias[neuron];
  }

  for (int square = 0; square < 64; square++) {
    Piece piece = pos.lookup_table[square];
    if (piece == NO_PIECE) continue;

    int feature =
        feature_index(perspective, decode_color(piece), decode_type(piece), Square(square));

    // The weights were transposed on export so one active feature is a single
    // contiguous row to add.
    const int16_t *weights = network->ft_weights[feature];
    for (int neuron = 0; neuron < L1; neuron++) {
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

  // Split out from one another because the three say different things: the
  // wrong kind of file, a stale export, and a network built for a different
  // architecture all need different fixes.
  if (header[0] != MAGIC) {
    Logger::error("nnue: '", path, "' is not a Clessy network (bad magic)");
    return false;
  }

  if (header[1] != VERSION) {
    Logger::error("nnue: '", path, "' is version ", header[1], ", expected ", VERSION);
    return false;
  }

  if (header[2] != NUM_FEATURES || header[3] != L1) {
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
        L1
    );
    return false;
  }

  auto net = std::make_unique<Network>();
  file.read(reinterpret_cast<char *>(net->ft_weights), sizeof(net->ft_weights));
  file.read(reinterpret_cast<char *>(net->ft_bias), sizeof(net->ft_bias));
  file.read(reinterpret_cast<char *>(net->out_weights), sizeof(net->out_weights));
  file.read(reinterpret_cast<char *>(&net->out_bias), sizeof(net->out_bias));
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

  int32_t stm_accumulator[L1];
  int32_t nstm_accumulator[L1];
  accumulate(pos, stm, stm_accumulator);
  accumulate(pos, opposite_color(stm), nstm_accumulator);

  int64_t sum = network->out_bias;
  for (int neuron = 0; neuron < L1; neuron++) {
    sum += crelu(stm_accumulator[neuron]) * network->out_weights[neuron];
    sum += crelu(nstm_accumulator[neuron]) * network->out_weights[L1 + neuron];
  }

  // Truncating toward zero, which is what clessy_nnue/inference.py reproduces.
  return static_cast<int>(sum * SCALE / (QA * QB));
}
