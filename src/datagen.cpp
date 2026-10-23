#include "datagen.hpp"

#include "eval.hpp"
#include "logger.hpp"
#include "move_gen.hpp"
#include "nnue.hpp"
#include "transposition_table.hpp"

#include <algorithm>
#include <cstdlib>
#include <map>

void Datagen::add_options(CLI::App &app) {
  app.add_option_function<std::string>(
         "--format",
         [this](const std::string &name) {
           options.format = Format::Binpack;
           if (name == "text") options.format = Format::Text;
         },
         "output format"
  )
      ->check(CLI::IsMember({"binpack", "text"}))
      ->default_str("binpack");
  app.add_option("--out", options.output, "output file, data.binpack or data.txt when unset");
  app.add_flag("--append", options.append, "add to an existing output file instead of refusing");

  app.add_option("--games", options.games, "games to play")->capture_default_str();
  app.add_option("--nodes", options.nodes, "search nodes per move")->capture_default_str();
  app.add_option("--depth", options.depth, "depth cap per move")->capture_default_str();
  app.add_option("--random-plies", options.random_plies, "random moves opening each game")
      ->capture_default_str();
  app.add_option(
         "--opening-max-score",
         options.opening_max_score,
         "reject openings scoring past this, in centipawns"
  )
      ->capture_default_str();
  app.add_option(
         "--adjudicate-score",
         options.adjudicate_score,
         "call a win at this score, 0 is off"
  )
      ->capture_default_str();
  app.add_option("--adjudicate-plies", options.adjudicate_plies, "consecutive plies needed for it")
      ->capture_default_str();
  app.add_option("--max-plies", options.max_plies, "discard games longer than this")
      ->capture_default_str();

  app.add_option("--hash", options.hash_mb, "transposition table size in MB")
      ->capture_default_str();
  app.add_option(
      "--eval-file",
      options.eval_file,
      ".nnue network to label with, hand-crafted if unset"
  );
  app.add_option("--seed", options.seed, "random seed, 0 picks one")->capture_default_str();
}

bool Datagen::open_output() {
  std::ifstream existing(options.output, std::ios::binary);
  bool has_data = existing.good() && existing.peek() != std::ifstream::traits_type::eof();
  existing.close();

  // NOTE: overwriting a finished dataset costs hours of compute
  if (has_data && !options.append) {
    Logger::error(
        "datagen: '",
        options.output,
        "' already holds data; pass --append or another --out"
    );
    return false;
  }

  std::ios::openmode mode = std::ios::trunc;
  if (options.append) mode = std::ios::app;
  if (options.format == Format::Binpack) mode |= std::ios::binary;

  out.open(options.output, mode);
  if (!out) {
    Logger::error("datagen: could not open '", options.output, "' for writing");
    return false;
  }

  if (options.format == Format::Binpack && !has_data) binpack.write_header();

  return true;
}

SearchLimits Datagen::search_limits() const {
  SearchLimits limits;
  limits.depth = options.depth;
  limits.nodes = options.nodes;

  return limits;
}

bool Datagen::insufficient_material() const {
  if (pos.pieces(WHITE, PAWN) | pos.pieces(BLACK, PAWN)) return false;
  if (pos.pieces(WHITE, ROOK) | pos.pieces(BLACK, ROOK)) return false;
  if (pos.pieces(WHITE, QUEEN) | pos.pieces(BLACK, QUEEN)) return false;

  uint64_t knights = pos.pieces(WHITE, KNIGHT) | pos.pieces(BLACK, KNIGHT);
  uint64_t bishops = pos.pieces(WHITE, BISHOP) | pos.pieces(BLACK, BISHOP);

  if (count_bits(knights | bishops) <= 1) return true;

  // NOTE: two knights can't force mate but can be mated, so only same colored bishops are dead
  if (knights) return false;

  return (bishops & DARK_SQUARES) == bishops || (bishops & DARK_SQUARES) == 0;
}

int Datagen::repetition_count() const {
  // NOTE: Position::is_repetition stops at twofold, a real game needs the third occurrence
  uint64_t key = history.back();
  int lookback = std::min<int>(pos.halfmove_clock, static_cast<int>(history.size()) - 1);

  int count = 1;
  for (int i = 1; i <= lookback; i++) {
    if (history[history.size() - 1 - i] == key) count++;
  }

  return count;
}

const char *Datagen::result_for(Outcome outcome, Color side_to_move) {
  if (outcome == Outcome::Draw) return "0.5";
  if (outcome == win_for(side_to_move)) return "1.0";

  return "0.0";
}

Datagen::Outcome Datagen::win_for(Color winner) {
  if (winner == WHITE) return Outcome::WhiteWin;

  return Outcome::BlackWin;
}

BinpackWriter::Result Datagen::binpack_result(Outcome outcome) {
  if (outcome == Outcome::WhiteWin) return BinpackWriter::WHITE_WIN;
  if (outcome == Outcome::BlackWin) return BinpackWriter::BLACK_WIN;

  return BinpackWriter::DRAW;
}

