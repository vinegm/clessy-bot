#include "datagen.hpp"

#include "logger.hpp"
#include "nnue.hpp"
#include "tt.hpp"

#include <algorithm>
#include <cstdlib>

void ClessDatagen::usage() {
  Logger::respond("usage: clessy datagen [options]");
  Logger::respond("");
  Logger::respond("  --format <binpack|text>   output format (default binpack)");
  Logger::respond("  --out <path>          output file (default data.binpack or data.txt)");
  Logger::respond("  --append              add to an existing output file instead of refusing");
  Logger::respond("  --games <n>           games to play (default 1000)");
  Logger::respond("  --nodes <n>           search nodes per move (default 5000)");
  Logger::respond("  --depth <n>           depth cap per move (default unlimited)");
  Logger::respond("  --random-plies <n>    random moves opening each game (default 8)");
  Logger::respond("  --opening-max-score <cp>  reject openings scoring past this (default 800)");
  Logger::respond("  --adjudicate-score <cp>   call a win at this score (default 2000, 0 off)");
  Logger::respond("  --adjudicate-plies <n>    consecutive plies needed for it (default 4)");
  Logger::respond("  --max-plies <n>       discard games longer than this (default 400)");
  Logger::respond("  --hash <mb>           transposition table size (default 16)");
  Logger::respond("  --eval-file <path>    .nnue network to label with (default hand-crafted)");
  Logger::respond("  --seed <n>            RNG seed (default random)");
}

bool ClessDatagen::parse_options(int argc, char **argv) {
  for (int i = 0; i < argc; i++) {
    std::string flag = argv[i];

    if (flag == "-h" || flag == "--help") return false;
    if (flag == "--append") {
      options.append = true;
      continue;
    }

    if (i + 1 >= argc) {
      Logger::error("datagen: '", flag, "' needs a value");
      return false;
    }
    std::string value = argv[++i];

    if (flag == "--out") {
      options.output = value;
    } else if (flag == "--format") {
      if (value == "binpack") {
        options.format = Format::Binpack;
      } else if (value == "text") {
        options.format = Format::Text;
      } else {
        Logger::error("datagen: unknown format '", value, "'");
        return false;
      }
    } else if (flag == "--eval-file") {
      options.eval_file = value;
    } else if (flag == "--games") {
      options.games = std::stoll(value);
    } else if (flag == "--nodes") {
      options.nodes = std::stoll(value);
    } else if (flag == "--depth") {
      options.depth = std::stoi(value);
    } else if (flag == "--random-plies") {
      options.random_plies = std::stoi(value);
    } else if (flag == "--opening-max-score") {
      options.opening_max_score = std::stoi(value);
    } else if (flag == "--adjudicate-score") {
      options.adjudicate_score = std::stoi(value);
    } else if (flag == "--adjudicate-plies") {
      options.adjudicate_plies = std::stoi(value);
    } else if (flag == "--max-plies") {
      options.max_plies = std::stoi(value);
    } else if (flag == "--hash") {
      options.hash_mb = std::stoul(value);
    } else if (flag == "--seed") {
      options.seed = std::stoull(value);
    } else {
      Logger::error("datagen: unknown option '", flag, "'");
      return false;
    }
  }

  // Named after the format so a run cannot leave a .txt holding binary.
  if (options.output.empty()) {
    options.output = (options.format == Format::Binpack) ? "data.binpack" : "data.txt";
  }

  return true;
}

bool ClessDatagen::open_output() {
  std::ifstream existing(options.output, std::ios::binary);
  bool has_data = existing.good() && existing.peek() != std::ifstream::traits_type::eof();
  existing.close();

  // Silently overwriting a finished dataset costs hours of compute, so make
  // the caller say which they meant.
  if (has_data && !options.append) {
    Logger::error(
        "datagen: '",
        options.output,
        "' already holds data; pass --append or another --out"
    );
    return false;
  }

  std::ios::openmode mode = options.append ? std::ios::app : std::ios::trunc;
  if (options.format == Format::Binpack) mode |= std::ios::binary;

  out.open(options.output, mode);
  if (!out) {
    Logger::error("datagen: could not open '", options.output, "' for writing");
    return false;
  }

  // Appending continues a file that already carries the header.
  if (options.format == Format::Binpack && !has_data) binpack.write_header();

  return true;
}

