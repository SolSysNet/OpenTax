#include "opentax/model.hpp"

#include "opentax/util.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <type_traits>
#include <utility>

namespace ot {

namespace fs = std::filesystem;

// ------------------------------------------------------------------ enums

const std::vector<Choice>& choices(FilingStatus) {
    static const std::vector<Choice> c = {
        {"single", "Single"},
        {"mfj", "Married filing jointly"},
        {"mfs", "Married filing separately"},
        {"hoh", "Head of household"},
        {"qss", "Qualifying surviving spouse"},
    };
    return c;
}

const std::vector<Choice>& choices(Owner) {
    static const std::vector<Choice> c = {{"taxpayer", "You"}, {"spouse", "Spouse"}};
    return c;
}

const std::vector<Choice>& choices(Term) {
    static const std::vector<Choice> c = {{"short", "Short-term (held 1 year or less)"},
                                          {"long", "Long-term (held more than 1 year)"}};
    return c;
}

const std::vector<Choice>& choices(Relationship) {
    static const std::vector<Choice> c = {
        {"child", "Son, daughter, stepchild or foster child"},
        {"sibling", "Brother, sister, half- or step-sibling"},
        {"grandchild", "Grandchild, niece, nephew (descendant of the above)"},
        {"parent", "Parent or grandparent"},
        {"other", "Other relative or household member"},
    };
    return c;
}

bool isMarried(FilingStatus s) { return s == FilingStatus::MarriedJoint || s == FilingStatus::MarriedSeparate; }

// ---------------------------------------------------------------- schemas

#define F(T, key, label, help, member) Field<T>{key, label, help, &T::member}

template <>
const Schema<ReturnInfo>& schema<ReturnInfo>() {
    using T = ReturnInfo;
    static const Schema<T> s{"INFO", "info", "Return", {
        F(T, "year", "Tax year", "Only 2025 is supported by this version.", year),
        F(T, "status", "Filing status", "", status),
        F(T, "street", "Street address", "", street),
        F(T, "city", "City", "", city),
        F(T, "state", "State", "Two-letter code", state),
        F(T, "zip", "ZIP code", "", zip),
        F(T, "livedapart", "Lived apart from spouse all year", "Married filing separately only. Changes how Social Security benefits and IRA deductions are figured.", livedApartAllYear),
        F(T, "spouseitemizes", "Spouse itemizes deductions", "Married filing separately only. If your spouse itemizes, your standard deduction is zero.", spouseItemizes),
        F(T, "eicseparated", "Separated from spouse for EIC", "Married filing separately only. You lived apart from your spouse for the last 6 months of the year (or are legally separated) and have a qualifying child.", separatedForEic),
        F(T, "forceitemize", "Itemize even if smaller", "Use Schedule A even when the standard deduction is larger (for example, to match a state return).", forceItemize),
    }};
    return s;
}

template <>
const Schema<Person>& schema<Person>() {
    using T = Person;
    static const Schema<T> s{"PERSON", "person", "Person", {
        F(T, "first", "First name", "", first),
        F(T, "last", "Last name", "", last),
        F(T, "dob", "Date of birth", "Used for the extra standard deduction and senior deduction (65+), IRA catch-up (50+) and the EIC age test.", birthDate),
        F(T, "blind", "Legally blind", "", blind),
        F(T, "dependent", "Can be claimed as someone's dependent", "Check if a parent or anyone else can claim this person as a dependent.", claimedAsDependent),
        F(T, "student", "Full-time student", "Enrolled full-time for part of 5 months of the year. Affects the saver's credit.", fullTimeStudent),
        F(T, "ssn", "Has an SSN valid for work", "Required for the senior, tips and overtime deductions and the EIC.", hasValidSsn),
        F(T, "occupation", "Occupation", "", occupation),
        F(T, "tradira", "Traditional IRA contributions", "Contributions for 2025, including those made by April 15, 2026.", traditionalIra),
        F(T, "rothira", "Roth IRA contributions", "Used for the saver's credit only.", rothIra),
    }};
    return s;
}

template <>
const Schema<Dependent>& schema<Dependent>() {
    using T = Dependent;
    static const Schema<T> s{"DEPENDENT", "dependent", "Dependent", {
        F(T, "first", "First name", "", first),
        F(T, "last", "Last name", "", last),
        F(T, "dob", "Date of birth", "", birthDate),
        F(T, "rel", "Relationship", "", relationship),
        F(T, "months", "Months lived with you", "Count temporary absences (school, medical care) as time lived with you. A child born or who died during the year counts as 12.", monthsLivedWithYou),
        F(T, "student", "Full-time student", "", fullTimeStudent),
        F(T, "disabled", "Permanently disabled", "", permanentlyDisabled),
        F(T, "ssn", "Has an SSN valid for work", "Needed for the child tax credit and EIC. Otherwise the $500 credit for other dependents may apply.", hasValidSsn),
        F(T, "care", "Care expenses paid", "Child or dependent care paid in 2025 so you (and your spouse) could work or look for work.", careExpenses),
    }};
    return s;
}

template <>
const Schema<W2>& schema<W2>() {
    using T = W2;
    static const Schema<T> s{"W2", "w2", "Form W-2", {
        F(T, "owner", "Employee", "", owner),
        F(T, "employer", "Employer name", "Box c", employer),
        F(T, "wages", "Box 1  Wages, tips, other comp.", "", wages),
        F(T, "fedwh", "Box 2  Federal income tax withheld", "", federalWithheld),
        F(T, "sswages", "Box 3  Social security wages", "", ssWages),
        F(T, "sswh", "Box 4  Social security tax withheld", "", ssWithheld),
        F(T, "medwages", "Box 5  Medicare wages and tips", "", medicareWages),
        F(T, "medwh", "Box 6  Medicare tax withheld", "", medicareWithheld),
        F(T, "sstips", "Box 7  Social security tips", "", ssTips),
        F(T, "depcare", "Box 10  Dependent care benefits", "", dependentCare),
        F(T, "deferrals", "Box 12  Elective deferrals", "Total of box 12 codes D, E, F, G, H, S, AA, BB and EE (401(k), 403(b), 457(b), TSP, SIMPLE, Roth). Used for the saver's credit.", electiveDeferrals),
        F(T, "retplan", "Box 13  Retirement plan", "Checked if you were covered by a workplace retirement plan. Limits the IRA deduction.", retirementPlan),
        F(T, "state", "Box 15  State", "", stateCode),
        F(T, "statewages", "Box 16  State wages", "", stateWages),
        F(T, "statewh", "Box 17  State income tax", "Counted as state income tax paid on Schedule A.", stateWithheld),
        F(T, "tips", "Qualified tips (Sch. 1-A)", "Tips received in an occupation on the IRS list of tipped occupations, generally included in box 7. Deductible up to $25,000.", qualifiedTips),
        F(T, "overtime", "Qualified overtime (Sch. 1-A)", "The premium part (the \"half\" in time-and-a-half) of FLSA overtime included in box 1. Your employer may report it in box 14 or separately.", qualifiedOvertime),
    }};
    return s;
}

template <>
const Schema<Interest1099>& schema<Interest1099>() {
    using T = Interest1099;
    static const Schema<T> s{"1099INT", "1099int", "Form 1099-INT", {
        F(T, "payer", "Payer", "", payer),
        F(T, "interest", "Box 1  Interest income", "", interest),
        F(T, "earlywd", "Box 2  Early withdrawal penalty", "Deducted on Schedule 1, line 18.", earlyWithdrawal),
        F(T, "usbonds", "Box 3  U.S. Savings Bonds and Treasury interest", "", usBonds),
        F(T, "fedwh", "Box 4  Federal income tax withheld", "", federalWithheld),
        F(T, "exempt", "Box 8  Tax-exempt interest", "", taxExempt),
    }};
    return s;
}

template <>
const Schema<Dividend1099>& schema<Dividend1099>() {
    using T = Dividend1099;
    static const Schema<T> s{"1099DIV", "1099div", "Form 1099-DIV", {
        F(T, "payer", "Payer", "", payer),
        F(T, "ordinary", "Box 1a  Total ordinary dividends", "", ordinary),
        F(T, "qualified", "Box 1b  Qualified dividends", "", qualified),
        F(T, "capgain", "Box 2a  Total capital gain distributions", "", capitalGainDist),
        F(T, "fedwh", "Box 4  Federal income tax withheld", "", federalWithheld),
        F(T, "sec199a", "Box 5  Section 199A dividends", "Qualified REIT dividends, eligible for the 20% QBI deduction.", section199a),
        F(T, "exempt", "Box 12  Exempt-interest dividends", "", exemptInterest),
    }};
    return s;
}

template <>
const Schema<CapitalTxn>& schema<CapitalTxn>() {
    using T = CapitalTxn;
    static const Schema<T> s{"CAPGAIN", "capgain", "Sale (Form 1099-B)", {
        F(T, "desc", "Description", "e.g. 100 sh XYZ", description),
        F(T, "acquired", "Date acquired", "", acquired),
        F(T, "sold", "Date sold", "", sold),
        F(T, "proceeds", "Proceeds", "Box 1d", proceeds),
        F(T, "basis", "Cost or other basis", "Box 1e", basis),
        F(T, "adjust", "Adjustment", "e.g. box 1g wash sale loss disallowed. Positive adjustments increase the gain.", adjustment),
        F(T, "term", "Holding period", "", term),
        F(T, "basisreported", "Basis reported to IRS", "Box 12. Determines Form 8949 box A/D versus B/E.", basisReported),
    }};
    return s;
}

template <>
const Schema<Business>& schema<Business>() {
    using T = Business;
    static const Schema<T> s{"SCHC", "business", "Schedule C business", {
        F(T, "owner", "Proprietor", "", owner),
        F(T, "name", "Business name", "", name),
        F(T, "activity", "Principal business or profession", "", activity),
        F(T, "receipts", "1  Gross receipts or sales", "Include Form 1099-NEC box 1 and 1099-K amounts.", receipts),
        F(T, "returns", "2  Returns and allowances", "", returns),
        F(T, "cogs", "4  Cost of goods sold", "", costOfGoods),
        F(T, "otherincome", "6  Other income", "", otherIncome),
        F(T, "advertising", "8  Advertising", "", advertising),
        F(T, "car", "9  Car and truck expenses", "", carTruck),
        F(T, "commissions", "10  Commissions and fees", "", commissions),
        F(T, "contract", "11  Contract labor", "", contractLabor),
        F(T, "depreciation", "13  Depreciation and section 179", "", depreciation),
        F(T, "insurance", "15  Insurance (other than health)", "", insurance),
        F(T, "interest", "16  Interest", "", interest),
        F(T, "legal", "17  Legal and professional services", "", legal),
        F(T, "office", "18  Office expense", "", office),
        F(T, "rent", "20  Rent or lease", "", rent),
        F(T, "repairs", "21  Repairs and maintenance", "", repairs),
        F(T, "supplies", "22  Supplies", "", supplies),
        F(T, "taxes", "23  Taxes and licenses", "", taxesLicenses),
        F(T, "travel", "24a  Travel", "", travel),
        F(T, "meals", "24b  Business meals (full amount)", "Enter what you paid. Only 50% is deductible, and OpenTax applies that for you.", meals),
        F(T, "utilities", "25  Utilities", "", utilities),
        F(T, "wages", "26  Wages paid", "", wagesPaid),
        F(T, "other", "27a  Other expenses", "", otherExpenses),
        F(T, "homeoffice", "30  Home office square feet", "Simplified method: $5 per square foot, up to 300 square feet, limited to the business's profit.", homeOfficeSqFt),
    }};
    return s;
}

template <>
const Schema<Retirement1099R>& schema<Retirement1099R>() {
    using T = Retirement1099R;
    static const Schema<T> s{"1099R", "1099r", "Form 1099-R", {
        F(T, "owner", "Recipient", "", owner),
        F(T, "payer", "Payer", "", payer),
        F(T, "gross", "Box 1  Gross distribution", "", gross),
        F(T, "taxable", "Box 2a  Taxable amount", "", taxable),
        F(T, "notdetermined", "Box 2b  Taxable amount not determined", "If checked, OpenTax treats the whole distribution as taxable unless you enter the taxable part in box 2a.", taxableNotDetermined),
        F(T, "fedwh", "Box 4  Federal income tax withheld", "", federalWithheld),
        F(T, "code", "Box 7  Distribution code(s)", "Code 1 means an early distribution (10% additional tax). Code G is a direct rollover.", code),
        F(T, "ira", "IRA/SEP/SIMPLE", "Box 7 checkbox. IRA distributions go on line 4; pensions and annuities on line 5.", ira),
        F(T, "exception", "Amount exempt from 10% tax", "The part of an early distribution that qualifies for an exception (Form 5329, line 2).", penaltyException),
    }};
    return s;
}

template <>
const Schema<SocialSecurity>& schema<SocialSecurity>() {
    using T = SocialSecurity;
    static const Schema<T> s{"SSA1099", "ssa1099", "Form SSA-1099", {
        F(T, "owner", "Beneficiary", "", owner),
        F(T, "benefits", "Box 5  Net benefits", "", benefits),
        F(T, "fedwh", "Box 6  Voluntary federal tax withheld", "", federalWithheld),
    }};
    return s;
}

template <>
const Schema<Unemployment1099G>& schema<Unemployment1099G>() {
    using T = Unemployment1099G;
    static const Schema<T> s{"1099G", "1099g", "Form 1099-G", {
        F(T, "payer", "Payer", "", payer),
        F(T, "comp", "Box 1  Unemployment compensation", "", compensation),
        F(T, "fedwh", "Box 4  Federal income tax withheld", "", federalWithheld),
    }};
    return s;
}

template <>
const Schema<OtherIncome>& schema<OtherIncome>() {
    using T = OtherIncome;
    static const Schema<T> s{"OTHERINCOME", "otherincome", "Other income", {
        F(T, "desc", "Description", "e.g. prizes, jury duty pay, hobby income", description),
        F(T, "amount", "Amount", "Reported on Schedule 1, line 8z.", amount),
    }};
    return s;
}

template <>
const Schema<Education>& schema<Education>() {
    using T = Education;
    static const Schema<T> s{"EDUCATION", "education", "Education expenses (Form 1098-T)", {
        F(T, "student", "Student name", "You, your spouse or a dependent.", student),
        F(T, "expenses", "Qualified expenses", "Tuition, required fees and course materials, minus tax-free scholarships and grants.", qualifiedExpenses),
        F(T, "aotc", "Eligible for American opportunity credit", "First 4 years of college, at least half-time, credit not claimed for 4 earlier years, and no felony drug conviction. Otherwise the lifetime learning credit is used.", aotcEligible),
    }};
    return s;
}

template <>
const Schema<Adjustments>& schema<Adjustments>() {
    using T = Adjustments;
    static const Schema<T> s{"ADJUSTMENTS", "adjustments", "Adjustments", {
        F(T, "educator", "Educator expenses (you)", "K-12 teachers and aides: up to $300.", educatorTaxpayer),
        F(T, "educatorspouse", "Educator expenses (spouse)", "Up to $300. Joint returns only.", educatorSpouse),
        F(T, "hsa", "HSA deduction", "Your own contributions to a health savings account (Form 8889, line 13). Don't include employer contributions (W-2 code W).", hsa),
        F(T, "sehealth", "Self-employed health insurance", "Premiums for you, your spouse and dependents, if you had self-employment income and no subsidized employer plan.", seHealthInsurance),
        F(T, "sep", "SEP, SIMPLE and qualified plans", "Your contributions to your own self-employed retirement plan.", sepSimple),
        F(T, "studentloan", "Student loan interest", "Form 1098-E box 1. Up to $2,500.", studentLoanInterest),
        F(T, "carloan", "Car loan interest (Sch. 1-A)", "Interest on a loan taken out after 2024 for a new personal-use car, SUV, van, pickup or motorcycle assembled in the U.S. Up to $10,000.", carLoanInterest),
    }};
    return s;
}

template <>
const Schema<Itemized>& schema<Itemized>() {
    using T = Itemized;
    static const Schema<T> s{"ITEMIZED", "itemized", "Itemized deductions", {
        F(T, "medical", "Medical and dental expenses", "Unreimbursed. Only the part above 7.5% of AGI counts.", medical),
        F(T, "stateincome", "State/local income tax paid", "Other than W-2 box 17, which is added automatically: estimated payments and balances paid in 2025.", stateIncomeTax),
        F(T, "usesales", "Deduct sales tax instead", "Use general sales taxes instead of income taxes.", useSalesTax),
        F(T, "salestax", "General sales taxes", "", salesTax),
        F(T, "realestate", "Real estate taxes", "", realEstateTax),
        F(T, "personalprop", "Personal property taxes", "The value-based part of car registration fees, for example.", personalPropertyTax),
        F(T, "othertaxes", "Other taxes", "", otherTaxes),
        F(T, "mortgage", "Home mortgage interest (1098)", "Form 1098 box 1. If your mortgage is over $750,000, enter only the deductible part.", mortgageInterest),
        F(T, "points", "Points (1098)", "Form 1098 box 6.", mortgagePoints),
        F(T, "investint", "Investment interest", "Limited to net investment income.", investmentInterest),
        F(T, "charitycash", "Gifts by cash or check", "", charityCash),
        F(T, "charitynoncash", "Gifts other than cash", "Fair market value of donated goods.", charityNoncash),
        F(T, "charitycarry", "Charity carryover from prior year", "", charityCarryover),
        F(T, "other", "Other itemized deductions", "Gambling losses (up to winnings), and other items on the Schedule A line 16 list.", otherItemized),
    }};
    return s;
}

template <>
const Schema<Payments>& schema<Payments>() {
    using T = Payments;
    static const Schema<T> s{"PAYMENTS", "payments", "Payments", {
        F(T, "estimated", "2025 estimated tax payments", "Total of your Form 1040-ES payments.", estimated),
        F(T, "prioryear", "Applied from 2024 return", "Overpayment from last year you applied to 2025.", priorYearApplied),
        F(T, "extension", "Paid with extension", "Amount paid with Form 4868.", extension),
        F(T, "applynext", "Apply to 2026 estimated tax", "Part of your refund to keep with the IRS for next year.", applyToNextYear),
    }};
    return s;
}

template <>
const Schema<Carryovers>& schema<Carryovers>() {
    using T = Carryovers;
    static const Schema<T> s{"CARRYOVERS", "carryovers", "Carryovers from 2024", {
        F(T, "stloss", "Short-term capital loss carryover", "From your 2024 Capital Loss Carryover Worksheet, line 8.", shortTermLoss),
        F(T, "ltloss", "Long-term capital loss carryover", "From your 2024 Capital Loss Carryover Worksheet, line 13.", longTermLoss),
        F(T, "qbiloss", "QBI net loss carryforward", "From your 2024 Form 8995, line 16.", qbiLoss),
    }};
    return s;
}

#undef F

// ------------------------------------------------------- field get / set

namespace {

template <class E>
std::string choiceKey(E value) {
    return choices(value)[static_cast<std::size_t>(value)].key;
}

template <class E>
E parseChoice(std::string_view text) {
    const auto& list = choices(E{});
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (iequals(text, list[i].key) || iequals(text, list[i].label)) return static_cast<E>(i);
    }
    std::string keys;
    for (const auto& c : list) keys += std::string(keys.empty() ? "" : ", ") + c.key;
    throw Error("'" + std::string(text) + "' is not one of: " + keys);
}

bool parseBool(std::string_view text) {
    const std::string t = toLower(trim(text));
    if (t == "yes" || t == "y" || t == "true" || t == "1" || t == "x") return true;
    if (t == "no" || t == "n" || t == "false" || t == "0" || t.empty()) return false;
    throw Error("'" + std::string(text) + "' is not yes or no");
}

}  // namespace

