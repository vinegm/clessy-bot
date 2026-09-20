#include "search.hpp"

#include "eval.hpp"
#include "move_gen.hpp"
#include "position.hpp"
#include "transposition_table.hpp"

#include <algorithm>
#include <chrono>

using Clock = std::chrono::steady_clock;

// NOTE: the king sits above the queen so it sorts last as an attacker
constexpr int ORDER_VALUE[7] = {0, 100, 320, 330, 500, 900, 1000};

// Ordering buckets: TT move, then captures and promotions (MVV-LVA), then
// killers, then quiets by history score.
constexpr int HASH_MOVE_ORDER = 1 << 20;
constexpr int CAPTURE_ORDER = 100000;
constexpr int KILLER_ORDER = 99000;

static int move_order_score(const Position &pos, const Move &move, const Move &hash_move) {
  if (move == hash_move) return HASH_MOVE_ORDER;

  int score = 0;

  if (move.is_capture()) {
    PieceType victim = move.is_en_passant() ? PAWN : decode_type(pos.lookup_table[move.to]);
    PieceType attacker = decode_type(pos.lookup_table[move.from]);
    score += CAPTURE_ORDER + 10 * ORDER_VALUE[victim] - ORDER_VALUE[attacker];
  }

  if (move.is_promotion()) score += CAPTURE_ORDER + ORDER_VALUE[move.promotion_piece];

  return score;
}

static void order_moves(const Position &pos, MoveList &moves) {
  const Move no_hash_move{};

  std::stable_sort(moves.begin(), moves.end(), [&pos, &no_hash_move](const Move &a, const Move &b) {
    return move_order_score(pos, a, no_hash_move) > move_order_score(pos, b, no_hash_move);
  });
}

// NOTE: mate scores are relative to the root while searching, but to the node in the table
static int score_to_table(int score, int ply) {
  if (!is_mate_score(score)) return score;
  if (score < 0) return score - ply;

  return score + ply;
}

static int score_from_table(int score, int ply) {
  if (!is_mate_score(score)) return score;
  if (score < 0) return score + ply;

  return score - ply;
}

static int64_t time_budget_ms(const SearchLimits &limits, Color side_to_move) {
  int64_t overhead = std::max<int64_t>(limits.move_overhead, 0);

  if (limits.movetime >= 0) return std::max<int64_t>(limits.movetime - overhead, 1);

  int64_t time = limits.wtime;
  int64_t increment = limits.winc;
  if (side_to_move == BLACK) {
    time = limits.btime;
    increment = limits.binc;
  }

  if (time < 0) return -1;

  // NOTE: without movestogo assume about 20 moves are left
  int64_t moves_left = 20;
  if (limits.movestogo > 0) moves_left = limits.movestogo;

  int64_t budget = time / moves_left + increment / 2 - overhead;
  return std::clamp<int64_t>(budget, 1, std::max<int64_t>(time - overhead, 1));
}

class Searcher {
public:
  Searcher(
      Position &pos,
      const SearchLimits &limits,
      SearchControl *control,
      Clock::time_point start
  ) : pos(pos), control(control) {
    if (!limits.infinite) node_limit = limits.nodes;
    if (!limits.infinite && limits.has_clock()) budget_ms = time_budget_ms(limits, pos.to_move);

    has_deadline = budget_ms >= 0;
    deadline = start;
    if (has_deadline) deadline += std::chrono::milliseconds(budget_ms);

    // NOTE: a ponder search only arms its deadline on ponderhit
    ponder_active = control != nullptr && control->pondering.load(std::memory_order_relaxed);
  }

  uint64_t nodes = 0;
  int seldepth = 0;
  bool stopped = false;
  bool time_checks_enabled = false;

  /// @brief Whether another iteration would only be thrown away.
  bool out_of_budget() {
    if (control && control->stop.load(std::memory_order_relaxed)) return true;
    if (node_limit >= 0 && nodes >= static_cast<uint64_t>(node_limit)) return true;

    return has_deadline && !ponder_active && Clock::now() >= deadline;
  }

  int quiescence(int alpha, int beta, int ply) {
    if (ply > seldepth) seldepth = ply;
    if (!count_node()) return 0;

    // Standing pat: the side to move is never forced to capture, so the
    // static score is a lower bound on what this node is worth.
    int best = evaluator.evaluate(pos);
    if (best >= beta) return best;
    alpha = std::max(alpha, best);

    MoveList moves = generator.generate_legal_captures(pos);
    order_moves(pos, moves);

    for (const Move &move : moves) {
      pos.make_move(move);
      int score = -quiescence(-beta, -alpha, ply + 1);
      pos.undo_move();
      if (stopped) return 0;

      best = std::max(best, score);
      alpha = std::max(alpha, score);
      if (alpha >= beta) break;
    }

    return best;
  }

