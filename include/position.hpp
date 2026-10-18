#pragma once

#include "chess_types.hpp"
#include "nnue.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
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

    if (color == WHITE) return code - 1;

    return code - 3;
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
  static const std::array<CastlingRights, 64> CASTLING_MASKS;
  static constexpr std::array<CastlingRights, 64> init_castling_masks();

  // NOTE: a copy never wants the accumulator, so it starts out unbuilt
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

  std::string en_passant_fen() const;

  static std::pair<Square, Square> castling_rook_squares(Square king_from, Square king_to);
};

constexpr std::array<CastlingRights, 64> Position::init_castling_masks() {
  std::array<CastlingRights, 64> castling_masks{};

  for (int square = 0; square < 64; square++) {
    castling_masks[square] = ANY_CASTLING;

    switch (square) {
      case E1: castling_masks[square] &= ~(W_CASTLE_KING | W_CASTLE_QUEEN); break;
      case H1: castling_masks[square] &= ~W_CASTLE_KING; break;
      case A1: castling_masks[square] &= ~W_CASTLE_QUEEN; break;
      case E8: castling_masks[square] &= ~(B_CASTLE_KING | B_CASTLE_QUEEN); break;
      case H8: castling_masks[square] &= ~B_CASTLE_KING; break;
      case A8: castling_masks[square] &= ~B_CASTLE_QUEEN; break;
      default: break;
    }
  }

  return castling_masks;
}

inline constexpr std::array<CastlingRights, 64> Position::CASTLING_MASKS =
    Position::init_castling_masks();
