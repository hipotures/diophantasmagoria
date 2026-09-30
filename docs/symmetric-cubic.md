# Symmetric cubic backend: mathematics and range safety

The target is sum(t^3+2t) = xyz+1. Tests may change the right-hand constant
from 1 to C, while keeping every other coefficient unchanged. All shipped
campaigns use C=1. This supplies small positive controls without hard-coding
solutions in the search.

Set a=x+y, b=xy, d=3a+z. Then

```text
b*d = a^3 + z^3 + 2a + 2z - C
     = d*(d^2 - 9ad + 27a^2 + 2) - (26a^3 + 4a + C).
```

Define H(a)=26a^3+4a+C. For d!=0, b is integral exactly when d divides H(a).
The discriminant of t^2-at+b is

```text
Delta = 4H(a)/d - 4d^2 + 36ad - 107a^2 - 8.
```

When Delta=s^2>=0 and a-s is even, x=(a+s)/2, y=(a-s)/2 and z=d-3a.
These conditions are necessary and sufficient for the reconstructed integers.
Every hit is also substituted into the original equation using cpp_int.

For C=1, d=0 implies H(a)=0. The rational-root theorem leaves integer roots
+/-1; H(1)=31 and H(-1)=-29. Therefore this exceptional case has no solutions.
H is odd for every integer a, so an even modulus has no root at prime 2.
The g1/g2 modulo-16 filter is NOT applied to this backend. Other C values
may allow even moduli; tests include C=4 with (x,y,z)=(2,-1,-1), d=2.
The engine retains its declared m>=2 restriction. Prime powers and m=1 are
outside the configured heuristic domain, rather than silently claimed covered.

## Root polynomial is not equation identity

The config requires `backend: "symmetric-cubic"`. Its root coefficients must be
[C,4,0,26], constant first; `polynomial: "symmetric"` names [1,4,0,26].
The stored domain/witness backend identity is `symmetric-cubic-v1`.
Giving those coefficients to the legacy backend still means yz(y+z)=H(x),
which is a different problem. New backend domains have different fingerprints;
unchanged legacy fingerprints remain byte-for-byte compatible.

The new witness schema is `dio-symmetric-witness-v1`. It retains `a=x+y`
as well as the original coordinates. x>=y orders the quadratic roots, not
all three coordinates. Full permutations are interpretation-level equivalences;
they need not have identical d or square-free factorizations.

## Exact recurrence

With m=abs(d), epsilon=sign(d), root 0<=r<m and a=r+km, H(a)/m is cubic in k.
For each sign, Delta is also cubic. `CandidateSequence<N,true>` initializes
four samples per root/sign and stores value plus three finite differences.
The candidate loop advances by additions, never by repeated cubic evaluation
or integer division. No modular division by a potentially noninvertible m is
introduced. The existing residue sieve runs before exact integer square root.

## Conservative native bound

Let M be the maximum declared modulus, K=max(abs(k_min),abs(k_max)),
X=(K+5)M, and T=sum_i abs(c_i)*(X+1)^i for the root polynomial coefficients.
X covers all coordinates used for initialization at k,k+1,k+2,k+3 and the
look-ahead values represented by differences at every last tile position.
For each sample an absolute term bound for Delta is

```text
P = 4T + 4M^2 + 36MX + 107X^2 + 8.
```

The quotient initializer is covered by the same conservative polynomial term
bounds. The absolute coefficient sums in the first, second and third
differences are 2,4,8. Addition intermediates and reconstructions have ample
headroom under the implemented gate

```text
128*(P + M + X + 1) < 2^127.
```

All bound computation uses cpp_int BEFORE entering a native kernel. Otherwise
`auto` selects cpp_int; forced `128` fails. Exact square-root correction uses
integers only, and the reconstructed triple/residual always use cpp_int.
Tests compare native and arbitrary-precision differences through negative k,
tile boundaries and the conservatively selected native limit.

The scheduler, checkpoints, durable result ordering and lazy stable IDs are
inherited from the optimized base. No 64-task persistence barrier is reintroduced.
