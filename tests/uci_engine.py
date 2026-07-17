#!/usr/bin/env python3

"""Shared helpers for driving the engine over UCI as a black box."""

import argparse
import subprocess
import time


class engineRun:
    """
    Result of a single engine invocation.

    A run that timed out carries no output, so check `timed_out` before
    reading `stdout`.
    """

    def __init__(self, stdout: str, elapsed: float, timed_out: bool = False):
        self.stdout = stdout
        self.elapsed = elapsed
        self.timed_out = timed_out


def position_command(fen: str) -> str:
    """UCI position command for a FEN, or for the startpos shorthand."""
    if fen == "startpos":
        return "position startpos"

    return f"position fen {fen}"


def run_engine(
    engine_path: str, fen: str, command: str, timeout: int | None = None
) -> engineRun:
    """
    Set up a position and run one command against a fresh engine process.

    Each call spawns its own process, so state that outlives a command
    (transposition table, killers, history) cannot leak between test cases.
    """
    input_text = (
        "uci\n"
        "isready\n"
        f"{position_command(fen)}\n"
        f"{command}\n"
        "quit\n"
    )

    start = time.perf_counter()
    try:
        result = subprocess.run(
            [engine_path],
            timeout=timeout,
            input=input_text,
            text=True,
            capture_output=True,
        )
    except subprocess.TimeoutExpired:
        return engineRun("", time.perf_counter() - start, timed_out=True)

    return engineRun(result.stdout, time.perf_counter() - start)


def base_parser(description: str) -> argparse.ArgumentParser:
    """Argument parser carrying the options every suite takes."""
    argparser = argparse.ArgumentParser(description=description)

    argparser.add_argument("engine", help="Path to the chess engine executable")
    argparser.add_argument(
        "--timeout",
        "-t",
        type=int,
        default=None,
        help="Timeout for each test in seconds",
    )

    return argparser
