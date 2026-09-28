// The interview: one screen per step, each editing the working return in place.

#include "app.hpp"

#include "imgui.h"
#include "opentax/util.hpp"
#include "theme.hpp"
#include "widgets.hpp"

#include <algorithm>
#include <cstring>

namespace otgui {

using namespace ot;

namespace {

bool is(const char* key, const char* name) { return std::strcmp(key, name) == 0; }

std::string ageText(const std::optional<Date>& dob, int year) {
    if (!dob) return "no birth date";
    return "age " + std::to_string(ageAtEndOfYear(*dob, year));
}

template <class T, class F>
Money sum(const std::vector<T>& items, F f) {
    Money m;
    for (const auto& i : items) m += f(i);
    return m;
}

void sectionGap() { ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 0.8f)); }

}  // namespace

// ------------------------------------------------------------- list screen

template <class T>
void App::drawList(const char* title, const char* intro, std::vector<T>& items, const char* addLabel,
                   const std::function<std::string(const T&)>& describe, const std::function<Money(const T&)>& amount,
                   const std::function<bool(const char*)>& hide) {
    const Schema<T>& schema = ot::schema<T>();
    const float fs = ImGui::GetFontSize();
    auto hideOwner = [&](const char* key) {
        if (is(key, "owner") && ret_->info.status != FilingStatus::MarriedJoint) return true;
        return hide ? hide(key) : false;
    };

    if (editing_ >= 0 && editing_ < static_cast<int>(items.size())) {
        T& item = items[static_cast<std::size_t>(editing_)];
        if (ui::LinkButton("< Back to the list")) editing_ = -1;
        ImGui::Spacing();
        ui::Heading(schema.title);
        const std::string d = describe(item);
        ui::Muted(d.empty() ? "Enter the amounts exactly as they appear on your form. Leave boxes blank if they're empty." : d.c_str());
        sectionGap();
        ui::BeginCard("##editor", std::min(ImGui::GetContentRegionAvail().x, fs * 46.0f));
        if (ui::RecordEditor("##fields", item, schema, hideOwner)) changed();
        ui::EndCard();
        ImGui::Spacing();
        if (ui::PrimaryButton("Done", ImVec2(fs * 7, fs * 1.8f))) editing_ = -1;
        ImGui::SameLine();
        if (ui::DangerButton("Delete", ImVec2(fs * 7, fs * 1.8f))) {
            const auto index = static_cast<std::size_t>(editing_);
            confirm("Delete this entry?", std::string("This removes the ") + schema.title + " from your return.", "Delete",
                    [this, &items, index] {
                        if (index < items.size()) items.erase(items.begin() + static_cast<std::ptrdiff_t>(index));
                        editing_ = -1;
                        changed();
                    });
        }
        return;
    }

    ui::Heading(title);
    ui::MutedWrapped(intro);
    sectionGap();
    const float width = std::min(ImGui::GetContentRegionAvail().x, fs * 46.0f);
    for (std::size_t i = 0; i < items.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        ui::BeginCard("##item", width);
        ImGui::AlignTextToFramePadding();
        std::string label = describe(items[i]);
        if (label.empty()) label = std::string(schema.title) + " " + std::to_string(i + 1);
        ImGui::PushFont(g_fonts.bold, 0.0f);
        ImGui::TextUnformatted(label.c_str());
        ImGui::PopFont();
        ImGui::SameLine(width - fs * 16.5f);
        if (amount) {
            ImGui::AlignTextToFramePadding();
            const std::string a = ui::usd(amount(items[i]));
            ImGui::SetCursorPosX(width - fs * 10.5f - ImGui::CalcTextSize(a.c_str()).x);
            ImGui::TextUnformatted(a.c_str());
        }
        ImGui::SameLine(width - fs * 9.5f);
        if (ImGui::Button("Edit", ImVec2(fs * 4, 0))) editing_ = static_cast<int>(i);
        ImGui::SameLine();
        if (ImGui::Button("Delete", ImVec2(fs * 4, 0))) {
            confirm("Delete this entry?", "This removes \"" + label + "\" from your return.", "Delete", [this, &items, i] {
                if (i < items.size()) items.erase(items.begin() + static_cast<std::ptrdiff_t>(i));
                changed();
            });
        }
        ui::EndCard();
        ImGui::PopID();
    }
    if (items.empty()) {
        ui::Muted("Nothing entered yet.");
        ImGui::Spacing();
    }
    if (ui::PrimaryButton(addLabel, ImVec2(0, fs * 1.8f))) {
        items.emplace_back();
        editing_ = static_cast<int>(items.size()) - 1;
        changed();
    }
}

