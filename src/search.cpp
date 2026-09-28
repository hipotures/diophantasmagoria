#include "search.hpp"
#include <algorithm>
#include <atomic>
#include <boost/version.hpp>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <sys/resource.h>
#include <thread>
namespace dio {
namespace {
using Clock = std::chrono::steady_clock;
constexpr auto generator_version = "lexicographic-prefix2-v2";
constexpr auto filter_version = "even-square-free-g1-g2-v1";
double elapsed(Clock::time_point a) {
    return std::chrono::duration<double>(Clock::now() - a).count();
}
U bounded(const Json &j, const std::string &key, U fallback) {
    Big n = integer(j.get<std::string>(key, std::to_string(fallback)));
    if (n < 0 || n > INT64_MAX)
        throw std::runtime_error(key + " must be in [0, 2^63-1]");
    return n.convert_to<U>();
}
int64_t signed_k(const Json &j) {
    Big n = integer(j.get_value<std::string>());
    if (n < -Big(1000000000000LL) || n > 1000000000000LL)
        throw std::runtime_error("k must be in [-10^12,10^12]");
    return n.convert_to<int64_t>();
}
U product(const std::vector<U> &factors) {
    U m = 1;
    for (U p : factors) {
        if (p < 2 || m > U(INT64_MAX) / p)
            throw std::runtime_error("Invalid or overflowing factored modulus");
        m *= p;
    }
    return m;
}
} // namespace
Domain domain(const Json &config, const Database &db) {
    Domain d;
    d.a = get_poly(config);
    d.exclude_even = excludes_even_square_free(d.a);
    if (d.a != db.a)
        throw std::runtime_error("Database polynomial does not match configuration");
    d.m_min = bounded(config, "m_min", 1);
    d.m_max = bounded(config, "m_max", 1000);
    if (d.m_min < 2 || d.m_min > d.m_max)
        throw std::runtime_error("Require 2 <= m_min <= m_max <= 2^63-1");
    if (auto ms = config.get_child_optional("moduli")) {
        for (const auto &v : *ms) {
            auto ps = numbers(v.second);
            if (ps.empty() || ps.size() > 5)
                throw std::runtime_error("Explicit moduli need 1 to 5 factors");
            std::sort(ps.begin(), ps.end());
            if (std::adjacent_find(ps.begin(), ps.end()) != ps.end())
                throw std::runtime_error("Modulus must be square-free");
            for (U p : ps)
                if (!db.data.contains(p))
                    throw std::runtime_error("Explicit factor missing from root database");
            U m = product(ps);
            if (m < d.m_min || m > d.m_max)
                throw std::runtime_error("Explicit modulus outside m bounds");
            d.moduli.push_back(ps);
            if (d.exclude_even && ps.front() == 2)
                ++d.excluded_explicit_moduli;
        }
        if (d.moduli.empty())
            throw std::runtime_error("Explicit modulus list is empty");
        std::sort(d.moduli.begin(), d.moduli.end());
        if (std::adjacent_find(d.moduli.begin(), d.moduli.end()) != d.moduli.end())
            throw std::runtime_error("Duplicate explicit modulus");
    } else {
        d.prime_limit = config.get<uint32_t>("prime_limit");
        if (db.explicit_only || d.prime_limit > db.limit || d.prime_limit < 2)
            throw std::runtime_error("Database lacks declared full prime coverage");
        d.factor_counts = numbers(config.get_child("factor_counts"));
        std::sort(d.factor_counts.begin(), d.factor_counts.end());
        if (d.factor_counts.empty() ||
            std::adjacent_find(d.factor_counts.begin(), d.factor_counts.end()) !=
                d.factor_counts.end())
            throw std::runtime_error("Empty or duplicate factor counts");
        for (U f : d.factor_counts)
            if (f < 1 || f > 5)
                throw std::runtime_error("Factor counts must be 1 through 5");
    }
    if (auto ranges = config.get_child_optional("k_ranges")) {
        for (const auto &r : *ranges) {
            if (r.second.size() != 2)
                throw std::runtime_error("Each k range requires two endpoints");
            auto i = r.second.begin();
            int64_t lo = signed_k((i++)->second), hi = signed_k(i->second);
            d.ranges.emplace_back(lo, hi);
        }
    } else {
        Json lo, hi;
        lo.put_value(config.get<std::string>("k_min"));
        hi.put_value(config.get<std::string>("k_max"));
        d.ranges.emplace_back(signed_k(lo), signed_k(hi));
    }
    if (d.ranges.empty())
        throw std::runtime_error("Empty k domain");
    std::sort(d.ranges.begin(), d.ranges.end());
    for (size_t i = 0; i < d.ranges.size(); ++i)
        if (d.ranges[i].first > d.ranges[i].second ||
            (i && d.ranges[i].first <= d.ranges[i - 1].second))
            throw std::runtime_error("Inverted or overlapping k ranges");
    auto &c = d.canonical;
    c.put("schema", "dio-domain-v1");
    c.put("generator", generator_version);
    Json filter;
    filter.put("version", filter_version);
    filter.put("exclude_even_square_free", d.exclude_even ? "true" : "false");
    c.add_child("local_filter", filter);
    c.put("tile", "roots16-k64-v1");
    c.add_child("coefficients", coefficients(d.a));
    c.put("root_database", db.checksum);
    c.put("m_min", d.m_min);
    c.put("m_max", d.m_max);
    c.put("prime_limit", d.prime_limit);
    c.add_child("factor_counts", array(d.factor_counts));
    Json ms;
    for (const auto &ps : d.moduli)
        ms.push_back({"", array(ps)});
    c.add_child("moduli", ms);
    Json ranges;
    for (auto [lo, hi] : d.ranges) {
        Json r, a, b;
        a.put_value(lo);
        b.put_value(hi);
        r.push_back({"", a});
        r.push_back({"", b});
        ranges.push_back({"", r});
    }
    c.add_child("k_ranges", ranges);
    c.put("signs", "+1,-1");
    d.fingerprint = sha256(json(c));
    return d;
}
Generator::Generator(const Domain &dom, const Database &db, U s, U n) : d(dom), shard(s), count(n) {
    for (const auto &[p, rs] : db.data)
        if (p <= d.prime_limit && !rs.empty() && !(d.exclude_even && p == 2))
            eligible.push_back(p);
}
bool Generator::next(std::vector<U> &factors) {
    yielded = false;
    if (!d.moduli.empty()) {
        while (explicit_pos < d.moduli.size() && !stopped) {
            if (Clock::now() >= deadline) {
                stopped = true;
                return false;
            }
            if (Clock::now() >= yield_deadline) {
                yielded = true;
                return false;
            }
            U i = explicit_pos++;
            // Retain requested-list indices for explicit sharding, but never issue
            // an analytically excluded modulus to the CRT/task source.
            if (d.exclude_even && d.moduli[i].front() == 2)
                continue;
            if (i % count == shard) {
                factors = d.moduli[i];
                return true;
            }
        }
        return false;
    }
    while (phase < d.factor_counts.size() && !stopped) {
        if (Clock::now() >= deadline) {
            stopped = 1;
            return false;
        }
        if (Clock::now() >= yield_deadline) {
            yielded = true;
            return false;
        }
        U f = d.factor_counts[phase], depth = chosen.size(), n = eligible.size();
        if (n < f || next_index.back() > n - (f - depth)) {
            if (depth == 0) {
                ++phase;
                next_index = {0};
            } else {
                chosen.pop_back();
                next_index.pop_back();
            }
            continue;
        }
        U i = next_index.back()++;
        U m = 1;
        for (U c : chosen)
            m *= eligible[c];
        U p = eligible[i];
        if (m > d.m_max / p) {
            next_index.back() = n;
            continue;
        }
        m *= p;
        U remaining = f - depth - 1, low = m;
        bool too_large = false;
        for (U j = 1; j <= remaining; ++j) {
            U q = eligible[i + j];
            if (low > d.m_max / q) {
                too_large = true;
                break;
            }
            low *= q;
        }
        if (too_large) {
            next_index.back() = n;
            continue;
        }
        U high = m;
        for (U j = 0; j < remaining; ++j) {
            U q = eligible[n - 1 - j];
            if (high >= d.m_min || high > (d.m_min - 1) / q) {
                high = d.m_min;
                break;
            }
            high *= q;
        }
        if (high < d.m_min)
            continue;
        chosen.push_back(i);
        if (chosen.size() == std::min<U>(2, f)) {
            // Prefix ranks use exact integer arithmetic and are stable across hosts.
            U rank = f == 1 ? i : static_cast<U>((__uint128_t(chosen[0]) * n + i) % count);
            if (rank % count != shard) {
                chosen.pop_back();
                continue;
            }
        }
        if (chosen.size() == f) {
            factors.clear();
            for (U c : chosen)
                factors.push_back(eligible[c]);
            chosen.pop_back();
            return true;
        }
        next_index.push_back(i + 1);
    }
    return false;
}
Json Generator::state() const {
    Json j;
    j.put("phase", phase);
    j.put("explicit_pos", explicit_pos);
    j.add_child("chosen", array(chosen));
    j.add_child("next_index", array(next_index));
    return j;
}
void Generator::restore(const Json &j) {
    phase = j.get<U>("phase");
    explicit_pos = j.get<U>("explicit_pos");
    chosen = numbers(j.get_child("chosen"));
    next_index = numbers(j.get_child("next_index"));
    if (phase > d.factor_counts.size() || explicit_pos > d.moduli.size() ||
        next_index.size() != chosen.size() + 1)
        throw std::runtime_error("Invalid generator checkpoint");
    for (U i : chosen)
        if (i >= eligible.size())
            throw std::runtime_error("Invalid checkpoint prime index");
}
namespace {
// Jobs are coarse chunks in a bounded epoch pipeline. Workers never persist progress.
class Pool {
    std::mutex mutex;
    std::condition_variable ready, finished;
    std::deque<std::function<void()>> queue;
    std::vector<std::thread> threads;
    size_t pending = 0;
    bool shutdown = false;