template <class T>
std::string getField(const T& record, const Field<T>& field) {
    return std::visit(
        [&](auto member) -> std::string {
            const auto& v = record.*member;
            using V = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<V, std::string>) return v;
            else if constexpr (std::is_same_v<V, Money>) return v.isZero() ? std::string() : v.str();
            else if constexpr (std::is_same_v<V, std::optional<Date>>) return v ? v->str() : std::string();
            else if constexpr (std::is_same_v<V, bool>) return v ? "yes" : "no";
            else if constexpr (std::is_same_v<V, int>) return std::to_string(v);
            else return choiceKey(v);
        },
        field.member);
}

template <class T>
void setField(T& record, const Field<T>& field, std::string_view text) {
    std::visit(
        [&](auto member) {
            auto& v = record.*member;
            using V = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<V, std::string>) {
                v = trim(text);
            } else if constexpr (std::is_same_v<V, Money>) {
                if (trim(text).empty()) {
                    v = Money();
                    return;
                }
                auto m = Money::parse(text);
                if (!m) throw Error(std::string(field.key) + ": '" + std::string(text) + "' is not an amount");
                v = *m;
            } else if constexpr (std::is_same_v<V, std::optional<Date>>) {
                if (trim(text).empty()) {
                    v.reset();
                    return;
                }
                auto d = Date::parse(text);
                if (!d) throw Error(std::string(field.key) + ": '" + std::string(text) + "' is not a date (YYYY-MM-DD)");
                v = *d;
            } else if constexpr (std::is_same_v<V, bool>) {
                v = parseBool(text);
            } else if constexpr (std::is_same_v<V, int>) {
                auto n = parseInt(text);
                if (!n || *n < 0 || *n > 1000000) throw Error(std::string(field.key) + ": '" + std::string(text) + "' is not a whole number");
                v = static_cast<int>(*n);
            } else {
                v = parseChoice<V>(trim(text));
            }
        },
        field.member);
}

