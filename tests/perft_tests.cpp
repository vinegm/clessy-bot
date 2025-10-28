#include "engine.hpp"

#include <criterion/criterion.h>
#include <vector>

void test_perft_position(ClessEngine &board, const std::vector<uint64_t> &expected_results) {
  for (int depth = 1; depth <= expected_results.size(); depth++) {
    uint64_t nodes = board.perft(depth);
    cr_assert_eq(
        nodes,
        expected_results[depth - 1],
        "Perft mismatch at depth %d: got %lu, expected %lu",
        depth,
        nodes,
        expected_results[depth - 1]
    );
  }
}

Test(perft, initial_position) {
  ClessEngine board(true);
  test_perft_position(board, {20, 400, 8902, 197281, 4865609, 119060324});
}

Test(perft, Kiwipete) {
  ClessEngine board("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", true);
  test_perft_position(board, {48, 2039, 97862, 4085603, 193690690}); //, 8031647685});
}

Test(perft, pos3) {
  ClessEngine board("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", true);
  test_perft_position(board, {14, 191, 2812, 43238, 674624, 11030083});
}

Test(perft, pos4) {
  ClessEngine board("rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", true);
  test_perft_position(board, {44, 1486, 62379, 2103487, 89941194});
}
