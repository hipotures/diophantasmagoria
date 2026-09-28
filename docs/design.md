# Architecture, persistent state and limits

`src/math.*` implements polynomial arithmetic, prime roots, CRT, exact square
roots, finite differences and arithmetic selection. `src/io.*` handles decimal
parsing, deterministic JSON, SHA-256, databases and durable files.
`src/search.*` contains the streaming generator, task source, bounded worker pool
and commit protocol. `src/main.cpp` supplies strict CLI parsing. Python tools are
independent verification, merging and experiment orchestration, not the inner
loop. No additional service or external database is required.

## Database contract

A database is JSON Lines: a `dio-roots-v1` header, increasing prime records
(including empty root sets), and a SHA-256 footer over all preceding bytes,
including newlines. The header records coefficients, `finite-field-gcd-split-v1`,
and either full prime coverage through `prime_limit`, or explicit-prime coverage.
Explicit coverage cannot satisfy a general prime-limit campaign. Full coverage
is checked against an independent sieve on load; sorted/distinct stored roots
are range-checked and substituted into the polynomial. Root-set completeness is
checked during generation by the degree of the extracted square-free linear
part; small-prime tests exhaustively check that algorithm. SHA-256 catches a
changed/truncated cache. It is an integrity check, not a signature against a
malicious producer deliberately recomputing the checksum.

Generation writes a separate `.generating` file, fsyncs, renames and fsyncs the
parent directory. Failed or interrupted generation does not replace a good
cache. Search does no finite-field factorization per task. A resumed active
modulus recomputes CRT from cached roots once.

## Generator, scheduling and shards

Eligible primes are sorted primes with at least one root. For each configured
factor count in ascending order, a resumable depth-first stack enumerates
increasing prime-index combinations lexicographically. Before extending a
prefix, division checks multiplication bounds. The smallest possible remaining
factors prune branches above m_max; the largest possible factors prune branches
below m_min. Every product has a unique prime factorization, so no duplicate
moduli are emitted across counts. Only the stack, one active modulus, and bounded
work are kept, not all prime triples/quadruples or all campaign tasks.

For factor count f>=2, the prefix consisting of its first two eligible-prime
indices `(i,j)` belongs to `(i*N+j) % shard_count`, where N is the eligible-prime
count for the domain. For f=1 the owner is `i % shard_count`. Explicit factored
moduli are sorted lexicographically and assigned by list index modulo shard
count. Assignment happens before descending into expensive suffix enumeration
or doing CRT. Hosts visit prefix indices but do not traverse each other's huge
suffix combination spaces. Factorization determines exactly one owner; modulo
partitioning is disjoint and exhaustive. The prime list and shard layout must
be identical on all hosts. This partition is deterministic, not a promise of
equal runtime or equal task counts across hosts.

Each modulus's sorted CRT roots are tiled into at most 16 roots and consecutive
k blocks of at most 64, both signs included. A task has at most 2048 discriminant
candidates. Its stable ID is SHA-256 of `domain:m:k_first:k_last:root_offset`.
The producer submits the first task immediately; it does not wait to enumerate
an entire prefix or campaign. A persistent pool takes work dynamically from a
queue holding at most 64 tiles. The batch size is fixed across thread counts.
After at most 64 tasks the producer waits for all to finish and commits. This
also bounds the outstanding recovery state, hit buffers and trace buffers.
Search-generation loops poll signals/time limits, and workers finish already
issued bounded tasks. Exceptions drain the pool and leave the last committed
checkpoint intact.

A modulus can have up to 1,000,000 CRT roots (normally at most 3^5 for cubics with
nonzero reductions). CRT materializes that single modulus's root list, not the
campaign. Larger lists fail explicitly. Coefficients are arbitrary precision;
very large user-supplied coefficients can still make individual operations slow.
Signals are represented by a lock-free atomic flag. No I/O occurs in a handler.

## Commit invariant and crash recovery

The checkpoint represents the cursor **after an entirely completed batch**.
Workers never advance persistent state. The next batch advances only an
in-memory generator/candidate cursor while workers run. Consequently no
out-of-order completion can move the committed cursor past an unfinished tile.
There is no unsafe "maximum completed task ID" cursor and no growing hole set.
`out_of_order_batches` makes reordered execution observable in reports.

The order of a commit is:

1. Finish every issued task and independently verify each hit.
2. Append complete newline-terminated witness records; fsync the result file.
   An optional coverage trace is also flushed before the checkpoint.
3. Write a checksum-protected checkpoint `.new`, fsync it, atomically replace
   `checkpoint.json`, and fsync its directory.

