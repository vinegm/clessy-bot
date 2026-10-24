#include "search.hpp"

#include "attacks.hpp"
#include "eval.hpp"
#include "move_gen.hpp"
#include "position.hpp"
#include "transposition_table.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>

using Clock = std::chrono::steady_clock;

// NOTE: the king sits above the queen so it sorts last as an attacker
constexpr int ORDER_VALUE[7] = {0, 100, 320, 330, 500, 900, 1000};

constexpr int HASH_MOVE_ORDER = 1 << 20;
constexpr int CAPTURE_ORDER = 100000;
constexpr int KILLER_ORDER = 99000;

constexpr int MAX_REDUCED_MOVES = 64;

// NOTE: the king is worth nothing here, it can only ever be the last piece to capture
constexpr int SEE_VALUE[7] = {0, 100, 320, 330, 500, 900, 0};

// NOTE: both colours, bishops and rooks both include the queens
static uint64_t attackers_to(
    const Position &pos,
    Square square,
    uint64_t occupancy,
    uint64_t bishops,
    uint64_t rooks
) {
  return (Attacks::pawn_attacks(BLACK, square) & pos.pieces(WHITE, PAWN))
         | (Attacks::pawn_attacks(WHITE, square) & pos.pieces(BLACK, PAWN))
         | (Attacks::knight_attacks(square)
            & (pos.pieces(WHITE, KNIGHT) | pos.pieces(BLACK, KNIGHT)))
         | (Attacks::king_attacks(square) & (pos.pieces(WHITE, KING) | pos.pieces(BLACK, KING)))
         | (Attacks::bishop_attacks(square, occupancy) & bishops)
         | (Attacks::rook_attacks(square, occupancy) & rooks);
}

// NOTE: static exchange evaluation, whether trading off everything that attacks the target
// square leaves the mover at least threshold up. Each side captures with its least valuable
// piece and may stop when that is better. Pins are ignored, as is usual, for speed
static bool see_at_least(const Position &pos, const Move &move, int threshold) {
  if (move.is_castling()) return threshold <= 0;

  const Square from = move.from();
  const Square to = move.to();

  int balance = -threshold;
  if (move.is_en_passant()) balance += SEE_VALUE[PAWN];
  else if (move.is_capture()) balance += SEE_VALUE[decode_type(pos.lookup_table[to])];

  PieceType victim = decode_type(pos.lookup_table[from]);
  if (move.is_promotion()) {
    victim = move.promotion_piece();
    balance += SEE_VALUE[victim] - SEE_VALUE[PAWN];
  }

  if (balance < 0) return false;

  balance -= SEE_VALUE[victim];
  if (balance >= 0) return true;

  uint64_t occupancy = (pos.occupancy[ANY_COLOR] ^ (1ULL << from)) | (1ULL << to);
  if (move.is_en_passant()) {
    occupancy ^= 1ULL << indexes_to_square(square_rank(from), square_file(to));
  }

  const uint64_t bishops = pos.pieces(WHITE, BISHOP) | pos.pieces(BLACK, BISHOP)
                           | pos.pieces(WHITE, QUEEN) | pos.pieces(BLACK, QUEEN);
  const uint64_t rooks = pos.pieces(WHITE, ROOK) | pos.pieces(BLACK, ROOK)
                         | pos.pieces(WHITE, QUEEN) | pos.pieces(BLACK, QUEEN);

  uint64_t attackers = attackers_to(pos, to, occupancy, bishops, rooks) & occupancy;
  Color side = opposite_color(pos.to_move);

  while (true) {
    const uint64_t ours = attackers & pos.occupancy[side];
    if (ours == 0) break;

    PieceType attacker = PAWN;
    while (attacker < KING && (ours & pos.pieces(side, attacker)) == 0) {
      attacker = PieceType(attacker + 1);
    }

    occupancy ^= 1ULL << lsb_square(ours & pos.pieces(side, attacker));

    // NOTE: whatever stood behind the capturing piece joins in
    if (attacker == PAWN || attacker == BISHOP || attacker == QUEEN) {
      attackers |= Attacks::bishop_attacks(to, occupancy) & bishops;
    }
    if (attacker == ROOK || attacker == QUEEN) {
      attackers |= Attacks::rook_attacks(to, occupancy) & rooks;
    }
    attackers &= occupancy;

    side = opposite_color(side);
    balance = -balance - 1 - SEE_VALUE[attacker];

    if (balance >= 0) {
      // NOTE: the king can't take into a square that is still defended
      if (attacker == KING && (attackers & pos.occupancy[side]) != 0) side = opposite_color(side);
      break;
    }
  }

  return side != pos.to_move;
}

