#include "uci.hpp"

#include "engine.hpp"
#include "eval.hpp"
#include "logger.hpp"
#include "nnue.hpp"
#include "tt.hpp"
#include "uci_utils.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <sys/types.h>

ClessUCI::~ClessUCI() { stop_search(); }

void ClessUCI::run() {
  std::string line;

  while (!quitting && std::getline(std::cin, line)) {
    std::vector<std::string> tokens = split_string(line);

    if (tokens.empty()) continue;

    // Keep the engine alive even if a malformed
    // or unhandled command is sent.
    try {
      call_command(tokens);
    } catch (const std::exception &error) {
      Logger::error("could not handle '", line, "': ", error.what());
    }
  }

  stop_search();
}

void ClessUCI::call_command(const std::vector<std::string> &leading_tokens) {
  size_t start = 0;
  while (start < leading_tokens.size() && !is_command(leading_tokens[start])) {
    start++;
  }

  if (start == leading_tokens.size()) {
    return Logger::warn("unknown command: '", leading_tokens[0], "'");
  }

  std::vector<std::string> tokens(leading_tokens.begin() + start, leading_tokens.end());
  const std::string &command = tokens[0];

  // Commands that answer without touching the position, so they may run
  // alongside a search.
  if (command == "uci") return handle_uci();
  if (command == "isready") return handle_isready();
  if (command == "stop") return handle_stop();
  if (command == "ponderhit") return handle_ponderhit();
  if (command == "debug") return handle_debug(tokens);
  if (command == "register") return handle_register();

  // Everything below reads or mutates the position, so it stops the search
  // first.
  if (command == "setoption") return handle_setoption(tokens);
  if (command == "ucinewgame") return handle_ucinewgame();
  if (command == "position") return handle_position(tokens);
  if (command == "go") return handle_go(tokens);
  if (command == "nnue") return handle_nnue(tokens);
  if (command == "d") return handle_d();
  if (command == "eval") return handle_eval();
  if (command == "quit" || command == "exit") {
    quitting = true;
    return;
  }
}

void ClessUCI::handle_uci() {
  Logger::respond("id name Clessy");
  Logger::respond("id author vinegm");

  Logger::respond(
      "option name Hash type spin default ",
      TranspositionTable::DEFAULT_MB,
      " min ",
      HASH_MIN_MB,
      " max ",
      HASH_MAX_MB
  );
  Logger::respond("option name Clear Hash type button");
  Logger::respond("option name Ponder type check default false");
  Logger::respond("option name MultiPV type spin default 1 min 1 max ", MULTIPV_MAX);
  Logger::respond("option name Move Overhead type spin default 10 min 0 max ", MOVE_OVERHEAD_MAX);
  Logger::respond("option name EvalFile type string default <empty>");

  Logger::respond("uciok");
}

void ClessUCI::handle_isready() { Logger::respond("readyok"); }

void ClessUCI::handle_debug(const std::vector<std::string> &tokens) {
  if (tokens.size() < 2) {
    // "debug" without an argument toggles the level
    const bool debug = Logger::get_level() == Logger::Level::Debug;

    Logger::set_level(debug ? Logger::Level::Info : Logger::Level::Debug);
    Logger::info("debug mode ", debug ? "off" : "on");

    return;
  }

  if (tokens[1] != "off") {
    Logger::set_level(Logger::Level::Debug);
    return;
  }

  Logger::set_level(Logger::Level::Info);
}

void ClessUCI::handle_register() {
  // Nothing here is copy protected, so "registration" is never advertised and
  // there is nothing to check. The spec still allows the GUI to send this.
}

void ClessUCI::handle_setoption(const std::vector<std::string> &tokens) {
  std::string name;
  std::string value;
  if (!parse_setoption(tokens, name, value)) return;

  // Resizing the table or swapping the network under a running search would
  // pull the ground out from under it.
  stop_search();

  apply_option(name, value);
}

void ClessUCI::handle_ucinewgame() {
  stop_search();

  engine.set_fen(INITIAL_POSITION_FEN);

  TranspositionTable::clear();
}