A witness ID is SHA-256 of `domain:x:y:z`, with canonical y>=z. A crash before
step 3 leaves the preceding checkpoint, so some work can replay. It cannot skip
work or lose a witness from a committed task. A partial last result line is
truncated on resume; complete malformed records cause failure. The checkpoint
stores the committed result byte boundary and a cumulative SHA-256 chain
`chain=SHA256(previous_chain + exact_record_bytes)`, starting at SHA256 of the
empty string. Resume checks that boundary and chain, then incorporates complete
uncommitted records so subsequent replay is deduplicable. Truncating committed
results or changing their bytes is an error. Verification costs one linear
journal scan per resume, not quadratic checkpoint rewrites. Checkpoints are
constant-size with respect to completed work, apart from integer digit lengths.
The final report reuses that chain instead of rescanning the entire result file
at shutdown. Offline merging computes and checks the chain while verifying records.

SIGINT/SIGTERM or time/work limits stop issuance, finish the bounded batch, and
write a partial report. The time limit applies to search, after cache setup, and
can overrun by finishing issued tiles and durable I/O. A hard kill can require
replay of at most 64 issued tiles plus uncommitted generator traversal. Killing
before initial checkpoint publication can be resumed if the journal is empty;
nonempty results without any checkpoint cause an actionable error.

A process-level advisory lock prevents concurrent writers to a run directory.
Filesystem correctness assumes normal local Linux atomic rename/fsync semantics;
there is no network-filesystem or external-coordinator protocol. Storage errors
are reported rather than marking a batch complete. Parent-directory fsyncs
protect file replacement ordering. Back up the whole run directory together.

The immutable domain hash covers coefficients, cache checksum, generator/tile
version, prime coverage, factor counts/explicit factorizations, modulus bounds,
signs, and disjoint k ranges. A checkpoint separately binds the shard count and
index, and the trace setting. Worker count is intentionally changeable.
Thread scheduling cannot change roots or candidate coordinates. Tests compare
exact work IDs, `(m,r,k,sign)` sets and witnesses across one/multiple workers and
all three shards, not merely hit counts.

## Reports and aggregation

Every integer field written by C++ is a decimal string. JSON arrays remain
arrays when empty. `manifest.json` includes the canonical domain definition,
SHA-256 identities, commit, source-content hash, compiler, Boost and build type.
`report.json` adds committed counters, elapsed search time, setup time,
generator/CRT wall time, summed worker candidate/sieve and exact-square times,
persistence wall time, result-record-chain checksum and byte boundary, and peak RSS (Linux VmHWM where
available, getrusage fallback). Candidate time includes reconstruction,
arbitrary-precision witness verification and serialization. Generation overlaps
workers, so these timings are not an additive decomposition of wall time.
Database precomputation is timed separately by the roots command/benchmark.

The merge tool reads stopped/copied local run directories. It checks equation,
domain/cache/shard provenance and exact witness derivations, deduplicates stable
IDs, and requires a matching complete report from every shard to label the
aggregate complete. Missing/stale reports remain partial. The tool does not
merge different k shells into one fictional domain or infer completion from an
empty witness file. Its deduplication memory scales with witnesses, not with all
candidate points. For evidence, retain per-host directories and reports alongside
the merged artifact.

## Validation and deferred work

Unit tests use independent enumeration for roots, CRT and modulus products;
compare native/large finite differences and exact square boundaries; and test
generator cursor restoration and shard partitioning. Integration tests use the
Python exact oracle on a matching domain with real synthetic hits; recover the
large supplied witness from factors; compare threads, shards, arithmetic and
sieve modes; change thread counts on resume; exercise disjoint k shells; simulate
a journal ahead of an old checkpoint and a truncated final append; reject corrupt
caches, checkpoints and committed results; send actual SIGINT/SIGTERM/SIGKILL;
and test verification, replay deduplication and missing/incompatible shards.

CI runs Release and AddressSanitizer/UndefinedBehaviorSanitizer suites on Linux.
Benchmarks intentionally stop at a bounded fixed domain. Neither the tests nor
the benchmark execute the optional multi-hour campaign.

Prime powers/Hensel lifting, more than five factors, block/bitset sieving,
GPU/CUDA, MPI, shared coordination, and the other equations from the issue's
non-goals are outside this release. The first generator is simple and deterministic;
more elaborate modulus selection should follow measured evidence. Nothing here
claims complete coordinate-height coverage, minimality, novelty, insolubility,
or a predicted discovery time.
