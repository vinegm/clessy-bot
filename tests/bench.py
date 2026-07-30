#!/usr/bin/env python3
"""Fixed-depth search bench and mate regression suite.

Reports total nodes, time and nps per position, which is what to compare
across a change that is meant to search the same tree faster or a smaller
tree for the same answer. Run it before and after; a node count that moves
without a matching change in best move or score is the thing to explain.

Unlike the other suites this one keeps a single engine alive across all
positions and drains its stdout on a background thread. A plain pipe closes
stdin, the engine reads EOF and quits, and the search is stopped before it
ever reports a bestmove -- a `go depth 8` piped in comes back instantly with
depth 0 and no info lines at all.

  python3 tests/bench.py ./build/clessy -d 8 --mates
  python3 tests/bench.py ./build/clessy -d 8 --nnue path/to/net.nnue
"""
import argparse
import re
import subprocess
import sys
import threading
import time

POSITIONS = [
    ("startpos", "position startpos"),
    ("kiwipete", "position fen r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"),
    ("endgame", "position fen 8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1"),
    ("promo", "position fen rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8"),
    ("midgame", "position fen r1bqkb1r/pp3ppp/2n1pn2/2pp4/3P1B2/2PBPN2/PP3PPP/RN1QK2R w KQkq - 0 7"),
    ("tactical", "position fen r2q1rk1/pP1p2pp/Q4n2/bbp1p3/Np6/1B3NBn/pPPP1PPP/R3K2R b KQ - 0 1"),
]

# fen, mate distance in moves. Pinned from the pre-change engine at depths 6
# and 8; a distance stable across both is forced rather than a horizon effect.
MATES = [
    ("r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5Q2/PPPP1PPP/RNB1K1NR w KQkq - 0 1", 1),
    ("7k/6pp/8/8/8/8/8/R6K w - - 0 1", 1),
    ("8/8/8/8/8/1k6/2q5/K7 b - - 0 1", 1),
    ("2bqkbn1/2pppp2/np2N3/r3P1p1/p2N2B1/5Q2/PPPPKPP1/RNB2r2 w - - 0 1", 2),
    ("6k1/pp4p1/2p5/2bp4/8/P5Pb/1P3rrP/2BRRN1K b - - 0 1", 2),
    ("r5rk/5p1p/5R2/4B3/8/8/7P/7K w - - 0 1", 3),
    ("3r1r1k/1p3p1p/p2p4/4n1NN/6bQ/1BPq4/P3p1PP/1R5K w - - 0 1", 3),
]


class Session:
    """A live engine process whose stdout is drained on a background thread."""

    def __init__(self, engine):
        self.proc = subprocess.Popen(
            [engine], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL, text=True, bufsize=1,
        )
        self.lines = []
        self.lock = threading.Lock()
        self.reader = threading.Thread(target=self._drain, daemon=True)
        self.reader.start()

    def _drain(self):
        for line in self.proc.stdout:
            with self.lock:
                self.lines.append(line.rstrip("\n"))

    def send(self, text):
        self.proc.stdin.write(text if text.endswith("\n") else text + "\n")
        self.proc.stdin.flush()

    def wait_for(self, prefix, timeout):
        """Wait until a line starting with `prefix` appears. Returns True on success."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            with self.lock:
                if any(l.startswith(prefix) for l in self.lines):
                    return True
            if self.proc.poll() is not None:
                return False
            time.sleep(0.005)
        return False

    def snapshot(self):
        with self.lock:
            return list(self.lines)

    def reset(self):
        with self.lock:
            self.lines = []

    def close(self):
        try:
            self.send("quit")
            self.proc.wait(timeout=5)
        except Exception:
            self.proc.kill()


def search(session, setup, go, timeout):
    session.reset()
    session.send(setup)
    start = time.time()
    session.send(go)
    finished = session.wait_for("bestmove", timeout)
    if not finished:
        session.send("stop")
        session.wait_for("bestmove", 10)
    return session.snapshot(), time.time() - start


def last_info(lines):
    depth = nodes = nps = seldepth = 0
    score = None
    best = None
    for line in lines:
        if line.startswith("bestmove"):
            parts = line.split()
            if len(parts) > 1:
                best = parts[1]
            continue
        if not line.startswith("info depth"):
            continue
        m = re.search(r"depth (\d+)", line)
        if m:
            depth = max(depth, int(m.group(1)))
        m = re.search(r"seldepth (\d+)", line)
        if m:
            seldepth = max(seldepth, int(m.group(1)))
        m = re.search(r"nodes (\d+)", line)
        if m:
            nodes = max(nodes, int(m.group(1)))
        m = re.search(r"nps (\d+)", line)
        if m:
            nps = int(m.group(1))
        m = re.search(r"score (cp|mate) (-?\d+)", line)
        if m:
            score = (m.group(1), int(m.group(2)))
    return depth, seldepth, nodes, nps, score, best


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("engine")
    ap.add_argument("-d", "--depth", type=int, default=9)
    ap.add_argument("-t", "--timeout", type=float, default=120.0)
    ap.add_argument("--mates", action="store_true", help="also run the mate suite")
    ap.add_argument("--nnue", help="load this network before benching")
    args = ap.parse_args()

    session = Session(args.engine)
    session.send("uci")
    session.wait_for("uciok", 10)
    if args.nnue:
        session.send(f"setoption name EvalFile value {args.nnue}")
    session.send("isready")
    session.wait_for("readyok", 10)

    rc = 0
    total_nodes = 0
    total_time = 0.0
    print(f"{'position':<10} {'depth':>5} {'seldep':>6} {'nodes':>12} {'time':>8} {'nps':>10}  best   score")
    for name, setup in POSITIONS:
        lines, elapsed = search(session, setup, f"go depth {args.depth}", args.timeout)
        depth, seldepth, nodes, nps, score, best = last_info(lines)
        total_nodes += nodes
        total_time += elapsed
        s = f"{score[0]} {score[1]}" if score else "-"
        print(f"{name:<10} {depth:>5} {seldepth:>6} {nodes:>12,} {elapsed:>7.2f}s {nps:>10,}  {best:<6} {s}")

    print()
    print(f"total nodes {total_nodes:,}   total time {total_time:.2f}s   "
          f"nps {int(total_nodes / total_time) if total_time else 0:,}")

    if args.mates:
        print()
        failures = 0
        for fen, want in MATES:
            lines, elapsed = search(
                session, f"position fen {fen}", f"go depth {2 * want}", args.timeout
            )
            _, _, _, _, score, best = last_info(lines)
            ok = score is not None and score[0] == "mate" and score[1] == want
            failures += 0 if ok else 1
            got = f"{score[0]} {score[1]}" if score else "none"
            print(f"[{'ok' if ok else 'FAIL'}] mate in {want}: got {got} ({best}) {elapsed:.2f}s")
        if failures:
            print(f"{failures} mate failure(s)")
            rc = 1

    session.close()
    return rc


if __name__ == "__main__":
    sys.exit(main())
