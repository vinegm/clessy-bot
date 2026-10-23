#pragma once

#include "chess_types.hpp"

#include <cstdint>
#include <memory>
#include <string>

class Position;

// NOTE: (768 -> HIDDEN_SIZE)x2 -> CReLU -> 1, must match clessy-nnue
class NNUE {
public:
  NNUE() = delete;

  static constexpr int NUM_FEATURES = 768;
  static constexpr int HIDDEN_SIZE = 256;

  static constexpr int SCALE = 400;
  static constexpr int QA = 255;
  static constexpr int QB = 64;

  static constexpr uint32_t MAGIC = 0x434e5545; // "CNUE"
  static constexpr uint32_t VERSION = 1;

  struct Network {
    int16_t feature_weights[NUM_FEATURES][HIDDEN_SIZE];
    int16_t feature_bias[HIDDEN_SIZE];
    int16_t output_weights[2 * HIDDEN_SIZE]; // side to move half first
    int32_t output_bias;
  };

  // NOTE: int16 halves the memory every move touches and doubles the SIMD width. It
  // cannot overflow: clessy-nnue clamps feature weights and bias to 1.98, so even
  // 34 terms of 505 (32 pieces, the bias, a promotion mid-update) stay under 17200
  struct Accumulator {
    int16_t values[2][HIDDEN_SIZE]; // [Color]

    // NOTE: the network generation it was built for, 0 means not built
    uint32_t generation = 0;

    bool is_computed() const { return generation != 0; }
  };

  static bool load(const std::string &path);
  static bool is_loaded();
  static int evaluate(const Position &pos);

  static void add_feature(Accumulator &accumulator, Color color, PieceType type, Square square);
  static void remove_feature(Accumulator &accumulator, Color color, PieceType type, Square square);

  static constexpr int
      feature_index(Color perspective, Color color, PieceType type, Square square) {
    int relative_color = 0;
    if (color != perspective) relative_color = 1;

    int relative_square = square;
    if (perspective == BLACK) relative_square = square ^ 56;

    return relative_color * 384 + (type - 1) * 64 + relative_square;
  }

private:
  static inline std::unique_ptr<Network> network;
  static inline uint32_t network_generation = 0;

  static void refresh(const Position &pos, Accumulator &accumulator);

  static constexpr int32_t crelu(int32_t value) {
    if (value < 0) return 0;
    if (value > QA) return QA;

    return value;
  }
};
