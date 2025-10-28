#pragma once

#include "chess_types.hpp"

#include <optional>
#include <string>
#include <vector>

struct UndoInfo {
  Move move{};
  uint8_t captured_piece_encoded{};

  uint8_t castling_rights{};
  std::optional<Square> en_passant_square{};
  int halfmove_clock{};
  int fullmove_counter{};
};

class Position {
public:
  Position(const std::string &fen);

  PieceColor to_move{};
  uint64_t bitboards[12]{};   // [BitboardIndex]
  uint64_t occupancy[3]{};    // [PieceColor]
  uint8_t lookup_table[64]{}; // Encoded pieces

  uint8_t castling_rights{};
  std::optional<Square> en_passant_square{};
  int halfmove_clock{};
  int fullmove_counter{};

  std::string get_fen() const;
  Piece get_piece_at(Square square);
  void make_move(const Move &move);
  void undo_move();

private:
  std::vector<UndoInfo> undo_stack{};

  Square get_captured_square(const Move &move) const;
  void push_undo_info(const Move &move, uint8_t captured_piece_encoded);
  void update_castling_rights(const Move &move, const Piece &piece, Square captured_square);
  void add_piece(PieceColor color, PieceType piece, Square square);
  void remove_piece(PieceColor color, PieceType piece, Square square);
  void pass_turn();
};
