# Diophantasmagoria

A resumable Linux C++23 searcher for integer solutions of `y*z*(y+z) = G(x)`.
The shared coefficient engine includes `g1 = x^3+x^2+3*x-1`,
`g2 = 6*x^3+x^2+1`, and `regression = 3*x^3+x^2+x-1`.

The production domain consists of square-free positive moduli, **both signs of
`d=y+z`**, every modular root, and explicit signed k intervals. A certified modulo-16
filter excludes even square-free moduli for exact g1/g2 coefficients before
combination generation; generic polynomials retain admissible even moduli. It is a heuristic
subset of all integer triples. Exhausting a configured domain does not prove
nonexistence. The large regression witness is an existing witness, not a discovery.
The original [LICENSE](LICENSE) is retained.

## Build and test

Dependencies: a C++23 compiler with `__int128`, CMake 3.20+, Boost headers 1.74+,
OpenSSL development headers/library (SHA-256 only), POSIX threads, and Python 3.11+. No CAS is used.
GCC 15.2, Boost 1.90, OpenSSL 3.5.5 and Python 3.14 were tested locally.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 4
ctest --test-dir build --output-on-failure

cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug -DSANITIZE=ON
cmake --build build-sanitize -j 3
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build-sanitize --output-on-failure
```

No command requires sudo. If headers are missing, unpack a distribution's Boost
development package or a Boost source archive into a user-owned directory. For
example, `apt download libboost1.88-dev` followed by
`dpkg-deb -x libboost1.88-dev_*.deb .deps/boost` works where that package is
available. Pass `-DBoost_NO_BOOST_CMAKE=ON -DBOOST_ROOT="$PWD/.deps/boost/usr"`
to CMake. For a user-local OpenSSL installation pass
`-DOPENSSL_ROOT_DIR=/absolute/prefix`; for a compiler pass
`-DCMAKE_CXX_COMPILER=/absolute/path/to/g++`. Check package availability on your
own distribution; installing the `libboost-dev` and `libssl-dev` development
packages through your usual package manager also works.

## Quick smoke search

Run from the repository root. Output directories are intentionally exclusive;
use a new directory for a new search or `--resume` for an existing one.

```sh
build/diophantasmagoria roots --polynomial g1 --limit 97 \
  --out Experiments/g1-97.roots.jsonl
build/diophantasmagoria search --config configs/smoke.json \
  --db Experiments/g1-97.roots.jsonl --out Experiments/smoke-run --threads 4
python3 tools/oracle.py verify --results Experiments/smoke-run/results.jsonl
```

The bounded smoke run enumerates admissible odd moduli for g1. An empty hit
file is expected for this smoke test. Its counters exclude analytically ruled-out
even candidates; the database still contains the correct prime-2 row. Completeness tests also use `x^3-x`, with
actual positive and negative solutions; they do not rely on empty real-target
results. Root databases are generated once, then loaded read-only by searches.

## Rediscover and verify the known witness

The following computes the prime roots, all CRT combinations, finite differences,
residue filters, exact squares, and reconstruction through the normal engine.
It takes only the factorization and k window as search inputs.

```sh
build/diophantasmagoria roots --polynomial regression \
  --primes 127,1783,4236203 --out Experiments/regression.roots.jsonl
build/diophantasmagoria search --config configs/regression.json \
  --db Experiments/regression.roots.jsonl --out Experiments/regression-run --threads 2
python3 tools/oracle.py verify --results Experiments/regression-run/results.jsonl
python3 tools/oracle.py verify --polynomial regression \
  --triple -783954692511 1797526169071 -838276125548
```

This produces the supplied witness after 90 signed candidates from nine CRT
roots. Automatic arithmetic selects arbitrary precision. Forcing `--arithmetic
128` on this declared domain is conservatively rejected. Perturbing the triple
fails exact verification.

## Short pilot with a reusable representative cache

This bounded pilot keeps the production modulus bounds and k window while using
complete prime coverage **through 2,000,000**, instead of through 20,000,000.
Generate the cache once; skip precomputation if it already exists. It is also
valid to reuse a complete larger cache for this smaller configured prime limit.

```sh
build/diophantasmagoria roots --polynomial g1 --limit 2000000 \
  --out Experiments/g1-2m.roots.jsonl
