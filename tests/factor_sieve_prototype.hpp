#pragma once
#include "io.hpp"
#include <algorithm>

namespace dio {
// Optional local necessary condition. All coefficients are obtained by exact
// division before reducing; no inverse of m modulo its factors is used.
class FactorSieve {
    std::map<U,std::vector<bool>> squares;
public:
    FactorSieve(const Database &db, uint32_t prime_limit, const std::vector<U> &explicit_primes = {}) {
        U entries = 0;
        for (const auto &[p,rs] : db.data) {
            if (rs.empty() || (prime_limit && p > prime_limit) ||
                (!explicit_primes.empty() && !std::binary_search(explicit_primes.begin(),explicit_primes.end(),p))) continue;
            // These primes are already present in the fixed-modulus sieve.
            if (p <= 13) continue;
            if (p > 1000000 || entries > 16000000-p)
                throw std::runtime_error("Factor sieve table budget exceeded");
            entries += p;
            std::vector<bool> table(p,false);
            for (U s=0;s<p;++s) table[(s*s)%p] = true;
            squares.emplace(p,std::move(table));
        }
    }
    std::array<U,2> masks(const Poly &poly, U m, U r, const std::vector<U> &factors,
                          int64_t lo, int64_t hi) const {
        U length = static_cast<U>(hi-lo+1);
        if (length < 1 || length > 64) throw std::runtime_error("Invalid factor-sieve tile");
        U all = length == 64 ? UINT64_MAX : (U(1)<<length)-1;
        std::array<U,2> masks = {all,all};
        Big value = eval(poly,Big(r));
        if (value%m != 0) throw std::runtime_error("Factor-sieve input is not a CRT root");
        Big quotient = value/m;
        Big derivative = (3*poly[3]*r+2*poly[2])*r+poly[1];
        for (U p : factors) {
            auto found = squares.find(p);
            if (found == squares.end()) continue;
            auto mod = [p](Big n) { n%=p; if(n<0) n+=p; return n.convert_to<U>(); };
            const auto &table = found->second;
            U slope = mod(Big(4*derivative)), negative_slope = (p-slope)%p;
            U common = mod(Big(-107*Big(r)*r-8));
            U q = mod(Big(4*(quotient+Big(lo)*derivative)));
            U plus = (common+q)%p, minus = (common+p-q)%p;
            // slope=0 is the singular case: the local condition is constant in k.
            for (U j=0;j<length;++j) {
                U bit = U(1)<<j;
                if ((masks[0]&bit) && !table[plus]) masks[0] &= ~bit;
                if ((masks[1]&bit) && !table[minus]) masks[1] &= ~bit;
                plus += slope; if (plus>=p) plus-=p;
                minus += negative_slope; if (minus>=p) minus-=p;
            }
            if (!(masks[0]|masks[1])) break;
        }
        return masks;
    }
};
} // namespace dio
