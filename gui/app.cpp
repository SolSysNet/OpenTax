#include "app.hpp"

#include "imgui.h"
#include "imgui_internal.h"  // ErrorRecoveryStoreState / TryToRecoverState
#include "opentax/cli.hpp"
#include "opentax/report.hpp"
#include "opentax/util.hpp"
#include "platform.hpp"
#include "theme.hpp"
#include "widgets.hpp"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace otgui {

namespace fs = std::filesystem;
using namespace ot;

namespace {

constexpr const char* kReturnFilterDescription = "OpenTax returns (*.otx)";
constexpr const char* kReturnFilterPattern = "*.otx";
constexpr float kNoticeSeconds = 6.0f;

// A path must be printable UTF-8 that converts to a filesystem path; anything else (for
// example a corrupted config) is ignored rather than trusted.
bool isUsablePath(const std::string& s) {
    if (s.empty() || s.size() > 4096) return false;
    for (char ch : s) {
        if (static_cast<unsigned char>(ch) < 0x20 || ch == 0x7F) return false;
    }
    try {
        (void)fs::u8path(s);
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

bool samePath(const std::string& a, const std::string& b) {
    if (!isUsablePath(a) || !isUsablePath(b)) return a == b;
    std::error_code ec;
    if (fs::equivalent(fs::u8path(a), fs::u8path(b), ec)) return true;
    const std::string na = fs::u8path(a).lexically_normal().u8string();
    const std::string nb = fs::u8path(b).lexically_normal().u8string();
#ifdef _WIN32
    return iequals(na, nb);
#else
    return na == nb;
#endif
}

std::string documentsDirectory() {
    const char* home = std::getenv("USERPROFILE");
    if (!home || !*home) home = std::getenv("HOME");
    if (!home || !*home) return fs::current_path().u8string();
    fs::path docs = fs::u8path(home) / "Documents";
    std::error_code ec;
    return fs::is_directory(docs, ec) ? docs.u8string() : fs::u8path(home).u8string();
}

std::string safeFileName(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '-' || c == '_') out += c;
    }
    return trim(out);
}

std::string defaultReturnPath(int year, const std::string& first, const std::string& last) {
    std::string name = safeFileName(trim(first + " " + last));
    name = std::to_string(year) + " Tax Return" + (name.empty() ? std::string() : " - " + name) + ".otx";
    return (fs::u8path(documentsDirectory()) / fs::u8path(name)).u8string();
}

// "2025 Tax Return..." style names are ours to update as the name or year changes.
bool isDefaultReturnName(const std::string& path) {
    const std::string name = fs::u8path(path).filename().u8string();
    return name.size() > 15 && std::isdigit(static_cast<unsigned char>(name[0])) && name.compare(4, 11, " Tax Return") == 0;
}

// New returns default to last year (the one people file for), within the supported years.
int defaultTaxYear() {
    const int y = Date::today().year() - 1;
    return y < 2025 ? 2025 : y > 2026 ? 2026 : y;
}

struct StepInfo {
    Step step;
    const char* label;
};

const StepInfo kSteps[] = {
    {Step::Home, "Tax home"},     {Step::AboutYou, "About you"}, {Step::Dependents, "Dependents"},
    {Step::Income, "Income"},     {Step::Deductions, "Deductions"}, {Step::Credits, "Credits"},
    {Step::Payments, "Payments"}, {Step::Review, "Review"},       {Step::Forms, "Forms & PDF"},
};

}  // namespace

// ------------------------------------------------------------ lifecycle

App::App(std::string initialPath) {
    const std::string dir = configDirectory();
    configPath_ = (fs::u8path(dir) / "opentax-gui.cfg").u8string();
    iniPath_ = (fs::u8path(dir) / "imgui.ini").u8string();
    ImGui::GetIO().IniFilename = iniPath_.c_str();
    loadConfig();
    applyTheme(darkTheme_);
    newReturn_.year = defaultTaxYear();
    newReturn_.path = defaultReturnPath(newReturn_.year, "", "");

    try {
        if (!initialPath.empty()) {
            openReturn(initialPath);
        } else if (!recent_.empty()) {
            std::error_code ec;
            if (fs::exists(fs::u8path(recent_.front()), ec)) openReturn(recent_.front());
        }
    } catch (const std::exception& e) {
        notify(std::string("Could not open your last return: ") + e.what(), true);
    }
}

