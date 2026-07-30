#!/usr/bin/env python3
"""Check the incremental NNUE accumulator against from-scratch evaluation.

The accumulator is folded forward in add_piece and unfolded in remove_piece,
so a wrong delta does not fail loudly -- it drifts, and the engine simply
evaluates positions wrongly from that point on. Nothing in the perft or mate
suites would notice.

Two properties pin it down, and both need a search to run first, because the
incremental path only engages once something has evaluated: a plain
`position fen` followed by `eval` is a refresh and proves nothing.

  eval before a search == eval after one
      make_move and undo_move are exact inverses on the accumulator, so a
      search that makes and unmakes millions of moves leaves it where it
      started.

  eval after a search == a reference binary's eval
      the incremental values are the right ones, not merely stable ones.

The reference is any build that recomputes -- the commit before the
incremental change, or a build with the refresh made unconditional.

  python3 tests/nnue_accumulator_test.py ./build/clessy ./build-ref/clessy net.nnue
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bench import Session, search

FENS = [
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
    "r1bqkb1r/pp3ppp/2n1pn2/2pp4/3P1B2/2PBPN2/PP3PPP/RN1QK2R w KQkq - 0 7",
    # black to move: the perspective flip and sq ^ 56
    "r2q1rk1/pP1p2pp/Q4n2/bbp1p3/Np6/1B3NBn/pPPP1PPP/R3K2R b KQ - 0 1",
    "8/8/8/8/8/1k6/2q5/K7 b - - 0 1",
    # en passant available
    "rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3",
]


def eval_of(session, fen, search_depth=None):
    session.reset()
    session.send(f"position fen {fen}")
    if search_depth:
        search(session, f"position fen {fen}", f"go depth {search_depth}", 120)
    session.reset()
    session.send("eval")
    if not session.wait_for("Eval", 10):
        return "NO-EVAL"
    for line in session.snapshot():
        if line.startswith("Eval"):
            return line.strip()
    return "NO-EVAL"


def open_session(binary, net):
    session = Session(binary)
    session.send("uci")
    session.wait_for("uciok", 10)
    session.send(f"setoption name EvalFile value {net}")
    session.send("isready")
    session.wait_for("readyok", 10)
    return session


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("engine")
    parser.add_argument("reference", help="a build that recomputes the accumulator")
    parser.add_argument("net", help="path to a .nnue network")
    parser.add_argument("-d", "--depth", type=int, default=6)
    args = parser.parse_args()

    engine = open_session(args.engine, args.net)
    reference = open_session(args.reference, args.net)

    failures = 0
    for fen in FENS:
        before = eval_of(engine, fen)
        after = eval_of(engine, fen, search_depth=args.depth)
        expected = eval_of(reference, fen)

        ok = before == after == expected and before != "NO-EVAL"
        failures += 0 if ok else 1
        print(f"[{'ok ' if ok else 'FAIL'}] {fen[:44]:<44} {before} | {after} | {expected}")

    engine.close()
    reference.close()

    if failures:
        print(f"\n{failures} mismatch(es)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
