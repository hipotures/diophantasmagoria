#include "io.hpp"
#include <boost/property_tree/json_parser.hpp>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <openssl/evp.h>
#include <set>
#include <sstream>
#include <sys/file.h>
#include <unistd.h>
namespace dio {
std::atomic<bool> stopped{false};
static_assert(std::atomic<bool>::is_always_lock_free, "Signal handling requires lock-free atomics");
Json parse(const std::string &s) {
    std::istringstream in(s);
    Json j;
    boost::property_tree::read_json(in, j);
    return j;
}
namespace {
std::string quote(const std::string &text) {
    std::ostringstream out;
    out << '"';
    for (unsigned char c : text) {
        if (c == '"' || c == '\\')
            out << '\\' << c;
        else if (c < 32)
            out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c);
        else
            out << c;
    }
    out << '"';
    return out.str();
}
void encode(std::ostream &out, const Json &node, const std::string &key, bool top = false) {
    static const std::set<std::string> arrays = {"coefficients", "roots",    "factor_counts",
                                                 "moduli",       "k_ranges", "chosen",
                                                 "next_index",   "active",   "factors"};
    bool is_array = (arrays.contains(key) && node.data().empty()) ||
                    (!node.empty() && node.front().first.empty());
    if (node.empty() && !is_array && !top) {
        out << quote(node.data());
        return;
    }
    out << (is_array ? '[' : '{');
    bool first = true;
    for (const auto &item : node) {
        if (!first)
            out << ',';
        first = false;
        if (!is_array)
            out << quote(item.first) << ':';
        encode(out, item.second, item.first);
    }
    out << (is_array ? ']' : '}');
}
} // namespace
std::string json(const Json &j) {
    std::ostringstream out;
    encode(out, j, "", true);
    out << '\n';
    return out.str();
}
namespace {
struct Hash {
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    Hash() {
        if (!ctx || EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1)
            throw std::runtime_error("SHA-256 initialization failed");
    }
    ~Hash() {
        EVP_MD_CTX_free(ctx);
    }
    void add(std::string_view s) {
        if (EVP_DigestUpdate(ctx, s.data(), s.size()) != 1)
            throw std::runtime_error("SHA-256 update failed");
    }
    std::string finish() {
        unsigned char bytes[EVP_MAX_MD_SIZE];
        unsigned n = 0;
        if (EVP_DigestFinal_ex(ctx, bytes, &n) != 1)
            throw std::runtime_error("SHA-256 finalization failed");
        std::ostringstream s;
        for (unsigned i = 0; i < n; ++i)
            s << std::hex << std::setw(2) << std::setfill('0') << unsigned(bytes[i]);
        return s.str();
    }
};
void write_all(int fd, const std::string &s) {
    size_t pos = 0;
    while (pos < s.size()) {
        ssize_t n = ::write(fd, s.data() + pos, s.size() - pos);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            throw std::runtime_error("File write failed");
        }
        pos += static_cast<size_t>(n);
    }
}
void sync_dir(const fs::path &p) {
    int fd = open(p.c_str(), O_RDONLY | O_DIRECTORY);
    if (fd < 0)
        throw std::runtime_error("Cannot open output directory");
    int r = fsync(fd);
    close(fd);
    if (r)
        throw std::runtime_error("Directory fsync failed");
}
} // namespace
std::string sha256(const std::string &s) {
    Hash h;
    h.add(s);
    return h.finish();
}
std::string sha252_concat(std::string_view a, std::string_view b) {
    Hash h;
    h.add(a);
    h.add(b);
    return h.finish();
}
std::string file_hash(const fs::path &p) {
    std::ifstream in(p, std::ios::binary);
    if (!in)
        throw std::runtime_error("Cannot read: " + p.string());
    Hash h;
    char buf[65536];
    while (in.read(buf, sizeof(buf)) || in.gcount())
        h.add(std::string_view(buf, static_cast<size_t>(in.gcount())));
    return h.finish();
}
std::string read_file(const fs::path &p) {
    std::ifstream in(p);
    if (!in)
        throw std::runtime_error("Cannot read: " + p.string());
    return {std::istreambuf_iterator<char>(in), {}};
}
void atomic_write(const fs::path &p, const std::string &data) {
    fs::path temp = p;
    temp += ".new";
    int fd = open(temp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        throw std::runtime_error("Cannot create: " + temp.string());
    try {
        write_all(fd, data);
        if (fsync(fd))
            throw std::runtime_error("fsync failed");
    } catch (...) {
        close(fd);
        throw;
    }
    close(fd);
    fs::rename(temp, p);
    sync_dir(p.parent_path().empty() ? fs::path(".") : p.parent_path());
}
void append_durable(const fs::path &p, const std::string &data) {
    if (data.empty())
        return;
    int fd = open(p.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0)
        throw std::runtime_error("Cannot append: " + p.string());
    try {
        write_all(fd, data);
        if (fsync(fd))
            throw std::runtime_error("Result fsync failed");
    } catch (...) {
        close(fd);
        throw;
    }
    close(fd);
}
Journal::Journal(const fs::path &p, bool recovered_uncommitted)
    : path(p), dirty(recovered_uncommitted) {
    // Creation is separate from appending. A new empty journal is made durable once.
    if (!fs::exists(path))
        atomic_write(path, "");
}
void Journal::open_file() {
    if (fd < 0) {
        fd = open(path.c_str(), O_WRONLY | O_APPEND);
        if (fd < 0)
            throw std::runtime_error("Cannot open journal: " + path.string());
    }
}
Journal::~Journal() {
    if (fd >= 0)
        close(fd);
}
void Journal::append(const std::string &data) {
    if (data.empty())
        return;
    open_file();
    write_all(fd, data);
    dirty = true;
    ++append_calls;
}
void Journal::sync() {
    if (!dirty)
        return;
    open_file();
    if (fsync(fd))
        throw std::runtime_error("Journal fsync failed: " + path.string());
    dirty = false;
    ++sync_calls;
}
void repair_tail(const fs::path &p) {
    if (!fs::exists(p))
        return;
    int fd = open(p.c_str(), O_RDWR);
    if (fd < 0)
        throw std::runtime_error("Cannot open result journal");
    off_t end = lseek(fd, 0, SEEK_END), good = end;
    char c;
    while (good > 0) {
        if (pread(fd, &c, 1, good - 1) != 1) {
            close(fd);
            throw std::runtime_error("Cannot inspect journal tail");
        }
        if (c == '\n')
            break;
        --good;
    }
    if (good != end && (ftruncate(fd, good) || fsync(fd))) {
        close(fd);
        throw std::runtime_error("Cannot repair incomplete journal tail");
    }
    close(fd);
}
Json array(const std::vector<U> &v) {
    Json a;
    for (U n : v) {
        Json x;
        x.put_value(std::to_string(n));
        a.push_back({"", x});
    }
    return a;
}
std::vector<U> numbers(const Json &a) {
    std::vector<U> v;
    for (const auto &item : a) {
        Big n = integer(item.second.get_value<std::string>());
        if (n < 0 || n > UINT64_MAX)
            throw std::runtime_error("Unsigned integer out of range");
        v.push_back(n.convert_to<U>());
    }
    return v;
}
Json coefficients(const Poly &p) {
    Json a;
    for (const Big &n : p) {
        Json x;
        x.put_value(decimal(n));
        a.push_back({"", x});
    }
    return a;
}
Poly get_poly(const Json &j) {
    if (auto c = j.get_child_optional("coefficients")) {
        if (c->size() != 4)
            throw std::runtime_error("Exactly four coefficients required, constant first");
        Poly p;
        size_t i = 0;
        for (const auto &x : *c)
            p[i++] = integer(x.second.get_value<std::string>());
        return p;
    }
    return polynomial(j.get<std::string>("polynomial"));
}
void make_database(const Poly &a, uint32_t limit, const std::vector<U> &selected,
                   const fs::path &path) {
    fs::create_directories(path.parent_path().empty() ? fs::path(".") : path.parent_path());
    Lock lock(path.string() + ".lock");
    std::vector<U> ps = selected;
    if (ps.empty()) {
        for (auto p : primes(limit))
            ps.push_back(p);
    } else {
        std::sort(ps.begin(), ps.end());
        if (std::adjacent_find(ps.begin(), ps.end()) != ps.end())
            throw std::runtime_error("Duplicate explicit prime");
        for (U p : ps) {
            if (p < 2 || p > 100000000)
                throw std::runtime_error("Explicit prime outside supported range");
            for (U d = 2; d * d <= p; ++d)
                if (p % d == 0)
                    throw std::runtime_error("Explicit factor is not prime");
        }
    }
    Json h;
    h.put("schema", "dio-roots-v1");
    h.put("algorithm", "finite-field-gcd-split-v1");
    h.add_child("coefficients", coefficients(a));
    h.put("coverage", selected.empty() ? "all-primes" : "explicit");
    h.put("prime_limit", limit);
    fs::path temp = path;
    temp += ".generating";
    std::ofstream out(temp);
    if (!out)
        throw std::runtime_error("Cannot create database");
    Hash hash;
    auto emit = [&](const std::string &s) {
        out << s;
        hash.add(s);
    };
    emit(json(h));
    for (U p : ps) {
        if (stopped)
            throw std::runtime_error("Precomputation interrupted; database not published");
        Json row;
        row.put("prime", p);
        row.add_child("roots", array(roots(a, p)));
        emit(json(row));
    }
    Json foot;
    foot.put("checksum", hash.finish());
    out << json(foot);
    out.close();
    if (!out)
        throw std::runtime_error("Database write failed");
    int fd = open(temp.c_str(), O_RDONLY);
    if (fd < 0 || fsync(fd)) {
        if (fd >= 0)
            close(fd);
        throw std::runtime_error("Database fsync failed");
    }
    close(fd);
    fs::rename(temp, path);
    sync_dir(path.parent_path().empty() ? fs::path(".") : path.parent_path());
}
Database load_database(const fs::path &path) {
    std::ifstream in(path);
    if (!in)
        throw std::runtime_error("Cannot open root database: " + path.string());
    std::string line;
    if (!std::getline(in, line))
        throw std::runtime_error("Empty root database");
    Hash hash;
    hash.add(line + "\n");
    Json h = parse(line);
    if (h.get<std::string>("schema") != "dio-roots-v1" ||
        h.get<std::string>("algorithm") != "finite-field-gcd-split-v1")
        throw std::runtime_error("Unsupported root database version");
    Database db;
    db.a = get_poly(h);
    db.limit = h.get<uint32_t>("prime_limit");
    std::string coverage = h.get<std::string>("coverage");
    if (coverage != "explicit" && coverage != "all-primes")
        throw std::runtime_error("Invalid database coverage");
    db.explicit_only = coverage == "explicit";
    U last = 0;
    bool footer = false;
    while (std::getline(in, line)) {
        Json row = parse(line);
        if (auto c = row.get_optional<std::string>("checksum")) {
            db.checksum = *c;
            footer = true;
            break;
        }
        hash.add(line + "\n");
        U p = row.get<U>("prime");
        if (p <= last || p > 100000000 || p < 2)
            throw std::runtime_error("Invalid prime ordering");
        last = p;
        auto rs = numbers(row.get_child("roots"));
        if (!std::is_sorted(rs.begin(), rs.end()) ||
            std::adjacent_find(rs.begin(), rs.end()) != rs.end())
            throw std::runtime_error("Invalid root ordering");
        for (U r : rs)
            if (r >= p || eval(db.a, Big(r)) % p != 0)
                throw std::runtime_error("Invalid stored modular root");
        db.data.emplace(p, std::move(rs));
    }
    if (!footer || db.checksum != hash.finish() || std::getline(in, line))
        throw std::runtime_error("Root database checksum/truncation failure; regenerate it");
    if (!db.explicit_only) {
        auto ps = primes(db.limit);
        if (ps.size() != db.data.size())
            throw std::runtime_error("Incomplete prime coverage");
        auto it = db.data.begin();
        for (auto p : ps)
            if ((it++)->first != p)
                throw std::runtime_error("Incorrect prime coverage");
    } else
        for (const auto &[p, rs] : db.data) {
            (void)rs;
            for (U d = 2; d * d <= p; ++d)
                if (p % d == 0)
                    throw std::runtime_error("Nonprime explicit database entry");
        }
    return db;
}
Lock::Lock(const fs::path &path) {
    fd = open(path.c_str(), O_RDWR | O_CREAT, 0644);
    if (fd < 0 || flock(fd, LOCK_EX | LOCK_NB)) {
        if (fd >= 0)
            close(fd);
        fd = -1;
        throw std::runtime_error("Output is locked by another process: " + path.string());
    }
}
Lock::~Lock() {
    if (fd >= 0)
        close(fd);
}
} // namespace dio
