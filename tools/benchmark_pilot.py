#!/usr/bin/env python3
"""Bounded production-shaped worker sweep with a reusable, declared root cache."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]


def main(production=False):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=Path("build/diophantasmagoria"))
    parser.add_argument("--config", type=Path, default=ROOT / ("configs/campaign-g1.json" if production else "configs/pilot-g1.json"))
    parser.add_argument("--db", type=Path, help="Reuse an existing complete cache without regenerating it")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--threads", default="1,2,4,8,16,24,32")
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--max-tasks", type=int, default=5000000 if production else 20000)
    parser.add_argument("--shard", default="0/3")
    parser.add_argument("--chunk-tiles", type=int, default=256)
    parser.add_argument("--queue-chunks", type=int, default=0)
    parser.add_argument("--checkpoint-tiles", type=int, default=262144)
    parser.add_argument("--checkpoint-seconds", type=float, default=2)
    args = parser.parse_args()
    if args.repeats < 1 or args.max_tasks < 1:
        parser.error("Positive repeats and task budget required")
    cpus = len(os.sched_getaffinity(0))
    workers = sorted({int(n) for n in args.threads.split(",") if 0 < int(n) <= cpus})
    if not workers:
        parser.error("No worker count fits CPU affinity")
    config = json.loads(args.config.read_text())
    args.out.mkdir(parents=True, exist_ok=False)
    config_path = args.out / "config.json"
    config_path.write_text(json.dumps(config, indent=2) + "\n")
    database = args.db or args.out / "roots.jsonl"
    precomputation = None
    precomputation_wall = None
    if args.db is None:
        start = time.perf_counter()
        setup = subprocess.run([str(args.exe), "roots", "--polynomial", config["polynomial"],
                                "--limit", str(config["prime_limit"]), "--out", str(database)],
                               check=True, text=True, capture_output=True)
        precomputation_wall = time.perf_counter() - start
        precomputation = json.loads(setup.stdout)
        print(f"Precomputation wall: {precomputation_wall:.3f}s", flush=True)
    with database.open() as stream:
        database_header = json.loads(stream.readline())
    common = [str(args.exe), "search", "--config", str(config_path), "--db", str(database),
              "--shard", args.shard, "--chunk-tiles", str(args.chunk_tiles),
              "--queue-chunks", str(args.queue_chunks), "--checkpoint-tiles", str(args.checkpoint_tiles),
              "--checkpoint-seconds", str(args.checkpoint_seconds)]
    # A separately bounded trace demonstrates where the generator starts after pruning.
    sample_dir = args.out / "first-tile"
    sample = json.loads(subprocess.check_output(common + ["--out", str(sample_dir), "--max-tasks", "1",
                                                        "--threads", "1", "--trace"], text=True))
    with (sample_dir / "coverage.jsonl").open() as stream:
        sample_points = [json.loads(line) for line in stream]
    assert sample_points and all(int(row["m"]) % 2 for row in sample_points)
    assert all(int(config["m_min"]) <= int(row["m"]) <= int(config["m_max"]) for row in sample_points)
    measurements, expected = [], None
    count_keys = ("tasks", "moduli", "roots", "candidates", "nonnegative", "survivors", "squares", "hits",
                  "native_tasks", "big_tasks")
    # Rotate order between repeats to avoid always measuring one count first.
    for repeat in range(args.repeats):
        order = workers[repeat % len(workers):] + workers[:repeat % len(workers)]
        for count in order:
            directory = args.out / f"workers-{count}-repeat-{repeat}"
            start = time.perf_counter()
            report = json.loads(subprocess.check_output(common + ["--out", str(directory), "--threads", str(count),
                                                                  "--max-tasks", str(args.max_tasks)], text=True))
            wall = time.perf_counter() - start
            cursor = json.loads((directory / "checkpoint.json").read_text())["payload"]["cursor"]
            work = ([report["counters"][key] for key in count_keys], cursor, report["domain"])
            if expected is None:
                expected = work
            assert work == expected, "Worker counts did not search identical work"
            assert int(report["counters"]["tasks"]) == args.max_tasks and report["complete"] == "false"
            assert int(report["counters"]["survivors"]) > 0, "Pilot did not reach sieve-admissible candidates"
            report["process_wall_seconds"] = wall
            report["candidates_per_search_second"] = int(report["counters"]["candidates"]) / float(report["search_seconds"])
            report["final_cursor_sha256"] = hashlib.sha256(json.dumps(cursor, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
            report["tasks_per_search_second"] = int(report["counters"]["tasks"]) / float(report["search_seconds"])
            report["repeat"] = str(repeat)
            measurements.append(report)
            (args.out / "measurements.json").write_text(json.dumps(measurements, indent=2) + "\n")
            print(f'workers={count} repeat={repeat} setup={float(report["setup_seconds"]):.3f}s '
                  f'search={float(report["search_seconds"]):.3f}s wall={wall:.3f}s '
                  f'candidates={report["counters"]["candidates"]} survivors={report["counters"]["survivors"]}', flush=True)
    environment = {"platform": platform.platform(), "cpu_affinity": sorted(os.sched_getaffinity(0)),
                   "lscpu": subprocess.check_output(["lscpu"], text=True), "configuration": config,
                   "cache_reused": args.db is not None, "database_header": database_header,
                   "precomputation_wall_seconds": precomputation_wall, "precomputation": precomputation,
                   "first_tile_report": sample, "first_candidate": sample_points[0],
                   "measurements": measurements,
                   "interpretation": "Fixed deterministic partial-shard prefix; cache coverage explicitly declared; no server scaling or discovery prediction"}
    (args.out / "benchmark.json").write_text(json.dumps(environment, indent=2) + "\n")


if __name__ == "__main__":
    main()
