#!/usr/bin/env python3
"""Deterministic abrupt-exit, epoch frontier, empty journal and v1 migration tests."""
import json
from pathlib import Path
import shutil
import subprocess
import sys

import integration as t

DRIVER = Path(sys.argv[2]).resolve()


def checkpoint(directory):
    return json.loads((directory / "checkpoint.json").read_text())["payload"]


def main():
    db = t.WORK / "roots.jsonl"
    t.run("roots", "--polynomial", "synthetic", "--limit", 31, "--out", db)
    data = {"polynomial": "synthetic", "prime_limit": "31", "factor_counts": [1, 2, 3],
            "m_min": "2", "m_max": "500", "k_min": "-4", "k_max": "4"}
    config = t.write("config.json", data)
    hits, points = t.structured(data)
    for flag, value in (("--chunk-tiles", "0"), ("--queue-chunks", "2049"),
                        ("--checkpoint-tiles", "0"), ("--checkpoint-seconds", "nan")):
        t.search("invalid", config, db, flag, value, ok=False)
    assert not (t.WORK / "invalid").exists()
    _, limited = t.search("progress", config, db, "--max-tasks", 2,
                          "--checkpoint-tiles", 1, "--progress-seconds", "0.000001")
    assert json.loads(limited.stdout)["counters"]["tasks"] == "2"
    assert "running" in limited.stderr and "paused" in limited.stderr
    assert "run_tiles=2/2 (100.0% of run tile budget)" in limited.stderr
    assert "campaign_total=unknown" in limited.stderr
    _, resumed = t.search("progress", config, db, "--resume", "--max-tasks", 1)
    assert "run_tiles=1/1 (100.0% of run tile budget)" in resumed.stderr
    assert "committed_tiles=3" in resumed.stderr
    _, silent = t.search("quiet", config, db, "--progress-seconds", 0)
    assert silent.stderr == "" and json.loads(silent.stdout)["complete"] == "true"
    _, timed = t.search("timed-progress", config, db, "--seconds", 60)
    assert "time_budget=" in timed.stderr and "shard=complete" in timed.stderr
    reference, _ = t.search("reference", config, db, "--trace")
    expected_tasks = t.taskset(reference)
    # Exit inside a chunk, during an epoch, after streamed witnesses, after fsync,
    # just before replacement, and after a noninitial checkpoint replacement.
    cases = [("tile_completed", n) for n in (1, 7, 33)] + [
        ("chunk_received", 4), ("results_synced", 1),
        ("before_checkpoint", 1), ("checkpoint_replaced", 1)]
    for i, (event, index) in enumerate(cases):
        name = f"crash-{event}-{index}"
        directory = t.WORK / name
        result = subprocess.run([str(DRIVER), str(config), str(db), str(directory), event, str(index)],
                                capture_output=True, text=True, timeout=30)
        assert result.returncode == 86, result.stderr
        before = checkpoint(directory)
        assert int(before["counters"]["tasks"]) < int(checkpoint(reference)["counters"]["tasks"])
        if event == "chunk_received":
            assert (directory / "results.jsonl").stat().st_size > int(before["result_bytes"])
        t.search(name, config, db, "--resume", "--trace", "--threads", (1, 2, 4)[i % 3],
                 "--chunk-tiles", 3, "--queue-chunks", 3, "--checkpoint-tiles", 17)
        assert t.hitset(directory) == hits and t.coverage(directory) == points
        assert t.taskset(directory) == expected_tasks
        after = checkpoint(directory)
        for key in ("tasks", "moduli", "roots", "candidates", "survivors", "squares", "hits"):
            assert after["counters"][key] == checkpoint(reference)["counters"][key]
        assert after["cursor"] == checkpoint(reference)["cursor"]
        assert after["schema"] == "dio-checkpoint-v2"
        summary, _ = t.merge([directory])
        assert summary["complete"]
    reordered = t.WORK / "reorder"
    subprocess.run([str(DRIVER), str(config), str(db), str(reordered), "reorder", "0"],
                   check=True, capture_output=True, timeout=30)
    report = json.loads((reordered / "report.json").read_text())
    assert int(report["invocation_metrics"]["out_of_order_epochs"]) > 0
    assert int(report["invocation_metrics"]["peak_inflight_chunks"]) <= 4
    assert t.coverage(reordered) == points and t.hitset(reordered) == hits
    # Many empty-hit epochs never append/fsync the result journal after creation.
    gdb = t.ROOT / "tests/fixtures/pre-filter-g1-roots97.jsonl"
    empty_cfg = t.write("empty.json", {"polynomial": "g1", "prime_limit": "97", "factor_counts": [2],
                                       "m_min": "2", "m_max": "10000", "k_min": "0", "k_max": "0"})
    empty, _ = t.search("empty", empty_cfg, gdb, "--checkpoint-tiles", 2, "--chunk-tiles", 1)
    r = json.loads((empty / "report.json").read_text())
    assert int(r["invocation_metrics"]["epochs"]) > 2
    assert r["counters"]["hits"] == "0"
    assert r["invocation_metrics"]["result_append_calls"] == "0"
    assert r["invocation_metrics"]["result_fsync_calls"] == "0"
    # Authentic issue #2 fixture: checksum/cursor/domain survive scheduler migration.
    fixture = t.ROOT / "tests/fixtures/issue2-run"
    legacy = t.WORK / "legacy"
    shutil.copytree(fixture, legacy)
    old = checkpoint(legacy)
    assert old["schema"] == "dio-checkpoint-v1"
    t.search("legacy", legacy / "config.json", legacy / "roots.jsonl", "--resume", "--threads", 4,
             "--chunk-tiles", 1, "--checkpoint-tiles", 2)
    new = checkpoint(legacy)
    assert new["schema"] == "dio-checkpoint-v2" and new["domain"] == old["domain"]
    expected, _ = t.structured(json.loads((fixture / "config.json").read_text()))
    assert t.hitset(legacy) == expected
    summary, _ = t.merge([legacy])
    assert summary["complete"]
    print("Epoch crash recovery, out-of-order frontier and migration passed")


if __name__ == "__main__":
    try:
        main()
    finally:
        shutil.rmtree(t.WORK)