SearchLimits ClessDatagen::search_limits() const {
  SearchLimits limits;
  limits.depth = options.depth;
  limits.nodes = options.nodes;

  return limits;
}

bool ClessDatagen::insufficient_material() const {
  if (pos.get_bb(WHITE, PAWN) | pos.get_bb(BLACK, PAWN)) return false;
  if (pos.get_bb(WHITE, ROOK) | pos.get_bb(BLACK, ROOK)) return false;
  if (pos.get_bb(WHITE, QUEEN) | pos.get_bb(BLACK, QUEEN)) return false;

  uint64_t knights = pos.get_bb(WHITE, KNIGHT) | pos.get_bb(BLACK, KNIGHT);
  uint64_t bishops = pos.get_bb(WHITE, BISHOP) | pos.get_bb(BLACK, BISHOP);

  if (count_bits(knights | bishops) <= 1) return true;

  // Two knights cannot force mate but can be mated, so the game is still worth
  // playing out. Only the same-colour bishop case is a genuine dead position.
  if (knights) return false;

  return (bishops & DARK_SQUARES) == bishops || (bishops & DARK_SQUARES) == 0;
}

int ClessDatagen::repetition_count() const {
  // Position::is_repetition answers a different question — it stops at the
  // first match because a twofold is already a draw score inside the search. A
  // real game needs the third occurrence.
  uint64_t key = history.back();
  int lookback = std::min<int>(pos.halfmove_clock, static_cast<int>(history.size()) - 1);

  int count = 1;
  for (int i = 1; i <= lookback; i++) {
    if (history[history.size() - 1 - i] == key) count++;
  }

  return count;
}

const char *ClessDatagen::result_for(Outcome outcome, Color stm) {
  if (outcome == Outcome::Draw) return "0.5";

  bool stm_won = (outcome == Outcome::WhiteWin) == (stm == WHITE);
  return stm_won ? "1.0" : "0.0";
}

BinpackWriter::Result ClessDatagen::binpack_result(Outcome outcome) {
  if (outcome == Outcome::WhiteWin) return BinpackWriter::WHITE_WIN;
  if (outcome == Outcome::BlackWin) return BinpackWriter::BLACK_WIN;

  return BinpackWriter::DRAW;
}

bool ClessDatagen::setup_opening() {
  pos.set_fen(INITIAL_POSITION_FEN);

  for (int i = 0; i < options.random_plies; i++) {
    MoveList moves = generator.generate_legal_moves(pos);
    if (moves.empty()) return false;

    std::uniform_int_distribution<int> pick(0, moves.count - 1);
    pos.make_move(moves[pick(rng)]);
  }

  if (generator.generate_legal_moves(pos).empty()) return false;
  if (options.opening_max_score <= 0) return true;

  // Random moves lose material often enough that most openings are already
  // decided. Those games are short and teach the net about positions no search
  // would ever walk into.
  return std::abs(run_search(pos, search_limits()).score) <= options.opening_max_score;
}

