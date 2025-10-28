#include "engine.hpp"

#include "chess_types.hpp"
#include "position.hpp"

#include <algorithm>
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

bool ClessEngine::validate_move(const Move &move) const {
  if (!legal_cache_valid) {
    legal_moves = generator.generate_legal_moves(pos);
    legal_cache_valid = true;
  }

  return std::find(legal_moves.begin(), legal_moves.end(), move) != legal_moves.end();
}

bool ClessEngine::make_move(const Move &move) {
  if (validate_moves && !validate_move(move)) return false;

  legal_cache_valid = false;
  pos.make_move(move);
  return true;
}

void ClessEngine::undo_move() {
  legal_cache_valid = false;
  pos.undo_move();
}

int ClessEngine::perft(int depth) {
  if (depth == 0) return 1;

  MoveList moves = get_legal_moves();
  if (depth == 1) return moves.count;

  int nodes = 0;
  for (int i = 0; i < moves.count; i++) {
    make_move(moves[i]);
    nodes += perft(depth - 1);
    undo_move();
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
