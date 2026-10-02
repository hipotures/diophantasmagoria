# Next symmetric searches: low factors and prime powers

Implementation of [issue #6](https://github.com/hipotures/diophantasmagoria/issues/6)
on `feature/symmetric-cubic-one-host`. The equation remains
`sum(t^3+2t) = xyz+1`. All shipped campaigns use C=1.

## Coverage and representation

The user reports exhaustion of square-free moduli with 3, 4 or 5 distinct
primes through 10,000, `2 <= |d| <= 10^13`, every CRT root, both signs,
and `-256 <= k <= 256`. This includes one known permutation class;
it is prior user coverage, not a run repeated for this issue.

The missing square-free 1–2-factor domain was completed locally with
`configs/symmetric-lowfactor256.json`, after a measured 90,000-tile prefix.
It contains 311,655 root-bearing moduli, 698,564 CRT roots and 716,726,664
signed candidates. The complete run took 5.2271 seconds of search wall time,
used 10,596 KiB peak RSS, made 3,935,622 exact-square calls and returned
zero hits. The independent Python verifier accepted the empty result journal.
Rootless moduli are excluded by the necessary divisibility condition, including
every even modulus for C=1. No new permutation class was found.

The new generator searches `m = product(p_i^e_i)` with distinct base primes.
`factor_counts` counts distinct primes; `prime_exponents` lists permitted
exponents, and `power_factor_counts` counts primes with exponent greater than
one. Omitting exponent controls retains the old square-free generator and its
byte-for-byte domain fingerprints. New exponent domains have generator
`prime-power-prefix2-v1`; exponents and the lifting version participate in
the fingerprint. The task hash includes that fingerprint, and witnesses carry
base primes in `factors` and the corresponding `factor_exponents`.
Old witnesses omit exponents and are read as exponent one. Old square-free
checkpoints remain usable; their cursors cannot be interpreted as power cursors.

The implementation supports exponents 1–62 wherever each prime power and the
product fit the declared signed 64-bit modulus bound. Both arithmetic kernels
use the same exact lifted roots. Search supports at most five distinct primes;
`plan` also permits six for estimates. The bounded derived cache holds at most
1,000,000 entries and 1,000,000 total roots. Hensel lifting and CRT each have
a 1,000,000-roots limit. Exceeding a limit raises an error without certifying
completion or returning a partial root set. The `dio-roots-v1` database is
unchanged: all prime powers are derived from its existing prime rows.

For explicit moduli, use prime/exponent objects, for example:

```json
"prime_power_moduli": [
  [{"prime": "13", "exponent": "2"}, {"prime": "79", "exponent": "1"}]
]
```

These replace `moduli`, `prime_exponents` and `power_factor_counts` in an
explicit configuration. The root database still contains the base primes.

## Complete Hensel lifting and the local square condition

For a root r modulo q=p^j, write `u=H(r)/q mod p` and `v=H'(r) mod p`.
Taylor expansion gives

```text
H(r+t*q)/q = H(r)/q + t*H'(r) (mod p).
```

If v is nonzero, the unique digit is `t=-u/v mod p`. If v=0 and u=0,
every digit 0 through p-1 lifts; if v=0 and u is nonzero, none lifts.
The implementation applies this rule to every surviving branch at every
level and sorts all resulting roots. CRT uses extended Euclid for inverses
modulo pairwise coprime prime powers.

Singularity is relevant to the actual polynomial: modulo 479, H has roots
299 and 360. At 299, `H'=0 mod 479`, but `H(299)=27782 mod 479^2`, so
this branch has no lift. Synthetic square and cube polynomials exercise
positive branching, and C=4 supplies an actual symmetric hit with m=4,
`(x,y,z)=(-1,-1,2)`, arising from a singular root modulo 2.

For any divisor q of m, including q=p^e, exact Taylor expansion yields

```text
H(r+k*m)/m = H(r)/m + k*H'(r) (mod q),
Delta_epsilon(k) = epsilon*4*(H(r)/m+k*H'(r)) - 107*r^2 - 8 (mod q).
```

The remaining quotient terms contain m, while `a=r+k*m=r mod q`, so
the congruence holds without assuming square-freeness. The quotient is formed
exactly before reduction; division by m in a residue field would be invalid.
A derivative-zero residue gives a constant condition in k. For q=p^e, square
residues could additionally enforce p-adic valuation and unit conditions;
this issue tests the identity modulo q but benchmarks a filter modulo p only.

The experimental filter builds 64-bit masks per CRT root/tile, skipping primes
already covered by the fixed 64/63/65/11 sieve. Three alternating A/B repeats
used identical million-tile prefixes, four workers and a 30-second ceiling.
Median changes in **process wall throughput** were -2.52% for three-factor
square-free moduli, +0.21% for five-factor square-free moduli, and -0.83%
for five-factor one-square moduli. Exact-square calls fell about 75%, but the
mask construction consumed the saved time. It was removed from production.
The test-only prototype, exhaustive small-domain positive controls, random
exact congruence comparisons and reproducible patch remain in the repository.

## Exact plans and bounded measurements

`plan` groups the last factor by exponent and root count, then sums feasible
intervals using binary search. It counts moduli, CRT roots, logical tiles and
signed candidates without constructing CRT combinations or scanning k.
Interrupted plans say `complete=false` and label their counts as lower bounds.
Tiny oracle tests compare its exact totals against the actual generator.

Every entry below uses primes through 10,000, `2 <= m <= 10^13`, both signs
and `-256 <= k <= 256`. Each prime-power stage has exponents in {1,2}.

| Disjoint new stage | Root-bearing moduli | CRT roots | Signed candidates |
|---|---:|---:|---:|
| Exactly one square, 1–2 primes | 622,521 | 1,394,765 | 1,431,028,890 |
| Exactly one square, 3–5 primes | 1,924,020,986 | 9,687,552,842 | 9,939,429,215,892 |
| At least two squares, 2–5 primes | 193,739,323 | 851,155,129 | 873,285,162,354 |
| Entire prime-power campaign | 2,118,382,830 | 10,540,102,736 | 10,814,145,407,136 |

The final full power plan took 2.97 seconds, aggregating 31,630,601 prefix nodes.
All three stages exclude the all-exponent-one region and their union is the
entire configured new domain; tests assert both properties. The first stage
also completed locally in 11.8527 seconds: 5,602,689 tiles, 7,876,579 exact-square
calls, 11,048 KiB peak RSS, zero hits and zero permutation classes. Its search
rate was 120.73 million candidates/s. Only bounded prefixes of the large stages
were executed.

Measurements used GCC 15.2, Boost 1.90, Python 3.14, four workers, and a VM
exposing 12 CPUs on a Ryzen 9 7950X3D host. Three repeats each capped at
1,000,000 tiles and 30 seconds gave these medians. The 1,000,000-prime-limit
cache was reused even for the 10,000-prime-limit domains; its load is included
in process wall measurements, and peak RSS was about 19,000 KiB.

| Prefix | Process wall candidates/s | Search wall candidates/s |
|---|---:|---:|
| One square, 1–2 primes | 112.51 million | 118.96 million |
| One square, 3 primes | 110.66 million | 116.68 million |
| One square, 4 primes | 107.19 million | 112.68 million |
| One square, 5 primes | 105.87 million | 111.13 million |
| Square-free 1–2 primes through 1,000,000 | 122.56 million | 135.88 million |

Additional final-build prefixes using the 10,000-prime cache measured median
process wall rates of 119.28 million/s for the aggregate power campaign and
115.09 million/s for the multiple-square stage. All repeats searched identical
candidate volumes and final cursors. Setup/cache load is included; compilation,
root-cache creation and planning are excluded.

Measured process wall rates ranged from 102.07 to 125.05 million candidates/s.
Dividing exact candidate counts by those endpoints projects 11.4–14.1 seconds
for the first power stage, 22.1–27.1 hours for the one-square 3–5 stage,
1.94–2.38 hours for multiple squares, and 24.0–29.5 hours for the entire
power domain. These are prefix extrapolations, not completion guarantees;
larger moduli, different factor distributions, thread counts and machine load
can change throughput. No full large campaign was started.

## One-machine commands

Build and validate once. Reuse the existing symmetric prime cache if available;
generate it only when absent.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 4
ctest --test-dir build --output-on-failure
test -f Experiments/symmetric-10000.roots.jsonl || \
  build/diophantasmagoria roots --polynomial symmetric --limit 10000 \
    --out Experiments/symmetric-10000.roots.jsonl

build/diophantasmagoria plan --config configs/symmetric-primepowers256.json \
  --db Experiments/symmetric-10000.roots.jsonl --seconds 30 \
  --out Experiments/primepowers-plan.json
```

The low-factor square-free run and the one-square 1–2 stage are already complete
here. To reproduce the former in a new output directory:

```sh
build/diophantasmagoria search --config configs/symmetric-lowfactor256.json \
  --db Experiments/symmetric-10000.roots.jsonl --out Experiments/lowfactor256 \
  --threads 4 --seconds 60
python3 tools/symmetric.py --results Experiments/lowfactor256/results.jsonl
```

To reproduce the short power stage, use the first command below. The remaining
commands start the two large disjoint stages. Each invocation has a finite
wall-time budget.

```sh
build/diophantasmagoria search --config configs/symmetric-one-square-lowfactor256.json \
  --db Experiments/symmetric-10000.roots.jsonl --out Experiments/one-square-low256 \
  --threads 4 --seconds 60
python3 tools/symmetric.py --results Experiments/one-square-low256/results.jsonl

build/diophantasmagoria search --config configs/symmetric-one-square-highfactor256.json \
  --db Experiments/symmetric-10000.roots.jsonl --out Experiments/one-square-high256 \
  --threads 4 --seconds 14400

build/diophantasmagoria search --config configs/symmetric-multiple-squares256.json \
  --db Experiments/symmetric-10000.roots.jsonl --out Experiments/multiple-squares256 \
  --threads 4 --seconds 14400
```

Run those commands sequentially on one machine. Add `--resume` to the same
search command/output directory for further bounded invocations; changing the
thread count is allowed. `complete=true` in the report certifies exhaustion
of that configured domain. Verify all result journals with `tools/symmetric.py`,
which also counts permutation classes. The aggregate `symmetric-primepowers256`
config overlaps these three power stages: use either the aggregate or the
stages to avoid repeating work. Exponent-three-and-higher domains require
separate configurations; the shipped power campaign covers only exponents 1–2.

## Comparison with broader brute force

These are exact work counts, with projections using the same measured
102.07–125.05 million process wall candidates/s. Six factors and wider k were
planned only; their rates use the measured workloads as a proxy.

| Proposed arithmetic domain | Moduli | CRT roots | Candidates | Projected time |
|---|---:|---:|---:|---:|
| Six square-free primes, limit 10,000, k=-256..256 | 1,877,025,741 | 13,963,757,639 | 14,326,815,337,614 | 31.8–39.0 h |
| Square-free 1–2 primes, limit 100,000, same k | 20,470,401 | 45,763,775 | 46,953,633,150 | 6.3–7.7 min |
| Square-free 1–2 primes, limit 1,000,000, same k | 1,371,334,635 | 3,084,658,523 | 3,164,859,644,598 | 7.0–8.7 h |
| Old 3–5-factor moduli, only 257<=|k|<=512 | 7,185,542,217 | 41,932,409,896 | 42,938,787,733,504 | 95.4–116.9 h |

For larger prime limits, subtract the completed 10,000-prime 1–2 domain:
the new counts are 20,158,746 / 1,371,022,980 moduli and 46,236,906,486 /
3,164,142,917,934 candidates, respectively. A search restricted to at least
one prime above 10,000 would need a further generator constraint; it is not
implemented here. The six-factor search also remains planning-only. The
research configurations are retained with the raw plans in the issue report.

## Elliptic and Hesse directions

For fixed (m,r,epsilon), write `Delta(k)=A*k^3+B*k^2+C*k+D`. Exact expansion gives

```text
A = epsilon*104*m^2
B = epsilon*312*r*m - 107*m^2
C = epsilon*4*(78*r^2+4) + epsilon*36*m^2 - 214*r*m
D = epsilon*4*H(r)/m - 4*m^2 + epsilon*36*r*m - 107*r^2 - 8.
```

If the cubic has no repeated root, this is a genus-one curve with a rational
point at infinity. With `X=A*k`, `Y=A*s` its integral Weierstrass model is
`Y^2=X^3+B*X^2+A*C*X+A^2*D`; recovered points must satisfy the divisibility,
k bounds and parity conditions. Singular cubics require separate treatment.
The curves for different r at the same signed d are isomorphic: they are
residue classes in the same equation in a. This could amortize curve work
across CRT roots. One common integral model, using `X=104*d*a` and
`Y=104*d^2*s`, is

```text
Y^2 = X^3 - 107*d^2*X^2 + 104*(36*d^4+16*d^2)*X
      + 104^2*(-4*d^6-8*d^4+4*d^3).
```

An integral-point algorithm could help a selected set of moduli with very
large k bounds, especially if a Mordell–Weil basis is already known. Even
after sharing work across roots, the full campaign contains billions of
signed moduli, with only 513 k values per root. Computing groups and complete
integral-point sets appears more expensive than the measured addition
recurrence. This is an inference, not an elliptic-method benchmark.
[Sage's integral-point documentation](https://doc.sagemath.org/html/en/reference/arithmetic_curves/sage/schemes/elliptic_curves/ell_rational_field.html#sage.schemes.elliptic_curves.ell_rational_field.EllipticCurve_rational_field.integral_points)
requires a Mordell–Weil basis and describes rank-dependent cost; supplying
an incomplete subgroup can miss integral points. No elliptic dependency or
unproved search exclusion was added.

The leading projective equation is Hesse form with parameter lambda=1/3:
`u^3+v^3+w^3-3*lambda*u*v*w=0`. Its gradient has no simultaneous nonzero
projective zero in characteristic zero, so the leading cubic is smooth.
For an exact solution set `R=max(|x|,|y|,|z|)` and `(u,v,w)=(x,y,z)/R`.
Then the homogeneous cubic has absolute value at most `6/R^2+1/R^3`.
Large integer solutions therefore approach its real projective locus.
This gives a precise necessary asymptotic condition but no proven finite
lattice enumeration, parametrization of the inhomogeneous equation, or bound
on all solutions. A useful lattice restriction needs explicit approximation
estimates and a completeness proof; this remains research.

Raw counters, configurations, source identities, sieve reproduction and
validation logs: [Experiments/reports/issue6](../Experiments/reports/issue6/README.md).
