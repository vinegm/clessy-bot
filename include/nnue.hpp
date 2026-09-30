#pragma once

#include "chess_types.hpp"
#include "position.hpp"

#include <cstdint>
#include <string>

/// @brief The engine's single loaded NNUE network.
///
/// All members are static: at most one network is loaded per process, and
/// every evaluation goes through it.
class NNUE {
public:
  NNUE() = delete;

  static constexpr int NUM_FEATURES = 768;
  static constexpr int HIDDEN_SIZE = 256;

  // Quantization constants, must match clessy-nnue/clessy_nnue/model.py.
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

  /**
   * @brief Load a .nnue network file produced by clessy-nnue.
   *
   * @param path Path to the network file
   * @return true on success, false on IO error or header mismatch
   */
  static bool load(const std::string &path);
  static bool is_loaded();

  /**
   * @brief Feature index of a piece for one perspective.
   *
   * Layout: rel_color * 384 + (piece_type - 1) * 64 + rel_square, where
   * rel_square is vertically flipped (sq ^ 56) for the black perspective.
   */
  static constexpr int
      feature_index(Color perspective, Color color, PieceType type, Square square) {
    int rel_color = (color == perspective) ? 0 : 1;
    int rel_square = (perspective == WHITE) ? square : square ^ 56;

    return rel_color * 384 + (type - 1) * 64 + rel_square;
  }

  /**
   * @brief Evaluate a position with the loaded network.
   *
   * Rebuilds both accumulators from scratch (no incremental updates yet).
   *
   * @param pos The position to evaluate
   * @return Evaluation in centipawns from the side to move's point of view
   */
  static int evaluate(const Position &pos);

private:
  /// @brief Rebuild one perspective's accumulator from the pieces on the board.
  static void
      accumulate(const Position &pos, Color perspective, int32_t (&accumulator)[HIDDEN_SIZE]);

  /// @brief Clipped ReLU, the activation the trainer quantizes against.
  static constexpr int32_t crelu(int32_t value) {
    if (value < 0) return 0;
    if (value > QA) return QA;

    return value;
  }
};