template <class T>
const Field<T>* findField(std::string_view key) {
    for (const auto& f : schema<T>().fields) {
        if (iequals(key, f.key)) return &f;
    }
    return nullptr;
}

#define OPENTAX_INSTANTIATE(T)                                                   \
    template std::string getField<T>(const T&, const Field<T>&);                 \
    template void setField<T>(T&, const Field<T>&, std::string_view);            \
    template const Field<T>* findField<T>(std::string_view);

OPENTAX_INSTANTIATE(ReturnInfo)
OPENTAX_INSTANTIATE(Person)
OPENTAX_INSTANTIATE(Dependent)
OPENTAX_INSTANTIATE(W2)
OPENTAX_INSTANTIATE(Interest1099)
OPENTAX_INSTANTIATE(Dividend1099)
OPENTAX_INSTANTIATE(CapitalTxn)
OPENTAX_INSTANTIATE(Business)
OPENTAX_INSTANTIATE(Retirement1099R)
OPENTAX_INSTANTIATE(SocialSecurity)
OPENTAX_INSTANTIATE(Unemployment1099G)
OPENTAX_INSTANTIATE(OtherIncome)
OPENTAX_INSTANTIATE(Education)
OPENTAX_INSTANTIATE(Adjustments)
OPENTAX_INSTANTIATE(Itemized)
OPENTAX_INSTANTIATE(Payments)
OPENTAX_INSTANTIATE(Carryovers)

