#!/usr/bin/env python3

import re
import argparse

from uci_engine import base_parser, run_engine


class mateCase:
    def __init__(self, fen: str, mate_in: int, best_moves: list[str] | None = None):
        self.fen = fen
        self.mate_in = mate_in
        self.best_moves = best_moves

    @property
    def depth(self) -> int:
        """Plies needed to see the mate."""
        return 2 * self.mate_in


TESTS = [
    mateCase("6k1/5ppp/8/8/8/8/8/R5K1 w - - 0 1", 1, ["a1a8"]),
    mateCase("r1b2k1r/ppp1bppp/8/1B1Q4/5q2/2P5/PPP2PPP/R3R1K1 w - - 0 1", 2, ["d5d8"]),
    mateCase(
        "2bqkbn1/2pppp2/np2N3/r3P1p1/p2N2B1/5Q2/PPPPKPP1/RNB2r2 w - - 0 1", 2, ["f3f7"]
    ),
    mateCase("r5rk/5p1p/5R2/4B3/8/8/7P/7K w - - 0 1", 3, ["f6a6"]),
]


def parse_mate_in(stdout: str) -> int | None:
    scores = re.findall(r"score (mate|cp) (-?\d+)", stdout)
    if not scores or scores[-1][0] != "mate":
        return None

    return int(scores[-1][1])


def check_mate(engine: str, case: mateCase, timeout: int | None = None) -> bool:
    run = run_engine(engine, case.fen, f"go depth {case.depth}", timeout)

    if run.timed_out:
        print(f"Timeout at depth {case.depth}")
        return False

    best_move = ""
    match = re.search(r"bestmove (\S+)", run.stdout)
    if match:
        best_move = match.group(1)

    mate_in = parse_mate_in(run.stdout)

    if mate_in is None:
        print(f"Failed: no mate found at depth {case.depth}, played {best_move}")
        return False

    if mate_in != case.mate_in:
        print(f"Failed: got mate in {mate_in}, expected mate in {case.mate_in}")
        return False

    if case.best_moves is not None and best_move not in case.best_moves:
        expected = " or ".join(case.best_moves)
        print(
            f"Failed: mate in {mate_in} found, but played {best_move}, expected {expected}"
        )
        return False

    print(f"Mate in {mate_in}: {best_move} (depth {case.depth}, {run.elapsed:.3f}s)")
    return True


def parse_args() -> argparse.Namespace:
    argparser = base_parser("Mate search test for UCI-compliant chess engine")

    return argparser.parse_args()


def main() -> None:
    args = parse_args()

    passed = 0
    for i, case in enumerate(TESTS):
        if i > 0:
            print()

        print(f"Testing: {case.fen}")
        if check_mate(args.engine, case, args.timeout):
            passed += 1

    print(f"\nPassed {passed}/{len(TESTS)} tests")


if __name__ == "__main__":
    main()
