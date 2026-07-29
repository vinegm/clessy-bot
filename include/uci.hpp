#pragma once

#include "engine.hpp"

#include <string>
#include <vector>

class ClessUCI {
public:
  void run();

private:
  ClessEngine engine;

  bool quitting = false;

  void call_command(const std::vector<std::string> &leading_tokens);

  // Commands
  void handle_uci();
  void handle_isready();
  void handle_debug(const std::vector<std::string> &tokens);
  void handle_setoption(const std::vector<std::string> &tokens);
  void handle_register();
  void handle_ucinewgame();
  void handle_position(const std::vector<std::string> &tokens);
  void handle_go(const std::vector<std::string> &tokens);
  void handle_d();

  // Whether a token names a command, so junk before one can be
  // skipped the way the spec asks.
  static bool is_command(const std::string &token);

  // Resolve a UCI move string against the current legal moves, which
  // is the only way to recover the flags the engine's Move carries.
  bool resolve_move(const std::string &uci, Move &move) const;

  void process_moves(const std::vector<std::string> &tokens, size_t start_index);
};
