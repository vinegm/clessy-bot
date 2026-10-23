#pragma once

#include "chess_types.hpp"
#include "eval.hpp"
#include "move_gen.hpp"
#include "position.hpp"
#include "search.hpp"

#include <string>

class Engine {
public:
  Engine() : pos(INITIAL_POSITION_FEN) {}

  std::string fen() const { return pos.fen(); }
  void set_fen(const std::string &fen) {
    legal_cache_valid = false;
    pos.set_fen(fen);
  }

  Color to_move() const { return pos.to_move; }
  uint64_t hash() const { return pos.hash; }
  Piece piece_at(Square square) { return pos.piece_at(square); }

  bool in_check() const { return MoveGenerator::is_in_check(pos, pos.to_move); }
  int evaluate() const { return Eval::evaluate(pos); }

  SearchResult search(
      const SearchLimits &limits,
      const SearchInfoCallback &on_iteration = nullptr,
      SearchControl *control = nullptr
  ) {
    return Search::run(pos, limits, on_iteration, control);
  }

  MoveList legal_moves() const;
  void make_move(const Move &move);
  void undo_move();
  std::vector<std::pair<Move, uint64_t>> perft_divide(int depth);

private:
  Position pos;

  static uint64_t perft(Position &position, int depth);

  mutable bool legal_cache_valid = false;
  mutable MoveList legal_move_cache;
};