ClessDatagen::Outcome ClessDatagen::play_game() {
  plies.clear();

  history.clear();
  history.push_back(pos.hash);

  SearchLimits limits = search_limits();

  int winning_streak = 0;
  Color streak_winner = WHITE;

  for (int ply = 0; ply < options.max_plies; ply++) {
    MoveList moves = generator.generate_legal_moves(pos);

    // run_search reads moves[0] before doing anything else, so a terminal
    // position must never reach it.
    if (moves.empty()) {
      if (!generator.is_in_check(pos, pos.to_move)) return Outcome::Draw;
      return (pos.to_move == WHITE) ? Outcome::BlackWin : Outcome::WhiteWin;
    }

    if (pos.halfmove_clock >= 100) return Outcome::Draw;
    if (repetition_count() >= 3) return Outcome::Draw;
    if (insufficient_material()) return Outcome::Draw;

    bool in_check = generator.is_in_check(pos, pos.to_move);
    SearchResult result = run_search(pos, limits);

    // The trainer fits a static evaluation, so a position whose score comes
    // from a tactic the static function cannot see is noise: in check, one
    // move from a capture or promotion, or a mate distance rather than a
    // material judgement.
    bool quiet = !in_check && !result.best_move.is_capture() && !result.best_move.is_promotion()
                 && !is_mate_score(result.score);

    PlyRecord record;
    record.move = result.best_move;
    record.score = result.score;
    record.stm = pos.to_move;
    record.sample = quiet;
    if (quiet && options.format == Format::Text) record.fen = pos.get_fen();
    plies.push_back(record);

    // Playing a decided game to mate rarely changes the result and costs most
    // of the search time spent on that game.
    if (options.adjudicate_score > 0 && std::abs(result.score) >= options.adjudicate_score) {
      Color winner = (result.score > 0) ? pos.to_move : opposite_color(pos.to_move);

      winning_streak = (winner == streak_winner) ? winning_streak + 1 : 1;
      streak_winner = winner;

      if (winning_streak >= options.adjudicate_plies) {
        return (winner == WHITE) ? Outcome::WhiteWin : Outcome::BlackWin;
      }
    } else {
      winning_streak = 0;
    }

    pos.make_move(result.best_move);

    if (pos.halfmove_clock == 0) history.clear();
    history.push_back(pos.hash);
  }

  return Outcome::Unfinished;
}

void ClessDatagen::write_game(const std::string &start_fen, Outcome outcome) {
  if (options.format == Format::Binpack) {
    Position start(start_fen);
    binpack.begin_game(start, binpack_result(outcome), static_cast<uint16_t>(plies.size()));

    for (const PlyRecord &ply : plies) {
      binpack.add_ply(ply.move, ply.score, ply.sample);
    }

    binpack.end_game();
  } else {
    for (const PlyRecord &ply : plies) {
      if (!ply.sample) continue;

      out << ply.fen << ';' << ply.score << ';' << result_for(outcome, ply.stm) << '\n';
    }
  }

  // A run killed partway through should keep every game it finished.
  out.flush();
}

void ClessDatagen::report(int64_t games) const {
  auto now = std::chrono::steady_clock::now();
  int64_t elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - started).count();
  int64_t per_second = (elapsed_ms > 0) ? written * 1000 / elapsed_ms : 0;

  Logger::respond(
      "datagen: ",
      games,
      " games (",
      played,
      " kept), ",
      written,
      " positions, ",
      elapsed_ms / 1000,
      "s, ",
      per_second,
      " pos/s"
  );
}

int ClessDatagen::run(int argc, char **argv) {
  if (!parse_options(argc, argv)) {
    usage();
    return 1;
  }

  if (!open_output()) return 1;

  TT::resize(options.hash_mb);

  // NNUE::load reports its own failures.
  if (!options.eval_file.empty() && !NNUE::load(options.eval_file)) return 1;

  uint64_t seed = options.seed ? options.seed : std::random_device{}();
  rng.seed(seed);

  Logger::respond(
      "datagen: seed ",
      seed,
      ", ",
      options.games,
      " games, ",
      options.nodes,
      " nodes/move, eval ",
      NNUE::is_loaded() ? "nnue" : "hce"
  );

  started = std::chrono::steady_clock::now();

  for (int64_t game = 0; game < options.games; game++) {
    // Scores left by the previous game describe unrelated positions.
    TT::clear();

    bool opened = false;
    for (int attempt = 0; attempt < OPENING_ATTEMPTS && !opened; attempt++) {
      opened = setup_opening();
    }

    if (!opened) {
      discarded++;
      continue;
    }

    // The binpack record replays from here, so it has to be taken before
    // play_game moves the board off it.
    std::string start_fen = pos.get_fen();

    Outcome outcome = play_game();

    // A game with no result has no label, and inventing one poisons the
    // target the trainer blends the score into.
    if (outcome == Outcome::Unfinished) {
      discarded++;
      continue;
    }

    write_game(start_fen, outcome);

    played++;
    for (const PlyRecord &ply : plies) {
      if (ply.sample) written++;
    }

    if ((game + 1) % PROGRESS_INTERVAL == 0) report(game + 1);
  }

  report(options.games);

  if (discarded > 0) Logger::info("datagen: discarded ", discarded, " unusable games");

  return 0;
}
