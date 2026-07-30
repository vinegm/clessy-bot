#include "uci_utils.hpp"

#include "logger.hpp"

#include <cctype>
#include <sstream>
#include <stdexcept>

// Names and values in a UCI line are space separated and may be several words.
void append_token(std::string &target, const std::string &token) {
  if (!target.empty()) target += " ";
  target += token;
}

// Split a string into tokens separated by whitespace.
std::vector<std::string> split_string(const std::string &str) {
  std::vector<std::string> tokens;
  std::istringstream iss(str);
  std::string token;

  while (iss >> token) {
    tokens.push_back(token);
  }

  return tokens;
}

// Convert a square index (0-63) to UCI notation (e.g., "e4").
std::string square_to_uci(Square square) {
  int file = square % 8;
  int rank = square / 8;

  std::string result;
  result += ('a' + file);
  result += ('1' + rank);

  return result;
}

// Convert UCI notation (e.g., "e4") to a square index (0-63).
Square uci_to_square(const std::string &uci) {
  if (uci.length() < 2) { throw std::invalid_argument("Invalid UCI square notation"); }

  char file_char = std::tolower(uci[0]);
  char rank_char = uci[1];

  if (file_char < 'a' || file_char > 'h' || rank_char < '1' || rank_char > '8') {
    throw std::invalid_argument("Invalid UCI square notation");
  }

  int file = file_char - 'a';
  int rank = rank_char - '1';

  return static_cast<Square>(rank * 8 + file);
}

// Convert a Move to UCI notation (e.g., "e2e4" or "e7e8q" for promotion).
std::string move_to_uci(const Move &move) {
  std::string result = square_to_uci(move.from) + square_to_uci(move.to);

  if (move.is_promotion()) { result += piece_type_to_char(move.promotion_piece); }

  return result;
}

// Convert a list of Moves to UCI notation, separated by spaces.
std::string moves_to_uci(const MoveList &moves) {
  std::string result;

  for (int i = 0; i < moves.count; i++) {
    append_token(result, move_to_uci(moves.moves[i]));
  }

  return result;
}

// Convert a PieceType to a character for UCI notation (e.g., 'q' for QUEEN).
char piece_type_to_char(PieceType type) {
  switch (type) {
    case QUEEN: return 'q';
    case ROOK: return 'r';
    case BISHOP: return 'b';
    case KNIGHT: return 'n';
    default: return 'q';
  }
}

// Convert a character to a PieceType for UCI notation (e.g., 'q' to QUEEN).
PieceType char_to_piece_type(char c) {
  switch (std::tolower(c)) {
    case 'q': return QUEEN;
    case 'r': return ROOK;
    case 'b': return BISHOP;
    case 'n': return KNIGHT;
    default: return QUEEN;
  }
}

// Convert a Piece to a character for UCI notation (e.g., 'Q' for WHITE QUEEN).
char piece_to_char(Piece piece) {
  constexpr char SYMBOLS[] = " pnbrqk";

  char symbol = SYMBOLS[decode_type(piece)];
  if (decode_color(piece) == WHITE) symbol = static_cast<char>(std::toupper(symbol));

  return symbol;
}

// Convert UCI notation (e.g., "e2e4" or "e7e8q") to a Move.
Move uci_to_move(const std::string &uci) {
  if (uci.length() < 4) { throw std::invalid_argument("Invalid UCI move notation: too short"); }

  Square from = uci_to_square(uci.substr(0, 2));
  Square to = uci_to_square(uci.substr(2, 2));

  Move move;
  move.from = from;
  move.to = to;
  move.type = NORMAL_MOVE;
  move.promotion_piece = NO_TYPE;

  // Check for promotion
  if (uci.length() == 5) {
    move.type = static_cast<MoveType>(move.type | PROMOTION);
    move.promotion_piece = char_to_piece_type(uci[4]);
  }

  return move;
}

// Check if a token is a recognized UCI command.
bool is_command(const std::string &token) {
  return token == "uci" || token == "debug" || token == "isready" || token == "setoption"
         || token == "register" || token == "ucinewgame" || token == "position" || token == "go"
         || token == "stop" || token == "ponderhit" || token == "quit" || token == "exit"
         || token == "d" || token == "nnue" || token == "eval";
}

