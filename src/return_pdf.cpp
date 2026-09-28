#include "opentax/return_pdf.hpp"

#include "opentax/pdf.hpp"
#include "opentax/report.hpp"
#include "opentax/util.hpp"

#include <algorithm>

namespace ot {
namespace {

using pdf::Align;
using pdf::Color;
using pdf::Font;

constexpr Color kInk{0.12, 0.13, 0.15};
constexpr Color kMuted{0.42, 0.45, 0.49};
constexpr Color kAccent{0.13, 0.40, 0.72};
constexpr Color kPositive{0.12, 0.51, 0.16};
constexpr Color kNegative{0.78, 0.18, 0.16};
constexpr Color kRule{0.84, 0.86, 0.89};
constexpr Color kBand{0.95, 0.96, 0.98};
constexpr Color kTotalBg{0.90, 0.94, 0.99};

constexpr double kMargin = 48;
constexpr double kBody = 9.5;
constexpr double kSmall = 7.8;
constexpr double kRow = 14;
constexpr double kNoteRow = 10;

constexpr const char* kFooter =
    "Prepared with OpenTax. A computation record, not an official IRS form: copy these amounts onto the official forms to file.";

class Writer {
public:
    Writer(pdf::Document& doc, std::string runningTitle) : doc_(doc), title_(std::move(runningTitle)) {
        size_ = doc.size();
        left_ = kMargin;
        right_ = size_.width - kMargin;
    }

    void newPage() {
        page_ = &doc_.addPage();
        ++pages_;
        y_ = size_.height - 44;
        page_->text(left_, y_, title_, Font::Regular, kSmall, kMuted);
        page_->text(right_, y_, "Page " + std::to_string(pages_), Font::Regular, kSmall, kMuted, Align::Right);
        page_->line(left_, 30 + 12, right_, 30 + 12, 0.5, kRule);
        page_->text(left_, 30, kFooter, Font::Regular, 6.8, kMuted);
        y_ -= 22;
    }

    // Starts a new page when fewer than `needed` points remain.
    void ensure(double needed) {
        if (!page_ || y_ - needed < 56) newPage();
    }

