#include "opentax/calc.hpp"

#include "opentax/util.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <deque>
#include <map>

namespace ot {

// ----------------------------------------------------------- result helpers

const Line* FormResult::find(std::string_view number) const {
    for (const auto& l : lines) {
        if (l.number == number) return &l;
    }
    return nullptr;
}

Money FormResult::get(std::string_view number) const {
    const Line* l = find(number);
    return l ? l->amount : Money();
}

const FormResult* Result::form(std::string_view id) const {
    for (const auto& f : forms) {
        if (f.id == id) return &f;
    }
    return nullptr;
}

Money Result::line(std::string_view formId, std::string_view number) const {
    const FormResult* f = form(formId);
    return f ? f->get(number) : Money();
}

bool Result::hasErrors() const {
    return std::any_of(diagnostics.begin(), diagnostics.end(),
                       [](const Diagnostic& d) { return d.severity == Severity::Error; });
}

// ------------------------------------------------------------ money helpers

namespace {

constexpr Money kZero{};

Money dollars(long long d) { return Money::fromCents(d * 100); }
Money maxOf(Money a, Money b) { return a > b ? a : b; }
Money minOf(Money a, Money b) { return a < b ? a : b; }
Money pos(Money a) { return a > kZero ? a : kZero; }
Money pct(Money amount, int percent) { return percentOf(amount, Decimal::fromInt(percent)); }
Money pct(Money amount, Decimal percent) { return percentOf(amount, percent); }
Money pct(Money amount, const char* percent) { return percentOf(amount, *Decimal::parse(percent)); }

// Nearest whole dollar, halves rounded up (tax amounts are never negative here).
Money roundToDollar(Money m) {
    const std::int64_t c = m.cents();
    const std::int64_t q = c >= 0 ? (c + 50) / 100 : -((-c + 50) / 100);
    return dollars(q);
}

// Whole thousands in `m`, rounded down or up.
long long thousandsDown(Money m) { return m.cents() <= 0 ? 0 : m.cents() / 100000; }
long long thousandsUp(Money m) { return m.cents() <= 0 ? 0 : (m.cents() + 99999) / 100000; }

// A ratio num/den as a decimal rounded to three places, capped at 1.000, in thousandths.
std::int64_t ratio3(Money num, Money den) {
    if (den.cents() <= 0 || num.cents() <= 0) return 0;
    if (num >= den) return 1000;
    const std::int64_t n = num.cents() * 1000;
    return (n + den.cents() / 2) / den.cents();
}

Money times3(Money amount, std::int64_t thousandths) {
    return multiply(amount, Decimal::fromRaw(thousandths * (Decimal::kScale / 1000)));
}

std::string ratioText(std::int64_t thousandths) {
    std::string s = std::to_string(thousandths / 1000) + ".";
    std::string frac = std::to_string(thousandths % 1000);
    s += std::string(3 - frac.size(), '0') + frac;
    return s;
}

std::string usd(Money m) {
    std::string s = m.formatted();
    if (!s.empty() && s[0] == '-') return "-$" + s.substr(1);
    return "$" + s;
}

std::string plural(std::size_t n, const char* one, const char* many) {
    return std::to_string(n) + " " + (n == 1 ? one : many);
}

// Tax at the given ordinary rates, exact to the cent (as the Tax Computation Worksheet).
Money bracketTax(Money taxable, const std::vector<Bracket>& brackets) {
    if (taxable <= kZero) return kZero;
    // Accumulate cents * percent exactly, then round once.
    std::int64_t total = 0;
    std::int64_t lower = 0;
    for (std::size_t i = 0; i < brackets.size(); ++i) {
        const bool last = i + 1 == brackets.size();
        const std::int64_t upper = last ? taxable.cents() : std::min(taxable.cents(), brackets[i].upTo.cents());
        if (upper > lower) total += (upper - lower) * brackets[i].ratePercent;
        if (!last && taxable.cents() <= brackets[i].upTo.cents()) break;
        lower = brackets[i].upTo.cents();
    }
    return Money::fromCents((total + 50) / 100);
}

bool isQualifyingRelationship(Relationship r) {
    return r == Relationship::Child || r == Relationship::Sibling || r == Relationship::Grandchild;
}

}  // namespace

Money incomeTax(Money taxable, FilingStatus status, const Rules& rules) {
    const auto& brackets = pick(rules.brackets, status);
    if (taxable <= kZero) return kZero;
    if (taxable >= dollars(100000)) return bracketTax(taxable, brackets);
    const long long d = taxable.cents() / 100;  // table rows are whole-dollar ranges
    std::int64_t midCents;
    if (d < 5) return kZero;
    if (d < 15) midCents = 1000;
    else if (d < 25) midCents = 2000;
    else if (d < 3000) midCents = ((d / 25) * 25) * 100 + 1250;
    else midCents = ((d / 50) * 50) * 100 + 2500;
    return roundToDollar(bracketTax(Money::fromCents(midCents), brackets));
}

Money eicTable(Money amount, int children, bool joint, const Rules& rules) {
    if (amount < dollars(1)) return kZero;
    const EicColumn& c = rules.eic[static_cast<std::size_t>(std::clamp(children, 0, 3))];
    const long long d = amount.cents() / 100;
    // Rows: $1-$49, then $50 bands. The first row's midpoint is $25.
    const Money mid = d < 50 ? dollars(25) : Money::fromCents(((d / 50) * 50) * 100 + 2500);
    Money credit = minOf(pct(mid, c.ratePercent), c.maxCredit);
    const Money start = joint ? c.phaseoutStartJoint : c.phaseoutStart;
    if (mid > start) credit -= pct(mid - start, c.phaseoutPercent);
    return pos(roundToDollar(pos(credit)));
}

int ageAtEndOfYear(Date birth, int year) { return year - birth.year(); }

int irsAgeAtEndOfYear(Date birth, int year) {
    // You reach an age the day before your birthday, so the age on December 31 is the
    // ordinary age on January 1 of the next year: a January 1 birthday counts early.
    const bool janFirst = birth.month() == 1 && birth.day() == 1;
    return year - birth.year() + (janFirst ? 1 : 0);
}

// ------------------------------------------------------------------ engine

namespace {

enum Order {
    kOrder1040 = 0,
    kOrderSch1,
    kOrderSch1A,
    kOrderSch2,
    kOrderSch3,
    kOrderSchA,
    kOrderSchB,
    kOrderSchC,
    kOrderSchD,
    kOrder8949,
    kOrderSchSE,
    kOrder8812,
    kOrder2441,
    kOrder6251,
    kOrder8863,
    kOrder8880,
    kOrder8959,
    kOrder8960,
    kOrder8995,
    kOrderWorksheet,
};

struct Builder {
    FormResult form;
    int order = 0;

    Money add(const std::string& number, const std::string& label, Money amount, const std::string& how = {}) {
        form.lines.push_back({number, label, amount, {}, how});
        return amount;
    }
    void text(const std::string& number, const std::string& label, const std::string& value, const std::string& how = {}) {
        form.lines.push_back({number, label, kZero, value, how});
    }
};

class Calculator {
public:
    explicit Calculator(const TaxReturn& r) : r_(r), rules_(rulesFor(r.info.year)), st_(r.info.status) {}

    Result run() {
        validate();
        businesses();
        selfEmployment();
        careBenefits();
        income();
        adjustmentsAndSocialSecurity();
        deductions();
        schedule1A();
        qbi();
        itemizedLimitation();
        taxableIncome();
        tax();
        amt();
        nonrefundableCredits();
        otherTaxes();
        refundableCreditsAndPayments();
        finish();

        tidy();
        std::stable_sort(builders_.begin(), builders_.end(),
                         [](const Builder& a, const Builder& b) { return a.order < b.order; });
        for (auto& b : builders_) result_.forms.push_back(std::move(b.form));
        return std::move(result_);
    }

private:
    // ------------------------------------------------------------ plumbing
    Builder& begin(std::string id, std::string title, int order, bool worksheet = false) {
        builders_.push_back({});
        Builder& b = builders_.back();
        b.form.id = std::move(id);
        b.form.title = std::move(title);
        b.form.worksheet = worksheet;
        b.order = order;
        return b;
    }
    // Sorts Form 1040 lines into line order and drops schedules with nothing on them.
    void tidy() {
        auto key = [](const std::string& n) {
            std::size_t i = 0;
            int num = 0;
            while (i < n.size() && std::isdigit(static_cast<unsigned char>(n[i]))) num = num * 10 + (n[i++] - '0');
            return std::make_pair(num, n.substr(i));
        };
        auto& lines = builders_[i1040_].form.lines;
        std::stable_sort(lines.begin(), lines.end(), [&](const Line& a, const Line& b) { return key(a.number) < key(b.number); });
        const FormResult& s1 = builders_[iSch1_].form;
        if (std::all_of(s1.lines.begin(), s1.lines.end(), [](const Line& l) { return l.amount.isZero(); }))
            builders_[iSch1_].order = -1;
        builders_.erase(std::remove_if(builders_.begin(), builders_.end(), [](const Builder& b) { return b.order < 0; }),
                        builders_.end());
    }
    Builder& f1040() { return builders_[i1040_]; }
    Money L(const char* number) const { return builders_[i1040_].form.get(number); }

    void diag(Severity s, std::string topic, std::string message) {
        result_.diagnostics.push_back({s, std::move(topic), std::move(message)});
    }

    bool joint() const { return st_ == FilingStatus::MarriedJoint; }
    bool mfs() const { return st_ == FilingStatus::MarriedSeparate; }
    const Person& person(Owner o) const { return o == Owner::Spouse ? r_.spouse : r_.taxpayer; }
    bool counts(Owner o) const { return o == Owner::Taxpayer || joint(); }  // spouse items only on joint returns
    static std::size_t idx(Owner o) { return static_cast<std::size_t>(o); }
    std::string ownerName(Owner o) const {
        const Person& p = person(o);
        const std::string n = trim(p.first);
        return n.empty() ? (o == Owner::Spouse ? "Spouse" : "You") : n;
    }

    bool is65(const Person& p) const { return p.birthDate && irsAgeAtEndOfYear(*p.birthDate, year()) >= 65; }
    bool is50(const Person& p) const { return p.birthDate && irsAgeAtEndOfYear(*p.birthDate, year()) >= 50; }
    int year() const { return r_.info.year; }
    std::string yr(int offset = 0) const { return std::to_string(year() + offset); }
    // The 65-or-older test: born before January 2 of the year 64 years before the tax year.
    std::string seniorBirthDate() const { return "January 2, " + yr(-64); }

    Money wagesOf(Owner o) const {
        Money m;
        for (const auto& w : r_.w2s) {
            if (w.owner == o) m += w.wages;
        }
        return m;
    }

    // --------------------------------------------------------- validation
    void validate() {
        const std::string fs = "Filing status";
        if (trim(r_.taxpayer.first).empty()) diag(Severity::Warning, "About you", "Enter your name.");
        if (joint() && trim(r_.spouse.first).empty()) diag(Severity::Error, fs, "Married filing jointly needs your spouse's name.");
        if (st_ == FilingStatus::HeadOfHousehold) {
            const bool qualifying = std::any_of(r_.dependents.begin(), r_.dependents.end(), [](const Dependent& d) {
                return d.monthsLivedWithYou >= 7 || d.relationship == Relationship::Parent;
            });
            if (!qualifying)
                diag(Severity::Error, fs,
                     "Head of household requires a qualifying person who lived with you more than half the year "
                     "(or a dependent parent). Add them under Dependents or choose another status.");
        }
        if (st_ == FilingStatus::QualifyingSurvivingSpouse) {
            const bool child = std::any_of(r_.dependents.begin(), r_.dependents.end(),
                                           [](const Dependent& d) { return d.relationship == Relationship::Child; });
            if (!child)
                diag(Severity::Error, fs,
                     "Qualifying surviving spouse requires a dependent child. Add the child under Dependents.");
        }
        if (r_.taxpayer.claimedAsDependent && !r_.dependents.empty())
            diag(Severity::Error, "Dependents", "Someone who can be claimed as a dependent can't claim dependents.");
        if (!r_.taxpayer.birthDate)
            diag(Severity::Info, "About you",
                 "Add your date of birth so OpenTax can check the 65-or-older deductions, IRA catch-up and EIC age rules.");
        if (joint() && !r_.spouse.birthDate)
            diag(Severity::Info, "About you", "Add your spouse's date of birth.");

        for (std::size_t i = 0; i < r_.dependents.size(); ++i) {
            const auto& d = r_.dependents[i];
            const std::string topic = "Dependent " + (trim(d.first).empty() ? std::to_string(i + 1) : d.first);
            if (!d.birthDate) diag(Severity::Warning, topic, "Add a date of birth. Child credits need it.");
            if (d.monthsLivedWithYou > 12) diag(Severity::Error, topic, "Months lived with you can't exceed 12.");
        }
        for (const auto& w : r_.w2s) {
            const std::string topic = "W-2 " + w.employer;
            if (!counts(w.owner)) diag(Severity::Warning, topic, "This W-2 belongs to your spouse and is ignored unless you file jointly.");
            const Money expected = pct(w.ssWages + w.ssTips, "6.2");
            if (w.ssWithheld > expected + dollars(1))
                diag(Severity::Warning, topic, "Box 4 is more than 6.2% of boxes 3 and 7. Check the amounts; your employer may owe you a refund.");
            if (!w.qualifiedTips.isZero() && w.qualifiedTips > w.ssTips + w.wages)
                diag(Severity::Warning, topic, "Qualified tips are more than your reported tips.");
        }
        for (const auto& d : r_.dividends) {
            if (d.qualified > d.ordinary)
                diag(Severity::Error, "1099-DIV " + d.payer, "Qualified dividends (1b) can't be more than ordinary dividends (1a).");
            if (d.section199a > d.ordinary)
                diag(Severity::Error, "1099-DIV " + d.payer, "Section 199A dividends (5) can't be more than ordinary dividends (1a).");
        }
        for (const auto& x : r_.retirement) {
            if (x.taxable > x.gross)
                diag(Severity::Error, "1099-R " + x.payer, "The taxable amount (2a) can't be more than the gross distribution (1).");
        }
        if (mfs() && !r_.adjustments.studentLoanInterest.isZero())
            diag(Severity::Warning, "Student loan interest", "Married filing separately can't deduct student loan interest.");
    }

    // ------------------------------------------------------- Schedule C
    struct BusinessResult {
        Owner owner;
        std::string name;
        Money net;
        bool active;
    };
    std::vector<BusinessResult> biz_;
    std::array<Money, 2> bizNet_{};  // by owner

