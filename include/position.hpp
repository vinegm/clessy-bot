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
  uint64_t hash{};
};

class Position {
public:
  Position(const std::string &fen) { set_fen(fen); }

  Color to_move{};
  uint64_t occupancy[3]{};  // [Color]
  Piece lookup_table[64]{}; // Encoded pieces
  uint64_t hash{};          // Zobrist hash

  CastlingRights castling_rights{};
  std::optional<Square> en_passant_square{};
  int halfmove_clock{};
  int fullmove_counter{};

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

  void set_fen(const std::string &fen);
  std::string get_fen() const;
  bool is_repetition() const;
  Piece get_piece_at(Square square);
  void make_move(const Move &move);
  void undo_move();

  // NOTE: illegal in check and unsound in zugzwang, both are the caller's to rule out
  void make_null_move();

  bool has_non_pawn_material(Color color) const {
    return (occupancy[color] & ~get_bb(color, PAWN) & ~get_bb(color, KING)) != 0;
  }

  NNUE::Accumulator &accumulator() const { return accumulator_slot.value; }

private:
  /**
   * Holder that drops the accumulator on copy.
   *
   * Copying a Position is rare and never wants the accumulator with it — the
   * en passant legality probe copies one only to make and unmake a move on
   * it, and never evaluates. Leaving the copy's accumulator uncomputed keeps
   * that probe from paying for two kilobytes it would throw away, and a
   * refresh is one call away if anyone ever does evaluate a copy.
   */
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
