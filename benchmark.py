#!/usr/bin/env python3
"""
UNSW Battlecode Multithreaded / Multiprocess Benchmark Runner

Runs head-to-head matches across all maps between two bots in parallel,
collecting and displaying the winner, victory method, round number, and match duration.
"""

import argparse
import concurrent.futures
import glob
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass
from typing import List, Optional, Tuple


@dataclass
class MatchResult:
    map_name: str
    bot_a: str
    bot_b: str
    winner: str
    loser: str
    method: str
    round_num: int
    duration: float
    status: str
    error: str = ""


# ANSI Color formatting
class Colors:
    GREEN = "\033[92m"
    RED = "\033[91m"
    YELLOW = "\033[93m"
    BLUE = "\033[94m"
    CYAN = "\033[96m"
    BOLD = "\033[1m"
    RESET = "\033[0m"


def find_unswbc(explicit_path: Optional[str] = None) -> str:
    """Finds the unswbc binary executable."""
    if explicit_path and os.path.isfile(explicit_path) and os.access(explicit_path, os.X_OK):
        return os.path.abspath(explicit_path)

    candidates = [
        shutil.which("unswbc"),
        os.path.expanduser("~/.local/bin/unswbc"),
        "/usr/local/bin/unswbc",
        "/home/charitra-jain/.local/bin/unswbc",
    ]
    for candidate in candidates:
        if candidate and os.path.isfile(candidate) and os.access(candidate, os.X_OK):
            return os.path.abspath(candidate)

    raise FileNotFoundError(
        "Could not find 'unswbc' executable. Please ensure it is in your PATH or specify via --unswbc."
    )


def prepare_worker_bot_dir(source_dir: str, target_dir: str):
    """
    Creates an isolated copy/symlink of the bot directory for each worker,
    including a private copy of `.unswbc-build` so workers do not recompile C++ 32 times
    and do not collide on `.unswbc-build`.
    """
    os.makedirs(target_dir, exist_ok=True)
    abs_src = os.path.abspath(source_dir)
    for item in os.listdir(abs_src):
        if item in ("build", ".git", "__pycache__"):
            continue
        s_path = os.path.join(abs_src, item)
        t_path = os.path.join(target_dir, item)
        if not os.path.exists(t_path):
            if item == ".unswbc-build" and os.path.isdir(s_path):
                shutil.copytree(s_path, t_path)
                continue
            try:
                os.symlink(s_path, t_path)
            except OSError:
                if os.path.isdir(s_path):
                    shutil.copytree(s_path, t_path)
                else:
                    shutil.copy2(s_path, t_path)