// --------------------------------------------------------------------- home

void App::drawHome() {
    const float fs = ImGui::GetFontSize();
    TaxReturn& r = *ret_;
    const std::string first = trim(r.taxpayer.first);
    ui::Heading(first.empty() ? ("Your " + yearText() + " tax return").c_str() : ("Welcome, " + first).c_str());
    ui::Muted("Work through each step. Your refund updates as you go, and everything saves automatically.");
    sectionGap();

    int errors = 0, warnings = 0;
    for (const auto& d : result_.diagnostics) {
        if (d.severity == Severity::Error) ++errors;
        if (d.severity == Severity::Warning) ++warnings;
    }
    if (errors > 0) {
        ui::Callout((std::to_string(errors) + (errors == 1 ? " thing needs" : " things need") +
                     " fixing before you file. See Review.").c_str(), colorNegative());
    } else if (warnings > 0) {
        ui::Callout((std::to_string(warnings) + (warnings == 1 ? " item" : " items") + " to double-check. See Review.").c_str(),
                    colorWarning());
    }

    const Summary& s = result_.summary;
    std::size_t incomeForms = r.w2s.size() + r.interest.size() + r.dividends.size() + r.capitalTxns.size() +
                              r.businesses.size() + r.retirement.size() + r.socialSecurity.size() + r.unemployment.size() +
                              r.otherIncome.size();
    struct Row {
        Step step;
        const char* title;
        std::string detail;
    };
    std::string about = choiceLabel(r.info.status);
    if (!r.taxpayer.birthDate) about += "  -  add your date of birth";
    const std::vector<Row> rows = {
        {Step::AboutYou, "About you", about},
        {Step::Dependents, "Dependents", r.dependents.empty() ? "None" : std::to_string(r.dependents.size()) + (r.dependents.size() == 1 ? " dependent" : " dependents")},
        {Step::Income, "Income", std::to_string(incomeForms) + (incomeForms == 1 ? " form" : " forms") + "  -  total income " + ui::usd(s.totalIncome)},
        {Step::Deductions, "Deductions", std::string(s.itemized ? "Itemized " : "Standard deduction ") + ui::usd(s.deduction) +
                                             (s.schedule1A.isZero() ? "" : "  +  Schedule 1-A " + ui::usd(s.schedule1A))},
        {Step::Credits, "Credits", "Credits " + ui::usd(s.credits + result_.line("1040", "27a") + result_.line("1040", "28") + result_.line("1040", "29"))},
        {Step::Payments, "Payments", "Withholding and payments " + ui::usd(s.totalPayments - s.refundableCredits)},
        {Step::Review, "Review", errors ? std::to_string(errors) + " to fix" : warnings ? std::to_string(warnings) + " to check" : "Looks good"},
        {Step::Forms, "Forms & PDF", std::to_string(std::count_if(result_.forms.begin(), result_.forms.end(), [](const FormResult& f) { return !f.worksheet; })) + " forms and schedules"},
    };
    const float width = std::min(ImGui::GetContentRegionAvail().x, fs * 46.0f);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        ui::BeginCard("##row", width);
        ImGui::BeginGroup();
        ImGui::PushFont(g_fonts.bold, 0.0f);
        ImGui::Text("%d. %s", static_cast<int>(i + 1), rows[i].title);
        ImGui::PopFont();
        ui::Muted(rows[i].detail.c_str());
        ImGui::EndGroup();
        ImGui::SameLine(width - fs * 7.0f);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + fs * 0.4f);
        if (ImGui::Button("Go", ImVec2(fs * 5, 0))) go(rows[i].step);
        ui::EndCard();
        ImGui::PopID();
    }
    stepFooter(Step::Home, Step::AboutYou, "Get started");
}

// ---------------------------------------------------------------- about you

