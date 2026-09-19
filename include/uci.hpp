#pragma once

#include "engine.hpp"
#include "search.hpp"

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
  void handle_d();
  void handle_eval();

  void parse_go_limits(const std::vector<std::string> &tokens, SearchLimits &limits);

  void start_search(const SearchLimits &limits);
  void search_worker(SearchLimits limits);

  // Ask the search to stop and wait for it, leaving the engine idle.
  void stop_search();

  static std::string score_to_uci(int score);
  void send_iteration_info(const SearchResult &result);

  static char piece_to_char(Piece piece);
  static bool is_command(const std::string &token);
  bool resolve_move(const std::string &uci, Move &move) const;
  void process_moves(const std::vector<std::string> &tokens, size_t start_index);
};
