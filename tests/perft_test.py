#!/usr/bin/env python3

import re
import argparse

from uci_engine import base_parser, run_engine


class testCase:
    def __init__(self, fen: str, expected_results: dict[int, int]):
        self.fen = fen
        self.max_depth = max(expected_results)
        self.expected_results = expected_results

    def cases_to_run(
        self, set_max_depth: int, all_depths: bool
    ) -> list[tuple[int, int]]:
        depths = []

        for depth in range(1, set_max_depth + 1):
            if depth in self.expected_results:
                depths.append(depth)

        if not depths:
            return []

        if not all_depths:
            depths = depths[-1:]

        cases = []
        for depth in depths:
            cases.append((depth, self.expected_results[depth]))

        return cases


TESTS = [
    testCase(
        "startpos",
        {
            1: 20,
            2: 400,
            3: 8_902,
            4: 197_281,
            5: 4_865_609,
            6: 119_060_324,
            7: 3_195_901_860,
            8: 84_998_978_956,
            9: 2_439_530_234_167,
            10: 69_352_859_712_417,
            11: 2_097_651_003_696_806,
        },
    ),
    testCase(
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        {1: 48, 2: 2_039, 3: 97_862, 4: 4_085_603, 5: 193_690_690, 6: 8_031_647_685},
    ),
    testCase(
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        {1: 14, 2: 191, 3: 2_812, 4: 43_238, 5: 674_624, 6: 11_030_083},
    ),
    testCase(
        "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
        {1: 44, 2: 1_486, 3: 62_379, 4: 2_103_487, 5: 89_941_194, 6: 3_048_196_529},
    ),
]


def check_perft(
    engine: str, fen: str, depth: int, expected: int, timeout: int | None = None
) -> bool:
    """Run perft test and check results."""
    print(f"Depth {depth}:", end=" ", flush=True)
    run = run_engine(engine, fen, f"go perft {depth}", timeout)

    if run.timed_out:
        print(f"(Failed) Timed out")
        return False

    match = re.search(r"Nodes searched: (\d+)", run.stdout)
    if match is None:
        print(f"(Failed) engine reported no node count")
        return False

    nodes = int(match.group(1))
    if nodes != expected:
        print(f"(Failed) got {nodes} nodes, expected {expected}")
        return False

    nps = int(nodes / run.elapsed) if run.elapsed > 0 else 0
    print(f"{nodes} nodes ({run.elapsed:.3f}s, {nps:,} NPS)")
    return True


def parse_args() -> argparse.Namespace:
    argparser = base_parser("Perft test for UCI-compliant chess engine")

    argparser.add_argument(
        "--depth",
        "-d",
        type=int,
        default=6,
        help="Sets the maximum depth to test per position (default: 6)",
    )
    argparser.add_argument(
        "--all-depths",
        "-a",
        action="store_true",
        help="Run all depths from 1 up to maximum depth",
    )

    return argparser.parse_args()


def main() -> None:
    args = parse_args()

    passed = 0
    tests_ran = 0
    for i, case in enumerate(TESTS):
        if i > 0:
            print()

        print(f"Testing: {case.fen}")

        for depth, expected in case.cases_to_run(args.depth, args.all_depths):
            tests_ran += 1
            if check_perft(args.engine, case.fen, depth, expected, args.timeout):
                passed += 1

    print(f"\nPassed {passed}/{tests_ran} tests")


if __name__ == "__main__":
    main()
