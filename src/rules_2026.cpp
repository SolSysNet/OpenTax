// Tax year 2026 constants.
//
// Sources:
//   * Rev. Proc. 2025-32 (2026 inflation adjustments after P.L. 119-21): rate tables, capital
//     gain thresholds, child tax credit, EIC, AMT, educator expenses, standard deduction, QBI,
//     student loan interest.
//   * Notice 2025-67 (2026 retirement amounts): IRA limits and phase-outs, saver's credit.
//   * 2026 draft forms (IRS.gov/DraftForms): Form 1040, Schedules 1, 1-A, 2, 3, 3-A, A, SE;
//     Forms 2441 (and its 2026 instructions), 6251, 8880, 8959, 8995 (and instructions).
//   * P.L. 119-21 for amounts that are not indexed: Schedule 1-A limits, the non-itemizer
//     charitable deduction (sec. 170(p)), the 0.5% charitable floor (sec. 170(b)(1)(I)), the
//     itemized deduction limitation (sec. 68), and the AMT exemption phase-out rate (50%).
//
// The 2026 Tax Table and EIC Table aren't published yet. They are built the same way every
// year (tax at the midpoint of each row), which is how incomeTax() and eicTable() work.

#include "rules_detail.hpp"

namespace ot::rules_detail {

Rules makeRules2026() {
    Rules r{};
    r.year = 2026;

    const auto single = B({12400, 50400, 105700, 201775, 256225, 640600});
    const auto joint = B({24800, 100800, 211400, 403550, 512450, 768700});
    const auto separate = B({12400, 50400, 105700, 201775, 256225, 384350});
    const auto head = B({17700, 67450, 105700, 201750, 256200, 640600});
    r.brackets = {single, joint, separate, head, joint};

    r.standardDeduction = S(16100, 32200, 16100, 24150, 32200);
    r.additionalMarried = D(1650);
    r.additionalUnmarried = D(2050);
    r.dependentMinimum = D(1350);
    r.dependentEarnedAdd = D(450);

    r.zeroRateTop = S(49450, 98900, 49450, 66200, 98900);
    r.fifteenRateTop = S(545500, 613700, 306850, 579600, 613700);
    r.capitalLossLimit = D(3000);
    r.capitalLossLimitMfs = D(1500);

    r.ssBase = S(25000, 32000, 25000, 25000, 25000);
    r.ssAdjustedBase = S(9000, 12000, 9000, 9000, 9000);

    r.ssWageBase = D(184500);
    r.seEarningsPercent = P("92.35");
    r.seMinimum = D(400);
    r.maxSsWithholding = D(11439);  // 6.2% of $184,500

    r.medicareThreshold = S(200000, 250000, 125000, 200000, 200000);
    r.niitThreshold = S(200000, 250000, 125000, 200000, 250000);

    r.ctcPerChild = D(2200);
    r.odcPerDependent = D(500);
    r.actcPerChild = D(1700);
    r.actcEarnedFloor = D(2500);
    r.ctcPhaseoutStart = S(200000, 400000, 200000, 200000, 200000);

    r.eic = {{
        {P("7.65"), D(8680), D(664), P("7.65"), D(10860), D(18140)},
        {P("34"), D(13020), D(4427), P("15.98"), D(23890), D(31160)},
        {P("40"), D(18290), D(7316), P("21.06"), D(23890), D(31160)},
        {P("45"), D(18290), D(8231), P("21.06"), D(23890), D(31160)},
    }};
    r.eicInvestmentLimit = D(12200);

    r.medicalFloorPercent = P("7.5");
    r.saltCap = D(40400);
    r.saltFloor = D(10000);
    r.saltPhaseStart = D(505000);
    r.saltPhaseStartMfs = D(252500);
    r.saltPhasePercent = P("30");

    r.educatorLimit = D(350);
    r.studentLoanLimit = D(2500);
    r.studentLoanPhaseStart = S(85000, 175000, 0, 85000, 85000);
    r.studentLoanPhaseRange = S(15000, 30000, 0, 15000, 15000);
    r.iraLimit = D(7500);
    r.iraCatchUp = D(1100);
    r.iraPhaseEnd = S(91000, 149000, 10000, 91000, 149000);
    r.iraPhaseEndSpouseCovered = D(252000);

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
    r.careBenefitExclusion = D(7500);
    r.careBenefitExclusionMfs = D(3750);
    r.careTopRate = 50;
    r.careMidRate = 35;
    r.careSecondStart = S(75000, 150000, 75000, 75000, 75000);
    r.careSecondStep = S(2000, 4000, 2000, 2000, 2000);

    // Form 8880 line 9 table. Order: Single, MFJ, MFS, HoH, QSS.
    r.saverTiers = {
        {S(24250, 48500, 24250, 36375, 24250), 50},
        {S(26250, 52500, 26250, 39375, 26250), 20},
        {S(40250, 80500, 40250, 60375, 40250), 10},
    };
    r.saverContributionLimit = D(2000);

    r.qbiThreshold = S(201750, 403500, 201775, 201750, 201750);
    r.qbiMinimumDeduction = D(400);
    r.qbiMinimumActive = D(1000);

    r.amtExemption = S(90100, 140200, 70100, 90100, 140200);
    r.amtPhaseStart = S(500000, 1000000, 500000, 500000, 1000000);
    r.amt28Threshold = S(244500, 244500, 122250, 244500, 244500);
    r.amt28Subtract = S(4890, 4890, 2445, 4890, 4890);
    r.amtPhasePercent = P("50");

    r.nonItemizerCharity = S(1000, 2000, 1000, 1000, 1000);
    r.charityFloorPercent = P("0.5");
    r.itemizedLimitation = true;
    r.mortgageInsurance = true;
    r.publicBenefitSchedule = true;
    r.formsYear = 2026;

    r.homeOfficeRate = D(5);
    r.homeOfficeMaxSqFt = 300;
    return r;
}

}  // namespace ot::rules_detail
