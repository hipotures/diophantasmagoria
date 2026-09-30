#pragma once
#include "math.hpp"
#include <algorithm>

namespace dio {
// The polynomial database describes divisibility, not the equation being solved.
inline constexpr auto symmetric_backend = "symmetric-cubic-v1";
inline bool symmetric_coefficients(const Poly &a) {
    return a[1] == 4 && a[2] == 0 && a[3] == 26;
}
inline Big symmetric_residual(const Big &x, const Big &y, const Big &z, const Big &constant) {
    return x*x*x + y*y*y + z*z*z + 2*(x+y+z) - x*y*z - constant;
}

// Conservative bounds include the extra three samples used for initialization,
// all polynomial terms, and the absolute coefficient sums of finite differences.
// Reconstruction and independent residual verification always use Big.
inline bool symmetric_native_safe(const Poly &a, U m, int64_t lo, int64_t hi) {
    Big k = std::max(Big(lo < 0 ? -Big(lo) : Big(lo)),
                     Big(hi < 0 ? -Big(hi) : Big(hi)));
    Big x = (k + 5) * m, t = 0, power = 1;
    for (const Big &c : a) {
        t += (c < 0 ? -c : c) * power;
        power *= x + 1;
    }
    Big bound = 128 * (4*t + 4*Big(m)*m + 36*Big(m)*x + 107*x*x + 8 + m + x + 1);
    return bound < (Big(1) << 127);
}

template <class N> struct CubicSequence {
    N value, first, second, third;
    explicit CubicSequence(const std::array<N, 4> &h)
        : value(h[0]), first(h[1]-h[0]), second(h[2]-2*h[1]+h[0]),
          third(h[3]-3*h[2]+3*h[1]-h[0]) {}
    void next() {
        value += first;
        first += second;
        second += third;
    }
};

template <class N, bool Symmetric> struct CandidateSequence;
template <class N> struct CandidateSequence<N, false> {
    Differences<N> quotient;
    N m;
    CandidateSequence(const std::array<N, 4> &a, U modulus, U root, int64_t k)
        : quotient(a, modulus, root, k), m(modulus) {}
    N delta(int sign) const { return m*m - 4*sign*quotient.h; }
    void next() { quotient.next(); }
};

template <class N> struct CandidateSequence<N, true> {
    CubicSequence<N> positive, negative;
    static std::array<std::array<N, 4>, 2> samples(const std::array<N, 4> &a,
                                                  U modulus, U root, int64_t k) {
        const N m = modulus;
        Differences<N> quotient(a, modulus, root, k);
        std::array<std::array<N, 4>, 2> values;
        N coordinate = N(root) + N(k)*m;
        for (size_t i = 0; i < 4; ++i) {
            const N common = -4*m*m - 107*coordinate*coordinate - 8;
            const N odd = 4*quotient.h + 36*coordinate*m;
            values[0][i] = common + odd;
            values[1][i] = common - odd;
            if (i != 3) {
                quotient.next();
                coordinate += m;
            }
        }
        return values;
    }
    explicit CandidateSequence(const std::array<std::array<N, 4>, 2> &h)
        : positive(h[0]), negative(h[1]) {}
    CandidateSequence(const std::array<N, 4> &a, U m, U r, int64_t k)
        : CandidateSequence(samples(a, m, r, k)) {}
    N delta(int sign) const { return sign == 1 ? positive.value : negative.value; }
    void next() { positive.next(); negative.next(); }
};
} // namespace dio
