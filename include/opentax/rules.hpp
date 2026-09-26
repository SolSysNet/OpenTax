#pragma once

// Tax-year constants. Every number the calculator uses lives here, with its source, so a
// new tax year is a new table rather than a code change.

#include "opentax/model.hpp"
#include "opentax/money.hpp"

#include <array>
#include <vector>

namespace ot {

// Indexed by FilingStatus: Single, MFJ, MFS, HoH, QSS.
template <class V>
using ByStatus = std::array<V, 5>;

template <class V>
const V& pick(const ByStatus<V>& table, FilingStatus s) {
    return table[static_cast<std::size_t>(s)];
}

struct Bracket {
    Money upTo;       // top of the bracket; the last bracket's value is ignored
    int ratePercent;
};

struct EicColumn {
    Decimal ratePercent;       // phase-in rate
    Money earnedAmount;        // earned income at which the maximum is reached
    Money maxCredit;           // whole dollars, as used to build the EIC Table
    Decimal phaseoutPercent;
    Money phaseoutStart;       // single, HoH, QSS (and MFS when allowed)
    Money phaseoutStartJoint;  // married filing jointly
};

struct SaverTier {
    ByStatus<Money> agiUpTo;
    int ratePercent;  // 50, 20, 10
};

struct Rules {
    int year;

    // Tax rate schedules (Rev. Proc. 2024-40; rates made permanent by P.L. 119-21).
    ByStatus<std::vector<Bracket>> brackets;

    // Standard deduction (P.L. 119-21 sec. 70102).
    ByStatus<Money> standardDeduction;
    Money additionalMarried;    // per box on line 12d: MFJ, MFS, QSS
    Money additionalUnmarried;  // single, HoH
    Money dependentMinimum;
    Money dependentEarnedAdd;

    // Capital gains rate thresholds (QDCG worksheet lines 6 and 13).
    ByStatus<Money> zeroRateTop;
    ByStatus<Money> fifteenRateTop;

    Money capitalLossLimit;       // 3,000
    Money capitalLossLimitMfs;    // 1,500

    // Social security.
    ByStatus<Money> ssBase;        // SS benefits worksheet line 8
    ByStatus<Money> ssAdjustedBase;  // line 10 (difference to the second threshold)

    // Self-employment tax (Schedule SE).
    Money ssWageBase;
    Decimal seEarningsPercent;   // 92.35
    Money seMinimum;             // 400
    Money maxSsWithholding;      // 6.2% of the wage base

    // Additional Medicare tax and net investment income tax thresholds.
    ByStatus<Money> medicareThreshold;
    ByStatus<Money> niitThreshold;

    // Child tax credit (Schedule 8812).
    Money ctcPerChild;
    Money odcPerDependent;
    Money actcPerChild;
    Money actcEarnedFloor;
    ByStatus<Money> ctcPhaseoutStart;

    // Earned income credit, by number of qualifying children (0..3).
    std::array<EicColumn, 4> eic;
    Money eicInvestmentLimit;

    // Itemized deductions (Schedule A).
    Decimal medicalFloorPercent;
    Money saltCap;               // before halving for MFS
    Money saltFloor;
    Money saltPhaseStart;
    Money saltPhaseStartMfs;
    Decimal saltPhasePercent;

    // Adjustments.
    Money educatorLimit;
    Money studentLoanLimit;
    ByStatus<Money> studentLoanPhaseStart;
    ByStatus<Money> studentLoanPhaseRange;
    Money iraLimit;
    Money iraCatchUp;

    // Schedule 1-A.
    Money tipsLimit;
    ByStatus<Money> overtimeLimit;
    ByStatus<Money> tipsPhaseStart;      // also overtime
    Money carLoanLimit;
    ByStatus<Money> carLoanPhaseStart;
    Money seniorDeduction;
    ByStatus<Money> seniorPhaseStart;
    Decimal seniorPhasePercent;

    // Education credits (Form 8863).
    ByStatus<Money> educationPhaseEnd;   // line 2 / line 13
    ByStatus<Money> educationPhaseRange; // line 5 / line 16

    // Child and dependent care (Form 2441).
    Money careLimitOne;
    Money careLimitTwo;
    Money careBenefitExclusion;
    Money careBenefitExclusionMfs;

    // Saver's credit (Form 8880).
    std::vector<SaverTier> saverTiers;
    Money saverContributionLimit;

    // QBI deduction (Form 8995).
    ByStatus<Money> qbiThreshold;

    // Alternative minimum tax (Form 6251).
    ByStatus<Money> amtExemption;
    ByStatus<Money> amtPhaseStart;
    ByStatus<Money> amt28Threshold;
    ByStatus<Money> amt28Subtract;

    // Simplified home office.
    Money homeOfficeRate;
    int homeOfficeMaxSqFt;
};

// Throws ot::Error for an unsupported year.
const Rules& rulesFor(int year);
bool isSupportedYear(int year);

}  // namespace ot
