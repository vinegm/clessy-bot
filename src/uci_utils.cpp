#include "uci_utils.hpp"

#include "logger.hpp"

#include <cctype>
#include <sstream>
#include <stdexcept>

static void append_token(std::string &target, const std::string &token) {
  if (!target.empty()) target += ' ';
  target += token;
}

std::vector<std::string> split_string(const std::string &str) {
  std::vector<std::string> tokens;
  std::istringstream iss(str);
  std::string token;

  while (iss >> token) {
    tokens.push_back(token);
  }

  return tokens;
}

std::string square_to_uci(Square square) {
  int file = square % 8;
  int rank = square / 8;

  std::string result;
  result += static_cast<char>('a' + file);
  result += static_cast<char>('1' + rank);

  return result;
}

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

std::string move_to_uci(const Move &move) {
  std::string result = square_to_uci(move.from()) + square_to_uci(move.to());

  if (move.is_promotion()) { result += piece_type_to_char(move.promotion_piece()); }

  return result;
}

std::string moves_to_uci(const MoveList &moves) {
  std::string result;

  for (int i = 0; i < moves.count; i++) {
    append_token(result, move_to_uci(moves.moves[i]));
  }

  return result;
}

char piece_type_to_char(PieceType type) {
  switch (type) {
    case QUEEN: return 'q';
    case ROOK: return 'r';
    case BISHOP: return 'b';
    case KNIGHT: return 'n';
    default: return 'q';
  }
}

PieceType char_to_piece_type(char c) {
  switch (std::tolower(c)) {
    case 'q': return QUEEN;
    case 'r': return ROOK;
    case 'b': return BISHOP;
    case 'n': return KNIGHT;
    default: return QUEEN;
  }
}

char piece_to_char(Piece piece) {
  constexpr char SYMBOLS[] = " pnbrqk";

  char symbol = SYMBOLS[decode_type(piece)];
  if (decode_color(piece) == WHITE) symbol = static_cast<char>(std::toupper(symbol));

  return symbol;
}

Move uci_to_move(const std::string &uci) {
  if (uci.length() < 4) { throw std::invalid_argument("Invalid UCI move notation: too short"); }

  Square from = uci_to_square(uci.substr(0, 2));
  Square to = uci_to_square(uci.substr(2, 2));

  // NOTE: text carries no flags, the caller matches it against the legal moves
  if (uci.length() == 5) { return Move(from, to, PROMOTION, char_to_piece_type(uci[4])); }

  return Move(from, to);
}

bool is_command(const std::string &token) {
  return token == "uci" || token == "debug" || token == "isready" || token == "setoption"
         || token == "register" || token == "ucinewgame" || token == "position" || token == "go"
         || token == "stop" || token == "ponderhit" || token == "quit" || token == "exit"
         || token == "d" || token == "nnue" || token == "eval";
}

bool is_go_keyword(const std::string &token) {
  return token == "searchmoves" || token == "ponder" || token == "wtime" || token == "btime"
         || token == "winc" || token == "binc" || token == "movestogo" || token == "depth"
         || token == "nodes" || token == "mate" || token == "movetime" || token == "infinite"
         || token == "perft";
}

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

bool parse_position_fen(const std::vector<std::string> &tokens, std::string &fen, size_t &end) {
  int fields = 0;
  for (end = 2; end < tokens.size() && tokens[end] != "moves"; end++) {
    append_token(fen, tokens[end]);
    fields++;
  }

  // NOTE: the clocks are optional in practice
  if (fields >= 4) return true;

  Logger::warn("position: FEN needs at least 4 fields, got ", fields);
  return false;
}

void parse_go_limits(
    const std::vector<std::string> &tokens,
    SearchLimits &limits,
    bool &ponder,
    std::vector<std::string> &searchmoves
) {
  for (size_t i = 1; i < tokens.size(); i++) {
    const std::string &token = tokens[i];

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
        searchmoves.push_back(tokens[i]);
      }

      // NOTE: step back so the loop lands on the keyword that ended the list
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

std::string score_to_uci(int score) {
  if (!is_mate_score(score)) return "cp " + std::to_string(score);

  int mate_in = mate_distance_moves(score);
  if (score < 0) mate_in = -mate_in;

  return "mate " + std::to_string(mate_in);
}

std::string format_info_line(const SearchResult &result, int multipv, int hashfull) {
  std::string info = Logger::format("info depth ", result.depth, " seldepth ", result.seldepth);

  // NOTE: GUIs that never set MultiPV don't expect the field
  if (multipv > 1) info += Logger::format(" multipv ", result.multipv);

  info += Logger::format(
      " score ",
      score_to_uci(result.score),
      " nodes ",
      result.nodes,
      " time ",
      result.elapsed_ms
  );

  if (result.elapsed_ms > 0) {
    int64_t nps = result.nodes * 1000 / result.elapsed_ms;
    if (nps > 0) info += Logger::format(" nps ", nps);
  }

  info += Logger::format(" hashfull ", hashfull);

  info += " pv";
  if (result.pv.empty()) return info + " " + move_to_uci(result.best_move);

  for (const Move &move : result.pv) {
    info += " " + move_to_uci(move);
  }

  return info;
}

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
