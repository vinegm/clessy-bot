#include "uci.hpp"

#include "engine.hpp"
#include "logger.hpp"
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

    // A bridge like lichess-bot counts a dead engine as a lost game, so bad
    // input has to be survivable.
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
      TT::DEFAULT_MB,
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
  // Spec says "debug [ on | off ]" with no default, but GUIs do send a bare
  // "debug". Treat that as a request to turn it on.
  bool on = tokens.size() < 2 || tokens[1] != "off";
  Logger::set_level(on ? Logger::Level::Debug : Logger::Level::Info);
}

void ClessUCI::handle_register() {
  // Nothing here is copy protected, so "registration" is never advertised and
  // there is nothing to check. The spec still allows the GUI to send this.
}

void ClessUCI::handle_setoption(const std::vector<std::string> &tokens) {
  size_t index = 1;
  if (index >= tokens.size() || tokens[index] != "name") {
    return Logger::warn("setoption: expected 'name'");
  }

  // Both the name and the value can be several words, so they run until the
  // next keyword rather than taking one token each.
  std::string name;
  for (index++; index < tokens.size() && tokens[index] != "value"; index++) {
    if (!name.empty()) name += " ";
    name += tokens[index];
  }

  std::string value;
  if (index < tokens.size() && tokens[index] == "value") {
    for (index++; index < tokens.size(); index++) {
      if (!value.empty()) value += " ";
      value += tokens[index];
    }
  }

  // Resizing the table or swapping the network under a running search would
  // pull the ground out from under it.
  stop_search();

  apply_option(name, value);
}

void ClessUCI::apply_option(const std::string &name, const std::string &value) {
  std::string key = name;
  std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
    return std::tolower(c);
  });

  if (key == "hash") {
    TT::resize(std::clamp(std::stoi(value), HASH_MIN_MB, HASH_MAX_MB));
    return;
  }

  if (key == "clear hash") return TT::clear();

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

void ClessUCI::handle_ucinewgame() {
  stop_search();

  engine.set_fen(INITIAL_POSITION_FEN);

  // Whatever is stored describes a different game.
  TT::clear();
}

void ClessUCI::handle_position(const std::vector<std::string> &tokens) {
  if (tokens.size() < 2) return Logger::warn("position: requires at least one argument");

  stop_search();

  std::string position_type = tokens[1];

  if (position_type == "startpos") {
    engine.set_fen(INITIAL_POSITION_FEN);

    if (tokens.size() > 2 && tokens[2] == "moves") { process_moves(tokens, 3); }

    return;
  }

  if (position_type == "fen") {
    size_t index = 2;
    std::string fen;
    int fields = 0;

    for (; index < tokens.size() && tokens[index] != "moves"; index++) {
      if (!fen.empty()) fen += " ";
      fen += tokens[index];
      fields++;
    }

    // The clocks are optional in practice, so anything up to the castling and
    // en passant fields is enough to place the pieces.
    if (fields < 4) {
      Logger::warn("position: FEN needs at least 4 fields, got ", fields);
      return;
    }

    engine.set_fen(fen);

    if (index < tokens.size() && tokens[index] == "moves") process_moves(tokens, index + 1);
    return;
  }

  Logger::warn("position: unknown type '", position_type, "'");
}

void ClessUCI::handle_perft(const std::vector<std::string> &tokens) {
  if (tokens.size() < 3) return Logger::error("go perft: requires a depth argument");

  int depth = std::stoi(tokens[2]);
  if (tokens.size() > 3 && tokens[3] == "threads") {
    uint64_t total_nodes = engine.perft_parallel(depth, pool);
    Logger::respond("Nodes searched: ", total_nodes);
    return;
  }

  auto results = engine.perft_divide(depth);

  unsigned long total_nodes = 0;
  for (const auto &[move, nodes] : results) {
    Logger::respond(move_to_uci(move), ": ", nodes);
    total_nodes += nodes;
  }

  Logger::respond("\nNodes searched: ", total_nodes);
}

void ClessUCI::parse_go_limits(
    const std::vector<std::string> &tokens,
    SearchLimits &limits,
    bool &ponder
) {
  for (size_t i = 1; i < tokens.size(); i++) {
    const std::string &token = tokens[i];

    // The two limits that take no argument, so either can be the last token.
    if (token == "infinite") {
      limits.infinite = true;
      continue;
    }
    if (token == "ponder") {
      ponder = true;
      continue;
    }

    if (token == "searchmoves") {
      for (i++; i < tokens.size() && !is_go_keyword(tokens[i]); i++) {
        Move move;
        if (resolve_move(tokens[i], move)) {
          limits.searchmoves.push_back(move);
          continue;
        }

        Logger::warn("go searchmoves: '", tokens[i], "' is not legal here, ignoring it");
      }

      // The loop's own increment must not step past the keyword we stopped on.
      i--;
      continue;
    }

    if (i + 1 >= tokens.size()) continue;

    if (token == "depth") limits.depth = std::stoi(tokens[i + 1]);
    if (token == "nodes") limits.nodes = std::stoll(tokens[i + 1]);
    if (token == "mate") limits.mate = std::stoi(tokens[i + 1]);
    if (token == "movetime") limits.movetime = std::stoll(tokens[i + 1]);
    if (token == "movestogo") limits.movestogo = std::stoi(tokens[i + 1]);
    if (token == "wtime") limits.wtime = std::stoll(tokens[i + 1]);
    if (token == "btime") limits.btime = std::stoll(tokens[i + 1]);
    if (token == "winc") limits.winc = std::stoll(tokens[i + 1]);
    if (token == "binc") limits.binc = std::stoll(tokens[i + 1]);
  }
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
  parse_go_limits(tokens, limits, ponder);

  // A bare "go" bounds nothing, which the spec defines as searching until
  // "stop". A ponder search is already open-ended in the same way.
  bool bounded = limits.has_clock() || limits.nodes >= 0 || limits.mate > 0
                 || limits.depth != MAX_SEARCH_DEPTH;
  if (!bounded && !ponder) limits.infinite = true;

  start_search(limits, ponder);
}