App::~App() {
    if (dirty_) saveNow();
    saveConfig();
}

void App::loadConfig() {
    std::ifstream in(fs::u8path(configPath_));
    std::string line;
    while (std::getline(in, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        if (key == "theme") darkTheme_ = value == "dark";
        if (key == "recent_files") rememberRecentFiles_ = value != "off";
        if (key == "recent" && rememberRecentFiles_ && isUsablePath(value) && recent_.size() < 8 &&
            std::none_of(recent_.begin(), recent_.end(), [&](const std::string& r) { return samePath(r, value); }))
            recent_.push_back(value);
    }
}

void App::saveConfig() const {
    // Write a temporary file and rename it over the old one, so a crash mid-write can't leave a
    // truncated config behind.
    const fs::path target = fs::u8path(configPath_);
    fs::path tmp = target;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        out << "theme=" << (darkTheme_ ? "dark" : "light") << '\n';
        out << "recent_files=" << (rememberRecentFiles_ ? "on" : "off") << '\n';
        if (rememberRecentFiles_) {
            for (const auto& r : recent_) {
                if (isUsablePath(r)) out << "recent=" << r << '\n';
            }
        }
        if (!out) return;
    }
    std::error_code ec;
    fs::rename(tmp, target, ec);
    if (ec) {
        fs::remove(target, ec);
        fs::rename(tmp, target, ec);
    }
}

void App::rememberRecent(std::string path) {
    if (!rememberRecentFiles_) return;
    recent_.erase(std::remove_if(recent_.begin(), recent_.end(), [&](const std::string& r) { return samePath(r, path); }),
                  recent_.end());
    recent_.insert(recent_.begin(), path);
    if (recent_.size() > 8) recent_.resize(8);
    saveConfig();
}

bool App::openReturn(std::string path) {
    if (dirty_) saveNow();
    if (!isUsablePath(path)) {
        notify("That file name can't be opened.", true);
        return false;
    }
    try {
        if (TaxReturn::isEncryptedFile(path)) {
            unlockPath_ = path;
            wipeString(unlockPassword_);
            unlockError_.clear();
            requestPopup("Unlock return");
            return false;
        }
        finishOpen(path, TaxReturn::load(path), std::nullopt);
        return true;
    } catch (const std::exception& e) {
        notify(std::string("Could not open the return: ") + e.what(), true);
        return false;
    }
}

void App::finishOpen(std::string path, TaxReturn r, std::optional<PasswordKey> key) {
    ret_ = std::move(r);
    key_ = std::move(key);
    path_ = path;
    dirty_ = false;
    saveError_.clear();
    ++version_;
    step_ = Step::Home;
    income_ = IncomeKind::Hub;
    editing_ = -1;
    selectedForm_ = "1040";
    selectedLine_.clear();
    rememberRecent(path);
    notify("Opened " + fs::u8path(path).filename().u8string());
}

void App::closeReturn() {
    if (dirty_) saveNow();
    ret_.reset();
    key_.reset();
    path_.clear();
    wipeString(newReturn_.password);
    wipeString(newReturn_.confirm);
    newReturn_ = {};
    newReturn_.year = defaultTaxYear();
    newReturn_.path = defaultReturnPath(newReturn_.year, "", "");
    ++version_;
}

bool App::createReturn(const NewReturnForm& form) {
    if (trim(form.path).empty()) {
        newReturn_.error = "Choose where to save your return.";
        return false;
    }
    std::error_code ec;
    if (fs::exists(fs::u8path(form.path), ec)) {
        newReturn_.error = "That file already exists. Open it instead, or choose another name.";
        return false;
    }
    TaxReturn r;
    r.info.year = form.year;
    r.taxpayer.first = trim(form.first);
    r.taxpayer.last = trim(form.last);
    r.info.status = form.status;
    std::optional<PasswordKey> key;
    try {
        if (form.encrypt) {
            const std::string problem = passwordProblem(form.password);
            if (!problem.empty()) {
                newReturn_.error = problem;
                return false;
            }
            if (form.password != form.confirm) {
                newReturn_.error = "The passwords don't match.";
                return false;
            }
            key = PasswordKey::fromNewPassword(form.password);
        }
        r.save(form.path, key ? &*key : nullptr);
    } catch (const std::exception& e) {
        newReturn_.error = e.what();
        return false;
    }
    const std::string path = form.path;
    wipeString(newReturn_.password);
    wipeString(newReturn_.confirm);
    finishOpen(path, std::move(r), std::move(key));
    step_ = Step::AboutYou;
    return true;
}

