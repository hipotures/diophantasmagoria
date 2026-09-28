#!/usr/bin/env python3
"""End-to-end coverage, recovery, provenance and witness regression tests."""
import json
import itertools
import math
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from oracle import coefficients, coordinate_box, evaluate, records, structured, verify_record
from merge import merge

EXE = Path(sys.argv[1]).resolve()
WORK = ROOT / "Experiments" / ("test-" + str(os.getpid()))
WORK.mkdir(parents=True, exist_ok=False)


def run(*args, ok=True):
    result = subprocess.run([str(EXE), *map(str, args)], capture_output=True, text=True)
    if (result.returncode == 0) != ok:
        raise AssertionError(f"Command {args}: {result.returncode}\n{result.stdout}\n{result.stderr}")
    return result


def write(name, data):
    path = WORK / name
    path.write_text(json.dumps(data))
    return path


def hitset(directory):
    return {verify_record(row) for row in records(directory / "results.jsonl")}


def coverage(directory):
    return {(int(row["m"]), int(row["r"]), int(row["k"]), int(row["sign"]))
            for row in records(directory / "coverage.jsonl")}


def search(name, config, db, *args, ok=True):
    directory = WORK / name
    result = run("search", "--config", config, "--db", db, "--out", directory, *args, ok=ok)
    return directory, result


def taskset(directory):
    return {row["task"] for row in records(directory / "coverage.jsonl")}


