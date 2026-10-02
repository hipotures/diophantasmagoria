#!/usr/bin/env python3
"""Bounded, reproducible symmetric prefixes with independent witness checks."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import statistics
import subprocess
import time
from symmetric import verify_record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", default="build/diophantasmagoria")
    parser.add_argument("--config", type=Path, nargs="+", required=True)
    parser.add_argument("--db", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--threads", type=int, default=4)
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--max-tasks", type=int, default=1000000)
    parser.add_argument("--seconds", type=float, default=30)
    parser.add_argument("--compare-factor-sieve", action="store_true",
                        help="Research only: requires the report's factor-sieve-prototype.patch")
    args = parser.parse_args()
    if min(args.threads,args.repeats,args.max_tasks,args.seconds) <= 0:
        parser.error("Positive worker, repeat, task and time budgets required")
    args.out.mkdir(parents=True,exist_ok=False)
    measurements,reference = [],{}
    for repeat in range(args.repeats):
        for index,cfg in enumerate(args.config):
            modes = [False,True] if args.compare_factor_sieve else [False]
            if repeat%2: modes.reverse()
            for enabled in modes:
                out = args.out/f"config-{index}-repeat-{repeat}-local-{int(enabled)}"
                command = [args.exe,"search","--config",str(cfg),"--db",str(args.db),
                           "--out",str(out),"--threads",str(args.threads),
                           "--max-tasks",str(args.max_tasks),"--seconds",str(args.seconds),
                           "--progress-seconds","0"]
                if enabled: command.append("--factor-sieve")
                start = time.perf_counter()
                process = subprocess.run(command,text=True,capture_output=True,check=True,
                                         timeout=max(60,args.seconds+30))
                wall = time.perf_counter()-start
                report = json.loads(process.stdout)
                cursor = json.loads((out/"checkpoint.json").read_text())["payload"]["cursor"]
                rows = [json.loads(line) for line in (out/"results.jsonl").read_text().splitlines()]
                witnesses = {verify_record(row) for row in rows}
                counters = report["counters"]
                assert (int(counters["tasks"])==args.max_tasks or report["complete"]=="true"), \
                    "Time budget reached; lower --max-tasks for identical-prefix comparisons"
                identity = ([counters[k] for k in ("tasks","moduli","roots","candidates","nonnegative")],
                            cursor,report["domain"],witnesses)
                if index in reference: assert identity==reference[index],"Different arithmetic coverage"
                else: reference[index]=identity
                report["process_wall_seconds"] = wall
                report["candidates_per_wall_second"] = int(counters["candidates"])/wall
                report["candidates_per_search_second"] = int(counters["candidates"])/float(report["search_seconds"])
                report["final_cursor_sha256"] = hashlib.sha256(json.dumps(cursor,sort_keys=True).encode()).hexdigest()
                report["configuration"] = json.loads(cfg.read_text())
                report["configuration_path"] = str(cfg)
                report["command"] = command
                report["repeat"] = repeat
                report["local_filter_enabled"] = enabled
                measurements.append(report)
                (args.out/"measurements.json").write_text(json.dumps(measurements,indent=2)+"\n")
                print(f"{cfg.name} repeat={repeat} local={enabled} wall={wall:.3f}s "
                      f"rate={report['candidates_per_wall_second']/1e6:.2f}M/s "
                      f"squares={counters['squares']}",flush=True)
    summary = []
    for cfg in args.config:
        row = {"configuration_path":str(cfg)}
        for enabled in ([False,True] if args.compare_factor_sieve else [False]):
            chosen = [r for r in measurements if r["configuration_path"]==str(cfg)
                      and r["local_filter_enabled"]==enabled]
            row["factor" if enabled else "fixed"] = {
                "median_candidates_per_wall_second":statistics.median(r["candidates_per_wall_second"] for r in chosen),
                "min_candidates_per_wall_second":min(r["candidates_per_wall_second"] for r in chosen),
                "max_candidates_per_wall_second":max(r["candidates_per_wall_second"] for r in chosen)}
        summary.append(row)
    environment = {"platform":platform.platform(),"cpu_affinity":sorted(os.sched_getaffinity(0)),
                   "threads":args.threads,"max_tasks":args.max_tasks,"seconds_cap":args.seconds,
                   "summary":summary,"measurements":measurements,
                   "scope":"Deterministic bounded prefixes; no full-campaign timing guarantee"}
    (args.out/"benchmark.json").write_text(json.dumps(environment,indent=2)+"\n")


if __name__=="__main__":
    main()
