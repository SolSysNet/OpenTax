#include "opentax/report.hpp"

#include "opentax/util.hpp"

#include <algorithm>

namespace ot {
namespace {

std::string padRight(const std::string& s, std::size_t width) {
    const std::size_t w = displayWidth(s);
    return w >= width ? s : s + std::string(width - w, ' ');
}

std::string padLeft(const std::string& s, std::size_t width) {
    const std::size_t w = displayWidth(s);
    return w >= width ? s : std::string(width - w, ' ') + s;
}

std::string csvField(const std::string& s) {
    if (s.find_first_of(",\"\n") == std::string::npos) return s;
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out += '"';
        out += c;
    }
    return out + "\"";
}

// Wraps `text` to `width` columns with `indent` spaces on every line.
std::string wrap(const std::string& text, std::size_t width, std::size_t indent) {
    std::string out;
    std::string line;
    for (const auto& word : split(text, ' ')) {
        if (!line.empty() && displayWidth(line) + 1 + displayWidth(word) > width) {
            out += std::string(indent, ' ') + line + "\n";
            line.clear();
        }
        line += (line.empty() ? "" : " ") + word;
    }
    if (!line.empty()) out += std::string(indent, ' ') + line + "\n";
    return out;
}

}  // namespace

std::string formatUsd(Money m) {
    std::string s = m.formatted();
    if (!s.empty() && s[0] == '-') return "-$" + s.substr(1);
    return "$" + s;
}

std::string headline(const Summary& s) {
    if (!s.refund.isZero()) return "Federal refund " + formatUsd(s.refund);
    if (!s.owed.isZero()) return "Federal tax due " + formatUsd(s.owed);
    return "No refund or balance due";
}

std::string renderSummary(const TaxReturn& r, const Result& result) {
    const Summary& s = result.summary;
    std::string out;
    const std::string name = r.displayName();
    out += std::to_string(r.info.year) + " federal return" + (name.empty() ? "" : " for " + name) + " (" +
           choiceLabel(r.info.status) + ")\n\n";
    out += "  " + headline(s) + "\n\n";
    struct Row {
        const char* label;
        Money amount;
    };
    const Row rows[] = {
        {"Total income", s.totalIncome},
        {"Adjusted gross income", s.agi},
        {s.itemized ? "Itemized deductions" : "Standard deduction", s.deduction},
        {"QBI deduction", s.qbiDeduction},
        {"Schedule 1-A deductions", s.schedule1A},
        {"Taxable income", s.taxableIncome},
        {"Income tax", s.incomeTax},
        {"Nonrefundable credits", -s.credits},
        {"Other taxes (SE, Medicare, NIIT)", s.otherTaxes},
        {"Total tax", s.totalTax},
        {"Withholding", s.withholding},
        {"Refundable credits and other payments", s.totalPayments - s.withholding},
        {"Total payments", s.totalPayments},
    };
    for (const auto& row : rows) {
        if (row.amount.isZero() && std::string(row.label) != "Total tax" && std::string(row.label) != "Taxable income") continue;
        out += "  " + padRight(row.label, 40) + padLeft(formatUsd(row.amount), 16) + "\n";
    }
    if (!s.agi.isZero() && s.totalTax > Money()) {
        // Effective rate on AGI, one decimal place.
        const long long tenths = (s.totalTax.cents() * 1000 + s.agi.cents() / 2) / s.agi.cents();
        out += "\n  Effective tax rate: " + std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + "% of AGI\n";
    }
    std::size_t errors = 0, warnings = 0;
    for (const auto& d : result.diagnostics) {
        if (d.severity == Severity::Error) ++errors;
        if (d.severity == Severity::Warning) ++warnings;
    }
    if (errors || warnings)
        out += "\n  " + std::to_string(errors) + " error(s), " + std::to_string(warnings) +
               " warning(s). Run 'opentax check' for details.\n";
    return out;
}

std::string renderForm(const FormResult& form, bool explain) {
    std::string out = form.id == form.title ? form.title : form.id + ": " + form.title;
    out += "\n" + std::string(std::min<std::size_t>(displayWidth(out), 78), '-') + "\n";
    for (const auto& l : form.lines) {
        const std::string value = l.text.empty() ? formatUsd(l.amount) : l.text;
        std::string label = l.label;
        if (displayWidth(label) > 52) label = label.substr(0, 49) + "...";
        out += padLeft(l.number, 6) + "  " + padRight(label, 52) + padLeft(value, 16) + "\n";
        if (explain && !l.how.empty()) out += wrap(l.how, 66, 8);
    }
    return out;
}

std::string renderDiagnostics(const Result& result) {
    if (result.diagnostics.empty()) return "No issues found.\n";
    std::string out;
    for (Severity sev : {Severity::Error, Severity::Warning, Severity::Info}) {
        for (const auto& d : result.diagnostics) {
            if (d.severity != sev) continue;
            const char* tag = sev == Severity::Error ? "ERROR" : sev == Severity::Warning ? "WARN " : "info ";
            out += std::string(tag) + "  " + d.topic + ": " + d.message + "\n";
        }
    }
    return out;
}

std::string renderCsv(const Result& result) {
    std::string out = "form,line,label,amount\n";
    for (const auto& f : result.forms) {
        for (const auto& l : f.lines) {
            out += csvField(f.id) + "," + csvField(l.number) + "," + csvField(l.label) + "," +
                   csvField(l.text.empty() ? l.amount.str() : l.text) + "\n";
        }
    }
    return out;
}

}  // namespace ot