def run_single_match(
    task_id: int,
    unswbc_path: str,
    temp_dir: str,
    map_path: str,
    bot_a: str,
    bot_b: str,
    bot_a_name: str,
    bot_b_name: str,
    timeout: int = 60,
) -> MatchResult:
    """Executes a single match between bot_a (Team A) and bot_b (Team B) on map_path."""
    map_name = os.path.basename(map_path)
    worker_bot_a = os.path.join(temp_dir, f"w_{task_id}", bot_a_name)
    worker_bot_b = os.path.join(temp_dir, f"w_{task_id}", bot_b_name)

    prepare_worker_bot_dir(bot_a, worker_bot_a)
    prepare_worker_bot_dir(bot_b, worker_bot_b)

    cmd = [unswbc_path, "run", "--no-replay", map_path, worker_bot_a, worker_bot_b]
    t0 = time.time()

    try:
        res = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=timeout,
        )
        duration = time.time() - t0

        output = res.stdout + "\n" + res.stderr
        
        # Regex to match victory line: "team A wins after 47 rounds (by elimination) (0.2s)"
        match = re.search(
            r"team\s+([AB])\s+wins\s+after\s+(\d+)\s+rounds\s+\(([^)]+)\)",
            output,
        )

        if match:
            winning_team = match.group(1)
            round_num = int(match.group(2))
            method = match.group(3).strip()
            winner = bot_a_name if winning_team == "A" else bot_b_name
            loser = bot_b_name if winning_team == "A" else bot_a_name
            return MatchResult(
                map_name=map_name,
                bot_a=bot_a_name,
                bot_b=bot_b_name,
                winner=winner,
                loser=loser,
                method=method,
                round_num=round_num,
                duration=duration,
                status="OK",
            )

        # Check for draw
        draw_match = re.search(r"draw\s+after\s+(\d+)\s+rounds", output, re.IGNORECASE)
        if draw_match:
            round_num = int(draw_match.group(1))
            return MatchResult(
                map_name=map_name,
                bot_a=bot_a_name,
                bot_b=bot_b_name,
                winner="DRAW",
                loser="DRAW",
                method="draw",
                round_num=round_num,
                duration=duration,
                status="DRAW",
            )

        # Failure / unexpected output
        first_err = res.stderr.strip() or res.stdout.strip()
        first_err_line = first_err.splitlines()[-1] if first_err else "unknown error"
        return MatchResult(
            map_name=map_name,
            bot_a=bot_a_name,
            bot_b=bot_b_name,
            winner="ERROR",
            loser="ERROR",
            method="error",
            round_num=0,
            duration=duration,
            status="ERROR",
            error=first_err_line,
        )

    except subprocess.TimeoutExpired:
        duration = time.time() - t0
        return MatchResult(
            map_name=map_name,
            bot_a=bot_a_name,
            bot_b=bot_b_name,
            winner="TIMEOUT",
            loser="TIMEOUT",
            method="timeout",
            round_num=0,
            duration=duration,
            status="TIMEOUT",
            error=f"Exceeded timeout of {timeout}s",
        )
    except Exception as exc:
        duration = time.time() - t0
        return MatchResult(
            map_name=map_name,
            bot_a=bot_a_name,
            bot_b=bot_b_name,
            winner="ERROR",
            loser="ERROR",
            method="error",
            round_num=0,
            duration=duration,
            status="ERROR",
            error=str(exc),
        )


