#pragma once

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

/**
 * @brief Self-play training-data generation, driven from argv rather than UCI.
 *
 * Datagen lives inside the engine instead of in an external script because the
 * cost is dominated by search: a driver speaking UCI pays a process spawn, a
 * text round-trip and a move re-parse for every ply, and cannot reuse the
 * transposition table across a game. It also needs a full chess rules
 * implementation of its own just to decide when a game has ended, which is
 * something this binary already has.
 *
 * A run is one long-lived object rather than a chain of free functions: the
 * options, the RNG, the board, the output stream and the running totals are
 * all read by nearly every step, and threading them through arguments buys
 * nothing.
 *
 * The output is what `clessy-nnue` consumes: one line per position,
 * `<fen>;<score_cp>;<result>`, with both the score and the result from the
 * side to move. `clessy_nnue/dataset.py` is the other end of that contract;
 * changing a field here means changing it there.
 */
class ClessDatagen {
public:
  /**
   * @brief Parse options from argv and generate until the run is done.
   *
   * @param argc Argument count after the "datagen" subcommand
   * @param argv Arguments after the "datagen" subcommand
   * @return Process exit status
   */
  int run(int argc, char **argv);

private:
  enum class Outcome {
    WhiteWin,
    BlackWin,
    Draw,
    Unfinished
  };

  struct Options {
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

    size_t hash_mb = 16;
    uint64_t seed = 0; // 0 means "draw one from the system"
    bool append = false;
  };

  /// @brief One position kept from a played game.
  ///
  /// The result is only known once the game is over, so the side to move is
  /// carried along until there is one to write.
  struct Sample {
    std::string fen;
    int score = 0;
    Color stm = WHITE;
  };

  // A random opening can land in a position that is already decided or already
  // over. Give up on a game rather than loop forever looking for a playable one.
  static constexpr int OPENING_ATTEMPTS = 32;
  static constexpr int64_t PROGRESS_INTERVAL = 50;

  static constexpr uint64_t DARK_SQUARES = 0xAA55AA55AA55AA55ULL;

  Options options;

  // The generator is stateless and the board is reused between games, so both
  // outlive any one of them.
  MoveGenerator generator;
  Position pos{INITIAL_POSITION_FEN};
  std::mt19937_64 rng;

  std::ofstream out;

  std::vector<Sample> samples;

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

  /// @brief Open the output file, refusing to clobber a finished dataset.
  bool open_output();

  /// @brief Put the board in a random opening, or report an unusable attempt.
  bool setup_opening();

  /// @brief Play the board out, keeping the positions worth training on.
  Outcome play_game();

  void write_game(Outcome outcome);
  void report(int64_t games) const;

  SearchLimits search_limits() const;

  /// @brief Whether neither side can deliver mate with the material left.
  bool insufficient_material() const;

  /// @brief How many times the position on the board has occurred this game.
  int repetition_count() const;

  static const char *result_for(Outcome outcome, Color stm);
};
