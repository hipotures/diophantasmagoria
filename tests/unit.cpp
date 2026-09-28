#include "search.hpp"
#include <algorithm>
#include <iostream>
#include <random>
#include <set>
using namespace dio;
void require(bool b, const char *message) {
    if (!b)
        throw std::runtime_error(message);
}
int main() {
    try {
        std::vector<Poly> polys = {polynomial("g1"),
                                   polynomial("g2"),
                                   polynomial("regression"),
                                   polynomial("synthetic"),
                                   {1, -3, 3, -1},
                                   {2, 2, 2, 2},
                                   {0, 0, 0, 0},
                                   {101, 202, 0, 101},
                                   {1, 1, 101, 101}};
        for (const auto &a : polys)
            for (U p : primes(1500)) {
                std::vector<U> expected;
                for (U x = 0; x < p; ++x)
                    if (eval(a, Big(x)) % p == 0)
                        expected.push_back(x);
                require(roots(a, p) == expected, "Root completeness mismatch");
            }
        for (const auto &a : polys) {
            std::vector<U> ps = {2, 3, 5, 7};
            std::vector<std::vector<U>> rs;
            for (U p : ps)
                rs.push_back(roots(a, p));
            auto got = crt(ps, rs);
            std::vector<U> want;
            for (U x = 0; x < 210; ++x)
                if (eval(a, Big(x)) % 210 == 0)
                    want.push_back(x);
            require(got == want, "CRT mismatch");
        }
        for (const auto &a : polys)
            for (U m : {2, 5, 127, 997})
                for (U r : roots(a, m))
                    for (int64_t start : {-257, -65, -1, 0, 1, 63, 256}) {
                        Differences<Big> diff(a, m, r, start);
                        std::array<__int128_t, 4> small;
                        for (size_t i = 0; i < 4; ++i)
                            small[i] = a[i].convert_to<__int128_t>();
                        Differences<__int128_t> fast(small, m, r, start);
                        for (int64_t k = start; k < start + 130; ++k) {
                            Big want = eval(a, Big(r) + Big(k) * m) / m;
                            require(diff.h == want && Big(fast.h) == want,
                                    "Finite difference mismatch");
                            diff.next();
                            fast.next();
                        }
                    }
        for (unsigned q : {64U, 63U, 65U, 11U})
            for (unsigned s = 0; s < q; ++s)
                require(residue_allowed(q, s * s % q), "Sieve false negative");
        for (unsigned bits = 0; bits <= 512; ++bits) {
            Big s = Big(1) << bits, sq = s * s;
            require(square_root(sq) == s, "Large square failure");
            require(square_root(Big(sq + 1)) == s, "Near square failure");
            require(square_root(Big(sq - 1)) == s - 1, "Lower near square failure");
            require(sieve(sq), "Large sieve false negative");
        }
        // Exercise finite differences at the conservative native-domain boundary.
        {
            Poly a = polynomial("synthetic");
            U low = 2, high = U(INT64_MAX);
            while (low < high) {
                U mid = low + (high - low + 1) / 2;
                if (native_safe(a, mid, -256, 256))
                    low = mid;
                else
                    high = mid - 1;
            }
            require(native_safe(a, low, -256, 256) && !native_safe(a, low + 1, -256, 256),
                    "Native boundary selection mismatch");
            std::array<__int128_t, 4> aa = {0, -1, 0, 1};
            for (int64_t start : {-256, -1, 0, 253}) {
                Differences<__int128_t> fast(aa, low, 0, start);
                for (int64_t k = start; k <= start + 3; ++k) {
                    require(Big(fast.h) == eval(a, Big(k) * low) / low,
                            "Native boundary difference mismatch");
                    if (k < start + 3)
                        fast.next();
                }
            }
        }
        require(square_root(Big(0)) == 0, "Zero square failure");
        require(square_root(Big(1)) == 1, "One square failure");
        for (unsigned bits = 1; bits < 126; ++bits) {
            __int128_t v = (__int128_t(1) << bits) - 1;
            auto s = square_root(v);
            require(s * s <= v && (s + 1) * (s + 1) > v, "Native square boundary failure");
        }
        require(!native_safe(polynomial("g1"), 10000000000000ULL, -256, 256),
                "Unsafe native domain accepted");
        require(native_safe(polynomial("g1"), 1000, -16, 16), "Small domain rejected");
        Database db;
        db.a = polynomial("synthetic");
        db.limit = 19;
        db.checksum = "test";
        for (U p : primes(19))
            db.data[p] = roots(db.a, p);
        Json config = parse(
            R"({"polynomial":"synthetic","m_min":"20","m_max":"3000","prime_limit":"19","factor_counts":[1,2,3,4,5],"k_min":"-2","k_max":"2"})");
        Domain d = domain(config, db);
        std::set<std::vector<U>> expected, got;
        std::vector<U> ps;
        for (const auto &[p, r] : db.data)
            if (!r.empty())
                ps.push_back(p);
        for (U mask = 1; mask < (U(1) << ps.size()); ++mask) {
            U m = 1;
            std::vector<U> f;
            for (size_t i = 0; i < ps.size(); ++i)
                if (mask & (U(1) << i)) {
                    m *= ps[i];
                    f.push_back(ps[i]);
                }
            if (f.size() <= 5 && m >= 20 && m <= 3000)
                expected.insert(f);
        }
        Generator gen(d, db, 0, 1);
        std::vector<U> f;
        while (gen.next(f))
            require(got.insert(f).second, "Duplicate product");
        require(got == expected, "Generator coverage mismatch");
        std::set<std::vector<U>> union_set;
        for (U shard = 0; shard < 3; ++shard) {
            Generator g(d, db, shard, 3);
            while (g.next(f)) {
                require(union_set.insert(f).second, "Shard overlap");
                Generator restored(d, db, shard, 3);
                restored.restore(g.state());
                auto saved = g.state();
                std::vector<U> a, b;
                bool aa = g.next(a), bb = restored.next(b);
                require(aa == bb && a == b, "Generator resume mismatch");
                g.restore(saved);
            }
        }
        require(union_set == expected, "Shard union mismatch");
        Big x = integer("-783954692511"), y = integer("1797526169071"),
            z = integer("-838276125548");
        require(y * z * (y + z) == eval(polynomial("regression"), x),
                "Regression witness mismatch");
        require((y + 1) * z * (y + z + 1) != eval(polynomial("regression"), x),
                "Perturbed witness accepted");
        std::cout << "All mathematical unit tests passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
