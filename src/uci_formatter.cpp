#include "uci_formatter.hpp"

#include "logger.hpp"
#include "notation.hpp"

std::string UciFormatter::score_to_uci(int score) {
  if (!Search::is_mate_score(score)) return "cp " + std::to_string(score);

  int mate_in = Search::mate_distance_moves(score);
  if (score < 0) mate_in = -mate_in;

  return "mate " + std::to_string(mate_in);
}

std::string UciFormatter::format_info_line(const SearchResult &result, int multipv, int hashfull) {
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
  if (result.pv.empty()) return info + " " + Notation::move_to_uci(result.best_move);

  for (const Move &move : result.pv) {
    info += " " + Notation::move_to_uci(move);
  }

  return info;
}

std::string UciFormatter::format_board(const Piece squares[64]) {
  constexpr const char *RULE = " +---+---+---+---+---+---+---+---+";

  std::string board;
  for (int rank = 7; rank >= 0; rank--) {
    board += RULE;
    board += "\n |";

    for (int file = 0; file < 8; file++) {
      board += ' ';
      board += Notation::piece_to_char(squares[indexes_to_square(rank, file)]);
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