def filtered_targets():
    # Independent local certificate, with a positive control for generic even moduli.
    def feasible(a):
        return any((y + z) % 4 == 2 and (y*z*(y+z) - evaluate(a, x)) % 16 == 0
                   for x in range(16) for y in range(16) for z in range(16))
    assert not feasible([-1, 3, 1, 1]) and not feasible([1, 0, 1, 6])
    assert feasible([0, -1, 0, 1])
    for name in ("g1", "g2"):
        db = WORK / f"{name}-filtered.roots.jsonl"
        if name == "g1":
            # A byte-for-byte pre-fix database must remain reusable, with its row for 2.
            shutil.copyfile(ROOT / "tests/fixtures/pre-filter-g1-roots97.jsonl", db)
        else:
            run("roots", "--polynomial", name, "--limit", 43, "--out", db)
        original_database = db.read_bytes()
        assert next(row for row in records(db) if row.get("prime") == "2")["roots"] == ["1"]
        data = {"polynomial": name, "prime_limit": "31", "factor_counts": [1, 2, 3, 4],
                "m_min": "2", "m_max": "100000", "k_min": "-2", "k_max": "2"}
        # Test both generator and explicit-list routing; explicit all-excluded is below.
        for mode in ("generated", "explicit"):
            current = dict(data)
            if mode == "explicit":
                odd = [int(row["prime"]) for row in records(db)
                       if "prime" in row and 2 < int(row["prime"]) <= 31 and row["roots"]]
                current["moduli"] = [[2], [2, odd[0]], [2, *odd[:2]], [2, *odd[:3]],
                                     odd[:1], odd[:2], odd[:3]]
            config = write(f"{name}-{mode}.json", current)
            hits, unfiltered = structured(current)
            admissible = {point for point in unfiltered if point[0] % 2}
            excluded = unfiltered - admissible
            assert admissible and excluded and not (admissible & excluded)
            assert unfiltered == admissible | excluded
            assert all((y + z) % 2 for x, y, z in hits)
            one, _ = search(f"{name}-{mode}-one", config, db, "--trace")
            # Match all unfiltered oracle solutions, but account separately for excluded candidates.
            assert hitset(one) == hits and coverage(one) == admissible
            report = json.loads((one / "report.json").read_text())
            assert int(report["counters"]["candidates"]) == len(admissible)
            assert int(report["counters"]["moduli"]) == len({point[0] for point in admissible})
            assert report["analytical_exclusions"]["generated_moduli_excluded"] == "not_enumerated"
            assert int(report["analytical_exclusions"]["explicit_moduli_excluded"]) == (4 if mode == "explicit" else 0)
            custom = dict(current, coefficients=coefficients(current))
            del custom["polynomial"]
            custom_config = write(f"{name}-{mode}-coefficients.json", custom)
            many, _ = search(f"{name}-{mode}-many", custom_config, db, "--threads", 4, "--trace")
            assert hitset(many) == hits and coverage(many) == admissible
            assert taskset(one) == taskset(many)
            assert json.loads((many / "manifest.json").read_text())["domain"] == report["domain"]
            union, work_union, shards = set(), set(), []
            for shard in range(3):
                directory, _ = search(f"{name}-{mode}-shard{shard}", config, db, "--trace", "--threads", 2, "--shard", f"{shard}/3")
                points, work = coverage(directory), taskset(directory)
                assert not union & points and not work_union & work
                assert all(m % 2 for m, r, k, sign in points)
                union |= points
                work_union |= work
                shards.append(directory)
            assert union == admissible and work_union == taskset(one)
            summary, merged = merge(shards)
            assert summary["complete"] and {verify_record(row) for row in merged} == hits
            # Replay a completed batch against a saved filtered cursor; threads may change.
            replay, _ = search(f"{name}-{mode}-replay", config, db, "--trace", "--max-tasks", 2)
            checkpoint = (replay / "checkpoint.json").read_bytes()
            search(f"{name}-{mode}-replay", config, db, "--trace", "--resume", "--threads", 3)
            (replay / "checkpoint.json").write_bytes(checkpoint)
            search(f"{name}-{mode}-replay", config, db, "--trace", "--resume", "--threads", 2)
            assert coverage(replay) == admissible and hitset(replay) == hits
        # g2's smallest admissible four-factor modulus exceeds the small oracle's cap.
        # Trial residue progressions provide a separate bounded reference, without CRT.
        a = coefficients(data)
        root_lists = {p: [x for x in range(p) if evaluate(a, x) % p == 0]
                      for p in range(2, 44) if all(p % d for d in range(2, math.isqrt(p) + 1))}
        prime_list = [p for p, rs in root_lists.items() if rs]
        reference, reference_hits, factor_counts = set(), set(), {}
        for count in (3, 4):
            for factors in itertools.combinations(prime_list, count):
                m = math.prod(factors)
                if m > 1000000:
                    continue
                factor_counts[m] = count
                p = factors[0]
                for small_root in root_lists[p]:
                    for r in range(small_root, m, p):
                        value = evaluate(a, r)
                        if value % m:
                            continue
                        for sign in (1, -1):
                            reference.add((m, r, 0, sign))
                            d = sign * m
                            delta = m*m - 4 * (value // d)
                            if delta >= 0:
                                square = math.isqrt(delta)
                                if square*square == delta and (d-square) % 2 == 0:
                                    reference_hits.add((r, (d+square)//2, (d-square)//2))
        large = write(f"{name}-factor34.json", dict(data, prime_limit="43", factor_counts=[3, 4],
                                                   m_max="1000000", k_min="0", k_max="0"))
        expected = {point for point in reference if point[0] % 2}
        assert reference - expected
        single, _ = search(f"{name}-factor34-one", large, db, "--trace")
        assert coverage(single) == expected and hitset(single) == reference_hits
        union, work_union = set(), set()
        for shard in range(3):
            directory, _ = search(f"{name}-factor34-shard{shard}", large, db, "--trace", "--threads", 3,
                                  "--shard", f"{shard}/3")
            points, work = coverage(directory), taskset(directory)
            assert points and {factor_counts[m] for m, r, k, sign in points} == {3, 4}
            assert all(m % 2 for m, r, k, sign in points)
            assert not points & union and not work & work_union
            union |= points
            work_union |= work
        assert union == expected and work_union == taskset(single)
        empty_config = write(f"{name}-all-excluded.json", dict(data, moduli=[[2], [2, 7, 11]]))
        empty, _ = search(f"{name}-all-excluded", empty_config, db, "--trace")
        empty_report = json.loads((empty / "report.json").read_text())
        assert empty_report["complete"] == "true" and empty_report["counters"]["tasks"] == "0"
        assert empty_report["counters"]["roots"] == "0" and not coverage(empty)
        plan_out = WORK / f"{name}-plan"
        plan = json.loads(run("search", "--config", empty_config, "--db", db,
                              "--out", plan_out, "--dry-run").stdout)
        assert plan["analytical_exclusions"]["explicit_moduli_excluded"] == "2"
        assert plan["domain_definition"]["local_filter"]["exclude_even_square_free"] == "true"
        assert not plan_out.exists() and original_database == db.read_bytes()
    # An authentic v1 checkpoint must be rejected before its cursor or journal is read.
    legacy = WORK / "legacy"
    legacy.mkdir()
    old = (ROOT / "tests/fixtures/pre-filter-g1-checkpoint.json").read_bytes()
    (legacy / "checkpoint.json").write_bytes(old)
    _, result = search("legacy", ROOT / "configs/smoke.json", ROOT / "tests/fixtures/pre-filter-g1-roots97.jsonl", "--resume", ok=False)
    assert "generator/filter version mismatch" in result.stderr
    assert "Keep the root database" in result.stderr
    assert (legacy / "checkpoint.json").read_bytes() == old and not (legacy / "results.jsonl").exists()


def main():
    db = WORK / "roots.jsonl"
    run("roots", "--polynomial", "synthetic", "--limit", 31, "--out", db)
    config_data = {"polynomial": "synthetic", "prime_limit": "31", "factor_counts": [1, 2, 3, 4, 5],
                   "m_min": "2", "m_max": "500", "k_min": "-4", "k_max": "4"}
    config = write("tiny.json", config_data)
    expected_hits, expected_coverage = structured(config_data)
    assert {(-2, 3, -1), (2, 1, -3)} <= expected_hits
    assert expected_hits and any(x < 0 for x, y, z in expected_hits)
    assert any(x > 0 for x, y, z in expected_hits)
    assert any(y + z < 0 for x, y, z in expected_hits) and any(y + z > 0 for x, y, z in expected_hits)
    assert any(y + z == 0 for x, y, z in coordinate_box(coefficients(config_data), 4))
    one, _ = search("one", config, db, "--threads", 1, "--trace", "--arithmetic", "128")
    many, _ = search("many", config, db, "--threads", 4, "--trace", "--arithmetic", "big")
    assert hitset(one) == hitset(many) == expected_hits
    assert coverage(one) == coverage(many) == expected_coverage
    no_sieve, _ = search("no-sieve", config, db, "--no-sieve", "--trace")
    assert hitset(no_sieve) == expected_hits and coverage(no_sieve) == expected_coverage
    tasks_one = {row["task"] for row in records(one / "coverage.jsonl")}
    assert tasks_one == {row["task"] for row in records(many / "coverage.jsonl")}
    shards, union, task_union = [], set(), set()
    for shard in range(3):
        directory, _ = search(f"shard{shard}", config, db, "--threads", 2, "--shard", f"{shard}/3", "--trace")
        shard_coverage = coverage(directory)
        assert not union & shard_coverage
        union |= shard_coverage
        task_set = {row["task"] for row in records(directory / "coverage.jsonl")}
        assert not task_union & task_set
        task_union |= task_set
        shards.append(directory)
    assert union == expected_coverage and task_union == tasks_one
    summary, hits = merge(shards)
    assert summary["complete"] and {verify_record(row) for row in hits} == expected_hits
    summary, _ = merge(shards[:2])
    assert not summary["complete"] and summary["missing_shards"] == ["2"]
    # A fully written result batch with an older checkpoint emulates hard-kill replay.
    replay, _ = search("replay", config, db, "--max-tasks", 3, "--trace")
    checkpoint = (replay / "checkpoint.json").read_bytes()
    search("replay", config, db, "--resume", "--threads", 4, "--trace")
    (replay / "checkpoint.json").write_bytes(checkpoint)
    with (replay / "results.jsonl").open("ab") as stream:
        stream.write(b'{"truncated":')
    with (replay / "coverage.jsonl").open("ab") as stream:
        stream.write(b'{"truncated":')
    search("replay", config, db, "--resume", "--threads", 2, "--trace")
    assert hitset(replay) == expected_hits and coverage(replay) == expected_coverage
    summary, merged = merge([replay, replay])
    assert summary["complete"] and len(merged) == len(expected_hits)
    assert len(list(records(replay / "results.jsonl"))) > len(expected_hits)
    # Interrupted bounded work and arbitrary restart offsets.
    long_data = dict(config_data, k_ranges=[[-130, -17], [17, 130]])
    long_data.pop("k_min"); long_data.pop("k_max")
    long_config = write("shell.json", long_data)
    shell_hits, shell_coverage = structured(long_data)
    shell, _ = search("shell", long_config, db, "--max-tasks", 7, "--trace", "--threads", 4)
    assert json.loads((shell / "report.json").read_text())["complete"] == "false"
    search("shell", long_config, db, "--resume", "--threads", 1, "--trace")
    assert hitset(shell) == shell_hits and coverage(shell) == shell_coverage
    # Mismatched and corrupted state must fail, never silently start fresh.
    search("one", long_config, db, "--resume", "--trace", ok=False)
    search("one", config, db, "--resume", "--trace", "--shard", "0/3", ok=False)
    corrupt = WORK / "corrupt.jsonl"
    corrupt.write_bytes(db.read_bytes().replace(b'"prime":"2"', b'"prime":"4"', 1))
    search("bad-cache", config, corrupt, ok=False)
    bad = WORK / "bad-checkpoint"
    shutil.copytree(one, bad)
    (bad / "checkpoint.json").write_text((bad / "checkpoint.json").read_text()[:50])
    search("bad-checkpoint", config, db, "--resume", "--trace", ok=False)
    broken = WORK / "bad-results"
    shutil.copytree(one, broken)
    data = (broken / "results.jsonl").read_bytes()
    (broken / "results.jsonl").write_bytes(data.replace(b'"residual":"0"', b'"residual":"1"', 1))
    search("bad-results", config, db, "--resume", "--trace", ok=False)
    wrong = write("wrong.json", dict(config_data, polynomial="g2"))
    search("wrong", wrong, db, ok=False)
    incomplete = write("incomplete.json", dict(config_data, prime_limit="37"))
    search("incomplete", incomplete, db, ok=False)
    # Interrupted initialization before any checkpoint publication is safely restartable.
    init = WORK / "initialization"
    init.mkdir()
    shutil.copyfile(one / "manifest.json", init / "manifest.json")
    (init / "results.jsonl").write_bytes(b"")
    search("initialization", config, db, "--resume", "--trace")
    assert hitset(init) == expected_hits and coverage(init) == expected_coverage
    # Coefficient-driven CLI and cache are equivalent to the named synthetic target.
    custom_db = WORK / "custom.jsonl"
    run("roots", "--coefficients", "0,-1,0,1", "--limit", 31, "--out", custom_db)
    custom_data = dict(config_data, coefficients=[0, -1, 0, 1])
    custom_data.pop("polynomial")
    custom_config = write("custom.json", custom_data)
    custom, _ = search("custom", custom_config, custom_db, "--trace")
    assert hitset(custom) == expected_hits and coverage(custom) == expected_coverage
    # Real regression: only factors and k window are search inputs.
    regression_db = WORK / "regression.jsonl"
    run("roots", "--polynomial", "regression", "--primes", "127,1783,4236203", "--out", regression_db)
    regression, _ = search("regression", ROOT / "configs/regression.json", regression_db, "--threads", 2)
    witness = (-783954692511, 1797526169071, -838276125548)
    assert witness in hitset(regression)
    record = next(records(regression / "results.jsonl"))
    record["x"] = str(int(record["x"]) + 1)
    try:
        verify_record(record)
        raise AssertionError("Perturbed witness accepted")
    except ValueError:
        pass
    search("unsafe", ROOT / "configs/regression.json", regression_db, "--arithmetic", "128", ok=False)
    try:
        merge([one, regression])
        raise AssertionError("Incompatible domains merged")
    except ValueError:
        pass
    filtered_targets()
    even_config = write("even-synthetic.json", {"coefficients": [0, -1, 0, 1], "moduli": [[2]],
                                              "m_min": "2", "m_max": "2", "k_min": "-1", "k_max": "1"})
    even, _ = search("even-synthetic", even_config, db, "--trace")
    assert {(-2, 3, -1), (2, 1, -3)} <= hitset(even)
    assert json.loads((even / "manifest.json").read_text())["domain_definition"]["local_filter"]["exclude_even_square_free"] == "false"
    # Real signal stopping and hard-kill recovery on generic and filtered generators.
    for target, database in (("synthetic", db), ("g1", WORK / "g1-filtered.roots.jsonl")):
        long_run = write(f"{target}-long-run.json", dict(config_data, polynomial=target,
                         k_min="-1000000000000", k_max="1000000000000"))
        for sig, event in ((signal.SIGINT, "interrupt"), (signal.SIGTERM, "terminate"), (signal.SIGKILL, "kill")):
            name = f"{target}-{event}"
            output = WORK / name
            command = [str(EXE), "search", "--config", str(long_run), "--db", str(database),
                       "--out", str(output), "--threads", "2"]
            with (WORK / (name + ".log")).open("w") as log:
                process = subprocess.Popen(command, stdout=log, stderr=log)
                deadline = time.monotonic() + 15
                while not (output / "checkpoint.json").exists():
                    assert process.poll() is None and time.monotonic() < deadline
                    time.sleep(0.005)
                time.sleep(0.015)
                process.send_signal(sig)
                process.wait(timeout=15)
            if sig != signal.SIGKILL:
                assert process.returncode == 0
                assert json.loads((output / "report.json").read_text())["complete"] == "false"
            search(name, long_run, database, "--resume", "--seconds", "0.03", "--threads", 3)
            assert json.loads((output / "report.json").read_text())["complete"] == "false"
    print(f"Integration suite passed: {len(expected_coverage)} tiny-domain candidates, {len(expected_hits)} unique witnesses; artifacts: {WORK}")


if __name__ == "__main__":
    main()
