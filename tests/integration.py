#!/usr/bin/env python3
"""End-to-end coverage, recovery, provenance and witness regression tests."""
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from oracle import coefficients, coordinate_box, records, structured, verify_record
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


def main():
    db = WORK / "roots.jsonl"
    run("roots", "--polynomial", "synthetic", "--limit", 31, "--out", db)
    config_data = {"polynomial": "synthetic", "prime_limit": "31", "factor_counts": [1, 2, 3, 4, 5],
                   "m_min": "2", "m_max": "500", "k_min": "-4", "k_max": "4"}
    config = write("tiny.json", config_data)
    expected_hits, expected_coverage = structured(config_data)
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
    # Both actual targets are runnable, including the degree-drop prime 2 for g2.
    for polynomial in ("g1", "g2"):
        target_db = WORK / f"{polynomial}.jsonl"
        run("roots", "--polynomial", polynomial, "--limit", 31, "--out", target_db)
        target_data = dict(config_data, polynomial=polynomial)
        target_config = write(f"{polynomial}.json", target_data)
        directory, _ = search(polynomial, target_config, target_db, "--trace")
        hits, candidates = structured(target_data)
        assert hitset(directory) == hits and coverage(directory) == candidates
    # Actual signals and SIGKILL against an intentionally long k domain.
    long_run = write("long-run.json", dict(config_data, k_min="-1000000000000", k_max="1000000000000"))
    for sig, name in ((signal.SIGINT, "interrupt"), (signal.SIGTERM, "terminate"), (signal.SIGKILL, "kill")):
        output = WORK / name
        command = [str(EXE), "search", "--config", str(long_run), "--db", str(db), "--out", str(output), "--threads", "2"]
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
        search(name, long_run, db, "--resume", "--seconds", "0.03", "--threads", 3)
        assert json.loads((output / "report.json").read_text())["complete"] == "false"
    print(f"Integration suite passed: {len(expected_coverage)} tiny-domain candidates, {len(expected_hits)} unique witnesses; artifacts: {WORK}")


if __name__ == "__main__":
    main()
