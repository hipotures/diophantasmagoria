#!/usr/bin/env python3
"""Fixed-domain end-to-end worker sweep. No production campaign is started."""
import argparse
import json
import os
from pathlib import Path
import platform
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=Path("build/diophantasmagoria"))
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--threads", default="1,2,4,8,16,24,32")
    parser.add_argument("--repeats", type=int, default=3)
    args = parser.parse_args()
    if args.repeats < 1:
        parser.error("At least one repeat is required")
    args.out.mkdir(parents=True, exist_ok=False)
    config = {"polynomial": "g1", "prime_limit": "97", "factor_counts": [2, 3, 4],
              "m_min": "10", "m_max": "1000000", "k_min": "-256", "k_max": "256"}
    config_path = args.out / "config.json"
    config_path.write_text(json.dumps(config, indent=2) + "\n")
    database = args.out / "roots.jsonl"
    start = time.perf_counter()
    setup = subprocess.run([str(args.exe), "roots", "--polynomial", "g1", "--limit", "97", "--out", str(database)],
                           check=True, text=True, capture_output=True)
    precomputation_wall = time.perf_counter() - start
    cpus = len(os.sched_getaffinity(0))
    threads = sorted(set(int(n) for n in args.threads.split(",") if 0 < int(n) <= cpus))
    if not threads:
        parser.error("No requested worker count fits CPU affinity")
    measurements = []
    expected_candidates = None
    for workers, no_sieve in [(n, False) for n in threads] + [(threads[0], True)]:
        for repeat in range(args.repeats):
            output = args.out / f'workers-{workers}-sieve-{not no_sieve}-repeat-{repeat}'
            command = [str(args.exe), "search", "--config", str(config_path), "--db", str(database),
                       "--out", str(output), "--threads", str(workers)]
            if no_sieve:
                command.append("--no-sieve")
            start = time.perf_counter()
            result = subprocess.run(command, check=True, capture_output=True, text=True)
            wall = time.perf_counter() - start
            report = json.loads(result.stdout)
            count = int(report["counters"]["candidates"])
            if expected_candidates is None:
                expected_candidates = count
            assert report["complete"] == "true" and count == expected_candidates
            report["process_wall_seconds"] = wall
            report["candidates_per_process_second"] = count / wall
            report["repeat"] = str(repeat)
            measurements.append(report)
            print(f'workers={workers} sieve={not no_sieve} repeat={repeat} candidates={count} wall={wall:.4f}s', flush=True)
    environment = {"platform": platform.platform(), "cpu_affinity": sorted(os.sched_getaffinity(0)),
                   "lscpu": subprocess.check_output(["lscpu"], text=True),
                   "precomputation_wall_seconds": precomputation_wall,
                   "precomputation": json.loads(setup.stdout), "configuration": config,
                   "measurements": measurements,
                   "interpretation": "Local bounded fixed domain; no estimate for independent servers or discovery time"}
    (args.out / "benchmark.json").write_text(json.dumps(environment, indent=2) + "\n")


if __name__ == "__main__":
    main()
