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
  Piece get_piece_at(Square square);
  void make_move(const Move &move);
  void undo_move();

  /**
   * Pass the turn without moving a piece, for null move pruning.
   *
   * Pushed onto the same undo stack as a real move and reversed by the same
   * undo_move, which recognises the null move and skips the piece work. The
   * en passant square goes with it: the chance to capture does not survive
   * the opponent declining to move.
   *
   * Illegal when in check, and unsound in zugzwang; both are the caller's to
   * rule out.
   */
  void make_null_move();

  // Whether this side has a piece other than pawns and the king. Null move
  // pruning needs it, since a side down to pawns is where zugzwang lives.
  bool has_non_pawn_material(Color color) const {
    return (occupancy[color] & ~get_bb(color, PAWN) & ~get_bb(color, KING)) != 0;
  }

  /**
   * Whether the current position already occurred in the make_move
   * history (twofold), looking back at most halfmove_clock plies.
   */
  bool is_repetition() const;

  /**
   * The NNUE feature transformer's state for this position.
   *
   * Mutable and handed out from a const position because refreshing it is a
   * cache fill, not a change of position — the same reason ClessEngine's
   * legal move cache is mutable. Whoever evaluates owns deciding whether the
   * contents are current; Position only keeps them in step with the board.
   */
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

  // The hash contribution of an en passant square, if there is one.
  static uint64_t en_passant_key(const std::optional<Square> &square);
};
