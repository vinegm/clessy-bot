#pragma once

#include "binpack.hpp"
#include "chess_types.hpp"
#include "move_gen.hpp"
#include "position.hpp"
#include "search.hpp"

#include <chrono>
#include <cstdint>
#include <fstream>
#include <random>
#include <string>
#include <vector>

class ClessDatagen {
public:
  int run(int argc, char **argv);

private:
  enum class Format {
    Binpack,
    Text
  };
  enum class Outcome {
    WhiteWin,
    BlackWin,
    Draw,
    Unfinished
  };

  struct Options {
    Format format = Format::Binpack;
    std::string output;
    std::string eval_file;

    int64_t games = 1000;

    // A node budget rather than a time budget: the same position must cost the
    // same search on every machine, or the labels depend on the host.
    int64_t nodes = 5000;
    int depth = MAX_SEARCH_DEPTH;

    int random_plies = 8;
    int opening_max_score = 800;

    int adjudicate_score = 2000;
    int adjudicate_plies = 4;
    int max_plies = 400;

    // seed as 0 will be replaced with a random value from the system RNG
    uint64_t seed = 0;
    size_t hash_mb = 16;
    bool append = false;
  };

  // The binpack format needs every move to keep its chain intact, so plies
  // the filter rejected are recorded too and marked. Only a sample carries a
  // FEN, since only the text writer reads one and building it is not free.
  struct PlyRecord {
    std::string fen;
    Move move{};
    int score = 0;
    Color stm = WHITE; // whose result the label must be written from
    bool sample = false;
  };

  // A random opening can land in a position that is already decided or already
  // over. Give up on a game rather than loop forever looking for a playable one.
  static constexpr int OPENING_ATTEMPTS = 32;
  static constexpr int64_t PROGRESS_INTERVAL = 50;

  static constexpr uint64_t DARK_SQUARES = 0xAA55AA55AA55AA55ULL;

  Options options;

  // The board is reused between games, so it outlives any one of them.
  Position pos{INITIAL_POSITION_FEN};
  std::mt19937_64 rng;

  // Declared before the writer, which holds a reference to it.
  std::ofstream out;
  BinpackWriter binpack{out};

  std::vector<PlyRecord> plies;

  // Zobrist keys of the current game, the last entry being the position on the
  // board. Cleared by every irreversible move, since nothing before one can
  // repeat again.
  std::vector<uint64_t> history;

  int64_t played = 0;
  int64_t written = 0;
  int64_t discarded = 0;
  std::chrono::steady_clock::time_point started;

  static void usage();
  bool parse_options(int argc, char **argv);

  // Open the output file, refusing to clobber a finished dataset.
  bool open_output();

  // Put the board in a random opening, or report an unusable attempt.
  bool setup_opening();

  // Play the board out, recording every ply played.
  Outcome play_game();

  void write_game(const std::string &start_fen, Outcome outcome);
  void report(int64_t games) const;

  SearchLimits search_limits() const;

  // Whether neither side can deliver mate with the material left.
  bool insufficient_material() const;

  // How many times the position on the board has occurred this game.
  int repetition_count() const;

  static const char *result_for(Outcome outcome, Color stm);
  static BinpackWriter::Result binpack_result(Outcome outcome);
};
