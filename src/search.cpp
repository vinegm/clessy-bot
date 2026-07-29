#include "search.hpp"

#include "eval.hpp"
#include "move_gen.hpp"
#include "position.hpp"
#include "tt.hpp"

#include <algorithm>
#include <chrono>

namespace {
using Clock = std::chrono::steady_clock;

// MVV-LVA: prefer capturing valuable victims with cheap attackers, and try
// promotions early too.
// The king only ever appears as an attacker; just above the queen so a king
// capture sorts last among captures of the same victim but stays inside the
// capture bucket.
constexpr int ORDER_VALUE[7] = {0, 100, 320, 330, 500, 900, 1000};

// Ordering buckets: TT move, then captures and promotions (MVV-LVA), then
// killers, then quiets by history score.
constexpr int TT_ORDER = 1 << 20;
constexpr int CAPTURE_ORDER = 100000;
constexpr int KILLER_ORDER = 99000;

int move_order_score(const Position &pos, const Move &move, const Move &tt_move) {
  if (move == tt_move) return TT_ORDER;

  int score = 0;

  if (move.is_capture()) {
    PieceType victim = move.is_en_passant() ? PAWN : decode_type(pos.lookup_table[move.to]);
    PieceType attacker = decode_type(pos.lookup_table[move.from]);
    score += CAPTURE_ORDER + 10 * ORDER_VALUE[victim] - ORDER_VALUE[attacker];
  }

  if (move.is_promotion()) score += CAPTURE_ORDER + ORDER_VALUE[move.promotion_piece];

  return score;
}

void order_moves(const Position &pos, MoveList &moves, const Move &tt_move = Move{}) {
  std::stable_sort(moves.begin(), moves.end(), [&pos, &tt_move](const Move &a, const Move &b) {
    return move_order_score(pos, a, tt_move) > move_order_score(pos, b, tt_move);
  });
}

// Mate scores are root-relative during the search but node-relative in the
// table, so distances stay correct wherever the entry is probed.
int score_to_tt(int score, int ply) {
  if (is_mate_score(score)) return score > 0 ? score + ply : score - ply;
  return score;
}

int score_from_tt(int score, int ply) {
  if (is_mate_score(score)) return score > 0 ? score - ply : score + ply;
  return score;
}

int64_t time_budget_ms(const SearchLimits &limits, Color stm) {
  int64_t overhead = std::max<int64_t>(limits.move_overhead, 0);

  if (limits.movetime >= 0) return std::max<int64_t>(limits.movetime - overhead, 1);

  int64_t time = (stm == WHITE) ? limits.wtime : limits.btime;
  int64_t inc = (stm == WHITE) ? limits.winc : limits.binc;
  if (time < 0) return -1;

  // With movestogo the horizon is known, so spread the clock over exactly
  // that many moves. Without it, assume a game has ~20 moves left. Half the
  // increment on top, and always leave the overhead behind.
  int64_t divisor = (limits.movestogo > 0) ? limits.movestogo : 20;
  int64_t budget = time / divisor + inc / 2 - overhead;
  return std::clamp<int64_t>(budget, 1, std::max<int64_t>(time - overhead, 1));
}

class Searcher {
public:
  Searcher(
      Position &pos,
      const SearchLimits &limits,
      SearchControl *control,
      Clock::time_point start
  ) : pos(pos), control(control), node_limit(limits.infinite ? -1 : limits.nodes) {
    // "infinite" ignores the clock outright.
    budget_ms = (!limits.infinite && limits.has_clock()) ? time_budget_ms(limits, pos.to_move) : -1;
    has_deadline = budget_ms >= 0;
    deadline = start + std::chrono::milliseconds(has_deadline ? budget_ms : 0);
  }

  uint64_t nodes = 0;
  int seldepth = 0;
  bool stopped = false;
  bool time_checks_enabled = false;

  /// @brief Whether another iteration would only be thrown away.
  bool out_of_budget() {
    if (control && control->stop.load(std::memory_order_relaxed)) return true;
    if (node_limit >= 0 && nodes >= static_cast<uint64_t>(node_limit)) return true;

    return has_deadline && Clock::now() >= deadline;
  }