void ClessUCI::handle_position(const std::vector<std::string> &tokens) {
  if (tokens.size() < 2) return Logger::warn("position: requires at least one argument");

  stop_search();

  const std::string &position_type = tokens[1];

  if (position_type == "startpos") {
    engine.set_fen(INITIAL_POSITION_FEN);

    if (tokens.size() > 2 && tokens[2] == "moves") { process_moves(tokens, 3); }

    return;
  }

  if (position_type != "fen") {
    return Logger::warn("position: unknown type '", position_type, "'");
  }

  std::string fen;
  size_t end = 0;
  if (!parse_position_fen(tokens, fen, end)) return;

  engine.set_fen(fen);

  if (end < tokens.size() && tokens[end] == "moves") process_moves(tokens, end + 1);
}

void ClessUCI::handle_go(const std::vector<std::string> &tokens) {
  // The GUI is not supposed to start a search during one, but if it does the
  // old search has to go before anything touches the position.
  stop_search();

  if (tokens.size() > 1 && tokens[1] == "perft") return handle_perft(tokens);

  if (engine.get_legal_moves().empty()) return Logger::respond("bestmove (none)");

  SearchLimits limits;
  limits.multipv = multipv;
  limits.move_overhead = move_overhead;

  bool ponder = false;
  std::vector<std::string> searchmoves;
  parse_go_limits(tokens, limits, ponder, searchmoves);
  resolve_searchmoves(searchmoves, limits);

  // A bare "go" bounds nothing, which the spec defines as searching until
  // "stop". A ponder search is already open-ended in the same way.
  bool bounded = limits.has_clock() || limits.nodes >= 0 || limits.mate > 0
                 || limits.depth != MAX_SEARCH_DEPTH;
  if (!bounded && !ponder) limits.infinite = true;

  start_search(limits, ponder);
}

void ClessUCI::handle_perft(const std::vector<std::string> &tokens) {
  if (tokens.size() < 3) return Logger::error("go perft: requires a depth argument");

  int depth = std::stoi(tokens[2]);
  auto results = engine.perft_divide(depth);

  unsigned long total_nodes = 0;
  for (const auto &[move, nodes] : results) {
    Logger::respond(move_to_uci(move), ": ", nodes);
    total_nodes += nodes;
  }

  Logger::respond("\nNodes searched: ", total_nodes);
}

void ClessUCI::handle_stop() {
  {
    std::lock_guard<std::mutex> lock(search_mutex);
    control.stop.store(true, std::memory_order_relaxed);
    control.pondering.store(false, std::memory_order_relaxed);
  }

  search_release.notify_all();
}

void ClessUCI::handle_ponderhit() {
  {
    std::lock_guard<std::mutex> lock(search_mutex);
    control.pondering.store(false, std::memory_order_relaxed);
  }

  search_release.notify_all();
}

void ClessUCI::handle_d() {
  stop_search();

  Piece squares[64];
  for (int square = 0; square < 64; square++) {
    squares[square] = engine.get_piece_at(static_cast<Square>(square));
  }

  Logger::respond(format_board(squares));
  Logger::respond("");

  Logger::respond("Fen: ", engine.get_fen());

  char hash_str[19];
  snprintf(hash_str, sizeof(hash_str), "0x%016lx", engine.get_hash());
  Logger::respond("Hash: ", hash_str);

  Logger::respond("Side to move: ", engine.to_move() == WHITE ? "white" : "black");
  Logger::respond("In check: ", engine.in_check() ? "yes" : "no");

  MoveList legal_moves = engine.get_legal_moves();
  Logger::respond("Legal moves (", legal_moves.count, "): ", moves_to_uci(legal_moves));
}

void ClessUCI::handle_nnue(const std::vector<std::string> &tokens) {
  if (tokens.size() < 2) return Logger::error("nnue: requires a file path");

  stop_search();

  if (!load_network(tokens[1])) return;

  Logger::respond("Network loaded: ", tokens[1]);
}

void ClessUCI::handle_eval() {
  stop_search();

  Logger::respond("Eval (stm, cp, ", Eval::source_name(), "): ", engine.evaluate());
}

void ClessUCI::apply_option(const std::string &name, const std::string &value) {
  std::string key = name;
  std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
    return std::tolower(c);
  });

  if (key == "hash") {
    TranspositionTable::resize(std::clamp(std::stoi(value), HASH_MIN_MB, HASH_MAX_MB));
    return;
  }

  if (key == "clear hash") return TranspositionTable::clear();

  if (key == "multipv") {
    multipv = std::clamp(std::stoi(value), 1, MULTIPV_MAX);
    return;
  }

  if (key == "move overhead") {
    move_overhead = std::clamp<int64_t>(std::stoll(value), 0, MOVE_OVERHEAD_MAX);
    return;
  }

  if (key == "evalfile") {
    // The advertised default; treat it as "no network" rather than a path.
    if (value.empty() || value == "<empty>") return;

    load_network(value);
    return;
  }

  // Pondering is driven entirely by the GUI sending "go ponder", so there is
  // no engine-side state behind the option.
  if (key == "ponder") return;

  Logger::warn("unknown option: '", name, "'");
}

