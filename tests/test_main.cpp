// Self-contained test runner (no external framework needed).
//
// Expected values are worked by hand from the 2025 IRS forms and instructions, or read
// from the IRS tables themselves (tests/data/tax_table_2025.txt is the complete 2025 Tax
// Table extracted from the Form 1040 instructions).

#include "opentax/calc.hpp"
#include "opentax/cli.hpp"
#include "opentax/crypto.hpp"
#include "opentax/model.hpp"
#include "opentax/pdf.hpp"
#include "opentax/report.hpp"
#include "opentax/return_pdf.hpp"

#include "monocypher.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace ot;

namespace {

int g_checks = 0;
int g_failures = 0;

struct TestCase {
    const char* name;
    void (*fn)();
};

std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

#define TEST(name)                                 \
    void name();                                   \
    const Registrar registrar_##name(#name, name); \
    void name()

#define CHECK(cond)                                                                    \
    do {                                                                               \
        ++g_checks;                                                                    \
        if (!(cond)) {                                                                 \
            ++g_failures;                                                              \
            std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK failed: " #cond "\n"; \
        }                                                                              \
    } while (0)

#define CHECK_EQ(a, b)                                                                                  \
    do {                                                                                                \
        ++g_checks;                                                                                     \
        const auto va = (a);                                                                            \
        const auto vb = (b);                                                                            \
        if (!(va == vb)) {                                                                              \
            ++g_failures;                                                                               \
            std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK_EQ failed: " #a " == " #b "  (" << va \
                      << " vs " << vb << ")\n";                                                         \
        }                                                                                               \
    } while (0)

#define CHECK_THROWS(expr)                                                                        \
    do {                                                                                          \
        ++g_checks;                                                                               \
        bool threw = false;                                                                       \
        try {                                                                                     \
            expr;                                                                                 \
        } catch (const ot::Error&) {                                                              \
            threw = true;                                                                         \
        }                                                                                         \
        if (!threw) {                                                                             \
            ++g_failures;                                                                         \
            std::cerr << __FILE__ << ":" << __LINE__ << ": expected ot::Error from: " #expr "\n"; \
        }                                                                                         \
    } while (0)

Money M(const char* s) { return *Money::parse(s); }
Date D(const char* s) { return *Date::parse(s); }
const Rules& R() { return rulesFor(2025); }

W2 w2(const char* wages, const char* withheld = "0", Owner owner = Owner::Taxpayer) {
    W2 w;
    w.owner = owner;
    w.employer = "Employer";
    w.wages = M(wages);
    w.federalWithheld = M(withheld);
    w.ssWages = w.wages;
    w.medicareWages = w.wages;
    w.ssWithheld = percentOf(w.wages, *Decimal::parse("6.2"));
    w.medicareWithheld = percentOf(w.wages, *Decimal::parse("1.45"));
    return w;
}

TaxReturn single(const char* wages, const char* withheld = "0") {
    TaxReturn r;
    r.taxpayer.first = "Pat";
    r.taxpayer.last = "Taxpayer";
    r.taxpayer.birthDate = D("1985-06-15");
    if (Money::parse(wages)->cents() != 0) r.w2s.push_back(w2(wages, withheld));
    return r;
}

Dependent child(const char* name, const char* dob) {
    Dependent d;
    d.first = name;
    d.last = "Taxpayer";
    d.birthDate = D(dob);
    return d;
}

bool hasForm(const Result& r, const char* id) { return r.form(id) != nullptr; }

// ------------------------------------------------------------ tax table

TEST(tax_table_matches_every_irs_row) {
    std::ifstream in(std::string(OPENTAX_TEST_DATA) + "/tax_table_2025.txt");
    CHECK(in.good());
    std::string line;
    int rows = 0;
    const FilingStatus statuses[] = {FilingStatus::Single, FilingStatus::MarriedJoint, FilingStatus::MarriedSeparate,
                                     FilingStatus::HeadOfHousehold};
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        long long lo, hi, tax[4];
        ss >> lo >> hi >> tax[0] >> tax[1] >> tax[2] >> tax[3];
        ++rows;
        for (int i = 0; i < 4; ++i) {
            // Check the bottom of the row, a cents value inside it, and the top.
            for (long long cents : {lo * 100, lo * 100 + 37, hi * 100 - 1}) {
                const Money got = incomeTax(Money::fromCents(cents), statuses[i], R());
                if (got != Money::fromCents(tax[i] * 100)) {
                    ++g_failures;
                    std::cerr << "tax table mismatch at " << cents / 100.0 << " status " << i << ": got " << got
                              << ", IRS " << tax[i] << "\n";
                }
                ++g_checks;
            }
        }
        // Qualifying surviving spouse uses the married filing jointly column.
        CHECK_EQ(incomeTax(Money::fromCents(lo * 100), FilingStatus::QualifyingSurvivingSpouse, R()),
                 Money::fromCents(tax[1] * 100));
    }
    CHECK(rows > 2000);
}

TEST(tax_computation_worksheet) {
    // Worksheet rows: taxable x rate - subtraction amount.
    CHECK_EQ(incomeTax(M("100000"), FilingStatus::Single, R()), M("16914.00"));        // 22000 - 5086
    CHECK_EQ(incomeTax(M("150000"), FilingStatus::Single, R()), M("28847.00"));        // 36000 - 7153
    CHECK_EQ(incomeTax(M("700000"), FilingStatus::Single, R()), M("216020.25"));       // 259000 - 42979.75
    CHECK_EQ(incomeTax(M("500000"), FilingStatus::MarriedJoint, R()), M("114126.00"));  // 160000 - 45874
    CHECK_EQ(incomeTax(M("800000"), FilingStatus::MarriedJoint, R()), M("220062.50"));  // 296000 - 75937.50
    CHECK_EQ(incomeTax(M("400000"), FilingStatus::MarriedSeparate, R()), M("110031.25"));  // 148000 - 37968.75
    CHECK_EQ(incomeTax(M("700000"), FilingStatus::HeadOfHousehold, R()), M("214282.00"));  // 259000 - 44718
    CHECK_EQ(incomeTax(M("123456.78"), FilingStatus::HeadOfHousehold, R()), M("20737.63"));  // 29629.6272 - 8892
    CHECK_EQ(incomeTax(M("0"), FilingStatus::Single, R()), Money());
    CHECK_EQ(incomeTax(M("4.99"), FilingStatus::Single, R()), Money());
}

TEST(eic_table_rows) {
    // Rows read from the 2025 EIC Table: {amount, single 0/1/2/3, joint 0/1/2/3}.
    struct Row {
        const char* amount;
        int single[4];
        int joint[4];
    };
    const Row rows[] = {
        {"1", {2, 9, 10, 11}, {2, 9, 10, 11}},
        {"2825", {216, 961, 1130, 1271}, {216, 961, 1130, 1271}},
        {"15000", {312, 4328, 6010, 6761}, {649, 4328, 6010, 6761}},
        {"20000", {0, 4328, 7152, 8046}, {473, 4328, 7152, 8046}},
        {"45049", {0, 864, 2587, 3481}, {0, 2002, 4087, 4981}},
        {"48200", {0, 353, 1913, 2807}, {0, 1491, 3413, 4307}},
    };
    for (const auto& row : rows) {
        for (int k = 0; k < 4; ++k) {
            CHECK_EQ(eicTable(M(row.amount), k, false, R()), Money::fromCents(row.single[k] * 100LL));
            CHECK_EQ(eicTable(M(row.amount), k, true, R()), Money::fromCents(row.joint[k] * 100LL));
        }
    }
    CHECK_EQ(eicTable(M("0.50"), 1, false, R()), Money());
    CHECK_EQ(eicTable(M("70000"), 3, true, R()), Money());
}

TEST(ages) {
    CHECK_EQ(irsAgeAtEndOfYear(D("1961-01-01"), 2025), 65);  // born before Jan 2, 1961
    CHECK_EQ(irsAgeAtEndOfYear(D("1961-01-02"), 2025), 64);
    CHECK_EQ(irsAgeAtEndOfYear(D("1960-12-31"), 2025), 65);
    CHECK_EQ(ageAtEndOfYear(D("2009-01-01"), 2025), 16);
    CHECK_EQ(ageAtEndOfYear(D("2008-12-31"), 2025), 17);
}

// ------------------------------------------------------------- returns

TEST(simple_single_w2) {
    const Result r = calculate(single("60000", "6000"));
    CHECK_EQ(r.summary.agi, M("60000"));
    CHECK_EQ(r.summary.deduction, M("15750"));
    CHECK_EQ(r.summary.taxableIncome, M("44250"));
    // Tax Table row 44,250-44,300: 10% of 11,925 + 12% of (44,275 - 11,925) = 5,074.50 -> 5,075.
    CHECK_EQ(r.summary.incomeTax, M("5075"));
    CHECK_EQ(r.summary.totalTax, M("5075"));
    CHECK_EQ(r.summary.refund, M("925"));
    CHECK_EQ(r.summary.owed, Money());
    CHECK(!r.hasErrors());
    CHECK(hasForm(r, "1040"));
    CHECK(!hasForm(r, "Schedule 1"));
    CHECK_EQ(r.forms.front().id, std::string("1040"));
    // Lines come out in form order.
    const auto& lines = r.forms.front().lines;
    CHECK_EQ(lines.front().number, std::string("1a"));
    CHECK_EQ(lines.back().number, std::string("37"));
}

TEST(married_two_children) {
    TaxReturn t;
    t.info.status = FilingStatus::MarriedJoint;
    t.taxpayer.first = "Alex";
    t.taxpayer.birthDate = D("1988-02-01");
    t.spouse.first = "Sam";
    t.spouse.birthDate = D("1989-03-01");
    t.w2s.push_back(w2("60000", "3000"));
    t.w2s.push_back(w2("30000", "1500", Owner::Spouse));
    t.dependents.push_back(child("Kid", "2015-05-05"));
    t.dependents.push_back(child("Tot", "2020-07-07"));
    const Result r = calculate(t);
    CHECK_EQ(r.summary.taxableIncome, M("58500"));
    CHECK_EQ(r.summary.incomeTax, M("6546"));  // 2,385 + 12% of (58,525 - 23,850)
    CHECK_EQ(r.line("Schedule 8812", "5"), M("4400"));
    CHECK_EQ(r.line("1040", "19"), M("4400"));
    CHECK_EQ(r.summary.totalTax, M("2146"));
    CHECK_EQ(r.line("1040", "27a"), Money());  // AGI too high for EIC
    CHECK_EQ(r.summary.refund, M("2354"));
}

TEST(single_parent_low_income_refundable_credits) {
    TaxReturn t = single("20000", "800");
    t.info.status = FilingStatus::HeadOfHousehold;
    t.dependents.push_back(child("Riley", "2018-01-10"));
    const Result r = calculate(t);
    CHECK_EQ(r.summary.deduction, M("23625"));
    CHECK_EQ(r.summary.taxableIncome, Money());
    CHECK_EQ(r.line("1040", "19"), Money());
    // ACTC: min(2,200, 1,700, 15% of (20,000 - 2,500) = 2,625) = 1,700.
    CHECK_EQ(r.line("1040", "28"), M("1700"));
    // EIC: plateau, AGI below the phase-out start.
    CHECK_EQ(r.line("1040", "27a"), M("4328"));
    CHECK_EQ(r.summary.refund, M("6828"));
}

TEST(eic_phaseout_uses_smaller_of_earned_and_agi) {
    TaxReturn t = single("30000");
    t.info.status = FilingStatus::HeadOfHousehold;
    t.dependents.push_back(child("Riley", "2018-01-10"));
    Interest1099 i;
    i.payer = "Bank";
    i.interest = M("500");
    t.interest.push_back(i);
    const Result r = calculate(t);
    // Earned 30,000: 4,328 - 15.98% x (30,025 - 23,350) = 3,261.34 -> 3,261.
    // AGI 30,500: 4,328 - 15.98% x (30,525 - 23,350) = 3,181.44 -> 3,181.
    CHECK_EQ(r.line("EIC Worksheet", "2"), M("3261"));
    CHECK_EQ(r.line("EIC Worksheet", "5"), M("3181"));
    CHECK_EQ(r.line("1040", "27a"), M("3181"));
}

TEST(eic_without_children_age_test) {
    TaxReturn t = single("12000");
    t.taxpayer.birthDate = D("2002-05-05");  // 23: too young
    CHECK_EQ(calculate(t).line("1040", "27a"), Money());
    t.taxpayer.birthDate = D("1990-05-05");
    // 12,000: 649 - 7.65% x (12,025 - 10,620) = 541.52 -> 542.
    CHECK_EQ(calculate(t).line("1040", "27a"), M("542"));
}

TEST(eic_investment_income_limit) {
    TaxReturn t = single("15000");
    t.dependents.push_back(child("Riley", "2018-01-10"));
    t.info.status = FilingStatus::HeadOfHousehold;
    Dividend1099 d;
    d.ordinary = M("12000");
    t.dividends.push_back(d);
    CHECK_EQ(calculate(t).line("1040", "27a"), Money());
}

TEST(self_employment_and_qbi) {
    TaxReturn t = single("0");
    Business b;
    b.name = "Studio";
    b.receipts = M("60000");
    b.supplies = M("10000");
    t.businesses.push_back(b);
    const Result r = calculate(t);
    CHECK_EQ(r.line("Schedule C (Studio)", "31"), M("50000"));
    const char* se = "Schedule SE (Pat)";
    CHECK_EQ(r.line(se, "4a"), M("46175"));
    CHECK_EQ(r.line(se, "10"), M("5725.70"));
    CHECK_EQ(r.line(se, "11"), M("1339.08"));
    CHECK_EQ(r.line(se, "12"), M("7064.78"));
    CHECK_EQ(r.line(se, "13"), M("3532.39"));
    CHECK_EQ(r.summary.agi, M("46467.61"));
    // QBI: 20% of (50,000 - 3,532.39) = 9,293.52; limited to 20% of 30,717.61 = 6,143.52.
    CHECK_EQ(r.line("Form 8995", "5"), M("9293.52"));
    CHECK_EQ(r.line("Form 8995", "14"), M("6143.52"));
    CHECK_EQ(r.summary.qbiDeduction, M("6143.52"));
    CHECK_EQ(r.summary.taxableIncome, M("24574.09"));
    CHECK_EQ(r.summary.incomeTax, M("2711"));  // row 24,550-24,600
    CHECK_EQ(r.summary.totalTax, M("9775.78"));
    CHECK_EQ(r.line("Schedule 2", "4"), M("7064.78"));
}

TEST(se_tax_social_security_wage_base) {
    TaxReturn t = single("170000");
    Business b;
    b.name = "Side";
    b.receipts = M("20000");
    t.businesses.push_back(b);
    const Result r = calculate(t);
    const char* se = "Schedule SE (Pat)";
    CHECK_EQ(r.line(se, "4a"), M("18470"));
    CHECK_EQ(r.line(se, "9"), M("6100"));             // 176,100 - 170,000
    CHECK_EQ(r.line(se, "10"), M("756.40"));          // 12.4% of 6,100
    CHECK_EQ(r.line(se, "11"), M("535.63"));          // 2.9% of 18,470
}

TEST(meals_and_home_office) {
    TaxReturn t = single("0");
    Business b;
    b.name = "Consult";
    b.receipts = M("10000");
    b.meals = M("1000");
    b.homeOfficeSqFt = 400;
    t.businesses.push_back(b);
    const Result r = calculate(t);
    CHECK_EQ(r.line("Schedule C (Consult)", "24b"), M("500"));
    CHECK_EQ(r.line("Schedule C (Consult)", "30"), M("1500"));  // capped at 300 sq ft
    CHECK_EQ(r.line("Schedule C (Consult)", "31"), M("8000"));
}

TEST(qualified_dividends_and_capital_gains) {
    TaxReturn t = single("100000");
    Dividend1099 d;
    d.payer = "Fund";
    d.ordinary = M("10000");
    d.qualified = M("10000");
    t.dividends.push_back(d);
    CapitalTxn c;
    c.description = "Stock";
    c.proceeds = M("50000");
    c.basis = M("30000");
    c.term = Term::Long;
    t.capitalTxns.push_back(c);
    const Result r = calculate(t);
    CHECK_EQ(r.summary.taxableIncome, M("114250"));
    const char* w = "QDCG Worksheet";
    CHECK_EQ(r.line(w, "4"), M("30000"));
    CHECK_EQ(r.line(w, "5"), M("84250"));
    CHECK_EQ(r.line(w, "9"), Money());
    CHECK_EQ(r.line(w, "18"), M("4500"));
    CHECK_EQ(r.line(w, "22"), M("13455"));  // table row 84,250-84,300
    CHECK_EQ(r.line(w, "24"), M("20267"));  // 24% x 114,250 - 7,153
    CHECK_EQ(r.summary.incomeTax, M("17955"));
    CHECK(hasForm(r, "Schedule B"));
    CHECK(hasForm(r, "Schedule D"));
    CHECK(hasForm(r, "Form 8949"));
}

TEST(zero_percent_capital_gains) {
    TaxReturn t = single("30000");
    CapitalTxn c;
    c.proceeds = M("20000");
    c.basis = M("10000");
    c.term = Term::Long;
    t.capitalTxns.push_back(c);
    const Result r = calculate(t);
    // Taxable 24,250 of which 10,000 is gain; ordinary 14,250 and the gain fits under 48,350.
    CHECK_EQ(r.line("QDCG Worksheet", "9"), M("10000"));
    CHECK_EQ(r.summary.incomeTax, incomeTax(M("14250"), FilingStatus::Single, R()));
}

TEST(capital_loss_limit_and_carryover) {
    TaxReturn t = single("50000");
    CapitalTxn c;
    c.proceeds = M("5000");
    c.basis = M("15000");
    t.capitalTxns.push_back(c);
    const Result r = calculate(t);
    CHECK_EQ(r.line("Schedule D", "16"), M("-10000"));
    CHECK_EQ(r.line("1040", "7a"), M("-3000"));
    CHECK_EQ(r.summary.agi, M("47000"));
    CHECK_EQ(r.line("Capital Loss Carryover", "8"), M("7000"));
    CHECK_EQ(r.line("Capital Loss Carryover", "13"), Money());

    // Carryover in: 7,000 short-term against a 2,000 long-term gain.
    TaxReturn next = single("50000");
    next.carryovers.shortTermLoss = M("7000");
    CapitalTxn g;
    g.proceeds = M("12000");
    g.basis = M("10000");
    g.term = Term::Long;
    next.capitalTxns.push_back(g);
    const Result r2 = calculate(next);
    CHECK_EQ(r2.line("Schedule D", "7"), M("-7000"));
    CHECK_EQ(r2.line("Schedule D", "16"), M("-5000"));
    CHECK_EQ(r2.line("1040", "7a"), M("-3000"));
    CHECK_EQ(r2.line("Capital Loss Carryover", "8"), M("2000"));  // 7,000 - (3,000 + 2,000)
}

TEST(capital_gain_distributions_without_schedule_d) {
    TaxReturn t = single("40000");
    Dividend1099 d;
    d.ordinary = M("1000");
    d.capitalGainDist = M("2500");
    t.dividends.push_back(d);
    const Result r = calculate(t);
    CHECK(!hasForm(r, "Schedule D"));
    CHECK_EQ(r.line("1040", "7a"), M("2500"));
    CHECK(hasForm(r, "QDCG Worksheet"));
    CHECK_EQ(r.line("QDCG Worksheet", "3"), M("2500"));
}

TEST(social_security_benefits_and_seniors) {
    TaxReturn t;
    t.info.status = FilingStatus::MarriedJoint;
    t.taxpayer.first = "Lee";
    t.taxpayer.birthDate = D("1958-04-04");
    t.spouse.first = "Kim";
    t.spouse.birthDate = D("1959-08-08");
    SocialSecurity s;
    s.benefits = M("30000");
    t.socialSecurity.push_back(s);
    Retirement1099R p;
    p.payer = "Pension";
    p.gross = M("40000");
    p.taxable = M("40000");
    p.federalWithheld = M("2000");
    t.retirement.push_back(p);
    const Result r = calculate(t);
    CHECK_EQ(r.line("SS Benefits Worksheet", "9"), M("23000"));
    CHECK_EQ(r.line("SS Benefits Worksheet", "15"), M("9350"));
    CHECK_EQ(r.line("1040", "6b"), M("15350"));
    CHECK_EQ(r.line("1040", "5b"), M("40000"));
    CHECK_EQ(r.summary.agi, M("55350"));
    CHECK_EQ(r.summary.deduction, M("34700"));  // 31,500 + 2 x 1,600
    CHECK_EQ(r.line("Schedule 1-A", "37"), M("12000"));
    CHECK_EQ(r.summary.taxableIncome, M("8650"));
    CHECK_EQ(r.summary.incomeTax, M("868"));
    CHECK_EQ(r.summary.refund, M("1132"));
}

TEST(social_security_not_taxable_below_base) {
    TaxReturn t = single("0");
    t.taxpayer.birthDate = D("1955-01-01");
    SocialSecurity s;
    s.benefits = M("24000");
    t.socialSecurity.push_back(s);
    Interest1099 i;
    i.interest = M("5000");
    t.interest.push_back(i);
    const Result r = calculate(t);
    // 12,000 + 5,000 = 17,000 < 25,000.
    CHECK_EQ(r.line("1040", "6b"), Money());
}

TEST(social_security_mfs_lived_together) {
    TaxReturn t = single("20000");
    t.info.status = FilingStatus::MarriedSeparate;
    SocialSecurity s;
    s.benefits = M("10000");
    t.socialSecurity.push_back(s);
    const Result r = calculate(t);
    // Line 7 = 5,000 + 20,000 = 25,000; 85% = 21,250; capped at 85% of benefits = 8,500.
    CHECK_EQ(r.line("1040", "6b"), M("8500"));
}

TEST(senior_deduction_phaseout_per_person) {
    TaxReturn t = single("95000");
    t.taxpayer.birthDate = D("1950-01-01");
    const Result r = calculate(t);
    // MAGI 95,000: 6,000 - 6% of 20,000 = 4,800.
    CHECK_EQ(r.line("Schedule 1-A", "35"), M("4800"));
    CHECK_EQ(r.line("1040", "13b"), M("4800"));
    CHECK_EQ(r.summary.deduction, M("17750"));  // 15,750 + 2,000 (65+)
}

TEST(schedule_1a_tips_overtime_car_loan) {
    TaxReturn t = single("60000");
    t.w2s[0].qualifiedTips = M("10000");
    t.w2s[0].qualifiedOvertime = M("5000");
    t.adjustments.carLoanInterest = M("3000");
    Result r = calculate(t);
    CHECK_EQ(r.line("Schedule 1-A", "13"), M("10000"));
    CHECK_EQ(r.line("Schedule 1-A", "21"), M("5000"));
    CHECK_EQ(r.line("Schedule 1-A", "30"), M("3000"));
    CHECK_EQ(r.line("1040", "13b"), M("18000"));
    CHECK_EQ(r.summary.taxableIncome, M("26250"));

    // Phase-outs: tips round the excess down to whole thousands, car loan rounds up.
    TaxReturn high = single("160500");
    high.w2s[0].qualifiedTips = M("10000");
    high.adjustments.carLoanInterest = M("5000");
    r = calculate(high);
    CHECK_EQ(r.line("Schedule 1-A", "12"), M("1000"));   // floor(10.5) x 100
    CHECK_EQ(r.line("Schedule 1-A", "13"), M("9000"));
    CHECK_EQ(r.line("Schedule 1-A", "29"), M("12200"));  // ceil(60.5) x 200
    CHECK_EQ(r.line("Schedule 1-A", "30"), Money());

    // Married filing separately can't take the tips deduction.
    TaxReturn mfs = single("60000");
    mfs.info.status = FilingStatus::MarriedSeparate;
    mfs.w2s[0].qualifiedTips = M("10000");
    r = calculate(mfs);
    CHECK_EQ(r.line("Schedule 1-A", "13"), Money());
}

TEST(itemized_salt_cap_and_phase_down) {
    TaxReturn t = single("520000");
    t.itemized.realEstateTax = M("50000");
    Result r = calculate(t);
    CHECK_EQ(r.line("Schedule A", "5e"), M("34000"));  // 40,000 - 30% of 20,000
    t.w2s[0].wages = M("700000");
    r = calculate(t);
    CHECK_EQ(r.line("Schedule A", "5e"), M("10000"));  // floor
    TaxReturn low = single("100000");
    low.itemized.realEstateTax = M("45000");
    r = calculate(low);
    CHECK_EQ(r.line("Schedule A", "5e"), M("40000"));
    // MFS: half of line 9.
    low.info.status = FilingStatus::MarriedSeparate;
    r = calculate(low);
    CHECK_EQ(r.line("Schedule A", "5e"), M("20000"));
}

TEST(itemized_versus_standard) {
    TaxReturn t = single("150000", "25000");
    t.w2s[0].stateWithheld = M("8000");
    t.itemized.realEstateTax = M("6000");
    t.itemized.mortgageInterest = M("9000");
    t.itemized.charityCash = M("2000");
    t.itemized.medical = M("12000");  // 7.5% of 150,000 = 11,250 -> 750 deductible
    const Result r = calculate(t);
    CHECK_EQ(r.line("Schedule A", "4"), M("750"));
    CHECK_EQ(r.line("Schedule A", "5a"), M("8000"));
    CHECK_EQ(r.line("Schedule A", "17"), M("25750"));
    CHECK(r.summary.itemized);
    CHECK_EQ(r.summary.deduction, M("25750"));
    CHECK(!r.form("Schedule A")->worksheet);

    TaxReturn s = single("150000");
    s.w2s[0].stateWithheld = M("5000");
    const Result r2 = calculate(s);
    CHECK(!r2.summary.itemized);
    CHECK_EQ(r2.summary.deduction, M("15750"));
    CHECK(r2.form("Schedule A")->worksheet);  // shown for comparison only
}

TEST(student_loan_interest_phaseout) {
    TaxReturn t = single("90000");
    t.adjustments.studentLoanInterest = M("3000");
    const Result r = calculate(t);
    const char* w = "Student Loan Interest Worksheet";
    CHECK_EQ(r.line(w, "1"), M("2500"));
    CHECK_EQ(r.form(w)->find("7")->text, std::string("0.333"));
    CHECK_EQ(r.line(w, "8"), M("832.50"));
    CHECK_EQ(r.line("Schedule 1", "21"), M("1667.50"));
}

TEST(ira_deduction_phaseout) {
    TaxReturn t = single("84000");
    t.w2s[0].retirementPlan = true;
    t.taxpayer.traditionalIra = M("7000");
    Result r = calculate(t);
    CHECK_EQ(r.line("IRA Deduction Worksheet", "7a"), M("3500"));  // 5,000 x 70%
    CHECK_EQ(r.line("Schedule 1", "20"), M("3500"));

    // Rounding up to $10 and the $200 minimum.
    t.w2s[0].wages = M("88990");
    r = calculate(t);
    CHECK_EQ(r.line("Schedule 1", "20"), M("200"));
    t.w2s[0].wages = M("85555");
    r = calculate(t);
    CHECK_EQ(r.line("Schedule 1", "20"), M("2420"));  // 3,445 x 70% = 2,411.50 -> 2,420

    // Not covered: fully deductible; age 50+ catch-up.
    TaxReturn n = single("200000");
    n.taxpayer.birthDate = D("1970-01-01");
    n.taxpayer.traditionalIra = M("9000");
    r = calculate(n);
    CHECK_EQ(r.line("Schedule 1", "20"), M("8000"));
}

TEST(child_tax_credit_phaseout_rounding) {
    TaxReturn t = single("200425");
    t.info.status = FilingStatus::HeadOfHousehold;
    t.dependents.push_back(child("A", "2012-01-01"));
    const Result r = calculate(t);
    CHECK_EQ(r.line("Schedule 8812", "10"), M("1000"));  // 425 rounds up to 1,000
    CHECK_EQ(r.line("Schedule 8812", "11"), M("50"));
    CHECK_EQ(r.line("1040", "19"), M("2150"));
}

TEST(other_dependents_and_age_17) {
    TaxReturn t = single("80000");
    t.info.status = FilingStatus::HeadOfHousehold;
    t.dependents.push_back(child("Teen", "2008-12-31"));  // 17 at year end: other dependent
    Dependent parent;
    parent.first = "Mom";
    parent.birthDate = D("1950-01-01");
    parent.relationship = Relationship::Parent;
    parent.monthsLivedWithYou = 0;
    t.dependents.push_back(parent);
    const Result r = calculate(t);
    CHECK_EQ(r.form("Schedule 8812")->find("4")->text, std::string("0"));
    CHECK_EQ(r.form("Schedule 8812")->find("6")->text, std::string("2"));
    CHECK_EQ(r.line("1040", "19"), M("1000"));
}

TEST(actc_three_children_part_2b) {
    TaxReturn t = single("20000");
    t.info.status = FilingStatus::HeadOfHousehold;
    t.dependents.push_back(child("A", "2012-01-01"));
    t.dependents.push_back(child("B", "2014-01-01"));
    t.dependents.push_back(child("C", "2016-01-01"));
    const Result r = calculate(t);
    // 16b = 5,100, line 20 = 2,625 < line 17 = 5,100, so Part II-B applies:
    // withheld SS+Medicare 1,530; minus EIC (20,000 on the 3-child plateau is 8,046) -> 0.
    CHECK_EQ(r.line("Schedule 8812", "16b"), M("5100"));
    CHECK_EQ(r.line("Schedule 8812", "21"), M("1530"));
    CHECK_EQ(r.line("Schedule 8812", "27"), M("2625"));
    CHECK_EQ(r.line("1040", "27a"), M("8046"));
}

TEST(dependent_care_credit) {
    TaxReturn t = single("40000");
    t.info.status = FilingStatus::HeadOfHousehold;
    Dependent d = child("Tot", "2021-01-01");
    d.careExpenses = M("5000");
    t.dependents.push_back(d);
    const Result r = calculate(t);
    CHECK_EQ(r.line("Form 2441", "3"), M("3000"));
    CHECK_EQ(r.form("Form 2441")->find("8")->text, std::string("0.22"));  // AGI 40,000: 35 - 13
    CHECK_EQ(r.line("Form 2441", "9c"), M("660"));
    CHECK_EQ(r.line("Schedule 3", "2"), M("660"));
}

TEST(dependent_care_benefits_part_3) {
    TaxReturn t = single("50000");
    t.info.status = FilingStatus::HeadOfHousehold;
    t.w2s[0].dependentCare = M("5000");
    Dependent a = child("A", "2020-01-01");
    a.careExpenses = M("4000");
    Dependent b = child("B", "2022-01-01");
    b.careExpenses = M("4000");
    t.dependents.push_back(a);
    t.dependents.push_back(b);
    const Result r = calculate(t);
    CHECK_EQ(r.line("Form 2441 Part III", "25"), M("5000"));
    CHECK_EQ(r.line("Form 2441 Part III", "26"), Money());
    CHECK_EQ(r.line("Form 2441 Part III", "31"), M("1000"));  // 6,000 - 5,000
    CHECK_EQ(r.line("Form 2441", "9c"), M("200"));            // 20% of 1,000
}

TEST(dependent_care_benefits_taxable_excess) {
    TaxReturn t = single("50000");
    t.w2s[0].dependentCare = M("5000");
    Dependent a = child("A", "2020-01-01");
    a.careExpenses = M("3000");
    t.dependents.push_back(a);
    t.info.status = FilingStatus::HeadOfHousehold;
    const Result r = calculate(t);
    CHECK_EQ(r.line("1040", "1e"), M("2000"));
    CHECK_EQ(r.line("1040", "1z"), M("52000"));
}

TEST(education_credits) {
    TaxReturn t = single("50000", "5000");
    Education e;
    e.student = "Pat";
    e.qualifiedExpenses = M("5000");
    t.education.push_back(e);
    Result r = calculate(t);
    CHECK_EQ(r.line("Form 8863", "1"), M("2500"));
    CHECK_EQ(r.line("Form 8863", "8"), M("1000"));  // 40% refundable
    CHECK_EQ(r.line("Form 8863", "19"), M("1500"));
    CHECK_EQ(r.line("1040", "29"), M("1000"));

    // Phase-out: MAGI 85,000 -> fraction 0.5.
    t.w2s[0].wages = M("85000");
    r = calculate(t);
    CHECK_EQ(r.line("Form 8863", "7"), M("1250"));

    // Lifetime learning credit.
    TaxReturn l = single("50000");
    Education g;
    g.qualifiedExpenses = M("12000");
    g.aotcEligible = false;
    l.education.push_back(g);
    r = calculate(l);
    CHECK_EQ(r.line("Form 8863", "18"), M("2000"));
    CHECK_EQ(r.line("Form 8863", "19"), M("2000"));
}

TEST(savers_credit) {
    TaxReturn t = single("24000");
    t.w2s[0].electiveDeferrals = M("1500");
    t.taxpayer.rothIra = M("1000");
    const Result r = calculate(t);
    CHECK_EQ(r.line("Form 8880", "6a"), M("2000"));
    CHECK_EQ(r.form("Form 8880")->find("9")->text, std::string("0.2"));
    CHECK_EQ(r.line("Form 8880", "10"), M("400"));
    // Tax on 8,250 is 826, so the full 400 is allowed.
    CHECK_EQ(r.line("Schedule 3", "4"), M("400"));
}

TEST(additional_medicare_and_niit) {
    TaxReturn t = single("250000");
    t.w2s[0].medicareWithheld = M("4075");  // 1.45% of 250,000 + 0.9% of 50,000
    Interest1099 i;
    i.interest = M("10000");
    t.interest.push_back(i);
    const Result r = calculate(t);
    CHECK_EQ(r.line("Form 8959", "7"), M("450"));
    CHECK_EQ(r.line("Form 8959", "24"), M("450"));
    CHECK_EQ(r.line("1040", "25c"), M("450"));
    CHECK_EQ(r.line("Form 8960", "16"), M("10000"));
    CHECK_EQ(r.line("Form 8960", "17"), M("380"));
    CHECK_EQ(r.line("Schedule 2", "21"), M("830"));
}

TEST(excess_social_security) {
    TaxReturn t = single("0");
    W2 a = w2("110000");
    W2 b = w2("100000");
    t.w2s = {a, b};
    const Result r = calculate(t);
    // 6,820 + 6,200 = 13,020 - 10,918.20 = 2,101.80.
    CHECK_EQ(r.line("Schedule 3", "11"), M("2101.80"));
    CHECK_EQ(r.line("1040", "31"), M("2101.80"));
}

TEST(early_distribution_penalty) {
    TaxReturn t = single("50000");
    Retirement1099R x;
    x.payer = "IRA";
    x.gross = M("10000");
    x.taxable = M("10000");
    x.code = "1";
    x.ira = true;
    x.penaltyException = M("2000");
    t.retirement.push_back(x);
    const Result r = calculate(t);
    CHECK_EQ(r.line("1040", "4b"), M("10000"));
    CHECK_EQ(r.line("Schedule 2", "8"), M("800"));

    // A direct rollover (code G) is not taxable.
    TaxReturn g = single("50000");
    x.code = "G";
    g.retirement.push_back(x);
    CHECK_EQ(calculate(g).line("1040", "4b"), Money());
}

TEST(dependent_filer_standard_deduction) {
    TaxReturn t = single("5000");
    t.taxpayer.claimedAsDependent = true;
    const Result r = calculate(t);
    CHECK_EQ(r.summary.deduction, M("5450"));
    TaxReturn u = single("500");
    u.taxpayer.claimedAsDependent = true;
    CHECK_EQ(calculate(u).summary.deduction, M("1350"));
}

TEST(mfs_spouse_itemizes) {
    TaxReturn t = single("50000");
    t.info.status = FilingStatus::MarriedSeparate;
    t.info.spouseItemizes = true;
    CHECK_EQ(calculate(t).summary.deduction, Money());
}

TEST(amt_not_triggered_for_ordinary_returns) {
    TaxReturn t = single("400000");
    t.itemized.realEstateTax = M("40000");
    t.itemized.stateIncomeTax = M("30000");
    t.itemized.mortgageInterest = M("20000");
    const Result r = calculate(t);
    CHECK_EQ(r.line("1040", "17"), Money());
    CHECK(!hasForm(r, "Form 6251"));
}

TEST(qbi_above_threshold_is_flagged) {
    TaxReturn t = single("150000");
    Business b;
    b.name = "Big";
    b.receipts = M("200000");
    t.businesses.push_back(b);
    const Result r = calculate(t);
    CHECK(r.hasErrors());
    CHECK_EQ(r.summary.qbiDeduction, Money());
}

TEST(filing_status_checks) {
    TaxReturn t = single("50000");
    t.info.status = FilingStatus::HeadOfHousehold;
    CHECK(calculate(t).hasErrors());
    t.dependents.push_back(child("Kid", "2015-01-01"));
    CHECK(!calculate(t).hasErrors());
    t.info.status = FilingStatus::MarriedJoint;
    CHECK(calculate(t).hasErrors());  // no spouse name
    t.spouse.first = "Sam";
    CHECK(!calculate(t).hasErrors());
}

TEST(spouse_items_ignored_unless_joint) {
    TaxReturn t = single("50000");
    t.w2s.push_back(w2("40000", "0", Owner::Spouse));
    const Result r = calculate(t);
    CHECK_EQ(r.summary.agi, M("50000"));
}

TEST(marginal_rate) {
    CHECK_EQ(marginalRate(single("60000")), Decimal::fromInt(12));
    CHECK_EQ(marginalRate(single("120000")), Decimal::fromInt(24));
}

TEST(unsupported_year) {
    TaxReturn t = single("50000");
    t.info.year = 2024;
    CHECK(calculate(t).hasErrors());
}

// ------------------------------------------------------------ storage

TaxReturn fullReturn() {
    TaxReturn t = single("85000", "9000");
    t.info.status = FilingStatus::MarriedJoint;
    t.info.street = "12 Main St\twith tab";
    t.info.city = "Springfield";
    t.spouse.first = "Sam";
    t.spouse.birthDate = D("1984-09-30");
    t.spouse.traditionalIra = M("7000");
    t.w2s[0].retirementPlan = true;
    t.w2s[0].stateCode = "IL";
    t.w2s[0].qualifiedTips = M("123.45");
    t.dependents.push_back(child("Kid", "2016-02-29"));
    t.dependents.back().relationship = Relationship::Grandchild;
    Interest1099 i;
    i.payer = "Bank = \"First\"\\";
    i.interest = M("12.34");
    t.interest.push_back(i);
    CapitalTxn c;
    c.description = "100 sh\nXYZ";
    c.term = Term::Long;
    c.acquired = D("2020-01-02");
    c.proceeds = M("1000");
    t.capitalTxns.push_back(c);
    Business b;
    b.name = "Studio";
    b.homeOfficeSqFt = 150;
    b.receipts = M("1");
    t.businesses.push_back(b);
    t.itemized.useSalesTax = true;
    t.payments.estimated = M("-0.01");
    t.carryovers.shortTermLoss = M("100");
    return t;
}

TEST(serialize_round_trip) {
    const TaxReturn t = fullReturn();
    const std::string text = t.serialize();
    const TaxReturn back = TaxReturn::parse(text);
    CHECK_EQ(back.serialize(), text);
    CHECK_EQ(back.info.street, t.info.street);
    CHECK_EQ(back.interest[0].payer, t.interest[0].payer);
    CHECK_EQ(back.capitalTxns[0].description, t.capitalTxns[0].description);
    CHECK(back.dependents[0].relationship == Relationship::Grandchild);
    CHECK(back.itemized.useSalesTax);
    CHECK_EQ(back.businesses[0].homeOfficeSqFt, 150);
    CHECK(text.find("OPENTAX 1\n") == 0);
    // Blank fields are not written.
    CHECK(text.find("sswh=") != std::string::npos);
    CHECK(text.find("depcare=") == std::string::npos);
}

TEST(parse_rejects_bad_input) {
    CHECK_THROWS(TaxReturn::parse("NOT A RETURN\n"));
    CHECK_THROWS(TaxReturn::parse("OPENTAX 1\nW2\tbogus=1\n"));
    CHECK_THROWS(TaxReturn::parse("OPENTAX 1\nMYSTERY\ta=1\n"));
    CHECK_THROWS(TaxReturn::parse("OPENTAX 1\nW2\twages=abc\n"));
    CHECK_THROWS(TaxReturn::parse("OPENTAX 1\nINFO\tstatus=married\n"));
    CHECK_THROWS(TaxReturn::parse("OPENTAX 1\nTAXPAYER\tdob=2025-02-30\n"));
    const TaxReturn ok = TaxReturn::parse("\xEF\xBB\xBFOPENTAX 1\r\n# comment\r\nINFO\tstatus=hoh\r\n");
    CHECK(ok.info.status == FilingStatus::HeadOfHousehold);
}

TEST(save_and_load_atomic) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "opentax_test_save";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string path = (dir / "r.otx").u8string();
    TaxReturn t = fullReturn();
    t.save(path);
    t.taxpayer.first = "Changed";
    t.save(path);
    CHECK(fs::exists(path + ".bak"));
    CHECK(!fs::exists(path + ".tmp"));
    CHECK_EQ(TaxReturn::load(path).taxpayer.first, std::string("Changed"));
    CHECK_EQ(TaxReturn::load(path + ".bak").taxpayer.first, std::string("Pat"));
    fs::remove_all(dir);
}

// ----------------------------------------------------------------- PDF

TEST(return_pdf_is_safe_and_complete) {
    TaxReturn t = fullReturn();
    t.taxpayer.first = "Evil) Tj (injected";
    const Result r = calculate(t);
    const std::string pdf = returnPdf(t, r);
    CHECK(pdf.rfind("%PDF-1.4", 0) == 0);
    CHECK(pdf.find("%%EOF") != std::string::npos);
    for (const char* bad : {"/JavaScript", "/JS", "/URI", "/Launch", "/EmbeddedFile", "/OpenAction", "/AA"})
        CHECK(pdf.find(bad) == std::string::npos);
    CHECK(pdf.find("(Evil) Tj") == std::string::npos);
    CHECK(pdf.find("Evil\\) Tj \\(injected") != std::string::npos);
    CHECK_EQ(returnPdf(t, r), pdf);  // deterministic
    CHECK_EQ(returnPdfFileName(t), std::string("2025 Federal Return - Evil) Tj (injected & Sam Taxpayer.pdf"));
    TaxReturn bad;
    bad.taxpayer.first = "../../etc/passwd";
    CHECK(returnPdfFileName(bad).find('/') == std::string::npos);
}

// ----------------------------------------------------------------- CLI

int cli(std::vector<std::string> args, std::string* output = nullptr) {
    std::ostringstream out, err;
    const int code = runCli(args, out, err);
    if (output) *output = out.str() + err.str();
    return code;
}

TEST(cli_end_to_end) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "opentax_test_cli";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string file = (dir / "cli.otx").u8string();
    std::string out;
    CHECK_EQ(cli({"-f", file, "new", "status=single", "first=Pat", "dob=1985-06-15"}, &out), 0);
    CHECK_EQ(cli({"-f", file, "new"}), 1);  // exists
    CHECK_EQ(cli({"-f", file, "add", "w2", "employer=Acme", "wages=60000", "fedwh=6000"}, &out), 0);
    CHECK(out.find("#1") != std::string::npos);
    CHECK_EQ(cli({"-f", file, "summary"}, &out), 0);
    CHECK(out.find("Federal refund $925.00") != std::string::npos);
    CHECK_EQ(cli({"-f", file, "explain", "1040", "16"}, &out), 0);
    CHECK(out.find("Tax Table") != std::string::npos);
    CHECK_EQ(cli({"-f", file, "form", "1040", "--explain"}, &out), 0);
    CHECK(out.find("44,250.00") != std::string::npos);
    CHECK_EQ(cli({"-f", file, "edit", "w2", "1", "wages=70000"}, &out), 0);
    CHECK_EQ(cli({"-f", file, "set", "payments", "estimated=500"}, &out), 0);
    CHECK_EQ(cli({"-f", file, "list", "w2"}, &out), 0);
    CHECK(out.find("wages=70000.00") != std::string::npos);
    CHECK_EQ(cli({"-f", file, "add", "w2", "nonsense=1"}, &out), 1);
    CHECK_EQ(cli({"-f", file, "edit", "w2", "5", "wages=1"}, &out), 1);
    CHECK_EQ(cli({"-f", file, "check"}, &out), 0);
    CHECK_EQ(cli({"-f", file, "set", "info", "status=hoh"}, &out), 0);
    CHECK_EQ(cli({"-f", file, "check"}, &out), 1);  // HoH without a qualifying person
    CHECK_EQ(cli({"-f", file, "csv"}, &out), 0);
    CHECK(out.rfind("form,line,label,amount", 0) == 0);
    const std::string pdf = (dir / "out.pdf").u8string();
    CHECK_EQ(cli({"-f", file, "pdf", pdf}, &out), 0);
    CHECK(fs::file_size(pdf) > 2000);
    CHECK_EQ(cli({"-f", file, "pdf", pdf}, &out), 1);  // exists
    CHECK_EQ(cli({"-f", file, "remove", "w2", "1"}, &out), 0);
    CHECK_EQ(cli({"-f", file, "fields", "w2"}, &out), 0);
    CHECK(out.find("Box 1") != std::string::npos);
    CHECK_EQ(cli({"-f", file, "bogus"}), 2);
    CHECK_EQ(cli({"--version"}, &out), 0);
    fs::remove_all(dir);
}


// ------------------------------------------------------------- 2026

const Rules& R26() { return rulesFor(2026); }

TaxReturn single26(const char* wages, const char* withheld = "0") {
    TaxReturn r = single(wages, withheld);
    r.info.year = 2026;
    return r;
}

TEST(y2026_rate_schedules_match_rev_proc) {
    // Rev. Proc. 2025-32 section 4.01: the tax at the top of each bracket.
    const FilingStatus S = FilingStatus::Single, J = FilingStatus::MarriedJoint, H = FilingStatus::HeadOfHousehold,
                       MS = FilingStatus::MarriedSeparate;
    CHECK_EQ(incomeTax(M("105700"), S, R26()), M("17966.00"));
    CHECK_EQ(incomeTax(M("201775"), S, R26()), M("41024.00"));
    CHECK_EQ(incomeTax(M("256225"), S, R26()), M("58448.00"));
    CHECK_EQ(incomeTax(M("640600"), S, R26()), M("192979.25"));
    CHECK_EQ(incomeTax(M("100800"), J, R26()), M("11600.00"));
    CHECK_EQ(incomeTax(M("211400"), J, R26()), M("35932.00"));
    CHECK_EQ(incomeTax(M("403550"), J, R26()), M("82048.00"));
    CHECK_EQ(incomeTax(M("512450"), J, R26()), M("116896.00"));
    CHECK_EQ(incomeTax(M("768700"), J, R26()), M("206583.50"));
    CHECK_EQ(incomeTax(M("105700"), H, R26()), M("16155.00"));
    CHECK_EQ(incomeTax(M("201750"), H, R26()), M("39207.00"));
    CHECK_EQ(incomeTax(M("256200"), H, R26()), M("56631.00"));
    CHECK_EQ(incomeTax(M("640600"), H, R26()), M("191171.00"));
    CHECK_EQ(incomeTax(M("384350"), MS, R26()), M("103291.75"));
    CHECK_EQ(incomeTax(M("800000"), J, R26()), M("218164.50"));  // 206,583.50 + 37% x 31,300
    // Below $100,000 the Tax Table method: row 43,900-43,950, midpoint 43,925.
    CHECK_EQ(incomeTax(M("43900"), S, R26()), M("5023"));  // 1,240 + 12% x 31,525 = 5,023
}

TEST(y2026_eic_table) {
    CHECK_EQ(eicTable(M("20000"), 3, false, R26()), M("8231"));   // plateau
    CHECK_EQ(eicTable(M("45000"), 1, false, R26()), M("1050"));   // 4,427 - 15.98% x 21,135
    CHECK_EQ(eicTable(M("15000"), 0, false, R26()), M("345"));    // 664 - 7.65% x 4,165
    CHECK_EQ(eicTable(M("1"), 1, false, R26()), M("9"));
    CHECK_EQ(eicTable(M("51600"), 1, false, R26()), Money());     // past the $51,593 phase-out end
}

TEST(y2026_simple_single) {
    const Result r = calculate(single26("60000", "6000"));
    CHECK(!r.hasErrors());
    CHECK_EQ(r.summary.deduction, M("16100"));
    CHECK_EQ(r.summary.taxableIncome, M("43900"));
    CHECK_EQ(r.summary.incomeTax, M("5023"));
    CHECK_EQ(r.summary.refund, M("977"));
    // 2026 layout: 24a/24c and 32a/32c.
    CHECK(r.form("1040")->find("24c") != nullptr);
    CHECK(r.form("1040")->find("24") == nullptr);
    CHECK_EQ(r.line("1040", "24c"), M("5023"));
    CHECK(r.form("1040")->find("32c") != nullptr);
}

TEST(y2026_standard_deduction_seniors) {
    TaxReturn t = single26("50000");
    t.taxpayer.birthDate = D("1961-06-01");  // born before January 2, 1962
    Result r = calculate(t);
    CHECK_EQ(r.summary.deduction, M("18150"));  // 16,100 + 2,050
    CHECK_EQ(r.line("Schedule 1-A", "43"), M("6000"));
    CHECK_EQ(r.line("Schedule 1-A", "44"), M("6000"));
    CHECK_EQ(r.line("1040", "13a"), M("6000"));  // Schedule 1-A is line 13a in 2026
    CHECK_EQ(r.summary.seniorDeduction, M("6000"));
    // The same birth date is 64 at the end of 2025.
    t.info.year = 2025;
    CHECK_EQ(calculate(t).summary.seniorDeduction, Money());
}

TEST(y2026_schedule_1a_line_numbers) {
    TaxReturn t = single26("160500");
    t.w2s[0].qualifiedTips = M("10000");
    t.w2s[0].qualifiedOvertime = M("5000");
    t.adjustments.carLoanInterest = M("5000");
    const Result r = calculate(t);
    CHECK_EQ(r.line("Schedule 1-A", "14"), M("1000"));  // floor(10.5) x 100
    CHECK_EQ(r.line("Schedule 1-A", "15"), M("9000"));
    CHECK_EQ(r.line("Schedule 1-A", "27"), M("4000"));
    CHECK_EQ(r.line("Schedule 1-A", "35"), M("12200"));  // ceil(60.5) x 200
    CHECK_EQ(r.line("Schedule 1-A", "36"), Money());
    CHECK_EQ(r.line("Schedule 1-A", "44"), M("13000"));
}

TEST(y2026_non_itemizer_charity) {
    TaxReturn t = single26("60000");
    t.itemized.charityCash = M("1500");
    Result r = calculate(t);
    CHECK(!r.summary.itemized);
    CHECK_EQ(r.line("1040", "12f"), M("1000"));
    CHECK_EQ(r.summary.taxableIncome, M("42900"));
    t.info.status = FilingStatus::MarriedJoint;
    t.spouse.first = "Sam";
    t.itemized.charityCash = M("2500");
    r = calculate(t);
    CHECK_EQ(r.line("1040", "12f"), M("2000"));
    // Not available in 2025.
    t.info.year = 2025;
    CHECK(calculate(t).form("1040")->find("12f") == nullptr);
}

TEST(y2026_itemized_charity_floor_and_salt) {
    TaxReturn t = single26("200000");
    t.itemized.realEstateTax = M("30000");
    t.itemized.stateIncomeTax = M("20000");
    t.itemized.mortgageInterest = M("20000");
    t.itemized.charityCash = M("5000");
    const Result r = calculate(t);
    CHECK_EQ(r.line("Schedule A", "5e"), M("40400"));
    CHECK_EQ(r.line("Schedule A", "13"), M("4000"));  // 5,000 - 0.5% of 200,000
    CHECK_EQ(r.line("Schedule A", "18"), M("64400"));
    CHECK(r.summary.itemized);
    CHECK_EQ(r.summary.deduction, M("64400"));
    TaxReturn high = single26("525000");
    high.itemized.realEstateTax = M("50000");
    CHECK_EQ(calculate(high).line("Schedule A", "5e"), M("34400"));  // 40,400 - 30% x 20,000
}

TEST(y2026_itemized_limitation_37_percent) {
    TaxReturn t = single26("800000");
    t.itemized.realEstateTax = M("20000");
    t.itemized.mortgageInterest = M("30000");
    t.itemized.charityCash = M("50000");
    const Result r = calculate(t);
    // SALT floors at 10,000; charity 50,000 - 4,000; total 86,000.
    CHECK_EQ(r.line("Itemized Deduction Limitation", "1"), M("86000"));
    CHECK_EQ(r.line("Itemized Deduction Limitation", "4"), M("159400"));  // 800,000 - 640,600
    CHECK_EQ(r.line("Itemized Deduction Limitation", "6"), M("4648.65"));  // 86,000 x 2/37
    CHECK_EQ(r.summary.deduction, M("81351.35"));
    CHECK_EQ(r.line("Schedule A", "18"), M("81351.35"));
    CHECK_EQ(r.summary.taxableIncome, M("718648.65"));
}

TEST(y2026_mortgage_insurance) {
    TaxReturn t = single26("105500");
    t.itemized.mortgageInsurance = M("1000");
    t.itemized.mortgageInterest = M("20000");
    const Result r = calculate(t);
    CHECK_EQ(r.line("Schedule A", "8d"), M("400"));  // 6 x 10% reduction
    t.info.year = 2025;
    const Result r25 = calculate(t);
    CHECK(r25.form("Schedule A")->find("8d") == nullptr);
}

TEST(y2026_dependent_care_rates) {
    TaxReturn t = single26("40000");
    t.info.status = FilingStatus::HeadOfHousehold;
    Dependent d = child("Tot", "2022-01-01");
    d.careExpenses = M("5000");
    t.dependents.push_back(d);
    Result r = calculate(t);
    CHECK_EQ(r.form("Form 2441")->find("8")->text, std::string("0.37"));
    CHECK_EQ(r.line("Form 2441", "9c"), M("1110"));
    t.w2s[0].wages = M("60000");
    CHECK_EQ(calculate(t).form("Form 2441")->find("8")->text, std::string("0.35"));
    t.w2s[0].wages = M("80000");
    CHECK_EQ(calculate(t).form("Form 2441")->find("8")->text, std::string("0.32"));
    t.w2s[0].wages = M("120000");
    CHECK_EQ(calculate(t).form("Form 2441")->find("8")->text, std::string("0.20"));
    t.info.status = FilingStatus::MarriedJoint;
    t.spouse.first = "Sam";
    t.w2s[0].wages = M("100000");
    t.w2s.push_back(w2("60000", "0", Owner::Spouse));
    CHECK_EQ(calculate(t).form("Form 2441")->find("8")->text, std::string("0.32"));  // 160,000 joint
}

TEST(y2026_dependent_care_benefit_exclusion) {
    TaxReturn t = single26("50000");
    t.info.status = FilingStatus::HeadOfHousehold;
    t.w2s[0].dependentCare = M("7500");
    Dependent d = child("Tot", "2022-01-01");
    d.careExpenses = M("8000");
    t.dependents.push_back(d);
    const Result r = calculate(t);
    CHECK_EQ(r.line("Form 2441 Part III", "21"), M("7500"));
    CHECK_EQ(r.line("Form 2441 Part III", "26"), Money());
}

TEST(y2026_qbi_minimum_deduction) {
    TaxReturn t = single26("50000");
    Business b;
    b.name = "Side";
    b.receipts = M("1500");
    t.businesses.push_back(b);
    Result r = calculate(t);
    CHECK_EQ(r.line("Schedule SE (Pat)", "13"), M("105.97"));
    CHECK_EQ(r.line("Form 8995", "15"), M("278.81"));  // 20% of 1,394.03
    CHECK_EQ(r.line("Form 8995", "16"), M("400"));
    CHECK_EQ(r.line("Form 8995", "17"), M("400"));
    CHECK_EQ(r.line("1040", "13b"), M("400"));  // QBI is line 13b in 2026
    CHECK_EQ(r.summary.qbiDeduction, M("400"));
    t.businesses[0].materialParticipation = false;
    CHECK_EQ(calculate(t).summary.qbiDeduction, M("278.81"));
    t.businesses[0].materialParticipation = true;
    t.info.year = 2025;
    CHECK_EQ(calculate(t).summary.qbiDeduction, M("278.81"));
}

TEST(y2026_ira_phaseout) {
    TaxReturn t = single26("86000");
    t.w2s[0].retirementPlan = true;
    t.taxpayer.traditionalIra = M("7500");
    CHECK_EQ(calculate(t).line("Schedule 1", "20"), M("3750"));  // 7,500 x 5,000 / 10,000
    TaxReturn j = single26("139000");
    j.info.status = FilingStatus::MarriedJoint;
    j.spouse.first = "Sam";
    j.taxpayer.birthDate = D("1970-03-03");
    j.w2s[0].retirementPlan = true;
    j.taxpayer.traditionalIra = M("8600");
    CHECK_EQ(calculate(j).line("Schedule 1", "20"), M("4300"));  // 8,600 x 10,000 / 20,000
}

TEST(y2026_student_loan_joint_phaseout) {
    TaxReturn t = single26("190000");
    t.info.status = FilingStatus::MarriedJoint;
    t.spouse.first = "Sam";
    t.adjustments.studentLoanInterest = M("2500");
    const Result r = calculate(t);
    CHECK_EQ(r.form("Student Loan Interest Worksheet")->find("7")->text, std::string("0.500"));  // 15,000 / 30,000
    CHECK_EQ(r.line("Schedule 1", "21"), M("1250"));
}

TEST(y2026_self_employment_wage_base) {
    TaxReturn t = single26("180000");
    Business b;
    b.name = "Side";
    b.receipts = M("20000");
    t.businesses.push_back(b);
    const Result r = calculate(t);
    CHECK_EQ(r.line("Schedule SE (Pat)", "9"), M("4500"));  // 184,500 - 180,000
    CHECK_EQ(r.line("Schedule SE (Pat)", "10"), M("558"));
}

TEST(y2026_schedule_2_and_8959_routing) {
    TaxReturn t = single26("250000");
    t.w2s[0].medicareWithheld = M("4075");
    Interest1099 i;
    i.interest = M("10000");
    t.interest.push_back(i);
    const Result r = calculate(t);
    CHECK_EQ(r.line("Form 8959", "12"), M("450"));
    CHECK_EQ(r.line("Schedule 2", "17b"), M("450"));
    CHECK_EQ(r.line("Schedule 2", "20"), M("450"));
    CHECK_EQ(r.line("Schedule 2", "6"), M("380"));   // NIIT moved to line 6
    CHECK_EQ(r.line("Schedule 2", "21"), M("830"));
    CHECK_EQ(r.line("1040", "25c"), M("450"));
}

TEST(y2026_schedule_3a_public_benefit) {
    TaxReturn t = single26("20000", "500");
    t.info.status = FilingStatus::HeadOfHousehold;
    t.dependents.push_back(child("Riley", "2018-01-10"));
    Result r = calculate(t);
    CHECK_EQ(r.line("1040", "27a"), M("4427"));
    CHECK_EQ(r.line("1040", "28"), M("1700"));
    CHECK_EQ(r.line("1040", "32a"), M("6127"));
    CHECK_EQ(r.line("Schedule 3-A", "6"), M("6127"));
    CHECK_EQ(r.line("1040", "32b"), Money());
    CHECK_EQ(r.summary.refund, M("6627"));
    t.info.citizenOrQualifiedAlien = false;
    r = calculate(t);
    CHECK_EQ(r.line("1040", "32b"), M("6127"));
    CHECK_EQ(r.line("1040", "32c"), Money());
    CHECK_EQ(r.summary.refund, M("500"));
}

TEST(y2026_ctc_and_amt_phaseout_rules) {
    TaxReturn t = single26("90000");
    t.info.status = FilingStatus::HeadOfHousehold;
    t.dependents.push_back(child("A", "2015-01-01"));
    CHECK_EQ(calculate(t).line("1040", "19"), M("2200"));
    CHECK_EQ(R26().amtPhasePercent, Decimal::fromInt(50));
    // AMT exemption fully phased out at 500,000 + 2 x 90,100 = 680,200 (Rev. Proc. 2025-32).
    const Money phasedOut = R26().amtPhaseStart[0] + multiply(R26().amtExemption[0], Decimal::fromInt(2));
    CHECK_EQ(phasedOut, M("680200"));
    TaxReturn rich = single26("1000000");
    CHECK_EQ(calculate(rich).line("1040", "17"), Money());
}

TEST(y2026_unsupported_years) {
    TaxReturn t = single26("50000");
    t.info.year = 2027;
    CHECK(calculate(t).hasErrors());
    CHECK(isSupportedYear(2026));
    CHECK(!isSupportedYear(2024));
}

TEST(y2026_cli) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "opentax_test_cli26";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string file = (dir / "r26.otx").u8string();
    std::string out;
    CHECK_EQ(cli({"-f", file, "new", "year=2026", "first=Pat", "dob=1985-06-15"}, &out), 0);
    CHECK(out.find("2026") != std::string::npos);
    CHECK_EQ(cli({"-f", file, "add", "w2", "wages=60000", "fedwh=6000"}, &out), 0);
    CHECK_EQ(cli({"-f", file, "summary"}, &out), 0);
    CHECK(out.find("Federal refund $977.00") != std::string::npos);
    CHECK_EQ(cli({"-f", (dir / "bad.otx").u8string(), "new", "year=2027"}, &out), 1);
    fs::remove_all(dir);
}


// ------------------------------------------------------------ encryption

// Light settings keep the tests fast; real files use the 256 MiB default.
const KdfParams kTestKdf{8 * 1024, 1, 1};

std::string toHex(const std::uint8_t* data, std::size_t size) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    for (std::size_t i = 0; i < size; ++i) {
        out += digits[data[i] >> 4];
        out += digits[data[i] & 15];
    }
    return out;
}