// Check if a token is a recognized keyword in the "go" command.
bool is_go_keyword(const std::string &token) {
  return token == "searchmoves" || token == "ponder" || token == "wtime" || token == "btime"
         || token == "winc" || token == "binc" || token == "movestogo" || token == "depth"
         || token == "nodes" || token == "mate" || token == "movetime" || token == "infinite"
         || token == "perft";
}

// Parse the "setoption" command, extracting the option name and value.
bool parse_setoption(
    const std::vector<std::string> &tokens,
    std::string &name,
    std::string &value
) {
  if (tokens.size() < 2 || tokens[1] != "name") {
    Logger::warn("setoption: expected 'name'");
    return false;
  }

  size_t index = 2;
  for (; index < tokens.size() && tokens[index] != "value"; index++) {
    append_token(name, tokens[index]);
  }

  for (index++; index < tokens.size(); index++) {
    append_token(value, tokens[index]);
  }

  return true;
}

// Parse the "position" command, extracting the FEN string and the index of the "moves" token.
bool parse_position_fen(const std::vector<std::string> &tokens, std::string &fen, size_t &end) {
  int fields = 0;
  for (end = 2; end < tokens.size() && tokens[end] != "moves"; end++) {
    append_token(fen, tokens[end]);
    fields++;
  }

  // The clocks are optional for a UCI engine,
  // the FEN string is valid without them.
  if (fields >= 4) return true;

  Logger::warn("position: FEN needs at least 4 fields, got ", fields);
  return false;
}

// Parse the "go" command, extracting search limits, ponder mode, and search moves.
void parse_go_limits(
    const std::vector<std::string> &tokens,
    SearchLimits &limits,
    bool &ponder,
    std::vector<std::string> &searchmoves
) {
  for (size_t i = 1; i < tokens.size(); i++) {
    const std::string &token = tokens[i];

    // Takes no arguments, so we can just set the flag and continue.
    if (token == "infinite") {
      limits.infinite = true;
      continue;
    }

    // Takes no arguments, so we can just set the flag and continue.
    if (token == "ponder") {
      ponder = true;
      continue;
    }

    if (token == "searchmoves") {
      for (i++; i < tokens.size() && !is_go_keyword(tokens[i]); i++) {
        searchmoves.push_back(tokens[i]);
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

// Convert a score to UCI format, handling both centipawn and mate scores.
std::string score_to_uci(int score) {
  if (!is_mate_score(score)) return "cp " + std::to_string(score);

  int mate_in = mate_distance_moves(score);
  return "mate " + std::to_string(score > 0 ? mate_in : -mate_in);
}

// Format a search result into a UCI "info" line, including depth, score, nodes, time, and principal
// variation.
std::string format_info_line(const SearchResult &result, int multipv, int hashfull) {
  std::string info = Logger::format("info depth ", result.depth, " seldepth ", result.seldepth);

  // Only meaningful when more than one line is asked for,
  // GUIs that never set MultiPV do not expect the field.
  if (multipv > 1) info += Logger::format(" multipv ", result.multipv);

  info += Logger::format(
      " score ",
      score_to_uci(result.score),
      " nodes ",
      result.nodes,
      " time ",
      result.elapsed_ms
  );

  int64_t nps = (result.elapsed_ms > 0) ? result.nodes * 1000 / result.elapsed_ms : 0;
  if (nps > 0) info += Logger::format(" nps ", nps);
  info += Logger::format(" hashfull ", hashfull);

  info += " pv";
  if (result.pv.empty()) {
    info += " " + move_to_uci(result.best_move);
  } else {
    for (const Move &move : result.pv) {
      info += " " + move_to_uci(move);
    }
  }

  return info;
}

// Format the board into a human-readable string representation, showing ranks, files, and pieces.
std::string format_board(const Piece squares[64]) {
  constexpr const char *RULE = " +---+---+---+---+---+---+---+---+";

  std::string board;
  for (int rank = 7; rank >= 0; rank--) {
    board += RULE;
    board += "\n |";

    for (int file = 0; file < 8; file++) {
      board += ' ';
      board += piece_to_char(squares[indexes_to_square(rank, file)]);
      board += " |";
    }

    board += ' ';
    board += static_cast<char>('1' + rank);
    board += '\n';
  }

  board += RULE;
  board += "\n   a   b   c   d   e   f   g   h";

  return board;
}
