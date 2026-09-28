#!/usr/bin/env python3
"""Verify and merge local shard results without claiming partial coverage complete."""
import argparse
import hashlib
import json
from pathlib import Path
from oracle import coefficients, verify_record


def journal(path, state):
    """Hash complete records in one pass; an incomplete tail cannot certify completion."""
    with path.open("rb") as stream:
        for raw in stream:
            if not raw.endswith(b"\n"):
                state["truncated"] = True
                break
            state["checksum"] = hashlib.sha256(state["checksum"].encode() + raw).hexdigest()
            state["bytes"] += len(raw)
            yield json.loads(raw)


def merge(directories):
    domain = None
    shard_count = None
    completed = set()
    present = set()
    hits = {}
    for directory in map(Path, directories):
        manifest = json.loads((directory / "manifest.json").read_text())
        fingerprint = manifest["domain"]
        count = int(manifest["shards"])
        shard = int(manifest["shard"])
        if domain is None:
            domain, shard_count = fingerprint, count
        if fingerprint != domain or count != shard_count or not 0 <= shard < count:
            raise ValueError("Incompatible domains or shard layouts")
        present.add(shard)
        report_path = directory / "report.json"
        report = json.loads(report_path.read_text()) if report_path.exists() else None
        state = {"checksum": hashlib.sha256(b"").hexdigest(), "bytes": 0, "truncated": False}
        for row in journal(directory / "results.jsonl", state):
            verify_record(row)
            if (row["domain"] != domain or row["shard"] != f"{shard}/{count}"
                    or coefficients(row) != coefficients(manifest["domain_definition"])
                    or row["root_database"] != manifest["root_database"]):
                raise ValueError("Witness provenance does not match manifest")
            definition = manifest["domain_definition"]
            m, k = int(row["m"]), int(row["k"])
            factors = list(map(int, row["factors"]))
            explicit = definition["moduli"]
            if (not int(definition["m_min"]) <= m <= int(definition["m_max"])
                    or not any(int(lo) <= k <= int(hi) for lo, hi in definition["k_ranges"])
                    or (explicit and factors not in [list(map(int, ps)) for ps in explicit])
                    or (not explicit and (len(factors) not in map(int, definition["factor_counts"])
                                          or max(factors) > int(definition["prime_limit"])))):
                raise ValueError("Witness is outside the declared search domain")
            if row["id"] in hits and any(row[key] != hits[row["id"]][key] for key in ("x", "y", "z")):
                raise ValueError("Conflicting duplicate witness")
            hits[row["id"]] = row
        # Reports may predate an interrupted resumed run. Only an exact match certifies a shard.
        if (report and report["domain"] == domain and int(report["shard"]) == shard
                and int(report["shards"]) == count and report["complete"] == "true"
                and report["result_checksum_algorithm"] == "sha256-record-chain-v1"
                and report["result_checksum"] == state["checksum"]
                and int(report["result_bytes"]) == state["bytes"] and not state["truncated"]):
            completed.add(shard)
    missing = sorted(set(range(shard_count)) - present)
    summary = {"domain": domain, "shards": str(shard_count),
               "present_shards": list(map(str, sorted(present))),
               "missing_shards": list(map(str, missing)),
               "incomplete_shards": list(map(str, sorted(set(range(shard_count)) - completed))),
               "complete": len(completed) == shard_count,
               "unique_witnesses": str(len(hits)),
               "meaning": "Exhaustion applies only to the configured square-free domain"}
    return summary, [hits[key] for key in sorted(hits)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directories", nargs="+", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    summary, hits = merge(args.directories)
    args.out.mkdir(parents=True, exist_ok=False)
    (args.out / "results.jsonl").write_text("".join(json.dumps(row) + "\n" for row in hits))
    (args.out / "report.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary))


if __name__ == "__main__":
    main()
