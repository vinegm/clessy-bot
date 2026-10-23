#include "engine.hpp"

#include "chess_types.hpp"
#include "position.hpp"

#include <cstdint>
#include <vector>

MoveList Engine::legal_moves() const {
  if (legal_cache_valid) return legal_move_cache;

  legal_move_cache = MoveGenerator::generate_legal_moves(pos);
  legal_cache_valid = true;

  return legal_move_cache;
}

void Engine::make_move(const Move &move) {
  legal_cache_valid = false;
  pos.make_move(move);
}

void Engine::undo_move() {
  legal_cache_valid = false;
  pos.undo_move();
}

std::vector<std::pair<Move, uint64_t>> Engine::perft_divide(int depth) {
  std::vector<std::pair<Move, uint64_t>> results;
  if (depth == 0) return results;

  for (const Move &move : legal_moves()) {
    pos.make_move(move);
    results.emplace_back(move, perft(pos, depth - 1));
    pos.undo_move();
  }

  return results;
}

uint64_t Engine::perft(Position &position, int depth) {
  if (depth == 0) return 1;

  MoveList moves = MoveGenerator::generate_legal_moves(position);
  if (depth == 1) return moves.count;

  uint64_t nodes = 0;
  for (const Move &move : moves) {
    position.make_move(move);
    nodes += perft(position, depth - 1);
    position.undo_move();
  }

  return nodes;
}
