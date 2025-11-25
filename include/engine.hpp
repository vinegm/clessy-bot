#pragma once

#include "chess_types.hpp"
#include "move_gen.hpp"
#include "position.hpp"

#include <string>

class ClessEngine {
public:
  ClessEngine() : pos(INITIAL_POSITION_FEN) {}
  ClessEngine(std::string fen) : pos(fen) {}

  std::string get_fen() const { return pos.get_fen(); }
  void set_fen(const std::string &fen) {
    legal_cache_valid = false;
    pos.set_fen(fen);
  }

  PieceColor to_move() const { return pos.to_move; }
  Piece get_piece_at(Square square) { return pos.get_piece_at(square); }

  MoveList get_legal_moves() const;
  MoveList get_legal_moves_from(Square square) const;

  void make_move(const Move &move);
  void undo_move();
  uint64_t perft(int depth);
  std::vector<std::pair<Move, int>> perft_divide(int depth);

private:
  MoveGenerator generator{};
  Position pos;

  mutable bool legal_cache_valid = false;
  mutable MoveList legal_moves;
};
