#pragma once

// The tax return data model: everything the user enters, and nothing that is computed.
//
// Each record type (W-2, 1099-INT, ...) is a plain struct plus a schema: a list of fields
// with a file key, a label (as printed on the source form, e.g. "Box 1 Wages") and a
// pointer to the struct member. The schema drives the file format, the command line and
// the desktop app's form editors, so a field is declared exactly once.

#include "opentax/date.hpp"
#include "opentax/money.hpp"

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace ot {

struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// ------------------------------------------------------------------ enums

enum class FilingStatus { Single, MarriedJoint, MarriedSeparate, HeadOfHousehold, QualifyingSurvivingSpouse };
enum class Owner { Taxpayer, Spouse };
enum class Term { Short, Long };
enum class Relationship { Child, Sibling, Grandchild, Parent, OtherRelative };

struct Choice {
    const char* key;    // stored in files and typed on the command line
    const char* label;  // shown to people
};

const std::vector<Choice>& choices(FilingStatus);
const std::vector<Choice>& choices(Owner);
const std::vector<Choice>& choices(Term);
const std::vector<Choice>& choices(Relationship);

template <class E>
const char* choiceLabel(E value) {
    return choices(value)[static_cast<std::size_t>(value)].label;
}

bool isMarried(FilingStatus s);  // MFJ or MFS

// ---------------------------------------------------------------- records
// Money fields default to zero, which also means "blank".

struct ReturnInfo {
    int year = 2025;
    FilingStatus status = FilingStatus::Single;
    std::string street;
    std::string city;
    std::string state;
    std::string zip;
    bool livedApartAllYear = false;   // MFS only: lived apart from spouse all year
    bool spouseItemizes = false;      // MFS only: spouse itemizes on a separate return
    bool separatedForEic = false;     // MFS only: separated / apart last 6 months (EIC)
    bool forceItemize = false;        // itemize even when the standard deduction is larger
};

struct Person {
    std::string first;
    std::string last;
    std::optional<Date> birthDate;
    bool blind = false;
    bool claimedAsDependent = false;  // someone else can claim this person as a dependent
    bool fullTimeStudent = false;     // affects the saver's credit
    bool hasValidSsn = true;
    std::string occupation;
    Money traditionalIra;             // contributions for the tax year
    Money rothIra;
};

struct Dependent {
    std::string first;
    std::string last;
    std::optional<Date> birthDate;
    Relationship relationship = Relationship::Child;
    int monthsLivedWithYou = 12;
    bool fullTimeStudent = false;
    bool permanentlyDisabled = false;
    bool hasValidSsn = true;          // SSN valid for employment (required for the child tax credit)
    Money careExpenses;               // child/dependent care paid so you could work
};

struct W2 {
    Owner owner = Owner::Taxpayer;
    std::string employer;
    Money wages;                // box 1
    Money federalWithheld;      // box 2
    Money ssWages;              // box 3
    Money ssWithheld;           // box 4
    Money medicareWages;        // box 5
    Money medicareWithheld;     // box 6
    Money ssTips;               // box 7
    Money dependentCare;        // box 10
    Money electiveDeferrals;    // box 12 codes D, E, F, G, H, S, AA, BB, EE
    bool retirementPlan = false;  // box 13
    std::string stateCode;      // box 15
    Money stateWages;           // box 16
    Money stateWithheld;        // box 17
    Money qualifiedTips;        // tips in a listed occupation (Schedule 1-A)
    Money qualifiedOvertime;    // FLSA overtime premium (Schedule 1-A)
};

struct Interest1099 {
    std::string payer;
    Money interest;             // box 1
    Money earlyWithdrawal;      // box 2
    Money usBonds;              // box 3
    Money federalWithheld;      // box 4
    Money taxExempt;            // box 8
};

struct Dividend1099 {
    std::string payer;
    Money ordinary;             // box 1a
    Money qualified;            // box 1b
    Money capitalGainDist;      // box 2a
    Money federalWithheld;      // box 4
    Money section199a;          // box 5
    Money exemptInterest;       // box 12
};

struct CapitalTxn {
    std::string description;
    std::optional<Date> acquired;
    std::optional<Date> sold;
    Money proceeds;
    Money basis;
    Money adjustment;           // e.g. wash sale loss disallowed (positive)
    Term term = Term::Short;
    bool basisReported = true;  // basis reported to the IRS (Form 8949 box A/D)
};

struct Business {
    Owner owner = Owner::Taxpayer;
    std::string name;
    std::string activity;
    Money receipts;             // line 1
    Money returns;              // line 2
    Money costOfGoods;          // line 4
    Money otherIncome;          // line 6
    Money advertising;          // 8
    Money carTruck;             // 9
    Money commissions;          // 10
    Money contractLabor;        // 11
    Money depreciation;         // 13
    Money insurance;            // 15
    Money interest;             // 16
    Money legal;                // 17
    Money office;               // 18
    Money rent;                 // 20
    Money repairs;              // 21
    Money supplies;             // 22
    Money taxesLicenses;        // 23
    Money travel;               // 24a
    Money meals;                // 24b (enter the full amount; 50% is deductible)
    Money utilities;            // 25
    Money wagesPaid;            // 26
    Money otherExpenses;        // 27a
    int homeOfficeSqFt = 0;     // simplified method, line 30
};

struct Retirement1099R {
    Owner owner = Owner::Taxpayer;
    std::string payer;
    Money gross;                // box 1
    Money taxable;              // box 2a
    bool taxableNotDetermined = false;  // box 2b
    Money federalWithheld;      // box 4
    std::string code;           // box 7 distribution code(s)
    bool ira = false;           // IRA/SEP/SIMPLE box
    Money penaltyException;     // part of the taxable amount exempt from the 10% tax
};

struct SocialSecurity {
    Owner owner = Owner::Taxpayer;
    Money benefits;             // box 5
    Money federalWithheld;      // box 6
};

struct Unemployment1099G {
    std::string payer;
    Money compensation;         // box 1
    Money federalWithheld;      // box 4
};

struct OtherIncome {
    std::string description;
    Money amount;
};

struct Education {
    std::string student;
    Money qualifiedExpenses;    // tuition, fees, course materials, less tax-free aid
    bool aotcEligible = true;   // first 4 years, at least half-time, not claimed 4 times, no felony drug conviction
};

struct Adjustments {
    Money educatorTaxpayer;
    Money educatorSpouse;
    Money hsa;                  // deductible HSA contributions (Form 8889 line 13)
    Money seHealthInsurance;
    Money sepSimple;
    Money studentLoanInterest;
    Money carLoanInterest;      // qualified passenger vehicle loan interest (Schedule 1-A)
};

struct Itemized {
    Money medical;
    Money stateIncomeTax;       // paid other than W-2 withholding (estimated payments, prior-year balance)
    bool useSalesTax = false;
    Money salesTax;
    Money realEstateTax;
    Money personalPropertyTax;
    Money otherTaxes;
    Money mortgageInterest;     // Form 1098 box 1
    Money mortgagePoints;       // Form 1098 box 6
    Money investmentInterest;
    Money charityCash;
    Money charityNoncash;
    Money charityCarryover;
    Money otherItemized;
};

struct Payments {
    Money estimated;            // 2025 estimated tax payments
    Money priorYearApplied;     // overpayment applied from the 2024 return
    Money extension;            // paid with an extension request
    Money applyToNextYear;      // part of the refund to apply to 2026 estimated tax
};

struct Carryovers {
    Money shortTermLoss;        // capital loss carryover from 2024 (positive number)
    Money longTermLoss;
    Money qbiLoss;              // qualified business net loss carryforward (positive number)
};

// ---------------------------------------------------------------- schema

template <class T>
using FieldMember = std::variant<std::string T::*, Money T::*, std::optional<Date> T::*, bool T::*, int T::*,
                                 FilingStatus T::*, Owner T::*, Term T::*, Relationship T::*>;

template <class T>
struct Field {
    const char* key;
    const char* label;
    const char* help;
    FieldMember<T> member;
};

template <class T>
struct Schema {
    const char* tag;      // file record type, e.g. "W2"
    const char* command;  // command-line name, e.g. "w2"
    const char* title;    // e.g. "Form W-2"
    std::vector<Field<T>> fields;
};

template <class T>
const Schema<T>& schema();
template <>
const Schema<ReturnInfo>& schema<ReturnInfo>();
template <>
const Schema<Person>& schema<Person>();
template <>
const Schema<Dependent>& schema<Dependent>();
template <>
const Schema<W2>& schema<W2>();
template <>
const Schema<Interest1099>& schema<Interest1099>();
template <>
const Schema<Dividend1099>& schema<Dividend1099>();
template <>
const Schema<CapitalTxn>& schema<CapitalTxn>();
template <>
const Schema<Business>& schema<Business>();
template <>
const Schema<Retirement1099R>& schema<Retirement1099R>();
template <>
const Schema<SocialSecurity>& schema<SocialSecurity>();
template <>
const Schema<Unemployment1099G>& schema<Unemployment1099G>();
template <>
const Schema<OtherIncome>& schema<OtherIncome>();
template <>
const Schema<Education>& schema<Education>();
template <>
const Schema<Adjustments>& schema<Adjustments>();
template <>
const Schema<Itemized>& schema<Itemized>();
template <>
const Schema<Payments>& schema<Payments>();
template <>
const Schema<Carryovers>& schema<Carryovers>();

// Field values as text: money as plain cents ("1234.50"), dates as YYYY-MM-DD, booleans
// as "yes"/"no", choices by key. setField throws ot::Error on invalid input.
template <class T>
std::string getField(const T& record, const Field<T>& field);
template <class T>
void setField(T& record, const Field<T>& field, std::string_view text);
template <class T>
const Field<T>* findField(std::string_view key);

// ----------------------------------------------------------------- return

struct TaxReturn {
    ReturnInfo info;
    Person taxpayer;
    Person spouse;
    Adjustments adjustments;
    Itemized itemized;
    Payments payments;
    Carryovers carryovers;

    std::vector<Dependent> dependents;
    std::vector<W2> w2s;
    std::vector<Interest1099> interest;
    std::vector<Dividend1099> dividends;
    std::vector<CapitalTxn> capitalTxns;
    std::vector<Business> businesses;
    std::vector<Retirement1099R> retirement;
    std::vector<SocialSecurity> socialSecurity;
    std::vector<Unemployment1099G> unemployment;
    std::vector<OtherIncome> otherIncome;
    std::vector<Education> education;

    bool hasSpouse() const { return isMarried(info.status); }
    std::string displayName() const;  // "Jane Doe" or "Jane & John Doe"

    // Serialization. The file is UTF-8 text: an "OPENTAX 1" header, then one record per
    // line: TAG, then tab-separated key=value fields. Unknown tags or keys are errors, so
    // nothing is silently dropped.
    std::string serialize() const;
    static TaxReturn parse(std::string_view text);

    // Atomic save (write temp, keep .bak, rename) and load.
    void save(const std::string& path) const;
    static TaxReturn load(const std::string& path);
};

// Calls f(schema, vector) for every repeated record type, in file order.
template <class F>
void forEachList(TaxReturn& r, F&& f) {
    f(schema<Dependent>(), r.dependents);
    f(schema<W2>(), r.w2s);
    f(schema<Interest1099>(), r.interest);
    f(schema<Dividend1099>(), r.dividends);
    f(schema<CapitalTxn>(), r.capitalTxns);
    f(schema<Business>(), r.businesses);
    f(schema<Retirement1099R>(), r.retirement);
    f(schema<SocialSecurity>(), r.socialSecurity);
    f(schema<Unemployment1099G>(), r.unemployment);
    f(schema<OtherIncome>(), r.otherIncome);
    f(schema<Education>(), r.education);
}
template <class F>
void forEachList(const TaxReturn& r, F&& f) {
    forEachList(const_cast<TaxReturn&>(r), [&](const auto& s, auto& v) { f(s, std::as_const(v)); });
}

// Calls f(schema, record) for every single-instance record. The taxpayer and spouse share
// the Person schema, so a tag is passed explicitly.
template <class F>
void forEachSingle(TaxReturn& r, F&& f) {
    f("INFO", "info", schema<ReturnInfo>(), r.info);
    f("TAXPAYER", "taxpayer", schema<Person>(), r.taxpayer);
    f("SPOUSE", "spouse", schema<Person>(), r.spouse);
    f("ADJUSTMENTS", "adjustments", schema<Adjustments>(), r.adjustments);
    f("ITEMIZED", "itemized", schema<Itemized>(), r.itemized);
    f("PAYMENTS", "payments", schema<Payments>(), r.payments);
    f("CARRYOVERS", "carryovers", schema<Carryovers>(), r.carryovers);
}

}  // namespace ot
