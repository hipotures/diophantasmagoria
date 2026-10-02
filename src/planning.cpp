#include "search.hpp"
#include <algorithm>

namespace dio {
Json plan(const Domain &d, const Database &db, double seconds, U max_prefixes) {
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    const auto deadline = seconds > 0
        ? start + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(std::min(seconds,1e9)))
        : Clock::time_point::max();
    Generator generator(d,db,0,1);
    struct Entry { U prime, modulus, exponent, roots, next; };
    std::vector<Entry> entries;
    if (d.prime_powers) {
        for (size_t j = 0; j < generator.power_options.size(); ++j) {
            U q = generator.power_options[j];
            const auto &factor = generator.powers.at(q);
            entries.push_back({factor.prime,q,factor.exponent,factor.roots.size(),generator.next_distinct[j]});
        }
    } else {
        for (U p : generator.eligible)
            entries.push_back({p,p,1,db.data.at(p).size(),entries.size()+1});
    }
    // Each column has increasing prime and modulus, and one fixed root count.
    // Binary search can sum its whole feasible last-factor interval exactly.
    using Key = std::pair<U,U>; // exponent, number of roots
    std::map<Key,std::vector<std::pair<U,U>>> columns;
    for (const auto &entry : entries)
        columns[{entry.exponent,entry.roots}].emplace_back(entry.prime,entry.modulus);
    struct Counts { Big moduli = 0, roots = 0, tasks = 0; };
    auto root_product = [](U a,U b) {
        if (b && a > 1000000/b)
            throw std::runtime_error("Planned CRT exceeds 1000000 roots per modulus");
        return a*b;
    };
    std::map<std::pair<U,U>,Counts> counts; // distinct primes, powered primes
    Big k_values = 0, k_tiles = 0;
    for (auto [lo,hi] : d.ranges) {
        Big length = Big(hi)-lo+1;
        k_values += length;
        k_tiles += (length+63)/64;
    }
    U prefixes = 0;
    bool complete = true;
    auto add = [&](U f, U powered, U n, U root_count) {
        if (root_count > 1000000)
            throw std::runtime_error("Planned CRT exceeds 1000000 roots per modulus");
        auto &c = counts[{f,powered}];
        c.moduli += n;
        c.roots += Big(n)*root_count;
        c.tasks += Big(n)*((root_count+15)/16)*k_tiles;
    };
    auto allowed = [&](U powered) {
        return std::binary_search(d.power_factor_counts.begin(),d.power_factor_counts.end(),powered);
    };
    std::function<void(U,U,U,U,U,U,U)> visit;
    visit = [&](U f, U remaining, U begin, U m, U root_count, U powered, U previous_prime) {
        if (!complete) return;
        if (stopped || (max_prefixes && prefixes >= max_prefixes) ||
            ((prefixes & 1023U) == 0 && Clock::now() >= deadline)) {
            complete = false;
            return;
        }
        ++prefixes;
        if (remaining == 1) {
            U low = d.m_min/m + (d.m_min % m != 0), high = d.m_max/m;
            for (const auto &[key,column] : columns) {
                auto [exponent,rs] = key;
                U count_powered = powered + (exponent > 1);
                if (!allowed(count_powered)) continue;
                auto first_prime = std::upper_bound(column.begin(),column.end(),previous_prime,
                    [](U p, const auto &item) { return p < item.first; });
                auto first = std::lower_bound(first_prime,column.end(),low,
                    [](const auto &item,U q) { return item.second < q; });
                auto last = std::upper_bound(first,column.end(),high,
                    [](U q,const auto &item) { return q < item.second; });
                U n = static_cast<U>(last-first);
                if (n) add(f,count_powered,n,root_product(root_count,rs));
            }
            return;
        }
        for (U j = begin; j < entries.size() && complete; ++j) {
            const auto &entry = entries[j];
            if (m > d.m_max/entry.modulus) continue;
            U next_m = m*entry.modulus, next_powered = powered + (entry.exponent > 1);
            bool viable = false;
            for (U target : d.power_factor_counts)
                if (next_powered <= target && target <= next_powered+remaining-1) viable = true;
            if (!viable) continue;
            U minimum = next_m, index = entry.next;
            for (U r = 1; r < remaining; ++r) {
                if (index >= entries.size() || minimum > d.m_max/entries[index].modulus) {
                    viable = false; break;
                }
                minimum *= entries[index].modulus;
                index = entries[index].next;
            }
            if (!viable) continue;
            visit(f,remaining-1,entry.next,next_m,root_product(root_count,entry.roots),next_powered,entry.prime);
        }
    };
    if (d.moduli.empty()) {
        for (U f : d.factor_counts) visit(f,f,0,1,1,0,0);
    } else {
        for (const auto &qs : d.moduli) {
            if (stopped || Clock::now() >= deadline || (max_prefixes && prefixes >= max_prefixes)) {
                complete = false; break;
            }
            ++prefixes;
            U root_count = 1, powered = 0;
            for (U q : qs) {
                if (d.prime_powers) {
                    const auto &factor = generator.powers.at(q);
                    root_count = root_product(root_count,factor.roots.size());
                    powered += factor.exponent > 1;
                } else root_count = root_product(root_count,db.data.at(q).size());
            }
            if (root_count && !(d.exclude_even && qs.front() == 2))
                add(qs.size(),powered,1,root_count);
        }
    }
    Counts total;
    Json strata;
    for (const auto &[key,c] : counts) {
        Json row;
        row.put("distinct_primes",key.first);
        row.put("powered_primes",key.second);
        row.put("moduli",decimal(c.moduli));
        row.put("roots",decimal(c.roots));
        row.put("candidates",decimal(Big(2*k_values*c.roots)));
        row.put("tasks",decimal(c.tasks));
        strata.push_back({"",row});
        total.moduli += c.moduli;
        total.roots += c.roots;
        total.tasks += c.tasks;
    }
    Json report;
    report.put("schema","dio-plan-v1");
    report.put("domain",d.fingerprint);
    report.add_child("domain_definition",d.canonical);
    report.put("complete",complete ? "true" : "false");
    report.put("count_scope",complete ? "exact entire unsharded domain" : "lower bound from counted prefix intervals");
    report.put("moduli",decimal(total.moduli));
    report.put("roots",decimal(total.roots));
    report.put("candidates",decimal(Big(2*k_values*total.roots)));
    report.put("tasks",decimal(total.tasks));
    report.put("prefixes",prefixes);
    report.add_child("strata",strata);
    report.put("plan_seconds",std::chrono::duration<double>(Clock::now()-start).count());
    report.put("build_commit",BUILD_REV);
    report.put("build_source",BUILD_SOURCE);
    report.put("meaning","Counts arithmetic work; no candidates or witnesses evaluated");
    return report;
}
} // namespace dio
