#pragma once
#include "math.hpp"
#include <atomic>
#include <boost/property_tree/ptree.hpp>
#include <csignal>
#include <filesystem>
#include <map>
#include <string_view>
namespace dio {
using Json = boost::property_tree::ptree;
namespace fs = std::filesystem;
extern std::atomic<bool> stopped;
Json parse(const std::string &s);
std::string json(const Json &j);
std::string sha256(const std::string &s);
std::string sha256_concat(std::string_view a, std::string_view b);
std::string file_hash(const fs::path &p);
std::string read_file(const fs::path &p);
void atomic_write(const fs::path &p, const std::string &data);
void append_durable(const fs::path &p, const std::string &data);
// Main-thread journal: stream records now, sync dirty bytes before a checkpoint.
class Journal {
    fs::path path;
    int fd = -1;
    bool dirty = false;
    void open_file();

  public:
    U append_calls = 0, sync_calls = 0;
    explicit Journal(const fs::path &p, bool recovered_uncommitted = false);
    ~Journal();
    Journal(const Journal &) = delete;
    void append(const std::string &data);
    void sync();
};
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