TEST(crypto_argon2id_rfc9106_vector) {
    // RFC 9106 section 5.3 (Argon2id): t=3, m=32 KiB, p=4, with a secret and associated data.
    std::uint8_t pass[32], salt[16], secret[8], ad[12], tag[32];
    std::memset(pass, 1, sizeof pass);
    std::memset(salt, 2, sizeof salt);
    std::memset(secret, 3, sizeof secret);
    std::memset(ad, 4, sizeof ad);
    std::vector<std::uint8_t> work(32 * 1024);
    crypto_argon2_config config{CRYPTO_ARGON2_ID, 32, 3, 4};
    crypto_argon2_inputs inputs{pass, salt, sizeof pass, sizeof salt};
    crypto_argon2_extras extras{secret, ad, sizeof secret, sizeof ad};
    crypto_argon2(tag, sizeof tag, work.data(), config, inputs, extras);
    CHECK_EQ(toHex(tag, sizeof tag), std::string("0d640df58d78766c08c037a34a8b53c9d01ef0452d75b65eb52520e96b01e659"));
}

TEST(crypto_round_trip) {
    const PasswordKey key = PasswordKey::fromNewPassword("correct horse battery", kTestKdf);
    const std::string secret = "OPENTAX 1\nTAXPAYER\tfirst=Pat\n";
    const std::string file = encryptText(secret, key);
    CHECK(isEncryptedText(file));
    CHECK(file.rfind("OPENTAX-ENCRYPTED 1\nkdf=argon2id memory=8192 passes=1 lanes=1 salt=", 0) == 0);
    CHECK(file.find("Pat") == std::string::npos);
    PasswordKey back;
    CHECK_EQ(decryptText(file, "correct horse battery", &back), secret);
    CHECK(back.key() == key.key());
    CHECK(back.salt() == key.salt());
    // A fresh nonce each time: the same text encrypts differently, and both decrypt.
    const std::string again = encryptText(secret, key);
    CHECK(again != file);
    CHECK_EQ(decryptText(again, "correct horse battery"), secret);
    // Line endings converted by an editor or git still open.
    std::string crlf;
    for (char ch : file) {
        if (ch == '\n') crlf += '\r';
        crlf += ch;
    }
    CHECK_EQ(decryptText(crlf, "correct horse battery"), secret);
    // Empty and non-ASCII contents.
    CHECK_EQ(decryptText(encryptText("", key), "correct horse battery"), std::string());
    const PasswordKey unicode = PasswordKey::fromNewPassword("pässwörd-😀-steuer", kTestKdf);
    CHECK_EQ(decryptText(encryptText("é", unicode), "pässwörd-😀-steuer"), std::string("é"));
}

