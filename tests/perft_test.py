#!/usr/bin/env python3

import subprocess
import time
import re
import argparse


TESTS = [
    (
        "startpos",
        [20, 400, 8902, 197281, 4865609, 119060324, 3195901860],
    ),
    (
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        [48, 2039, 97862, 4085603, 193690690, 8031647685],
    ),
    (
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        [14, 191, 2812, 43238, 674624, 11030083],
    ),
    (
        "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
        [44, 1486, 62379, 2103487, 89941194, 3048196529],
    ),
]


def run_perft(
    engine_path: str, fen: str, depth: int, timeout: int = None
) -> tuple[int, float]:
    """Run perft test on the given engine and return nodes and elapsed time."""
    cmd = [engine_path]
    input_text = "uci\nisready\n"

    if fen == "startpos":
        input_text += "position startpos\n"
    else:
        input_text += f"position fen {fen}\n"

    input_text += f"go perft {depth}\nquit\n"

    start = time.perf_counter()
    try:
        result = subprocess.run(
            cmd, timeout=timeout, input=input_text, text=True, capture_output=True
        )
    except subprocess.TimeoutExpired:
        return -1, -1.0
    elapsed = time.perf_counter() - start

    nodes = 0
    match = re.search(r"Nodes searched: (\d+)", result.stdout)
    if match:
        nodes = int(match.group(1))

    return nodes, elapsed


def check_perft(
    engine: str, fen: str, depth: int, expected: int, timeout: int = None
) -> None:
    """Run perft test and check results."""
    nodes, elapsed = run_perft(engine, fen, depth, timeout=timeout)

    if nodes == -1 and elapsed == -1.0:
        print(f"Timeout at Depth {depth}")
        return

    nps = int(nodes / elapsed) if elapsed > 0 else 0

    if nodes != expected:
        print(f"Failed Depth {depth}: got {nodes}, expected {expected}")
        return

    print(f"Depth {depth}: {nodes} nodes ({elapsed:.3f}s, {nps:,} NPS)")
    return


def parse_args() -> argparse.Namespace:
    """Parse command-line arguments."""
    argparser = argparse.ArgumentParser(
        description="Perft test for UCI-compliant chess engine"
    )

    argparser.add_argument("engine", help="Path to the chess engine executable")
    argparser.add_argument(
        "--depth",
        "-d",
        type=int,
        default=None,
        help="Sets the maximum depth to test (if not specified, runs the maximum depth for the position)",
    )
    argparser.add_argument(
        "--all-depths",
        "-a",
        action="store_true",
        help="Run all depths from 1 up to maximum depth",
    )
    argparser.add_argument(
        "--timeout",
        "-t",
        type=int,
        default=None,
        help="Timeout for each perft test in seconds",
    )

    return argparser.parse_args()


def main() -> None:
    args = parse_args()

    engine = args.engine
    set_max_depth = args.depth
    all_depths = args.all_depths
    timeout = args.timeout

    for i, (fen, expected_res_list) in enumerate(TESTS):
        if i > 0:
            print()
        print(f"Testing: {fen}")

        # Handle run all depths request
        if all_depths:
            for depth, expected in enumerate(expected_res_list, 1):
                if set_max_depth is not None and depth > set_max_depth:
                    break

                check_perft(engine, fen, depth, expected, timeout)
            continue

        # Handle specific depth request
        if set_max_depth is not None:
            expected = expected_res_list[set_max_depth - 1]
            check_perft(engine, fen, set_max_depth, expected, timeout)
            continue

        # Default: run only the maximum depth available for this position
        expected = expected_res_list[-1]
        max_depth = len(expected_res_list)
        check_perft(engine, fen, max_depth, expected, timeout)


if __name__ == "__main__":
    main()