    void businesses() {
        int n = 0;
        for (const auto& b : r_.businesses) {
            ++n;
            if (!counts(b.owner)) {
                diag(Severity::Warning, "Schedule C " + b.name, "This business belongs to your spouse and is ignored unless you file jointly.");
                continue;
            }
            const std::string name = trim(b.name).empty() ? "Business " + std::to_string(n) : b.name;
            Builder& f = begin("Schedule C (" + name + ")", "Profit or Loss From Business - " + name + " (" + ownerName(b.owner) + ")", kOrderSchC);
            f.add("1", "Gross receipts or sales", b.receipts);
            f.add("2", "Returns and allowances", b.returns);
            const Money l3 = f.add("3", "Subtract line 2 from line 1", b.receipts - b.returns);
            f.add("4", "Cost of goods sold", b.costOfGoods);
            const Money l5 = f.add("5", "Gross profit", l3 - b.costOfGoods);
            f.add("6", "Other income", b.otherIncome);
            const Money l7 = f.add("7", "Gross income", l5 + b.otherIncome);

            const Money meals = pct(b.meals, 50);
            const std::pair<const char*, std::pair<const char*, Money>> expenses[] = {
                {"8", {"Advertising", b.advertising}},
                {"9", {"Car and truck expenses", b.carTruck}},
                {"10", {"Commissions and fees", b.commissions}},
                {"11", {"Contract labor", b.contractLabor}},
                {"13", {"Depreciation and section 179", b.depreciation}},
                {"15", {"Insurance (other than health)", b.insurance}},
                {"16", {"Interest", b.interest}},
                {"17", {"Legal and professional services", b.legal}},
                {"18", {"Office expense", b.office}},
                {"20", {"Rent or lease", b.rent}},
                {"21", {"Repairs and maintenance", b.repairs}},
                {"22", {"Supplies", b.supplies}},
                {"23", {"Taxes and licenses", b.taxesLicenses}},
                {"24a", {"Travel", b.travel}},
                {"24b", {"Deductible meals", meals}},
                {"25", {"Utilities", b.utilities}},
                {"26", {"Wages", b.wagesPaid}},
                {"27a", {"Other expenses", b.otherExpenses}},
            };
            Money total;
            for (const auto& e : expenses) {
                if (e.second.second.isZero()) continue;
                const std::string how = std::string(e.first) == "24b" ? "50% of " + usd(b.meals) + " business meals" : "";
                total += f.add(e.first, e.second.first, e.second.second, how);
            }
            f.add("28", "Total expenses", total);
            const Money l29 = f.add("29", "Tentative profit or (loss)", l7 - total);
            Money office;
            if (b.homeOfficeSqFt > 0) {
                const int sqft = std::min(b.homeOfficeSqFt, rules_.homeOfficeMaxSqFt);
                office = minOf(multiply(rules_.homeOfficeRate, Decimal::fromInt(sqft)), pos(l29));
                f.add("30", "Business use of home (simplified method)", office,
                      std::to_string(sqft) + " sq ft x " + usd(rules_.homeOfficeRate) + ", limited to the tentative profit");
            }
            const Money net = f.add("31", "Net profit or (loss)", l29 - office);
            biz_.push_back({b.owner, name, net, b.materialParticipation});
            bizNet_[idx(b.owner)] += net;
        }
    }

    // ----------------------------------------------------- Schedule SE
    std::array<Money, 2> seTax_{};
    std::array<Money, 2> seHalf_{};
    std::array<Money, 2> seEarnings_{};  // line 6 (for Form 8959)
    std::array<Money, 2> seEarned_{};    // net earnings for earned income: line 3 minus line 13

    void selfEmployment() {
        for (Owner o : {Owner::Taxpayer, Owner::Spouse}) {
            const std::size_t i = idx(o);
            const bool any = std::any_of(biz_.begin(), biz_.end(), [o](const BusinessResult& b) { return b.owner == o; });
            if (!any) continue;
            const Money l3 = bizNet_[i];
            const Money l4a = l3 > kZero ? pct(l3, rules_.seEarningsPercent) : l3;
            const Money l4c = l4a >= rules_.seMinimum ? l4a : kZero;
            if (l4c.isZero()) {
                seEarned_[i] = l3;
                if (l3 > kZero)
                    diag(Severity::Info, "Self-employment tax", ownerName(o) + ": net earnings under $400, so no self-employment tax is due.");
                continue;
            }
            Builder& f = begin(std::string("Schedule SE (") + ownerName(o) + ")", "Self-Employment Tax - " + ownerName(o), kOrderSchSE);
            f.add("2", "Net profit from Schedule C", l3);
            f.add("3", "Combine lines 1a, 1b, and 2", l3);
            f.add("4a", "Line 3 x 92.35%", l4a);
            f.add("4c", "Net earnings from self-employment", l4c);
            const Money l6 = f.add("6", "Add lines 4c and 5b", l4c);
            const Money l7 = f.add("7", "Maximum earnings subject to social security", rules_.ssWageBase);
            Money ssWages;
            for (const auto& w : r_.w2s) {
                if (w.owner == o) ssWages += w.ssWages + w.ssTips;
            }
            f.add("8a", "Social security wages and tips (W-2 boxes 3 and 7)", ssWages);
            f.add("8d", "Add lines 8a through 8c", ssWages);
            const Money l9 = f.add("9", "Subtract line 8d from line 7", pos(l7 - ssWages));
            const Money l10 = f.add("10", "Social security part: smaller of line 6 or 9 x 12.4%", pct(minOf(l6, l9), "12.4"));
            const Money l11 = f.add("11", "Medicare part: line 6 x 2.9%", pct(l6, "2.9"));
            const Money l12 = f.add("12", "Self-employment tax", l10 + l11, "To Schedule 2, line 4");
            const Money l13 = f.add("13", "Deduction for one-half of self-employment tax", pct(l12, 50), "To Schedule 1, line 15");
            seTax_[i] = l12;
            seHalf_[i] = l13;
            seEarnings_[i] = l6;
            seEarned_[i] = l3 - l13;
        }
    }

    Money earnedIncome(Owner o) const { return wagesOf(o) + seEarned_[idx(o)]; }

    // ------------------------------------ dependent care benefits (2441 Part III)
    std::vector<const Dependent*> careQualifying_;
    Money careExpenses_;
    Money careBenefitsTaxable_;
    Money careLine3_;
    bool careBenefits_ = false;
    bool careNoCredit_ = false;

    void careBenefits() {
        for (const auto& d : r_.dependents) {
            const bool young = d.birthDate && ageAtEndOfYear(*d.birthDate, year()) < 13;
            if ((young || d.permanentlyDisabled) && d.monthsLivedWithYou >= 7) {
                careQualifying_.push_back(&d);
                careExpenses_ += d.careExpenses;
            } else if (!d.careExpenses.isZero()) {
                diag(Severity::Warning, "Dependent care", d.first + " isn't a qualifying person for the care credit "
                     "(must be under 13, or disabled, and live with you more than half the year).");
            }
        }
        Money benefits;
        for (const auto& w : r_.w2s) {
            if (counts(w.owner)) benefits += w.dependentCare;
        }
        const Money limit = careQualifying_.size() >= 2 ? rules_.careLimitTwo : rules_.careLimitOne;
        if (benefits.isZero()) {
            careLine3_ = minOf(careExpenses_, limit);
            return;
        }
        careBenefits_ = true;
        Builder& f = begin("Form 2441 Part III", "Dependent Care Benefits (Form 2441, Part III)", kOrder2441);
        const Money l12 = f.add("12", "Dependent care benefits (W-2 box 10)", benefits);
        const Money l15 = f.add("15", "Combine lines 12 through 14", l12);
        const Money l16 = f.add("16", "Qualified expenses incurred", careExpenses_);
        const Money l17 = f.add("17", "Smaller of line 15 or 16", minOf(l15, l16));
        const Money l18 = f.add("18", "Your earned income", earnedIncome(Owner::Taxpayer));
        const Money l19 = f.add("19", joint() ? "Spouse's earned income" : "Your earned income",
                                joint() ? earnedIncome(Owner::Spouse) : l18);
        const Money l20 = f.add("20", "Smallest of line 17, 18, or 19", pos(minOf(l17, minOf(l18, l19))));
        const Money l21 = f.add("21", "Exclusion limit", mfs() ? rules_.careBenefitExclusionMfs : rules_.careBenefitExclusion);
        const Money l23 = f.add("23", "Subtract line 22 from line 15", l15);
        const Money l25 = f.add("25", "Excluded benefits", minOf(l20, l21));
        careBenefitsTaxable_ = f.add("26", "Taxable benefits", pos(l23 - l25), "To Form 1040, line 1e");
        const Money l27 = f.add("27", "Expense limit", limit);
        const Money l28 = f.add("28", "Add lines 24 and 25", l25);
        const Money l29 = f.add("29", "Subtract line 28 from line 27", pos(l27 - l28));
        if (l29.isZero()) {
            careNoCredit_ = true;
            careLine3_ = kZero;
            return;
        }
        const Money l30 = f.add("30", "Expenses not paid with benefits", pos(careExpenses_ - l28));
        careLine3_ = f.add("31", "Smaller of line 29 or 30", minOf(l29, l30), "To Form 2441, line 3");
    }

    // ------------------------------------------------------------ income
    std::size_t i1040_ = 0;
    std::size_t iSch1_ = 0;
    bool hasSchD_ = false;
    Money schD15_, schD16_, schD7_;
    Money capGainDist_;
    Money capitalLine7_;
    Money schedule1Income_;

    void income() {
        i1040_ = builders_.size();
        begin("1040", "U.S. Individual Income Tax Return", kOrder1040);

        // Wages
        Money wages, fedW2;
        int w2count = 0;
        for (const auto& w : r_.w2s) {
            if (!counts(w.owner)) continue;
            wages += w.wages;
            ++w2count;
        }
        f1040().add("1a", "Total amount from Form(s) W-2, box 1", wages, "Box 1 of " + plural(static_cast<std::size_t>(w2count), "Form W-2", "Forms W-2"));
        if (!careBenefitsTaxable_.isZero())
            f1040().add("1e", "Taxable dependent care benefits from Form 2441, line 26", careBenefitsTaxable_);
        f1040().add("1z", "Add lines 1a through 1h", wages + careBenefitsTaxable_);

        // Interest and dividends
        Money taxExempt, interest, ordinary, qualified;
        for (const auto& i : r_.interest) {
            taxExempt += i.taxExempt;
            interest += i.interest + i.usBonds;
        }
        for (const auto& d : r_.dividends) {
            taxExempt += d.exemptInterest;
            ordinary += d.ordinary;
            qualified += d.qualified;
            capGainDist_ += d.capitalGainDist;
        }
        f1040().add("2a", "Tax-exempt interest", taxExempt, "Box 8 of Forms 1099-INT and box 12 of Forms 1099-DIV. Not taxed, but used for Social Security benefits.");
        f1040().add("2b", "Taxable interest", interest, "Boxes 1 and 3 of " + plural(r_.interest.size(), "Form 1099-INT", "Forms 1099-INT"));
        f1040().add("3a", "Qualified dividends", qualified, "Box 1b of Forms 1099-DIV. Taxed at capital gain rates.");
        f1040().add("3b", "Ordinary dividends", ordinary, "Box 1a of " + plural(r_.dividends.size(), "Form 1099-DIV", "Forms 1099-DIV"));
        if (interest > dollars(1500) || ordinary > dollars(1500)) scheduleB();

        // Retirement
        Money iraGross, iraTaxable, penGross, penTaxable;
        for (const auto& x : r_.retirement) {
            if (!counts(x.owner)) continue;
            Money taxable = x.taxable;
            if (x.taxableNotDetermined && taxable.isZero()) {
                taxable = x.gross;
                diag(Severity::Warning, "1099-R " + x.payer,
                     "The taxable amount wasn't determined, so the whole distribution is treated as taxable. "
                     "If you have after-tax basis, figure the taxable part (Form 8606 or the Simplified Method) and enter it in box 2a.");
            }
            if (toLower(x.code).find('g') != std::string::npos) taxable = kZero;  // direct rollover
            if (x.ira) {
                iraGross += x.gross;
                iraTaxable += taxable;
            } else {
                penGross += x.gross;
                penTaxable += taxable;
            }
        }
        f1040().add("4a", "IRA distributions", iraGross);
        f1040().add("4b", "IRA distributions - taxable amount", iraTaxable, iraGross != iraTaxable ? "Rollovers (code G) and nontaxable amounts are excluded" : "");
        f1040().add("5a", "Pensions and annuities", penGross);
        f1040().add("5b", "Pensions and annuities - taxable amount", penTaxable);

        capital();

        // Schedule 1, Part I
        iSch1_ = builders_.size();
        Builder& s1 = begin("Schedule 1", "Additional Income and Adjustments to Income", kOrderSch1);
        Money business;
        for (const auto& b : biz_) business += b.net;
        Money unemployment, other;
        for (const auto& u : r_.unemployment) unemployment += u.compensation;
        for (const auto& o : r_.otherIncome) other += o.amount;
        s1.add("3", "Business income or (loss) from Schedule C", business, plural(biz_.size(), "Schedule C", "Schedules C"));
        s1.add("7", "Unemployment compensation", unemployment, "Box 1 of Forms 1099-G");
        if (!other.isZero()) s1.add("8z", "Other income", other, plural(r_.otherIncome.size(), "item", "items"));
        s1.add("9", "Total other income", other);
        schedule1Income_ = s1.add("10", "Additional income", business + unemployment + other, "To Form 1040, line 8");
    }

    void scheduleB() {
        Builder& f = begin("Schedule B", "Interest and Ordinary Dividends", kOrderSchB);
        int n = 0;
        Money interest;
        for (const auto& i : r_.interest) {
            f.add("1." + std::to_string(++n), i.payer.empty() ? "Interest" : i.payer, i.interest + i.usBonds);
            interest += i.interest + i.usBonds;
        }
        f.add("4", "Total interest", interest, "To Form 1040, line 2b");
        n = 0;
        Money dividends;
        for (const auto& d : r_.dividends) {
            f.add("5." + std::to_string(++n), d.payer.empty() ? "Dividends" : d.payer, d.ordinary);
            dividends += d.ordinary;
        }
        f.add("6", "Total ordinary dividends", dividends, "To Form 1040, line 3b");
    }

