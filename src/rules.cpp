#include "opentax/rules.hpp"

#include "rules_detail.hpp"

#include <string>

namespace ot {

bool isSupportedYear(int year) { return year == 2025 || year == 2026; }

const Rules& rulesFor(int year) {
    static const Rules r2025 = rules_detail::makeRules2025();
    static const Rules r2026 = rules_detail::makeRules2026();
    if (year == 2025) return r2025;
    if (year == 2026) return r2026;
    throw Error("tax year " + std::to_string(year) + " is not supported (this version supports 2025 and 2026)");
}

}  // namespace ot