  public:
    explicit Pool(unsigned n) {
        try {
            for (unsigned i = 0; i < n; ++i)
                threads.emplace_back([this] {
                    for (;;) {
                        std::function<void()> job;
                        {
                            std::unique_lock lock(mutex);
                            ready.wait(lock, [&] { return shutdown || !queue.empty(); });
                            if (shutdown && queue.empty())
                                return;
                            job = std::move(queue.front());
                            queue.pop_front();
                        }
                        job();
                        {
                            std::lock_guard lock(mutex);
                            --pending;
                            if (!pending)
                                finished.notify_one();
                        }
                    }
                });
        } catch (...) {
            close();
            throw;
        }
    }
    void close() {
        {
            std::lock_guard lock(mutex);
            shutdown = true;
        }
        ready.notify_all();
        for (auto &t : threads)
            if (t.joinable())
                t.join();
    }
    ~Pool() {
        close();
    }
    void submit(std::function<void()> job) {
        {
            std::lock_guard lock(mutex);
            queue.push_back(std::move(job));
            ++pending;
        }
        ready.notify_one();
    }
    void wait() {
        std::unique_lock lock(mutex);
        finished.wait(lock, [&] { return pending == 0; });
    }
};
std::string chain_records(std::string chain, const std::string &records) {
    size_t begin = 0;
    while (begin < records.size()) {
        size_t end = records.find('\n', begin);
        if (end == std::string::npos)
            throw std::runtime_error("Incomplete internal result record");
        chain = sha256(chain + records.substr(begin, end - begin + 1));
        begin = end + 1;
    }
    return chain;
}
std::string validate_journal(const fs::path &path, U committed, const std::string &expected,
                             const std::string &domain) {
    std::ifstream in(path);
    std::string line, chain = sha256("");
    U offset = 0;
    bool checked = committed == 0 && chain == expected;
    while (std::getline(in, line)) {
        if (in.eof())
            throw std::runtime_error("Unrepaired journal tail");
        Json row = parse(line);
        if (row.get<std::string>("domain") != domain)
            throw std::runtime_error("Result journal domain mismatch");
        chain = sha256(chain + line + "\n");
        offset += line.size() + 1;
        if (offset == committed) {
            if (chain != expected)
                throw std::runtime_error("Committed result journal checksum failure");
            checked = true;
        }
    }
    if (!checked)
        throw std::runtime_error("Result journal commit boundary is missing");
    return chain;
}
struct Task {
    std::vector<U> factors, rs;
    U m;
    int64_t lo, hi;
    bool first_modulus = false, first_roots = false;
    size_t root_offset = 0;
};
struct Source {
    const Domain &d;
    const Database &db;
    Generator gen;
    std::vector<U> active, rs;
    size_t range = 0, offset = 0;
    int64_t k = 0;
    U m = 0;
    double generation_seconds = 0;
    Source(const Domain &dom, const Database &database, U s, U n)
        : d(dom), db(database), gen(dom, database, s, n) {}
    void compute() {
        m = product(active);
        std::vector<std::vector<U>> lists;
        for (U p : active)
            lists.push_back(db.data.at(p));
        rs = crt(active, lists);
    }
    bool next(Task &t) {
        auto start = Clock::now();
        while (active.empty() && !stopped) {
            if (!gen.next(active)) {
                generation_seconds += elapsed(start);
                return false;
            }
            compute();
            range = offset = 0;
            k = d.ranges[0].first;
            if (rs.empty())
                active.clear();
        }
        if (stopped) {
            generation_seconds += elapsed(start);
            return false;
        }
        t.factors = active;
        t.m = m;
        t.lo = k;
        t.hi = std::min<int64_t>(k + 63, d.ranges[range].second);
        t.first_modulus = offset == 0 && range == 0 && k == d.ranges[0].first;
        t.first_roots = range == 0 && k == d.ranges[0].first;
        size_t end = std::min(offset + 16, rs.size());
        t.rs.assign(rs.begin() + static_cast<ptrdiff_t>(offset),
                    rs.begin() + static_cast<ptrdiff_t>(end));
        t.root_offset = offset;
        offset = end;
        if (offset == rs.size()) {
            offset = 0;
            k = t.hi + 1;
            if (k > d.ranges[range].second) {
                ++range;
                if (range == d.ranges.size()) {
                    active.clear();
                    rs.clear();
                    range = 0;
                } else
                    k = d.ranges[range].first;
            }
        }
        generation_seconds += elapsed(start);
        return true;
    }
    Json state() const {
        Json j;
        j.add_child("generator", gen.state());
        j.add_child("active", array(active));
        j.put("range", range);
        j.put("offset", offset);
        j.put("k", k);
        return j;
    }
    void restore(const Json &j) {
        gen.restore(j.get_child("generator"));
        active = numbers(j.get_child("active"));
        range = j.get<size_t>("range");
        offset = j.get<size_t>("offset");
        k = j.get<int64_t>("k");
        if (!active.empty()) {
            compute();
            if (range >= d.ranges.size() || offset >= rs.size() || k < d.ranges[range].first ||
                k > d.ranges[range].second)
                throw std::runtime_error("Invalid task cursor");
        }
    }
};
struct Stats {
    U tasks = 0, moduli = 0, roots = 0, candidates = 0, nonnegative = 0, survivors = 0, squares = 0,
      hits = 0, native_tasks = 0, big_tasks = 0, out_of_order_batches = 0;
    double candidate_seconds = 0, square_seconds = 0;
    void add(const Stats &b) {
        if (candidates > UINT64_MAX - b.candidates || tasks > UINT64_MAX - b.tasks ||
            roots > UINT64_MAX - b.roots || moduli > UINT64_MAX - b.moduli)
            throw std::runtime_error("64-bit statistics exhausted; split the campaign domain");
        tasks += b.tasks;
        moduli += b.moduli;
        roots += b.roots;
        candidates += b.candidates;
        nonnegative += b.nonnegative;
        survivors += b.survivors;
        squares += b.squares;
        hits += b.hits;
        native_tasks += b.native_tasks;
        big_tasks += b.big_tasks;
        candidate_seconds += b.candidate_seconds;
        square_seconds += b.square_seconds;
    }
    Json save() const {
        Json j;
#define PUT(x) j.put(#x, x)
        PUT(tasks);
        PUT(moduli);
        PUT(roots);
        PUT(candidates);
        PUT(nonnegative);
        PUT(survivors);
        PUT(squares);
        PUT(hits);
        PUT(native_tasks);
        PUT(big_tasks);
        PUT(out_of_order_batches);
        PUT(candidate_seconds);
        PUT(square_seconds);
#undef PUT
        return j;
    }
    void restore(const Json &j) {
#define GET(x) x = j.get<decltype(x)>(#x)
        GET(tasks);
        GET(moduli);
        GET(roots);
        GET(candidates);
        GET(nonnegative);
        GET(survivors);
        GET(squares);
        GET(hits);
        GET(native_tasks);
        GET(big_tasks);
        GET(out_of_order_batches);
        GET(candidate_seconds);
        GET(square_seconds);
#undef GET
    }
};
struct Result {
    Stats stats;
    std::string hits, trace;
};
template <class N> Big big(const N &n) {
    return Big(n);
}
template <class N> Result run_task(const Domain &d, const Task &t, const Options &o) {
    auto start = Clock::now();
    Result out;
    std::string task_id;
    auto id = [&]() -> const std::string & {
        if (task_id.empty())
            task_id =
                sha256(d.fingerprint + ":" + std::to_string(t.m) + ":" + std::to_string(t.lo) +
                       ":" + std::to_string(t.hi) + ":" + std::to_string(t.root_offset));
        return task_id;
    };
    auto &st = out.stats;
    st.tasks = 1;
    st.moduli = t.first_modulus;
    st.roots = t.first_roots ? t.rs.size() : 0;
    if constexpr (std::is_same_v<N, __int128_t>)
        st.native_tasks = 1;
    else
        st.big_tasks = 1;
    std::array<N, 4> a;
    for (size_t i = 0; i < 4; ++i)
        a[i] = d.a[i].convert_to<N>();
    const N m = t.m;
    for (U r : t.rs) {
        Differences<N> diff(a, t.m, r, t.lo);
        for (int64_t k = t.lo; k <= t.hi; ++k) {
            for (int sign : {1, -1}) {
                ++st.candidates;
                if (o.trace) {
                    Json row;
                    row.put("task", id());
                    row.put("m", t.m);
                    row.put("r", r);
                    row.put("k", k);
                    row.put("sign", sign);
                    out.trace += json(row);
                }
                N delta = m * m - 4 * sign * diff.h;
                if (delta < 0)
                    continue;
                ++st.nonnegative;
                if (!o.no_sieve && !sieve(delta))
                    continue;
                ++st.survivors;
                auto square_start = Clock::now();
                N s = square_root(delta);
                bool square = s * s == delta;
                st.square_seconds += elapsed(square_start);
                ++st.squares;
                if (!square)
                    continue;
                N dd = sign * m;
                if ((dd - s) % 2 != 0)
                    continue;
                Big x = Big(r) + Big(k) * t.m, y = (big(dd) + big(s)) / 2,
                    z = (big(dd) - big(s)) / 2;
                Big residual = y * z * (y + z) - eval(d.a, x);
                if (residual != 0)
                    throw std::runtime_error("Independent witness verification failed");
                Json hit;
                hit.put("schema", "dio-witness-v1");
                hit.put("domain", d.fingerprint);
                hit.add_child("coefficients", coefficients(d.a));
                hit.put("root_database", d.canonical.get<std::string>("root_database"));
                hit.put("shard", std::to_string(o.shard) + "/" + std::to_string(o.shards));
                hit.put("task", id());
                hit.put("id", sha256(d.fingerprint + ":" + decimal(x) + ":" + decimal(y) + ":" +
                                     decimal(z)));
                hit.put("x", decimal(x));
                hit.put("y", decimal(y));
                hit.put("z", decimal(z));
                hit.put("d", decimal(big(dd)));
                hit.put("m", t.m);
                hit.add_child("factors", array(t.factors));
                hit.put("root", r);
                hit.put("k", k);
                hit.put("discriminant", decimal(big(delta)));
                hit.put("square_root", decimal(big(s)));
                hit.put("residual", decimal(residual));
                hit.put("build_commit", BUILD_REV);
                hit.put("build_source", BUILD_SOURCE);
                out.hits += json(hit);
                ++st.hits;
            }
            if (k < t.hi)
                diff.next();
        }
    }
    st.candidate_seconds = elapsed(start) - st.square_seconds;
    return out;
}
Json envelope(const Json &payload) {
    Json out;
    out.add_child("payload", payload);
    out.put("checksum", sha256(json(payload)));
    return out;
}
Json unwrap(const fs::path &path) {
    Json j = parse(read_file(path));
    Json p = j.get_child("payload");
    if ((p.get<std::string>("schema") != "dio-checkpoint-v1" &&
         p.get<std::string>("schema") != "dio-checkpoint-v2") ||
        j.get<std::string>("checksum") != sha256(json(p)))
        throw std::runtime_error(
            "Checkpoint checksum failure; restore a backup or start a new output directory");
    return p;
}
} // namespace
int search(const Domain &d, const Database &db, const Options &o, double setup_seconds) {
    auto setup_start = Clock::now();
    if (o.threads < 1 || o.threads > 1024 || o.shards < 1 || o.shard >= o.shards)
        throw std::runtime_error("Invalid threads or shard");
    const U queue_chunks = o.queue_chunks ? o.queue_chunks : std::max<U>(2, 2 * o.threads);
    if (o.chunk_tiles < 1 || o.chunk_tiles > 4096 || queue_chunks < 1 || queue_chunks > 2048 ||
        o.chunk_tiles > 262144 / queue_chunks || o.checkpoint_tiles < 1 ||
        o.checkpoint_tiles > 1000000000 || !std::isfinite(o.checkpoint_seconds) ||
        o.checkpoint_seconds <= 0 || o.checkpoint_seconds > 3600)
        throw std::runtime_error(
            "Invalid scheduler limits: chunk tiles 1..4096, queue chunks 1..2048, at most 262144 "
            "queued logical tiles, checkpoint tiles 1..10^9, checkpoint seconds (0,3600]");
    if (o.arithmetic != "auto" && o.arithmetic != "128" && o.arithmetic != "big")
        throw std::runtime_error("Arithmetic must be auto, 128, or big");
    bool safe = true;
    for (auto [lo, hi] : d.ranges)
        safe = safe && native_safe(d.a, d.m_max, lo, hi);
    if (o.arithmetic == "128" && !safe)
        throw std::runtime_error(
            "Unsafe forced native arithmetic for declared domain; use auto or big");
    Json manifest;
    manifest.put("schema", "dio-run-v1");
    manifest.add_child("domain_definition", d.canonical);
    manifest.put("domain", d.fingerprint);
    manifest.put("root_database", db.checksum);
    manifest.put("shard", o.shard);
    manifest.put("shards", o.shards);
    manifest.put("threads", o.threads);
    manifest.put("arithmetic", o.arithmetic == "auto" ? (safe ? "128" : "big") : o.arithmetic);
    manifest.put("build_commit", BUILD_REV);
    manifest.put("compiler", BUILD_COMPILER);
    manifest.put("boost", BOOST_LIB_VERSION);
    manifest.put("build_source", BUILD_SOURCE);
    manifest.put("build_type", BUILD_TYPE);
    manifest.put("setup_seconds", setup_seconds);
    manifest.put("sieve", o.no_sieve ? "disabled" : "64,63,65,11");
    manifest.put("scheduler", "chunked-epochs-v1");
    manifest.put("chunk_tiles", o.chunk_tiles);
    manifest.put("queue_chunks", queue_chunks);
    manifest.put("checkpoint_tiles", o.checkpoint_tiles);
    manifest.put("checkpoint_seconds", o.checkpoint_seconds);
    manifest.put("trace", o.trace ? "true" : "false");
    manifest.put("planning", "Streaming domain; total work and ETA are not enumerated");
    Json exclusions;
    exclusions.put("rule", filter_version);
    exclusions.put("region", d.exclude_even ? "All even square-free moduli" : "None");
    exclusions.put("basis",
                   "Exact coefficient recognition; modulo-16 certificate; both signs of d");
    exclusions.put("application", "Before combination generation or explicit-modulus CRT");
    exclusions.put("generated_moduli_excluded", d.exclude_even ? "not_enumerated" : "0");
    exclusions.put("explicit_moduli_excluded", d.excluded_explicit_moduli);
    exclusions.put("explicit_count_scope", "Entire requested list, before sharding");
    exclusions.put("counter_scope",
                   "Enumerated work only; analytically excluded candidates are not counted");
    manifest.add_child("analytical_exclusions", exclusions);
    manifest.put("task_limit", o.max_tasks);
    manifest.put("time_limit_seconds", o.seconds);
    if (o.dry_run) {
        std::cout << json(manifest);
        return 0;
    }
    fs::create_directories(o.output);
    Lock lock(o.output / "run.lock");
    fs::path checkpoint = o.output / "checkpoint.json", results = o.output / "results.jsonl",
             trace = o.output / "coverage.jsonl";
    Source source(d, db, o.shard, o.shards);
    Stats totals;
    double previous_seconds = 0, previous_generation = 0, previous_persistence = 0;
    bool complete = false;
    std::string result_chain = sha256("");
    bool recovered_results = false, recovered_trace = false;
    bool continuing = o.resume && fs::exists(checkpoint);
    if (o.resume && !continuing) {
        if (fs::exists(results) && fs::file_size(results) != 0)
            throw std::runtime_error("Missing checkpoint with nonempty results; restore the "
                                     "checkpoint or use a new directory");
        if (fs::exists(o.output / "manifest.json")) {
            Json old = parse(read_file(o.output / "manifest.json"));
            if (old.get<std::string>("domain") != d.fingerprint || old.get<U>("shard") != o.shard ||
                old.get<U>("shards") != o.shards)
                throw std::runtime_error("Interrupted initialization domain mismatch");
        }
    }
    if (continuing) {
        Json p = unwrap(checkpoint);
        if (p.get<std::string>("generator_version", "") != generator_version ||
            p.get<std::string>("filter_version", "") != filter_version)
            throw std::runtime_error(
                "Checkpoint generator/filter version mismatch: pre-filter checkpoints are "
                "incompatible. Keep the root database and start a new output directory");
        if (p.get<std::string>("domain") != d.fingerprint || p.get<U>("shard") != o.shard ||
            p.get<U>("shards") != o.shards ||
            p.get<std::string>("trace") != (o.trace ? "true" : "false"))
            throw std::runtime_error(
                "Checkpoint domain/shard/trace mismatch; use a new output directory");
        source.restore(p.get_child("cursor"));
        totals.restore(p.get_child("counters"));
        previous_seconds = p.get<double>("search_seconds");
        previous_generation = p.get<double>("generation_seconds");
        previous_persistence = p.get<double>("persistence_seconds");
        complete = p.get<std::string>("complete") == "true";
        if (!fs::exists(results) || fs::file_size(results) < p.get<U>("result_bytes"))
            throw std::runtime_error("Committed result journal is missing or truncated");
        repair_tail(results);
        recovered_results = fs::file_size(results) > p.get<U>("result_bytes");
        result_chain = validate_journal(results, p.get<U>("result_bytes"),
                                        p.get<std::string>("result_chain"), d.fingerprint);
        if (o.trace) {
            if (!fs::exists(trace) || fs::file_size(trace) < p.get<U>("trace_bytes"))
                throw std::runtime_error("Committed trace is missing or truncated");
            repair_tail(trace);
            recovered_trace = fs::file_size(trace) > p.get<U>("trace_bytes");
        }
    } else if (!o.resume && (fs::exists(checkpoint) || fs::exists(results)))
        throw std::runtime_error(
            "Output already contains a search; pass --resume or choose a new directory");
    manifest.put("setup_seconds", setup_seconds + elapsed(setup_start));
    atomic_write(o.output / "manifest.json", json(manifest));
    Journal result_journal(results, recovered_results);
    std::unique_ptr<Journal> trace_journal;
    if (o.trace)
        trace_journal = std::make_unique<Journal>(trace, recovered_trace);
    auto start = Clock::now();
    double persistence = 0;
    U issued_this_run = 0, epoch_count = 0, chunks_issued = 0, checkpoint_writes = 0,
      reordered_epochs = 0;
    U peak_inflight_chunks = 0;
    double producer_wait = 0, drain_seconds = 0;
    struct rusage cpu_begin{};
    getrusage(RUSAGE_SELF, &cpu_begin);
    auto observe = [&](std::string_view event, U index) {
        if (o.observer)
            o.observer(event, index);
    };
    if (o.seconds > 0)
        source.gen.deadline = start + std::chrono::duration_cast<Clock::duration>(
                                          std::chrono::duration<double>(std::min(o.seconds, 1e9)));
    Pool pool(o.threads);
    auto persist = [&] {
        auto at = Clock::now();
        result_journal.sync();
        if (trace_journal)
            trace_journal->sync();
        observe("results_synced", totals.tasks);
        Json p;
        p.put("schema", "dio-checkpoint-v2");
        p.put("generator_version", generator_version);
        p.put("filter_version", filter_version);
        p.put("domain", d.fingerprint);
        p.put("shard", o.shard);
        p.put("shards", o.shards);
        p.put("trace", o.trace ? "true" : "false");
        p.put("complete", complete ? "true" : "false");
        p.add_child("cursor", source.state());
        p.add_child("counters", totals.save());
        p.put("result_bytes", fs::file_size(results));
        p.put("result_chain", result_chain);
        p.put("trace_bytes", o.trace ? fs::file_size(trace) : 0);
        p.put("search_seconds", previous_seconds + elapsed(start));
        p.put("generation_seconds", previous_generation + source.generation_seconds);
        p.put("persistence_seconds", previous_persistence + persistence);
        observe("before_checkpoint", totals.tasks);
        atomic_write(checkpoint, json(envelope(p)));
        ++checkpoint_writes;
        persistence += elapsed(at);
        observe("checkpoint_replaced", totals.tasks);
    };
    if (!continuing)
        persist();
    const bool use_native = o.arithmetic == "128" || (o.arithmetic == "auto" && safe);
    // The persisted cursor remains the previous epoch's frontier until every issued
    // chunk is received. Futures bound both outstanding work and result buffers.
    while (!complete && !stopped) {
        if ((o.seconds > 0 && elapsed(start) >= o.seconds) ||
            (o.max_tasks && issued_this_run >= o.max_tasks))
            break;
        const auto epoch_start = Clock::now();
        source.gen.yield_deadline =
            epoch_start + std::chrono::duration_cast<Clock::duration>(
                              std::chrono::duration<double>(o.checkpoint_seconds));
        struct ChunkResult {
            Result output;
            U order = 0;
        };
        std::deque<std::future<ChunkResult>> pending;
        std::atomic<U> completion_order{0};
        U issued_epoch = 0, received = 0;
        bool found = false, reordered = false;
        auto receive = [&] {
            auto wait_start = Clock::now();
            ChunkResult chunk = pending.front().get();
            pending.pop_front();
            producer_wait += elapsed(wait_start);
            reordered = reordered || chunk.order != received++;
            auto at = Clock::now();
            totals.add(chunk.output.stats);
            result_journal.append(chunk.output.hits);
            result_chain = chain_records(result_chain, chunk.output.hits);
            if (trace_journal)
                trace_journal->append(chunk.output.trace);
            found = found || chunk.output.stats.hits > 0;
            persistence += elapsed(at);
            observe("chunk_received", totals.tasks);
        };
        try {
            while (!stopped && !complete && issued_epoch < o.checkpoint_tiles) {
                while (!pending.empty() && pending.front().wait_for(std::chrono::seconds(0)) ==
                                               std::future_status::ready)
                    receive();
                if (found && o.stop_on_hit)
                    break;
                if (pending.size() >= queue_chunks)
                    receive();
                if ((found && o.stop_on_hit) || Clock::now() >= source.gen.yield_deadline ||
                    (o.seconds > 0 && elapsed(start) >= o.seconds) ||
                    (o.max_tasks && issued_this_run >= o.max_tasks))
                    break;
                std::vector<Task> tasks;
                tasks.reserve(static_cast<size_t>(o.chunk_tiles));
                U first = issued_this_run;
                for (U n = 0; n < o.chunk_tiles && !stopped && issued_epoch < o.checkpoint_tiles;
                     ++n) {
                    if (Clock::now() >= source.gen.yield_deadline ||
                        (o.seconds > 0 && elapsed(start) >= o.seconds) ||
                        (o.max_tasks && issued_this_run >= o.max_tasks))
                        break;
                    Task t;
                    if (!source.next(t)) {
                        if (!stopped && !source.gen.yielded)
                            complete = true;
                        break;
                    }
                    tasks.push_back(std::move(t));
                    ++issued_this_run;
                    ++issued_epoch;
                }
                if (tasks.empty())
                    break;
                auto job = std::make_shared<std::packaged_task<ChunkResult()>>(
                    [&, tasks = std::move(tasks), first] {
                        observe("chunk_started", first);
                        ChunkResult chunk;
                        for (size_t i = 0; i < tasks.size(); ++i) {
                            Result tile = use_native ? run_task<__int128_t>(d, tasks[i], o)
                                                     : run_task<Big>(d, tasks[i], o);
                            chunk.output.stats.add(tile.stats);
                            chunk.output.hits += tile.hits;
                            chunk.output.trace += tile.trace;
                            observe("tile_completed", first + i + 1);
                        }
                        chunk.order = completion_order.fetch_add(1);
                        observe("chunk_completed", first);
                        return chunk;
                    });
                pending.push_back(job->get_future());
                pool.submit([job] { (*job)(); });
                ++chunks_issued;
                peak_inflight_chunks = std::max<U>(peak_inflight_chunks, pending.size());
            }
            auto drain_start = Clock::now();
            while (!pending.empty())
                receive();
            pool.wait();
            drain_seconds += elapsed(drain_start);
        } catch (...) {
            pool.wait();
            throw;
        }
        if (reordered) {
            ++totals.out_of_order_batches;
            ++reordered_epochs;
        }
        ++epoch_count;
        observe("epoch_drained", totals.tasks);
        persist();
        if (found && o.stop_on_hit)
            break;
    }
    // Includes no-work resumes/migrations; ordinary epochs have already committed.
    if (continuing && epoch_count == 0)
        persist();
    Json report = manifest;
    report.put("complete", complete ? "true" : "false");
    report.add_child("counters", totals.save());
    report.put("search_seconds", previous_seconds + elapsed(start));
    report.put("generation_crt_seconds", previous_generation + source.generation_seconds);
    report.put("persistence_seconds", previous_persistence + persistence);
    Json metrics;
    metrics.put("epochs", epoch_count);
    metrics.put("chunks", chunks_issued);
    metrics.put("checkpoint_writes", checkpoint_writes);
    metrics.put("result_append_calls", result_journal.append_calls);
    metrics.put("result_fsync_calls", result_journal.sync_calls);
    metrics.put("trace_append_calls", trace_journal ? trace_journal->append_calls : 0);
    metrics.put("trace_fsync_calls", trace_journal ? trace_journal->sync_calls : 0);
    metrics.put("out_of_order_epochs", reordered_epochs);
    metrics.put("peak_inflight_chunks", peak_inflight_chunks);
    metrics.put("producer_wait_seconds", producer_wait);
    metrics.put("epoch_drain_seconds", drain_seconds);
    report.add_child("invocation_metrics", metrics);
    struct rusage cpu_end{};
    getrusage(RUSAGE_SELF, &cpu_end);
    auto cpu = [](const rusage &r) {
        return static_cast<double>(r.ru_utime.tv_sec + r.ru_stime.tv_sec) +
               static_cast<double>(r.ru_utime.tv_usec + r.ru_stime.tv_usec) / 1e6;
    };
    report.put("invocation_cpu_seconds", cpu(cpu_end) - cpu(cpu_begin));
    report.put("invocation_mean_busy_cpus", (cpu(cpu_end) - cpu(cpu_begin)) / elapsed(start));
    report.put("result_checksum_algorithm", "sha256-record-chain-v1");
    report.put("result_checksum", result_chain);
    report.put("result_bytes", fs::file_size(results));
    struct rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    long peak = usage.ru_maxrss;
    std::ifstream status("/proc/self/status");
    std::string status_line;
    while (std::getline(status, status_line))
        if (status_line.starts_with("VmHWM:")) {
            std::istringstream value(status_line.substr(6));
            value >> peak;
            break;
        }
    report.put("peak_rss_kib", peak);
    report.put("meaning", complete
                              ? "Configured shard exhausted; no claim of mathematical nonexistence"
                              : "Partial configured shard; resume required");
    atomic_write(o.output / "report.json", json(report));
    std::cout << json(report);
    return 0;
}
} // namespace dio