build/diophantasmagoria search --config configs/pilot-g1.json \
  --db Experiments/g1-2m.roots.jsonl --out Experiments/pilot-g1-v2 \
  --threads 2 --shard 0/3 --dry-run
build/diophantasmagoria search --config configs/pilot-g1.json \
  --db Experiments/g1-2m.roots.jsonl --out Experiments/pilot-g1-v2 \
  --threads 2 --shard 0/3 --max-tasks 20000
build/diophantasmagoria search --config configs/pilot-g1.json \
  --db Experiments/g1-2m.roots.jsonl --out Experiments/pilot-g1-v2 \
  --threads 2 --shard 0/3 --seconds 5 --resume
```

The first 20,000 tiles on shard 0/3 start at odd m=100000579 and include 1,974,588
signed candidates and 5,482 sieve survivors/exact-square tests. This is a partial
prefix of the three-factor stage, not exhaustion of either factor-count family.
The declared `m_max=10^13` correctly selects arbitrary precision even though this
initial work is near m=10^8. There is no injected root or positive control shortcut.

Run the fixed-work worker sweep with the same cached data:

```sh
python3 tools/benchmark_pilot.py --exe build/diophantasmagoria \
  --config configs/pilot-g1.json --db Experiments/g1-2m.roots.jsonl \
  --out Experiments/benchmark-pilot-local --max-tasks 20000 --repeats 3
```

The tool compares counters **and the final generator/root/k cursor** across every
worker count, separates cache setup from search, records a first-tile trace,
and requires nonzero sieve survivors. Omitting `--db` generates one cache in the
benchmark output directory and reports precomputation separately. For a pilot
with the full cache, substitute `configs/campaign-g1.json` and your unchanged
`Experiments/g1-20m.roots.jsonl`; retain the explicit short work budget.

The historical [issue #2 measurements](Experiments/reports/issue2/README.md)
predate the chunked scheduler. For current production tuning, run the full-cache
sweep (five million identical logical tiles per run, three repeats):

```sh
python3 tools/benchmark_parallel.py --exe build/diophantasmagoria \
  --db Experiments/g1-20m.roots.jsonl --out Experiments/benchmark-parallel-local
```

Start with **eight workers** on the measured local machine; 16 gives only a small
additional throughput gain. Rerun the sweep on each server: the producer becomes
significant at higher worker counts. See [full-cache measurements](Experiments/reports/issue3/README.md).

Production defaults aggregate 256 logical tiles per worker chunk, keep at most
`max(2, 2*threads)` chunks in flight, and checkpoint after two seconds or 262144
issued tiles. Tune with `--chunk-tiles`, `--queue-chunks`, `--checkpoint-seconds`,
and `--checkpoint-tiles`. Each commit drains issued work and fsyncs actual witness
records before replacing the checkpoint. Empty result appends perform no I/O.
These settings can change on resume without changing the mathematical domain.

## Three independent hosts

These **opt-in, potentially very large** presets are not benchmark estimates.
Reuse your existing complete g1 database unchanged. If none is available, prepare
one with the command below. The 20,000,000 prime limit is configurable, not required
for a smoke run. SIGINT/SIGTERM abort precomputation without publishing a partial
cache. Interrupted precomputation restarts from scratch.

```sh
build/diophantasmagoria roots --polynomial g1 --limit 20000000 \
  --out Experiments/g1-20m.roots.jsonl
```

**Upgrade from issue #2:** stop the old process gracefully, back up its entire run
directory, then use the new binary with the same config/cache/shard and `--resume`.
Its checkpoint is accepted and rewritten as schema v2 at the next commit; the
domain, cursor, stable task IDs and witness chain are unchanged. All scheduler
settings, including worker count, may change. An authentic issue #2 checkpoint
is exercised by the migration test. The old binary cannot read the new schema.

**Upgrade from issue #1:** keep your root database, but start every host with the
new binary and a **new output directory**. Pre-filter checkpoints are rejected:
the eligible prime list and its shard indices have changed. Old and new results
belong to different domain fingerprints and cannot be merged as one campaign.

Copy exactly `configs/campaign-g1.json` and `Experiments/g1-20m.roots.jsonl` to
**each** host, along with the same source revision/build and the Python tools.
Keep these relative paths, or adjust the command paths. Use the same database
bytes; its checksum is part of the domain identity. No checkpoint, shared
filesystem, service, or live connection between hosts is needed. First inspect
one plan; this does not enumerate the campaign or invent a total/ETA:

```sh
build/diophantasmagoria search --config configs/campaign-g1.json \
  --db Experiments/g1-20m.roots.jsonl --out Experiments/host0-g1-v2 \
  --threads 8 --shard 0/3 --dry-run
