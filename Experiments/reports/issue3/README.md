# Issue #3: bounded epochs and full-cache parallel scaling

Implementation commit: `c5c97bb`.

Measured locally on 28 September 2026 with the existing unmodified complete g1
cache through 20,000,000 and exactly `configs/campaign-g1.json`: three/four factors,
`10^8 <= m <= 10^13`, `k=-16..16`, shard 0/3. No wider k shell was needed.

The guest exposes 16 CPUs by affinity, reports AMD Ryzen 9 7950X3D and KVM;
24/32 workers were skipped. This does not establish physical-core topology or
predict performance on the user's other servers. GCC 15.2, Boost 1.90, Release.
Cache loading and configuration setup are measured separately from search.

Reproduction (reuses the cache; does not launch an unbounded campaign):

```sh
python3 tools/benchmark_parallel.py --exe build/diophantasmagoria \
  --config configs/campaign-g1.json --db Experiments/g1-20m.roots.jsonl \
  --out Experiments/parallel-reproduction --max-tasks 5000000 --repeats 3
```

Each of 15 runs completes exactly 5,000,000 logical tiles/moduli, 12,507,574 roots,
825,499,884 signed candidates and 2,317,948 square tests, with zero hits. The
arithmetic contract selects `cpp_int` for all tiles. The script asserts identical
mathematical counters, domain and final generator/root/k cursor across every run.
All reports remain partial; none exhausts the campaign. Cache precomputation was
not repeated. Worker counts rotate between repeats. No builds or other benchmark
jobs ran during this sweep.

## Measurements

Medians of three repetitions. Rates use search wall time; process wall includes
startup and final reporting. Worker candidate and square times are sums across
threads; generation overlaps them, so columns are not additive wall components.

| Workers | Setup s | Search s | Process wall s | Generation/CRT s | Candidate/sieve sum s | Square sum s | Persistence s |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 1.568 | 23.369 | 25.066 | 2.340 | 22.041 | 0.829 | 0.289 |
| 2 | 1.524 | 11.970 | 13.598 | 2.310 | 22.292 | 0.834 | 0.288 |
| 4 | 1.537 | 6.216 | 7.854 | 2.276 | 22.564 | 0.845 | 0.286 |
| 8 | 1.527 | 3.848 | 5.509 | 2.333 | 23.665 | 0.883 | 0.271 |
| 16 | 1.544 | 3.602 | 5.242 | 2.544 | 25.465 | 0.953 | 0.284 |

| Workers | Candidates/s | Logical tiles/s | Mean busy CPUs | Peak RSS KiB |
|---:|---:|---:|---:|---:|
| 1 | 35,325,186 | 213,962 | 1.11 | 153,732 |
| 2 | 68,961,202 | 417,694 | 2.18 | 153,744 |
| 4 | 132,802,467 | 804,376 | 4.24 | 153,764 |
| 8 | 214,526,069 | 1,299,371 | 7.19 | 153,840 |
| 16 | 229,188,840 | 1,388,182 | 8.32 | 153,964 |

Four workers are **3.76x** faster than one; eight are **6.07x** faster. Sixteen
improves throughput only another 6.8%. Start with **8 workers** on this machine,
or 16 if maximum local throughput matters more than the extra cores. Rerun this
sweep on each server rather than copying the recommendation unquestioningly.
CPU utilization is process user+system CPU / search wall: 7.19 busy CPUs is about
719% in the convention where one core is 100%; it includes the producer.

Persistence accounts for about 1.2% of one-worker search and 7.0% at eight workers,
compared with the historical issue #2 pilot's dominant persistence cost (that
pilot used a different cache/work budget and is not a direct speedup baseline).
Every run uses 20 epochs and 21 checkpoint replacements including initialization,
versus 78,125 batches for five million tiles at the old cadence. Journals are
created durably once; every measured run has **zero result appends and fsyncs**.
No witness durability guarantee is weakened: nonempty chunks stream records,
and dirty journals sync before the completed epoch's checkpoint is published.

## Remaining limit and implementation choices

An initial post-pipeline probe of one million tiles measured generation at
1.30 s out of 1.46 s search with four workers. Inspection identified eager SHA-256
formatting of every tile ID as avoidable producer work. IDs now use the identical
key/hash lazily when a trace or witness needs one. No generator/CRT parallelism
was added. In the final sweep generation/CRT still takes 2.33 s of 3.85 s at eight
workers and 2.54 s of 3.60 s at sixteen. This serial producer, queue/result
coordination and finite epoch drains explain the scaling plateau; the timings
do not establish a precise attribution for all remaining overhead.

Production defaults used here: 256 tiles per chunk, queue capacity twice the
worker count (minimum two), commits after 262144 issued tiles or two seconds.
Generation overlaps worker execution; only epoch boundaries drain all issued
work. Outstanding tasks/results are bounded by queue size, independently of the
number of completed epochs. The report includes producer wait, drain time,
queue peak, actual checkpoint writes and dirty journal I/O counts.

## Correctness and migration

Release: 3/3 CTest suites passed (13.09 s), including all previous oracle,
coefficient filter, arithmetic, shard, regression, signal and journal tests.
AddressSanitizer/UndefinedBehaviorSanitizer: 3/3 suites passed (99.91 s), with
leak detection and halt-on-UB enabled; no findings. Logs are retained alongside
this report. The issue #2 binary's dry-run also produced the same full-campaign
domain fingerprint as every new benchmark run.

The new epoch suite abruptly exits inside chunks and epochs, after witnesses are
streamed, after fsync, before replacement and after checkpoint publication.
It forces an out-of-order first chunk across the timed boundary, changes threads
and scheduler settings on resume, and checks exact coverage, task IDs, counters,
final cursors and independently verified hits. Unit tests check empty journal
operations perform no open and recovered uncommitted bytes still sync.

An authentic issue #2 v1 checkpoint and witness journal are resumed in tests;
its domain/cursor format remains valid and the next commit writes v2. Use the
same run directory/config/cache/shard with `--resume`. Issue #1 pre-filter cursors
remain explicitly incompatible. The old binary cannot read new v2 checkpoints.
See the [start/resume commands for all three hosts](../../../README.md#three-independent-hosts).

`benchmark.json` preserves every measurement, cache header, configuration,
affinity, lscpu output, build metadata and final cursor hashes. The benchmark
binary was built from the exact final application source (source SHA-256
`5d191d439733ef027b7c577cd75d75e8b9d9d6e1541cefcc85d6f71e892b2f12`)
before the implementation commit, so its build_commit field names the parent
`8e7613c`. Tests and documentation were finalized afterward. No remote CI run,
hour-long production campaign, new witness or nonexistence result is claimed.