void ClessUCI::start_search(const SearchLimits &limits, bool ponder) {
  // A finished search still owns a joinable thread, and assigning over one
  // calls std::terminate.
  stop_search();

  control.stop.store(false, std::memory_order_relaxed);
  control.pondering.store(ponder, std::memory_order_relaxed);

  search_thread = std::thread([this, limits] { search_worker(limits); });
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

void ClessUCI::handle_stop() {
  // Written under the mutex, not just atomically: await_release can otherwise
  // miss the flag between testing its predicate and going to sleep, and the
  // bestmove would never come out.
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
  int64_t nps = (result.elapsed_ms > 0) ? result.nodes * 1000 / result.elapsed_ms : 0;

  std::string info = Logger::format("info depth ", result.depth, " seldepth ", result.seldepth);

  // Only meaningful when more than one line is asked for, and GUIs that never
  // set MultiPV do not expect the field.
  if (multipv > 1) info += Logger::format(" multipv ", result.multipv);

  info += Logger::format(
      " score ",
      score_to_uci(result.score),
      " nodes ",
      result.nodes,
      " time ",
      result.elapsed_ms
  );

  if (nps > 0) info += Logger::format(" nps ", nps);
  info += Logger::format(" hashfull ", TT::hashfull());

  info += " pv";
  if (result.pv.empty()) {
    info += " " + move_to_uci(result.best_move);
  } else {
    for (const Move &move : result.pv) {
      info += " " + move_to_uci(move);
    }
  }

  Logger::respond(info);
}

std::string ClessUCI::score_to_uci(int score) {
  if (!is_mate_score(score)) return "cp " + std::to_string(score);

  int mate_in = mate_distance_moves(score);
  return "mate " + std::to_string(score > 0 ? mate_in : -mate_in);
}

void ClessUCI::handle_d() {
  stop_search();

  // Ranks print from 8 down to 1 so the board reads the way a board looks,
  // which is the opposite of the square numbering.
  constexpr const char *RULE = " +---+---+---+---+---+---+---+---+";

  for (int rank = 7; rank >= 0; rank--) {
    Logger::respond(RULE);

    std::string row = " |";
    for (int file = 0; file < 8; file++) {
      row += ' ';
      row += piece_to_char(engine.get_piece_at(indexes_to_square(rank, file)));
      row += " |";
    }

    row += ' ';
    row += static_cast<char>('1' + rank);
    Logger::respond(row);
  }

  Logger::respond(RULE);
  Logger::respond("   a   b   c   d   e   f   g   h");
  Logger::respond("");

  Logger::respond("Fen: ", engine.get_fen());

  char hash_str[19];
  snprintf(hash_str, sizeof(hash_str), "0x%016lx", engine.get_hash());
  Logger::respond("Hash: ", hash_str);

  Logger::respond("Side to move: ", engine.to_move() == WHITE ? "white" : "black");
  Logger::respond("In check: ", engine.in_check() ? "yes" : "no");

  std::string moves_str;
  MoveList legal_moves = engine.get_legal_moves();
  for (int i = 0; i < legal_moves.count; i++) {
    if (!moves_str.empty()) moves_str += " ";
    moves_str += move_to_uci(legal_moves.moves[i]);
  }

  Logger::respond("Legal moves (", legal_moves.count, "): ", moves_str);
}

bool ClessUCI::load_network(const std::string &path) {
  if (!NNUE::load(path)) return false;

  // Stored scores came from the previous evaluation.
  TT::clear();
  return true;
}

void ClessUCI::handle_nnue(const std::vector<std::string> &tokens) {
  if (tokens.size() < 2) return Logger::error("nnue: requires a file path");

  stop_search();

  if (!load_network(tokens[1])) return;

  Logger::respond("Network loaded: ", tokens[1]);
}

void ClessUCI::handle_eval() {
  stop_search();

  std::string kind = NNUE::is_loaded() ? "nnue" : "hce";
  Logger::respond("Eval (stm, cp, ", kind, "): ", engine.evaluate());
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

char ClessUCI::piece_to_char(Piece piece) {
  constexpr char SYMBOLS[] = " pnbrqk";

  char symbol = SYMBOLS[decode_type(piece)];
  if (decode_color(piece) == WHITE) symbol = static_cast<char>(std::toupper(symbol));

  return symbol;
}

bool ClessUCI::is_go_keyword(const std::string &token) {
  return token == "searchmoves" || token == "ponder" || token == "wtime" || token == "btime"
         || token == "winc" || token == "binc" || token == "movestogo" || token == "depth"
         || token == "nodes" || token == "mate" || token == "movetime" || token == "infinite"
         || token == "perft";
}

bool ClessUCI::is_command(const std::string &token) {
  return token == "uci" || token == "debug" || token == "isready" || token == "setoption"
         || token == "register" || token == "ucinewgame" || token == "position" || token == "go"
         || token == "stop" || token == "ponderhit" || token == "quit" || token == "exit"
         || token == "d" || token == "nnue" || token == "eval";
}
