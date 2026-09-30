#include "equation.hpp"
#include "search.hpp"
#include <iostream>
#include <random>
using namespace dio;
void require(bool ok, const char *message) {
    if (!ok) throw std::runtime_error(message);
}
int main() {
    try {
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
        std::cout << "Symmetric roots, both-sign finite differences, arithmetic boundaries and witness passed\n";
        return 0;
    } catch(const std::exception &e) {
        std::cerr << e.what() << '\n'; return 1;
    }
}