```

Host 0:

```sh
build/diophantasmagoria search --config configs/campaign-g1.json \
  --db Experiments/g1-20m.roots.jsonl --out Experiments/host0-g1-v2 \
  --threads 8 --shard 0/3 --seconds 3600
```

Host 1:

```sh
build/diophantasmagoria search --config configs/campaign-g1.json \
  --db Experiments/g1-20m.roots.jsonl --out Experiments/host1-g1-v2 \
  --threads 8 --shard 1/3 --seconds 3600
```

Host 2:

```sh
build/diophantasmagoria search --config configs/campaign-g1.json \
  --db Experiments/g1-20m.roots.jsonl --out Experiments/host2-g1-v2 \
  --threads 8 --shard 2/3 --seconds 3600
```

The same interfaces are covered by the three-shard integration tests. The bounded
benchmark uses the full 20-million-prime cache and this exact campaign config;
no hour-long or remote-host campaign was started.
For g2, generate a separate database with `--polynomial g2` and use
`configs/campaign-g2.json` and separate output paths. Coefficient/cache mismatches
are rejected.

Reported core/thread counts are not assumed to mean physical cores. Benchmark
1, 2, 4, 8, 16, 24 and 32 workers where CPU affinity permits. Short k windows can become producer-limited; more workers then add little
throughput. `--stop-on-hit` finishes the current bounded queue and stops **only that
process**, not the other hosts.

## Stop, resume, extend, merge

Ctrl-C, SIGTERM, `--seconds`, and `--max-tasks` stop issuance and commit finished
bounded work. No automatic subsequent stage is launched. Use `--resume` with the
same configuration, cache and shard; changing local scheduler settings is allowed.
Run the matching command on each host:

```sh
# Host 0
build/diophantasmagoria search --config configs/campaign-g1.json \
  --db Experiments/g1-20m.roots.jsonl --out Experiments/host0-g1-v2 \
  --threads 8 --shard 0/3 --seconds 3600 --resume
# Host 1
build/diophantasmagoria search --config configs/campaign-g1.json \
  --db Experiments/g1-20m.roots.jsonl --out Experiments/host1-g1-v2 \
  --threads 8 --shard 1/3 --seconds 3600 --resume
# Host 2
build/diophantasmagoria search --config configs/campaign-g1.json \
  --db Experiments/g1-20m.roots.jsonl --out Experiments/host2-g1-v2 \
  --threads 8 --shard 2/3 --seconds 3600 --resume
```

After SIGKILL, the last uncommitted epoch can be replayed. Stable witness IDs
allow deduplication. A corrupt checkpoint or committed result journal causes an
error, not a fresh search under an old output directory. Keep the entire run
directory when copying or backing up state. Only a truncated final append record
is automatically removed on resume. See [recovery details](docs/design.md).

`configs/shell64-g1.json` searches `[-64,-17] ∪ [17,64]`;
`configs/shell256-g1.json` searches `[-256,-65] ∪ [65,256]`.
Use each as a **new** three-shard campaign with new output directories and the
same root database. These shells avoid repeating previous k values. A changed
domain or shard count cannot reuse an old checkpoint. Merge each shell separately.

After copying the three run directories onto one machine:

```sh
python3 tools/merge.py Experiments/host0-g1-v2 Experiments/host1-g1-v2 \
  Experiments/host2-g1-v2 --out Experiments/merged-g1-v2
