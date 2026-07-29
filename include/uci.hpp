#pragma once

#include "engine.hpp"
#include "search.hpp"

#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class ClessUCI {
public:
  ~ClessUCI();

  void run();

private:
  ClessEngine engine;

  std::thread search_thread;
  SearchControl control;

  std::mutex search_mutex;

  // Raised when the GUI releases a held bestmove ("stop" or "ponderhit").
  std::condition_variable search_release;

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
  void handle_perft(const std::vector<std::string> &tokens);
  void handle_stop();
  void handle_ponderhit();
  void handle_d();
  void handle_eval();

  void parse_go_limits(const std::vector<std::string> &tokens, SearchLimits &limits, bool &ponder);

  void start_search(const SearchLimits &limits, bool ponder);
  void search_worker(SearchLimits limits);

  // Ask the search to stop and wait for it, leaving the engine idle.
  void stop_search();

  // Block until the GUI allows a held bestmove out.
  void await_release(bool was_pondering);

  static std::string score_to_uci(int score);
  void send_iteration_info(const SearchResult &result);

  // Board character for a piece: uppercase White, lowercase Black, a
  // space for an empty square, which is the convention the FEN uses.
  static char piece_to_char(Piece piece);

  // Whether a token names a command, so junk before one can be
  // skipped the way the spec asks.
  static bool is_command(const std::string &token);

  // Resolve a UCI move string against the current legal moves, which
  // is the only way to recover the flags the engine's Move carries.
  bool resolve_move(const std::string &uci, Move &move) const;

  void process_moves(const std::vector<std::string> &tokens, size_t start_index);
};