def main():
    parser = argparse.ArgumentParser(
        description="Run high-speed multithreaded Battlecode benchmarks across all maps."
    )
    parser.add_argument("bot1", nargs="?", default="v3", help="Directory of Bot 1 (default: v3)")
    parser.add_argument("bot2", nargs="?", default="v2", help="Directory of Bot 2 (default: v2)")
    parser.add_argument("--maps-dir", default="maps", help="Directory containing map files (default: maps)")
    parser.add_argument("--map", default=None, help="Filter for specific map name (e.g. 'arena' or 'trophy')")
    parser.add_argument(
        "--single-side",
        action="store_true",
        help="Only run bot1 as Team A vs bot2 as Team B (by default both sides are run)",
    )
    parser.add_argument(
        "-j",
        "--workers",
        type=int,
        default=None,
        help="Number of concurrent worker threads (default: min(cpu_count, 16))",
    )
    parser.add_argument("--timeout", type=int, default=120, help="Per-match timeout in seconds (default: 120)")
    parser.add_argument("--unswbc", default=None, help="Path to unswbc executable")
    parser.add_argument("--no-color", action="store_true", help="Disable colored terminal output")

    args = parser.parse_args()

    # Color configuration
    use_color = not args.no_color and sys.stdout.isatty()
    C_GREEN = Colors.GREEN if use_color else ""
    C_RED = Colors.RED if use_color else ""
    C_YELLOW = Colors.YELLOW if use_color else ""
    C_BLUE = Colors.BLUE if use_color else ""
    C_CYAN = Colors.CYAN if use_color else ""
    C_BOLD = Colors.BOLD if use_color else ""
    C_RESET = Colors.RESET if use_color else ""

    # Locate unswbc
    try:
        unswbc_path = find_unswbc(args.unswbc)
    except FileNotFoundError as e:
        print(f"{C_RED}Error:{C_RESET} {e}", file=sys.stderr)
        sys.exit(1)

    # Locate bot directories
    bot1_path = os.path.abspath(args.bot1)
    bot2_path = os.path.abspath(args.bot2)
    bot1_name = os.path.basename(bot1_path.rstrip("/\\"))
    bot2_name = os.path.basename(bot2_path.rstrip("/\\"))

    if not os.path.isdir(bot1_path):
        print(f"{C_RED}Error:{C_RESET} Bot 1 directory '{args.bot1}' not found.", file=sys.stderr)
        sys.exit(1)
    if not os.path.isdir(bot2_path):
        print(f"{C_RED}Error:{C_RESET} Bot 2 directory '{args.bot2}' not found.", file=sys.stderr)
        sys.exit(1)

    # Locate map files
    if not os.path.isdir(args.maps_dir):
        print(f"{C_RED}Error:{C_RESET} Maps directory '{args.maps_dir}' not found.", file=sys.stderr)
        sys.exit(1)

    map_pattern = os.path.join(args.maps_dir, "*.map")
    map_files = sorted(glob.glob(map_pattern))

    if args.map:
        filter_str = args.map.lower()
        map_files = [m for m in map_files if filter_str in os.path.basename(m).lower()]

    if not map_files:
        print(f"{C_RED}Error:{C_RESET} No map files found matching '{args.map or map_pattern}'.", file=sys.stderr)
        sys.exit(1)

    # Build match task list
    tasks = []
    task_id = 0
    for map_path in map_files:
        # Match 1: bot1 (Team A) vs bot2 (Team B)
        tasks.append((task_id, map_path, bot1_path, bot2_path, bot1_name, bot2_name))
        task_id += 1

        if not args.single_side:
            # Match 2: bot2 (Team A) vs bot1 (Team B)
            tasks.append((task_id, map_path, bot2_path, bot1_path, bot2_name, bot1_name))
            task_id += 1

    # Worker count
    max_workers = args.workers or min(os.cpu_count() or 4, len(tasks), 4)

    print(f"\n{C_BOLD}{C_CYAN}========================================================================{C_RESET}")
    print(f"{C_BOLD} BATTLECODE BENCHMARK: {C_GREEN}{bot1_name}{C_RESET}{C_BOLD} vs {C_YELLOW}{bot2_name}{C_RESET}")
    print(f"{C_CYAN}========================================================================{C_RESET}")
    print(f"Maps:     {len(map_files)} ({', '.join(os.path.basename(m) for m in map_files)})")
    print(f"Matches:  {len(tasks)} ({'single-sided' if args.single_side else 'both sides: A vs B & B vs A'})")
    print(f"Engine:   {unswbc_path}")
    print(f"Threads:  {max_workers} parallel workers")
    print(f"{C_CYAN}------------------------------------------------------------------------{C_RESET}")

    # Pre-compile bot1 and bot2 once in-place so parallel workers reuse `.unswbc-build`
    print(f"Building bots ({bot1_name}, {bot2_name})...")
    subprocess.run(
        [unswbc_path, "run", "--no-replay", map_files[0], bot1_path, bot2_path],
        capture_output=True,
        text=True,
        timeout=args.timeout,
    )
    print("")

    # Create temporary directory for worker bot isolation
    temp_dir = tempfile.mkdtemp(prefix="bc_workers_")

    results: List[MatchResult] = []
    t_start = time.time()

    try:
        with concurrent.futures.ThreadPoolExecutor(max_workers=max_workers) as executor:
            future_to_task = {
                executor.submit(
                    run_single_match,
                    tid,
                    unswbc_path,
                    temp_dir,
                    m_path,
                    b_a,
                    b_b,
                    b_a_name,
                    b_b_name,
                    args.timeout,
                ): (tid, m_path, b_a_name, b_b_name)
                for (tid, m_path, b_a, b_b, b_a_name, b_b_name) in tasks
            }

            completed = 0
            for future in concurrent.futures.as_completed(future_to_task):
                completed += 1
                res = future.result()
                results.append(res)

                # Live progress line
                if res.status == "OK":
                    win_color = C_GREEN if res.winner == bot1_name else C_YELLOW
                    print(
                        f"[{completed:2d}/{len(tasks)}] {C_GREEN}PASS{C_RESET} | "
                        f"{res.map_name:20} | {res.bot_a} (A) vs {res.bot_b} (B) -> "
                        f"Winner: {win_color}{res.winner:6}{C_RESET} | "
                        f"{res.method} on round {res.round_num:3d} ({res.duration:4.1f}s)"
                    )
                elif res.status == "DRAW":
                    print(
                        f"[{completed:2d}/{len(tasks)}] {C_BLUE}DRAW{C_RESET} | "
                        f"{res.map_name:20} | {res.bot_a} (A) vs {res.bot_b} (B) -> "
                        f"Draw after {res.round_num} rounds ({res.duration:4.1f}s)"
                    )
                else:
                    print(
                        f"[{completed:2d}/{len(tasks)}] {C_RED}FAIL{C_RESET} | "
                        f"{res.map_name:20} | {res.bot_a} (A) vs {res.bot_b} (B) -> "
                        f"Error: {res.error} ({res.duration:4.1f}s)"
                    )

    finally:
        shutil.rmtree(temp_dir, ignore_errors=True)
        # Clean up any leftover replays in current working directory
        for r_file in glob.glob("*.replay"):
            try:
                os.remove(r_file)
            except OSError:
                pass

    total_time = time.time() - t_start

    # Sort results by map name, then team matchup
    results.sort(key=lambda r: (r.map_name, r.bot_a != bot1_name))

    # Print Summary Table
    print(f"\n{C_BOLD}{C_CYAN}==================================================================================================={C_RESET}")
    print(f"{C_BOLD}{'MAP':22} {'MATCHUP':22} {'WINNER':12} {'METHOD':20} {'TURN':6} {'TIME':8} {'STATUS':6}{C_RESET}")
    print(f"{C_CYAN}---------------------------------------------------------------------------------------------------{C_RESET}")

    bot1_wins = 0
    bot2_wins = 0
    draws = 0
    errors = 0

    for r in results:
        matchup = f"{r.bot_a} (A) vs {r.bot_b} (B)"
        if r.status == "OK":
            if r.winner == bot1_name:
                bot1_wins += 1
                win_str = f"{C_GREEN}{r.winner}{C_RESET}"
                status_str = f"{C_GREEN}PASS{C_RESET}"
            elif r.winner == bot2_name:
                bot2_wins += 1
                win_str = f"{C_YELLOW}{r.winner}{C_RESET}"
                status_str = f"{C_YELLOW}PASS{C_RESET}"
            else:
                win_str = r.winner
                status_str = f"{C_BLUE}PASS{C_RESET}"
            turn_str = str(r.round_num)
        elif r.status == "DRAW":
            draws += 1
            win_str = f"{C_BLUE}DRAW{C_RESET}"
            status_str = f"{C_BLUE}DRAW{C_RESET}"
            turn_str = str(r.round_num)
        else:
            errors += 1
            win_str = f"{C_RED}ERROR{C_RESET}"
            status_str = f"{C_RED}FAIL{C_RESET}"
            turn_str = "-"

        time_str = f"{r.duration:4.1f}s"
        print(f"{r.map_name:22} {matchup:22} {win_str:21} {r.method:20} {turn_str:6} {time_str:8} {status_str}")

    print(f"{C_BOLD}{C_CYAN}==================================================================================================={C_RESET}")
    print(f"{C_BOLD}OVERALL SUMMARY:{C_RESET}")
    print(f"  Total Matches:   {len(results)}")
    pct_b1 = (bot1_wins / len(results) * 100) if results else 0
    pct_b2 = (bot2_wins / len(results) * 100) if results else 0
    print(f"  {C_BOLD}{bot1_name:10}{C_RESET} Wins: {C_GREEN}{bot1_wins:2d}{C_RESET} ({pct_b1:5.1f}%)")
    print(f"  {C_BOLD}{bot2_name:10}{C_RESET} Wins: {C_YELLOW}{bot2_wins:2d}{C_RESET} ({pct_b2:5.1f}%)")
    if draws > 0:
        print(f"  Draws:           {C_BLUE}{draws:2d}{C_RESET}")
    if errors > 0:
        print(f"  Errors:          {C_RED}{errors:2d}{C_RESET}")
    print(f"  Total Benchmark Time: {C_BOLD}{total_time:.2f}s{C_RESET} across {max_workers} workers")
    print(f"{C_BOLD}{C_CYAN}==================================================================================================={C_RESET}\n")


if __name__ == "__main__":
    main()
