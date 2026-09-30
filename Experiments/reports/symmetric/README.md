# Symmetric backend validation and known-witness rediscovery

Implementation: `8a1fc3d918ac701784f2fd18172955dfe72e97c2` on
`feature/symmetric-cubic-one-host`, based on optimized commit
`b3754497e6243e870e54686fd9c8b8d1a3baac61`. Main was not changed.

## Local validation

Environment: GCC 14.2.0, Boost 1.83, Python 3.13.5, five CPUs available by
process affinity. The application was compiled directly with g++ and linked
against OpenSSL Crypto and pthreads. Source files were retrieved through the
GitHub integration and checked against Git blob hashes; a local git clone was
not available. This is not a claim that the entire upstream CTest suite ran
locally.

The added mathematical and integration suites passed in optimized and
ASan/UBSan builds, with leak detection and halt-on-UB enabled. No runtime
sanitizer findings were reported. GCC emitted Boost.PropertyTree
maybe-uninitialized warnings during compilation.

Coverage includes both signs of d, negative coordinates and k, finite-difference
initialization/boundaries, native/big agreement near the conservative limit,
prime roots including the degree drop at 13, actual small witnesses for C=3
and C=4, and 3,996 independently enumerated tiny-domain signed candidates.
C=1 is used by all shipped campaigns; alternate constants supply positive tests.

Integration tests exercised single/multiple workers, three-shard disjoint union,
stop-on-hit, thread changes on resume, out-of-order completion, truncated journal
replay, abrupt exits before and after witness/checkpoint persistence, backend
confusion rejection, and exact verification of both the symmetric and original
legacy large regression witnesses. Sharding is retained and tested, but the new
operating workflow is one process on one computer.

The inherited GitHub Actions workflow runs the full CMake/CTest suite in
Release and sanitizer builds after the implementation push. It includes the
three existing suites plus `symmetric_math` and `symmetric_backend`; check the
run for the implementation commit for its independently recorded status.

## Explicit regression

Input: the root polynomial, factors `[13,79,181,269,613]` and `-16<=k<=16`.
The real search path computed nine CRT roots and tested 594 signed candidates.
Two passed the residue sieve; one was an exact square and produced the supplied
known witness. The search was not given the CRT root or the coordinates.

## Automatically generated smooth-modulus campaign

The separate automatic run used exactly `configs/symmetric-smooth.json`:
all primes through 1000, five distinct prime factors, `2<=m<=10^11`,
`-16<=k<=16`, both signs of d, unsharded `0/1`, four workers.
No factorization, CRT root or known coordinates were passed as search inputs.

One 30-second invocation was followed by a bounded resume. It stopped after
recovering the same known witness, with **71.69797717 seconds cumulative search
wall time** (not including idle time between invocations or compilation).
This is one local observation, not a user-machine runtime prediction.

| Counter | Observed value |
|---|---:|
| Logical tiles | 10,842,302 |
| Moduli started | 10,054,331 |
| CRT roots started | 54,919,523 |
| Signed candidates | 3,624,688,518 |
| Nonnegative discriminants | 1,763,291,462 |
| Sieve survivors / exact square tests | 22,897,930 |
| Verified witnesses | 1 |

The result is:

```text
x = 723809206820
y = -631758385864
z = -245500254229
a = 92050820956
d = m = 30652208639 = 13*79*181*269*613
root = 94195039
k = 3
square_root = 1355567592684
residual = 0
```

`known-witness.jsonl` is the actual automatic-run record. Verify it using:

```sh
python3 tools/symmetric.py --results Experiments/reports/symmetric/known-witness.jsonl
```

The verifier accepted one record, one stable ID and one permutation class.
This is rediscovery of the user's existing witness, not a new mathematical
result, proof of minimality or claim about all solutions. The campaign remained
partial because stop-on-hit was enabled. The larger exploration preset was not
run for hours and its completion time is unknown.

## Source identity

The exploratory executable was built before commit creation and retains
`b375449+working-symmetric` / `pending` metadata in the raw result rather than
invented commit identifiers. Its source-content SHA-256 was
`315763de894874f237d800ad8a3b472ed4c48da64bff1ea542e435b2492a8cfe`.

Published source-content SHA-256 is
`2d20a52daf2fc9c2091658b46b46c09a9c000771674c0f9d676b5552613f098c`.
The only source difference from the measured build is whitespace joining the
two-line exception message in `src/math.cpp`; no algorithm changed. Source
identity is SHA-256 of the concatenated per-file SHA-256 digests of sorted
`src/*.cpp` and `src/*.hpp`, as used by CMake. Published files were checked
against their Git blob hashes. The implementation's remote CI builds those
published source bytes, not the exploratory executable.

Operating instructions: [SYMMETRIC.md](../../../SYMMETRIC.md).
