#pragma once
#include "io.hpp"
#include <chrono>
#include <functional>
#include <string_view>
namespace dio {
struct Domain {
    Poly a;
    U m_min = 1, m_max = 1000;
    uint32_t prime_limit = 0;
    std::vector<U> factor_counts;
    std::vector<U> prime_exponents = {1}, power_factor_counts = {0};
    bool prime_powers = false;
    // Explicit prime-power modulus -> (base prime, exponent).
    std::map<U, std::pair<U,U>> power_bases;
    std::vector<std::vector<U>> moduli;
    std::vector<std::pair<int64_t, int64_t>> ranges;
    Json canonical;
    std::string fingerprint;
    bool symmetric = false;
    bool exclude_even = false;
    U excluded_explicit_moduli = 0;
};
Domain domain(const Json &config, const Database &db, bool planning = false);
// Counts complete last-factor intervals without constructing CRT roots or
// scanning k. Interrupted plans are explicitly labelled as lower bounds.
Json plan(const Domain &d, const Database &db, double seconds, U max_prefixes);
struct PowerFactor {
    U prime, exponent;
    std::vector<U> roots;
};
struct Generator {
    const Domain &d;
    std::vector<U> eligible;
    U shard, count, phase = 0, explicit_pos = 0;
    std::vector<U> chosen, next_index = {0};
    std::map<U, PowerFactor> powers;
    std::vector<U> power_options, next_distinct;
    std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max();
    std::chrono::steady_clock::time_point yield_deadline =
        std::chrono::steady_clock::time_point::max();
    bool yielded = false;
    Generator(const Domain &dom, const Database &db, U s, U n);
    bool next(std::vector<U> &factors);
    bool next_power(std::vector<U> &factors);
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
    U chunk_tiles = 256, queue_chunks = 0, checkpoint_tiles = 262144;
    double checkpoint_seconds = 2;
    double progress_seconds = 60; // Zero disables human-readable stderr progress.
    // Optional library observer for deterministic fault tests; absent in the CLI.
    std::function<void(std::string_view, U)> observer;
};
int search(const Domain &d, const Database &db, const Options &options, double setup_seconds);
} // namespace dio
