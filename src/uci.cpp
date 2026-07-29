#include "uci.hpp"

#include "engine.hpp"
#include "logger.hpp"
#include "uci_utils.hpp"

#include <iostream>
#include <sys/types.h>

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

  if (command == "uci") return handle_uci();
  if (command == "isready") return handle_isready();
  if (command == "debug") return handle_debug(tokens);
  if (command == "register") return handle_register();
  if (command == "setoption") return handle_setoption(tokens);
  if (command == "ucinewgame") return handle_ucinewgame();
  if (command == "position") return handle_position(tokens);
  if (command == "go") return handle_go(tokens);
  if (command == "d") return handle_d();
  if (command == "quit" || command == "exit") {
    quitting = true;
    return;
  }
}

void ClessUCI::handle_uci() {
  Logger::respond("id name Clessy");
  Logger::respond("id author vinegm");
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
  Logger::warn("setoption: no options are advertised yet");
}

void ClessUCI::handle_ucinewgame() { engine.set_fen(INITIAL_POSITION_FEN); }

void ClessUCI::handle_position(const std::vector<std::string> &tokens) {
  if (tokens.size() < 2) return Logger::warn("position: requires at least one argument");

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

void ClessUCI::handle_go(const std::vector<std::string> &tokens) {
  if (tokens.size() > 1 && tokens[1] == "perft") {
    if (tokens.size() < 3) return Logger::error("go perft: requires a depth argument");

    int depth = std::stoi(tokens[2]);

    auto results = engine.perft_divide(depth);

    unsigned long total_nodes = 0;
    for (const auto &[move, nodes] : results) {
      Logger::respond(move_to_uci(move), ": ", nodes);
      total_nodes += nodes;
    }

    Logger::respond("\nNodes searched: ", total_nodes);
    return;
  }

  MoveList legal_moves = engine.get_legal_moves();

  if (legal_moves.empty()) return Logger::respond("bestmove (none)");

  Logger::respond("bestmove ", move_to_uci(legal_moves[0]));
}

void ClessUCI::handle_d() {
  Logger::respond("Fen: ", engine.get_fen());

  std::string moves_str;
  MoveList legal_moves = engine.get_legal_moves();
  for (int i = 0; i < legal_moves.count; i++) {
    if (!moves_str.empty()) moves_str += " ";
    moves_str += move_to_uci(legal_moves.moves[i]);
  }

  Logger::respond("Legal moves (", legal_moves.count, "): ", moves_str);
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

bool ClessUCI::is_command(const std::string &token) {
  return token == "uci" || token == "debug" || token == "isready" || token == "setoption"
         || token == "register" || token == "ucinewgame" || token == "position" || token == "go"
         || token == "quit" || token == "exit" || token == "d";
}
