#pragma once

#include "engine.hpp"
#include "search.hpp"

#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class Uci {
public:
  ~Uci();

  void run();

private:
  static constexpr int HASH_MIN_MB = 1;
  static constexpr int HASH_MAX_MB = 4096;
  static constexpr int MULTIPV_MAX = 256;
  static constexpr int MOVE_OVERHEAD_MAX = 5000;

  Engine engine;

  // NOTE: only read while a search is being started
  int multipv = 1;
  int64_t move_overhead = 10;

  std::thread search_thread;
  SearchControl control;

  std::mutex search_mutex;

  // NOTE: notified on stop and ponderhit
  std::condition_variable search_release;

  bool quitting = false;

  void call_command(const std::vector<std::string> &leading_tokens);

  // Commands
  void handle_uci();
  void handle_isready();
  void handle_debug(const std::vector<std::string> &tokens);
  void handle_register();
  void handle_setoption(const std::vector<std::string> &tokens);
  void handle_ucinewgame();
  void handle_position(const std::vector<std::string> &tokens);
  void handle_go(const std::vector<std::string> &tokens);
  void handle_perft(const std::vector<std::string> &tokens);
  void handle_stop();
  void handle_ponderhit();
  void handle_d();
  void handle_nnue(const std::vector<std::string> &tokens);
  void handle_eval();

  void apply_option(const std::string &name, const std::string &value);
  bool load_network(const std::string &path);

  void start_search(const SearchLimits &limits, bool ponder);
  void stop_search();
  void send_iteration_info(const SearchResult &result);
  void search_worker(const SearchLimits &limits);
  void await_release(bool was_pondering);
  bool resolve_move(const std::string &uci, Move &move) const;
  void resolve_searchmoves(const std::vector<std::string> &uci_moves, SearchLimits &limits) const;
  void process_moves(const std::vector<std::string> &tokens, size_t start_index);
};
