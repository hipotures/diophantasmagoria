#include "math.hpp"
#include "io.hpp"
#include <algorithm>
#include <limits>
#include <random>
#include <stdexcept>
namespace dio {
Big integer(const std::string &s) {
    if (s.empty())
        throw std::runtime_error("Empty integer");
    size_t i = s[0] == '-' ? 1 : 0;
    if (i == s.size())
        throw std::runtime_error("Invalid integer: " + s);
    Big n = 0;
    for (; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9')
            throw std::runtime_error("Invalid decimal integer: " + s);
        n = n * 10 + (s[i] - '0');
    }
    return s[0] == '-' ? -n : n;
}
std::string decimal(const Big &n) {
    return n.convert_to<std::string>();
}
Poly polynomial(const std::string &name) {
    if (name == "symmetric")
        return {1, 4, 0, 26};
    if (name == "g1")
        return {-1, 3, 1, 1};
    if (name == "g2")
        return {1, 0, 1, 6};
    if (name == "regression")
        return {-1, 1, 1, 3};
    if (name == "synthetic")
        return {0, -1, 0, 1};
    throw std::runtime_error("Unknown polynomial: " + name);
}
bool excludes_even_square_free(const Poly &a) {
    return a == polynomial("g1") || a == polynomial("g2");
}
Big eval(const Poly &a, const Big &x) {
    return evaluate(a, x);
}
std::vector<uint32_t> primes(uint32_t limit) {
    if (limit > 100000000)
        throw std::runtime_error("Prime limit exceeds supported bounded sieve limit 100000000");
    std::vector<uint32_t> out;
    if (limit < 2)
        return out;
    out.push_back(2);
    if (limit < 3)
        return out;
    // Odd-only sieve: index i represents the odd number 2*i+3, halving memory and
    // marking work by skipping even multiples.
    size_t size = (static_cast<size_t>(limit) - 1) / 2;
    std::vector<bool> composite(size, false);
    for (size_t i = 0; i < size; ++i) {
        if ((i & 1023U) == 0 && stopped)
            throw std::runtime_error("Prime sieve interrupted");
        if (composite[i])
            continue;
        uint32_t p = static_cast<uint32_t>(2 * i + 3);
        out.push_back(p);
        if (U(p) * p <= limit)
            for (U j = U(p) * p; j <= limit; j += 2 * U(p))
                composite[(j - 3) / 2] = true;
    }
    return out;
}
namespace {
using F = std::vector<U>;
U mul(U a, U b, U p) {
    return static_cast<U>((__uint128_t(a) * b) % p);
}
U power(U a, U e, U p) {
    U r = 1;
    for (; e; e >>= 1, a = mul(a, a, p))
        if (e & 1)
            r = mul(r, a, p);
    return r;
}
void trim(F &a) {
    while (!a.empty() && a.back() == 0)
        a.pop_back();
}
F rem(F a, const F &b, U p) {
    if (b.empty())
        throw std::runtime_error("Polynomial division by zero");
    const U inv = power(b.back(), p - 2, p);
    while (a.size() >= b.size()) {
        U c = mul(a.back(), inv, p);
        size_t shift = a.size() - b.size();
        for (size_t i = 0; i < b.size(); ++i)
            a[i + shift] = (a[i + shift] + p - mul(c, b[i], p)) % p;
        trim(a);
    }
    return a;
}
F monic(F a, U p) {
    if (a.empty())
        return a;
    U inv = power(a.back(), p - 2, p);
    for (auto &c : a)
        c = mul(c, inv, p);
    return a;
}
F gcd(F a, F b, U p) {
    while (!b.empty()) {
        F c = rem(a, b, p);
        a = b;
        b = c;
    }
    return monic(a, p);
}
F multiply(const F &a, const F &b, const F &f, U p) {
    if (a.empty() || b.empty())
        return {};
    F c(a.size() + b.size() - 1);
    for (size_t i = 0; i < a.size(); ++i)
        for (size_t j = 0; j < b.size(); ++j)
            c[i + j] = (c[i + j] + mul(a[i], b[j], p)) % p;
    trim(c);
    return rem(c, f, p);
}
F powmod(F a, U e, const F &f, U p) {
    F r = {1};
    a = rem(a, f, p);
    for (; e; e >>= 1, a = multiply(a, a, f, p))
        if (e & 1)
            r = multiply(r, a, f, p);
    return r;
}
F quotient(F a, const F &b, U p) {
    F q(a.size() - b.size() + 1);
    U inv = power(b.back(), p - 2, p);
    while (a.size() >= b.size()) {
        size_t j = a.size() - b.size();
        U c = mul(a.back(), inv, p);
        q[j] = c;
        for (size_t i = 0; i < b.size(); ++i)
            a[i + j] = (a[i + j] + p - mul(c, b[i], p)) % p;
        trim(a);
    }
    if (!a.empty())
        throw std::runtime_error("Incomplete factor division");
    trim(q);
    return q;
}
void split(const F &f, U p, std::mt19937_64 &rng, std::vector<U> &out) {
    if (f.size() == 1)
        return;
    if (f.size() == 2) {
        out.push_back(mul((p - f[0]) % p, power(f[1], p - 2, p), p));
        return;
    }
    for (unsigned attempt = 0; attempt < 4096; ++attempt) {
        F a(f.size() - 1);
        for (auto &c : a)
            c = rng() % p;
        trim(a);
        F g = gcd(f, a, p);
        if (g.size() <= 1 || g.size() == f.size()) {
            F h = powmod(a, (p - 1) / 2, f, p);
            if (h.empty())
                h.push_back(0);
            h[0] = (h[0] + p - 1) % p;
            trim(h);
            g = gcd(f, h, p);
        }
        if (g.size() > 1 && g.size() < f.size()) {
            split(g, p, rng, out);
            split(quotient(f, g, p), p, rng, out);
            return;
        }
    }
    throw std::runtime_error("Finite-field split retry limit reached; no database was published");
}
} // namespace
std::vector<U> roots(const Poly &a, U p) {
    if (p < 2 || p > 100000000)
        throw std::runtime_error("Root prime must be in [2,100000000]");
    F f;
    for (const auto &c : a) {
        Big n = c % p;
        if (n < 0)
            n += p;
        f.push_back(n.convert_to<U>());
    }
    trim(f);
    std::vector<U> out;
    if (p <= 97 || f.empty()) {
        if (f.empty() && p > 1000000)
            throw std::runtime_error("Zero polynomial modulo large prime: more than 1000000 roots unsupported");
        for (U x = 0; x < p; ++x) {
            U v = 0;
            for (auto i = f.rbegin(); i != f.rend(); ++i)
                v = (mul(v, x, p) + *i) % p;
            if (v == 0)
                out.push_back(x);
        }
        return out;
    }
    if (f.size() == 1)
        return out;
    F h = powmod({0, 1}, p, f, p);
    h.resize(std::max<size_t>(2, h.size()));
    h[1] = (h[1] + p - 1) % p;
    trim(h);
    F linear = gcd(f, h, p);
    std::mt19937_64 rng(0xd10fa17ULL ^ p);
    split(linear, p, rng, out);
    std::sort(out.begin(), out.end());
    if (out.size() + 1 != linear.size() || std::adjacent_find(out.begin(), out.end()) != out.end())
        throw std::runtime_error("Incomplete or duplicate finite-field roots");
    for (U r : out)
        if (eval(a, Big(r)) % p != 0)
            throw std::runtime_error("Invalid finite-field root");
    return out;
}
std::vector<U> crt(const std::vector<U> &ps, const std::vector<std::vector<U>> &rs) {
    if (ps.size() != rs.size())
        throw std::runtime_error("CRT dimensions differ");
    std::vector<U> out = {0};
    U m = 1;
    for (size_t i = 0; i < ps.size(); ++i) {
        U p = ps[i];
        if (m > U(INT64_MAX) / p)
            throw std::runtime_error("CRT modulus exceeds signed 64-bit domain");
        U inv = power(m % p, p - 2, p);
        if (inv == 0)
            throw std::runtime_error("CRT factors are not coprime");
        std::vector<U> next;
        if (rs[i].size() && out.size() > 1000000 / rs[i].size())
            throw std::runtime_error("CRT exceeds 1000000 roots per modulus; reduce factors");
        next.reserve(out.size() * rs[i].size());
        for (U r : out)
            for (U t : rs[i]) {
                U c = mul((t + p - r % p) % p, inv, p);
                next.push_back(r + m * c);
            }
        m *= p;
        out = std::move(next);
    }
    std::sort(out.begin(), out.end());
    return out;
}
Big arithmetic_bound(const Poly &a, U m, int64_t lo, int64_t hi) {
    Big k = std::max(Big(lo < 0 ? -Big(lo) : Big(lo)), Big(hi < 0 ? -Big(hi) : Big(hi)));
    Big x = (k + 4) * m + m, t = 0, pow = 1;
    for (const Big &c : a) {
        t += (c < 0 ? -c : c) * pow;
        pow *= x + 1;
    }
    return 128 * (t + Big(m) * m + m + x + 1);
}
bool native_safe(const Poly &a, U m, int64_t lo, int64_t hi) {
    return arithmetic_bound(a, m, lo, hi) < (Big(1) << 127);
}
bool residue_allowed(unsigned modulus, unsigned value) {
    static const auto tables = [] {
        std::array<std::array<bool, 65>, 66> t{};
        for (unsigned q : {64U, 63U, 65U, 11U})
            for (unsigned s = 0; s < q; ++s)
                t[q][s * s % q] = true;
        return t;
    }();
    return tables[modulus][value];
}
} // namespace dio
