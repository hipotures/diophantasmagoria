#!/usr/bin/env python3
"""Independent symmetric-backend coverage, verification and recovery gates."""
import copy
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from symmetric import structured, verify_record, residual
from oracle import records, verify_record as dispatch_verify, structured as legacy_oracle
from merge import merge

EXE = Path(sys.argv[1]).resolve()
FAULT = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else None
WORK = ROOT / "Experiments" / f"test-symmetric-{os.getpid()}"
WORK.mkdir(parents=True, exist_ok=False)


def run(*args, good=True):
    p = subprocess.run([str(EXE), *map(str, args)], text=True, capture_output=True, timeout=60)
    assert (p.returncode == 0) == good, (args, p.returncode, p.stdout, p.stderr)
    return p


def config(name, data):
    p = WORK / f"{name}.json"
    p.write_text(json.dumps(data))
    return p


def search(name, cfg, db, *args, good=True):
    out = WORK / name
    p = run("search", "--config", cfg, "--db", db, "--out", out, *args, good=good)
    return out, p


def points(out):
    return {(int(r["m"]), int(r["r"]), int(r["k"]), int(r["sign"]))
            for r in records(out / "coverage.jsonl")}


def hits(out):
    return {verify_record(row) for row in records(out / "results.jsonl")}


def reject_record(row):
    try:
        dispatch_verify(row)
    except (ValueError, KeyError):
        return
    raise AssertionError("Invalid record accepted")


