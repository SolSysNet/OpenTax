#pragma once

// The OpenTax desktop application: a guided interview over the same TaxReturn the command
// line uses. Platform-independent; the platform main loop calls frame() once per frame.
//
// Screens edit the working return directly through bound widgets. Every change bumps a
// version so the return is recalculated (it takes well under a millisecond), and the file
// is saved as soon as no field is being edited, so work is never lost.

#include "opentax/calc.hpp"
#include "opentax/model.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace otgui {

enum class Step {
    Home,
    AboutYou,
    Dependents,
    Income,
    Deductions,
    Credits,
    Payments,
    Review,
    Forms,
};

enum class IncomeKind {
    Hub,
    Wages,
    Interest,
    Dividends,
    Sales,
    Business,
    Retirement,
    SocialSecurity,
    Unemployment,
    Other,
};

struct ConfirmRequest {
    std::string title;
    std::string message;
    std::string button;
    std::function<void()> action;
};

struct NewReturnForm {
    std::string first;
    std::string last;
    std::string path;
    ot::FilingStatus status = ot::FilingStatus::Single;
    std::string error;
};

class App {
public:
    explicit App(std::string initialPath);
    ~App();

    void frame();
    bool quitRequested() const { return quit_; }
    std::string windowTitle() const;
    bool wantsFrequentRedraw() const;

private:
    // ---- persistence (app.cpp)
    bool openReturn(const std::string& path);
    void closeReturn();
    bool createReturn(const NewReturnForm& form);
    void changed();  // the working return was edited
    void saveNow();
    void recalculate();
    void loadConfig();
    void saveConfig() const;
    void rememberRecent(const std::string& path);
    void chooseOpenFile();

    // ---- chrome (app.cpp)
    void drawMenuBar();
    void drawSidebar();
    void drawRefundBar();
    void drawStatusBar();
    void drawWelcome();
    void drawModals();
    void go(Step step);
    void notify(std::string message, bool error = false);
    void requestPopup(const char* name);
    void confirm(std::string title, std::string message, std::string button, std::function<void()> action);
    void stepFooter(Step back, Step next, const char* nextLabel = "Continue");

    // ---- interview screens (app_screens.cpp)
    void drawHome();
    void drawAboutYou();
    void drawDependents();
    void drawIncome();
    void drawIncomeHub();
    void drawDeductions();
    void drawCredits();
    void drawPayments();
    template <class T>
    void drawList(const char* title, const char* intro, std::vector<T>& items, const char* addLabel,
                  const std::function<std::string(const T&)>& describe, const std::function<ot::Money(const T&)>& amount,
                  const std::function<bool(const char*)>& hide = {});

    // ---- review (app_review.cpp)
    void drawReview();
    void drawForms();
    void exportPdf(bool preview);
    void exportCsv();
    std::string returnPdfBytes() const;
    Step stepForTopic(const std::string& topic) const;

    // ---- state
    std::optional<ot::TaxReturn> ret_;
    ot::Result result_;
    ot::Decimal marginal_;
    std::string path_;
    std::uint64_t version_ = 1;
    std::uint64_t computedVersion_ = 0;
    bool dirty_ = false;
    std::chrono::steady_clock::time_point lastChange_;
    std::string saveError_;

    Step step_ = Step::Home;
    IncomeKind income_ = IncomeKind::Hub;
    int editing_ = -1;  // entry being edited on a list screen, -1 for the list
    std::string selectedForm_ = "1040";
    std::string selectedLine_;

    bool quit_ = false;
    bool darkTheme_ = false;
    std::vector<std::string> recent_;
    std::string configPath_;
    std::string iniPath_;

    std::string notice_;
    bool noticeIsError_ = false;
    std::chrono::steady_clock::time_point noticeTime_;
    std::string pendingPopup_;
    ConfirmRequest confirm_;
    std::string typedPath_;
    bool typedPathForPdf_ = false;
    NewReturnForm newReturn_;
};

}  // namespace otgui
