#!/usr/bin/env python3
"""Independent standard-library exact oracle. Never used by the C++ inner loop."""
import argparse
import hashlib
import itertools
import json
import math
from pathlib import Path

POLYNOMIALS = {
    "g1": [-1, 3, 1, 1],
    "g2": [1, 0, 1, 6],
    "regression": [-1, 1, 1, 3],
    "synthetic": [0, -1, 0, 1],
}


def coefficients(config):
    if "coefficients" not in config and config.get("polynomial") == "symmetric":
        return [1, 4, 0, 26]
    return list(map(int, config["coefficients"])) if "coefficients" in config else POLYNOMIALS[config["polynomial"]]


def evaluate(a, x):
    return sum(c * x**i for i, c in enumerate(a))


def verify(a, x, y, z):
    return y * z * (y + z) == evaluate(a, x)


def is_prime(p):
    return p >= 2 and all(p % d for d in range(2, math.isqrt(p) + 1))


def verify_record(row):
    if row.get("schema") == "dio-symmetric-witness-v1":
        from symmetric import verify_record as verify_symmetric
        return verify_symmetric(row)
    if row.get("backend", "yz-sum") != "yz-sum":
        raise ValueError("Legacy witness/backend mismatch")
    if row["schema"] != "dio-witness-v1":
        raise ValueError("Unsupported witness schema")
    a = coefficients(row)
    x, y, z = (int(row[key]) for key in ("x", "y", "z"))
    if not verify(a, x, y, z) or y < z:
        raise ValueError("Invalid or noncanonical witness")
    d, m, r, k, delta, s = (int(row[key]) for key in
                           ("d", "m", "root", "k", "discriminant", "square_root"))
    ps = list(map(int, row["factors"]))
    if (d != y + z or m != abs(d) or m == 0 or not 0 <= r < m
            or x != r + k * m or evaluate(a, r) % m
            or math.prod(ps) != m or ps != sorted(set(ps))
            or not all(is_prime(p) for p in ps)
            or s < 0 or s != y - z or s * s != delta
            or delta != d * d - 4 * (evaluate(a, x) // d)
            or evaluate(a, x) % d or int(row["residual"]) != 0):
        raise ValueError("Invalid witness derivation")
    expected = hashlib.sha256(f'{row["domain"]}:{x}:{y}:{z}'.encode()).hexdigest()
    if row["id"] != expected:
        raise ValueError("Invalid witness ID")
    return x, y, z


def coordinate_box(a, bound):
    """All triples in [-bound,bound]^3, canonical y>=z; includes d=0."""
    return {(x, y, z) for x in range(-bound, bound + 1)
            for y in range(-bound, bound + 1) for z in range(-bound, y + 1)
            if verify(a, x, y, z)}


def structured(config):
    """Tiny exact modulus/root/k domain, not a coordinate-height search."""
    if config.get("backend") == "symmetric-cubic":
        from symmetric import structured as symmetric_structured
        return symmetric_structured(config)
    a = coefficients(config)
    minimum, maximum = int(config["m_min"]), int(config["m_max"])
    if maximum > 100000:
        raise ValueError("Direct-residue oracle limited to m_max <= 100000")
    if "moduli" in config:
        moduli = [math.prod(map(int, factors)) for factors in config["moduli"]]
    else:
        limit = int(config["prime_limit"])
        if limit > 5000:
            raise ValueError("Oracle prime limit exceeds 5000")
        ps = [p for p in range(2, limit + 1) if is_prime(p)]
        moduli = (math.prod(factors) for count in config["factor_counts"]
                  for factors in itertools.combinations(ps, int(count)))
    ranges = config.get("k_ranges", [[config.get("k_min"), config.get("k_max")]])
    hits, coverage = set(), set()
    for m in moduli:
        if not minimum <= m <= maximum:
            continue
        for r in range(m):
            if evaluate(a, r) % m:
                continue
            for lo, hi in ranges:
                for k in range(int(lo), int(hi) + 1):
                    x = r + k * m
                    for sign in (1, -1):
                        coverage.add((m, r, k, sign))
                        d = sign * m
                        delta = d * d - 4 * (evaluate(a, x) // d)
                        if delta < 0:
                            continue
                        s = math.isqrt(delta)
                        if s * s == delta and (d - s) % 2 == 0:
                            y, z = (d + s) // 2, (d - s) // 2
                            assert verify(a, x, y, z)
                            hits.add((x, y, z))
    return hits, coverage


def records(path):
    """Only a non-newline-terminated final record may be ignored after a crash."""
    with Path(path).open("rb") as stream:
        for line in stream:
            if not line.endswith(b"\n"):
                break
            yield json.loads(line)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("verify")
    p.add_argument("--polynomial", choices=POLYNOMIALS, default="regression")
    p.add_argument("--triple", nargs=3, type=int)
    p.add_argument("--results", type=Path)
    p = sub.add_parser("box")
    p.add_argument("--polynomial", choices=POLYNOMIALS, default="synthetic")
    p.add_argument("--bound", type=int, default=8)
    p = sub.add_parser("structured")
    p.add_argument("--config", type=Path, required=True)
    args = parser.parse_args()
    if args.command == "verify":
        if args.results:
            count = sum(1 for row in records(args.results) if verify_record(row))
            print(json.dumps({"verified_records": str(count)}))
        elif args.triple:
            if not verify(POLYNOMIALS[args.polynomial], *args.triple):
                raise ValueError("Triple does not satisfy the equation")
            print("Exact substitution accepted")
        else:
            parser.error("Provide --triple or --results")
    else:
        if args.command == "box":
            if not 0 <= args.bound <= 100:
                raise ValueError("Box bound must be between 0 and 100")
            hits = coordinate_box(POLYNOMIALS[args.polynomial], args.bound)
        else:
            hits, _ = structured(json.loads(args.config.read_text()))
        print(json.dumps([[str(v) for v in hit] for hit in sorted(hits)]))


if __name__ == "__main__":
    main()