#undef OPENTAX_INSTANTIATE

// --------------------------------------------------------- serialization

namespace {

constexpr const char* kHeader = "OPENTAX 1";

std::string escape(std::string_view s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '\t': out += "\\t"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            default: out += c;
        }
    }
    return out;
}

std::string unescape(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\' || i + 1 == s.size()) {
            out += s[i];
            continue;
        }
        const char n = s[++i];
        out += n == 't' ? '\t' : n == 'n' ? '\n' : n == 'r' ? '\r' : n;
    }
    return out;
}

// Writes only fields that differ from a default-constructed record.
template <class T>
std::string recordLine(const char* tag, const Schema<T>& s, const T& record) {
    static const T blank{};
    std::string line = tag;
    for (const auto& f : s.fields) {
        const std::string value = getField(record, f);
        if (value == getField(blank, f)) continue;
        line += '\t';
        line += f.key;
        line += '=';
        line += escape(value);
    }
    return line;
}

template <class T>
void applyFields(T& record, const Schema<T>& s, const std::vector<std::string>& parts, int lineNo) {
    for (std::size_t i = 1; i < parts.size(); ++i) {
        const auto eq = parts[i].find('=');
        if (eq == std::string::npos) throw Error("line " + std::to_string(lineNo) + ": expected key=value");
        const std::string key = parts[i].substr(0, eq);
        const Field<T>* f = nullptr;
        for (const auto& candidate : s.fields) {
            if (key == candidate.key) f = &candidate;
        }
        if (!f) throw Error("line " + std::to_string(lineNo) + ": unknown field '" + key + "' in " + parts[0]);
        try {
            setField(record, *f, unescape(std::string_view(parts[i]).substr(eq + 1)));
        } catch (const Error& e) {
            throw Error("line " + std::to_string(lineNo) + ": " + e.what());
        }
    }
}

}  // namespace

