#pragma once
#include "io.hpp"
#include <chrono>
namespace dio {
struct Domain {
    Poly a;
    U m_min = 1, m_max = 1000;
    uint32_t prime_limit = 0;
    std::vector<U> factor_counts;
    std::vector<std::vector<U>> moduli;
    std::vector<std::pair<int64_t, int64_t>> ranges;
    Json canonical;
    std::string fingerprint;
    bool exclude_even = false;
    U excluded_explicit_moduli = 0;
};
Domain domain(const Json &config, const Database &db);
struct Generator {
    const Domain &d;
    std::vector<U> eligible;
    U shard, count, phase = 0, explicit_pos = 0;
    std::vector<U> chosen, next_index = {0};
    std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max();
    Generator(const Domain &dom, const Database &db, U s, U n);
    bool next(std::vector<U> &factors);
    Json state() const;
    void restore(const Json &j);
};
struct Options {
    fs::path output;
    unsigned threads = 1;
    U shard = 0, shards = 1;
    std::string arithmetic = "auto";
    bool resume = false, dry_run = false, stop_on_hit = false, trace = false, no_sieve = false;
    double seconds = 0;
    U max_tasks = 0;
};
int search(const Domain &d, const Database &db, const Options &options, double setup_seconds);
} // namespace dio
