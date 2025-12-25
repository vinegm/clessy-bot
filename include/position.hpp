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
  Position(const std::string &fen) { set_fen(fen); }

  Color to_move{};
  uint64_t occupancy[3]{};    // [Color]
  uint8_t lookup_table[64]{}; // Encoded pieces

  uint8_t castling_rights{};
  std::optional<Square> en_passant_square{};
  int halfmove_clock{};
  int fullmove_counter{};

  constexpr int get_bb_idx(Color color, PieceType type) const { return (color * 6) + type - 1; }
  constexpr uint64_t get_bb(Color color, PieceType type) const {
    return bitboards[get_bb_idx(color, type)];
  }
  constexpr uint64_t &get_bb_ref(Color color, PieceType type) {
    return bitboards[get_bb_idx(color, type)];
  }

  void set_fen(const std::string &fen);
  std::string get_fen() const;
  Piece get_piece_at(Square square);
  void make_move(const Move &move);
  void undo_move();

private:
  std::vector<UndoInfo> undo_stack{};
  uint64_t bitboards[12]{};

  Square get_captured_square(const Move &move) const;
  void push_undo_info(const Move &move, uint8_t captured_piece_encoded);
  void update_castling_rights(const Move &move, const Piece &piece, Square captured_square);
  void add_piece(Color color, PieceType piece, Square square);
  void remove_piece(Color color, PieceType piece, Square square);
  void pass_turn();
};
