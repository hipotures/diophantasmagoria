# Issue #2: certified even-modulus pruning

Implementation commit: `8c22c3461328b35c7e6a82e5ada9970a64baa21a`.
Source-content SHA-256: `d3be41ecfa85d8d852bede5d6e2099d2b63243576c57c66404d828ecda1d25aa`.

Measured on 28 September 2026, on the local KVM guest with 16 available CPUs
and an AMD Ryzen 9 7950X3D model string. GCC 15.2.0, Boost 1.90, OpenSSL 3.5.5,
Python 3.14.4, Release build. Guest-visible core counts do not describe the
proposed three servers. No remote throughput was measured.

## Correctness and compatibility

* Release: `ctest --test-dir build --output-on-failure` passed 2/2 suites in 11.51 seconds.
* Sanitizers: `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build-sanitize --output-on-failure` passed 2/2 suites in 77.00 seconds, with no findings.
* Independent modulo-16 enumeration rules out `d=2 mod 4` for both targets. It finds feasible residues for the synthetic positive control.
* Generator-level tests inspect eligible primes and emitted factors before CRT. Traces cover three/four factors on each of three shards, with exact disjoint unions and unchanged retained work across thread counts.
* Named/coefficient modes, explicit even/odd lists, all-excluded lists, and dry-run exclusion reports pass. Unfiltered references separately account for excluded candidates; synthetic/custom even witnesses `(-2,3,-1)` and `(2,1,-3)` remain present.
* The authentic pre-filter cache is reused byte-for-byte; its prime-2 row remains correct. An authentic pre-filter checkpoint is rejected before cursor or journal restoration with an actionable version error.
* Filtered checkpoint replay, generic and filtered SIGINT/SIGTERM/SIGKILL recovery, and thread changes on resume pass.
* The regression target still recovers the existing large witness from its factors: nine CRT roots, 90 signed candidates, one independently verified witness.

Tests ran on the exact committed application source content before the commit;
the selected benchmark and regression artifacts then ran with the commit in
their build metadata. The checked-in CTest logs retain actual timestamps and
command paths. Remote CI was not run or observed as part of this task.

## Representative production-shaped cache and work

The cache covers **every prime through 2,000,000**, not the full 20,000,000
campaign limit: 148,933 prime rows and 99,227 root-bearing primes, including 2.
After certified pruning the generator has 99,226 eligible primes. The complete
database is unchanged, with internal checksum:

```text
e84913276ab9c99474a17a18032ab51a666e7edcc693b1524d4e5c1dedf2e980
```

One initial precomputation took 1.223325 seconds process wall
(1.220345 seconds reported inside the roots command).
The final benchmark reused that cache; its precomputation fields are therefore
null, not zero-cost regeneration. `precomputation.json` records the actual setup.

Configuration: `configs/pilot-g1.json`, factor counts 3 and 4,
`10^8 <= m <= 10^13`, `-16 <= k <= 16`, both signs of d, shard 0/3.
Every run issues exactly 20,000 tiles from the same deterministic start. This
budget samples only the first part of the three-factor stage; it is not a
completed campaign and does not benchmark the later four-factor stage.

All 15 runs match these counters **and the final generator/root/k cursor**:

| Quantity | Actual count |
|---|---:|
| Tasks | 20,000 |
| Moduli | 20,000 |
| CRT roots | 29,918 |
| Signed candidates | 1,974,588 |
| Nonnegative discriminants | 1,024,995 |
| Sieve survivors / exact-square calls | 5,482 |
| Verified witnesses | 0 |
| Arbitrary-precision tasks | 20,000 |

The independent first-tile trace starts at **m=100000579**, an odd modulus.
There are 5,482 sieve survivors, so the pilot reaches candidates not eliminated
by the discriminant filters. This demonstrates useful routing, not the existence
of a solution. The declared large modulus domain correctly selects arbitrary
precision. No CRT root or witness is supplied as a search input.

## Fixed-work worker sweep

Reproduce after one precomputation, reusing the same cache for every run:

```sh
build/diophantasmagoria roots --polynomial g1 --limit 2000000 \
  --out Experiments/g1-2m.roots.jsonl
python3 tools/benchmark_pilot.py --exe build/diophantasmagoria \
  --config configs/pilot-g1.json --db Experiments/g1-2m.roots.jsonl \
  --out Experiments/benchmark-pilot-reproduction --max-tasks 20000 --repeats 3
```

Each table entry is a median of three runs. Run order rotates between repeats.
Worker counts 24 and 32 are skipped because affinity exposes 16 CPUs. Setup is
cache/config loading and initialization; search timing includes persistence.
Process wall also includes startup, final output and process teardown.

| Workers | Setup (s) | Search (s) | Process wall (s) | Candidates/search second | Persistence (s) | Generation/CRT (s) | Candidate/sieve worker sum (s) | Square worker sum (s) |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 0.178427 | 0.462887 | 0.672064 | 4,265,812 | 0.398775 | 0.031646 | 0.047560 | 0.001883 |
| 2 | 0.172718 | 0.466077 | 0.666396 | 4,236,613 | 0.409394 | 0.034589 | 0.048936 | 0.001987 |
| 4 | 0.179739 | 0.473526 | 0.675248 | 4,169,965 | 0.402067 | 0.034184 | 0.053141 | 0.002183 |
| 8 | 0.177400 | 0.513117 | 0.728293 | 3,848,223 | 0.429721 | 0.036470 | 0.066122 | 0.003095 |
| 16 | 0.173770 | 0.531532 | 0.732815 | 3,714,897 | 0.436220 | 0.036436 | 0.077448 | 0.003572 |

One and two workers are effectively tied at this precision: one has the lowest
median search time, two the lowest median process wall. The documented default
of **two workers** is a small, measured starting point, not a statistically
established advantage over one. Four through sixteen were slower in this sweep.
Retest on each server; physical cores, SMT and 128 GB RAM do not justify a
worker-count assumption.

The fixed 64-tile durable commit policy accounts for approximately 86–88% of
search time for one/two workers. It remains the dominant measured bottleneck.
Generation overlaps worker execution and worker times are summed, so timing
columns are not additive wall components. No persistence/concurrency redesign
was included in this fix.

Linux peak RSS was 26,728–26,940 KiB.
This applies to the declared representative cache, not a memory estimate for
the full cache or the proposed servers.

## Operational checks and scope

The README pilot commands were exercised: cache reuse, dry-run, a 20,000-tile
start, and a five-second resume. The resumed report remained partial. Three
simulated local hosts also started and resumed under short time limits using
the representative cache; aggregation reported all three present and incomplete,
never complete. Unit/integration tests separately exhaust small domains and
prove the three-shard disjoint union.

No full 20-million-prime cache, hour-long run, or remote-host campaign was
started. Counts of excluded generated moduli were not computed here. The
generator reports that region as analytically excluded without enumeration,
rather than counting it as searched. The full-cache figures in issue #2 are
independent review measurements, not measurements reproduced by this report.

All hosts must use new output directories with generator
`lexicographic-prefix2-v2` and filter `even-square-free-g1-g2-v1`. Old root
databases remain reusable, but old checkpoints and mixed old/new shard results
are incompatible. The proof assumes square-free moduli and must not be applied
unchanged to future prime-power searches. Finite exhaustion proves no general
nonexistence result.