    void capital() {
        const Money stCarry = r_.carryovers.shortTermLoss;
        const Money ltCarry = r_.carryovers.longTermLoss;
        hasSchD_ = !r_.capitalTxns.empty() || !stCarry.isZero() || !ltCarry.isZero();
        if (!hasSchD_) {
            capitalLine7_ = capGainDist_;
            f1040().add("7a", "Capital gain or (loss)", capGainDist_,
                        capGainDist_.isZero() ? "" : "Capital gain distributions (1099-DIV box 2a); Schedule D not required");
            return;
        }

        // Form 8949 detail, grouped by box.
        struct Box {
            Money proceeds, basis, adjust, gain;
            int count = 0;
        };
        std::map<char, Box> boxes;  // A, B (short), D, E (long)
        Builder& f8949 = begin("Form 8949", "Sales and Other Dispositions of Capital Assets", kOrder8949);
        int n = 0;
        for (const auto& t : r_.capitalTxns) {
            const char box = t.term == Term::Short ? (t.basisReported ? 'A' : 'B') : (t.basisReported ? 'D' : 'E');
            const Money gain = t.proceeds - t.basis + t.adjustment;
            Box& b = boxes[box];
            b.proceeds += t.proceeds;
            b.basis += t.basis;
            b.adjust += t.adjustment;
            b.gain += gain;
            ++b.count;
            std::string how = usd(t.proceeds) + " proceeds - " + usd(t.basis) + " basis";
            if (!t.adjustment.isZero()) how += " + " + usd(t.adjustment) + " adjustment";
            how += std::string(" (box ") + box + ")";
            if (t.acquired && t.sold) how += ", " + t.acquired->str() + " to " + t.sold->str();
            f8949.add(std::to_string(++n), t.description.empty() ? "Sale" : t.description, gain, how);
        }

        Builder& d = begin("Schedule D", "Capital Gains and Losses", kOrderSchD);
        auto boxLine = [&](const char* line, char box, const char* label) {
            auto it = boxes.find(box);
            if (it == boxes.end()) return kZero;
            return d.add(line, label, it->second.gain, plural(static_cast<std::size_t>(it->second.count), "sale", "sales") +
                                                           ": " + usd(it->second.proceeds) + " proceeds, " + usd(it->second.basis) + " basis");
        };
        Money st = boxLine("1b", 'A', "Short-term, basis reported (Form 8949 box A)");
        st += boxLine("2", 'B', "Short-term, basis not reported (Form 8949 box B)");
        if (!stCarry.isZero()) st += d.add("6", "Short-term capital loss carryover", -stCarry);
        schD7_ = d.add("7", "Net short-term capital gain or (loss)", st);
        Money lt = boxLine("8b", 'D', "Long-term, basis reported (Form 8949 box D)");
        lt += boxLine("9", 'E', "Long-term, basis not reported (Form 8949 box E)");
        if (!capGainDist_.isZero()) lt += d.add("13", "Capital gain distributions", capGainDist_, "1099-DIV box 2a");
        if (!ltCarry.isZero()) lt += d.add("14", "Long-term capital loss carryover", -ltCarry);
        schD15_ = d.add("15", "Net long-term capital gain or (loss)", lt);
        schD16_ = d.add("16", "Combine lines 7 and 15", st + lt);
        if (schD16_ >= kZero) {
            capitalLine7_ = schD16_;
            f1040().add("7a", "Capital gain or (loss)", schD16_, "Schedule D, line 16");
        } else {
            const Money limit = mfs() ? rules_.capitalLossLimitMfs : rules_.capitalLossLimit;
            capitalLine7_ = d.add("21", "Allowable capital loss", maxOf(schD16_, -limit),
                                  "The smaller of the loss or " + usd(limit) + "; the rest carries over to " + yr(1));
            f1040().add("7a", "Capital gain or (loss)", capitalLine7_, "Schedule D, line 21");
        }
    }

    // --------------------------------- adjustments and Social Security
    Money adjExIraSli_;   // Schedule 1 lines 11-19a, 23, 25
    Money ira_;
    Money sli_;
    Money ssTaxable_;

    Money incomeExceptSs() const {
        return L("1z") + L("2b") + L("3b") + L("4b") + L("5b") + L("7a") + schedule1Income_;
    }

    // Social Security Benefits Worksheet. Returns the taxable amount; emits the worksheet when `emit`.
    Money socialSecurityWorksheet(Money line6, bool emit) {
        Money benefits;
        for (const auto& s : r_.socialSecurity) {
            if (counts(s.owner)) benefits += s.benefits;
        }
        if (benefits.isZero()) return kZero;
        Builder* f = emit ? &begin("SS Benefits Worksheet", "Social Security Benefits Worksheet (lines 6a and 6b)", kOrderWorksheet, true) : nullptr;
        auto add = [&](const char* n, const char* label, Money m, const std::string& how = {}) {
            if (f) f->add(n, label, m, how);
            return m;
        };
        const Money l1 = add("1", "Total benefits (SSA-1099 box 5)", benefits);
        const Money l2 = add("2", "Line 1 x 50%", pct(l1, 50));
        const Money l3 = add("3", "Other income (lines 1z, 2b, 3b, 4b, 5b, 7a, 8)", incomeExceptSs());
        const Money l4 = add("4", "Tax-exempt interest (line 2a)", L("2a"));
        const Money l5 = add("5", "Combine lines 2, 3, and 4", l2 + l3 + l4);
        const Money l6 = add("6", "Adjustments (Schedule 1, lines 11-20, 23, 25)", line6);
        if (l6 >= l5) {
            add("18", "Taxable benefits", kZero, "Line 6 is not less than line 5, so none of the benefits are taxable");
            return kZero;
        }
        const Money l7 = add("7", "Subtract line 6 from line 5", l5 - l6);
        if (mfs() && !r_.info.livedApartAllYear) {
            const Money l16 = add("16", "Line 7 x 85% (married filing separately, lived with spouse)", pct(l7, 85));
            const Money l17 = add("17", "Line 1 x 85%", pct(l1, 85));
            return add("18", "Taxable benefits", minOf(l16, l17), "Smaller of line 16 or line 17");
        }
        const Money l8 = add("8", "Base amount", pick(rules_.ssBase, st_));
        if (l8 >= l7) {
            add("18", "Taxable benefits", kZero, "Line 8 is not less than line 7, so none of the benefits are taxable");
            return kZero;
        }
        const Money l9 = add("9", "Subtract line 8 from line 7", l7 - l8);
        const Money l10 = add("10", "Additional amount", pick(rules_.ssAdjustedBase, st_));
        const Money l11 = add("11", "Subtract line 10 from line 9", pos(l9 - l10));
        const Money l12 = add("12", "Smaller of line 9 or line 10", minOf(l9, l10));
        const Money l13 = add("13", "One-half of line 12", pct(l12, 50));
        const Money l14 = add("14", "Smaller of line 2 or line 13", minOf(l2, l13));
        const Money l15 = add("15", "Line 11 x 85%", pct(l11, 85));
        const Money l16 = add("16", "Add lines 14 and 15", l14 + l15);
        const Money l17 = add("17", "Line 1 x 85%", pct(l1, 85));
        return add("18", "Taxable benefits", minOf(l16, l17), "Smaller of line 16 or line 17");
    }

    void adjustmentsAndSocialSecurity() {
        const Adjustments& a = r_.adjustments;
        Builder& s1 = builders_[iSch1_];
        auto adj = [&](const char* n, const char* label, Money m, const std::string& how = {}) {
            if (!m.isZero()) s1.add(n, label, m, how);
            return m;
        };
        Money educator = minOf(a.educatorTaxpayer, rules_.educatorLimit);
        if (joint()) educator += minOf(a.educatorSpouse, rules_.educatorLimit);
        Money total;
        total += adj("11", "Educator expenses", educator, "Up to " + usd(rules_.educatorLimit) + " per educator");
        total += adj("13", "Health savings account deduction", a.hsa);
        const Money half = seHalf_[0] + (joint() ? seHalf_[1] : kZero);
        total += adj("15", "Deductible part of self-employment tax", half, "Schedule SE, line 13");
        total += adj("16", "Self-employed SEP, SIMPLE, and qualified plans", a.sepSimple);
        Money seProfit = bizNet_[0] + (joint() ? bizNet_[1] : kZero);
        const Money seHealthLimit = pos(seProfit - half - a.sepSimple);
        const Money seHealth = minOf(a.seHealthInsurance, seHealthLimit);
        if (a.seHealthInsurance > seHealthLimit)
            diag(Severity::Warning, "Self-employed health insurance", "Limited to your business profit minus the deductible part of SE tax and SEP contributions.");
        total += adj("17", "Self-employed health insurance deduction", seHealth, a.seHealthInsurance > seHealthLimit ? "Limited to net self-employment profit" : "");
        Money early;
        for (const auto& i : r_.interest) early += i.earlyWithdrawal;
        total += adj("18", "Penalty on early withdrawal of savings", early, "Box 2 of Forms 1099-INT");
        adjExIraSli_ = total;
        seHealth_ = seHealth;

        // IRA: MAGI uses taxable benefits figured without the IRA deduction (Pub. 590-A, Appendix B).
        const Money ssForIra = socialSecurityWorksheet(adjExIraSli_, false);
        ira_ = iraDeduction(incomeExceptSs() + ssForIra);
        total += adj("20", "IRA deduction", ira_, "IRA Deduction Worksheet");

        Money benefits;
        for (const auto& s : r_.socialSecurity) {
            if (counts(s.owner)) benefits += s.benefits;
        }
        ssTaxable_ = socialSecurityWorksheet(adjExIraSli_ + ira_, true);
        f1040().add("6a", "Social security benefits", benefits);
        f1040().add("6b", "Social security benefits - taxable amount", ssTaxable_, benefits.isZero() ? "" : "Social Security Benefits Worksheet, line 18");
        f1040().add("8", "Additional income from Schedule 1, line 10", schedule1Income_);
        const Money line9 = f1040().add("9", "Total income", incomeExceptSs() + ssTaxable_, "Add lines 1z, 2b, 3b, 4b, 5b, 6b, 7a, and 8");

        sli_ = studentLoan(line9, adjExIraSli_ + ira_);
        total += adj("21", "Student loan interest deduction", sli_, "Student Loan Interest Deduction Worksheet");
        s1.add("26", "Adjustments to income", total, "To Form 1040, line 10");
        f1040().add("10", "Adjustments to income from Schedule 1, line 26", total);
        const Money agi = f1040().add("11a", "Adjusted gross income", line9 - total, "Line 9 minus line 10");
        f1040().add("11b", "Amount from line 11a", agi);
    }

    Money seHealth_;

    Money iraDeduction(Money totalIncomeForMagi) {
        const Person& tp = r_.taxpayer;
        const Person& sp = r_.spouse;
        const bool hasTp = !tp.traditionalIra.isZero();
        const bool hasSp = joint() && !sp.traditionalIra.isZero();
        if (!hasTp && !hasSp) return kZero;
        Builder& f = begin("IRA Deduction Worksheet", "IRA Deduction Worksheet (Schedule 1, line 20)", kOrderWorksheet, true);
        auto covered = [&](Owner o) {
            return std::any_of(r_.w2s.begin(), r_.w2s.end(), [o](const W2& w) { return w.owner == o && w.retirementPlan; });
        };
        const bool coveredTp = covered(Owner::Taxpayer);
        const bool coveredSp = joint() && covered(Owner::Spouse);
        f.text("1a", "Were you covered by a retirement plan?", coveredTp ? "Yes" : "No", "From W-2 box 13");
        if (joint()) f.text("1b", "Was your spouse covered by a retirement plan?", coveredSp ? "Yes" : "No");

        const Money l3 = totalIncomeForMagi;
        const Money l4 = adjExIraSli_;
        const Money magi = l3 - l4;
        const Money wages = wagesOf(Owner::Taxpayer) + (joint() ? wagesOf(Owner::Spouse) : kZero);
        Money seComp = seEarned_[0] + (joint() ? seEarned_[1] : kZero) - r_.adjustments.sepSimple;
        seComp = pos(seComp);
        const Money comp = wages + seComp;

        Money total;
        for (Owner o : {Owner::Taxpayer, Owner::Spouse}) {
            const Person& p = person(o);
            if (o == Owner::Spouse && !joint()) continue;
            if (p.traditionalIra.isZero()) continue;
            const char* col = o == Owner::Taxpayer ? "a" : "b";
            const std::string who = ownerName(o);
            const Money full = rules_.iraLimit + (is50(p) ? rules_.iraCatchUp : kZero);
            Money limit = full;
            const bool anyCovered = coveredTp || coveredSp;
            if (anyCovered) {
                const bool selfCovered = o == Owner::Taxpayer ? coveredTp : coveredSp;
                // Line 2: where the deduction reaches zero. The phase-out range is $10,000, or
                // $20,000 for a covered person filing jointly (or as a qualifying surviving spouse).
                Money l2;
                Money range = dollars(10000);
                switch (st_) {
                    case FilingStatus::MarriedJoint:
                        l2 = selfCovered ? pick(rules_.iraPhaseEnd, st_) : rules_.iraPhaseEndSpouseCovered;
                        if (selfCovered) range = dollars(20000);
                        break;
                    case FilingStatus::QualifyingSurvivingSpouse:
                        l2 = pick(rules_.iraPhaseEnd, st_);
                        range = dollars(20000);
                        break;
                    case FilingStatus::MarriedSeparate:
                        l2 = r_.info.livedApartAllYear ? pick(rules_.iraPhaseEnd, FilingStatus::Single)
                                                       : pick(rules_.iraPhaseEnd, st_);
                        break;
                    default: l2 = pick(rules_.iraPhaseEnd, st_);
                }
                f.add(std::string("2") + col, who + ": phase-out end", l2);
                f.add(std::string("5") + col, who + ": modified AGI", magi, "Line 3 (total income " + usd(l3) + ") minus line 4 (adjustments " + usd(l4) + ")");
                if (magi >= l2) {
                    limit = kZero;
                    f.add(std::string("7") + col, who + ": deduction limit", kZero, "Modified AGI is at or above the phase-out end, so none is deductible");
                } else {
                    const Money l6 = l2 - magi;
                    f.add(std::string("6") + col, who + ": line 2 minus line 5", l6);
                    if (l6 < range) {
                        // Line 7: the limit times the part of the range still remaining (the
                        // worksheet's 70%/35%, or 80%/40% with the catch-up, for 2025), rounded up
                        // to the next $10, and at least $200.
                        const std::int64_t tenDollars = 1000;
                        const std::int64_t num = full.cents() * l6.cents();
                        const std::int64_t den = range.cents() * tenDollars;
                        Money l7 = Money::fromCents(((num + den - 1) / den) * tenDollars);
                        if (l7 < dollars(200)) l7 = dollars(200);
                        limit = minOf(l7, full);
                        const Decimal rate = Decimal::fromRaw(full.cents() * 100 * Decimal::kScale / range.cents());
                        f.add(std::string("7") + col, who + ": deduction limit", limit,
                              "Line 6 x " + rate.str() + "%, rounded up to a multiple of $10 (at least $200)");
                    } else {
                        f.add(std::string("7") + col, who + ": deduction limit", full, "Not reduced");
                    }
                }
            } else {
                f.add(std::string("7") + col, who + ": deduction limit", full, "No one was covered by a workplace plan");
            }
            const Money contributed = p.traditionalIra;
            if (contributed > full)
                diag(Severity::Warning, "IRA", who + " contributed more than the " + usd(full) + " limit. The excess may be subject to a 6% tax (Form 5329).");
            f.add(std::string("11") + col, who + ": traditional IRA contributions", contributed);
            const Money deduct = minOf(minOf(limit, comp), contributed);
            f.add(std::string("12") + col, who + ": deductible amount", deduct, "Smallest of lines 7, 10 (" + usd(comp) + " compensation), and 11");
            if (deduct < contributed)
                diag(Severity::Info, "IRA", who + ": " + usd(contributed - deduct) + " of IRA contributions isn't deductible. Report it on Form 8606 as a nondeductible contribution.");
            total += deduct;
        }
        f.add("8", "Wages (W-2 box 1)", wages);
        f.add("9", "Self-employment compensation", seComp);
        f.add("10", "Add lines 8 and 9", comp);
        if (joint() && comp < dollars(14000))
            diag(Severity::Warning, "IRA", "Combined compensation is low; check the spousal IRA limits in Pub. 590-A.");
        if (total > comp) total = comp;
        f.add("12", "IRA deduction", total, "To Schedule 1, line 20");
        return total;
    }

