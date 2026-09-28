# Local validation and benchmark

These are historical issue #1 measurements, before certified even-modulus
pruning. For the current production-shaped pilot see [issue #2](issue2/README.md).

Implementation commit: `427e551ab4e57464eaca5150d2401642a939bfdf`.
Source-content SHA-256: `9255c78dd2f4636913318e9b738823532d275cb1b9c6e2f368007155495f2464`.

Measured on 28 September 2026. The machine reports 16 available CPUs, an AMD
Ryzen 9 7950X3D model string and KVM virtualization. These are guest-visible
properties; they are not a characterization of the proposed three servers.
GCC 15.2.0, Boost 1.90, OpenSSL 3.5.5, Python 3.14.4, Release build.

## Correctness

* `ctest --test-dir build --output-on-failure`: 2/2 suites passed, 5.40 seconds.
* `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build-sanitize --output-on-failure`: 2/2 suites passed, 69.45 seconds; no sanitizer findings.
* Synthetic matching-domain comparison: 29,034 signed candidates, 838 unique witnesses. Both x signs and d signs are exercised.
* One/multiple workers, native/wide paths, sieve on/off, three-shard candidate/work unions, restart offsets, signal stopping, hard-kill replay, corrupted state, initial-checkpoint interruption, verifier and merge tests passed.
* The final sanitizer multiworker run recorded three out-of-order batches; its candidate and witness sets matched the single-worker oracle.
* The documented small plan/search/resume/merge commands were run with three simulated local hosts. The aggregate was complete for that small domain.
* Stop-on-hit followed by ordinary resume reached completion.
* The real regression path computed nine CRT roots, searched 90 signed candidates, and recovered the supplied witness. Independent exact substitution accepted it; perturbed-coordinate tests reject it.

The CTest logs retain their actual command paths and timestamps. The tests were
run on the exact committed source content before creating the implementation
commit. Benchmark and selected regression artifacts were then generated with a
Release build reporting that commit. CI is configured but its remote execution
has not been observed here.

## Fixed-domain benchmark

Reproduce with:

```sh
python3 tools/benchmark.py --exe build/diophantasmagoria \
  --out Experiments/benchmark-reproduction --repeats 3
```

Every run exhausts g1 with prime coverage through 97, factor counts 2,3,4,
`10 <= m <= 1,000,000`, and `-256 <= k <= 256`, both signs of d.
There are 1,399 moduli, 3,407 roots, 12,654 tiles and **3,495,582 candidates**.
No witnesses were found in this configured benchmark domain.
Each row below is the median of three runs; process wall includes startup,
cache/config loading, search and final output. Worker counts 24 and 32 were
skipped because CPU affinity exposes only 16 CPUs.

| Workers | Process wall (s) | Candidates/s | Search (s) | Persistence (s) | Generation/CRT (s) | Worker candidate/sieve sum (s) | Worker square sum (s) |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 0.282138 | 12,389,598 | 0.276369 | 0.251872 | 0.014309 | 0.013083 | 0.000298 |
| 2 | 0.288183 | 12,129,740 | 0.282481 | 0.256432 | 0.014440 | 0.013289 | 0.000287 |
| 4 | 0.311520 | 11,221,062 | 0.305793 | 0.264653 | 0.014839 | 0.013725 | 0.000308 |
| 8 | 0.326760 | 10,697,708 | 0.320971 | 0.276707 | 0.014896 | 0.014796 | 0.000340 |
| 16 | 0.337520 | 10,356,668 | 0.331271 | 0.280946 | 0.015099 | 0.015980 | 0.000361 |

The one-worker no-sieve median was **0.418715 seconds** on the same domain.
The filters reduced exact-square calls from 1,752,078 nonnegative discriminants
to 4,596 survivors: 99.738% rejection among nonnegative discriminants. The
one-worker filtered median was about 1.48 times faster than the no-sieve
baseline end to end. Timers around exact-square calls themselves add some cost.

Persistence accounted for about 91% of one-worker search wall time. The fixed
64-tile durable commit policy dominates this short native workload; adding
workers does not improve it. This is a measured bottleneck and a possible future
tuning target, not evidence for linear scaling or a discovery-time estimate.
Generation overlaps workers and worker times are summed, so columns are not
additive wall-time components.

Database precomputation process wall was 0.036707 seconds;
the roots command's internal setup time was 0.034260 seconds.
Across all runs Linux peak RSS was 9,912–10,248 KiB.
This is a tiny cache; it does not estimate memory for a 20-million-prime database.

`benchmark.json` retains every measurement, configuration, source/compiler
metadata, setup time, rejection counts, CPU affinity and lscpu output. Raw
runtime directories remain ignored under `Experiments/benchmark-final/`.

## Scope of evidence

No 20-million-prime database, hour-long search, or remote three-host campaign
was run. The three-host commands use the tested interface and partitioning, but
their production throughput is unmeasured. Exhaustion applies only to the
configured square-free modulus/root/k domain. The regression witness is known;
neither an empty result nor a complete shard report proves nonexistence.
