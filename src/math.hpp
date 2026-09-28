#pragma once
#include <array>
#include <boost/multiprecision/cpp_int.hpp>
#include <cstdint>
#include <string>
#include <vector>
namespace dio {
using Big = boost::multiprecision::cpp_int;
using U = uint64_t;
using Poly = std::array<Big, 4>; // Constant coefficient first.
Big integer(const std::string &s);
std::string decimal(const Big &n);
Poly polynomial(const std::string &name);
// Certified only for the exact g1/g2 coefficients and square-free moduli.
bool excludes_even_square_free(const Poly &a);
Big eval(const Poly &a, const Big &x);
std::vector<uint32_t> primes(uint32_t limit);
std::vector<U> roots(const Poly &a, U p);
std::vector<U> crt(const std::vector<U> &ps, const std::vector<std::vector<U>> &rs);
Big arithmetic_bound(const Poly &a, U m, int64_t lo, int64_t hi);
bool native_safe(const Poly &a, U m, int64_t lo, int64_t hi);
template <class N> N square_root(const N &n) {
    if (n < 0)
        throw std::runtime_error("Square root of a negative integer");
    if (n < 2)
        return n;
    N v = n, x = 1;
    unsigned bits = 0;
    while (v != 0) {
        v >>= 1;
        ++bits;
    }
    x <<= (bits + 1) / 2;
    for (;;) {
        N y = (x + n / x) / 2;
        if (y >= x)
            return x;
        x = y;
    }
}
template <class N> N evaluate(const std::array<N, 4> &a, const N &x) {
    return ((a[3] * x + a[2]) * x + a[1]) * x + a[0];
}
template <class N> struct Differences {
    N h, first, second, third;
    Differences(const std::array<N, 4> &a, U modulus, U root, int64_t k) {
        const N m = modulus, r = root;
        const N b0 = evaluate(a, r) / m;
        const N b1 = 3 * a[3] * r * r + 2 * a[2] * r + a[1];
        const N b2 = m * (3 * a[3] * r + a[2]);
        const N b3 = a[3] * m * m;
        auto at = [&](N t) -> N { return ((b3 * t + b2) * t + b1) * t + b0; };
        h = at(N(k));
        N h1 = at(N(k) + 1), h2 = at(N(k) + 2), h3 = at(N(k) + 3);
        first = h1 - h;
        second = h2 - 2 * h1 + h;
        third = h3 - 3 * h2 + 3 * h1 - h;
    }
    void next() {
        h += first;
        first += second;
        second += third;
    }
};
bool residue_allowed(unsigned modulus, unsigned value);
template <class N> bool sieve(const N &delta) {
    for (unsigned q : {64U, 63U, 65U, 11U}) {
        if (!residue_allowed(q, static_cast<unsigned>(delta % q)))
            return false;
    }
    return true;
}
} // namespace dio
