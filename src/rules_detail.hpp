#pragma once

// Helpers shared by the per-year rule tables (rules_2025.cpp, rules_2026.cpp).

#include "opentax/rules.hpp"

#include <initializer_list>

namespace ot::rules_detail {

inline Money D(long long dollars) { return Money::fromCents(dollars * 100); }
inline Decimal P(const char* percent) { return *Decimal::parse(percent); }

// Order: Single, MFJ, MFS, HoH, QSS.
inline ByStatus<Money> S(long long single, long long mfj, long long mfs, long long hoh, long long qss) {
    return {D(single), D(mfj), D(mfs), D(hoh), D(qss)};
}

// Bracket tops for the 10, 12, 22, 24, 32 and 35% rates; 37% applies above the last.
inline std::vector<Bracket> B(std::initializer_list<long long> tops) {
    static const int rates[] = {10, 12, 22, 24, 32, 35, 37};
    std::vector<Bracket> out;
    int i = 0;
    for (long long top : tops) out.push_back({D(top), rates[i++]});
    out.push_back({Money(), 37});
    return out;
}

Rules makeRules2025();
Rules makeRules2026();

}  // namespace ot::rules_detail
