#include "search.hpp"
#include <chrono>
#include <iostream>
#include <set>
#include <sstream>
namespace {
void signal_handler(int) {
    dio::stopped = 1;
}
std::vector<dio::U> list(const std::string &s) {
    std::vector<dio::U> out;
    std::istringstream in(s);
    std::string part;
    while (std::getline(in, part, ',')) {
        auto n = dio::integer(part);
        if (n < 0 || n > UINT64_MAX)
            throw std::runtime_error("Unsigned value out of range");
        out.push_back(n.convert_to<dio::U>());
    }
    return out;
}
} // namespace
int main(int argc, char **argv) {
    try {
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);
        if (argc < 2 || std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h") {
            std::cout << "diophantasmagoria roots --polynomial g1|g2|regression|synthetic|symmetric "
                         "[--coefficients E,C,B,A] (--limit N | --primes p,q,...) --out FILE\n"
                      << "diophantasmagoria search --config FILE --db FILE --out DIR [--threads N] "
                         "[--shard I/N] [--arithmetic auto|128|big] [--resume] [--dry-run] "
                         "[--seconds N] [--max-tasks N] [--stop-on-hit] [--trace] [--no-sieve] "
                         "[--chunk-tiles N] [--queue-chunks N] [--checkpoint-tiles N] "
                         "[--checkpoint-seconds N] [--progress-seconds N]\n"
                      << "Independent verification, direct enumeration and merging: python3 "
                         "tools/oracle.py --help; python3 tools/merge.py --help\n"
                      << "diophantasmagoria plan --config FILE --db FILE [--seconds N] "
                         "[--max-prefixes N] [--out FILE]\n";
            return 0;
        }
        std::string cmd = argv[1];
        if (cmd != "roots" && cmd != "search" && cmd != "plan")
            throw std::runtime_error("Unknown command");
        std::map<std::string, std::string> opts;
        const std::set<std::string> flags = {"--resume", "--dry-run", "--stop-on-hit", "--trace",
                                             "--no-sieve"};
        const std::set<std::string> values =
            cmd == "roots" ? std::set<std::string>{"--polynomial", "--coefficients", "--limit",
                                                   "--primes", "--out"}
                           : cmd == "plan" ? std::set<std::string>{"--config", "--db", "--seconds", "--max-prefixes", "--out"}
                           : std::set<std::string>{"--config",
                                                   "--db",
                                                   "--out",
                                                   "--threads",
                                                   "--shard",
                                                   "--arithmetic",
                                                   "--seconds",
                                                   "--max-tasks",
                                                   "--chunk-tiles",
                                                   "--queue-chunks",
                                                   "--checkpoint-tiles",
                                                   "--checkpoint-seconds",
                                                   "--progress-seconds"};
        for (int i = 2; i < argc; ++i) {
            std::string k = argv[i];
            if (opts.contains(k))
                throw std::runtime_error("Duplicate option: " + k);
            if (cmd == "search" && flags.contains(k))
                opts[k] = "true";
            else if (values.contains(k) && i + 1 < argc)
                opts[k] = argv[++i];
            else
                throw std::runtime_error("Unknown option or missing value: " + k);
        }
        auto required = [&](const std::string &k) {
            if (!opts.contains(k))
                throw std::runtime_error("Missing option: " + k);
            return opts.at(k);
        };
        auto get = [&](const std::string &k, const std::string &fallback) {
            return opts.contains(k) ? opts.at(k) : fallback;
        };
        auto u = [&](const std::string &k, dio::U fallback) -> dio::U {
            auto n = dio::integer(get(k, std::to_string(fallback)));
            if (n < 0 || n > UINT64_MAX)
                throw std::runtime_error(k + " out of range");
            return n.convert_to<dio::U>();
        };
        auto start = std::chrono::steady_clock::now();
        if (cmd == "roots") {
            dio::Poly a;
            if (opts.contains("--coefficients")) {
                std::istringstream in(opts.at("--coefficients"));
                std::string part;
                size_t i = 0;
                while (std::getline(in, part, ',')) {
                    if (i >= 4)
                        throw std::runtime_error("Too many coefficients; exactly four are required");
                    a[i++] = dio::integer(part);
                }
                if (i != 4)
                    throw std::runtime_error("Exactly four coefficients are required");
            } else
                a = dio::polynomial(required("--polynomial"));
            if (opts.contains("--limit") == opts.contains("--primes"))
                throw std::runtime_error("Specify exactly one of --limit or --primes");
            dio::U limit = u("--limit", 0);
            if (limit > 100000000 || (!opts.contains("--primes") && limit < 2))
                throw std::runtime_error("Prime limit must be in [2,100000000]");
            auto ps = opts.contains("--primes") ? list(opts.at("--primes")) : std::vector<dio::U>{};
            if (opts.contains("--primes") && ps.empty())
                throw std::runtime_error("Empty prime list");
            dio::make_database(a, static_cast<uint32_t>(limit), ps, required("--out"));
            dio::Json j;
            j.put("database", opts.at("--out"));
            j.put("setup_seconds",
                  std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
            std::cout << dio::json(j);
            return 0;
        }
        dio::Options o;
        if (cmd == "search") o.output = required("--out");
        auto workers = u("--threads", 1);
        if (workers > 1024)
            throw std::runtime_error("At most 1024 workers supported");
        o.threads = static_cast<unsigned>(workers);
        o.arithmetic = get("--arithmetic", "auto");
        o.resume = opts.contains("--resume");
        o.dry_run = opts.contains("--dry-run");
        o.stop_on_hit = opts.contains("--stop-on-hit");
        o.trace = opts.contains("--trace");
        o.no_sieve = opts.contains("--no-sieve");
        o.max_tasks = u("--max-tasks", 0);
        o.chunk_tiles = u("--chunk-tiles", 256);
        o.queue_chunks = u("--queue-chunks", 0);
        o.checkpoint_tiles = u("--checkpoint-tiles", 262144);
        std::string interval = get("--checkpoint-seconds", "2");
        size_t interval_used = 0;
        o.checkpoint_seconds = std::stod(interval, &interval_used);
        if (interval_used != interval.size())
            throw std::runtime_error("Invalid checkpoint interval");
        std::string progress = get("--progress-seconds", "60");
        size_t progress_used = 0;
        o.progress_seconds = std::stod(progress, &progress_used);
        if (progress_used != progress.size() || !std::isfinite(o.progress_seconds) ||
            o.progress_seconds < 0)
            throw std::runtime_error("Invalid progress interval");
        std::string seconds = get("--seconds", "0");
        size_t used = 0;
        o.seconds = std::stod(seconds, &used);
        if (used != seconds.size() || !std::isfinite(o.seconds) || o.seconds < 0)
            throw std::runtime_error("Invalid time limit");
        std::string shard = get("--shard", "0/1");
        auto slash = shard.find('/');
        if (slash == std::string::npos)
            throw std::runtime_error("Shard format is I/N");
        auto pair = list(shard.substr(0, slash) + "," + shard.substr(slash + 1));
        if (pair.size() != 2)
            throw std::runtime_error("Invalid shard");
        o.shard = pair[0];
        o.shards = pair[1];
        if (cmd == "search" && o.progress_seconds > 0 && !o.dry_run)
            std::cerr << "[search] Loading root database and validating configuration...\n";
        auto db = dio::load_database(required("--db"));
        auto d = dio::domain(dio::parse(dio::read_file(required("--config"))), db, cmd == "plan");
        if (cmd == "plan") {
            std::string report = dio::json(dio::plan(d,db,o.seconds,u("--max-prefixes",0)));
            if (opts.contains("--out")) dio::atomic_write(opts.at("--out"),report);
            std::cout << report;
            return 0;
        }
        return dio::search(
            d, db, o,
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}