  void append_pv(std::vector<Move> &out, int ply) const {
    if (ply >= MAX_SEARCH_DEPTH) return;

    for (int i = 0; i < pv_length[ply]; i++) {
      out.push_back(pv_table[ply][i]);
    }
  }

  int negamax(int depth, int ply, int alpha, int beta) {
    if (ply > seldepth) seldepth = ply;

    if (ply < MAX_SEARCH_DEPTH) pv_length[ply] = 0;

    if (depth == 0) return quiescence(alpha, beta, ply);

    if (!count_node()) return 0;

    // NOTE: twofold counts as a draw, if we can repeat once we can repeat twice
    if (pos.halfmove_clock >= 100 || pos.is_repetition()) return 0;

    Move hash_move{};
    if (const TranspositionTable::Entry *entry = TranspositionTable::probe(pos.hash)) {
      hash_move = entry->move;

      if (entry->depth >= depth) {
        int table_score = score_from_table(entry->score, ply);

        if (entry->bound == TranspositionTable::EXACT
            || (entry->bound == TranspositionTable::LOWER && table_score >= beta)
            || (entry->bound == TranspositionTable::UPPER && table_score <= alpha)) {
          return table_score;
        }
      }
    }

    MoveList moves = generator.generate_legal_moves(pos);
    if (moves.empty()) { return generator.is_in_check(pos, pos.to_move) ? -(MATE_SCORE - ply) : 0; }

    order_quiet_aware(moves, hash_move, ply);

    int original_alpha = alpha;
    int best = -INFINITE_SCORE;
    Move best_move = moves[0];

    for (const Move &move : moves) {
      pos.make_move(move);
      int score = -negamax(depth - 1, ply + 1, -beta, -alpha);
      pos.undo_move();
      if (stopped) return 0;

      if (score > best) {
        best = score;
        best_move = move;
      }

      if (score > alpha) {
        alpha = score;
        update_pv(move, ply);
      }

      if (alpha >= beta) {
        if (is_quiet(move)) record_cutoff(move, depth, ply);
        break;
      }
    }

    TranspositionTable::Bound bound = TranspositionTable::UPPER;
    if (best > original_alpha) bound = TranspositionTable::EXACT;
    if (best >= beta) bound = TranspositionTable::LOWER;

    TranspositionTable::store(pos.hash, best_move, score_to_table(best, ply), depth, bound);

    return best;
  }

private:
  Position &pos;
  MoveGenerator generator{};
  Evaluator evaluator{};
  SearchControl *control;
  int64_t node_limit;

  Clock::time_point deadline;
  int64_t budget_ms = -1;
  bool has_deadline = false;

  // Cleared the first time the search notices "ponderhit"; that is when the
  // deadline is armed, since only then is the engine spending its own time.
  bool ponder_active = false;

  Move killers[MAX_SEARCH_DEPTH][2]{};
  int history[2][64][64]{}; // [side to move][from][to]

  // Triangular table: row `ply` holds the best line found from that ply down,
  // built by prepending the current move to the row below it.
  Move pv_table[MAX_SEARCH_DEPTH][MAX_SEARCH_DEPTH]{};
  int pv_length[MAX_SEARCH_DEPTH]{};

  void update_pv(const Move &move, int ply) {
    if (ply >= MAX_SEARCH_DEPTH) return;

    pv_table[ply][0] = move;

    int child_length = 0;
    if (ply + 1 < MAX_SEARCH_DEPTH) child_length = pv_length[ply + 1];

    for (int i = 0; i < child_length && i + 1 < MAX_SEARCH_DEPTH; i++) {
      pv_table[ply][i + 1] = pv_table[ply + 1][i];
    }

    pv_length[ply] = std::min(child_length + 1, MAX_SEARCH_DEPTH);
  }

  static bool is_quiet(const Move &move) { return !move.is_capture() && !move.is_promotion(); }

  void record_cutoff(const Move &move, int depth, int ply) {
    // Clamped below the killer bucket so hot quiets never outrank killers or
    // captures.
    int &score = history[pos.to_move][move.from][move.to];
    score = std::min(score + depth * depth, KILLER_ORDER - 1);

    if (ply < MAX_SEARCH_DEPTH && move != killers[ply][0]) {
      killers[ply][1] = killers[ply][0];
      killers[ply][0] = move;
    }
  }

  void order_quiet_aware(MoveList &moves, const Move &hash_move, int ply) {
    std::stable_sort(
        moves.begin(),
        moves.end(),
        [this, &hash_move, ply](const Move &a, const Move &b) {
          return quiet_aware_score(a, hash_move, ply) > quiet_aware_score(b, hash_move, ply);
        }
    );
  }

  int quiet_aware_score(const Move &move, const Move &hash_move, int ply) {
    if (!is_quiet(move) || move == hash_move) return move_order_score(pos, move, hash_move);

    if (ply < MAX_SEARCH_DEPTH) {
      if (move == killers[ply][0]) return KILLER_ORDER + 1;
      if (move == killers[ply][1]) return KILLER_ORDER;
    }

    return history[pos.to_move][move.from][move.to];
  }