  int quiescence(int alpha, int beta, int ply) {
    if (ply > seldepth) seldepth = ply;
    if (!bump_node()) return 0;

    // Standing pat: the side to move is never forced to capture, so the
    // static score is a lower bound on what this node is worth.
    int best = evaluate_hce(pos);
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

  // Append the line stored below `ply` by the last completed search.
  void append_pv(std::vector<Move> &out, int ply) const {
    if (ply >= MAX_SEARCH_DEPTH) return;

    for (int i = 0; i < pv_length[ply]; i++) {
      out.push_back(pv_table[ply][i]);
    }
  }

  int negamax(int depth, int ply, int alpha, int beta) {
    if (ply > seldepth) seldepth = ply;

    // Every exit below leaves the line empty unless a move improves alpha.
    if (ply < MAX_SEARCH_DEPTH) pv_length[ply] = 0;

    if (depth == 0) return quiescence(alpha, beta, ply);

    if (!bump_node()) return 0;

    // Draw by fifty-move rule or repetition. Twofold counts as a draw inside
    // the tree: if we can repeat once we can repeat twice.
    if (pos.halfmove_clock >= 100 || pos.is_repetition()) return 0;

    Move tt_move{};
    if (const TT::Entry *entry = TT::probe(pos.hash)) {
      tt_move = entry->move;

      if (entry->depth >= depth) {
        int tt_score = score_from_tt(entry->score, ply);

        if (entry->bound == TT::EXACT || (entry->bound == TT::LOWER && tt_score >= beta)
            || (entry->bound == TT::UPPER && tt_score <= alpha)) {
          return tt_score;
        }
      }
    }

    MoveList moves = generator.generate_legal_moves(pos);
    if (moves.empty()) { return generator.is_in_check(pos, pos.to_move) ? -(MATE_SCORE - ply) : 0; }

    order_quiet_aware(moves, tt_move, ply);

    int alpha_orig = alpha;
    int best = -INF_SCORE;
    Move best_move = moves[0];

    for (const Move &move : moves) {
      pos.make_move(move);
      int score = -negamax(depth - 1, ply + 1, -beta, -alpha);
      pos.undo_move();
      if (stopped) return 0;

      if (score > best) {
        best = score;
        best_move = move;

        // alpha already tracks best, so improving on it means this move heads
        // the best line found here so far.
        if (score > alpha) {
          alpha = score;
          update_pv(move, ply);
        }
      }
      if (alpha >= beta) {
        if (is_quiet(move)) record_cutoff(move, depth, ply);
        break;
      }
    }

    TT::Bound bound = (best >= beta) ? TT::LOWER : (best > alpha_orig ? TT::EXACT : TT::UPPER);
    TT::store(pos.hash, best_move, score_to_tt(best, ply), depth, bound);

    return best;
  }

private:
  Position &pos;
  MoveGenerator generator{};
  SearchControl *control;
  int64_t node_limit;

  Clock::time_point deadline;
  int64_t budget_ms = -1;
  bool has_deadline = false;

  Move killers[MAX_SEARCH_DEPTH][2]{};
  int history[2][64][64]{}; // [side to move][from][to]

  // Triangular table: row `ply` holds the best line found from that ply down,
  // built by prepending the current move to the row below it.
  Move pv_table[MAX_SEARCH_DEPTH][MAX_SEARCH_DEPTH]{};
  int pv_length[MAX_SEARCH_DEPTH]{};

  void update_pv(const Move &move, int ply) {
    if (ply >= MAX_SEARCH_DEPTH) return;

    pv_table[ply][0] = move;

    int child_length = (ply + 1 < MAX_SEARCH_DEPTH) ? pv_length[ply + 1] : 0;
    for (int i = 0; i < child_length && i + 1 < MAX_SEARCH_DEPTH; i++) {
      pv_table[ply][i + 1] = pv_table[ply + 1][i];
    }

    pv_length[ply] = std::min(child_length + 1, MAX_SEARCH_DEPTH);
  }

  static bool is_quiet(const Move &move) { return !move.is_capture() && !move.is_promotion(); }

  // Killers and history reward quiet moves that caused beta cutoffs: killers
  // retry them at the same ply, history at any ply.
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

  void order_quiet_aware(MoveList &moves, const Move &tt_move, int ply) {
    std::stable_sort(
        moves.begin(),
        moves.end(),
        [this, &tt_move, ply](const Move &a, const Move &b) {
          return quiet_aware_score(a, tt_move, ply) > quiet_aware_score(b, tt_move, ply);
        }
    );
  }

  int quiet_aware_score(const Move &move, const Move &tt_move, int ply) {
    if (!is_quiet(move) || move == tt_move) return move_order_score(pos, move, tt_move);

    if (ply < MAX_SEARCH_DEPTH) {
      if (move == killers[ply][0]) return KILLER_ORDER + 1;
      if (move == killers[ply][1]) return KILLER_ORDER;
    }

    return history[pos.to_move][move.from][move.to];
  }

  // Count a node and test the budgets. Returns false once the search should
  // stop.
  bool bump_node() {
    nodes++;

    if (stopped) return false;

    // "stop" is a protocol obligation rather than a budget, so unlike the
    // limits below it is honoured at every depth.
    if (control && control->stop.load(std::memory_order_relaxed)) {
      stopped = true;
      return false;
    }

    // Disabled for depth 1 so a legal best move always exists.
    if (!time_checks_enabled) return true;

    if (node_limit >= 0 && nodes >= static_cast<uint64_t>(node_limit)) {
      stopped = true;
      return false;
    }

    // Reading the clock is comparatively expensive, so sample it rather than
    // testing it on every node.
    if (has_deadline && (nodes & 2047) == 0 && Clock::now() >= deadline) stopped = true;

    return !stopped;
  }
};
} // namespace

SearchResult run_search(
    Position &pos,
    const SearchLimits &limits,
    const SearchInfoCallback &on_iteration,
    SearchControl *control
) {
  Clock::time_point start = Clock::now();
  MoveGenerator generator;

  MoveList root_moves = generator.generate_legal_moves(pos);

  SearchResult result;
  if (root_moves.empty()) return result;

  // Something legal to return before a single node is searched, in case "stop"
  // arrives during the first iteration.
  result.best_move = root_moves[0];

  Searcher searcher(pos, limits, control, start);
  order_moves(pos, root_moves);

  int max_depth = std::clamp(limits.depth, 1, MAX_SEARCH_DEPTH);

  for (int depth = 1; depth <= max_depth; depth++) {
    // Depth 1 runs without time checks so a best move always exists.
    searcher.time_checks_enabled = depth > 1;
    searcher.seldepth = 0;

    SearchResult current;
    current.depth = depth;

    int alpha = -INF_SCORE;
    bool has_move = false;

    for (const Move &move : root_moves) {
      pos.make_move(move);
      int score = -searcher.negamax(depth - 1, 1, -INF_SCORE, -alpha);
      pos.undo_move();
      if (searcher.stopped) break;

      if (!has_move || score > current.score) {
        has_move = true;
        current.score = score;
        current.best_move = move;

        // The root move heads the line; the rest is whatever the child stored.
        current.pv.assign(1, move);
        searcher.append_pv(current.pv, 1);
      }
      alpha = std::max(alpha, score);
    }

    if (searcher.stopped || !has_move) break; // discard the partial iteration

    current.seldepth = std::max(searcher.seldepth, depth);
    current.nodes = searcher.nodes;
    current.elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();

    result = current;
    if (on_iteration) on_iteration(result);

    // Search what this iteration liked first on the next one.
    Move *found = std::find(root_moves.begin(), root_moves.end(), result.best_move);
    if (found != root_moves.end()) std::rotate(root_moves.begin(), found, found + 1);

    // An infinite search stops when the GUI says so and not before, so none of
    // the reasons to leave early apply to it.
    if (limits.infinite) continue;

    // Mate found: deeper iterations cannot improve on it.
    if (is_mate_score(result.score)) break;

    if (searcher.out_of_budget()) break;
  }

  result.nodes = searcher.nodes;
  return result;
}
