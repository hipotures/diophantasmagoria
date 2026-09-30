# Symmetric cubic search on one computer

Branch: `feature/symmetric-cubic-one-host`. Implementation task: issue #4.
Base: `b3754497e6243e870e54686fd9c8b8d1a3baac61`, the published optimized
main branch containing the #3 chunked-epoch scheduler. When this branch was
created, GitHub exposed no separate optimization branch. Main remains unchanged.

The new backend searches

```text
x^3 + 2*x + y^3 + 2*y + z^3 + 2*z = x*y*z + 1.
```

It reuses the optimized CPU workers, modular roots, CRT, exact arithmetic,
quadratic-residue sieve, checkpoint/resume and 60-second progress reporting.
It does not replace the existing g1/g2/regression engine.

## Isolate the checkout from ongoing searches

Run once from the original repository. The separate worktree avoids changing
source files, binaries, configurations or results used by existing processes.
The worktree is intentionally detached at the new branch's current revision.

```sh
git fetch origin
git worktree add --detach ../diophantasmagoria-symmetric origin/feature/symmetric-cubic-one-host
cd ../diophantasmagoria-symmetric
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 4
ctest --test-dir build --output-on-failure
```

Dependencies are unchanged: a C++23 compiler supporting __int128, CMake 3.20+,
Boost headers 1.74+, OpenSSL Crypto development files, threads and Python 3.11+.
Use the existing README dependency instructions. No CAS or GPU is required.

## First run: automatically generated smooth moduli

This is the useful initial campaign, NOT an explicit-modulus regression.
The program receives all primes through 1000, five distinct factors, modulus
bounds and a k interval. No factorization, CRT root, coordinate, or special
known-solution shortcut is supplied to the search.

```sh
build/diophantasmagoria roots --polynomial symmetric --limit 1000 \
  --out Experiments/symmetric-1000.roots.jsonl
build/diophantasmagoria search --config configs/symmetric-smooth.json \
  --db Experiments/symmetric-1000.roots.jsonl \
  --out Experiments/symmetric-smooth-run \
  --threads 8 --shard 0/1 --seconds 14400 --stop-on-hit
```

The four-hour limit is a maximum per invocation, not a prediction. A hit or
exhaustion ends the run earlier. `--threads 8` is a starting point inherited
from the optimized engine, not a new hardware-specific scaling guarantee.
Only ONE process on ONE computer is needed. Thread count can change on resume.
Do not reuse a g1/g2 root cache: its polynomial is different.

Domain: square-free `2 <= m <= 10^11`, exactly five distinct prime factors,
each at most 1000, all modular roots, `-16 <= k <= 16`, both signs of d.
The supplied known solution lies inside this domain. A local bounded run of
this automatic campaign recovered it; see the validation report. This is
rediscovery of an existing witness, not a novel result or a minimality claim.

## Resume without losing committed work

Use the SAME config, database, output directory and shard. Do not regenerate
or substitute the cache while a campaign is active.

```sh
build/diophantasmagoria search --config configs/symmetric-smooth.json \
  --db Experiments/symmetric-1000.roots.jsonl \
  --out Experiments/symmetric-smooth-run \
  --threads 8 --shard 0/1 --seconds 14400 --stop-on-hit --resume
```

Ctrl-C/SIGTERM drains issued work and saves a durable checkpoint. A hard kill
can replay uncommitted work but must not skip it. Existing g1/g2 directories are
not valid symmetric checkpoints; their unchanged backend remains available.
The mathematical domain fingerprint, including the new backend identity,
prevents cross-equation reuse. Scheduler settings are not mathematical bounds.

## Read and independently verify results

```sh
python3 tools/symmetric.py --results Experiments/symmetric-smooth-run/results.jsonl
python3 tools/oracle.py verify --results Experiments/symmetric-smooth-run/results.jsonl
```

Each hit contains x,y,z,a,d,m, factors, CRT root, k, discriminant, square root,
backend identity, exact residual and provenance. Arbitrarily large integers
are decimal strings. The independent verifier substitutes in the ORIGINAL
symmetric equation and checks the derivation through b=xy.

`--stop-on-hit` stops issuance after a completed chunk reports a hit, drains
in-flight chunks, syncs results and checkpoints, then exits. Several hits may
be recorded. Without that flag, the search continues until its limit or domain
exhaustion. To continue beyond an existing hit, resume without `--stop-on-hit`.
Other processes are never automatically stopped or notified.

The pair is ordered x>=y; the full equation is invariant under permutations.
The same unordered triple can be represented with another choice of z.
`tools/symmetric.py` reports both raw record and permutation-class counts.
Crash replay can also repeat a record. `tools/merge.py` deduplicates stable IDs
and supports these records; it refuses cross-backend/cross-domain mixing.

## Optional larger exploration

Use a NEW cache and output directory; this is not a checkpoint extension.
It overlaps the smooth campaign and may rediscover the same known solution.
It is not a guaranteed new-solution search. Omit `--stop-on-hit` to keep
collecting witnesses throughout the time budget.

```sh
build/diophantasmagoria roots --polynomial symmetric --limit 10000 \
  --out Experiments/symmetric-10000.roots.jsonl
build/diophantasmagoria search --config configs/symmetric-explore.json \
  --db Experiments/symmetric-10000.roots.jsonl \
  --out Experiments/symmetric-explore-run \
  --threads 8 --shard 0/1 --seconds 14400
```

This uses factor counts 3,4,5, primes through 10000, `m<=10^13` and
`-64<=k<=64`. Stages are lexicographic and factor-count ordered; a short run
need not reach the five-factor stage. No automatic multi-hour run was performed.
Finite exhaustion only concerns the configured heuristic domain, not all
integer triples or a proof of nonexistence.

## Explicit known-witness regression

Unlike the automatic campaign, this test supplies the factorization of m.
It still computes all modular roots, CRT, the discriminant and reconstruction.

```sh
build/diophantasmagoria roots --polynomial symmetric --primes 13,79,181,269,613 \
  --out Experiments/symmetric-regression.roots.jsonl
build/diophantasmagoria search --config configs/symmetric-regression.json \
  --db Experiments/symmetric-regression.roots.jsonl \
  --out Experiments/symmetric-regression-run --threads 2 --shard 0/1
python3 tools/symmetric.py --results Experiments/symmetric-regression-run/results.jsonl
```

Expected witness: `(723809206820, -631758385864, -245500254229)`.
The normal path computes 9 CRT roots and 594 signed candidates in this window.
Forcing native 128-bit arithmetic is conservatively rejected for the declared
regression domain. Use `--arithmetic auto` (default) or `big`.

Mathematics and safety: [docs/symmetric-cubic.md](docs/symmetric-cubic.md).
Validation: [Experiments/reports/symmetric/README.md](Experiments/reports/symmetric/README.md).