void App::changed() {
    dirty_ = true;
    lastChange_ = std::chrono::steady_clock::now();
    ++version_;
}

void App::saveNow() {
    if (!ret_ || path_.empty()) return;
    try {
        ret_->save(path_, key_ ? &*key_ : nullptr);
        dirty_ = false;
        saveError_.clear();
    } catch (const std::exception& e) {
        saveError_ = e.what();
    }
}

const Rules& App::rules() const {
    return rulesFor(ret_ && isSupportedYear(ret_->info.year) ? ret_->info.year : 2025);
}

std::string App::yearText(int offset) const { return std::to_string((ret_ ? ret_->info.year : 2025) + offset); }

void App::recalculate() {
    if (!ret_ || computedVersion_ == version_) return;
    result_ = calculate(*ret_);
    marginal_ = marginalRate(*ret_);
    computedVersion_ = version_;
}

std::string App::windowTitle() const {
    if (!ret_) return "OpenTax";
    std::string name = ret_->displayName();
    return (name.empty() ? std::to_string(ret_->info.year) + " return" : name) + " - OpenTax (" + fs::u8path(path_).filename().u8string() + ")";
}

bool App::wantsFrequentRedraw() const {
    // ImGui hands a burst of keystrokes (a paste-like password manager auto-type, say) to the
    // app one per frame; keep drawing until the queue is empty or typing crawls.
    if (!ImGui::GetCurrentContext()->InputEventsQueue.empty()) return true;
    if (dirty_) return true;  // keep ticking until the autosave lands
    if (notice_.empty()) return false;
    const float age = std::chrono::duration<float>(std::chrono::steady_clock::now() - noticeTime_).count();
    return age < kNoticeSeconds + 1.0f;
}

void App::notify(std::string message, bool error) {
    notice_ = std::move(message);
    noticeIsError_ = error;
    noticeTime_ = std::chrono::steady_clock::now();
}

void App::requestPopup(const char* name) { pendingPopup_ = name; }

void App::confirm(std::string title, std::string message, std::string button, std::function<void()> action) {
    confirm_ = ConfirmRequest{std::move(title), std::move(message), std::move(button), std::move(action)};
    requestPopup("Confirm##dialog");
}

void App::go(Step step) {
    step_ = step;
    editing_ = -1;
    if (step == Step::Income) income_ = IncomeKind::Hub;
    ImGui::SetScrollY(0.0f);
}

void App::chooseOpenFile() {
    if (nativeFileDialogsAvailable()) {
        if (auto path = openFileDialog("Open a return", {kReturnFilterDescription, kReturnFilterPattern})) openReturn(*path);
    } else {
        typedPath_.clear();
        typedPathForPdf_ = false;
        requestPopup("Enter a path##typed");
    }
}

// ----------------------------------------------------------------- frame

// Draws one frame. An exception while drawing (a bug, or data the UI didn't expect) is caught
// here: ImGui's window stack is unwound, the user sees the message, and the app keeps running.
void App::frame() {
    ImGuiErrorRecoveryState recovery;
    ImGui::ErrorRecoveryStoreState(&recovery);
    try {
        drawFrame();
    } catch (const std::exception& e) {
        ImGuiIO& io = ImGui::GetIO();
        const bool asserts = io.ConfigErrorRecoveryEnableAssert;
        io.ConfigErrorRecoveryEnableAssert = false;  // recovering on purpose
        ImGui::ErrorRecoveryTryToRecoverState(&recovery);
        io.ConfigErrorRecoveryEnableAssert = asserts;
        notify(std::string("Something went wrong: ") + e.what(), true);
        if (ret_) step_ = Step::Home;  // don't keep re-entering the failing screen
        editing_ = -1;
    }
}