void App::drawAboutYou() {
    const float fs = ImGui::GetFontSize();
    TaxReturn& r = *ret_;
    ui::Heading("About you");
    ui::Muted("Your filing status and a few personal details decide your tax rates and standard deduction.");
    sectionGap();

    ui::SubHeading("Tax year");
    ImGui::Spacing();
    for (int y : {2025, 2026}) {
        if (y != 2025) ImGui::SameLine();
        if (ImGui::RadioButton(std::to_string(y).c_str(), r.info.year == y)) {
            r.info.year = y;
            changed();
        }
    }
    ImGui::SameLine();
    ui::Muted(r.info.year == 2026 ? "  Uses 2026 inflation adjustments and draft IRS forms." : "  The return you file in 2026.");
    sectionGap();

    ui::SubHeading("Filing status");
    ImGui::Spacing();
    struct StatusHelp {
        FilingStatus status;
        const char* help;
    };
    const std::string singleHelp = "Unmarried, divorced, or legally separated on December 31, " + yearText() + ".";
    const std::string qssHelp = "Your spouse died in " + yearText(-2) + " or " + yearText(-1) +
                                ", you haven't remarried, and a dependent child lives with you.";
    const StatusHelp statuses[] = {
        {FilingStatus::Single, singleHelp.c_str()},
        {FilingStatus::MarriedJoint, "Married on December 31 and filing one return together. Usually the lowest total tax."},
        {FilingStatus::MarriedSeparate, "Married, each filing your own return. Often costs more, and several credits aren't allowed."},
        {FilingStatus::HeadOfHousehold, "Unmarried, and you paid more than half the cost of a home for a qualifying person who lived with you."},
        {FilingStatus::QualifyingSurvivingSpouse, qssHelp.c_str()},
    };
    const float width = std::min(ImGui::GetContentRegionAvail().x, fs * 46.0f);
    ui::BeginCard("##status", width);
    for (const auto& s : statuses) {
        ImGui::PushID(static_cast<int>(s.status));
        if (ImGui::RadioButton(choiceLabel(s.status), r.info.status == s.status)) {
            r.info.status = s.status;
            changed();
        }
        ImGui::Indent(fs * 1.7f);
        ui::MutedWrapped(s.help);
        ImGui::Unindent(fs * 1.7f);
        ImGui::PopID();
    }
    if (r.info.status == FilingStatus::MarriedSeparate) {
        ImGui::Separator();
        const auto& info = schema<ReturnInfo>();
        if (ui::RecordEditor("##mfs", r.info, info, [](const char* k) {
                return !(is(k, "livedapart") || is(k, "spouseitemizes") || is(k, "eicseparated"));
            }))
            changed();
    }
    ui::EndCard();
    sectionGap();

    auto personFields = [&](const char* k) { return is(k, "tradira") || is(k, "rothira"); };
    ui::SubHeading("You");
    ui::BeginCard("##you", width);
    if (ui::RecordEditor("##tp", r.taxpayer, schema<Person>(), personFields)) changed();
    ui::EndCard();

    if (r.hasSpouse()) {
        sectionGap();
        ui::SubHeading("Your spouse");
        ui::BeginCard("##spouse", width);
        if (ui::RecordEditor("##sp", r.spouse, schema<Person>(), [&](const char* k) {
                if (personFields(k)) return true;
                return r.info.status == FilingStatus::MarriedSeparate && !(is(k, "first") || is(k, "last"));
            }))
            changed();
        ui::EndCard();
    }

    sectionGap();
    ui::SubHeading("Home address");
    ui::BeginCard("##address", width);
    if (ui::RecordEditor("##addr", r.info, schema<ReturnInfo>(), [](const char* k) {
            return !(is(k, "street") || is(k, "city") || is(k, "state") || is(k, "zip"));
        }))
        changed();
    ui::EndCard();
    stepFooter(Step::Home, Step::Dependents);
}

// --------------------------------------------------------------- dependents

void App::drawDependents() {
    drawList<Dependent>(
        "Dependents",
        "Children and relatives you support. Each qualifying child under 17 is worth up to $2,200 (child tax credit); "
        "other dependents up to $500. Children also count toward the earned income credit and head of household status.",
        ret_->dependents, "Add a dependent",
        [year = ret_->info.year](const Dependent& d) {
            std::string name = trim(d.first + " " + d.last);
            if (name.empty()) return std::string();
            std::string rel = choiceLabel(d.relationship);
            rel = rel.substr(0, rel.find(','));
            const bool ctc = d.birthDate && ageAtEndOfYear(*d.birthDate, year) < 17 && d.monthsLivedWithYou >= 7 &&
                             d.hasValidSsn && d.relationship != Relationship::Parent && d.relationship != Relationship::OtherRelative;
            return name + "  -  " + rel + ", " + ageText(d.birthDate, year) + (ctc ? "  -  child tax credit" : "  -  other dependent");
        },
        {});
    if (editing_ < 0) stepFooter(Step::AboutYou, Step::Income);
}

