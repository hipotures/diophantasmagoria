#!/usr/bin/env python3
"""Independent exact verifier/oracle for sum(t^3+2t) = xyz + C (default C=1)."""
import argparse
import hashlib
import itertools
import json
import math
from pathlib import Path

BACKEND = "symmetric-cubic-v1"
SCHEMA = "dio-symmetric-witness-v1"


def residual(x, y, z, constant=1):
    return x**3 + y**3 + z**3 + 2*(x+y+z) - x*y*z - constant


def root_polynomial(a, constant=1):
    return 26*a**3 + 4*a + constant


def prime(p):
    return p >= 2 and all(p % n for n in range(2, math.isqrt(p) + 1))


def verify_record(row):
    if row.get("schema") != SCHEMA or row.get("backend") != BACKEND:
        raise ValueError("Invalid symmetric witness schema/backend")
    coefficients = list(map(int, row["coefficients"]))
    if len(coefficients) != 4 or coefficients[1:] != [4, 0, 26]:
        raise ValueError("Invalid symmetric root polynomial")
    constant = coefficients[0]
    x, y, z = (int(row[key]) for key in ("x", "y", "z"))
    if x < y or residual(x, y, z, constant) != 0:
        raise ValueError("Invalid or noncanonical symmetric witness")
    a, d, m, r, k, delta, s = (int(row[key]) for key in
                              ("a", "d", "m", "root", "k", "discriminant", "square_root"))
    factors = list(map(int, row["factors"]))
    if (a != x+y or d != 3*a+z or m != abs(d) or m < 2 or not 0 <= r < m
            or a != r+k*m or root_polynomial(r, constant) % m
            or not 1 <= len(factors) <= 5 or factors != sorted(set(factors))
            or math.prod(factors) != m or not all(prime(p) for p in factors)
            or root_polynomial(a, constant) % d):
        raise ValueError("Invalid symmetric divisibility/CRT derivation")
    # Independent reconstruction through the product b=xy, not the searcher's
    # expanded discriminant or finite differences.
    b = d*d - 9*a*d + 27*a*a + 2 - root_polynomial(a, constant)//d
    if (b != x*y or delta != a*a-4*b or s < 0 or s != x-y or s*s != delta
            or (a-s) % 2 or int(row["residual"]) != 0):
        raise ValueError("Invalid symmetric square/reconstruction derivation")
    expected = hashlib.sha256(f'{row["domain"]}:{x}:{y}:{z}'.encode()).hexdigest()
    if row["id"] != expected:
        raise ValueError("Invalid symmetric witness ID")
    return x, y, z


def structured(config):
    """Tiny direct-residue oracle; returns (hits, candidate coverage)."""
    if config.get("backend") != "symmetric-cubic":
        raise ValueError("Explicit symmetric-cubic backend required")
    c = list(map(int, config.get("coefficients", [1, 4, 0, 26])))
    if len(c) != 4 or c[1:] != [4, 0, 26]:
        raise ValueError("Expected coefficients [C,4,0,26]")
    lo, hi = int(config["m_min"]), int(config["m_max"])
    if not 2 <= lo <= hi <= 100000:
        raise ValueError("Oracle requires 2 <= m_min <= m_max <= 100000")
    if "moduli" in config:
        moduli = [math.prod(map(int, ps)) for ps in config["moduli"]]
    else:
        limit = int(config["prime_limit"])
        if limit > 1000:
            raise ValueError("Tiny oracle prime limit exceeds 1000")
        ps = [p for p in range(2, limit+1) if prime(p)]
        moduli = (math.prod(factors) for count in config["factor_counts"]
                  for factors in itertools.combinations(ps, int(count)))
    ranges = config.get("k_ranges", [[config.get("k_min"), config.get("k_max")]])
    hits, coverage = set(), set()
    for m in moduli:
        if not lo <= m <= hi:
            continue
        for r in range(m):
            if root_polynomial(r, c[0]) % m:
                continue
            for low, high in ranges:
                for k in range(int(low), int(high)+1):
                    a = r+k*m
                    for sign in (1, -1):
                        coverage.add((m, r, k, sign))
                        d = sign*m
                        b = d*d - 9*a*d + 27*a*a + 2 - root_polynomial(a, c[0])//d
                        delta = a*a-4*b
                        if delta < 0:
                            continue
                        s = math.isqrt(delta)
                        if s*s == delta and (a-s) % 2 == 0:
                            triple = ((a+s)//2, (a-s)//2, d-3*a)
                            if residual(*triple, c[0]):
                                raise AssertionError("Oracle reconstruction failed")
                            hits.add(triple)
    return hits, coverage


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--results", type=Path)
    source.add_argument("--triple", nargs=3, type=int)
    parser.add_argument("--constant", type=int, default=1)
    args = parser.parse_args()
    if args.triple:
        if residual(*args.triple, args.constant):
            raise ValueError("Triple does not satisfy the original equation")
        print("Exact symmetric substitution accepted")
        return
    count, ids, unordered = 0, set(), set()
    with args.results.open("rb") as stream:
        for line in stream:
            if not line.endswith(b"\n"):
                raise ValueError("Incomplete result journal; stop/resume before final verification")
            row = json.loads(line)
            triple = verify_record(row)
            if int(row["coefficients"][0]) != args.constant:
                raise ValueError("Unexpected equation constant")
            count += 1
            ids.add(row["id"])
            unordered.add(tuple(sorted(triple)))
    print(json.dumps({"verified_records": str(count), "unique_ids": str(len(ids)),
                      "permutation_classes": str(len(unordered))}))


if __name__ == "__main__":
    main()