bool Datagen::setup_opening() {
  pos.set_fen(INITIAL_POSITION_FEN);

  for (int i = 0; i < options.random_plies; i++) {
    MoveList moves = MoveGenerator::generate_legal_moves(pos);
    if (moves.empty()) return false;

    std::uniform_int_distribution<int> pick(0, moves.count - 1);
    pos.make_move(moves[pick(rng)]);
  }

  if (MoveGenerator::generate_legal_moves(pos).empty()) return false;
  if (options.opening_max_score <= 0) return true;

  // NOTE: random moves often leave the game already decided, which teaches the net nothing
  return std::abs(Search::run(pos, search_limits()).score) <= options.opening_max_score;
}

Datagen::Outcome Datagen::play_game() {
  plies.clear();

  history.clear();
  history.push_back(pos.hash);

  SearchLimits limits = search_limits();

  int winning_streak = 0;
  Color streak_winner = WHITE;

  for (int ply = 0; ply < options.max_plies; ply++) {
    MoveList moves = MoveGenerator::generate_legal_moves(pos);

    // NOTE: Search::run reads moves[0], so a terminal position must never reach it
    if (moves.empty() && !MoveGenerator::is_in_check(pos, pos.to_move)) return Outcome::Draw;
    if (moves.empty()) return win_for(opposite_color(pos.to_move));

    if (pos.halfmove_clock >= 100) return Outcome::Draw;
    if (repetition_count() >= 3) return Outcome::Draw;
    if (insufficient_material()) return Outcome::Draw;

    bool in_check = MoveGenerator::is_in_check(pos, pos.to_move);
    SearchResult result = Search::run(pos, limits);

    // NOTE: the trainer fits a static eval, so tactical positions are noise
    bool quiet = !in_check && !result.best_move.is_capture() && !result.best_move.is_promotion()
                 && !Search::is_mate_score(result.score);

    PlyRecord record;
    record.move = result.best_move;
    record.score = result.score;
    record.side_to_move = pos.to_move;
    record.sample = quiet;
    if (quiet && options.format == Format::Text) record.fen = pos.fen();
    plies.push_back(record);

    // NOTE: playing a decided game to mate rarely changes the result
    const bool decided =
        options.adjudicate_score > 0 && std::abs(result.score) >= options.adjudicate_score;
    if (!decided) winning_streak = 0;

    if (decided) {
      Color winner = pos.to_move;
      if (result.score < 0) winner = opposite_color(pos.to_move);

      if (winner != streak_winner) winning_streak = 0;
      winning_streak++;
      streak_winner = winner;

      if (winning_streak >= options.adjudicate_plies) return win_for(winner);
    }

    pos.make_move(result.best_move);

    if (pos.halfmove_clock == 0) history.clear();
    history.push_back(pos.hash);
  }

  return Outcome::Unfinished;
}

void Datagen::write_game(const std::string &start_fen, Outcome outcome) {
  if (options.format == Format::Binpack) {
    Position start(start_fen);
    binpack.begin_game(start, binpack_result(outcome), static_cast<uint16_t>(plies.size()));

    for (const PlyRecord &ply : plies) {
      binpack.add_ply(ply.move, ply.score, ply.sample);
    }

    binpack.end_game();
  }

  if (options.format == Format::Text) {
    for (const PlyRecord &ply : plies) {
      if (!ply.sample) continue;

      out << ply.fen << ';' << ply.score << ';' << result_for(outcome, ply.side_to_move) << '\n';
    }
  }

  // NOTE: a run killed partway through keeps every game it finished
  out.flush();
}

void Datagen::report(int64_t games) const {
  auto now = std::chrono::steady_clock::now();
  int64_t elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - started).count();
  int64_t per_second = 0;
  if (elapsed_ms > 0) per_second = written * 1000 / elapsed_ms;

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

int Datagen::run(int argc, char **argv) {
  CLI::App app{"Self-play training data for clessy-nnue", "clessy datagen"};
  add_options(app);

  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError &error) { return app.exit(error); }

  // NOTE: named after the format so a run can't leave a .txt holding binary
  if (options.output.empty() && options.format == Format::Binpack) options.output = "data.binpack";
  if (options.output.empty()) options.output = "data.txt";

  if (!open_output()) return 1;

  TranspositionTable::resize(options.hash_mb);

  if (!options.eval_file.empty() && !NNUE::load(options.eval_file)) return 1;

  uint64_t seed = options.seed;
  if (seed == 0) seed = std::random_device{}();
  rng.seed(seed);

  Logger::respond(
      "datagen: seed ",
      seed,
      ", ",
      options.games,
      " games, ",
      options.nodes,
      " nodes/move, eval ",
      Eval::source_name()
  );

  started = std::chrono::steady_clock::now();

  for (int64_t game = 0; game < options.games; game++) {
    TranspositionTable::clear();

    bool opened = false;
    for (int attempt = 0; attempt < OPENING_ATTEMPTS && !opened; attempt++) {
      opened = setup_opening();
    }

    if (!opened) {
      discarded++;
      continue;
    }

    // NOTE: the binpack record replays from here
    std::string start_fen = pos.fen();

    Outcome outcome = play_game();

    // NOTE: a game with no result has no label, inventing one poisons the target
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
