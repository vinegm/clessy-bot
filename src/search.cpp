#include "search.hpp"

#include "eval.hpp"
#include "move_gen.hpp"
#include "position.hpp"
#include "tt.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace {
using Clock = std::chrono::steady_clock;

// MVV-LVA: prefer capturing valuable victims with cheap attackers, and try
// promotions early too.
// The king only ever appears as an attacker; just above the queen so a king
// capture sorts last among captures of the same victim but stays inside the
// capture bucket.
constexpr int ORDER_VALUE[7] = {0, 100, 320, 330, 500, 900, 1000};

constexpr int MAX_LMR_MOVES = 64;

/**
 * How much depth a late quiet move gives up, by depth and by position in the
 * move list.
 *
 * Both logarithms matter. Growth in depth is what makes the reduction pay,
 * growth in move index is what keeps it off the moves ordering actually
 * believes in, and taking logs of both is what stops a shallow node from
 * being reduced into nothing.
 */
struct LmrTable {
  int values[MAX_SEARCH_DEPTH + 1][MAX_LMR_MOVES]{};

  LmrTable() {
    for (int depth = 1; depth <= MAX_SEARCH_DEPTH; depth++) {
      for (int move_index = 1; move_index < MAX_LMR_MOVES; move_index++) {
        values[depth][move_index] =
            static_cast<int>(0.75 + std::log(depth) * std::log(move_index) / 2.25);
      }
    }
  }
};

const LmrTable LMR_TABLE;

// Ordering buckets: TT move, then captures and promotions (MVV-LVA), then
// killers, then quiets by history score.
constexpr int TT_ORDER = 1 << 20;
constexpr int CAPTURE_ORDER = 100000;
constexpr int KILLER_ORDER = 99000;

