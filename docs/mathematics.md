# Mathematical and arithmetic contract

Coefficients are stored constant-first as `[E,C,B,A]`. The reference to Grechuk,
*A systematic approach to Diophantine equations: open problems*,
[arXiv:2404.08518v9, Table 15](https://arxiv.org/html/2404.08518v9#S7), dated
30 August 2026, is the issue's dated source for g1 and g2. It is not a statement
that their open status has been checked today. The modulus generator here is our
own deterministic bounded generator; it does not reproduce an unavailable
original searcher's selection strategy or runtime.

## Reduction and signed domain

For `d=y+z != 0`, put `m=abs(d)`. Necessary divisibility is `m | G(x)`.
For each canonical modular root `0 <= r < m`, write `x=r+k*m` with **signed** k.
Both `d=+m` and `d=-m` are searched. Dividing by signed d gives `Q=yz`.
The quadratic with roots y,z is `t²-d*t+Q=0`, so

```
Delta = d² - 4*Q
s = isqrt(Delta)
y = (d+s)/2
z = (d-s)/2
```

Existence is equivalent to `Delta >= 0`, `s²=Delta` and equal parity of d,s.
These are both necessary and sufficient given exact divisibility. Ordering
`y >= z` removes only the interchange symmetry. A negative d cannot be dropped
for the fixed polynomials. Every reconstructed witness is substituted into
`y*z*(y+z)-G(x)` in arbitrary precision, independently of the chosen inner loop.

When `d=0`, `G(x)=0` is required and `(y,z)=(t,-t)` gives an infinite family at
each integer root. For each named polynomial the constant coefficient is ±1,
so the rational root theorem restricts integer roots to ±1. At x=-1 all three
have value -4; at x=1, g1 and regression have value 4, and g2 has value 8.
None has an integer root. The production structured domain excludes d=0;
the Python coordinate-box oracle tests it explicitly for `x^3-x` at x=-1,0,1.
It never divides by zero.

## Prime roots and CRT

A bounded Eratosthenes sieve supplies all primes through the declared limit.
Primes up to 97 use direct evaluation. For larger primes, reduction and trimming
happen **before** deciding the polynomial degree. For nonconstant reduced f,
compute `L=gcd(f,t^p-t)` by binary modular exponentiation. The polynomial `t^p-t`
is square-free and has exactly the field elements as roots, so L is the product
of all **distinct linear factors**, including roots repeated in f.

A reproducibly seeded splitter finds factors using `gcd(L,a)` or
`gcd(L,a^((p-1)/2)-1)`. Recursion ends in linear factors. The seed is
`0xd10fa17 XOR p`; random draws use `mt19937_64` directly modulo p.
After at most 4096 unsuccessful attempts at any split the operation errors and
does not publish the database. The count must equal `degree(L)`, roots must be
distinct, and each must satisfy f. Constant nonzero polynomials have no roots;
identically zero reductions enumerate all residues (explicitly rejected above
1,000,000 residues to preserve bounded memory). No exceptional prime is omitted
by a generic-degree assumption. Tests exhaustively compare nine polynomials
against every prime through 1500, including degree drops and repeated roots.

For coprime primes, inductively lift a root r modulo M with root t modulo p:

```
c = (t-r) * inverse(M mod p) mod p, with 0 <= c < p
new_r = r + M*c, with 0 <= new_r < M*p
```

Every prime-root tuple is combined; CRT is a bijection from tuples to roots.
Results are sorted. Production moduli have 1–5 distinct increasing factors;
`m=1` is not a supported search modulus. Multiplication is checked by division
before it occurs. Finite-field products use unsigned 128-bit arithmetic;
prime operands are at most 100,000,000. CRT `M*c` is at most `M*(p-1)`, while
`M*p <= 2^63-1` is checked first. Prefix rank multiplication uses unsigned
128-bit arithmetic and cannot overflow (the prime-index count is below 10^8).

## Integral finite differences and sieve

For a CRT root, `G(r)/m` is an integer. The engine initializes

```
H(k) = G(r)/m
     + (3*A*r*r+2*B*r+C)*k
     + m*(3*A*r+B)*k*k
     + A*m*m*k*k*k.
```

It evaluates this integral polynomial at k,k+1,k+2,k+3, then stores successive
first, second and third forward differences. Advancing a candidate uses three
additions. Each tile is independently initialized at its actual signed start;
no assumption that k starts at zero is made. `Q=epsilon*H(k)` and
`Delta=m*m-4*epsilon*H(k)` for both signs epsilon.

Nonnegative discriminants pass residue tables for 64, 63, 65, and 11 before the
exact integer Newton square root. A square modulo an integer must be one of
that table's enumerated square residues, so the tests cannot discard a square.
All residues come from the integral discriminant; no modular division occurs.
Tests check all square residues and large squares. `--no-sieve` measures the
filter's effect without changing the candidate domain. Newton iteration starts
at a power of two above the square root and decreases to the exact floor;
acceptance requires exact equality `s*s==Delta`. No floating point is used for
integer arithmetic or candidate acceptance.

## Conservative native range proof

Range selection is computed in unbounded integers **before** running native
arithmetic. For a declared maximum modulus M and one k interval define

```
K = max(abs(k_min), abs(k_max))
X = (K+4)*M + M
T = |E| + |C|*(X+1) + |B|*(X+1)^2 + |A|*(X+1)^3
B = 128*(T + M*M + M + X + 1).
```

The whole domain uses native signed 128-bit arithmetic only if `B < 2^127`
for every interval. Otherwise `auto` selects arbitrary precision; forcing 128
errors rather than narrowing the domain. `abs` is evaluated in arbitrary
precision; signed minimum values are never negated in their native type.
Input k is restricted to ±10^12, so k+63, k+3 and the final loop increment are
safe signed 64-bit operations.

Here is why the deliberately loose bound covers the implementation:

* All r satisfy `0<=r<m<=M`. At initialization and after any needed forward
  update, k through k+3 satisfy `|r+(k+j)*m|<X`. Thus Horner intermediate
  magnitudes and each original polynomial term are bounded by T.
* The absolute-coefficient sum of the expanded H polynomial at `|k+j|` is at
  most the positive polynomial at `r+|k+j|*m`, divided by m. It is therefore
  bounded by T. Its coefficient construction terms are also bounded by T,
  since X is at least 5M. Exact division by positive m is safe.
* The initialization formulas for first, second, third differences have
  absolute coefficient sums 2, 4, 8 respectively. An update may temporarily
  add bounds of adjacent differences (at most 12T). The reserve factor 128
  exceeds every such partial expression. No extra update occurs after the
  last k of a tile. Initialization's three extra values are included in X.
* `|Q|<=T`, `Delta<=M²+4T`. Computing `4*epsilon*H` and subtracting it from
  M² fits the reserve. Nonnegative square-root iteration uses only positive
  integers. Its seed is below `2^64` under this contract; `x+n/x` is bounded
  by `2*n+2`, far below the reserved native limit. Squaring the resulting
  floor root is at most Delta.
* `|d-s|<=M+Delta+1` covers the parity subtraction. Reconstruction of y,z and
  the final x calculation and substitution are actually performed with
  unbounded integers, providing an independent release check.

This bound is intentionally more conservative than a finely optimized
polynomial-specific proof. In particular M=10^13 with |k|<=256 is **not** a
native campaign. The regression also takes the wide path. There is no plain
unchecked fixed-width Boost integer path. Statistics use unsigned 64-bit
counters and reject aggregate overflow before addition; subcounts are bounded
by checked candidate/task totals. Persistent large integers are decimal strings.

Tests cover finite differences at negative/positive starts and restart/block
boundaries, native/wide agreement, exact squares and neighboring integers through
1024-bit values, native boundaries, forced-unsafe rejection, and arbitrary-
precision verification of the supplied regression witness.
