#include "equation.hpp"
#include "search.hpp"
#include "factor_sieve_prototype.hpp"
#include <iostream>
#include <random>
using namespace dio;
void require(bool ok, const char *message) {
    if (!ok) throw std::runtime_error(message);
}
int main() {
    try {
        std::vector<Poly> lifting_polys = {
            polynomial("symmetric"), {4,4,0,26}, {0,0,1,0}, {-5,0,1,0},
            {-1,3,-3,1}, {0,0,0,0}, {2,2,2,2}, {9,0,1,0}, {0,0,0,1}
        };
        std::mt19937_64 rng(6);
        for (int j=0;j<12;++j) {
            Poly poly;
            for (auto &c : poly) c = static_cast<int>(rng()%41)-20;
            lifting_polys.push_back(poly);
        }
        U lifting_cases = 0;
        for (const auto &poly : lifting_polys)
            for (U p : primes(31))
                for (U e=1,q=p; q<=4096; ++e,q*=p) {
                    std::vector<U> want;
                    for (U r=0;r<q;++r) if (eval(poly,Big(r))%q==0) want.push_back(r);
                    require(hensel_roots(poly,p,e,roots(poly,p))==want,
                            "Incomplete simple/singular Hensel lifts");
                    ++lifting_cases;
                }
        require(hensel_roots(Poly{-5,0,1,0},5,2,{0}).empty(), "Singular root must die");
        require(hensel_roots(Poly{0,0,1,0},5,2,{0})==std::vector<U>({0,5,10,15,20}),
                "All singular branches must survive");
        require(hensel_roots(Poly{-7,1,0,0},2,62,{1})==std::vector<U>{7},
                "High-exponent lift mismatch");
        require(hensel_roots(polynomial("symmetric"),479,2,{299}).empty(),
                "Actual H singular root was assumed simple");
        for (const auto &poly : lifting_polys) {
            std::vector<U> qs = {8,9,25};
            auto rs = std::vector<std::vector<U>>{
                hensel_roots(poly,2,3,roots(poly,2)),
                hensel_roots(poly,3,2,roots(poly,3)),
                hensel_roots(poly,5,2,roots(poly,5))};
            std::vector<U> want;
            for (U r=0;r<1800;++r) if (eval(poly,Big(r))%1800==0) want.push_back(r);
            require(crt(qs,rs)==want,"CRT over prime powers mismatch");
        }
        bool rejected = false;
        try { crt({4,8},{{0},{0}}); } catch (const std::runtime_error &) { rejected=true; }
        require(rejected,"Noncoprime prime powers accepted");
        rejected = false;
        try { prime_power(2,63); } catch (const std::runtime_error &) { rejected=true; }
        require(rejected,"Overflowing prime power accepted");
        // Independently compare the local quotient/discriminant congruence with
        // exact evaluation. The same identity holds modulo each p^e dividing m.
        for (int constant : {1,3,4}) {
            Poly poly = {constant,4,0,26};
            Database db;
            db.a = poly;
            for (U p : primes(31)) db.data[p] = roots(poly,p);
            FactorSieve filter(db,31);
            U positive_squares=0;
            for (U m=2;m<=500;++m) {
                U remainder=m;
                std::vector<U> factors;
                for (U p : primes(31)) {
                    if (remainder%p) continue;
                    factors.push_back(p);
                    do { remainder/=p; } while(remainder%p==0);
                }
                if (remainder!=1) continue;
                for (U r=0;r<m;++r) {
                    if (eval(poly,Big(r))%m) continue;
                    auto masks=filter.masks(poly,m,r,factors,-4,4);
                    for (int64_t k=-4;k<=4;++k) {
                        Big a=Big(r)+Big(k)*m;
                        for (int sign : {1,-1}) {
                            Big d=Big(sign)*m;
                            // Direct product reconstruction, independent of the
                            // affine formula and the finite-difference search.
                            Big b=d*d-9*a*d+27*a*a+2-eval(poly,a)/d;
                            Big delta=a*a-4*b;
                            if (delta<0) continue;
                            Big s=square_root(delta);
                            if (s*s!=delta || (a-s)%2) continue;
                            ++positive_squares;
                            require((masks[sign==1 ? 0 : 1] & (U(1)<<static_cast<U>(k+4)))!=0,
                                    "Exhaustive tiny-domain A/B factor sieve false negative");
                        }
                    }
                }
            }
            if (constant!=1) require(positive_squares>0,"Factor sieve lacks positive controls");
            for (int trial=0;trial<100;++trial) {
                std::vector<U> ps,qs;
                std::vector<std::vector<U>> lists;
                U m=1;
                for (U p : primes(31)) {
                    if (rng()%3) continue;
                    U e=1+rng()%3, q=prime_power(p,e);
                    auto rs=hensel_roots(poly,p,e,db.data.at(p));
                    if (rs.empty() || m>1000000000/q) continue;
                    ps.push_back(p); qs.push_back(q); lists.push_back(rs); m*=q;
                    if (ps.size()==3) break;
                }
                if (ps.empty()) continue;
                auto rs=crt(qs,lists);
                for (U r : rs) {
                    int64_t lo=static_cast<int64_t>(rng()%513)-256;
                    auto masks=filter.masks(poly,m,r,ps,lo,lo+63);
                    Big quotient=eval(poly,Big(r))/m, derivative=78*Big(r)*r+4;
                    for (int64_t k=lo;k<=lo+63;++k) {
                        Big a=Big(r)+Big(k)*m;
                        for (int sign : {1,-1}) {
                            Big d=Big(sign)*m;
                            Big exact=4*eval(poly,a)/d-4*d*d+36*a*d-107*a*a-8;
                            Big local=sign*4*(quotient+Big(k)*derivative)-107*Big(r)*r-8;
                            bool admissible=true;
                            for (size_t j=0;j<ps.size();++j) {
                                require((exact-local)%qs[j]==0,"Prime-power local congruence mismatch");
                                if (ps[j]<=13) continue;
                                Big v=exact%ps[j]; if(v<0) v+=ps[j];
                                bool square=false;
                                for (U s=0;s<ps[j];++s) if(Big(s*s%ps[j])==v) square=true;
                                admissible &= square;
                            }
                            bool accepted=(masks[sign==1 ? 0 : 1] & (U(1)<<static_cast<U>(k-lo)))!=0;
                            require(accepted==admissible,"Factor mask differs from exact local residues");
                            if (exact>=0) {
                                Big s=square_root(exact);
                                if (s*s==exact) require(accepted,"Factor sieve false negative");
                            }
                        }
                    }
                }
            }
        }
        for (int constant : {1, 3, 4, -3}) {
            Poly a = {constant, 4, 0, 26};
            for (U p : primes(1500)) {
                std::vector<U> expected;
                for (U r=0; r<p; ++r)
                    if (eval(a, Big(r)) % p == 0) expected.push_back(r);
                require(roots(a,p) == expected, "Root coverage/degree drop mismatch");
            }
            std::array<__int128_t,4> native = {constant,4,0,26};
            for (U p : {5, 7, 13, 79, 181})
                for (U r : roots(a,p))
                    for (int64_t lo : {-257, -65, -1, 0, 63, 256}) {
                        CandidateSequence<Big,true> wide(a,p,r,lo);
                        CandidateSequence<__int128_t,true> fast(native,p,r,lo);
                        for (int64_t k=lo; k<lo+130; ++k) {
                            Big coordinate=Big(r)+Big(k)*p;
                            for (int sign : {1,-1}) {
                                Big d=Big(sign)*p;
                                Big b=d*d-9*coordinate*d+27*coordinate*coordinate+2-eval(a,coordinate)/d;
                                Big want=coordinate*coordinate-4*b;
                                require(wide.delta(sign)==want && Big(fast.delta(sign))==want,
                                        "Signed symmetric finite differences mismatch");
                            }
                            if (k<lo+129) { wide.next(); fast.next(); }
                        }
                    }
        }
        require(roots(polynomial("symmetric"),2).empty(), "H is odd");
        require(roots(polynomial("symmetric"),13)==std::vector<U>{3}, "Degree drop at 13");
        require(!symmetric_coefficients(polynomial("g1")), "Wrong backend accepted");
        // Exercise native/big equivalence close to the conservative domain bound.
        Poly a = {0,4,0,26}; // r=0 is a root for every m, including composite m.
        std::array<__int128_t,4> aa={0,4,0,26};
        U low=2,high=INT64_MAX;
        while(low<high) {
            U mid=low+(high-low+1)/2;
            if(symmetric_native_safe(a,mid,-256,256)) low=mid;
            else high=mid-1;
        }
        require(symmetric_native_safe(a,low,-256,256) &&
                !symmetric_native_safe(a,low+1,-256,256), "Native boundary invalid");
        for(int64_t start : {-256, 0, 253}) {
            CandidateSequence<Big,true> wide(a,low,0,start);
            CandidateSequence<__int128_t,true> fast(aa,low,0,start);
            for(int j=0;j<4;++j) {
                for(int sign : {1,-1}) require(wide.delta(sign)==Big(fast.delta(sign)), "Boundary overflow");
                if(j<3) {wide.next();fast.next();}
            }
        }
        Big x=integer("723809206820"), y=integer("-631758385864"), z=integer("-245500254229");
        require(symmetric_residual(x,y,z,Big(1))==0, "Known witness rejected");
        require(symmetric_residual(Big(x+1),y,z,Big(1))!=0, "Perturbed witness accepted");
        require(!symmetric_native_safe(polynomial("symmetric"),10000000000000ULL,-64,64), "Unsafe native accepted");
        require(symmetric_native_safe(polynomial("symmetric"),500,-4,4), "Tiny native rejected");
        std::cout << "Symmetric roots, " << lifting_cases << " exhaustive Hensel cases, prime-power CRT, both-sign finite differences, arithmetic boundaries and witness passed\n";
        return 0;
    } catch(const std::exception &e) {
        std::cerr << e.what() << '\n'; return 1;
    }
}
