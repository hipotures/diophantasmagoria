# Issue #6 evidence

Branch: `feature/symmetric-cubic-one-host`; baseline revision
`ec2d4c3337296b56430d0a2e14915deaff15390c`. Main was not merged.
The mathematics, domain controls and operating commands are in
[docs/symmetric-next-search.md](../../../docs/symmetric-next-search.md).

## Completed arithmetic regions

Both regions use C=1, primes through 10,000, `2 <= m <= 10^13`, both signs
and every `-256 <= k <= 256`. All counts refer to root-bearing moduli.

| Completed region | Moduli | CRT roots | Candidates | Exact squares called | Hits | Search wall | Peak RSS |
|---|---:|---:|---:|---:|---:|---:|---:|
| Square-free 1–2 distinct primes | 311,655 | 698,564 | 716,726,664 | 3,935,622 | 0 | 5.2271 s | 10,596 KiB |
| Exactly one exponent two, 1–2 distinct primes | 622,521 | 1,394,765 | 1,431,028,890 | 7,876,579 | 0 | 11.8527 s | 11,048 KiB |

The first run followed a 90,000-tile bounded prefix. Its old checkpoint was
successfully resumed by the final binary with the same fingerprint and counts.
The second followed million-tile bounded benchmarks and a complete exact plan.
Both complete invocations had a 30-second ceiling. Independent verification
returned zero records and zero permutation classes. Reports and empty result
journals are checked in; no larger campaign completed or ran without a budget.

## Plans and benchmarks

`primepowers-plan.json` counts the entire new {1,2}-exponent domain with at
least one square and 1–5 distinct primes: 2,118,382,830 moduli,
10,540,102,736 CRT roots and 10,814,145,407,136 candidates. It completed by
aggregating last-factor intervals in 2.97 seconds. The first completed stage
leaves 10,812,714,378,246 candidates across the two proposed large stages.

The JSON plans for six factors, larger prime limits and the outer k shell are
exact and unsharded. `projections.json` divides exact candidate volumes by
observed process wall throughput endpoints. Its intervals are extrapolations,
not confidence intervals or guaranteed completion times.

`throughput-benchmark.json` contains three million-tile repeats for each of
the one-square factor strata and the larger-prime low-factor case.
`power-stage-throughput.json` contains three repeats each of the aggregate
power campaign and the multiple-square campaign on the final source build.
Each search used four workers, a million-tile maximum and a 30-second ceiling.
The driver checks identical counters, final generator cursors and independently
verified witness sets. Measurements contain both process/search wall time,
peak RSS, setup/persistence/generation time and exact-square counts.

Environment: GCC 15.2, Boost 1.90, Python 3.14.4, Linux VM exposing 12 CPUs
on an AMD Ryzen 9 7950X3D host. Cache headers, root counts and checksums are in
`root-cache-metadata.json`; caches themselves are not committed. The 1M cache
was used for the first throughput sweep, and its load is included in process
wall time. The final power-stage sweep used the original 10K cache.

The Phase A report predates the implementation. Plan/sieve/first-sweep reports
also record their actual intermediate source identities. The final search
source identity is `b6d9dd779dbb121cc8a85663a5992004d67130f5d7e3714993068e6b335f3670`;
the final stage sweep and completed short power run use those source bytes.
The earlier throughput sweep differs in the later planner resource guard,
plan progress message and empty-array metadata serialization. Its search
kernels are the same. Report `build_commit` names the checked-out baseline
at compilation; `build_source` identifies the measured source contents.

Reproduce a bounded power benchmark after creating/reusing the 10K cache:

```sh
python3 tools/benchmark_symmetric.py \
  --config configs/symmetric-primepowers256.json configs/symmetric-multiple-squares256.json \
  --db Experiments/symmetric-10000.roots.jsonl --out Experiments/power-benchmark-new \
  --threads 4 --max-tasks 1000000 --seconds 30 --repeats 3
```

Example research plan (six factors are accepted only by `plan`):

```sh
build/diophantasmagoria plan --config Experiments/reports/issue6/six-factor.json \
  --db Experiments/symmetric-10000.roots.jsonl --seconds 30
```

## Rejected factor-aware sieve

`factor-sieve-benchmark.json` records three alternating A/B repeats for the
same million-tile prefixes. The prototype reduced exact-square calls by about
75%, but median process wall throughput changed by -2.52%, +0.21% and -0.83%
for square-free 3, square-free 5 and one-square 5-factor prefixes respectively.
Its per-root mask cost did not produce a repeatable end-to-end improvement.
Production keeps the existing fixed-modulus sieve.

The mathematical prototype is retained in `tests/factor_sieve_prototype.hpp`.
The tests compare the congruence modulo each prime power against exact Big
discriminants, compare masks against directly enumerated local square residues,
and exhaust all small root-bearing moduli through 500 and k=-4..4 with C=1,3,4.
Positive controls include singular roots and genuine symmetric witnesses.
The integration suite was also run with the prototype enabled before removal:
fixed, disabled and factor-aware paths agreed on tiny-domain coverage and hits,
including prime powers. No false negative was found.

For reproducing the measured CLI prototype, apply the patch in a separate
checkout, build it, then use the retained comparison mode. The patch affects
the experimental checkout only:

```sh
git worktree add --detach ../diophantasmagoria-factor-sieve HEAD
cd ../diophantasmagoria-factor-sieve
git apply Experiments/reports/issue6/factor-sieve-prototype.patch
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 4
build/diophantasmagoria roots --polynomial symmetric --limit 10000 \
  --out Experiments/symmetric-10000.roots.jsonl
python3 tools/benchmark_symmetric.py \
  --config Experiments/reports/issue6/squarefree-3.json \
    Experiments/reports/issue6/squarefree-5.json Experiments/reports/issue6/powers-5.json \
  --db Experiments/symmetric-10000.roots.jsonl --out Experiments/factor-sieve-ab-new \
  --threads 4 --max-tasks 1000000 --seconds 30 --repeats 3 --compare-factor-sieve
```

## Validation

All five CTest suites passed in Release (15.95 seconds) and Debug ASan/UBSan
(121.83 seconds), with `ASAN_OPTIONS=detect_leaks=1` and
`UBSAN_OPTIONS=halt_on_error=1`. Logs are retained. The added tests cover full
residue comparisons for many small prime powers and singular synthetic
polynomials, a 2^62 simple lift, CRT over 8/9/25, tiny power-domain oracle
agreement in native and Big kernels, exact planner counts and interrupted
planning, disjoint shards, resume/thread changes, abrupt exit recovery,
exponent mismatch rejection, old witnesses and explicit power metadata.
Campaign definition tests assert disjointness from all-exponent-one coverage
and disjoint union of the three new stages.

`regression-witness.jsonl` is the known C=1 witness from the validated test
suite. `regression-verify.json` records one independently verified permutation
class; it is a regression, not a discovery. Verify the saved records directly:

```sh
python3 tools/symmetric.py --results Experiments/reports/issue6/regression-witness.jsonl
python3 tools/symmetric.py --results Experiments/reports/issue6/lowfactor-results.jsonl
python3 tools/symmetric.py --results Experiments/reports/issue6/one-square-lowfactor-results.jsonl
```