    Money studentLoan(Money line9, Money otherAdjustments) {
        const Money paid = r_.adjustments.studentLoanInterest;
        if (paid.isZero() || mfs()) return kZero;
        if (r_.taxpayer.claimedAsDependent || (joint() && r_.spouse.claimedAsDependent)) {
            diag(Severity::Warning, "Student loan interest", "You can't take this deduction if you can be claimed as a dependent.");
            return kZero;
        }
        Builder& f = begin("Student Loan Interest Worksheet", "Student Loan Interest Deduction Worksheet (Schedule 1, line 21)", kOrderWorksheet, true);
        const Money l1 = f.add("1", "Interest paid (up to $2,500)", minOf(paid, rules_.studentLoanLimit));
        f.add("2", "Total income (Form 1040, line 9)", line9);
        f.add("3", "Adjustments (Schedule 1, lines 11-20, 23, 25)", otherAdjustments);
        const Money l4 = f.add("4", "Modified AGI", line9 - otherAdjustments);
        const Money l5 = f.add("5", "Phase-out start", pick(rules_.studentLoanPhaseStart, st_));
        if (l4 <= l5) return f.add("9", "Student loan interest deduction", l1, "Modified AGI is not over line 5, so no reduction");
        const Money l6 = f.add("6", "Subtract line 5 from line 4", l4 - l5);
        const std::int64_t r = ratio3(l6, pick(rules_.studentLoanPhaseRange, st_));
        f.text("7", "Line 6 divided by " + usd(pick(rules_.studentLoanPhaseRange, st_)), ratioText(r));
        const Money l8 = f.add("8", "Line 1 x line 7", times3(l1, r));
        return f.add("9", "Student loan interest deduction", l1 - l8);
    }

    // -------------------------------------------------------- deductions
    Money standard_;
    Money itemized_;
    bool itemize_ = false;
    Money saltLine7_;  // Schedule A line 7 (for AMT)

    Money standardDeduction() {
        const Person& tp = r_.taxpayer;
        const Person& sp = r_.spouse;
        const Money base = pick(rules_.standardDeduction, st_);
        if (mfs() && r_.info.spouseItemizes) return kZero;
        int boxes = (is65(tp) ? 1 : 0) + (tp.blind ? 1 : 0);
        if (joint()) boxes += (is65(sp) ? 1 : 0) + (sp.blind ? 1 : 0);
        const bool marriedRate = st_ == FilingStatus::MarriedJoint || st_ == FilingStatus::MarriedSeparate ||
                                 st_ == FilingStatus::QualifyingSurvivingSpouse;
        const Money add = marriedRate ? rules_.additionalMarried : rules_.additionalUnmarried;
        const bool dependent = tp.claimedAsDependent || (joint() && sp.claimedAsDependent);
        if (!dependent && boxes == 0) return base;

        Builder& f = begin("Standard Deduction", dependent ? "Standard Deduction Worksheet for Dependents (line 12e)"
                                                           : "Standard Deduction (line 12e)", kOrderWorksheet, true);
        f.text("1", "Boxes checked (born before " + seniorBirthDate() + ", or blind)", std::to_string(boxes));
        Money l4a = base;
        if (dependent) {
            const Money earned = L("1z") + builders_[iSch1_].form.get("3") - builders_[iSch1_].form.get("15");
            const Money l2 = f.add("2", "Earned income plus $450 (at least $1,350)",
                                   earned > dollars(900) ? earned + rules_.dependentEarnedAdd : rules_.dependentMinimum,
                                   "Earned income is " + usd(earned));
            const Money l3 = f.add("3", "Standard deduction for your filing status", base);
            l4a = f.add("4a", "Smaller of line 2 or line 3", minOf(l2, l3));
        } else {
            f.add("4a", "Standard deduction for your filing status", base);
        }
        const Money l4b = f.add("4b", "Additional amount", multiply(add, Decimal::fromInt(boxes)),
                                std::to_string(boxes) + " x " + usd(add));
        return f.add("4c", "Standard deduction", l4a + l4b);
    }

    Money scheduleA() {
        const Itemized& it = r_.itemized;
        Money w2State;
        for (const auto& w : r_.w2s) {
            if (counts(w.owner)) w2State += w.stateWithheld;
        }
        const bool anyInput = !it.medical.isZero() || !it.stateIncomeTax.isZero() || !it.salesTax.isZero() ||
                              !it.realEstateTax.isZero() || !it.personalPropertyTax.isZero() || !it.otherTaxes.isZero() ||
                              !it.mortgageInterest.isZero() || !it.mortgagePoints.isZero() || !it.mortgageInsurance.isZero() ||
                              !it.investmentInterest.isZero() || !it.charityCash.isZero() || !it.charityNoncash.isZero() ||
                              !it.charityCarryover.isZero() || !it.otherItemized.isZero() || !w2State.isZero();
        if (!anyInput) return kZero;
        const bool y26 = rules_.formsYear >= 2026;
        const Money agi = L("11b");
        Builder& f = begin("Schedule A", "Itemized Deductions", kOrderSchA);
        const Money l1 = f.add("1", "Medical and dental expenses", it.medical);
        f.add("2", "Amount from Form 1040, line 11b", agi);
        const Money l3 = f.add("3", "Line 2 x 7.5%", pct(agi, rules_.medicalFloorPercent));
        const Money l4 = f.add("4", "Deductible medical expenses", pos(l1 - l3));
        const Money l5a = it.useSalesTax ? f.add("5a", "General sales taxes", it.salesTax)
                                         : f.add("5a", "State and local income taxes", w2State + it.stateIncomeTax,
                                                 usd(w2State) + " withheld on W-2s + " + usd(it.stateIncomeTax) + " paid directly");
        const Money l5b = f.add("5b", "State and local real estate taxes", it.realEstateTax);
        const Money l5c = f.add("5c", "State and local personal property taxes", it.personalPropertyTax);
        const Money l5d = f.add("5d", "Add lines 5a through 5c", l5a + l5b + l5c);
        const Money l5e = f.add("5e", "State and local taxes (limited)", salt(l5d, agi), "State and Local Tax Deduction Worksheet");
        const Money l6 = f.add("6", "Other taxes", it.otherTaxes);
        saltLine7_ = f.add("7", "Add lines 5e and 6", l5e + l6);
        const Money l8a = f.add("8a", "Home mortgage interest and points (Form 1098)", it.mortgageInterest + it.mortgagePoints);
        Money l8d;
        if (!it.mortgageInsurance.isZero()) {
            if (!rules_.mortgageInsurance) {
                diag(Severity::Warning, "Mortgage insurance",
                     "Mortgage insurance premiums aren't deductible for " + yr() + ". They are again from 2026.");
            } else {
                // Sec. 163(h)(3)(E): reduced 10% for each $1,000 ($500 if married filing separately),
                // or part of one, of AGI over $100,000 ($50,000).
                const Money start = mfs() ? dollars(50000) : dollars(100000);
                const std::int64_t step = mfs() ? 50000 : 100000;  // cents
                const std::int64_t over = pos(agi - start).cents();
                const int reduce = static_cast<int>(std::min<std::int64_t>(10, (over + step - 1) / step)) * 10;
                l8d = f.add("8d", "Mortgage insurance premiums", it.mortgageInsurance - pct(it.mortgageInsurance, reduce),
                            reduce == 0 ? std::string()
                                        : usd(it.mortgageInsurance) + " reduced by " + std::to_string(reduce) +
                                              "% because AGI is over " + usd(start));
            }
        }
        const Money l8e = f.add("8e", y26 ? "Add lines 8a through 8d" : "Add lines 8a through 8c", l8a + l8d);
        Money investLimit = pos(L("2b") + L("3b") - L("3a"));
        Money invest = minOf(it.investmentInterest, investLimit);
        if (it.investmentInterest > investLimit)
            diag(Severity::Warning, "Investment interest", "Limited to net investment income (" + usd(investLimit) + "). The rest carries forward (Form 4952).");
        const Money l9 = f.add("9", "Investment interest", invest);
        const Money l10 = f.add("10", "Add lines 8e and 9", l8e + l9);

        const Money l11 = f.add("11", "Gifts by cash or check", it.charityCash);
        const Money cap30 = pct(agi, 30);
        const Money l12 = f.add("12", "Other than by cash or check", minOf(it.charityNoncash, cap30),
                                it.charityNoncash > cap30 ? "Limited to 30% of AGI" : "");
        const Money cap60 = pct(agi, 60);
        Money gifts;
        bool overLimit = false;
        if (!y26) {
            const Money l13 = f.add("13", "Carryover from prior year", it.charityCarryover);
            gifts = f.add("14", "Gifts to charity", minOf(l11 + l12 + l13, cap60), l11 + l12 + l13 > cap60 ? "Limited to 60% of AGI" : "");
            overLimit = it.charityNoncash > cap30 || l11 + l12 + l13 > cap60;
        } else {
            // Gifts count only to the extent they exceed 0.5% of AGI (sec. 170(b)(1)(I)).
            const Money limited = minOf(l11 + l12, cap60);
            const Money floor = pct(agi, rules_.charityFloorPercent);
            const Money l13 = f.add("13", "Gifts after AGI limits and the 0.5% floor", pos(limited - floor),
                                    usd(limited) + " allowed" + (l11 + l12 > cap60 ? " (limited to 60% of AGI)" : "") +
                                        " minus " + usd(floor) + " (0.5% of AGI)");
            const Money l14 = f.add("14", "Carryover from prior year", it.charityCarryover);
            gifts = f.add("15", "Add lines 13 and 14", l13 + l14);
            overLimit = it.charityNoncash > cap30 || l11 + l12 > cap60;
            charityProvisional_ = !(l11 + l12).isZero();
        }
        if (overLimit)
            diag(Severity::Warning, "Charitable gifts", "Your gifts exceed the AGI limits. The excess carries forward for 5 years; see Pub. 526 for the exact limits that apply to you.");
        const Money other = f.add(y26 ? "17z" : "16", "Other itemized deductions", it.otherItemized);
        itemizedLine_ = y26 ? "18" : "17";
        return f.add(itemizedLine_, "Total itemized deductions", l4 + saltLine7_ + l10 + gifts + other);
    }

    Money salt(Money l5d, Money agi) {
        const Money floor = mfs() ? pct(rules_.saltFloor, 50) : rules_.saltFloor;
        if (l5d <= floor) return l5d;
        Builder& f = begin("SALT Worksheet", "State and Local Tax Deduction Worksheet (Schedule A, line 5e)", kOrderWorksheet, true);
        const Money l1 = f.add("1", "Cap", rules_.saltCap);
        const Money l4 = f.add("4", "Modified AGI", agi);
        const Money l5 = f.add("5", "Phase-down threshold", mfs() ? rules_.saltPhaseStartMfs : rules_.saltPhaseStart);
        Money l8 = l1;
        if (l4 > l5) {
            const Money l6 = f.add("6", "Subtract line 5 from line 4", l4 - l5);
            const Money l7 = f.add("7", "Line 6 x 30%", pct(l6, rules_.saltPhasePercent));
            l8 = f.add("8", "Subtract line 7 from line 1", l1 - l7);
        }
        const Money l9 = f.add("9", "Larger of line 8 or $10,000", maxOf(l8, rules_.saltFloor));
        return f.add("10", "State and local tax deduction", minOf(mfs() ? pct(l9, 50) : l9, l5d),
                     mfs() ? "Smaller of half of line 9 or Schedule A line 5d" : "Smaller of line 9 or Schedule A line 5d");
    }

