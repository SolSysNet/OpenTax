#pragma once

// The federal calculation. calculate() is a pure function of the TaxReturn: it fills in
// Form 1040, the schedules, forms and worksheets line by line, and records for every line
// how it was figured. Nothing here touches the file system.

#include "opentax/model.hpp"
#include "opentax/rules.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace ot {

enum class Severity { Error, Warning, Info };

struct Diagnostic {
    Severity severity;
    std::string topic;    // e.g. "Filing status", "Form W-2 (Acme)"
    std::string message;
};

struct Line {
    std::string number;   // e.g. "11a"
    std::string label;
    Money amount;
    std::string text;     // shown instead of the amount when set (counts, rates, checkboxes)
    std::string how;      // plain-language explanation
};

struct FormResult {
    std::string id;       // e.g. "1040", "Schedule 1", "Schedule C (Blue Door)"
    std::string title;
    bool worksheet = false;  // a worksheet you keep, not a form you file
    std::vector<Line> lines;

    const Line* find(std::string_view number) const;
    Money get(std::string_view number) const;  // zero when absent
};

struct Summary {
    Money totalIncome;        // 1040 line 9
    Money agi;                // line 11a
    Money deduction;          // line 12e
    bool itemized = false;
    Money standardDeduction;  // what the standard deduction would be
    Money itemizedDeduction;  // Schedule A total (zero if not computed)
    Money qbiDeduction;       // line 13a
    Money schedule1A;         // line 13b
    Money taxableIncome;      // line 15
    Money incomeTax;          // line 16
    Money credits;            // lines 19 + 20
    Money otherTaxes;         // line 23
    Money totalTax;           // line 24
    Money withholding;        // line 25d
    Money refundableCredits;  // line 32
    Money totalPayments;      // line 33
    Money overpaid;           // line 34
    Money refund;             // line 35a
    Money owed;               // line 37
};

struct Result {
    std::vector<FormResult> forms;  // Form 1040 first, then schedules, forms, worksheets
    std::vector<Diagnostic> diagnostics;
    Summary summary;

    const FormResult* form(std::string_view id) const;
    Money line(std::string_view formId, std::string_view number) const;
    bool hasErrors() const;
};

Result calculate(const TaxReturn& r);

// Tax on ordinary taxable income: the Tax Table below $100,000 (tax at the midpoint of the
// $25/$50 row, rounded to whole dollars) and the Tax Computation Worksheet above.
Money incomeTax(Money taxable, FilingStatus status, const Rules& rules);

// EIC Table lookup: the credit at the midpoint of the $50 row, rounded to whole dollars.
Money eicTable(Money amount, int children, bool joint, const Rules& rules);

// Age on December 31 of `year` using the IRS rule that you reach an age the day before
// your birthday (so someone born January 1 is a year older). Used for the 65+, 50+, and
// taxpayer EIC age tests.
int irsAgeAtEndOfYear(Date birth, int year);

// Plain age on December 31 of `year`. Used for the qualifying child age tests (under 13,
// 17, 19, 24), which count a child born in year Y as age (year - Y).
int ageAtEndOfYear(Date birth, int year);

// The marginal rate on the next $100 of wages, in percent (including phase-outs).
Decimal marginalRate(const TaxReturn& r);

}  // namespace ot
