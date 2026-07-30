#pragma once

#include "chess_types.hpp"
#include "position.hpp"

#include <cstdint>
#include <string>

class NNUE {
public:
  NNUE() = delete;

  // Architecture: (768 -> L1)x2 -> CReLU -> 1, see clessy-nnue.
  static constexpr int NUM_FEATURES = 768;
  static constexpr int L1 = 256;

  // Quantization constants, must match clessy-nnue's implementation.
  static constexpr int SCALE = 400;
  static constexpr int QA = 255;
  static constexpr int QB = 64;

  static constexpr uint32_t MAGIC = 0x434e5545; // "CNUE"
  static constexpr uint32_t VERSION = 1;

  struct Network {
    int16_t ft_weights[NUM_FEATURES][L1]; // [feature][neuron]
    int16_t ft_bias[L1];
    int16_t out_weights[2 * L1]; // side-to-move half first
    int32_t out_bias;
  };

  // Load a .nnue network file produced by clessy-nnue.
  static bool load(const std::string &path);

  static bool is_loaded();

  // Feature index of a piece for one perspective.
  static constexpr int
      feature_index(Color perspective, Color color, PieceType type, Square square) {
    int rel_color = (color == perspective) ? 0 : 1;
    int rel_square = (perspective == WHITE) ? square : square ^ 56;

    return rel_color * 384 + (type - 1) * 64 + rel_square;
  }

  // Evaluate a position with the loaded network.
  // Rebuilds both accumulators from scratch (no incremental updates yet).
  static int evaluate(const Position &pos);

private:
  // Rebuild one perspective's accumulator from the pieces on the board.
  static void accumulate(const Position &pos, Color perspective, int32_t (&accumulator)[L1]);

  // Clipped ReLU, the activation the trainer quantizes against.
  static constexpr int32_t crelu(int32_t value) {
    if (value < 0) return 0;
    if (value > QA) return QA;

    return value;
  }
};
