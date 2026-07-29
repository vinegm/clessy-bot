#include "engine.hpp"

#include "chess_types.hpp"
#include "position.hpp"

#include <cstdint>
#include <sys/types.h>
#include <vector>

MoveList ClessEngine::get_legal_moves() const {
  if (legal_cache_valid) return legal_moves;

  legal_moves = generator.generate_legal_moves(pos);
  legal_cache_valid = true;

  return legal_moves;
};

MoveList ClessEngine::get_legal_moves_from(Square square) const {
  if (!legal_cache_valid) {
    legal_moves = generator.generate_legal_moves(pos);
    legal_cache_valid = true;
  }

  MoveList filtered_moves;
  for (int i = 0; i < legal_moves.count; i++) {
    if (legal_moves.moves[i].from == square) { filtered_moves.add_move(legal_moves.moves[i]); }
  }

  return filtered_moves;
};

void ClessEngine::make_move(const Move &move) {
  legal_cache_valid = false;
  pos.make_move(move);
}

void ClessEngine::undo_move() {
  legal_cache_valid = false;
  pos.undo_move();
}

uint64_t ClessEngine::perft(int depth) {
  if (depth == 0) return 1;

  MoveList moves = get_legal_moves();
  if (depth == 1) return moves.count;

  uint64_t nodes = 0;
  for (int i = 0; i < moves.count; i++) {
    make_move(moves[i]);
    nodes += perft(depth - 1);
    undo_move();
  }

  return nodes;
}

uint64_t ClessEngine::perft_parallel(int depth, ThreadPool &pool) {
  if (depth <= 1) return perft(depth);

  MoveList moves = get_legal_moves();
  std::string fen = get_fen();

  // Root split: every task gets its own engine copy, so the workers never
  // share mutable state.
  std::vector<std::future<uint64_t>> futures;
  for (int i = 0; i < moves.count; i++) {
    futures.push_back(pool.submit([fen, move = moves[i], depth] {
      ClessEngine worker(fen);
      worker.make_move(move);
      return worker.perft(depth - 1);
    }));
  }

  uint64_t nodes = 0;
  for (std::future<uint64_t> &future : futures) {
    nodes += future.get();
  }

  return nodes;
}

std::vector<std::pair<Move, int>> ClessEngine::perft_divide(int depth) {
  std::vector<std::pair<Move, int>> results;

  if (depth == 0) return results;

  MoveList moves = get_legal_moves();

  for (int i = 0; i < moves.count; i++) {
    make_move(moves[i]);
    int nodes = (depth == 1) ? 1 : perft(depth - 1);
    results.emplace_back(moves[i], nodes);
    undo_move();
  }

  return results;
}