    void deductions() {
        standard_ = standardDeduction();
        itemized_ = scheduleA();
        const bool mustItemize = mfs() && r_.info.spouseItemizes;
        // From 2026, people who don't itemize can deduct some cash gifts (line 12f), so that is
        // part of the comparison.
        const Money charityCap = pick(rules_.nonItemizerCharity, st_);
        const Money nonItemizer = mustItemize ? kZero : minOf(r_.itemized.charityCash, charityCap);
        itemize_ = !itemized_.isZero() && (itemized_ > standard_ + nonItemizer || r_.info.forceItemize || mustItemize);
        const Money d = itemize_ ? itemized_ : standard_;
        if (!itemize_ && !itemized_.isZero()) {
            for (auto& b : builders_) {
                if (b.form.id == "Schedule A" || b.form.id == "SALT Worksheet") {
                    b.form.worksheet = true;
                    b.form.title += " (not filed: the standard deduction is larger)";
                    b.order = kOrderWorksheet;
                }
            }
        }
        std::string how;
        if (itemize_) {
            how = "Itemized deductions (Schedule A) of " + usd(itemized_);
            if (itemized_ > standard_ + nonItemizer) {
                how += ", more than the " + usd(standard_) + " standard deduction";
                if (!nonItemizer.isZero()) how += " plus " + usd(nonItemizer) + " of non-itemizer charitable gifts";
            } else {
                how += mustItemize ? " (required because your spouse itemizes)" : " (you chose to itemize)";
            }
        } else {
            how = "Standard deduction for " + std::string(choiceLabel(st_)) + " (" + usd(standard_) + ")";
            if (!itemized_.isZero()) how += ", more than itemized deductions of " + usd(itemized_);
            if (mustItemize) how = "Zero: your spouse itemizes on a separate return";
        }
        f1040().add("12e", "Standard deduction or itemized deductions", d, how);
        if (itemize_ && charityProvisional_)
            diag(Severity::Info, "Charitable gifts",
                 "Provisional: the 2026 Charitable Contribution Limitation Worksheet isn't published yet. OpenTax applies "
                 "the 60%/30% AGI limits and the new 0.5% floor as written in the law.");
        if (rules_.formsYear >= 2026) {
            nonItemizer_ = itemize_ ? kZero : nonItemizer;
            f1040().add("12f", "Charitable contribution deduction for non-itemizers", nonItemizer_,
                        nonItemizer_.isZero() ? std::string()
                                              : "Cash gifts of " + usd(r_.itemized.charityCash) + ", up to " + usd(charityCap));
            if (!nonItemizer_.isZero())
                diag(Severity::Info, "Charitable gifts",
                     "Line 12f counts only cash gifts to public charities; gifts to donor-advised funds and supporting organizations don't qualify.");
        }
    }

    // Sec. 68 (from 2026): itemized deductions are reduced by 2/37 of the smaller of the itemized
    // deductions or the amount by which taxable income plus those deductions exceeds the start
    // of the 37% bracket.
    void itemizedLimitation() {
        if (!rules_.itemizedLimitation || !itemize_) return;
        const LineIds& ids = lineIds(rules_.formsYear);
        const Money base = L("11b") - L(ids.schedule1A) - L(ids.qbi);
        const auto& brackets = pick(rules_.brackets, st_);
        const Money top = brackets[brackets.size() - 2].upTo;
        if (base <= top) return;
        Builder& f = begin("Itemized Deduction Limitation", "Itemized Deductions Limitation (Schedule A, line 18)", kOrderWorksheet, true);
        const Money l1 = f.add("1", "Itemized deductions before the limitation", itemized_);
        f.add("2", "Taxable income plus itemized deductions", base,
              std::string("Form 1040 line 11b minus lines ") + ids.schedule1A + " and " + ids.qbi);
        f.add("3", "Start of the 37% bracket", top);
        const Money l4 = f.add("4", "Subtract line 3 from line 2", base - top);
        const Money l5 = f.add("5", "Smaller of line 1 or line 4", minOf(l1, l4));
        const std::int64_t twice = l5.cents() * 2;
        const Money l6 = f.add("6", "Line 5 x 2/37", Money::fromCents((twice + 18) / 37));
        const Money l7 = f.add("7", "Limited itemized deductions", l1 - l6, "To Schedule A, line 18, and Form 1040, line 12e");
        diag(Severity::Info, "Itemized deductions",
             "Provisional: your itemized deductions are reduced by " + usd(l6) +
                 " under the 2026 limit for the 37% bracket. The IRS worksheet isn't published yet; OpenTax follows the law as written.");
        itemized_ = l7;
        for (auto& b : builders_) {
            for (auto& line : b.form.lines) {
                if ((b.form.id == "Schedule A" && line.number == itemizedLine_) || (b.form.id == "1040" && line.number == "12e")) {
                    line.amount = l7;
                    line.how = "Itemized deductions of " + usd(l1) + " less the " + usd(l6) + " limitation for the 37% bracket";
                }
            }
        }
    }

    // ------------------------------------------------------- Schedule 1-A
    Money sch1ASenior_;
    Money nonItemizer_;
    std::string itemizedLine_ = "17";
    bool charityProvisional_ = false;

    void schedule1A() {
        const LineIds& ids = lineIds(rules_.formsYear);
        const bool y26 = rules_.formsYear >= 2026;
        auto N = [y26](const char* a2025, const char* a2026) { return std::string(y26 ? a2026 : a2025); };
        Money tips, overtime;
        for (const auto& w : r_.w2s) {
            if (!counts(w.owner) || !person(w.owner).hasValidSsn) continue;
            tips += w.qualifiedTips;
            overtime += w.qualifiedOvertime;
        }
        const Money car = r_.adjustments.carLoanInterest;
        const bool senTp = is65(r_.taxpayer) && r_.taxpayer.hasValidSsn;
        const bool senSp = joint() && is65(r_.spouse) && r_.spouse.hasValidSsn;
        const std::string toLine = std::string("Additional deductions from Schedule 1-A, line ") + ids.sch1ATotal;
        if (tips.isZero() && overtime.isZero() && car.isZero() && !senTp && !senSp) {
            f1040().add(ids.schedule1A, toLine, kZero);
            return;
        }
        Builder& f = begin("Schedule 1-A", "Additional Deductions", kOrderSch1A);
        const Money magi = f.add("3", "Modified AGI", L("11b"), "Form 1040, line 11b");
        const bool marriedSeparate = mfs();
        Money tipsDed, overtimeDed, carDed, senior;
        if (!tips.isZero()) {
            if (marriedSeparate) {
                diag(Severity::Warning, "No tax on tips", "Married couples must file jointly to deduct qualified tips.");
            } else {
                f.add(N("4c", "5"), "Qualified tips received as an employee", tips, "Qualified tips entered on Forms W-2");
                const Money limited = f.add(N("7", "9"), "Smaller of qualified tips or $25,000", minOf(tips, rules_.tipsLimit));
                const Money start = pick(rules_.tipsPhaseStart, st_);
                const Money excess = f.add(N("10", "12"), "Modified AGI over " + usd(start), pos(magi - start));
                const Money reduce = f.add(N("12", "14"), "$100 for each full $1,000 of the excess", dollars(100 * thousandsDown(excess)));
                tipsDed = f.add(N("13", "15"), "Qualified tips deduction", pos(limited - reduce));
            }
        }
        if (!overtime.isZero()) {
            if (marriedSeparate) {
                diag(Severity::Warning, "No tax on overtime", "Married couples must file jointly to deduct qualified overtime.");
            } else {
                f.add(N("14c", "17"), "Qualified overtime compensation", overtime, "Qualified overtime entered on Forms W-2");
                const Money limited = f.add(N("15", "21"), "Smaller of qualified overtime or " + usd(pick(rules_.overtimeLimit, st_)),
                                            minOf(overtime, pick(rules_.overtimeLimit, st_)));
                const Money start = pick(rules_.tipsPhaseStart, st_);
                const Money excess = f.add(N("18", "24"), "Modified AGI over " + usd(start), pos(magi - start));
                const Money reduce = f.add(N("20", "26"), "$100 for each full $1,000 of the excess", dollars(100 * thousandsDown(excess)));
                overtimeDed = f.add(N("21", "27"), "Qualified overtime compensation deduction", pos(limited - reduce));
            }
        }
        if (!car.isZero()) {
            f.add(N("23", "29"), "Qualified passenger vehicle loan interest", car);
            const Money limited = f.add(N("24", "30"), "Smaller of the interest or $10,000", minOf(car, rules_.carLoanLimit));
            const Money start = pick(rules_.carLoanPhaseStart, st_);
            const Money excess = f.add(N("27", "33"), "Modified AGI over " + usd(start), pos(magi - start));
            const Money reduce = f.add(N("29", "35"), "$200 for each $1,000 (or part) of the excess", dollars(200 * thousandsUp(excess)));
            carDed = f.add(N("30", "36"), "Car loan interest deduction", pos(limited - reduce));
        }
        if (senTp || senSp) {
            if (marriedSeparate) {
                diag(Severity::Warning, "Senior deduction", "Married couples must file jointly to take the enhanced deduction for seniors.");
            } else {
                const Money start = pick(rules_.seniorPhaseStart, st_);
                const Money excess = f.add(N("33", "39"), "Modified AGI over " + usd(start), pos(magi - start));
                const Money reduce = f.add(N("34", "40"), "Excess x 6%", pct(excess, rules_.seniorPhasePercent));
                const Money each = f.add(N("35", "41"), "Subtract that from $6,000", pos(rules_.seniorDeduction - reduce));
                const Money a = f.add(N("36a", "42a"), "You (born before " + seniorBirthDate() + ")", senTp ? each : kZero);
                const Money b = joint() ? f.add(N("36b", "42b"), "Spouse (born before " + seniorBirthDate() + ")", senSp ? each : kZero) : kZero;
                senior = f.add(ids.sch1ASenior, "Enhanced deduction for seniors", a + b);
            }
        }
        sch1ASenior_ = senior;
        const Money total = f.add(ids.sch1ATotal, "Total additional deductions", tipsDed + overtimeDed + carDed + senior,
                                  std::string("To Form 1040, line ") + ids.schedule1A);
        f1040().add(ids.schedule1A, toLine, total);
    }

    // ------------------------------------------------------------- QBI
    Money netCapitalGainForRates() const {
        // QDCG worksheet line 4 / Form 8995 line 12.
        Money gains = hasSchD_ ? (schD15_ > kZero && schD16_ > kZero ? minOf(schD15_, schD16_) : kZero) : capGainDist_;
        return L("3a") + gains;
    }

    void qbi() {
        const LineIds& ids = lineIds(rules_.formsYear);
        const bool y26 = rules_.formsYear >= 2026;
        Money reit;
        for (const auto& d : r_.dividends) reit += d.section199a;
        const bool hasBiz = !biz_.empty();
        if (!hasBiz && reit.isZero() && r_.carryovers.qbiLoss.isZero()) {
            f1040().add(ids.qbi, "Qualified business income deduction", kZero);
            return;
        }
        const Money before = L("11b") - L("12e") - L("12f") - L(ids.schedule1A);
        if (before > pick(rules_.qbiThreshold, st_)) {
            diag(Severity::Error, "QBI deduction",
                 "Taxable income before the QBI deduction (" + usd(before) + ") is above " + usd(pick(rules_.qbiThreshold, st_)) +
                     ". Form 8995-A (wage and property limits, specified service businesses) isn't supported yet, so no QBI deduction is taken.");
            f1040().add(ids.qbi, "Qualified business income deduction", kZero, "Not computed: Form 8995-A required");
            return;
        }
        Builder& f = begin("Form 8995", "Qualified Business Income Deduction Simplified Computation", kOrder8995);
        Money totalNet, activeNet;
        int n = 0;
        for (const auto& b : biz_) {
            f.add("1." + std::to_string(++n), b.name + " net profit", b.net);
            totalNet += b.net;
            if (b.active) activeNet += b.net;
        }
        const Money half = seHalf_[0] + (joint() ? seHalf_[1] : kZero);
        const Money reductions = half + seHealth_ + r_.adjustments.sepSimple;
        if (!reductions.isZero())
            f.add("1.adj", "Less deductible SE tax, SE health insurance and SEP", -reductions,
                  "These deductions reduce qualified business income");
        const Money l2 = f.add("2", "Total qualified business income or (loss)", totalNet - reductions);
        const Money l3 = f.add("3", "Qualified business net loss carryforward", -r_.carryovers.qbiLoss);
        const Money l4 = f.add("4", "Total qualified business income", pos(l2 + l3));
        const Money l5 = f.add("5", "Line 4 x 20%", pct(l4, 20));
        const Money l6 = f.add("6", "Qualified REIT dividends (1099-DIV box 5)", reit);
        const Money l8 = f.add("8", "Total qualified REIT dividends", pos(l6));
        const Money l9 = f.add("9", "Line 8 x 20%", pct(l8, 20));
        const Money l10 = f.add("10", "Add lines 5 and 9", l5 + l9);
        const Money l11 = f.add("11", "Taxable income before QBI deduction", before,
                                std::string("Line 11b minus lines 12e") + (y26 ? ", 12f" : "") + " and " + ids.schedule1A);
        const Money l12 = f.add("12", "Net capital gain", netCapitalGainForRates(), "Qualified dividends plus net capital gain");
        const Money l13 = f.add("13", "Subtract line 12 from line 11", pos(l11 - l12));
        const Money l14 = f.add("14", "Income limitation (line 13 x 20%)", pct(l13, 20));
        const Money l15 = f.add("15", y26 ? "Deduction before the minimum deduction" : "Qualified business income deduction",
                                minOf(l10, l14), "Smaller of line 10 or line 14");
        Money deduction = l15;
        std::string carryLine = "16";
        if (y26) {
            // Minimum deduction for at least $1,000 of QBI from businesses you materially participate
            // in. Deductions that reduce QBI are all attributed to the active businesses.
            const Money activeQbi = activeNet - reductions;
            const bool eligible = !rules_.qbiMinimumDeduction.isZero() && activeQbi >= rules_.qbiMinimumActive;
            const Money l16 = f.add("16", "Minimum deduction for active qualified business income",
                                    eligible ? rules_.qbiMinimumDeduction : kZero,
                                    eligible ? "At least " + usd(rules_.qbiMinimumActive) + " of QBI from businesses you materially participate in"
                                             : std::string());
            deduction = f.add("17", "Qualified business income deduction", maxOf(l15, l16), "Larger of line 15 or line 16");
            carryLine = "18";
        }
        if (l2 + l3 < kZero) {
            f.add(carryLine, "Net loss carryforward to " + yr(1), l2 + l3);
            diag(Severity::Info, "QBI deduction", "Your qualified business loss of " + usd(-(l2 + l3)) + " carries forward to " +
                                                      yr(1) + " (Form 8995, line " + carryLine + ").");
        }
        f1040().add(ids.qbi, "Qualified business income deduction", deduction, std::string("Form 8995, line ") + (y26 ? "17" : "15"));
    }

    void taxableIncome() {
        const bool y26 = rules_.formsYear >= 2026;
        const Money l14 = f1040().add("14", "Total deductions", L("12e") + L("12f") + L("13a") + L("13b"),
                                      y26 ? "Add lines 12e, 12f, 13a, and 13b" : "Add lines 12e, 13a, and 13b");
        f1040().add("15", "Taxable income", pos(L("11b") - l14), "Line 11b minus line 14 (not less than zero)");
    }