std::string TaxReturn::serialize() const {
    std::string out = std::string(kHeader) + "\n";
    auto& self = const_cast<TaxReturn&>(*this);
    forEachSingle(self, [&](const char* tag, const char*, const auto& s, const auto& record) {
        const std::string line = recordLine(tag, s, record);
        if (line != tag || std::string(tag) == "INFO") out += line + "\n";
    });
    forEachList(*this, [&](const auto& s, const auto& items) {
        for (const auto& item : items) out += recordLine(s.tag, s, item) + "\n";
    });
    return out;
}

TaxReturn TaxReturn::parse(std::string_view text) {
    TaxReturn r;
    std::istringstream in{std::string(text)};
    std::string line;
    int lineNo = 0;
    bool sawHeader = false;
    while (std::getline(in, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (lineNo == 1 && line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF) line.erase(0, 3);  // BOM
        if (!sawHeader) {
            if (line != kHeader) throw Error("not an OpenTax file (missing '" + std::string(kHeader) + "' header)");
            sawHeader = true;
            continue;
        }
        if (trim(line).empty() || line[0] == '#') continue;
        const std::vector<std::string> parts = split(line, '\t');
        const std::string& tag = parts[0];
        bool handled = false;
        forEachSingle(r, [&](const char* t, const char*, const auto& s, auto& record) {
            if (handled || tag != t) return;
            applyFields(record, s, parts, lineNo);
            handled = true;
        });
        forEachList(r, [&](const auto& s, auto& items) {
            if (handled || tag != s.tag) return;
            items.emplace_back();
            applyFields(items.back(), s, parts, lineNo);
            handled = true;
        });
        if (!handled) throw Error("line " + std::to_string(lineNo) + ": unknown record type '" + tag + "'");
    }
    if (!sawHeader) throw Error("empty file");
    return r;
}

void TaxReturn::save(const std::string& path) const {
    const fs::path target = fs::u8path(path);
    fs::path tmp = target;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) throw Error("cannot write '" + tmp.u8string() + "'");
        out << serialize();
        out.flush();
        if (!out) throw Error("failed while writing '" + tmp.u8string() + "'");
    }
    std::error_code ec;
    if (fs::exists(target, ec)) {
        fs::path backup = target;
        backup += ".bak";
        fs::copy_file(target, backup, fs::copy_options::overwrite_existing, ec);
    }
    fs::rename(tmp, target, ec);
    if (ec) {  // some platforms refuse to rename over an existing file
        fs::remove(target, ec);
        ec.clear();
        fs::rename(tmp, target, ec);
        if (ec) throw Error("cannot replace '" + target.u8string() + "': " + ec.message());
    }
}

TaxReturn TaxReturn::load(const std::string& path) {
    std::ifstream in(fs::u8path(path), std::ios::binary);
    if (!in) throw Error("cannot open '" + path + "'");
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return parse(buffer.str());
}

std::string TaxReturn::displayName() const {
    std::string tp = trim(taxpayer.first + " " + taxpayer.last);
    if (info.status != FilingStatus::MarriedJoint || trim(spouse.first).empty()) return tp;
    if (spouse.last == taxpayer.last || trim(spouse.last).empty())
        return trim(taxpayer.first + " & " + spouse.first + " " + taxpayer.last);
    return tp + " & " + trim(spouse.first + " " + spouse.last);
}

}  // namespace ot