int move_order_score(const Position &pos, const Move &move, const Move &tt_move) {
  if (move == tt_move) return TT_ORDER;

  int score = 0;

  if (move.is_capture()) {
    PieceType victim = move.is_en_passant() ? PAWN : decode_type(pos.lookup_table[move.to()]);
    PieceType attacker = decode_type(pos.lookup_table[move.from()]);
    score += CAPTURE_ORDER + 10 * ORDER_VALUE[victim] - ORDER_VALUE[attacker];
  }

  if (move.is_promotion()) score += CAPTURE_ORDER + ORDER_VALUE[move.promotion_piece()];

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
    // "infinite" ignores the clock outright. A ponder search has one, but it
    // does not start running until the GUI confirms the move with "ponderhit",
    // so no deadline is armed until bump_node sees that flag clear.
    budget_ms = (!limits.infinite && limits.has_clock()) ? time_budget_ms(limits, pos.to_move) : -1;
    has_deadline = budget_ms >= 0;
    deadline = start + std::chrono::milliseconds(has_deadline ? budget_ms : 0);
    ponder_active = control != nullptr && control->pondering.load(std::memory_order_relaxed);
  }

  uint64_t nodes = 0;
  int seldepth = 0;
  bool stopped = false;
  bool time_checks_enabled = false;

  // Whether another iteration would only be thrown away.
  bool out_of_budget() {
    if (control && control->stop.load(std::memory_order_relaxed)) return true;
    if (node_limit >= 0 && nodes >= static_cast<uint64_t>(node_limit)) return true;

    return has_deadline && !ponder_active && Clock::now() >= deadline;
  }

  int quiescence(int alpha, int beta, int ply) {
    if (ply > seldepth) seldepth = ply;
    if (!bump_node()) return 0;

    // Standing pat: the side to move is never forced to capture, so the
    // static score is a lower bound on what this node is worth.
    int best = Eval::evaluate(pos);
    if (best >= beta) return best;
    alpha = std::max(alpha, best);

    MoveList moves = MoveGenerator::generate_legal_captures(pos);
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

  /**
   * @param allow_null False directly under a null move, so the search cannot
   *                   pass twice in a row and prune a subtree it never looked
   *                   at.
   */
  int negamax(int depth, int ply, int alpha, int beta, bool allow_null = true) {
    if (ply > seldepth) seldepth = ply;

    // A window wider than one point means the caller wants an exact score
    // here, not just a bound: this is a node on the principal variation, and
    // the speculative pruning below is not worth the risk on it.
    const bool is_pv = beta - alpha > 1;

    // Every exit below leaves the line empty unless a move improves alpha.
    if (ply < MAX_SEARCH_DEPTH) pv_length[ply] = 0;

    if (depth == 0) return quiescence(alpha, beta, ply);

    if (!bump_node()) return 0;

    // Draw by fifty-move rule or repetition. Twofold counts as a draw inside
    // the tree: if we can repeat once we can repeat twice.
    if (pos.halfmove_clock >= 100 || pos.is_repetition()) return 0;

    Move tt_move{};
    if (const TranspositionTable::Entry *entry = TranspositionTable::probe(pos.hash)) {
      tt_move = entry->move();

      if (entry->depth >= depth) {
        int tt_score = score_from_tt(entry->score, ply);
        TranspositionTable::Bound bound = entry->bound();

        if (bound == TranspositionTable::EXACT
            || (bound == TranspositionTable::LOWER && tt_score >= beta)
            || (bound == TranspositionTable::UPPER && tt_score <= alpha)) {
          return tt_score;
        }
      }
    }

    const bool in_check = MoveGenerator::is_in_check(pos, pos.to_move);

    MoveList moves = MoveGenerator::generate_legal_moves(pos);
    if (moves.empty()) return in_check ? -(MATE_SCORE - ply) : 0;

    // Null move pruning
    if (allow_null && !is_pv && !in_check && depth >= 3
        && pos.has_non_pawn_material(pos.to_move)) {
      const int reduction = 2 + depth / 6;

      pos.make_null_move();
      int score = -negamax(depth - 1 - reduction, ply + 1, -beta, -beta + 1, false);
      pos.undo_move();
      if (stopped) return 0;

      if (score >= beta) return is_mate_score(score) ? beta : score;
    }

    order_quiet_aware(moves, tt_move, ply);

    int alpha_orig = alpha;
    int best = -INF_SCORE;
    Move best_move = moves[0];
    int move_index = 0;

    for (const Move &move : moves) {
      pos.make_move(move);
      int score = search_move(move, depth, ply, alpha, beta, is_pv, in_check, move_index);
      pos.undo_move();
      move_index++;
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

    TranspositionTable::Bound bound = (best >= beta) ? TranspositionTable::LOWER : (best > alpha_orig ? TranspositionTable::EXACT : TranspositionTable::UPPER);
    TranspositionTable::store(pos.hash, best_move, score_to_tt(best, ply), depth, bound);

    return best;
  }

private:
  /**
   * Search one move, with the position already advanced past it.
   *
   * Two ideas share this one place because they share a re-search. Principal
   * variation search assumes the first move is best and asks of every later
   * one only whether it beats that, which a one-point window answers for a
   * fraction of the nodes. Late move reductions go further and assume the
   * tail of a well-ordered list is not merely worse but not worth full depth.
   *
   * Both are speculation, and both are undone the same way: whenever the cheap
   * search comes back above alpha, it was wrong and the move is searched
   * again properly. That is why they cost nothing when ordering is good and
   * only a re-search when it is not.
   */
  int search_move(
      const Move &move,
      int depth,
      int ply,
      int alpha,
      int beta,
      bool is_pv,
      bool in_check,
      int move_index
  ) {
    // The move ordering believes in, measured at full width because
    // everything after it is measured against it.
    if (move_index == 0) return -negamax(depth - 1, ply + 1, -beta, -alpha);

    const int reduction = late_move_reduction(move, depth, ply, move_index, is_pv, in_check);

    int score = -negamax(depth - 1 - reduction, ply + 1, -alpha - 1, -alpha);

    // The reduction was wrong: this move is not tail material after all, so
    // ask the same cheap question again at full depth.
    if (reduction > 0 && score > alpha) {
      score = -negamax(depth - 1, ply + 1, -alpha - 1, -alpha);
    }

    // It beat alpha, and a one-point window cannot say by how much. Inside a
    // real window that difference is the whole answer.
    //
    // At a non-PV node beta is alpha + 1 and this is unreachable, which is
    // the point: only nodes that need an exact score ever pay for one.
    if (score > alpha && score < beta) {
      score = -negamax(depth - 1, ply + 1, -beta, -alpha);
    }

    return score;
  }

  int late_move_reduction(
      const Move &move,
      int depth,
      int ply,
      int move_index,
      bool is_pv,
      bool in_check
  ) {
    // Captures and promotions change material and are why the tail of the
    // list might not be tail at all; shallow nodes have nothing to give back;
    // and in check every move is forced enough to deserve full depth.
    if (depth < 3 || move_index < 3 || in_check || !is_quiet(move)) return 0;

    // The position is already past the move, so this asks whether the move
    // gave check. A forcing move is the opposite of the "probably
    // irrelevant" bet a reduction makes, and it is how mates are delivered:
    // reducing checks costs exact mate distances at the depth that should
    // just prove them.
    if (MoveGenerator::is_in_check(pos, pos.to_move)) return 0;

    int reduction = LMR_TABLE.values[std::min(depth, MAX_SEARCH_DEPTH)]
                                    [std::min(move_index, MAX_LMR_MOVES - 1)];

    // A wrong reduction costs most on the principal variation, and a killer
    // has already produced a cutoff at this exact ply, so neither takes the
    // full reduction.
    if (is_pv) reduction--;
    if (ply < MAX_SEARCH_DEPTH && (move == killers[ply][0] || move == killers[ply][1])) {
      reduction--;
    }

    // Always leave a ply of real search underneath.
    return std::clamp(reduction, 0, depth - 2);
  }

  Position &pos;
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
    int &score = history[pos.to_move][move.from()][move.to()];
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

    return history[pos.to_move][move.from()][move.to()];
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

    // "ponderhit" means the predicted move was played: the search carries on,
    // but from here it is spending the engine's own clock.
    if (ponder_active) {
      if (control && control->pondering.load(std::memory_order_relaxed)) return true;

      ponder_active = false;
      if (has_deadline) deadline = Clock::now() + std::chrono::milliseconds(budget_ms);
    }

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

  // Entries from the previous search stay probeable but become the preferred
  // replacement victims once this one needs their slots.
  TranspositionTable::new_search();

  MoveList root_moves = MoveGenerator::generate_legal_moves(pos);

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

  // Something legal to return before a single node is searched, in case "stop"
  // arrives during the first iteration.
  result.best_move = root_moves[0];

  Searcher searcher(pos, limits, control, start);
  order_moves(pos, root_moves);

  int max_depth = std::clamp(limits.depth, 1, MAX_SEARCH_DEPTH);

  // A mate in x is delivered at ply 2x-1, and that node still needs depth left
  // to see the empty move list rather than falling into quiescence.
  if (limits.mate > 0) {
    max_depth = std::min(max_depth, std::clamp(2 * limits.mate, 1, MAX_SEARCH_DEPTH));
  }

  int lines = std::clamp(limits.multipv, 1, root_moves.count);

  for (int depth = 1; depth <= max_depth; depth++) {
    // Depth 1 runs without time checks so a best move always exists.
    searcher.time_checks_enabled = depth > 1;
    searcher.seldepth = 0;

    std::vector<SearchResult> iteration;
    std::vector<Move> claimed; // root moves already taken by a better line

    for (int line = 0; line < lines; line++) {
      SearchResult current;
      current.depth = depth;
      current.multipv = line + 1;

      int alpha = -INF_SCORE;
      bool has_move = false;

      for (const Move &move : root_moves) {
        if (std::find(claimed.begin(), claimed.end(), move) != claimed.end()) continue;

        pos.make_move(move);

        // Principal variation search at the root as well: the first move is
        // the one the previous iteration liked, and every move after it only
        // has to answer whether it is better, which a one-point window
        // settles far more cheaply. A move that clears alpha is searched
        // again at full width, since the reported score has to be exact.
        int score;
        if (!has_move) {
          score = -searcher.negamax(depth - 1, 1, -INF_SCORE, -alpha);
        } else {
          score = -searcher.negamax(depth - 1, 1, -alpha - 1, -alpha);
          if (score > alpha) score = -searcher.negamax(depth - 1, 1, -INF_SCORE, -alpha);
        }

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

      if (searcher.stopped || !has_move) break;

      current.seldepth = std::max(searcher.seldepth, depth);
      current.nodes = searcher.nodes;
      current.elapsed_ms =
          std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();

      claimed.push_back(current.best_move);
      iteration.push_back(current);
    }

    if (searcher.stopped || iteration.empty()) break; // discard the partial iteration

    result = iteration.front();

    for (const SearchResult &line : iteration) {
      if (on_iteration) on_iteration(line);
    }

    // Search the lines this iteration liked first on the next one, best first.
    for (int i = static_cast<int>(iteration.size()) - 1; i >= 0; i--) {
      Move *found = std::find(root_moves.begin(), root_moves.end(), iteration[i].best_move);
      if (found != root_moves.end()) std::rotate(root_moves.begin(), found, found + 1);
    }

    // An infinite search stops when the GUI says so and not before, so none of
    // the reasons to leave early apply to it.
    if (limits.infinite) continue;

    if (limits.mate > 0 && result.score > 0 && is_mate_score(result.score)
        && mate_distance_moves(result.score) <= limits.mate) {
      break;
    }

    // Mate found: deeper iterations cannot improve on it.
    if (is_mate_score(result.score)) break;

    if (searcher.out_of_budget()) break;
  }

  result.nodes = searcher.nodes;
  return result;
}