bool ClessUCI::load_network(const std::string &path) {
  if (!NNUE::load(path)) return false;

  // Stored scores came from the previous evaluation.
  TranspositionTable::clear();
  return true;
}

void ClessUCI::start_search(const SearchLimits &limits, bool ponder) {
  // A finished search still owns a joinable thread, and assigning over one
  // calls std::terminate.
  stop_search();

  control.stop.store(false, std::memory_order_relaxed);
  control.pondering.store(ponder, std::memory_order_relaxed);

  search_thread = std::thread([this, limits] { search_worker(limits); });
}

void ClessUCI::stop_search() {
  if (!search_thread.joinable()) return;

  {
    std::lock_guard<std::mutex> lock(search_mutex);
    control.stop.store(true, std::memory_order_relaxed);
    control.pondering.store(false, std::memory_order_relaxed);
  }
  search_release.notify_all();

  // The lock is released first: the worker needs it to leave await_release.
  search_thread.join();
}

void ClessUCI::send_iteration_info(const SearchResult &result) {
  Logger::respond(format_info_line(result, multipv, TranspositionTable::hashfull()));
}

void ClessUCI::search_worker(SearchLimits limits) {
  bool was_pondering = control.pondering.load(std::memory_order_relaxed);

  SearchInfoCallback report = [this](const SearchResult &line) {
    send_iteration_info(line);
  };
  SearchResult result = engine.search(limits, report, &control);

  // The spec forbids a bestmove before "stop" or "ponderhit" once the GUI has
  // asked for an infinite or ponder search, even when the search ran out of
  // depth on its own first.
  if (was_pondering || limits.infinite) await_release(was_pondering);

  std::string line = Logger::format("bestmove ", move_to_uci(result.best_move));

  // The move the engine expects in reply is what it would ponder on.
  if (result.pv.size() >= 2) line += " ponder " + move_to_uci(result.pv[1]);

  Logger::respond(line);
}

void ClessUCI::await_release(bool was_pondering) {
  std::unique_lock<std::mutex> lock(search_mutex);

  search_release.wait(lock, [this, was_pondering] {
    if (control.stop.load(std::memory_order_relaxed)) return true;

    return was_pondering && !control.pondering.load(std::memory_order_relaxed);
  });
}

bool ClessUCI::resolve_move(const std::string &uci, Move &move) const {
  Move parsed;
  try {
    parsed = uci_to_move(uci);
  } catch (const std::exception &) { return false; }

  // The engine's Move carries the flags that make_move needs, and only the
  // generator knows them, so the string has to be matched against a real move.
  MoveList legal_moves = engine.get_legal_moves();
  for (int i = 0; i < legal_moves.count; i++) {
    if (legal_moves.moves[i].to != parsed.to) continue;
    if (legal_moves.moves[i].from != parsed.from) continue;
    if (legal_moves.moves[i].promotion_piece != parsed.promotion_piece) continue;

    move = legal_moves.moves[i];
    return true;
  }

  return false;
}

void ClessUCI::resolve_searchmoves(
    const std::vector<std::string> &uci_moves,
    SearchLimits &limits
) const {
  for (const std::string &uci : uci_moves) {
    Move move;
    if (resolve_move(uci, move)) {
      limits.searchmoves.push_back(move);
      continue;
    }

    Logger::warn("go searchmoves: '", uci, "' is not legal here, ignoring it");
  }
}

void ClessUCI::process_moves(const std::vector<std::string> &tokens, size_t start_index) {
  for (size_t i = start_index; i < tokens.size(); ++i) {
    Move move;

    // Stopping here leaves the position half-applied, which is impossible to
    // diagnose from the other end unless we say so.
    if (!resolve_move(tokens[i], move)) {
      Logger::warn("position: illegal move '", tokens[i], "', ignoring the rest of the move list");
      return;
    }

    engine.make_move(move);
  }
}