void App::drawFrame() {
    recalculate();
    drawMenuBar();

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    const ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("##host", nullptr, hostFlags);
    ImGui::PopStyleVar(3);

    if (!ret_) {
        drawWelcome();
    } else {
        const float statusHeight = ImGui::GetFrameHeightWithSpacing() + 4.0f;
        const float sidebarWidth = ImGui::GetFontSize() * 13.0f;

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 12));
        ImGui::BeginChild("##sidebar", ImVec2(sidebarWidth, -statusHeight), ImGuiChildFlags_AlwaysUseWindowPadding);
        drawSidebar();
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();

        ImGui::SameLine(0, 0);
        ImGui::BeginGroup();
        drawRefundBar();
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(26, 18));
        ImGui::BeginChild("##content", ImVec2(0, -statusHeight), ImGuiChildFlags_AlwaysUseWindowPadding);
        switch (step_) {
            case Step::Home: drawHome(); break;
            case Step::AboutYou: drawAboutYou(); break;
            case Step::Dependents: drawDependents(); break;
            case Step::Income: drawIncome(); break;
            case Step::Deductions: drawDeductions(); break;
            case Step::Credits: drawCredits(); break;
            case Step::Payments: drawPayments(); break;
            case Step::Review: drawReview(); break;
            case Step::Forms: drawForms(); break;
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::EndGroup();
        drawStatusBar();
    }
    ImGui::End();

    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O, ImGuiInputFlags_RouteGlobal)) chooseOpenFile();
    if (ret_ && ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_RouteGlobal)) {
        saveNow();
        if (saveError_.empty()) notify("Saved");
    }

    if (!pendingPopup_.empty()) {
        ImGui::OpenPopup(pendingPopup_.c_str());
        pendingPopup_.clear();
    }
    drawModals();

    // Autosave when the user leaves the field, or after a second without edits (tabbing
    // from box to box keeps some field active). Only parsed values are ever written.
    const float idle = std::chrono::duration<float>(std::chrono::steady_clock::now() - lastChange_).count();
    if (dirty_ && (!ImGui::IsAnyItemActive() || idle > 1.0f)) saveNow();
}