TEST(crypto_rejects_wrong_password_and_tampering) {
    const PasswordKey key = PasswordKey::fromNewPassword("correct horse battery", kTestKdf);
    const std::string file = encryptText("OPENTAX 1\nINFO\tstatus=mfj\n", key);
    bool wrong = false;
    try {
        decryptText(file, "Correct horse battery");
    } catch (const WrongPassword&) {
        wrong = true;
    }
    CHECK(wrong);
    // Flip one character of the ciphertext.
    std::string tampered = file;
    const std::size_t body = tampered.find('\n', tampered.find("nonce=")) + 1;
    tampered[body + 5] = tampered[body + 5] == 'A' ? 'B' : 'A';
    CHECK_THROWS(decryptText(tampered, "correct horse battery"));
    // Change the authenticated header (same values, but a different nonce).
    std::string header = file;
    const std::size_t n = header.find("nonce=") + 6;
    header[n] = header[n] == '0' ? '1' : '0';
    CHECK_THROWS(decryptText(header, "correct horse battery"));
    // Truncated, garbled or unreasonable files are refused with an error.
    CHECK_THROWS(decryptText(file.substr(0, file.size() / 2), "correct horse battery"));
    CHECK_THROWS(decryptText("OPENTAX-ENCRYPTED 1\n", "x"));
    std::string weak = file;
    weak.replace(weak.find("memory=8192"), 11, "memory=1024");
    CHECK_THROWS(decryptText(weak, "correct horse battery"));
    std::string huge = file;
    huge.replace(huge.find("memory=8192"), 11, "memory=99999999");
    CHECK_THROWS(decryptText(huge, "correct horse battery"));
    std::string algo = file;
    algo.replace(algo.find("argon2id"), 8, "argon2i ");
    CHECK_THROWS(decryptText(algo, "correct horse battery"));
}