struct LateMoveReductions {
  int values[Search::MAX_DEPTH + 1][MAX_REDUCED_MOVES]{};

  LateMoveReductions() noexcept {
    for (int depth = 1; depth <= Search::MAX_DEPTH; depth++) {
      for (int move_index = 1; move_index < MAX_REDUCED_MOVES; move_index++) {
        values[depth][move_index] =
            static_cast<int>(0.75 + std::log(depth) * std::log(move_index) / 2.25);
      }
    }
  }
};

static const LateMoveReductions LATE_MOVE_REDUCTIONS;

class Searcher {
public:
  Searcher(Position &pos, const SearchLimits &limits, SearchControl *control) :
      pos(pos), limits(limits), control(control), start(Clock::now()) {
    if (!limits.infinite) node_limit = limits.nodes;
    if (!limits.infinite && limits.has_clock()) budget_ms = time_budget_ms(limits, pos.to_move);

    has_deadline = budget_ms >= 0;
    deadline = start;
    if (has_deadline) deadline += std::chrono::milliseconds(budget_ms);

    // NOTE: a ponder search only arms its deadline on ponderhit
    ponder_active = control != nullptr && control->pondering.load(std::memory_order_relaxed);
  }

  SearchResult run(const SearchInfoCallback &on_iteration) {
    TranspositionTable::new_search();

    MoveList root_moves =
        restrict_root_moves(MoveGenerator::generate_legal_moves(pos), limits.searchmoves);

    SearchResult result;
    if (root_moves.empty()) return result;

    // NOTE: something legal to return if stop arrives during the first iteration
    result.best_move = root_moves[0];
    order_moves(pos, root_moves);

    const int lines = std::clamp(limits.multipv, 1, root_moves.count);

    for (int depth = 1; depth <= max_depth(limits); depth++) {
      std::vector<SearchResult> iteration = iterate(depth, lines, root_moves);
      if (stopped || iteration.empty()) break;

      result = iteration.front();
      report(iteration, on_iteration);
      rotate_best_lines(root_moves, iteration);

      if (limits.infinite) continue;
      if (Search::is_mate_score(result.score)) break;
      if (out_of_budget()) break;
    }

    result.nodes = nodes;

    return result;
  }

private:
  std::vector<SearchResult> iterate(int depth, int lines, const MoveList &root_moves) {
    // NOTE: depth 1 always finishes, so a best move always exists
    time_checks_enabled = depth > 1;
    seldepth = 0;

    std::vector<SearchResult> iteration;
    std::vector<Move> claimed;

    for (int line = 0; line < lines; line++) {
      std::optional<SearchResult> current = search_line(depth, line, root_moves, claimed);
      if (!current.has_value()) break;

      claimed.push_back(current->best_move);
      iteration.push_back(*current);
    }

    return iteration;
  }

  // NOTE: one MultiPV line, the best move that an earlier line didn't claim
  std::optional<SearchResult> search_line(
      int depth,
      int line,
      const MoveList &root_moves,
      const std::vector<Move> &claimed
  ) {
    SearchResult current;
    current.depth = depth;
    current.multipv = line + 1;

    int alpha = -Search::INFINITE_SCORE;
    bool has_move = false;

    for (const Move &move : root_moves) {
      if (std::find(claimed.begin(), claimed.end(), move) != claimed.end()) continue;

      pos.make_move(move);

      int score = 0;
      if (has_move) score = -negamax(depth - 1, 1, -alpha - 1, -alpha);
      if (!has_move || score > alpha)
        score = -negamax(depth - 1, 1, -Search::INFINITE_SCORE, -alpha);

      pos.undo_move();
      if (stopped) break;

      alpha = std::max(alpha, score);

      if (has_move && score <= current.score) continue;

      has_move = true;
      current.score = score;
      current.best_move = move;
      current.pv.assign(1, move);
      append_pv(current.pv, 1);
    }

    if (stopped || !has_move) return std::nullopt;

    current.seldepth = std::max(seldepth, depth);
    current.nodes = nodes;
    current.elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();

    return current;
  }

  static void
      report(const std::vector<SearchResult> &iteration, const SearchInfoCallback &on_iteration) {
    if (!on_iteration) return;

    for (const SearchResult &line : iteration) {
      on_iteration(line);
    }
  }

  // NOTE: the next iteration searches this one's best lines first, best first
  static void rotate_best_lines(MoveList &root_moves, const std::vector<SearchResult> &iteration) {
    for (int i = static_cast<int>(iteration.size()) - 1; i >= 0; i--) {
      Move *found = std::find(root_moves.begin(), root_moves.end(), iteration[i].best_move);
      if (found != root_moves.end()) std::rotate(root_moves.begin(), found, found + 1);
    }
  }