// ------------------------------------------------------------------ income

void App::drawIncome() {
    if (income_ == IncomeKind::Hub) {
        drawIncomeHub();
        return;
    }
    if (editing_ < 0) {
        if (ui::LinkButton("< All income")) {
            income_ = IncomeKind::Hub;
            return;
        }
        ImGui::Spacing();
    }
    TaxReturn& r = *ret_;
    switch (income_) {
        case IncomeKind::Wages:
            drawList<W2>("Wages (Form W-2)",
                         "Enter each W-2 you received. Box 12 deferrals and box 13 affect your IRA deduction and saver's credit. "
                         "Enter qualified tips and overtime here to deduct them on Schedule 1-A.",
                         r.w2s, "Add a W-2",
                         [](const W2& w) { return w.employer; }, [](const W2& w) { return w.wages; });
            break;
        case IncomeKind::Interest:
            drawList<Interest1099>("Interest (Form 1099-INT)", "Bank, savings bond and bond interest.", r.interest, "Add a 1099-INT",
                                   [](const Interest1099& i) { return i.payer; },
                                   [](const Interest1099& i) { return i.interest + i.usBonds; });
            break;
        case IncomeKind::Dividends:
            drawList<Dividend1099>("Dividends (Form 1099-DIV)",
                                   "Dividends and mutual fund capital gain distributions. Qualified dividends are taxed at lower rates.",
                                   r.dividends, "Add a 1099-DIV", [](const Dividend1099& d) { return d.payer; },
                                   [](const Dividend1099& d) { return d.ordinary; });
            break;
        case IncomeKind::Sales:
            drawList<CapitalTxn>("Investment sales (Form 1099-B)",
                                 "Stocks, bonds, funds and crypto you sold. Enter each sale, or one summary line per 1099-B category. "
                                 "Losses offset gains, and up to $3,000 of net loss offsets other income.",
                                 r.capitalTxns, "Add a sale", [](const CapitalTxn& c) { return c.description; },
                                 [](const CapitalTxn& c) { return c.proceeds - c.basis + c.adjustment; });
            break;
        case IncomeKind::Business:
            drawList<Business>("Self-employment (Schedule C)",
                               "Freelance, contract and gig work, including Forms 1099-NEC and 1099-K. Profit is subject to "
                               "self-employment tax and may qualify for the 20% QBI deduction.",
                               r.businesses, "Add a business", [](const Business& b) { return b.name; },
                               [this](const Business& b) {
                                   const FormResult* f = result_.form("Schedule C (" + (trim(b.name).empty() ? std::string("Business") : b.name) + ")");
                                   return f ? f->get("31") : b.receipts;
                               });
            break;
        case IncomeKind::Retirement:
            drawList<Retirement1099R>("Retirement (Form 1099-R)", "Pensions, annuities, and IRA or 401(k) distributions.", r.retirement,
                                      "Add a 1099-R", [](const Retirement1099R& x) { return x.payer; },
                                      [](const Retirement1099R& x) { return x.gross; });
            break;
        case IncomeKind::SocialSecurity:
            drawList<SocialSecurity>("Social Security (Form SSA-1099)",
                                     "Up to 85% of benefits can be taxable, depending on your other income. OpenTax works it out.",
                                     r.socialSecurity, "Add an SSA-1099",
                                     [this](const SocialSecurity& s) {
                                         return std::string("Benefits for ") + (s.owner == Owner::Spouse ? "your spouse" : "you");
                                     },
                                     [](const SocialSecurity& s) { return s.benefits; });
            break;
        case IncomeKind::Unemployment:
            drawList<Unemployment1099G>("Unemployment (Form 1099-G)", "Unemployment compensation is fully taxable.", r.unemployment,
                                        "Add a 1099-G", [](const Unemployment1099G& u) { return u.payer; },
                                        [](const Unemployment1099G& u) { return u.compensation; });
            break;
        case IncomeKind::Other:
            drawList<OtherIncome>("Other income", "Prizes, jury duty pay, hobby income and other taxable income not reported elsewhere.",
                                  r.otherIncome, "Add other income", [](const OtherIncome& o) { return o.description; },
                                  [](const OtherIncome& o) { return o.amount; });
            break;
        case IncomeKind::Hub: break;
    }
}

