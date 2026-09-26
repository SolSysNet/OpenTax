// Review and Forms: checking the return, browsing every line, and exporting.

#include "app.hpp"

#include "imgui.h"
#include "opentax/report.hpp"
#include "opentax/return_pdf.hpp"
#include "opentax/util.hpp"
#include "platform.hpp"
#include "theme.hpp"
#include "widgets.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace otgui {

namespace fs = std::filesystem;
using namespace ot;

namespace {

bool contains(const std::string& haystack, const char* needle) {
    return toLower(haystack).find(toLower(needle)) != std::string::npos;
}

void writeFile(const std::string& path, const std::string& bytes) {
    std::ofstream out(fs::u8path(path), std::ios::binary | std::ios::trunc);
    if (!out) throw Error("cannot write '" + path + "'");
    out << bytes;
    out.flush();
    if (!out) throw Error("failed while writing '" + path + "'");
}

}  // namespace

Step App::stepForTopic(const std::string& topic) const {
    struct Map {
        const char* keyword;
        Step step;
    };
    static const Map map[] = {
        {"About you", Step::AboutYou},        {"Filing status", Step::AboutYou},
        {"Dependent care", Step::Credits},    {"Dependent", Step::Dependents},
        {"W-2", Step::Income},                {"1099", Step::Income},
        {"Schedule C", Step::Income},         {"Self-employment tax", Step::Income},
        {"Social security tax", Step::Income}, {"QBI", Step::Income},
        {"Capital losses", Step::Payments},   {"IRA", Step::Deductions},
        {"Student loan", Step::Deductions},   {"Self-employed health", Step::Deductions},
        {"Investment interest", Step::Deductions}, {"Charitable", Step::Deductions},
        {"No tax on", Step::Deductions},      {"Senior", Step::Deductions},
        {"Education", Step::Credits},         {"Saver", Step::Credits},
        {"Earned income", Step::Credits},     {"Estimated tax", Step::Payments},
    };
    for (const auto& m : map) {
        if (contains(topic, m.keyword)) return m.step;
    }
    return Step::Review;
}

// ------------------------------------------------------------------- review