TEST(crypto_password_rules) {
    CHECK(!passwordProblem("short").empty());
    CHECK(!passwordProblem("seven77").empty());
    CHECK(passwordProblem("eight888").empty());
    CHECK_THROWS(PasswordKey::fromNewPassword("short", kTestKdf));
    std::uint8_t a[32] = {}, b[32] = {};
    secureRandom(a, sizeof a);
    secureRandom(b, sizeof b);
    CHECK(std::memcmp(a, b, sizeof a) != 0);
    std::string pw = "secret password";
    wipeString(pw);
    CHECK(pw.empty());
}

TEST(crypto_encrypted_return_files) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "opentax_test_crypto";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string path = (dir / "r.otx").u8string();
    TaxReturn t = fullReturn();
    t.save(path);  // plain
    t.save(path);  // plain, with a plain .bak
    CHECK(!TaxReturn::isEncryptedFile(path + ".bak"));

    const PasswordKey key = PasswordKey::fromNewPassword("correct horse battery", kTestKdf);
    t.save(path, &key);
    CHECK(TaxReturn::isEncryptedFile(path));
    // The unencrypted backup is gone, and no plain copy of the old file was made.
    CHECK(!fs::exists(path + ".bak"));
    {
        std::ifstream raw(path, std::ios::binary);  // closed before the next save (Windows can't replace open files)
        const std::string contents((std::istreambuf_iterator<char>(raw)), std::istreambuf_iterator<char>());
        CHECK(contents.find("Pat") == std::string::npos);
    }

    bool required = false;
    try {
        TaxReturn::load(path);
    } catch (const PasswordRequired&) {
        required = true;
    }
    CHECK(required);
    CHECK_THROWS(TaxReturn::load(path, "wrong password"));
    PasswordKey opened;
    const TaxReturn back = TaxReturn::load(path, "correct horse battery", &opened);
    CHECK_EQ(back.serialize(), t.serialize());

    // Saving again with the key from opening keeps it encrypted, and the backup is encrypted.
    t.taxpayer.first = "Changed";
    t.save(path, &opened);
    CHECK(TaxReturn::isEncryptedFile(path + ".bak"));
    CHECK_EQ(TaxReturn::load(path, "correct horse battery").taxpayer.first, std::string("Changed"));
    CHECK_EQ(TaxReturn::load(path + ".bak", "correct horse battery").taxpayer.first, std::string("Pat"));

    // Removing the password writes a plain file again.
    t.save(path);
    CHECK(!TaxReturn::isEncryptedFile(path));
    CHECK_EQ(TaxReturn::load(path).taxpayer.first, std::string("Changed"));
    fs::remove_all(dir);
}