python3 tools/oracle.py verify --results Experiments/merged-g1-v2/results.jsonl
```

Merging verifies witnesses and provenance, rejects incompatible domains/shard
layouts, deduplicates replay records, and lists missing or incomplete shards.
A run killed without a final report remains partial. Merge only stopped/copied
run directories; it is not a snapshot protocol for concurrently changing files.
Even a complete aggregate means only that the configured domain was exhausted.

## Configuration and diagnostics

All intervals are inclusive. Configuration supports a named `polynomial` or
`coefficients: [E,C,B,A]`, `m_min`, `m_max`, `prime_limit`, `factor_counts` (1–5),
and either `k_min`/`k_max` or disjoint `k_ranges`. Explicit `moduli: [[p,q,...],...]`
replaces prime-limit/factor-count generation. It still uses database prime roots
and normal CRT. Custom database generation accepts `--coefficients E,C,B,A`.
All factors must be distinct primes present in the cache. For g1/g2, explicit
lists retain their requested indices for sharding, but even entries are reported
as analytically excluded and skipped before CRT. An all-excluded explicit list
finishes with zero tasks. Dry-run/manifest `analytical_exclusions` describes this
rule and the exact excluded explicit-list count; generated excluded-region totals
are not enumerated. `domain_definition.local_filter` and the generator version
bind the behavior to the fingerprint. No prime/root database rows are removed.

Supported limits: `2 <= m <= 2^63-1`, `|k| <= 10^12`, prime limit at most
100,000,000, at most five factors, and at most 1,000,000 CRT roots per modulus.
Unsupported limits fail explicitly. `m=1` and production `d=0` are excluded;
the named polynomials have no integer roots. The Python box oracle includes
synthetic `d=0` cases. Prime powers/Hensel lifting are deferred.

`--arithmetic auto` chooses a conservatively proven native 128-bit path for the
whole domain, otherwise unbounded Boost integers. `--arithmetic big` forces the
latter; unsafe `--arithmetic 128` is rejected. A plain unchecked `int256_t` is
never used. All potentially large JSON integers are decimal **strings**.

`--trace` records every `(m,r,k,sign,task)` for tiny coverage tests; leave it off
for campaigns. `--no-sieve` is a measurement control with identical coverage.
The immutable domain fingerprint excludes worker count, timing limits, and
arithmetic/sieve implementation choices; it includes coefficients, database
identity, generator/filter/tile version, factors, modulus bounds, signs and k intervals.

Each run writes `manifest.json`, `checkpoint.json`, `results.jsonl`, and
`report.json`. Reports include source/build identity, setup and search timing,
root/CRT generation time, candidate/filter and square time, persistence time,
counters, memory, shard and workers. Worker times are sums, not elapsed wall
components: generation overlaps workers. `invocation_metrics` reports chunks,
epochs, checkpoint writes, dirty journal append/fsync counts, queue peak, producer
wait and epoch drain time. `invocation_cpu_seconds` and `invocation_mean_busy_cpus`
exclude setup; Linux peak RSS includes setup. `out_of_order_batches` retains its
legacy name and now counts epochs completed out of order. On resume timings/counters cover committed work; crash
replay overhead is not retrospectively recovered. Manifest settings describe the
latest invocation; mixed worker/arithmetic histories can be distinguished by
witness build identities and cumulative native/big task counts.

## Small-domain benchmark and oracle

```sh
python3 tools/benchmark.py --exe build/diophantasmagoria \
  --out Experiments/benchmark-local --repeats 3
python3 tools/oracle.py box --polynomial synthetic --bound 8
python3 tools/oracle.py structured --config configs/smoke.json
```

This tiny-prime benchmark is a correctness/performance diagnostic, not a
production-readiness claim. Use `benchmark_parallel.py` above for full-cache production scaling. The small benchmark fixes the complete candidate domain for every worker count and
includes a no-sieve baseline. It skips requested worker counts above CPU affinity.
Its reports retain process wall time, setup/precomputation, counters, timing
breakdown and memory. Selected actual local measurements are in
[Experiments/reports](Experiments/reports); they do not predict three-server
throughput or time to a new solution.

The oracle's coordinate box and structured modulus/root/k domain are different
notions of bounds. See [mathematics and arithmetic proof](docs/mathematics.md),
[implementation and recovery](docs/design.md), and the executable tests in
[tests](tests). Large caches, runtime state and builds are git-ignored under
`Experiments/` or a user-selected output directory.