void App::drawReview() {
    const float fs = ImGui::GetFontSize();
    const Summary& s = result_.summary;
    ui::Heading("Review your return");
    ui::Muted("Fix anything marked as an error, look over the checks, then save your forms.");
    ImGui::Dummy(ImVec2(0, fs * 0.6f));
    const float width = std::min(ImGui::GetContentRegionAvail().x, fs * 46.0f);

    int errors = 0;
    for (const auto& d : result_.diagnostics) {
        if (d.severity == Severity::Error) ++errors;
    }
    if (errors > 0)
        ui::Callout((std::to_string(errors) + (errors == 1 ? " error needs" : " errors need") +
                     " fixing before this return is ready to file.").c_str(), colorNegative());
    else
        ui::Callout("No errors found. Review the notes below and your forms before you file.", colorPositive());

    ui::SubHeading("Things to review");
    ui::BeginCard("##diag", width);
    int n = 0;
    for (Severity sev : {Severity::Error, Severity::Warning, Severity::Info}) {
        for (const auto& d : result_.diagnostics) {
            if (d.severity != sev) continue;
            ImGui::PushID(n++);
            const char* tag = sev == Severity::Error ? "Error" : sev == Severity::Warning ? "Check" : "Note";
            const ImVec4 color = sev == Severity::Error ? colorNegative() : sev == Severity::Warning ? colorWarning() : colorMuted();
            ui::Badge(tag, color);
            ImGui::SameLine(fs * 4.6f);
            ImGui::BeginGroup();
            ImGui::PushFont(g_fonts.bold, 0.0f);
            ImGui::TextUnformatted(d.topic.c_str());
            ImGui::PopFont();
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width - fs * 7.5f);
            ImGui::TextUnformatted(d.message.c_str());
            ImGui::PopTextWrapPos();
            const Step target = stepForTopic(d.topic);
            if (target != Step::Review && ui::LinkButton("Go there")) go(target);
            ImGui::EndGroup();
            ImGui::Spacing();
            ImGui::PopID();
        }
    }
    ui::EndCard();
    ImGui::Dummy(ImVec2(0, fs * 0.6f));

    ui::SubHeading("Your 2025 tax summary");
    ui::BeginCard("##summary", width);
    struct Row {
        const char* label;
        Money amount;
        bool total;
        const char* line;
    };
    const Row rows[] = {
        {"Total income", s.totalIncome, false, "9"},
        {"Adjustments to income", -result_.line("1040", "10"), false, "10"},
        {"Adjusted gross income", s.agi, true, "11a"},
        {s.itemized ? "Itemized deductions" : "Standard deduction", -s.deduction, false, "12e"},
        {"Qualified business income deduction", -s.qbiDeduction, false, "13a"},
        {"Schedule 1-A deductions", -s.schedule1A, false, "13b"},
        {"Taxable income", s.taxableIncome, true, "15"},
        {"Income tax", s.incomeTax, false, "16"},
        {"Alternative minimum tax", result_.line("1040", "17"), false, "17"},
        {"Nonrefundable credits", -s.credits, false, "21"},
        {"Other taxes", s.otherTaxes, false, "23"},
        {"Total tax", s.totalTax, true, "24"},
        {"Withholding", -s.withholding, false, "25d"},
        {"Estimated and other payments", -result_.line("1040", "26"), false, "26"},
        {"Refundable credits", -s.refundableCredits, false, "32"},
    };
    if (ImGui::BeginTable("##waterfall", 3, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 3.0f);
        ImGui::TableSetupColumn("Line", ImGuiTableColumnFlags_WidthFixed, fs * 4.0f);
        ImGui::TableSetupColumn("Amount", ImGuiTableColumnFlags_WidthFixed, fs * 8.0f);
        for (const auto& row : rows) {
            if (row.amount.isZero() && !row.total) continue;
            ImGui::TableNextRow();
            if (row.total) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ImGuiCol_Header, 0.5f));
            if (row.total) ImGui::PushFont(g_fonts.bold, 0.0f);
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(row.label);
            const FormResult* f = result_.form("1040");
            const Line* l = f ? f->find(row.line) : nullptr;
            if (l && !l->how.empty()) ImGui::SetItemTooltip("%s", l->how.c_str());
            ImGui::TableSetColumnIndex(1);
            ui::Muted(row.line);
            ImGui::TableSetColumnIndex(2);
            ui::MoneyText(row.amount, true, false);
            if (row.total) ImGui::PopFont();
        }
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::PushFont(g_fonts.bold, kBaseFontSize * 1.15f);
        const bool refund = !s.refund.isZero();
        ImGui::TextColored(refund ? colorPositive() : s.owed.isZero() ? colorMuted() : colorNegative(), "%s",
                           refund ? "Your refund" : s.owed.isZero() ? "Balance" : "Amount you owe");
        ImGui::TableSetColumnIndex(1);
        ui::Muted(refund ? "35a" : "37");
        ImGui::TableSetColumnIndex(2);
        ui::MoneyText(refund ? s.refund : s.owed, true, false);
        ImGui::PopFont();
        ImGui::EndTable();
    }
    ui::EndCard();
    ImGui::Dummy(ImVec2(0, fs * 0.6f));

    ui::SubHeading("How to file");
    ui::BeginCard("##file", width);
    std::string forms;
    for (const auto& f : result_.forms) {
        if (!f.worksheet) forms += (forms.empty() ? "" : ", ") + f.id;
    }
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width - fs * 2.5f);
    ImGui::TextUnformatted(("Forms on your return: " + forms).c_str());
    ImGui::Spacing();
    ImGui::TextUnformatted("1.  Save the PDF. It has every form, schedule and worksheet, line by line, with explanations.");
    ImGui::TextUnformatted("2.  Copy the amounts onto the official 2025 IRS forms: IRS Free File Fillable Forms online, or paper forms from irs.gov.");
    ImGui::TextUnformatted("3.  Sign and file. 2025 returns were due April 15, 2026; with an extension, the deadline is October 15, 2026.");
    ui::Muted("OpenTax doesn't e-file and doesn't compute state returns or the underpayment penalty (Form 2210).");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    if (ui::PrimaryButton("Save PDF...", ImVec2(fs * 8, fs * 1.8f))) exportPdf(false);
    ImGui::SameLine();
    if (ImGui::Button("View forms", ImVec2(fs * 8, fs * 1.8f))) go(Step::Forms);
    ui::EndCard();
}

// -------------------------------------------------------------------- forms