def main():
    total_candidates = 0
    for constant in (1, 3, 4):
        db = WORK / f"roots-{constant}.jsonl"
        run("roots", "--coefficients", f"{constant},4,0,26", "--limit", 31, "--out", db)
        data = {"backend": "symmetric-cubic", "coefficients": [constant, 4, 0, 26],
                "prime_limit": "31", "factor_counts": [1, 2, 3], "m_min": "2",
                "m_max": "500", "k_min": "-4", "k_max": "4"}
        cfg = config(f"C{constant}", data)
        expected, coverage = structured(data)
        total_candidates += len(coverage)
        if constant == 3:
            assert {(-2, -3, 4), (1, 0, 0), (4, -2, -3)} <= expected
        if constant == 4:
            assert (2, -1, -1) in expected  # d=2; never apply g1/g2's filter here.
        one, _ = search(f"C{constant}-one", cfg, db, "--trace", "--threads", 1, "--arithmetic", "128")
        many, _ = search(f"C{constant}-many", cfg, db, "--trace", "--threads", 4, "--arithmetic", "big")
        bare, _ = search(f"C{constant}-bare", cfg, db, "--trace", "--no-sieve")
        for out in (one, many, bare):
            assert points(out) == coverage and hits(out) == expected
            assert json.loads((out / "report.json").read_text())["complete"] == "true"
        union, shards = set(), []
        for shard in range(3):
            out, _ = search(f"C{constant}-shard{shard}", cfg, db, "--trace", "--threads", 2,
                            "--shard", f"{shard}/3")
            current = points(out)
            assert not union & current
            union |= current
            shards.append(out)
        assert union == coverage
        summary, rows = merge(shards)
        assert summary["complete"] and {verify_record(r) for r in rows} == expected
        assert not merge(shards[:2])[0]["complete"]
        replay, _ = search(f"C{constant}-replay", cfg, db, "--trace", "--max-tasks", 2)
        checkpoint = (replay / "checkpoint.json").read_bytes()
        search(f"C{constant}-replay", cfg, db, "--trace", "--resume", "--threads", 3)
        (replay / "checkpoint.json").write_bytes(checkpoint)
        with (replay / "results.jsonl").open("ab") as stream:
            stream.write(b'{"truncated":')
        with (replay / "coverage.jsonl").open("ab") as stream:
            stream.write(b'{"truncated":')
        search(f"C{constant}-replay", cfg, db, "--trace", "--resume", "--threads", 2)
        assert hits(replay) == expected and points(replay) == coverage
        assert len(merge([replay, replay])[1]) == len(expected)
        # Same divisibility polynomial, different equation: cannot reuse progress.
        old_data = dict(data)
        del old_data["backend"]
        old_cfg = config(f"legacy-C{constant}", old_data)
        old, _ = search(f"legacy-C{constant}", old_cfg, db, "--trace")
        legacy_hits, legacy_coverage = legacy_oracle(old_data)
        assert {dispatch_verify(r) for r in records(old / "results.jsonl")} == legacy_hits
        assert points(old) == legacy_coverage
        search(f"C{constant}-one", old_cfg, db, "--trace", "--resume", good=False)
        search(f"legacy-C{constant}", cfg, db, "--trace", "--resume", good=False)
        assert json.loads((old / "manifest.json").read_text())["domain"] != json.loads((one / "manifest.json").read_text())["domain"]
        if constant == 3:
            stop, _ = search("stop-on-hit", cfg, db, "--trace", "--stop-on-hit", "--chunk-tiles", 1,
                             "--checkpoint-tiles", 2, "--threads", 2)
            assert hits(stop)
            search("stop-on-hit", cfg, db, "--trace", "--resume", "--threads", 4)
            assert hits(stop) == expected and points(stop) == coverage
            # Real abrupt exits, including after witness bytes were written but
            # before the epoch checkpoint. All resumed work must match the oracle.
            if FAULT:
                for event in ("tile_completed", "chunk_received", "results_synced", "before_checkpoint", "checkpoint_replaced", "reorder"):
                    out = WORK / f"fault-{event}"
                    p = subprocess.run([str(FAULT), str(cfg), str(db), str(out), event, "4"],
                                       capture_output=True, text=True, timeout=60)
                    assert p.returncode == (0 if event == "reorder" else 86), (event, p.stderr)
                    run("search", "--config", cfg, "--db", db, "--out", out,
                        "--trace", "--resume", "--threads", 2)
                    assert hits(out) == expected and points(out) == coverage
            row = next(records(one / "results.jsonl"))
            for key in ("x", "y", "z", "a", "d", "root", "k", "discriminant", "square_root"):
                bad = copy.deepcopy(row); bad[key] = str(int(bad[key])+1); reject_record(bad)
            bad = copy.deepcopy(row); bad["backend"] = "yz-sum"; reject_record(bad)
            bad = copy.deepcopy(row); bad["schema"] = "dio-witness-v1"; reject_record(bad)
            # A valid record cannot be relabeled as an unrelated backend in a manifest.
            forged = WORK / "forged-backend"
            shutil.copytree(one, forged)
            manifest = json.loads((forged / "manifest.json").read_text())
            manifest["domain_definition"].pop("backend")
            (forged / "manifest.json").write_text(json.dumps(manifest))
            try:
                merge([forged])
            except ValueError:
                pass
            else:
                raise AssertionError("Backend-confused manifest accepted")
    # Explicit large regression: factors only, never root/coordinates as input.
    db = WORK / "regression.roots.jsonl"
    run("roots", "--polynomial", "symmetric", "--primes", "13,79,181,269,613", "--out", db)
    regression, _ = search("regression", ROOT / "configs/symmetric-regression.json", db, "--threads", 4)
    expected_witness = (723809206820, -631758385864, -245500254229)
    assert expected_witness in hits(regression)
    record = next(records(regression / "results.jsonl"))
    assert record["a"] == "92050820956" and record["root"] == "94195039" and record["k"] == "3"
    assert json.loads((regression / "report.json").read_text())["counters"]["roots"] == "9"
    assert not residual(*expected_witness) and residual(expected_witness[0]+1, *expected_witness[1:])
    search("unsafe", ROOT / "configs/symmetric-regression.json", db, "--arithmetic", "128", good=False)
    bad = config("bad-coefficients", {"backend": "symmetric-cubic", "coefficients": [1, 5, 0, 26],
                                    "moduli": [[13]], "m_min": "2", "m_max": "100", "k_min": "0", "k_max": "0"})
    search("bad-config", bad, db, good=False)
    invalid = config("bad-backend", {"backend": "typo", "polynomial": "symmetric", "m_min": "2"})
    search("bad-backend", invalid, db, good=False)
    # Existing known yz-sum regression still follows the untouched backend.
    old_db = WORK / "legacy-regression.roots.jsonl"
    run("roots", "--polynomial", "regression", "--primes", "127,1783,4236203", "--out", old_db)
    old_cfg = config("legacy-regression", {"polynomial": "regression", "moduli": [[127,1783,4236203]],
                     "m_min": "959250043523", "m_max": "959250043523", "k_min": "-2", "k_max": "2"})
    old, _ = search("legacy-regression", old_cfg, old_db)
    assert (-783954692511,1797526169071,-838276125548) in {dispatch_verify(r) for r in records(old / "results.jsonl")}
    print(f"Symmetric backend passed: {total_candidates} oracle candidates, signed positive controls, regression, backend isolation, sharding, replay and faults")


if __name__ == "__main__":
    main()
