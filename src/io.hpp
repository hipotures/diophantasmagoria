#pragma once
#include "math.hpp"
#include <atomic>
#include <boost/property_tree/ptree.hpp>
#include <csignal>
#include <filesystem>
#include <map>
namespace dio {
using Json = boost::property_tree::ptree;
namespace fs = std::filesystem;
extern std::atomic<bool> stopped;
Json parse(const std::string &s);
std::string json(const Json &j);
std::string sha256(const std::string &s);
std::string file_hash(const fs::path &p);
std::string read_file(const fs::path &p);
void atomic_write(const fs::path &p, const std::string &data);
void append_durable(const fs::path &p, const std::string &data);
void repair_tail(const fs::path &p);
Json array(const std::vector<U> &v);
std::vector<U> numbers(const Json &a);
Json coefficients(const Poly &p);
Poly get_poly(const Json &j);
struct Database {
    Poly a;
    uint32_t limit = 0;
    bool explicit_only = false;
    std::map<U, std::vector<U>> data;
    std::string checksum;
};
void make_database(const Poly &a, uint32_t limit, const std::vector<U> &explicit_primes,
                   const fs::path &path);
Database load_database(const fs::path &path);
struct Lock {
    int fd = -1;
    explicit Lock(const fs::path &path);
    ~Lock();
    Lock(const Lock &) = delete;
};
} // namespace dio
