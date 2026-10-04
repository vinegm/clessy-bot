#pragma once

#include "chess_types.hpp"

class Position;

class Eval {
public:
  Eval() = delete;

  // NOTE: from the side to move's perspective
  static int evaluate(const Position &position);
  static const char *source_name();

private:
  static int evaluate_hce(const Position &position);
  static int piece_score(PieceType type, Color color, Square square);
};
