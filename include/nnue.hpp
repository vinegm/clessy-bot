#pragma once

#include "chess_types.hpp"

#include <cstdint>
#include <string>

// Only ever a parameter here. Declaring it rather than including position.hpp
// is what lets Position hold an Accumulator without a cycle.
class Position;

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

  /**
   * The feature transformer's output for both perspectives, kept up to date
   * across make_move and undo_move instead of rebuilt per evaluation.
   *
   * Indexed by the colour whose perspective it is, not by side to move, so a
   * move does not have to swap the halves. Adding and subtracting a feature's
   * weight row are exact integer inverses, which is what lets undo_move
   * reverse a move rather than recompute the position.
   *
   * `generation` is the network generation these values were built for. Zero
   * means "not built": a fresh position, a position whose board changed
   * outside make_move, or one copied from another. Loading a network bumps
   * the generation, so accumulators built for the old one refresh themselves
   * rather than going quietly stale.
   */
  struct Accumulator {
    int32_t values[2][L1]; // [perspective]
    uint32_t generation = 0;

    bool is_computed() const { return generation != 0; }
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

  // Evaluate a position with the loaded network, refreshing the position's
  // accumulator first if it is not current.
  static int evaluate(const Position &pos);

  /**
   * Fold one piece into an already-computed accumulator, both perspectives at
   * once.
   *
   * Called by Position for every board mutation, and only while the
   * accumulator is computed — an untouched one stays that way, so a perft
   * that never evaluates pays a predictable branch and nothing more.
   */
  static void add_feature(Accumulator &accumulator, Color color, PieceType type, Square square);
  static void remove_feature(Accumulator &accumulator, Color color, PieceType type, Square square);

private:
  // Rebuild both perspectives from the pieces on the board.
  static void refresh(const Position &pos, Accumulator &accumulator);

  // Clipped ReLU, the activation the trainer quantizes against.
  static constexpr int32_t crelu(int32_t value) {
    if (value < 0) return 0;
    if (value > QA) return QA;

    return value;
  }
};