void App::drawIncomeHub() {
    const float fs = ImGui::GetFontSize();
    TaxReturn& r = *ret_;
    ui::Heading("Income");
    ui::Muted(("Choose the kinds of income you had in " + yearText() + ". Most people only need W-2s.").c_str());
    sectionGap();

    Money business;
    for (const auto& f : result_.forms) {
        if (f.id.rfind("Schedule C (", 0) == 0) business += f.get("31");
    }
    struct Tile {
        IncomeKind kind;
        const char* title;
        const char* blurb;
        std::size_t count;
        Money total;
    };
    const Tile tiles[] = {
        {IncomeKind::Wages, "Wages", "Form W-2, tips and overtime", r.w2s.size(), sum(r.w2s, [](const W2& w) { return w.wages; })},
        {IncomeKind::Interest, "Interest", "Form 1099-INT", r.interest.size(), result_.line("1040", "2b")},
        {IncomeKind::Dividends, "Dividends", "Form 1099-DIV", r.dividends.size(), result_.line("1040", "3b")},
        {IncomeKind::Sales, "Investment sales", "Stocks, funds, crypto (1099-B)", r.capitalTxns.size(),
         sum(r.capitalTxns, [](const CapitalTxn& c) { return c.proceeds - c.basis + c.adjustment; })},
        {IncomeKind::Business, "Self-employment", "Schedule C, 1099-NEC, 1099-K", r.businesses.size(), business},
        {IncomeKind::Retirement, "Retirement", "Pensions, IRA, 401(k) (1099-R)", r.retirement.size(),
         result_.line("1040", "4b") + result_.line("1040", "5b")},
        {IncomeKind::SocialSecurity, "Social Security", "Form SSA-1099", r.socialSecurity.size(), result_.line("1040", "6a")},
        {IncomeKind::Unemployment, "Unemployment", "Form 1099-G", r.unemployment.size(),
         sum(r.unemployment, [](const Unemployment1099G& u) { return u.compensation; })},
        {IncomeKind::Other, "Other income", "Prizes, jury duty, hobbies", r.otherIncome.size(),
         sum(r.otherIncome, [](const OtherIncome& o) { return o.amount; })},
    };
    const float avail = std::min(ImGui::GetContentRegionAvail().x, fs * 60.0f);
    const int columns = avail > fs * 42 ? 3 : 2;
    const float tileW = (avail - fs * static_cast<float>(columns - 1) * 0.6f) / static_cast<float>(columns);
    int col = 0;
    for (const auto& t : tiles) {
        ImGui::PushID(static_cast<int>(t.kind));
        if (col > 0) ImGui::SameLine(0, fs * 0.6f);
        ui::BeginCard("##tile", tileW, fs * 6.8f);
        ImGui::PushFont(g_fonts.bold, kBaseFontSize * 1.05f);
        ImGui::TextUnformatted(t.title);
        ImGui::PopFont();
        ui::Muted(t.blurb);
        ImGui::Spacing();
        if (t.count > 0) {
            ImGui::Text("%d entered  -  %s", static_cast<int>(t.count), ui::usd(t.total).c_str());
            if (ImGui::Button("Review", ImVec2(fs * 5.5f, 0))) {
                income_ = t.kind;
                editing_ = -1;
            }
        } else {
            ui::Muted(" ");
            if (ImGui::Button("Add", ImVec2(fs * 5.5f, 0))) {
                income_ = t.kind;
                editing_ = -1;
            }
        }
        ui::EndCard(false);
        ImGui::PopID();
        col = (col + 1) % columns;
        if (col == 0) ImGui::Dummy(ImVec2(0, fs * 0.3f));
    }
    sectionGap();
    ImGui::Text("Total income:  %s", ui::usd(result_.summary.totalIncome).c_str());
    ImGui::SameLine();
    ui::HelpMarker(result_.form("1040") && result_.form("1040")->find("9") ? result_.form("1040")->find("9")->how.c_str() : "");
    stepFooter(Step::Dependents, Step::Deductions);
}

// -------------------------------------------------------------- deductions

