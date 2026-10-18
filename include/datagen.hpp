#pragma once

#include "binpack.hpp"
#include "chess_types.hpp"
#include "move_gen.hpp"
#include "position.hpp"
#include "search.hpp"

#include <CLI/CLI.hpp>
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
  enum class Format : uint8_t {
    Binpack,
    Text
  };
  enum class Outcome : uint8_t {
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

    // NOTE: nodes, not time, so the labels don't depend on the host
    int64_t nodes = 5000;
    int depth = MAX_SEARCH_DEPTH;

    int random_plies = 8;
    int opening_max_score = 800;

    int adjudicate_score = 2000;
    int adjudicate_plies = 4;
    int max_plies = 400;

    uint64_t seed = 0; // 0 picks a random one
    size_t hash_mb = 16;
    bool append = false;
  };

  // NOTE: binpack replays every move, so rejected plies are recorded too
  struct PlyRecord {
    std::string fen; // only set on samples, only the text format reads it
    Move move{};
    int score = 0;
    Color side_to_move = WHITE;
    bool sample = false;
  };

  static constexpr int OPENING_ATTEMPTS = 32;
  static constexpr int64_t PROGRESS_INTERVAL = 50;

  static constexpr uint64_t DARK_SQUARES = 0xAA55AA55AA55AA55ULL;

  Options options;

  Position pos{INITIAL_POSITION_FEN};
  std::mt19937_64 rng{std::random_device{}()};

  // NOTE: declared before the writer, which holds a reference to it
  std::ofstream out;
  BinpackWriter binpack{out};

  std::vector<PlyRecord> plies;

  // NOTE: hashes since the last irreversible move, the board's position last
  std::vector<uint64_t> history;

  int64_t played = 0;
  int64_t written = 0;
  int64_t discarded = 0;
  std::chrono::steady_clock::time_point started;

  void add_options(CLI::App &app);

  bool open_output();
  bool setup_opening();
  Outcome play_game();

  void write_game(const std::string &start_fen, Outcome outcome);
  void report(int64_t games) const;

  SearchLimits search_limits() const;

  bool insufficient_material() const;
  int repetition_count() const;

  static const char *result_for(Outcome outcome, Color side_to_move);
  static Outcome win_for(Color winner);
  static BinpackWriter::Result binpack_result(Outcome outcome);
};
