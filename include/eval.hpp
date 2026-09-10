#pragma once

#include "chess_types.hpp"

class Position;

class Evaluator {
public:
  int evaluate(const Position &position) const;

private:
  int piece_score(PieceType type, Color color, Square square) const;
};