void App::drawDeductions() {
    const float fs = ImGui::GetFontSize();
    TaxReturn& r = *ret_;
    const Summary& s = result_.summary;
    ui::Heading("Deductions");
    ui::Muted("Adjustments reduce your income directly. Then you get the larger of the standard deduction or your itemized deductions.");
    sectionGap();

    std::string compare;
    if (s.itemizedDeduction.isZero()) {
        compare = "You're getting the " + ui::usd(s.standardDeduction) + " standard deduction. Enter itemized deductions below "
                  "only if they might add up to more (mortgage interest, state and local taxes, charity, large medical bills).";
    } else if (s.itemized) {
        compare = "Itemizing saves you more: " + ui::usd(s.itemizedDeduction) + " itemized vs. the " + ui::usd(s.standardDeduction) +
                  " standard deduction.";
    } else {
        compare = "The standard deduction (" + ui::usd(s.standardDeduction) + ") is more than your itemized deductions (" +
                  ui::usd(s.itemizedDeduction) + "), so OpenTax uses it.";
    }
    if (!s.nonItemizerCharity.isZero())
        compare += " You also get " + ui::usd(s.nonItemizerCharity) + " for cash gifts to charity (line 12f), available from 2026 to people who don't itemize.";
    ui::Callout(compare.c_str(), colorAccent());
    sectionGap();

    const float width = std::min(ImGui::GetContentRegionAvail().x, fs * 46.0f);

    ui::SubHeading("Schedule 1-A: tips, overtime, car loan and senior deductions");
    ui::BeginCard("##1a", width);
    Money tips, overtime;
    for (const auto& w : r.w2s) {
        tips += w.qualifiedTips;
        overtime += w.qualifiedOvertime;
    }
    ImGui::Text("Qualified tips on your W-2s: %s", ui::usd(tips).c_str());
    ImGui::SameLine();
    ImGui::Text("   Qualified overtime: %s", ui::usd(overtime).c_str());
    if (ui::LinkButton("Enter tips and overtime on your W-2s")) {
        go(Step::Income);
        income_ = IncomeKind::Wages;
    }
    if (ui::RecordEditor("##car", r.adjustments, schema<Adjustments>(), [](const char* k) { return !is(k, "carloan"); }))
        changed();
    const Money senior = s.seniorDeduction;
    ImGui::Text("Enhanced deduction for seniors (65+): %s", ui::usd(senior).c_str());
    ImGui::SameLine();
    ui::HelpMarker(("Up to $6,000 for each spouse born before January 2, " + yearText(-64) +
                    ", reduced by 6% of modified AGI over $75,000 ($150,000 joint). Married couples must file jointly. "
                    "Set birth dates under About you.").c_str());
    ImGui::Separator();
    ImGui::Text("Total Schedule 1-A deductions (line 13b): %s", ui::usd(s.schedule1A).c_str());
    ui::EndCard();
    sectionGap();

    ui::SubHeading("Adjustments to income");
    ui::BeginCard("##adj", width);
    if (ui::RecordEditor("##adjf", r.adjustments, schema<Adjustments>(), [&](const char* k) {
            return is(k, "carloan") || (is(k, "educatorspouse") && r.info.status != FilingStatus::MarriedJoint);
        }))
        changed();
    if (ImGui::BeginTable("##ira", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX)) {
        ImGui::TableSetupColumn("l", ImGuiTableColumnFlags_WidthFixed, fs * 17.0f);
        ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Traditional IRA (you)");
        ImGui::SameLine();
        ui::HelpMarker(("Contributions for " + yearText() + ", up to " + ui::usd(rules().iraLimit) + " (" +
                        ui::usd(rules().iraLimit + rules().iraCatchUp) +
                        " if 50 or older). If you're covered by a workplace plan, the deduction phases out with income.").c_str());
        ImGui::TableSetColumnIndex(1);
        if (ui::MoneyInput("##iratp", r.taxpayer.traditionalIra, fs * 10)) changed();
        if (r.info.status == FilingStatus::MarriedJoint) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Traditional IRA (spouse)");
            ImGui::TableSetColumnIndex(1);
            if (ui::MoneyInput("##irasp", r.spouse.traditionalIra, fs * 10)) changed();
        }
        ImGui::EndTable();
    }
    ImGui::Separator();
    ImGui::Text("Total adjustments (line 10): %s", ui::usd(result_.line("1040", "10")).c_str());
    ui::EndCard();
    sectionGap();

    const bool open = s.itemized || !s.itemizedDeduction.isZero();
    ImGui::SetNextItemOpen(open, ImGuiCond_Once);
    if (ImGui::CollapsingHeader("Itemized deductions (Schedule A)")) {
        ui::BeginCard("##itemized", width);
        const std::string saltNote = "State income tax withheld on your W-2s (box 17) is included automatically. State and local taxes are capped at " +
                                     ui::usd(rules().saltCap) + " for " + yearText() + " (half if married filing separately), lower above " +
                                     ui::usd(rules().saltPhaseStart) + " of income." +
                                     (ret_->info.year >= 2026 ? " From 2026, charitable gifts count only above 0.5% of AGI, and mortgage insurance premiums are deductible again." : "");
        ui::MutedWrapped(saltNote.c_str());
        ImGui::Spacing();
        if (ui::RecordEditor("##items", r.itemized, schema<Itemized>(), [&](const char* k) {
                if (is(k, "salestax")) return !r.itemized.useSalesTax;
                if (is(k, "stateincome")) return r.itemized.useSalesTax;
                return false;
            }))
            changed();
        if (ImGui::Checkbox("Itemize even if the standard deduction is larger", &r.info.forceItemize)) changed();
        ImGui::Separator();
        ImGui::Text("Total itemized deductions: %s", ui::usd(s.itemizedDeduction).c_str());
        ui::EndCard();
    }
    stepFooter(Step::Income, Step::Credits);
}