    // --------------------------------------------------------------- tax
    bool usedQdcg_ = false;
    Money qdcg4_, qdcg5_;

    void tax() {
        const Money ti = L("15");
        const bool pref = L("3a") > kZero || (hasSchD_ ? (schD15_ > kZero && schD16_ > kZero) : capGainDist_ > kZero);
        if (!pref) {
            const Money t = incomeTax(ti, st_, rules_);
            f1040().add("16", "Tax", t, ti < dollars(100000) ? "From the " + yr() + " Tax Table" : "From the " + yr() + " Tax Computation Worksheet");
            return;
        }
        usedQdcg_ = true;
        Builder& f = begin("QDCG Worksheet", "Qualified Dividends and Capital Gain Tax Worksheet (line 16)", kOrderWorksheet, true);
        const Money l1 = f.add("1", "Taxable income (line 15)", ti);
        const Money l2 = f.add("2", "Qualified dividends (line 3a)", L("3a"));
        const Money l3 = f.add("3", hasSchD_ ? "Smaller of Schedule D line 15 or 16" : "Capital gain distributions (line 7a)",
                               netCapitalGainForRates() - L("3a"));
        const Money l4 = f.add("4", "Add lines 2 and 3", l2 + l3);
        const Money l5 = f.add("5", "Subtract line 4 from line 1", pos(l1 - l4));
        const Money l6 = f.add("6", "0% rate threshold", pick(rules_.zeroRateTop, st_));
        const Money l7 = f.add("7", "Smaller of line 1 or line 6", minOf(l1, l6));
        const Money l8 = f.add("8", "Smaller of line 5 or line 7", minOf(l5, l7));
        const Money l9 = f.add("9", "Taxed at 0%", l7 - l8);
        const Money l10 = f.add("10", "Smaller of line 1 or line 4", minOf(l1, l4));
        const Money l11 = f.add("11", "Amount from line 9", l9);
        const Money l12 = f.add("12", "Subtract line 11 from line 10", l10 - l11);
        const Money l13 = f.add("13", "15% rate threshold", pick(rules_.fifteenRateTop, st_));
        const Money l14 = f.add("14", "Smaller of line 1 or line 13", minOf(l1, l13));
        const Money l15 = f.add("15", "Add lines 5 and 9", l5 + l9);
        const Money l16 = f.add("16", "Subtract line 15 from line 14", pos(l14 - l15));
        const Money l17 = f.add("17", "Taxed at 15%", minOf(l12, l16));
        const Money l18 = f.add("18", "Line 17 x 15%", pct(l17, 15));
        const Money l19 = f.add("19", "Add lines 9 and 17", l9 + l17);
        const Money l20 = f.add("20", "Taxed at 20%", l10 - l19);
        const Money l21 = f.add("21", "Line 20 x 20%", pct(l20, 20));
        const Money l22 = f.add("22", "Tax on line 5", incomeTax(l5, st_, rules_), l5 < dollars(100000) ? "Tax Table" : "Tax Computation Worksheet");
        const Money l23 = f.add("23", "Add lines 18, 21, and 22", l18 + l21 + l22);
        const Money l24 = f.add("24", "Tax on line 1", incomeTax(l1, st_, rules_), l1 < dollars(100000) ? "Tax Table" : "Tax Computation Worksheet");
        const Money l25 = f.add("25", "Tax on all taxable income", minOf(l23, l24));
        qdcg4_ = l4;
        qdcg5_ = l5;
        f1040().add("16", "Tax", l25, "Qualified Dividends and Capital Gain Tax Worksheet, line 25");
    }

    // --------------------------------------------------------------- AMT
    Money amt26_28(Money x) const {
        if (x <= pick(rules_.amt28Threshold, st_)) return pct(x, 26);
        return pct(x, 28) - pick(rules_.amt28Subtract, st_);
    }

    void amt() {
        const Money l1a = L("14") - sch1ASenior_;
        const Money l1b = L("11b") - l1a;
        const Money l2a = itemize_ ? saltLine7_ : L("12e");
        const Money l4 = l1b + l2a;
        const Money exemption = pick(rules_.amtExemption, st_);
        const Money start = pick(rules_.amtPhaseStart, st_);
        const Money l5 = l4 > start ? pos(exemption - pct(l4 - start, rules_.amtPhasePercent)) : exemption;
        const Money l6 = pos(l4 - l5);
        Money amtTax;
        Money tmt;
        if (!l6.isZero()) {
            if (usedQdcg_) {
                const Money l12 = l6;
                const Money l13 = qdcg4_;
                const Money l15 = l13;
                const Money l16 = minOf(l12, l15);
                const Money l17 = l12 - l16;
                const Money l18 = amt26_28(l17);
                const Money l19 = pick(rules_.zeroRateTop, st_);
                const Money l20 = qdcg5_;
                const Money l21 = pos(l19 - l20);
                const Money l22 = minOf(l12, l13);
                const Money l23 = minOf(l21, l22);
                const Money l24 = l22 - l23;
                const Money l25 = pick(rules_.fifteenRateTop, st_);
                const Money l28 = l21 + qdcg5_;
                const Money l29 = pos(l25 - l28);
                const Money l30 = minOf(l24, l29);
                const Money l31 = pct(l30, 15);
                const Money l32 = l23 + l30;
                const Money l34 = l32 == l12 ? kZero : pct(l22 - l32, 20);
                const Money l38 = l18 + l31 + l34;
                tmt = minOf(l38, amt26_28(l12));
            } else {
                tmt = amt26_28(l6);
            }
            amtTax = pos(tmt - L("16"));
        }
        if (!amtTax.isZero()) {
            Builder& f = begin("Form 6251", "Alternative Minimum Tax", kOrder6251);
            f.add("1a", std::string("Form 1040 line 14 minus Schedule 1-A line ") + lineIds(rules_.formsYear).sch1ASenior, l1a);
            f.add("1b", "Line 11b minus line 1a", l1b);
            f.add("2a", itemize_ ? "Taxes from Schedule A, line 7" : "Standard deduction (line 12e)", l2a);
            f.add("4", "Alternative minimum taxable income", l4);
            f.add("5", "Exemption", l5, l4 > start ? "Reduced by " + rules_.amtPhasePercent.str() + "% of AMTI over " + usd(start) : "");
            f.add("6", "Subtract line 5 from line 4", l6);
            f.add("7", "Tentative minimum tax before credits", tmt, usedQdcg_ ? "Part III (capital gain rates)" : "26%/28% rates");
            f.add("9", "Tentative minimum tax", tmt);
            f.add("10", "Regular tax (Form 1040, line 16)", L("16"));
            f.add("11", "Alternative minimum tax", amtTax, "To Schedule 2, line 2");
        }
        amt_ = amtTax;
        if (!amtTax.isZero()) {
            Builder& s2 = schedule2();
            s2.add("2", "Alternative minimum tax (Form 6251)", amtTax);
            s2.add("3", "Add lines 1z and 2", amtTax, "To Form 1040, line 17");
        }
        f1040().add("17", "Amount from Schedule 2, line 3", amtTax);
        f1040().add("18", "Add lines 16 and 17", L("16") + amtTax);
    }

    Money amt_;
    std::ptrdiff_t iSch2_ = -1;
    Builder& schedule2() {
        if (iSch2_ < 0) {
            iSch2_ = static_cast<std::ptrdiff_t>(builders_.size());
            begin("Schedule 2", "Additional Taxes", kOrderSch2);
        }
        return builders_[static_cast<std::size_t>(iSch2_)];
    }

    // --------------------------------------------- nonrefundable credits
    Money careCredit_, eduNonrefundable_, aotcRefundable_, saver_, ctc_;
    int ctcChildren_ = 0;
    Money ctcLine12_;

    void nonrefundableCredits() {
        const Money line18 = L("18");
        const Money agi = L("11a");
        const bool dependentFiler = r_.taxpayer.claimedAsDependent || (joint() && r_.spouse.claimedAsDependent);

        // Form 2441
        if (!careQualifying_.empty() && !careExpenses_.isZero()) {
            if (mfs()) {
                diag(Severity::Warning, "Dependent care credit", "Married filing separately generally can't take the child and dependent care credit.");
            } else if (!careNoCredit_) {
                Builder& f = begin("Form 2441", "Child and Dependent Care Expenses", kOrder2441);
                f.text("2", "Qualifying persons", std::to_string(careQualifying_.size()), "Dependents under 13 (or disabled) who lived with you more than half the year");
                const Money l3 = f.add("3", "Qualified expenses (limited)", careLine3_,
                                       careBenefits_ ? "From Part III, line 31" : "Up to " + usd(careQualifying_.size() >= 2 ? rules_.careLimitTwo : rules_.careLimitOne));
                const Money l4 = f.add("4", "Your earned income", earnedIncome(Owner::Taxpayer));
                const Money l5 = f.add("5", joint() ? "Spouse's earned income" : "Your earned income",
                                       joint() ? earnedIncome(Owner::Spouse) : l4);
                const Money l6 = f.add("6", "Smallest of line 3, 4, or 5", pos(minOf(l3, minOf(l4, l5))));
                f.add("7", "Adjusted gross income", agi);
                const int rate = careRate(agi);
                std::string rateHow = std::to_string(rules_.careTopRate) + "% less 1% for each $2,000 (or part) of AGI over $15,000, but not below " +
                                      std::to_string(rules_.careMidRate) + "%";
                if (!pick(rules_.careSecondStart, st_).isZero())
                    rateHow += "; then less 1% for each " + usd(pick(rules_.careSecondStep, st_)) + " (or part) of AGI over " +
                               usd(pick(rules_.careSecondStart, st_)) + ", but not below 20%";
                f.text("8", "Decimal amount", "0." + std::to_string(rate), rateHow);
                const Money l9 = f.add("9c", "Line 6 x line 8", pct(l6, rate));
                const Money l10 = f.add("10", "Tax liability limit", line18);
                careCredit_ = f.add("11", "Credit for child and dependent care expenses", minOf(l9, l10), "To Schedule 3, line 2");
                if (l4.isZero() || l5.isZero())
                    diag(Severity::Info, "Dependent care credit", "The credit needs earned income for you (and your spouse if filing jointly). A student or disabled spouse may be treated as having income; see Form 2441 instructions.");
            }
        }

        // Form 8863
        if (!r_.education.empty()) {
            if (mfs()) {
                diag(Severity::Warning, "Education credits", "Married filing separately can't take education credits.");
            } else if (dependentFiler) {
                diag(Severity::Warning, "Education credits", "If you can be claimed as a dependent, only the person claiming you can take education credits.");
            } else {
                education(agi, line18 - careCredit_);
            }
        }

        // Form 8880
        saverCredit(agi, line18 - careCredit_ - eduNonrefundable_);

        // Schedule 8812 Part I
        childTaxCredit(agi, line18 - careCredit_ - eduNonrefundable_ - saver_);

        // Schedule 3 Part I
        const Money sch3 = careCredit_ + eduNonrefundable_ + saver_;
        if (!sch3.isZero()) {
            Builder& s3 = schedule3();
            if (!careCredit_.isZero()) s3.add("2", "Child and dependent care credit (Form 2441)", careCredit_);
            if (!eduNonrefundable_.isZero()) s3.add("3", "Education credits (Form 8863, line 19)", eduNonrefundable_);
            if (!saver_.isZero()) s3.add("4", "Retirement savings contributions credit (Form 8880)", saver_);
            s3.add("8", "Total nonrefundable credits", sch3, "To Form 1040, line 20");
        }
        f1040().add("19", "Child tax credit or credit for other dependents", ctc_, ctc_.isZero() ? "" : "Schedule 8812, line 14");
        f1040().add("20", "Amount from Schedule 3, line 8", sch3);
        const Money l21 = f1040().add("21", "Add lines 19 and 20", ctc_ + sch3);
        f1040().add("22", "Subtract line 21 from line 18", pos(line18 - l21));
    }

    // Form 2441 line 8, in whole percent.
    int careRate(Money agi) const {
        auto steps = [](Money over, Money step) {
            return over.cents() <= 0 ? 0 : static_cast<int>((over.cents() + step.cents() - 1) / step.cents());
        };
        int rate = std::max(rules_.careMidRate, rules_.careTopRate - steps(agi - dollars(15000), dollars(2000)));
        const Money second = pick(rules_.careSecondStart, st_);
        if (!second.isZero() && agi > second) rate = std::max(20, rules_.careMidRate - steps(agi - second, pick(rules_.careSecondStep, st_)));
        return rate;
    }

    std::ptrdiff_t iSch3_ = -1;
    Builder& schedule3() {
        if (iSch3_ < 0) {
            iSch3_ = static_cast<std::ptrdiff_t>(builders_.size());
            begin("Schedule 3", "Additional Credits and Payments", kOrderSch3);
        }
        return builders_[static_cast<std::size_t>(iSch3_)];
    }

    void education(Money agi, Money limit) {
        Builder& f = begin("Form 8863", "Education Credits", kOrder8863);
        Money aotcTotal, llcExpenses;
        int n = 0;
        for (const auto& e : r_.education) {
            ++n;
            const std::string who = trim(e.student).empty() ? "Student " + std::to_string(n) : e.student;
            if (e.aotcEligible) {
                const Money l27 = minOf(e.qualifiedExpenses, dollars(4000));
                const Money l28 = pos(l27 - dollars(2000));
                const Money l30 = l28.isZero() ? l27 : dollars(2000) + pct(l28, 25);
                f.add("30." + std::to_string(n), who + ": American opportunity credit", l30,
                      "100% of the first $2,000 and 25% of the next $2,000 of " + usd(e.qualifiedExpenses));
                aotcTotal += l30;
            } else {
                f.add("31." + std::to_string(n), who + ": lifetime learning expenses", e.qualifiedExpenses);
                llcExpenses += e.qualifiedExpenses;
            }
        }
        const Money end = pick(rules_.educationPhaseEnd, st_);
        const Money range = pick(rules_.educationPhaseRange, st_);
        Money l7, l8, l9;
        if (!aotcTotal.isZero()) {
            f.add("1", "Tentative American opportunity credit", aotcTotal);
            f.add("2", "Phase-out end", end);
            f.add("3", "Modified AGI", agi);
            const Money l4 = f.add("4", "Subtract line 3 from line 2", pos(end - agi));
            const std::int64_t r = ratio3(l4, range);
            f.text("6", "Phase-out fraction", ratioText(r));
            l7 = f.add("7", "Line 1 x line 6", times3(aotcTotal, r));
            l8 = f.add("8", "Refundable American opportunity credit (40%)", pct(l7, 40), "To Form 1040, line 29");
            l9 = f.add("9", "Nonrefundable part", l7 - l8);
        }
        Money l18;
        if (!llcExpenses.isZero()) {
            f.add("10", "Lifetime learning expenses", llcExpenses);
            const Money l11 = f.add("11", "Smaller of line 10 or $10,000", minOf(llcExpenses, dollars(10000)));
            const Money l12 = f.add("12", "Line 11 x 20%", pct(l11, 20));
            const Money l15 = f.add("15", "Phase-out end minus modified AGI", pos(end - agi));
            const std::int64_t r = ratio3(l15, range);
            f.text("17", "Phase-out fraction", ratioText(r));
            l18 = f.add("18", "Tentative lifetime learning credit", times3(l12, r));
        }
        aotcRefundable_ = l8;
        eduNonrefundable_ = f.add("19", "Nonrefundable education credits", minOf(l9 + l18, pos(limit)),
                                  "Limited to tax after the dependent care credit (" + usd(pos(limit)) + ")");
    }