void App::drawMenuBar() {
    if (!ImGui::BeginMainMenuBar()) return;
    const bool open = ret_.has_value();
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New Return...")) closeReturn();
        if (ImGui::MenuItem("Open...", "Ctrl+O")) chooseOpenFile();
        if (ImGui::BeginMenu("Open Recent", !recent_.empty())) {
            for (const auto& r : std::vector<std::string>(recent_)) {
                if (ImGui::MenuItem(r.c_str())) openReturn(r);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Clear Recent Files")) {
                recent_.clear();
                saveConfig();
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Save", "Ctrl+S", false, open)) {
            saveNow();
            if (saveError_.empty()) notify("Saved");
        }
        ImGui::Separator();
        if (ImGui::MenuItem(key_ ? "Change Password..." : "Protect with Password...", nullptr, false, open)) startSetPassword();
        if (ImGui::MenuItem("Remove Password...", nullptr, false, open && key_.has_value())) {
            confirm("Remove the password?",
                    "The return will be saved unencrypted, and anyone with access to the file can read it.", "Remove",
                    [this] {
                        key_.reset();
                        saveNow();
                        if (saveError_.empty()) notify("Password removed");
                    });
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Save Return as PDF...", nullptr, false, open)) exportPdf(false);
        if (ImGui::MenuItem("Preview PDF", nullptr, false, open && nativeFileDialogsAvailable())) exportPdf(true);
        if (ImGui::MenuItem("Export Lines as CSV...", nullptr, false, open)) exportCsv();
        ImGui::Separator();
        if (ImGui::MenuItem("Close Return", nullptr, false, open)) closeReturn();
        ImGui::Separator();
        if (ImGui::MenuItem("Exit")) quit_ = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Go", open)) {
        for (const auto& s : kSteps) {
            if (ImGui::MenuItem(s.label, nullptr, step_ == s.step)) go(s.step);
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Dark Theme", nullptr, darkTheme_)) {
            darkTheme_ = !darkTheme_;
            applyTheme(darkTheme_);
            saveConfig();
        }
        if (ImGui::MenuItem("Remember Recent Files", nullptr, rememberRecentFiles_)) {
            rememberRecentFiles_ = !rememberRecentFiles_;
            if (!rememberRecentFiles_) recent_.clear();
            else if (ret_) rememberRecent(path_);
            saveConfig();
        }
        ImGui::SetItemTooltip("Keep a list of recently opened returns (file names only) and reopen the last one at startup.");
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("About OpenTax")) requestPopup("About OpenTax");
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

void App::drawSidebar() {
    ImGui::PushFont(g_fonts.bold, kBaseFontSize * 1.2f);
    ImGui::PushStyleColor(ImGuiCol_Text, colorAccent());
    ImGui::TextUnformatted("OpenTax");
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::SameLine();
    ui::Muted(yearText().c_str());
    const std::string name = ret_->displayName();
    ImGui::PushTextWrapPos(0.0f);
    ui::Muted(name.empty() ? "Federal return" : name.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(0, 10));

    int errors = 0;
    for (const auto& d : result_.diagnostics) {
        if (d.severity == Severity::Error) ++errors;
    }
    const float fs = ImGui::GetFontSize();
    int number = 0;
    for (const auto& s : kSteps) {
        ImGui::PushID(static_cast<int>(s.step));
        const bool selected = step_ == s.step;
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        if (ImGui::Selectable("##step", selected, 0, ImVec2(0, fs * 1.7f))) go(s.step);
        // Number bubble and label drawn over the selectable. Tax home isn't numbered.
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 center(pos.x + fs * 0.8f, pos.y + fs * 0.85f);
        const ImU32 bubble = ImGui::GetColorU32(selected ? colorAccent() : colorMuted());
        if (s.step == Step::Home) {
            draw->AddCircle(center, fs * 0.5f, bubble, 0, 1.5f);
        } else {
            char num[4];
            std::snprintf(num, sizeof num, "%d", ++number);
            draw->AddCircleFilled(center, fs * 0.55f, bubble);
            ImGui::PushFont(nullptr, kBaseFontSize * 0.8f);
            const ImVec2 ts = ImGui::CalcTextSize(num);
            draw->AddText(ImVec2(center.x - ts.x / 2, center.y - ts.y / 2), IM_COL32_WHITE, num);
            ImGui::PopFont();
        }
        draw->AddText(ImVec2(pos.x + fs * 1.8f, pos.y + fs * 0.35f), ImGui::GetColorU32(ImGuiCol_Text), s.label);
        if (s.step == Step::Review && errors > 0) {
            char badge[16];
            std::snprintf(badge, sizeof badge, "%d", errors);
            const ImVec2 bs = ImGui::CalcTextSize(badge);
            const float right = pos.x + ImGui::GetContentRegionAvail().x;
            const ImVec2 bmin(right - bs.x - fs * 0.9f, pos.y + fs * 0.3f);
            draw->AddRectFilled(bmin, ImVec2(right - fs * 0.2f, bmin.y + bs.y + 2), ImGui::GetColorU32(colorNegative()), bs.y);
            draw->AddText(ImVec2(bmin.x + fs * 0.35f, bmin.y + 1), IM_COL32_WHITE, badge);
        }
        ImGui::PopID();
    }
}

void App::drawRefundBar() {
    const float fs = ImGui::GetFontSize();
    const Summary& s = result_.summary;
    const bool refund = !s.refund.isZero();
    const bool owe = !s.owed.isZero();
    ImVec4 tint = refund ? colorPositive() : owe ? colorNegative() : colorMuted();
    ImVec4 bg = tint;
    bg.w = themeIsDark() ? 0.16f : 0.09f;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(26, fs * 0.55f));
    ImGui::BeginChild("##refundbar", ImVec2(0, fs * 3.6f), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    ImGui::BeginGroup();
    ui::Muted(refund ? "Federal refund" : owe ? "Federal tax due" : "Federal balance");
    ImGui::PushFont(g_fonts.bold, kBaseFontSize * 1.65f);
    ImGui::TextColored(tint, "%s", ui::usd(refund ? s.refund : s.owed).c_str());
    ImGui::PopFont();
    ImGui::EndGroup();

    struct Stat {
        const char* label;
        std::string value;
    };
    std::string effective = "-";
    if (s.agi > Money() && s.totalTax > Money()) {
        const long long tenths = (s.totalTax.cents() * 1000 + s.agi.cents() / 2) / s.agi.cents();
        effective = std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + "%";
    }
    std::string marginal = marginal_.str() + "%";
    const Stat stats[] = {
        {"Adjusted gross income", ui::usd(s.agi)},
        {"Taxable income", ui::usd(s.taxableIncome)},
        {"Total tax", ui::usd(s.totalTax)},
        {"Effective rate", effective},
        {"Marginal rate", marginal},
    };
    const float col = fs * 9.5f;
    float x = ImGui::GetWindowWidth() - 26 - col * 5;
    x = std::max(x, fs * 14.0f);
    for (const auto& st : stats) {
        ImGui::SameLine(x);
        ImGui::BeginGroup();
        ui::Muted(st.label);
        ImGui::PushFont(g_fonts.bold, kBaseFontSize * 1.05f);
        ImGui::TextUnformatted(st.value.c_str());
        ImGui::PopFont();
        ImGui::EndGroup();
        if (std::string(st.label) == "Marginal rate")
            ImGui::SetItemTooltip("Tax on your next $100 of wages, including credits that phase out.");
        x += col;
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void App::drawStatusBar() {
    ImGui::Separator();
    ImGui::SetCursorPosX(10.0f);
    ImGui::AlignTextToFramePadding();
    if (!saveError_.empty()) {
        ImGui::TextColored(colorNegative(), "Not saved: %s", saveError_.c_str());
    } else {
        if (key_) {
            ImGui::TextColored(colorPositive(), "Encrypted");
            ImGui::SetItemTooltip("This return is protected with a password.");
            ImGui::SameLine();
        }
        ui::Muted((path_ + (dirty_ ? "  (saving...)" : "  (saved)")).c_str());
    }
    if (notice_.empty()) return;
    const float age = std::chrono::duration<float>(std::chrono::steady_clock::now() - noticeTime_).count();
    if (age > kNoticeSeconds) return;
    const float width = ImGui::CalcTextSize(notice_.c_str()).x;
    ImGui::SameLine(std::max(ImGui::GetWindowWidth() - width - 16.0f, ImGui::GetCursorPosX() + 20.0f));
    ImVec4 color = noticeIsError_ ? colorNegative() : colorPositive();
    color.w = std::min(1.0f, (kNoticeSeconds - age) / 1.0f);
    ImGui::TextColored(color, "%s", notice_.c_str());
}

void App::drawWelcome() {
    const float fs = ImGui::GetFontSize();
    const float width = std::min(ImGui::GetContentRegionAvail().x - fs * 4, fs * 52.0f);
    ImGui::SetCursorPos(ImVec2((ImGui::GetWindowWidth() - width) * 0.5f, fs * 3.0f));
    ImGui::BeginGroup();
    ImGui::PushFont(g_fonts.bold, kBaseFontSize * 2.6f);
    ImGui::TextColored(colorAccent(), "OpenTax");
    ImGui::PopFont();
    ImGui::PushFont(nullptr, kBaseFontSize * 1.25f);
    ImGui::TextUnformatted("Free, open source federal tax preparation for 2025 and 2026.");
    ImGui::PopFont();
    ui::Muted("Your return stays in one file on your computer. Every number shows how it was figured.");
    ImGui::Dummy(ImVec2(0, fs));

    const float half = (width - fs) * 0.5f;
    ui::BeginCard("##start", half);
    ui::SubHeading("Start a return");
    ImGui::Spacing();
    const float field = half - fs * 2.2f;
    ImGui::TextUnformatted("Tax year");
    bool yearChanged = false;
    for (int y : {2025, 2026}) {
        ImGui::SameLine(y == 2025 ? fs * 5.5f : 0.0f);
        if (ImGui::RadioButton(std::to_string(y).c_str(), newReturn_.year == y)) {
            newReturn_.year = y;
            yearChanged = true;
        }
    }
    ImGui::TextUnformatted("First name");
    ImGui::SetNextItemWidth(field);
    bool nameChanged = ui::InputString("##first", newReturn_.first);
    ImGui::TextUnformatted("Last name");
    ImGui::SetNextItemWidth(field);
    nameChanged |= ui::InputString("##last", newReturn_.last);
    if ((nameChanged || yearChanged) && isDefaultReturnName(newReturn_.path))
        newReturn_.path = defaultReturnPath(newReturn_.year, newReturn_.first, newReturn_.last);
    ImGui::TextUnformatted("Filing status");
    ui::ChoiceCombo("##status", newReturn_.status, field);
    ImGui::TextUnformatted("Save as");
    ImGui::SetNextItemWidth(field - (nativeFileDialogsAvailable() ? fs * 5.5f : 0.0f));
    ui::InputString("##path", newReturn_.path);
    if (nativeFileDialogsAvailable()) {
        ImGui::SameLine();
        if (ImGui::Button("Browse...")) {
            if (auto p = saveFileDialog("Save your return as", {kReturnFilterDescription, kReturnFilterPattern}, "otx",
                                        fs::u8path(newReturn_.path).filename().u8string()))
                newReturn_.path = *p;
        }
    }
    ImGui::Checkbox("Protect with a password", &newReturn_.encrypt);
    ImGui::SetItemTooltip("Encrypts the file so it can't be read without the password.");
    if (newReturn_.encrypt) {
        ImGui::SetNextItemWidth(field);
        ui::InputStringHint("##pw", "Password (at least 8 characters)", newReturn_.password, ImGuiInputTextFlags_Password);
        ImGui::SetNextItemWidth(field);
        ui::InputStringHint("##pw2", "Confirm password", newReturn_.confirm, ImGuiInputTextFlags_Password);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + field);
        ui::Muted("There's no way to recover a forgotten password, so keep it somewhere safe.");
        ImGui::PopTextWrapPos();
    }
    ImGui::Spacing();
    ui::ErrorText(newReturn_.error);
    if (ui::PrimaryButton("Start my return", ImVec2(field, fs * 2.0f))) {
        newReturn_.error.clear();
        createReturn(newReturn_);
    }
    ui::EndCard();

    ImGui::SameLine(0, fs);
    ImGui::BeginGroup();
    ui::BeginCard("##open", half);
    ui::SubHeading("Continue a return");
    ImGui::Spacing();
    if (ImGui::Button("Open a return...", ImVec2(half - fs * 2.2f, 0))) chooseOpenFile();
    if (!recent_.empty()) {
        ImGui::Spacing();
        ui::Muted("Recent");
        for (const auto& r : std::vector<std::string>(recent_)) {
            const std::string label = fs::u8path(r).filename().u8string();
            if (ImGui::Selectable(label.c_str())) openReturn(r);
            ImGui::SetItemTooltip("%s", r.c_str());
        }
    }
    ui::EndCard();
    ui::BeginCard("##why", half);
    ui::SubHeading("Why OpenTax");
    ImGui::Spacing();
    const char* points[] = {
        "Private: no account, no cloud, no telemetry. Nothing leaves this computer.",
        "Transparent: every line on every form explains how it was calculated.",
        "Current: 2025 and 2026 law, including the new tips, overtime, car loan and senior deductions.",
        "Free forever: MIT licensed. No upsells, no \"deluxe\" tier.",
    };
    for (const char* p : points) {
        ImGui::Bullet();
        ImGui::SameLine();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + half - fs * 3.2f);
        ImGui::TextUnformatted(p);
        ImGui::PopTextWrapPos();
    }
    ui::EndCard();
    ImGui::EndGroup();
    ImGui::EndGroup();
}

void App::stepFooter(Step back, Step next, const char* nextLabel) {
    const float fs = ImGui::GetFontSize();
    const float width = std::min(ImGui::GetContentRegionAvail().x, fs * 46.0f);
    ImGui::Dummy(ImVec2(0, fs));
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(p, ImVec2(p.x + width, p.y), ImGui::GetColorU32(ImGuiCol_Separator));
    ImGui::Dummy(ImVec2(0, fs * 0.6f));
    if (back != step_ && ImGui::Button("Back", ImVec2(fs * 6, fs * 1.9f))) go(back);
    if (next != step_) {
        ImGui::SameLine(ImGui::GetCursorStartPos().x + width - fs * 9);
        if (ui::PrimaryButton(nextLabel, ImVec2(fs * 9, fs * 1.9f))) go(next);
    }
}

void App::drawModals() {
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    const float fs = ImGui::GetFontSize();

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Confirm##dialog", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ui::SubHeading(confirm_.title.c_str());
        ImGui::PushTextWrapPos(fs * 26);
        ImGui::TextUnformatted(confirm_.message.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (ui::DangerButton(confirm_.button.c_str(), ImVec2(fs * 7, 0))) {
            if (confirm_.action) confirm_.action();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(fs * 7, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("About OpenTax", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushFont(g_fonts.bold, kBaseFontSize * 1.4f);
        ImGui::TextColored(colorAccent(), "OpenTax %s", kVersion);
        ImGui::PopFont();
        ImGui::TextUnformatted("Free, open source federal income tax preparation.");
        ui::Muted("Tax years 2025 and 2026. MIT License. Uses Dear ImGui (MIT).");
        ImGui::Spacing();
        ImGui::PushTextWrapPos(fs * 28);
        ui::Muted("OpenTax is not tax advice and does not e-file. It prepares a line-by-line computation of "
                  "Form 1040 and its schedules for you to review and copy onto the official IRS forms. "
                  "Check your return carefully; you are responsible for what you file.");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (ImGui::Button("Close", ImVec2(fs * 6, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    drawPasswordModals();

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Enter a path##typed", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(typedPathForPdf_ ? "Save the PDF to:" : "Open the return at:");
        ImGui::SetNextItemWidth(fs * 30);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        const bool enter = ui::InputString("##path", typedPath_, ImGuiInputTextFlags_EnterReturnsTrue);
        if (ui::PrimaryButton("OK", ImVec2(fs * 6, 0)) || enter) {
            if (typedPathForPdf_) {
                try {
                    const std::string bytes = returnPdfBytes();
                    std::ofstream out(fs::u8path(typedPath_), std::ios::binary | std::ios::trunc);
                    out << bytes;
                    if (!out) throw Error("cannot write '" + typedPath_ + "'");
                    notify("Saved " + typedPath_);
                } catch (const std::exception& e) {
                    notify(e.what(), true);
                }
            } else {
                openReturn(typedPath_);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(fs * 6, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// ------------------------------------------------------------ passwords

void App::startSetPassword() {
    wipeString(newPassword_);
    wipeString(confirmPassword_);
    passwordError_.clear();
    requestPopup("Password##set");
}

void App::drawPasswordModals() {
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    const float fs = ImGui::GetFontSize();

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Unlock return", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ui::SubHeading(fs::u8path(unlockPath_).filename().u8string().c_str());
        ImGui::TextUnformatted("This return is protected with a password.");
        ImGui::Spacing();
        ImGui::SetNextItemWidth(fs * 22);
        // Keep the cursor in the field, including after a wrong password (Enter ends editing).
        if (ImGui::IsWindowAppearing() || refocusPassword_) ImGui::SetKeyboardFocusHere();
        refocusPassword_ = false;
        const bool enter = ui::InputStringHint("##unlock", "Password", unlockPassword_,
                                               ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue);
        ui::ErrorText(unlockError_);
        ImGui::Spacing();
        if (ui::PrimaryButton("Unlock", ImVec2(fs * 7, 0)) || enter) {
            try {
                PasswordKey key;
                TaxReturn r = TaxReturn::load(unlockPath_, unlockPassword_, &key);
                wipeString(unlockPassword_);
                finishOpen(unlockPath_, std::move(r), std::move(key));
                ImGui::CloseCurrentPopup();
            } catch (const WrongPassword&) {
                wipeString(unlockPassword_);
                unlockError_ = "That password didn't work. Check Caps Lock and try again.";
                refocusPassword_ = true;
            } catch (const std::exception& e) {
                wipeString(unlockPassword_);
                unlockError_ = e.what();
                refocusPassword_ = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(fs * 7, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            wipeString(unlockPassword_);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Password##set", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ui::SubHeading(key_ ? "Change the password" : "Protect this return with a password");
        ImGui::PushTextWrapPos(fs * 26);
        ImGui::TextUnformatted("The file is encrypted, so it can't be read without the password. There's no way to recover a "
                               "forgotten password, so keep it somewhere safe.");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        ImGui::SetNextItemWidth(fs * 22);
        if (ImGui::IsWindowAppearing() || refocusPassword_) ImGui::SetKeyboardFocusHere();
        refocusPassword_ = false;
        ui::InputStringHint("##new", "New password (at least 8 characters)", newPassword_, ImGuiInputTextFlags_Password);
        ImGui::SetNextItemWidth(fs * 22);
        const bool enter = ui::InputStringHint("##confirm", "Confirm password", confirmPassword_,
                                               ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue);
        ui::ErrorText(passwordError_);
        ImGui::Spacing();
        if (ui::PrimaryButton(key_ ? "Change" : "Protect", ImVec2(fs * 7, 0)) || enter) {
            passwordError_ = passwordProblem(newPassword_);
            if (passwordError_.empty() && newPassword_ != confirmPassword_) passwordError_ = "The passwords don't match.";
            refocusPassword_ = !passwordError_.empty();
            if (passwordError_.empty()) {
                try {
                    const bool changing = key_.has_value();
                    key_ = PasswordKey::fromNewPassword(newPassword_);
                    saveNow();
                    if (saveError_.empty()) notify(changing ? "Password changed" : "The return is now encrypted");
                    wipeString(newPassword_);
                    wipeString(confirmPassword_);
                    ImGui::CloseCurrentPopup();
                } catch (const std::exception& e) {
                    passwordError_ = e.what();
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(fs * 7, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            wipeString(newPassword_);
            wipeString(confirmPassword_);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

}  // namespace otgui