// ------------------------------------------------------------------ credits

void App::drawCredits() {
    const float fs = ImGui::GetFontSize();
    TaxReturn& r = *ret_;
    ui::Heading("Credits");
    ui::Muted("Credits reduce your tax dollar for dollar. Refundable credits can be paid to you even if you owe no tax.");
    sectionGap();
    const float width = std::min(ImGui::GetContentRegionAvail().x, fs * 46.0f);

    struct CreditRow {
        const char* name;
        const char* form;
        const char* line;
        bool refundable;
    };
    const CreditRow credits[] = {
        {"Child tax credit / credit for other dependents", "1040", "19", false},
        {"Additional child tax credit", "1040", "28", true},
        {"Earned income credit", "1040", "27a", true},
        {"Child and dependent care credit", "Schedule 3", "2", false},
        {"Education credits", "Schedule 3", "3", false},
        {"American opportunity credit (refundable part)", "1040", "29", true},
        {"Retirement savings contributions credit", "Schedule 3", "4", false},
    };
    ui::BeginCard("##summary", width);
    ui::SubHeading("Your credits");
    if (ImGui::BeginTable("##credits", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Credit", ImGuiTableColumnFlags_WidthStretch, 3.0f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, fs * 7.0f);
        ImGui::TableSetupColumn("Amount", ImGuiTableColumnFlags_WidthFixed, fs * 7.0f);
        Money total;
        for (const auto& c : credits) {
            const FormResult* f = result_.form(c.form);
            const Line* l = f ? f->find(c.line) : nullptr;
            const Money amount = l ? l->amount : Money();
            total += amount;
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (amount.isZero()) ui::Muted(c.name);
            else ImGui::TextUnformatted(c.name);
            if (l && !l->how.empty()) ImGui::SetItemTooltip("%s", l->how.c_str());
            ImGui::TableSetColumnIndex(1);
            ui::Muted(c.refundable ? "refundable" : "");
            ImGui::TableSetColumnIndex(2);
            ui::MoneyText(amount);
        }
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::PushFont(g_fonts.bold, 0.0f);
        ImGui::TextUnformatted("Total");
        ImGui::TableSetColumnIndex(2);
        ui::MoneyText(total);
        ImGui::PopFont();
        ImGui::EndTable();
    }
    ui::MutedWrapped("Child credits and the EIC come from your dependents and income; there's nothing extra to enter.");
    ui::EndCard();
    sectionGap();

    ui::SubHeading("Child and dependent care");
    ui::BeginCard("##care", width);
    ui::MutedWrapped("What you paid for care of a child under 13 (or a disabled dependent) so you could work or look for work: "
                     "daycare, preschool, before/after-school care, day camp. Up to $3,000 for one person, $6,000 for two or more.");
    ImGui::Spacing();
    if (r.dependents.empty()) {
        ui::Muted("Add dependents first.");
    } else if (ImGui::BeginTable("##carel", 2, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("n", ImGuiTableColumnFlags_WidthFixed, fs * 17.0f);
        ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);
        for (std::size_t i = 0; i < r.dependents.size(); ++i) {
            Dependent& d = r.dependents[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            const std::string name = trim(d.first + " " + d.last);
            ImGui::Text("%s (%s)", name.empty() ? "Dependent" : name.c_str(), ageText(d.birthDate, r.info.year).c_str());
            ImGui::TableSetColumnIndex(1);
            if (ui::MoneyInput("##care", d.careExpenses, fs * 10)) changed();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ui::EndCard();
    sectionGap();

    ui::SubHeading("Education (Form 1098-T)");
    ui::BeginCard("##edu", width);
    ui::MutedWrapped("College tuition and required fees for you, your spouse or a dependent, minus tax-free scholarships. "
                     "The American opportunity credit is worth up to $2,500 per student (40% refundable); the lifetime "
                     "learning credit up to $2,000 per return.");
    ImGui::Spacing();
    int remove = -1;
    if (!r.education.empty() && ImGui::BeginTable("##edut", 4, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Student", ImGuiTableColumnFlags_WidthFixed, fs * 12);
        ImGui::TableSetupColumn("Qualified expenses", ImGuiTableColumnFlags_WidthFixed, fs * 10);
        ImGui::TableSetupColumn("First 4 years (AOTC)", ImGuiTableColumnFlags_WidthFixed, fs * 9);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, fs * 5);
        ImGui::TableHeadersRow();
        for (std::size_t i = 0; i < r.education.size(); ++i) {
            Education& e = r.education[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::SetNextItemWidth(-1);
            if (ui::InputString("##student", e.student)) changed();
            ImGui::TableSetColumnIndex(1);
            if (ui::MoneyInput("##exp", e.qualifiedExpenses, -1)) changed();
            ImGui::TableSetColumnIndex(2);
            if (ImGui::Checkbox("##aotc", &e.aotcEligible)) changed();
            ImGui::SetItemTooltip("%s", schema<Education>().fields[2].help);
            ImGui::TableSetColumnIndex(3);
            if (ImGui::SmallButton("Remove")) remove = static_cast<int>(i);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (remove >= 0) {
        r.education.erase(r.education.begin() + remove);
        changed();
    }
    if (ImGui::Button("Add a student")) {
        r.education.emplace_back();
        changed();
    }
    ui::EndCard();
    sectionGap();

    ui::SubHeading("Retirement savings contributions credit");
    ui::BeginCard("##saver", width);
    ui::MutedWrapped("For lower and moderate incomes: a credit of 10-50% of up to $2,000 you save for retirement. Traditional IRA "
                     "contributions (under Deductions) and W-2 box 12 deferrals count automatically. Add Roth IRA contributions here.");
    ImGui::Spacing();
    if (ImGui::BeginTable("##roth", 2, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("l", ImGuiTableColumnFlags_WidthFixed, fs * 17.0f);
        ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Roth IRA contributions (you)");
        ImGui::TableSetColumnIndex(1);
        if (ui::MoneyInput("##rothtp", r.taxpayer.rothIra, fs * 10)) changed();
        if (r.info.status == FilingStatus::MarriedJoint) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Roth IRA contributions (spouse)");
            ImGui::TableSetColumnIndex(1);
            if (ui::MoneyInput("##rothsp", r.spouse.rothIra, fs * 10)) changed();
        }
        ImGui::EndTable();
    }
    ui::EndCard();
    stepFooter(Step::Deductions, Step::Payments);
}

// ----------------------------------------------------------------- payments

void App::drawPayments() {
    const float fs = ImGui::GetFontSize();
    TaxReturn& r = *ret_;
    ui::Heading("Payments and carryovers");
    ui::Muted("Tax you've already paid, and amounts carried over from last year's return.");
    sectionGap();
    const float width = std::min(ImGui::GetContentRegionAvail().x, fs * 46.0f);

    ui::SubHeading("Withholding");
    ui::BeginCard("##withholding", width);
    const std::pair<const char*, const char*> rows[] = {
        {"From W-2s (line 25a)", "25a"},
        {"From 1099s (line 25b)", "25b"},
        {"Additional Medicare Tax withholding (line 25c)", "25c"},
    };
    for (const auto& [label, line] : rows) {
        ImGui::TextUnformatted(label);
        ImGui::SameLine(fs * 20);
        ImGui::TextUnformatted(ui::usd(result_.line("1040", line)).c_str());
    }
    ui::Muted("Withholding comes from the forms you entered under Income.");
    ui::EndCard();
    sectionGap();

    ui::SubHeading("Estimated and other payments");
    ui::BeginCard("##payments", width);
    if (ui::RecordEditor("##pay", r.payments, schema<Payments>())) changed();
    ui::EndCard();
    sectionGap();

    ui::SubHeading(("Carryovers from " + yearText(-1)).c_str());
    ui::BeginCard("##carry", width);
    if (ui::RecordEditor("##co", r.carryovers, schema<Carryovers>())) changed();
    ui::EndCard();
    stepFooter(Step::Credits, Step::Review, "Review");
}

}  // namespace otgui
