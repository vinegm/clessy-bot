#pragma once

#include "chess_types.hpp"
#include "nnue.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct UndoInfo {
  Move move{};
  uint8_t captured_piece_encoded{};

  CastlingRights castling_rights{};
  std::optional<Square> en_passant_square{};
  int halfmove_clock{};
  int fullmove_counter{};
  uint64_t hash{}; // position hash before the move
};

class Position {
public:
  Position(const std::string &fen) { set_fen(fen); }

  Color to_move{};
  uint64_t occupancy[3]{};  // [Color]
  Piece lookup_table[64]{}; // Encoded pieces
  uint64_t hash{};          // Zobrist hash, updated incrementally

  CastlingRights castling_rights{};
  std::optional<Square> en_passant_square{};
  int halfmove_clock{};
  int fullmove_counter{};

  NNUE::Accumulator &accumulator() const { return accumulator_slot.value; }
  constexpr int get_bb_idx(Color color, PieceType type) const {
    int code = encode_piece(color, type);
    return (color == WHITE) ? code - 1 : code - 3;
  }
  constexpr uint64_t get_bb(Color color, PieceType type) const {
    return bitboards[get_bb_idx(color, type)];
  }
  constexpr uint64_t &get_bb_ref(Color color, PieceType type) {
    return bitboards[get_bb_idx(color, type)];
  }
  bool has_non_pawn_material(Color color) const {
    return (occupancy[color] & ~get_bb(color, PAWN) & ~get_bb(color, KING)) != 0;
  }
  Piece get_piece_at(Square square) { return lookup_table[square]; }

  void set_fen(const std::string &fen);
  std::string get_fen() const;
  void make_move(const Move &move);
  void make_null_move();
  void undo_move();
  bool is_repetition() const;

private:
  struct AccumulatorSlot {
    NNUE::Accumulator value{};

    AccumulatorSlot() = default;
    AccumulatorSlot(const AccumulatorSlot &) {}
    AccumulatorSlot &operator=(const AccumulatorSlot &) {
      value.generation = 0;
      return *this;
    }
  };

  mutable AccumulatorSlot accumulator_slot{};

  std::vector<UndoInfo> undo_stack{};
  uint64_t bitboards[12]{};

  Square get_captured_square(const Move &move) const;
  void push_undo_info(const Move &move, Piece captured_piece_encoded, uint64_t hash_before);
  void update_castling_rights(const Move &move, Square captured_square);
  void add_piece(Color color, PieceType piece, Square square);
  void remove_piece(Color color, PieceType piece, Square square);
  void pass_turn();

  static uint64_t en_passant_key(const std::optional<Square> &square);
};