    void saverCredit(Money agi, Money limit) {
        struct Col {
            Owner owner;
            Money contributions;
        };
        std::vector<Col> cols;
        for (Owner o : {Owner::Taxpayer, Owner::Spouse}) {
            if (o == Owner::Spouse && !joint()) continue;
            const Person& p = person(o);
            Money c = p.traditionalIra + p.rothIra;
            for (const auto& w : r_.w2s) {
                if (w.owner == o) c += w.electiveDeferrals;
            }
            if (c.isZero()) continue;
            const bool tooYoung = !p.birthDate || irsAgeAtEndOfYear(*p.birthDate, year()) < 18;
            if (p.claimedAsDependent || p.fullTimeStudent || tooYoung) continue;
            cols.push_back({o, c});
        }
        if (cols.empty()) return;
        int rate = 0;
        for (const auto& t : rules_.saverTiers) {
            if (agi <= pick(t.agiUpTo, st_)) {
                rate = t.ratePercent;
                break;
            }
        }
        if (rate == 0) return;
        Builder& f = begin("Form 8880", "Credit for Qualified Retirement Savings Contributions", kOrder8880);
        Money l7;
        for (const auto& c : cols) {
            const char* col = c.owner == Owner::Taxpayer ? "a" : "b";
            f.add(std::string("3") + col, ownerName(c.owner) + ": IRA contributions and elective deferrals", c.contributions);
            l7 += f.add(std::string("6") + col, ownerName(c.owner) + ": smaller of line 5 or $2,000", minOf(c.contributions, rules_.saverContributionLimit));
        }
        diag(Severity::Info, "Saver's credit", "If you took retirement distributions in " + yr(-2) + "-" + yr() + " (or before your " + yr() + " filing date), subtract them on Form 8880 line 4; OpenTax doesn't track them.");
        f.add("7", "Add the amounts on line 6", l7);
        f.add("8", "Adjusted gross income", agi);
        f.text("9", "Decimal amount", rate == 50 ? "0.5" : rate == 20 ? "0.2" : "0.1");
        const Money l10 = f.add("10", "Line 7 x line 9", pct(l7, rate));
        f.add("11", "Credit limit", pos(limit));
        saver_ = f.add("12", "Credit for qualified retirement savings contributions", minOf(l10, pos(limit)), "To Schedule 3, line 4");
    }

    void childTaxCredit(Money agi, Money limit) {
        int children = 0;
        int others = 0;
        for (const auto& d : r_.dependents) {
            const bool under17 = d.birthDate && ageAtEndOfYear(*d.birthDate, year()) < 17;
            if (isQualifyingRelationship(d.relationship) && under17 && d.monthsLivedWithYou >= 7 && d.hasValidSsn) ++children;
            else ++others;
        }
        if (children == 0 && others == 0) return;
        Builder& f = begin("Schedule 8812", "Credits for Qualifying Children and Other Dependents", kOrder8812);
        const Money l3 = f.add("3", "Modified AGI", agi, "Form 1040, line 11a");
        f.text("4", "Qualifying children under 17 with an SSN", std::to_string(children));
        const Money l5 = f.add("5", "Line 4 x " + usd(rules_.ctcPerChild), multiply(rules_.ctcPerChild, Decimal::fromInt(children)));
        f.text("6", "Other dependents", std::to_string(others), "Dependents who aren't qualifying children under 17 with an SSN");
        const Money l7 = f.add("7", "Line 6 x " + usd(rules_.odcPerDependent), multiply(rules_.odcPerDependent, Decimal::fromInt(others)));
        const Money l8 = f.add("8", "Add lines 5 and 7", l5 + l7);
        const Money l9 = f.add("9", "Phase-out threshold", pick(rules_.ctcPhaseoutStart, st_));
        const Money l10 = f.add("10", "Line 3 over line 9, rounded up to a multiple of $1,000", dollars(1000 * thousandsUp(pos(l3 - l9))));
        const Money l11 = f.add("11", "Line 10 x 5%", pct(l10, 5));
        ctcChildren_ = children;
        if (l8 <= l11) {
            f.add("12", "Credit after phase-out", kZero, "Line 8 is not more than line 11");
            return;
        }
        ctcLine12_ = f.add("12", "Subtract line 11 from line 8", l8 - l11);
        const Money l13 = f.add("13", "Credit Limit Worksheet A", pos(limit), "Tax (line 18) minus the credits on Schedule 3, lines 1-4");
        ctc_ = f.add("14", "Child tax credit and credit for other dependents", minOf(ctcLine12_, l13), "To Form 1040, line 19");
    }

    // ------------------------------------------------------- other taxes
    Money addlMedicareWithheld_;

    void otherTaxes() {
        const Money se = seTax_[0] + (joint() ? seTax_[1] : kZero);
        // Additional tax on early distributions (reported directly on Schedule 2, line 8).
        Money early;
        std::string earlyHow;
        for (const auto& x : r_.retirement) {
            if (!counts(x.owner)) continue;
            const std::string code = toLower(x.code);
            int rate = 0;
            if (code.find('1') != std::string::npos || code.find('j') != std::string::npos) rate = 10;
            if (code.find('s') != std::string::npos) rate = 25;
            if (rate == 0) continue;
            Money taxable = x.taxable.isZero() && x.taxableNotDetermined ? x.gross : x.taxable;
            const Money base = pos(taxable - x.penaltyException);
            early += pct(base, rate);
            earlyHow += (earlyHow.empty() ? "" : "; ") + std::to_string(rate) + "% of " + usd(base) + " (" + x.payer + ")";
        }

        // Form 8959. In 2026 the wage part (line 12) goes to Schedule 2 line 17b and the
        // self-employment part (line 18) to line 11; in 2025 the total (line 18) goes to line 11.
        const bool y26 = rules_.formsYear >= 2026;
        Money medWages, medWithheld;
        for (const auto& w : r_.w2s) {
            if (!counts(w.owner)) continue;
            medWages += w.medicareWages;
            medWithheld += w.medicareWithheld;
        }
        const Money seIncome = pos(seEarnings_[0] + (joint() ? seEarnings_[1] : kZero));
        const Money thresh = pick(rules_.medicareThreshold, st_);
        Money medicareWagesTax, medicareSeTax;
        const Money l21 = pct(medWages, "1.45");
        const Money l22 = pos(medWithheld - l21);
        if (medWages > thresh || medWages + seIncome > thresh || !l22.isZero()) {
            Builder& f = begin("Form 8959", "Additional Medicare Tax", kOrder8959);
            const Money l1 = f.add("1", "Medicare wages (W-2 box 5)", medWages);
            if (y26) f.add("4", "Add lines 1 through 3", l1);
            const Money l5 = f.add("5", "Threshold", thresh);
            const Money l6 = f.add("6", y26 ? "Subtract line 5 from line 4" : "Subtract line 5 from line 1", pos(l1 - l5));
            medicareWagesTax = f.add("7", "Line 6 x 0.9%", pct(l6, "0.9"));
            if (y26) f.add("12", "Additional Medicare Tax on wages", medicareWagesTax, "To Schedule 2, line 17b");
            if (!seIncome.isZero()) {
                if (y26) {
                    const Money l13 = f.add("13", "Self-employment income (Schedule SE line 6)", seIncome);
                    const Money l14 = f.add("14", "Threshold", thresh);
                    const Money l15 = f.add("15", "Amount from line 4", l1);
                    const Money l16 = f.add("16", "Subtract line 15 from line 14", pos(l14 - l15));
                    const Money l17 = f.add("17", "Subtract line 16 from line 13", pos(l13 - l16));
                    medicareSeTax = f.add("18", "Additional Medicare Tax on self-employment income", pct(l17, "0.9"), "To Schedule 2, line 11");
                } else {
                    const Money l8 = f.add("8", "Self-employment income (Schedule SE line 6)", seIncome);
                    const Money l11 = f.add("11", "Threshold minus Medicare wages", pos(thresh - l1));
                    const Money l12 = f.add("12", "Subtract line 11 from line 8", pos(l8 - l11));
                    medicareSeTax = f.add("13", "Line 12 x 0.9%", pct(l12, "0.9"));
                }
            }
            if (!y26) f.add("18", "Additional Medicare Tax", medicareWagesTax + medicareSeTax, "To Schedule 2, line 11");
            f.add("19", "Medicare tax withheld (W-2 box 6)", medWithheld);
            f.add("21", "Line 20 x 1.45% (regular Medicare tax)", l21);
            addlMedicareWithheld_ = f.add("24", "Additional Medicare Tax withholding", l22, "To Form 1040, line 25c");
        }

        // Form 8960
        const Money niiThresh = pick(rules_.niitThreshold, st_);
        Money niit;
        const Money nii = pos(L("2b") + L("3b") + L("7a"));
        if (L("11b") > niiThresh && !nii.isZero()) {
            Builder& f = begin("Form 8960", "Net Investment Income Tax", kOrder8960);
            f.add("1", "Taxable interest", L("2b"));
            f.add("2", "Ordinary dividends", L("3b"));
            f.add("5a", "Net gain or loss", L("7a"));
            const Money l12 = f.add("12", "Net investment income", nii);
            const Money l13 = f.add("13", "Modified AGI", L("11b"));
            const Money l14 = f.add("14", "Threshold", niiThresh);
            const Money l15 = f.add("15", "Subtract line 14 from line 13", pos(l13 - l14));
            const Money l16 = f.add("16", "Smaller of line 12 or line 15", minOf(l12, l15));
            niit = f.add("17", "Net investment income tax (3.8%)", pct(l16, "3.8"), y26 ? "To Schedule 2, line 6" : "To Schedule 2, line 12");
        }

        const Money total = se + early + medicareWagesTax + medicareSeTax + niit;
        if (!total.isZero()) {
            Builder& s2 = schedule2();
            if (!se.isZero()) s2.add("4", "Self-employment tax (Schedule SE)", se);
            if (y26) {
                if (!early.isZero()) s2.add("5", "Additional tax on IRAs or other tax-favored accounts", early, earlyHow);
                if (!niit.isZero()) s2.add("6", "Net investment income tax (Form 8960)", niit);
                if (!medicareSeTax.isZero()) s2.add("11", "Additional Medicare Tax on self-employment income (Form 8959)", medicareSeTax);
                s2.add("15", "Total additional income taxes", se + early + niit + medicareSeTax, "Add lines 4 through 11 and line 14");
                if (!medicareWagesTax.isZero()) {
                    s2.add("17b", "Additional Medicare Tax on Medicare wages (Form 8959)", medicareWagesTax);
                    s2.add("17d", "Total other employment taxes", medicareWagesTax);
                }
                schedule2Line20_ = s2.add("20", "Total additional employment and other taxes", medicareWagesTax);
            } else {
                if (!early.isZero()) s2.add("8", "Additional tax on early distributions", early, earlyHow);
                if (!medicareWagesTax.isZero() || !medicareSeTax.isZero())
                    s2.add("11", "Additional Medicare Tax (Form 8959)", medicareWagesTax + medicareSeTax);
                if (!niit.isZero()) s2.add("12", "Net investment income tax (Form 8960)", niit);
            }
            s2.add("21", "Total other taxes", total, "To Form 1040, line 23");
        }
        f1040().add("23", y26 ? "Additional taxes, including self-employment tax" : "Other taxes, including self-employment tax", total);
        if (y26) {
            const Money l24a = f1040().add("24a", "Total tax", L("22") + total, "Add lines 22 and 23");
            f1040().add("24b", "Amount from Form 1062, line 15", kZero, "Deferred tax on qualified farmland sales (not supported)");
            f1040().add("24c", "Add lines 24a and 24b", l24a);
        } else {
            f1040().add("24", "Total tax", L("22") + total, "Add lines 22 and 23");
        }
    }

    // ------------------------------------------- payments and refundables
    Money eic_;
    Money schedule2Line20_;