TEST(crypto_cli) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "opentax_test_crypto_cli";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string file = (dir / "c.otx").u8string();
    std::string typed = "correct horse battery";
    int asked = 0;
    const PasswordPrompt prompt = [&](const std::string&) {
        ++asked;
        return typed;
    };
    auto run = [&](std::vector<std::string> args, std::string* output = nullptr) {
        std::ostringstream out, err;
        const int code = runCli(args, out, err, prompt);
        if (output) *output = out.str() + err.str();
        return code;
    };
    std::string out;
    CHECK_EQ(run({"-f", file, "new", "first=Pat", "--encrypt"}, &out), 0);
    CHECK(TaxReturn::isEncryptedFile(file));
    CHECK_EQ(asked, 2);  // new password and confirmation
    CHECK_EQ(run({"-f", file, "add", "w2", "wages=60000", "fedwh=6000"}, &out), 0);
    CHECK(TaxReturn::isEncryptedFile(file));  // saving keeps it encrypted
    CHECK_EQ(run({"-f", file, "summary"}, &out), 0);
    CHECK(out.find("Federal refund $925.00") != std::string::npos);

    typed = "not the password";
    CHECK_EQ(run({"-f", file, "summary"}, &out), 1);
    CHECK(out.find("wrong password") != std::string::npos);
    typed = "";
    CHECK_EQ(run({"-f", file, "summary"}, &out), 1);

    // Change the password: the prompt answers the current one, then the new one twice.
    std::vector<std::string> answers = {"correct horse battery", "a new pass phrase", "a new pass phrase"};
    std::size_t next = 0;
    const PasswordPrompt sequence = [&](const std::string&) { return answers[next++]; };
    std::ostringstream o, e;
    CHECK_EQ(runCli({"-f", file, "encrypt"}, o, e, sequence), 0);
    CHECK(TaxReturn::load(file, "a new pass phrase").w2s.size() == 1);

    typed = "a new pass phrase";
    CHECK_EQ(run({"-f", file, "decrypt"}, &out), 0);
    CHECK(!TaxReturn::isEncryptedFile(file));
    CHECK_EQ(run({"-f", file, "decrypt"}, &out), 1);  // not encrypted any more

    // Mismatched confirmation and weak passwords are refused.
    answers = {"first password", "second password"};
    next = 0;
    CHECK_EQ(runCli({"-f", file, "encrypt"}, o, e, sequence), 1);
    typed = "short";
    CHECK_EQ(run({"-f", file, "encrypt"}, &out), 1);
    CHECK(!TaxReturn::isEncryptedFile(file));
    fs::remove_all(dir);
}

}  // namespace

int main(int argc, char** argv) {
    const std::string only = argc > 1 ? argv[1] : "";
    int run = 0;
    for (const auto& t : registry()) {
        if (!only.empty() && std::string(t.name).find(only) == std::string::npos) continue;
        const int before = g_failures;
        try {
            t.fn();
        } catch (const std::exception& e) {
            ++g_failures;
            std::cerr << t.name << ": unexpected exception: " << e.what() << "\n";
        }
        ++run;
        if (g_failures != before) std::cerr << "  FAILED: " << t.name << "\n";
    }
    std::cout << run << " tests, " << g_checks << " checks, " << g_failures << " failures\n";
    return g_failures == 0 ? 0 : 1;
}
