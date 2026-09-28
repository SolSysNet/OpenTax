// Tax year 2025 constants.
//
// Sources (all checked against the 2025 IRS forms and instructions):
//   * Rev. Proc. 2024-40 (inflation adjustments), as amended by P.L. 119-21 (the 2025
//     reconciliation act) for the standard deduction, child tax credit, SALT cap and the
//     new Schedule 1-A deductions.
//   * 2025 Instructions for Form 1040: Tax Table, Tax Computation Worksheet, QDCG worksheet,
//     Social Security Benefits Worksheet, EIC worksheets, IRA and student loan worksheets.
//   * 2025 Schedules 1-A, 8812, A (and instructions), SE; Forms 2441, 6251, 8863, 8880,
//     8959, 8960, 8995.

#include "rules_detail.hpp"

namespace ot::rules_detail {

Rules makeRules2025() {
    Rules r{};
    r.year = 2025;

    const auto single = B({11925, 48475, 103350, 197300, 250525, 626350});
    const auto joint = B({23850, 96950, 206700, 394600, 501050, 751600});
    const auto separate = B({11925, 48475, 103350, 197300, 250525, 375800});
    const auto head = B({17000, 64850, 103350, 197300, 250500, 626350});
    r.brackets = {single, joint, separate, head, joint};

    r.standardDeduction = S(15750, 31500, 15750, 23625, 31500);
    r.additionalMarried = D(1600);
    r.additionalUnmarried = D(2000);
    r.dependentMinimum = D(1350);
    r.dependentEarnedAdd = D(450);

    r.zeroRateTop = S(48350, 96700, 48350, 64750, 96700);
    r.fifteenRateTop = S(533400, 600050, 300000, 566700, 600050);
    r.capitalLossLimit = D(3000);
    r.capitalLossLimitMfs = D(1500);

    r.ssBase = S(25000, 32000, 25000, 25000, 25000);  // MFS living with spouse: special case in code
    r.ssAdjustedBase = S(9000, 12000, 9000, 9000, 9000);

    r.ssWageBase = D(176100);
    r.seEarningsPercent = P("92.35");
    r.seMinimum = D(400);
    r.maxSsWithholding = Money::fromCents(1091820);  // $10,918.20

    r.medicareThreshold = S(200000, 250000, 125000, 200000, 200000);
    r.niitThreshold = S(200000, 250000, 125000, 200000, 250000);

    r.ctcPerChild = D(2200);
    r.odcPerDependent = D(500);
    r.actcPerChild = D(1700);
    r.actcEarnedFloor = D(2500);
    r.ctcPhaseoutStart = S(200000, 400000, 200000, 200000, 200000);

    r.eic = {{
        {P("7.65"), D(8490), D(649), P("7.65"), D(10620), D(17730)},
        {P("34"), D(12730), D(4328), P("15.98"), D(23350), D(30470)},
        {P("40"), D(17880), D(7152), P("21.06"), D(23350), D(30470)},
        {P("45"), D(17880), D(8046), P("21.06"), D(23350), D(30470)},
    }};
    r.eicInvestmentLimit = D(11950);

    r.medicalFloorPercent = P("7.5");
    r.saltCap = D(40000);
    r.saltFloor = D(10000);
    r.saltPhaseStart = D(500000);
    r.saltPhaseStartMfs = D(250000);
    r.saltPhasePercent = P("30");

    r.educatorLimit = D(300);
    r.studentLoanLimit = D(2500);
    r.studentLoanPhaseStart = S(85000, 170000, 0, 85000, 85000);
    r.studentLoanPhaseRange = S(15000, 30000, 0, 15000, 15000);
    r.iraLimit = D(7000);
    r.iraCatchUp = D(1000);
    r.iraPhaseEnd = S(89000, 146000, 10000, 89000, 146000);
    r.iraPhaseEndSpouseCovered = D(246000);

    r.tipsLimit = D(25000);
    r.overtimeLimit = S(12500, 25000, 12500, 12500, 12500);
    r.tipsPhaseStart = S(150000, 300000, 150000, 150000, 150000);
    r.carLoanLimit = D(10000);
    r.carLoanPhaseStart = S(100000, 200000, 100000, 100000, 100000);
    r.seniorDeduction = D(6000);
    r.seniorPhaseStart = S(75000, 150000, 75000, 75000, 75000);
    r.seniorPhasePercent = P("6");

    r.educationPhaseEnd = S(90000, 180000, 0, 90000, 90000);
    r.educationPhaseRange = S(10000, 20000, 0, 10000, 10000);

    r.careLimitOne = D(3000);
    r.careLimitTwo = D(6000);
    r.careBenefitExclusion = D(5000);
    r.careBenefitExclusionMfs = D(2500);
    r.careTopRate = 35;
    r.careMidRate = 20;
    r.careSecondStart = S(0, 0, 0, 0, 0);
    r.careSecondStep = S(2000, 2000, 2000, 2000, 2000);

    // Form 8880 line 9 table. Order: Single, MFJ, MFS, HoH, QSS.
    r.saverTiers = {
        {S(23750, 47500, 23750, 35625, 23750), 50},
        {S(25500, 51000, 25500, 38250, 25500), 20},
        {S(39500, 79000, 39500, 59250, 39500), 10},
    };
    r.saverContributionLimit = D(2000);

    r.qbiThreshold = S(197300, 394600, 197300, 197300, 197300);
    r.qbiMinimumDeduction = D(0);
    r.qbiMinimumActive = D(0);

    r.amtExemption = S(88100, 137000, 68500, 88100, 137000);
    r.amtPhaseStart = S(626350, 1252700, 626350, 626350, 1252700);
    r.amt28Threshold = S(239100, 239100, 119550, 239100, 239100);
    r.amt28Subtract = S(4782, 4782, 2391, 4782, 4782);
    r.amtPhasePercent = P("25");

    r.nonItemizerCharity = S(0, 0, 0, 0, 0);
    r.charityFloorPercent = P("0");
    r.itemizedLimitation = false;
    r.mortgageInsurance = false;
    r.publicBenefitSchedule = false;
    r.formsYear = 2025;

    r.homeOfficeRate = D(5);
    r.homeOfficeMaxSqFt = 300;
    return r;
}

}  // namespace ot::rules_detail