    void refundableCreditsAndPayments() {
        Money w2Withheld, form1099;
        for (const auto& w : r_.w2s) {
            if (counts(w.owner)) w2Withheld += w.federalWithheld;
        }
        for (const auto& i : r_.interest) form1099 += i.federalWithheld;
        for (const auto& d : r_.dividends) form1099 += d.federalWithheld;
        for (const auto& x : r_.retirement) {
            if (counts(x.owner)) form1099 += x.federalWithheld;
        }
        for (const auto& s : r_.socialSecurity) {
            if (counts(s.owner)) form1099 += s.federalWithheld;
        }
        for (const auto& u : r_.unemployment) form1099 += u.federalWithheld;
        f1040().add("25a", "Federal income tax withheld from Form(s) W-2", w2Withheld);
        f1040().add("25b", "Federal income tax withheld from Form(s) 1099", form1099);
        f1040().add("25c", "Withheld from other forms", addlMedicareWithheld_, addlMedicareWithheld_.isZero() ? "" : "Additional Medicare Tax withholding (Form 8959, line 24)");
        const Money withheld = f1040().add("25d", "Total federal income tax withheld", w2Withheld + form1099 + addlMedicareWithheld_);
        const Money est = r_.payments.estimated + r_.payments.priorYearApplied;
        f1040().add("26", yr() + " estimated tax payments and amount applied from " + yr(-1) + " return", est);

        // Excess social security (Schedule 3, line 11).
        Money excessSs;
        for (Owner o : {Owner::Taxpayer, Owner::Spouse}) {
            if (!counts(o)) continue;
            Money ss;
            int employers = 0;
            for (const auto& w : r_.w2s) {
                if (w.owner != o) continue;
                ss += w.ssWithheld;
                ++employers;
            }
            if (ss > rules_.maxSsWithholding) {
                if (employers >= 2) excessSs += ss - rules_.maxSsWithholding;
                else diag(Severity::Warning, "Social security tax", ownerName(o) + ": one employer withheld too much social security tax. Ask the employer for a refund; it can't be claimed on your return.");
            }
        }

        earnedIncomeCredit();
        f1040().add("27a", "Earned income credit (EIC)", eic_, eic_.isZero() ? "" : "EIC Worksheet");
        const Money actc = additionalChildTaxCredit(excessSs);
        f1040().add("28", "Additional child tax credit", actc, actc.isZero() ? "" : "Schedule 8812, line 27");
        f1040().add("29", "American opportunity credit (refundable)", aotcRefundable_, aotcRefundable_.isZero() ? "" : "Form 8863, line 8");

        const Money sch3b = r_.payments.extension + excessSs;
        if (!sch3b.isZero()) {
            Builder& s3 = schedule3();
            if (!r_.payments.extension.isZero()) s3.add("10", "Amount paid with request for extension", r_.payments.extension);
            if (!excessSs.isZero()) s3.add("11", "Excess social security tax withheld", excessSs, "More than " + usd(rules_.maxSsWithholding) + " withheld by two or more employers");
            s3.add("15", "Total other payments and refundable credits", sch3b, "To Form 1040, line 31");
        }
        f1040().add("31", "Amount from Schedule 3, line 15", sch3b);
        const Money refundable = eic_ + actc + aotcRefundable_ + sch3b;
        if (!rules_.publicBenefitSchedule) {
            const Money l32 = f1040().add("32", "Total other payments and refundable credits", refundable, "Add lines 27a, 28, 29, 30, and 31");
            f1040().add("33", "Total payments", withheld + est + l32, "Add lines 25d, 26, and 32");
            return;
        }
        const Money l32a = f1040().add("32a", "Total other payments and refundable credits", refundable, "Add lines 27a, 28, 29, 30, and 31");
        const Money l32b = f1040().add("32b", "Amount from Schedule 3-A", publicBenefit(l32a, sch3b));
        const Money l32c = f1040().add("32c", "Subtract line 32b from line 32a", l32a - l32b);
        f1040().add("33", "Total payments", withheld + est + l32c, "Add lines 25d, 26, and 32c");
    }

    // Schedule 3-A (2026): the refunded part of the EIC, ACTC and American opportunity credit is
    // a federal public benefit, which filers who aren't citizens, nationals or qualified aliens
    // can't receive. Everyone claiming those credits attaches it.
    Money publicBenefit(Money line32a, Money line31) {
        if ((eic_ + aotcRefundable_ + L("28")).isZero()) return kZero;
        Builder& f = begin("Schedule 3-A", "Federal Public Benefit", kOrderSch3);
        const Money l1a = f.add("1a", "Form 1040, line 32a", line32a);
        const Money l1b = f.add("1b", "Form 1040, line 31", line31);
        const Money l2 = f.add("2", "Subtract line 1b from line 1a", l1a - l1b);
        const Money l3 = f.add("3", "Form 1040, line 24a", L("24a"));
        const Money l4 = f.add("4", "Schedule 2, line 20", schedule2Line20_);
        const Money l5 = f.add("5", "Subtract line 4 from line 3", l3 - l4);
        const Money l6 = f.add("6", "Federal public benefit", pos(l2 - l5), "The part of your refundable credits that is paid to you, beyond your income tax");
        const bool eligible = r_.info.citizenOrQualifiedAlien;
        f.text("8q", "U.S. citizen, U.S. national, or qualified alien?", eligible ? "Yes" : "No");
        const Money l8 = f.add("8", "To Form 1040, line 32b", eligible ? kZero : l6);
        if (!eligible && !l6.isZero())
            diag(Severity::Warning, "Refundable credits", "Because neither you nor your spouse is a U.S. citizen, U.S. national or qualified alien, " +
                                                              usd(l6) + " of refundable credits can't be paid to you (Schedule 3-A).");
        return l8;
    }

    void earnedIncomeCredit() {
        int kids = 0;
        for (const auto& d : r_.dependents) {
            if (!isQualifyingRelationship(d.relationship) || d.monthsLivedWithYou < 7 || !d.hasValidSsn || !d.birthDate) continue;
            const int age = ageAtEndOfYear(*d.birthDate, year());
            if (age < 19 || (age < 24 && d.fullTimeStudent) || d.permanentlyDisabled) ++kids;
        }
        kids = std::min(kids, 3);
        const Money earned = L("1z") + seEarned_[0] + (joint() ? seEarned_[1] : kZero);
        if (earned <= kZero) return;
        if (r_.taxpayer.claimedAsDependent) return;
        if (!r_.taxpayer.hasValidSsn) return;
        if (mfs() && !(r_.info.separatedForEic && kids > 0)) return;
        const Money investment = L("2a") + L("2b") + L("3b") + pos(L("7a"));
        if (investment > rules_.eicInvestmentLimit) {
            if (earned < dollars(70000))
                diag(Severity::Info, "Earned income credit", "Investment income of " + usd(investment) + " is over " + usd(rules_.eicInvestmentLimit) + ", so you can't take the EIC.");
            return;
        }
        if (kids == 0) {
            auto ageOk = [&](const Person& p) {
                if (!p.birthDate) return false;
                const int age = irsAgeAtEndOfYear(*p.birthDate, year());
                return age >= 25 && age < 65;
            };
            if (!ageOk(r_.taxpayer) && !(joint() && ageOk(r_.spouse))) {
                if (!r_.taxpayer.birthDate && earned < dollars(20000))
                    diag(Severity::Info, "Earned income credit", "Add your date of birth to check whether you qualify for the EIC without a qualifying child (age 25-64).");
                return;
            }
        }
        const Money l2 = eicTable(earned, kids, joint(), rules_);
        if (l2.isZero()) return;
        Builder& f = begin("EIC Worksheet", "Earned Income Credit Worksheet (line 27a)", kOrderWorksheet, true);
        f.text("A", "Qualifying children", std::to_string(kids));
        f.add("1", "Earned income", earned, "Wages plus net self-employment earnings");
        f.add("2", "Credit for earned income (EIC Table)", l2);
        const Money agi = f.add("3", "Adjusted gross income", L("11b"));
        const EicColumn& c = rules_.eic[static_cast<std::size_t>(kids)];
        const Money start = joint() ? c.phaseoutStartJoint : c.phaseoutStart;
        Money credit = l2;
        if (agi != earned && agi >= start) {
            const Money l5 = f.add("5", "Credit for AGI (EIC Table)", eicTable(agi, kids, joint(), rules_));
            credit = minOf(l2, l5);
        }
        eic_ = f.add("6", "Earned income credit", credit, "To Form 1040, line 27a");
    }

    Money additionalChildTaxCredit(Money excessSs) {
        if (ctcChildren_ == 0 || ctcLine12_ <= ctc_) return kZero;
        Builder* f = nullptr;
        for (auto& b : builders_) {
            if (b.form.id == "Schedule 8812") f = &b;
        }
        if (!f) return kZero;
        const Money l16a = f->add("16a", "Subtract line 14 from line 12", ctcLine12_ - ctc_);
        const Money l16b = f->add("16b", "Children x " + usd(rules_.actcPerChild), multiply(rules_.actcPerChild, Decimal::fromInt(ctcChildren_)));
        const Money l17 = f->add("17", "Smaller of line 16a or 16b", minOf(l16a, l16b));
        const Money earned = L("1z") + seEarned_[0] + (joint() ? seEarned_[1] : kZero);
        const Money l18a = f->add("18a", "Earned income", earned);
        const Money l19 = f->add("19", "Earned income over $2,500", pos(l18a - rules_.actcEarnedFloor));
        const Money l20 = f->add("20", "Line 19 x 15%", pct(l19, 15));
        Money result;
        if (l16b < dollars(5100) || l20 >= l17) {
            result = minOf(l17, l20);
        } else {
            Money withheld;
            for (const auto& w : r_.w2s) {
                if (counts(w.owner)) withheld += w.ssWithheld + w.medicareWithheld;
            }
            const Money l21 = f->add("21", "Social security and Medicare tax withheld", withheld);
            const Money l22 = f->add("22", "Deductible part of SE tax (Schedule 1, line 15)", seHalf_[0] + (joint() ? seHalf_[1] : kZero));
            const Money l23 = f->add("23", "Add lines 21 and 22", l21 + l22);
            const Money l24 = f->add("24", "EIC plus excess social security", eic_ + excessSs);
            const Money l25 = f->add("25", "Subtract line 24 from line 23", pos(l23 - l24));
            const Money l26 = f->add("26", "Larger of line 20 or line 25", maxOf(l20, l25));
            result = minOf(l17, l26);
        }
        return f->add("27", "Additional child tax credit", result, "To Form 1040, line 28");
    }

    // ------------------------------------------------------------- finish
    void finish() {
        const LineIds& ids = lineIds(rules_.formsYear);
        const Money tax = L(ids.totalTax);
        const Money paid = L("33");
        const Money over = pos(paid - tax);
        f1040().add("34", "Amount overpaid", over);
        const Money applied = minOf(r_.payments.applyToNextYear, over);
        f1040().add("35a", "Refund", over - applied);
        f1040().add("36", "Applied to " + yr(1) + " estimated tax", applied);
        f1040().add("37", "Amount you owe", pos(tax - paid));

        capitalLossCarryover();

        Summary& s = result_.summary;
        s.totalIncome = L("9");
        s.agi = L("11a");
        s.deduction = L("12e");
        s.itemized = itemize_;
        s.standardDeduction = standard_;
        s.itemizedDeduction = itemized_;
        s.nonItemizerCharity = L("12f");
        s.qbiDeduction = L(ids.qbi);
        s.schedule1A = L(ids.schedule1A);
        s.seniorDeduction = sch1ASenior_;
        s.taxableIncome = L("15");
        s.incomeTax = L("16");
        s.amt = L("17");
        s.credits = L("19") + L("20");
        s.otherTaxes = L("23");
        s.totalTax = tax;
        s.withholding = L("25d");
        s.estimatedPayments = L("26");
        s.adjustments = L("10");
        s.refundableCredits = L(ids.refundable);
        s.totalPayments = paid;
        s.overpaid = over;
        s.refund = over - applied;
        s.owed = L("37");

        diag(Severity::Info, "Scope", "OpenTax computes your federal return only. State income tax returns are not calculated.");
        if (!s.owed.isZero() && s.owed > dollars(1000))
            diag(Severity::Info, "Estimated tax penalty", "You owe more than $1,000. You may owe an underpayment penalty (Form 2210), which OpenTax doesn't compute; the IRS will bill you if so.");
    }

    void capitalLossCarryover() {
        if (!hasSchD_ || schD16_ >= kZero) return;
        const Money l2 = -capitalLine7_;
        const Money loss = -schD16_;
        if (loss <= l2 && L("15") > kZero) return;  // fully used
        Builder& f = begin("Capital Loss Carryover", "Capital Loss Carryover Worksheet (to " + yr(1) + ")", kOrderWorksheet, true);
        // Line 1 uses taxable income, which may be negative before flooring at zero.
        const Money l1 = f.add("1", "Taxable income (may be negative)", L("11b") - L("14"));
        f.add("2", "Loss from Schedule D, line 21 (as a positive amount)", l2);
        const Money l3 = f.add("3", "Add lines 1 and 2 (not less than zero)", pos(l1 + l2));
        const Money l4 = f.add("4", "Smaller of line 2 or line 3", minOf(l2, l3));
        const Money l5 = f.add("5", "Short-term loss from Schedule D, line 7", pos(-schD7_));
        const Money l6 = f.add("6", "Long-term gain from Schedule D, line 15", pos(schD15_));
        const Money l7 = f.add("7", "Add lines 4 and 6", l4 + l6);
        const Money l8 = f.add("8", "Short-term capital loss carryover to " + yr(1), pos(l5 - l7));
        const Money l9 = f.add("9", "Long-term loss from Schedule D, line 15", pos(-schD15_));
        const Money l10 = f.add("10", "Short-term gain from Schedule D, line 7", pos(schD7_));
        const Money l11 = f.add("11", "Subtract line 5 from line 4", pos(l4 - l5));
        const Money l12 = f.add("12", "Add lines 10 and 11", l10 + l11);
        const Money l13 = f.add("13", "Long-term capital loss carryover to " + yr(1), pos(l9 - l12));
        if (!(l8 + l13).isZero())
            diag(Severity::Info, "Capital losses", "You have " + usd(l8 + l13) + " of capital losses to carry over to " + yr(1) + " (" + usd(l8) + " short-term, " + usd(l13) + " long-term).");
    }

    const TaxReturn& r_;
    const Rules& rules_;
    FilingStatus st_;
    Result result_;
    std::deque<Builder> builders_;  // deque: references stay valid as forms are added
};

}  // namespace

const LineIds& lineIds(int year) {
    static const LineIds y2025{"", "13a", "13b", "24", "32", "37", "38"};
    static const LineIds y2026{"12f", "13b", "13a", "24c", "32c", "43", "44"};
    return year >= 2026 ? y2026 : y2025;
}

Result calculate(const TaxReturn& r) {
    if (!isSupportedYear(r.info.year)) {
        Result result;
        result.diagnostics.push_back({Severity::Error, "Tax year", "Tax year " + std::to_string(r.info.year) + " isn't supported. This version of OpenTax prepares 2025 and 2026 returns."});
        return result;
    }
    return Calculator(r).run();
}

Decimal marginalRate(const TaxReturn& r) {
    if (!isSupportedYear(r.info.year)) return Decimal();
    const Result base = calculate(r);
    TaxReturn more = r;
    W2 extra;
    extra.employer = "(marginal rate probe)";
    extra.wages = Money::fromCents(10000);
    more.w2s.push_back(extra);
    const Result bumped = calculate(more);
    const Money before = base.summary.totalTax - base.summary.refundableCredits;
    const Money after = bumped.summary.totalTax - bumped.summary.refundableCredits;
    // (after - before) / $100, as a percentage with 4 decimals: cents difference == percent * 100.
    return Decimal::fromRaw((after - before).cents() * 100);
}

}  // namespace ot
