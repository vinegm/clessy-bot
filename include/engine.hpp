#pragma once

#include "chess_types.hpp"
#include "eval.hpp"
#include "move_gen.hpp"
#include "nnue.hpp"
#include "position.hpp"
#include "search.hpp"
#include "thread_pool.hpp"

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

  Color to_move() const { return pos.to_move; }
  uint64_t get_hash() const { return pos.hash; }
  Piece get_piece_at(Square square) { return pos.get_piece_at(square); }

  MoveList get_legal_moves() const;
  MoveList get_legal_moves_from(Square square) const;

  bool in_check() const { return generator.is_in_check(pos, pos.to_move); }

  int evaluate() const {
    if (NNUE::is_loaded()) return NNUE::evaluate(pos);

    return evaluate_hce(pos);
  }

  SearchResult search(
      const SearchLimits &limits,
      const SearchInfoCallback &on_iteration = nullptr,
      SearchControl *control = nullptr
  ) {
    return run_search(pos, limits, on_iteration, control);
  }

  void make_move(const Move &move);
  void undo_move();
  uint64_t perft(int depth);
  uint64_t perft_parallel(int depth, ThreadPool &pool);
  std::vector<std::pair<Move, int>> perft_divide(int depth);

private:
  MoveGenerator generator{};
  Position pos;

  mutable bool legal_cache_valid = false;
  mutable MoveList legal_moves;
};