  uint64_t nodes = 0;
  int seldepth = 0;
  bool stopped = false;
  bool time_checks_enabled = false;

  bool out_of_budget() {
    if (control && control->stop.load(std::memory_order_relaxed)) return true;
    if (node_limit >= 0 && nodes >= static_cast<uint64_t>(node_limit)) return true;

    return has_deadline && !ponder_active && Clock::now() >= deadline;
  }

  int quiescence(int alpha, int beta, int ply) {
    if (ply > seldepth) seldepth = ply;
    if (!count_node()) return 0;
    if (ply >= Search::MAX_DEPTH) return Eval::evaluate(pos);

    // NOTE: standing pat in check would pretend the threat can be ignored, so every
    // evasion is searched instead, and having none is mate
    const bool in_check = MoveGenerator::is_in_check(pos, pos.to_move);

    int best = -(Search::MATE_SCORE - ply);
    if (!in_check) {
      best = Eval::evaluate(pos);
      if (best >= beta) return best;
      alpha = std::max(alpha, best);
    }

    MoveList moves;
    if (in_check) moves = MoveGenerator::generate_legal_moves(pos);
    else moves = MoveGenerator::generate_legal_captures(pos);
    order_moves(pos, moves);

    for (const Move &move : moves) {
      // NOTE: a capture that loses material can't raise a stand pat score
      if (!in_check && !see_at_least(pos, move, 0)) continue;

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
    if (ply >= Search::MAX_DEPTH) return;

    for (int i = 0; i < pv_length[ply]; i++) {
      out.push_back(pv_table[ply][i]);
    }
  }

  // NOTE: allow_null is false right under a null move, so the search never passes twice
  int negamax(int depth, int ply, int alpha, int beta, bool allow_null = true) {
    if (ply > seldepth) seldepth = ply;

    const bool is_pv = beta - alpha > 1;

    if (ply < Search::MAX_DEPTH) pv_length[ply] = 0;

    const bool in_check = MoveGenerator::is_in_check(pos, pos.to_move);

    // NOTE: a side in check is searched a ply deeper, so a check at the horizon is
    // answered by the full search rather than by quiescence
    if (in_check) depth++;

    if (depth <= 0) return quiescence(alpha, beta, ply);
    if (ply >= Search::MAX_DEPTH) return Eval::evaluate(pos);

    if (!count_node()) return 0;

    // NOTE: twofold counts as a draw, if we can repeat once we can repeat twice
    if (pos.halfmove_clock >= 100 || pos.is_repetition()) return 0;

    Move hash_move{};
    if (const TranspositionTable::Entry *entry = TranspositionTable::probe(pos.hash)) {
      hash_move = entry->move();

      if (entry->depth >= depth
          && is_usable(*entry, score_from_table(entry->score, ply), alpha, beta)) {
        return score_from_table(entry->score, ply);
      }
    }

    MovePicker picker(*this, hash_move, ply);

    // Null move pruning
    // NOTE: only with pieces left, a side down to pawns is where zugzwang lives. A
    // stalemated side has nothing to pass on, so it needs a legal move first
    if (allow_null && !is_pv && !in_check && depth >= 3 && pos.has_non_pawn_material(pos.to_move)
        && picker.has_move()) {
      const int reduction = 2 + depth / 6;

      pos.make_null_move();
      int score = -negamax(depth - 1 - reduction, ply + 1, -beta, -beta + 1, false);
      pos.undo_move();
      if (stopped) return 0;

      // NOTE: a mate proven by a free move is only a bound, never a distance
      if (score >= beta && Search::is_mate_score(score)) return beta;

      if (score >= beta) return score;
    }

    int original_alpha = alpha;
    int best = -Search::INFINITE_SCORE;
    Move best_move{};
    int move_index = 0;

    Move move;
    while (picker.next(move)) {
      pos.make_move(move);
      int score = search_move(move, depth, ply, alpha, beta, is_pv, in_check, move_index);
      pos.undo_move();
      move_index++;
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

    if (move_index == 0 && in_check) return -(Search::MATE_SCORE - ply);
    if (move_index == 0) return 0;

    TranspositionTable::Bound bound = TranspositionTable::UPPER;
    if (best > original_alpha) bound = TranspositionTable::EXACT;
    if (best >= beta) bound = TranspositionTable::LOWER;

    TranspositionTable::store(pos.hash, best_move, score_to_table(best, ply), depth, bound);

    return best;
  }

  Position &pos;
  const SearchLimits &limits;
  SearchControl *control;
  Clock::time_point start;
  int64_t node_limit = -1;

  Clock::time_point deadline;
  int64_t budget_ms = -1;
  bool has_deadline = false;
  bool ponder_active = false;

  Move killers[Search::MAX_DEPTH][2]{};
  int history[2][64][64]{}; // [Color][from][to]

  // NOTE: row ply holds the best line from that ply down
  Move pv_table[Search::MAX_DEPTH][Search::MAX_DEPTH]{};
  int pv_length[Search::MAX_DEPTH]{};

  static bool is_usable(const TranspositionTable::Entry &entry, int score, int alpha, int beta) {
    if (entry.bound() == TranspositionTable::EXACT) return true;
    if (entry.bound() == TranspositionTable::LOWER) return score >= beta;
    if (entry.bound() == TranspositionTable::UPPER) return score <= alpha;

    return false;
  }

  // NOTE: the position is already past the move
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
    if (move_index == 0) return -negamax(depth - 1, ply + 1, -beta, -alpha);

    const int reduction = late_move_reduction(move, depth, ply, move_index, is_pv, in_check);

    int score = -negamax(depth - 1 - reduction, ply + 1, -alpha - 1, -alpha);

    if (reduction > 0 && score > alpha) {
      score = -negamax(depth - 1, ply + 1, -alpha - 1, -alpha);
    }

    // NOTE: only reachable on PV nodes, where beta isn't alpha + 1
    if (score > alpha && score < beta) { score = -negamax(depth - 1, ply + 1, -beta, -alpha); }

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
    if (depth < 3 || move_index < 3 || in_check || !is_quiet(move)) return 0;

    // NOTE: reducing checks loses exact mate distances
    if (MoveGenerator::is_in_check(pos, pos.to_move)) return 0;

    int reduction = LATE_MOVE_REDUCTIONS.values[std::min(depth, Search::MAX_DEPTH)]
                                               [std::min(move_index, MAX_REDUCED_MOVES - 1)];

    if (is_pv) reduction--;
    if (ply < Search::MAX_DEPTH && (move == killers[ply][0] || move == killers[ply][1])) {
      reduction--;
    }

    return std::clamp(reduction, 0, depth - 2);
  }

  void update_pv(const Move &move, int ply) {
    if (ply >= Search::MAX_DEPTH) return;

    pv_table[ply][0] = move;

    int child_length = 0;
    if (ply + 1 < Search::MAX_DEPTH) child_length = pv_length[ply + 1];

    for (int i = 0; i < child_length && i + 1 < Search::MAX_DEPTH; i++) {
      pv_table[ply][i + 1] = pv_table[ply + 1][i];
    }

    pv_length[ply] = std::min(child_length + 1, Search::MAX_DEPTH);
  }

  static bool is_quiet(const Move &move) { return !move.is_capture() && !move.is_promotion(); }

  void record_cutoff(const Move &move, int depth, int ply) {
    // NOTE: capped so history never outranks killers or captures
    int &score = history[pos.to_move][move.from()][move.to()];
    score = std::min(score + depth * depth, KILLER_ORDER - 1);

    if (ply < Search::MAX_DEPTH && move != killers[ply][0]) {
      killers[ply][1] = killers[ply][0];
      killers[ply][0] = move;
    }
  }

  // NOTE: the hash move, then captures and promotions, then quiets, then the captures that
  // lose material, each stage generated only once the one before it ran out without a
  // cutoff. Quiets are scored when their stage begins, so they see the history the earlier
  // subtrees left behind
  class MovePicker {
  public:
    MovePicker(Searcher &searcher, const Move &hash_move, int ply) :
        searcher(searcher), pos(searcher.pos), masks(MoveGenerator::generate_masks(searcher.pos)),
        hash_move(hash_move), ply(ply) {}

    // NOTE: whether a move is left, without handing it out
    bool has_move() {
      while (true) {
        if (index < moves.size()) {
          if (stage != Stage::Hash && hash_given && moves[index] == hash_move) {
            index++;
            continue;
          }

          return true;
        }

        if (!next_stage()) return false;
      }
    }

    bool next(Move &move) {
      if (!has_move()) return false;

      move = moves[index++];
      return true;
    }

  private:
    enum class Stage {
      Start,
      Hash,
      Noisy,
      Quiet,
      BadNoisy,
      Done
    };

    Searcher &searcher;
    const Position &pos;
    const MoveGenerator::Masks masks;
    const Move hash_move;
    const int ply;

    Stage stage = Stage::Start;
    MoveList moves;
    MoveList bad_noisy;
    int index = 0;
    bool hash_given = false;
    // NOTE: keeps the order the moves were sorted in, on both sides of the split
    void split_bad_noisy() {
      int kept = 0;
      for (const Move move : moves) {
        if (move.is_capture() && !move.is_promotion() && !see_at_least(pos, move, 0)) {
          bad_noisy.add_move(move);
          continue;
        }

        moves[kept++] = move;
      }

      moves.count = kept;
    }

    bool next_stage() {
      index = 0;
      moves.count = 0;

      switch (stage) {
        case Stage::Start:
          stage = Stage::Hash;
          hash_given = !hash_move.is_null() && MoveGenerator::is_legal(pos, masks, hash_move);
          if (hash_given) moves.add_move(hash_move);
          return true;

        case Stage::Hash:
          stage = Stage::Noisy;
          moves = MoveGenerator::generate_noisy(pos, masks);
          sort_by_score(moves, [this](const Move &move) {
            return move_order_score(pos, move, hash_move);
          });
          split_bad_noisy();
          return true;

        case Stage::Noisy:
          stage = Stage::Quiet;
          moves = MoveGenerator::generate_quiets(pos, masks);
          sort_by_score(moves, [this](const Move &move) {
            return searcher.quiet_score(move, ply);
          });
          return true;

        case Stage::Quiet:
          stage = Stage::BadNoisy;
          moves = bad_noisy;
          return true;

        default: stage = Stage::Done; return false;
      }
    }
  };

  // NOTE: scores each move once, a comparator rescores both sides of every comparison.
  // Insertion sort keeps equal scores in generation order, as stable_sort did, without
  // the heap buffer stable_sort allocates at every node
  template<typename Score>
  static void sort_by_score(MoveList &moves, Score score) {
    int scores[MAX_POSSIBLE_LEGAL_MOVES];
    for (int i = 0; i < moves.size(); i++) {
      scores[i] = score(moves[i]);
    }

    for (int i = 1; i < moves.size(); i++) {
      const Move move = moves[i];
      const int value = scores[i];

      int j = i - 1;
      while (j >= 0 && scores[j] < value) {
        scores[j + 1] = scores[j];
        moves[j + 1] = moves[j];
        j--;
      }

      scores[j + 1] = value;
      moves[j + 1] = move;
    }
  }

  int quiet_score(const Move &move, int ply) const {
    if (ply < Search::MAX_DEPTH) {
      if (move == killers[ply][0]) return KILLER_ORDER + 1;
      if (move == killers[ply][1]) return KILLER_ORDER;
    }

    return history[pos.to_move][move.from()][move.to()];
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
  static int move_order_score(const Position &pos, const Move &move, const Move &hash_move) {
    if (move == hash_move) return HASH_MOVE_ORDER;

    int score = 0;

    if (move.is_capture()) {
      PieceType victim = PAWN;
      if (!move.is_en_passant()) victim = decode_type(pos.lookup_table[move.to()]);

      PieceType attacker = decode_type(pos.lookup_table[move.from()]);
      score += CAPTURE_ORDER + 10 * ORDER_VALUE[victim] - ORDER_VALUE[attacker];
    }

    if (move.is_promotion()) score += CAPTURE_ORDER + ORDER_VALUE[move.promotion_piece()];

    return score;
  }

  static void order_moves(const Position &pos, MoveList &moves) {
    const Move no_hash_move{};

    sort_by_score(moves, [&pos, &no_hash_move](const Move &move) {
      return move_order_score(pos, move, no_hash_move);
    });
  }

  // NOTE: mate scores are relative to the root while searching, but to the node in the table
  static int score_to_table(int score, int ply) {
    if (!Search::is_mate_score(score)) return score;
    if (score < 0) return score - ply;

    return score + ply;
  }

  static int score_from_table(int score, int ply) {
    if (!Search::is_mate_score(score)) return score;
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

  static MoveList
      restrict_root_moves(const MoveList &root_moves, const std::vector<Move> &allowed) {
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
  static int max_depth(const SearchLimits &limits) {
    int depth = std::clamp(limits.depth, 1, Search::MAX_DEPTH);

    // NOTE: the mating ply still needs depth left to see the empty move list
    if (limits.mate > 0) depth = std::min(depth, std::clamp(2 * limits.mate, 1, Search::MAX_DEPTH));

    return depth;
  }
};

SearchResult Search::run(
    Position &pos,
    const SearchLimits &limits,
    const SearchInfoCallback &on_iteration,
    SearchControl *control
) {
  return Searcher(pos, limits, control).run(on_iteration);
}