    pdf::Page& page() { return *page_; }
    double& y() { return y_; }
    double left() const { return left_; }
    double right() const { return right_; }

private:
    pdf::Document& doc_;
    std::string title_;
    pdf::PageSize size_{};
    pdf::Page* page_ = nullptr;
    double y_ = 0;
    double left_ = 0;
    double right_ = 0;
    int pages_ = 0;
};

bool isTotalLine(const Line& l) {
    const std::string label = toLower(l.label);
    return label.find("total") != std::string::npos || label.find("taxable income") != std::string::npos ||
           label.find("adjusted gross income") != std::string::npos || label.find("refund") == 0 ||
           label.find("amount you owe") == 0;
}

void formSection(Writer& w, const FormResult& f, bool explanations) {
    w.ensure(60);
    double& y = w.y();
    auto& p = w.page();
    const std::string heading = f.id == f.title ? f.title : f.id + "  -  " + f.title;
    for (const auto& l : pdf::wrapText(heading, Font::Bold, 12, w.right() - w.left())) {
        w.page().text(w.left(), y, l, Font::Bold, 12, f.worksheet ? kMuted : kAccent);
        y -= 15;
    }
    (void)p;
    if (f.worksheet) {
        w.page().text(w.left(), y, "Worksheet - keep for your records", Font::Regular, kSmall, kMuted);
        y -= 12;
    }
    w.page().line(w.left(), y + 4, w.right(), y + 4, 1.0, f.worksheet ? kRule : kAccent);
    y -= 10;

    const double numRight = w.left() + 34;
    const double labelLeft = w.left() + 44;
    const double amountRight = w.right() - 4;
    const double labelWidth = amountRight - 110 - labelLeft;
    bool band = false;
    for (const auto& l : f.lines) {
        const auto labelLines = pdf::wrapText(l.label, Font::Regular, kBody, labelWidth);
        std::vector<std::string> noteLines;
        if (explanations && !l.how.empty()) noteLines = pdf::wrapText(l.how, Font::Regular, kSmall, labelWidth);
        const double height = static_cast<double>(labelLines.size()) * kRow +
                              static_cast<double>(noteLines.size()) * kNoteRow + (noteLines.empty() ? 0 : 2);
        w.ensure(height + 4);
        auto& page = w.page();
        const bool total = isTotalLine(l);
        if (total) page.fillRect(w.left(), y - height + kRow - 4, w.right() - w.left(), height, kTotalBg);
        else if (band) page.fillRect(w.left(), y - height + kRow - 4, w.right() - w.left(), height, kBand);
        band = !band;
        const Font font = total ? Font::Bold : Font::Regular;
        page.text(numRight, y, l.number, Font::Bold, kBody, kInk, Align::Right);
        double ly = y;
        for (const auto& t : labelLines) {
            page.text(labelLeft, ly, t, font, kBody, kInk);
            ly -= kRow;
        }
        const std::string value = l.text.empty() ? formatUsd(l.amount) : l.text;
        page.text(amountRight, y, value, font, kBody, l.amount < Money() ? kNegative : kInk, Align::Right);
        for (const auto& t : noteLines) {
            page.text(labelLeft + 8, ly + 3, t, Font::Regular, kSmall, kMuted);
            ly -= kNoteRow;
        }
        y -= height;
    }
    y -= 18;
}

}  // namespace

std::string returnPdfFileName(const TaxReturn& r) {
    std::string raw = std::to_string(r.info.year) + " Federal Return";
    const std::string name = r.displayName();
    if (!trim(name).empty()) raw += " - " + name;
    std::string out;
    for (char ch : raw) {
        const auto c = static_cast<unsigned char>(ch);
        if (c < 32 || std::string_view("<>:\"/\\|?*").find(ch) != std::string_view::npos) continue;
        out += ch;
    }
    out = trim(out);
    while (!out.empty() && (out.back() == '.' || out.back() == ' ')) out.pop_back();
    while (!out.empty() && out.front() == '.') out.erase(0, 1);
    if (out.empty()) out = "return";
    if (out.size() > 120) out.resize(120);
    return out + ".pdf";
}

std::string returnPdf(const TaxReturn& r, const Result& result, const ReturnPdfOptions& options) {
    pdf::Document doc(pdf::kLetter);
    const std::string name = r.displayName();
    doc.setTitle(std::to_string(r.info.year) + " Federal Income Tax Return");
    doc.setAuthor(name.empty() ? "OpenTax" : name);
    Writer w(doc, std::to_string(r.info.year) + " Form 1040" + (name.empty() ? "" : " - " + name));

    // ---- summary page
    w.newPage();
    double& y = w.y();
    w.page().text(w.left(), y, std::to_string(r.info.year) + " Federal Income Tax Return", Font::Bold, 20, kInk);
    y -= 20;
    std::string who = name.empty() ? "" : name + "  -  ";
    who += choiceLabel(r.info.status);
    w.page().text(w.left(), y, who, Font::Regular, 11, kMuted);
    y -= 14;
    const std::string address = trim(r.info.street + ", " + r.info.city + " " + r.info.state + " " + r.info.zip);
    if (address != ",") {
        w.page().text(w.left(), y, address, Font::Regular, 9.5, kMuted);
        y -= 12;
    }
    y -= 16;

    const Summary& s = result.summary;
    const bool refund = !s.refund.isZero();
    const bool owe = !s.owed.isZero();
    const Color box = refund ? Color{0.90, 0.96, 0.90} : owe ? Color{0.99, 0.92, 0.91} : kBand;
    w.page().fillRect(w.left(), y - 44, w.right() - w.left(), 58, box);
    w.page().text(w.left() + 16, y - 6, refund ? "Your federal refund" : owe ? "Federal tax you owe" : "Balance", Font::Regular, 11,
                  kMuted);
    w.page().text(w.left() + 16, y - 32, formatUsd(refund ? s.refund : s.owed), Font::Bold, 24,
                  refund ? kPositive : owe ? kNegative : kInk);
    if (!s.overpaid.isZero() && s.overpaid != s.refund)
        w.page().text(w.right() - 16, y - 32, formatUsd(s.overpaid - s.refund) + " applied to " + std::to_string(r.info.year + 1) + " estimated tax",
                      Font::Regular, 9.5, kMuted, Align::Right);
    y -= 72;

    struct Row {
        const char* label;
        Money amount;
        bool bold;
    };
    const LineIds& ids = lineIds(r.info.year);
    auto lineLabel = [](const char* text, const char* line) { return std::string(text) + " (line " + line + ")"; };
    const std::string charityLabel = lineLabel("Charitable deduction for non-itemizers", ids.charity);
    const std::string qbiLabel = lineLabel("Qualified business income deduction", ids.qbi);
    const std::string sch1ALabel = lineLabel("Schedule 1-A deductions", ids.schedule1A);
    const std::string totalTaxLabel = lineLabel("Total tax", ids.totalTax);
    const std::string refundableLabel = lineLabel("Refundable credits and other payments", ids.refundable);
    const Row rows[] = {
        {"Total income (line 9)", s.totalIncome, false},
        {"Adjustments to income (line 10)", s.totalIncome - s.agi, false},
        {"Adjusted gross income (line 11a)", s.agi, true},
        {s.itemized ? "Itemized deductions (line 12e)" : "Standard deduction (line 12e)", s.deduction, false},
        {charityLabel.c_str(), s.nonItemizerCharity, false},
        {sch1ALabel.c_str(), s.schedule1A, false},
        {qbiLabel.c_str(), s.qbiDeduction, false},
        {"Taxable income (line 15)", s.taxableIncome, true},
        {"Tax (line 16) and AMT (line 17)", s.incomeTax + result.line("1040", "17"), false},
        {"Nonrefundable credits (lines 19-20)", s.credits, false},
        {"Other taxes (line 23)", s.otherTaxes, false},
        {totalTaxLabel.c_str(), s.totalTax, true},
        {"Federal income tax withheld (line 25d)", s.withholding, false},
        {"Estimated payments (line 26)", result.line("1040", "26"), false},
        {refundableLabel.c_str(), s.refundableCredits, false},
        {"Total payments (line 33)", s.totalPayments, true},
    };
    bool band = false;
    for (const auto& row : rows) {
        if (row.amount.isZero() && !row.bold) continue;
        if (row.bold) w.page().fillRect(w.left(), y - 4, w.right() - w.left(), kRow + 2, kTotalBg);
        else if (band) w.page().fillRect(w.left(), y - 4, w.right() - w.left(), kRow + 2, kBand);
        band = !band;
        const Font f = row.bold ? Font::Bold : Font::Regular;
        w.page().text(w.left() + 8, y, row.label, f, 10, kInk);
        w.page().text(w.right() - 8, y, formatUsd(row.amount), f, 10, kInk, Align::Right);
        y -= kRow + 2;
    }
    y -= 14;

    // Forms included
    w.page().text(w.left(), y, "Forms and schedules to file", Font::Bold, 11, kInk);
    y -= 15;
    std::string forms;
    for (const auto& f : result.forms) {
        if (f.worksheet) continue;
        forms += (forms.empty() ? "" : ", ") + f.id;
    }
    for (const auto& l : pdf::wrapText(forms, Font::Regular, 9.5, w.right() - w.left())) {
        w.page().text(w.left(), y, l, Font::Regular, 9.5, kInk);
        y -= 12;
    }
    y -= 10;

    // Review notes
    if (!result.diagnostics.empty()) {
        w.ensure(40);
        w.page().text(w.left(), w.y(), "Review notes", Font::Bold, 11, kInk);
        w.y() -= 15;
        for (Severity sev : {Severity::Error, Severity::Warning, Severity::Info}) {
            for (const auto& d : result.diagnostics) {
                if (d.severity != sev) continue;
                const char* tag = sev == Severity::Error ? "Error" : sev == Severity::Warning ? "Check" : "Note";
                const Color c = sev == Severity::Error ? kNegative : sev == Severity::Warning ? Color{0.66, 0.42, 0.0} : kMuted;
                const auto lines = pdf::wrapText(d.topic + ": " + d.message, Font::Regular, 8.8, w.right() - w.left() - 44);
                w.ensure(static_cast<double>(lines.size()) * 11 + 4);
                w.page().text(w.left(), w.y(), tag, Font::Bold, 8.8, c);
                for (const auto& l : lines) {
                    w.page().text(w.left() + 44, w.y(), l, Font::Regular, 8.8, kInk);
                    w.y() -= 11;
                }
                w.y() -= 3;
            }
        }
    }

    // ---- forms
    for (const auto& f : result.forms) {
        if (f.worksheet && !options.worksheets) continue;
        if (&f == &result.forms.front()) w.newPage();
        formSection(w, f, options.explanations);
    }
    return doc.build();
}

}  // namespace ot
