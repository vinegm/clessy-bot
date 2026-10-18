#pragma once

#include "chess_types.hpp"

#include <string>

class Notation {
public:
  Notation() = delete;

  static std::string square_to_uci(Square square);
  static Square uci_to_square(const std::string &uci);

  static std::string move_to_uci(const Move &move);
  static Move uci_to_move(const std::string &uci);
  static std::string moves_to_uci(const MoveList &moves);

  static char piece_type_to_char(PieceType type);
  static PieceType char_to_piece_type(char c);
  static char piece_to_char(Piece piece);
};