void App::drawForms() {
    const float fs = ImGui::GetFontSize();
    ui::Heading("Forms and worksheets");
    ui::Muted("Every line OpenTax figured. Select a line to see exactly how it was calculated.");
    ImGui::Spacing();
    if (ui::PrimaryButton("Save PDF...")) exportPdf(false);
    ImGui::SameLine();
    if (nativeFileDialogsAvailable() && ImGui::Button("Preview PDF")) exportPdf(true);
    ImGui::SameLine();
    if (ImGui::Button("Export CSV...")) exportCsv();
    ImGui::Spacing();

    if (!result_.form(selectedForm_) && !result_.forms.empty()) selectedForm_ = result_.forms.front().id;

    const float listWidth = fs * 15.0f;
    ImGui::BeginChild("##formlist", ImVec2(listWidth, 0), ImGuiChildFlags_Borders);
    for (bool worksheets : {false, true}) {
        ui::Muted(worksheets ? "WORKSHEETS" : "FORMS TO FILE");
        for (const auto& f : result_.forms) {
            if (f.worksheet != worksheets) continue;
            if (ImGui::Selectable(f.id.c_str(), f.id == selectedForm_)) {
                selectedForm_ = f.id;
                selectedLine_.clear();
            }
            ImGui::SetItemTooltip("%s", f.title.c_str());
        }
        ImGui::Spacing();
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##formview", ImVec2(0, 0), ImGuiChildFlags_Borders);
    const FormResult* form = result_.form(selectedForm_);
    if (!form) {
        ui::Muted("No forms yet.");
        ImGui::EndChild();
        return;
    }
    ui::SubHeading(form->title.c_str());
    if (form->worksheet) ui::Muted("Worksheet - keep for your records; you don't file it.");
    ImGui::Spacing();

    const Line* selected = form->find(selectedLine_);
    const float detailHeight = selected && !selected->how.empty() ? fs * 5.0f : 0.0f;
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("##lines", 3, flags, ImVec2(0, -detailHeight))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Line", ImGuiTableColumnFlags_WidthFixed, fs * 3.5f);
        ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Amount", ImGuiTableColumnFlags_WidthFixed, fs * 8.5f);
        ImGui::TableHeadersRow();
        for (std::size_t i = 0; i < form->lines.size(); ++i) {
            const Line& l = form->lines[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (ImGui::Selectable(l.number.c_str(), l.number == selectedLine_, ImGuiSelectableFlags_SpanAllColumns))
                selectedLine_ = l.number;
            if (!l.how.empty()) ImGui::SetItemTooltip("%s", l.how.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(l.label.c_str());
            ImGui::TableSetColumnIndex(2);
            if (!l.text.empty()) ui::TextRight(l.text.c_str());
            else ui::MoneyText(l.amount);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (detailHeight > 0.0f) {
        ImGui::Spacing();
        ImGui::PushFont(g_fonts.bold, 0.0f);
        ImGui::Text("Line %s: %s", selected->number.c_str(), selected->label.c_str());
        ImGui::PopFont();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(selected->how.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::EndChild();
}

// ------------------------------------------------------------------- export

std::string App::returnPdfBytes() const { return returnPdf(*ret_, result_); }

void App::exportPdf(bool preview) {
    if (!ret_) return;
    try {
        if (preview) {
            const fs::path tmp = fs::temp_directory_path() / fs::u8path("OpenTax preview.pdf");
            writeFile(tmp.u8string(), returnPdfBytes());
            if (!openWithDefaultApp(tmp.u8string())) notify("Couldn't open a PDF viewer. The preview is at " + tmp.u8string(), true);
            return;
        }
        if (!nativeFileDialogsAvailable()) {
            typedPath_ = (fs::u8path(path_).parent_path() / fs::u8path(returnPdfFileName(*ret_))).u8string();
            typedPathForPdf_ = true;
            requestPopup("Enter a path##typed");
            return;
        }
        if (auto path = saveFileDialog("Save your return as PDF", {"PDF files (*.pdf)", "*.pdf"}, "pdf", returnPdfFileName(*ret_))) {
            writeFile(*path, returnPdfBytes());
            notify("Saved " + fs::u8path(*path).filename().u8string());
        }
    } catch (const std::exception& e) {
        notify(e.what(), true);
    }
}

void App::exportCsv() {
    if (!ret_) return;
    try {
        const std::string name = fs::u8path(returnPdfFileName(*ret_)).stem().u8string() + ".csv";
        if (!nativeFileDialogsAvailable()) {
            const std::string path = (fs::u8path(path_).parent_path() / fs::u8path(name)).u8string();
            writeFile(path, renderCsv(result_));
            notify("Saved " + path);
            return;
        }
        if (auto path = saveFileDialog("Export every line as CSV", {"CSV files (*.csv)", "*.csv"}, "csv", name)) {
            writeFile(*path, renderCsv(result_));
            notify("Saved " + fs::u8path(*path).filename().u8string());
        }
    } catch (const std::exception& e) {
        notify(e.what(), true);
    }
}

}  // namespace otgui