  bool count_node() {
    nodes++;

    if (stopped) return false;

    // NOTE: stop is a protocol obligation, so it is honoured even at depth 1
    if (control && control->stop.load(std::memory_order_relaxed)) {
      stopped = true;
      return false;
    }

    if (!time_checks_enabled) return true;

    if (ponder_active) {
      if (control && control->pondering.load(std::memory_order_relaxed)) return true;

      ponder_active = false;
      if (has_deadline) deadline = Clock::now() + std::chrono::milliseconds(budget_ms);
    }

    if (node_limit >= 0 && nodes >= static_cast<uint64_t>(node_limit)) {
      stopped = true;
      return false;
    }

    // NOTE: reading the clock is expensive, so it is only sampled
    if (has_deadline && (nodes & 2047) == 0 && Clock::now() >= deadline) stopped = true;

    return !stopped;
  }
};

static MoveList restrict_root_moves(const MoveList &root_moves, const std::vector<Move> &allowed) {
  if (allowed.empty()) return root_moves;

  MoveList restricted;
  for (const Move &move : root_moves) {
    if (std::find(allowed.begin(), allowed.end(), move) == allowed.end()) continue;

    restricted.add_move(move);
  }

  // NOTE: a list that leaves nothing is ignored, so a move always comes back
  if (restricted.empty()) return root_moves;

  return restricted;
}

SearchResult run_search(
    Position &pos,
    const SearchLimits &limits,
    const SearchInfoCallback &on_iteration,
    SearchControl *control
) {
  Clock::time_point start = Clock::now();
  MoveGenerator generator;

  MoveList root_moves = generator.generate_legal_moves(pos);

  // "go searchmoves" restricts the root without changing anything below it.
  // Moves the position does not have are dropped rather than refused, and a
  // list that leaves nothing behind is ignored so a move always comes back.
  if (!limits.searchmoves.empty()) {
    MoveList restricted;
    for (const Move &move : root_moves) {
      for (const Move &wanted : limits.searchmoves) {
        if (move != wanted) continue;

        restricted.add_move(move);
        break;
      }
    }

    if (!restricted.empty()) root_moves = restricted;
  }

  SearchResult result;
  if (root_moves.empty()) return result;

  // NOTE: something legal to return if stop arrives during the first iteration
  result.best_move = root_moves[0];

  Searcher searcher(pos, limits, control, start);
  order_moves(pos, root_moves);

  int max_depth = std::clamp(limits.depth, 1, MAX_SEARCH_DEPTH);

  // NOTE: the mating ply still needs depth left to see the empty move list
  if (limits.mate > 0) {
    max_depth = std::min(max_depth, std::clamp(2 * limits.mate, 1, MAX_SEARCH_DEPTH));
  }

  int lines = std::clamp(limits.multipv, 1, root_moves.count);

  for (int depth = 1; depth <= max_depth; depth++) {
    // NOTE: depth 1 always finishes, so a best move always exists
    searcher.time_checks_enabled = depth > 1;
    searcher.seldepth = 0;

    std::vector<SearchResult> iteration;
    std::vector<Move> claimed;

    for (int line = 0; line < lines; line++) {
      SearchResult current;
      current.depth = depth;
      current.multipv = line + 1;

      int alpha = -INFINITE_SCORE;
      bool has_move = false;

      for (const Move &move : root_moves) {
        if (std::find(claimed.begin(), claimed.end(), move) != claimed.end()) continue;

        pos.make_move(move);
        int score = -searcher.negamax(depth - 1, 1, -INFINITE_SCORE, -alpha);
        pos.undo_move();
        if (searcher.stopped) break;

        alpha = std::max(alpha, score);

        if (has_move && score <= current.score) continue;

        has_move = true;
        current.score = score;
        current.best_move = move;
        current.pv.assign(1, move);
        searcher.append_pv(current.pv, 1);
      }

      if (searcher.stopped || !has_move) break;

      current.seldepth = std::max(searcher.seldepth, depth);
      current.nodes = searcher.nodes;
      current.elapsed_ms =
          std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();

      claimed.push_back(current.best_move);
      iteration.push_back(current);
    }

    if (searcher.stopped || iteration.empty()) break;

    result = iteration.front();

    for (const SearchResult &line : iteration) {
      if (on_iteration) on_iteration(line);
    }

    for (int i = static_cast<int>(iteration.size()) - 1; i >= 0; i--) {
      Move *found = std::find(root_moves.begin(), root_moves.end(), iteration[i].best_move);
      if (found != root_moves.end()) std::rotate(root_moves.begin(), found, found + 1);
    }

    if (limits.infinite) continue;
    if (is_mate_score(result.score)) break;
    if (searcher.out_of_budget()) break;
  }

  result.nodes = searcher.nodes;
  return result;
}
