#include "opentax/cli.hpp"

#include "opentax/calc.hpp"
#include "opentax/model.hpp"
#include "opentax/report.hpp"
#include "opentax/return_pdf.hpp"
#include "opentax/util.hpp"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <ostream>
#include <type_traits>
#include <variant>

namespace ot {
namespace {

namespace fs = std::filesystem;

constexpr const char* kDefaultFile = "return.otx";

struct Context {
    std::string path;
    std::ostream& out;
    std::ostream& err;
    std::vector<std::string> args;  // after the command name
    std::vector<std::string> flags;

    bool flag(const std::string& name) const {
        for (const auto& f : flags) {
            if (f == name) return true;
        }
        return false;
    }
    std::optional<std::string> option(const std::string& name) const {
        const std::string prefix = name + "=";
        for (const auto& f : flags) {
            if (startsWith(f, prefix)) return f.substr(prefix.size());
        }
        return std::nullopt;
    }
};

TaxReturn loadReturn(const Context& c) {
    std::error_code ec;
    if (!fs::exists(fs::u8path(c.path), ec))
        throw Error("no return at '" + c.path + "'. Create one with 'opentax new', or pass -f FILE.");
    return TaxReturn::load(c.path);
}

std::string normalizeFormId(std::string_view s) {
    std::string t = toLower(s);
    std::string out;
    for (char ch : t) {
        if (ch == ' ' || ch == '-' || ch == '_' || ch == '(' || ch == ')') continue;
        out += ch;
    }
    for (const char* prefix : {"schedule", "form"}) {
        if (startsWith(out, prefix)) out = (std::string(prefix) == "schedule" ? "sch" : "") + out.substr(std::strlen(prefix));
    }
    return out;
}

const FormResult* findForm(const Result& result, const std::string& query) {
    const std::string q = normalizeFormId(query);
    for (const auto& f : result.forms) {
        if (normalizeFormId(f.id) == q) return &f;
    }
    for (const auto& f : result.forms) {
        if (startsWith(normalizeFormId(f.id), q)) return &f;
    }
    return nullptr;
}

// Applies key=value arguments to a record through its schema.
template <class T>
void applyAssignments(T& record, const Schema<T>& schema, const std::vector<std::string>& args, std::size_t first) {
    for (std::size_t i = first; i < args.size(); ++i) {
        const auto eq = args[i].find('=');
        if (eq == std::string::npos) throw Error("expected key=value, got '" + args[i] + "'");
        const std::string key = args[i].substr(0, eq);
        const Field<T>* f = findField<T>(key);
        if (!f) throw Error("'" + key + "' is not a field of " + schema.command + ". See 'opentax fields " + schema.command + "'.");
        setField(record, *f, args[i].substr(eq + 1));
    }
}

template <class T>
std::string describe(const T& record, const Schema<T>& schema) {
    static const T blank{};
    std::string out;
    for (const auto& f : schema.fields) {
        const std::string v = getField(record, f);
        if (v == getField(blank, f)) continue;
        out += "    " + std::string(f.key) + "=" + v + "\n";
    }
    return out.empty() ? "    (empty)\n" : out;
}

// Finds the list whose command name matches and calls f(schema, vector).
template <class F>
bool withList(TaxReturn& r, const std::string& name, F&& f) {
    bool found = false;
    forEachList(r, [&](const auto& s, auto& items) {
        if (!found && iequals(name, s.command)) {
            found = true;
            f(s, items);
        }
    });
    return found;
}

template <class F>
bool withSingle(TaxReturn& r, const std::string& name, F&& f) {
    bool found = false;
    forEachSingle(r, [&](const char*, const char* command, const auto& s, auto& record) {
        if (!found && iequals(name, command)) {
            found = true;
            f(s, record);
        }
    });
    return found;
}

std::string recordNames() {
    TaxReturn r;
    std::string lists, singles;
    forEachList(r, [&](const auto& s, auto&) { lists += std::string(lists.empty() ? "" : ", ") + s.command; });
    forEachSingle(r, [&](const char*, const char* command, const auto&, auto&) {
        singles += std::string(singles.empty() ? "" : ", ") + command;
    });
    return "  Forms you can add: " + lists + "\n  Sections you can set: " + singles + "\n";
}

std::size_t parseIndex(const std::string& s, std::size_t size) {
    auto n = parseInt(s);
    if (!n || *n < 1 || static_cast<std::size_t>(*n) > size)
        throw Error("'" + s + "' is not an entry number (1-" + std::to_string(size) + ")");
    return static_cast<std::size_t>(*n - 1);
}

// ------------------------------------------------------------- commands

int cmdNew(Context& c) {
    std::error_code ec;
    if (fs::exists(fs::u8path(c.path), ec) && !c.flag("force"))
        throw Error("'" + c.path + "' already exists (use --force to replace it)");
    TaxReturn r;
    for (const auto& a : c.args) {
        const auto eq = a.find('=');
        if (eq == std::string::npos) throw Error("expected key=value, got '" + a + "'");
        const std::string key = a.substr(0, eq);
        const std::string value = a.substr(eq + 1);
        if (const auto* f = findField<ReturnInfo>(key)) setField(r.info, *f, value);
        else if (const auto* p = findField<Person>(key)) setField(r.taxpayer, *p, value);
        else throw Error("unknown field '" + key + "'");
    }
    if (!isSupportedYear(r.info.year)) throw Error("tax year " + std::to_string(r.info.year) + " is not supported");
    r.save(c.path);
    c.out << "Created " << c.path << " (" << r.info.year << ", " << choiceLabel(r.info.status) << ")\n";
    return 0;
}

int cmdSet(Context& c) {
    if (c.args.empty()) throw Error("usage: opentax set SECTION key=value...\n" + recordNames());
    TaxReturn r = loadReturn(c);
    const bool ok = withSingle(r, c.args[0], [&](const auto& s, auto& record) { applyAssignments(record, s, c.args, 1); });
    if (!ok) throw Error("'" + c.args[0] + "' is not a section.\n" + recordNames());
    r.save(c.path);
    c.out << "Updated " << c.args[0] << "\n";
    return 0;
}

int cmdAdd(Context& c) {
    if (c.args.empty()) throw Error("usage: opentax add FORM key=value...\n" + recordNames());
    TaxReturn r = loadReturn(c);
    std::size_t count = 0;
    const bool ok = withList(r, c.args[0], [&](const auto& s, auto& items) {
        typename std::decay_t<decltype(items)>::value_type item{};
        applyAssignments(item, s, c.args, 1);
        items.push_back(item);
        count = items.size();
    });
    if (!ok) throw Error("'" + c.args[0] + "' is not a form type.\n" + recordNames());
    r.save(c.path);
    c.out << "Added " << c.args[0] << " #" << count << "\n";
    return 0;
}

int cmdEdit(Context& c) {
    if (c.args.size() < 2) throw Error("usage: opentax edit FORM N key=value...");
    TaxReturn r = loadReturn(c);
    const bool ok = withList(r, c.args[0], [&](const auto& s, auto& items) {
        auto& item = items[parseIndex(c.args[1], items.size())];
        applyAssignments(item, s, c.args, 2);
    });
    if (!ok) throw Error("'" + c.args[0] + "' is not a form type.\n" + recordNames());
    r.save(c.path);
    c.out << "Updated " << c.args[0] << " #" << c.args[1] << "\n";
    return 0;
}

int cmdRemove(Context& c) {
    if (c.args.size() != 2) throw Error("usage: opentax remove FORM N");
    TaxReturn r = loadReturn(c);
    const bool ok = withList(r, c.args[0], [&](const auto&, auto& items) {
        items.erase(items.begin() + static_cast<std::ptrdiff_t>(parseIndex(c.args[1], items.size())));
    });
    if (!ok) throw Error("'" + c.args[0] + "' is not a form type.\n" + recordNames());
    r.save(c.path);
    c.out << "Removed " << c.args[0] << " #" << c.args[1] << "\n";
    return 0;
}

int cmdList(Context& c) {
    TaxReturn r = loadReturn(c);
    const std::string only = c.args.empty() ? "" : c.args[0];
    bool any = false;
    forEachSingle(r, [&](const char*, const char* command, const auto& s, const auto& record) {
        if (!only.empty() && !iequals(only, command)) return;
        any = true;
        c.out << command << "\n" << describe(record, s);
    });
    forEachList(r, [&](const auto& s, const auto& items) {
        if (!only.empty() && !iequals(only, s.command)) return;
        any = true;
        for (std::size_t i = 0; i < items.size(); ++i) c.out << s.command << " #" << (i + 1) << "\n" << describe(items[i], s);
        if (items.empty() && !only.empty()) c.out << "No " << s.command << " entries.\n";
    });
    if (!any) throw Error("'" + only + "' is not a form type or section.\n" + recordNames());
    return 0;
}

int cmdFields(Context& c) {
    if (c.args.empty()) throw Error("usage: opentax fields FORM\n" + recordNames());
    TaxReturn r;
    auto print = [&](const auto& s, const auto& blank) {
        c.out << s.title << " (" << s.command << ")\n";
        for (const auto& f : s.fields) {
            std::string kind = std::visit(
                [&](auto member) -> std::string {
                    using V = std::decay_t<decltype(blank.*member)>;
                    if constexpr (std::is_same_v<V, Money>) return "amount";
                    else if constexpr (std::is_same_v<V, std::optional<Date>>) return "date";
                    else if constexpr (std::is_same_v<V, bool>) return "yes/no";
                    else if constexpr (std::is_same_v<V, int>) return "number";
                    else if constexpr (std::is_same_v<V, std::string>) return "text";
                    else {
                        std::string keys;
                        for (const auto& ch : choices(V{})) keys += std::string(keys.empty() ? "" : "|") + ch.key;
                        return keys;
                    }
                },
                f.member);
            c.out << "  " << f.key << std::string(f.key[0] && std::strlen(f.key) < 16 ? 16 - std::strlen(f.key) : 1, ' ')
                  << f.label << "  [" << kind << "]\n";
            if (*f.help) c.out << "                  " << f.help << "\n";
        }
    };
    bool ok = withList(r, c.args[0], [&](const auto& s, auto& items) {
        typename std::decay_t<decltype(items)>::value_type blank{};
        print(s, blank);
    });
    if (!ok) ok = withSingle(r, c.args[0], [&](const auto& s, auto& record) { print(s, record); });
    if (!ok) throw Error("'" + c.args[0] + "' is not a form type or section.\n" + recordNames());
    return 0;
}

int cmdSummary(Context& c) {
    const TaxReturn r = loadReturn(c);
    const Result result = calculate(r);
    c.out << renderSummary(r, result);
    return 0;
}

int cmdForms(Context& c) {
    const Result result = calculate(loadReturn(c));
    for (const auto& f : result.forms)
        c.out << "  " << f.id << std::string(f.id.size() < 34 ? 34 - f.id.size() : 1, ' ') << f.title
              << (f.worksheet ? " [worksheet]" : "") << "\n";
    return 0;
}

int cmdForm(Context& c) {
    if (c.args.empty()) throw Error("usage: opentax form NAME [--explain]   (see 'opentax forms')");
    const Result result = calculate(loadReturn(c));
    std::string query = join(c.args, " ");
    const FormResult* f = findForm(result, query);
    if (!f) throw Error("no form matching '" + query + "' on this return. See 'opentax forms'.");
    c.out << renderForm(*f, c.flag("explain"));
    return 0;
}

int cmdExplain(Context& c) {
    if (c.args.size() < 2) throw Error("usage: opentax explain FORM LINE   e.g. opentax explain 1040 16");
    const Result result = calculate(loadReturn(c));
    const std::string line = c.args.back();
    std::vector<std::string> formWords(c.args.begin(), c.args.end() - 1);
    const FormResult* f = findForm(result, join(formWords, " "));
    if (!f) throw Error("no form matching '" + join(formWords, " ") + "'");
    const Line* l = f->find(line);
    if (!l) throw Error(f->id + " has no line " + line + " on this return");
    c.out << f->id << ", line " << l->number << ": " << l->label << "\n  "
          << (l->text.empty() ? formatUsd(l->amount) : l->text) << "\n";
    if (!l->how.empty()) c.out << "  " << l->how << "\n";
    return 0;
}

int cmdCheck(Context& c) {
    const Result result = calculate(loadReturn(c));
    c.out << renderDiagnostics(result);
    return result.hasErrors() ? 1 : 0;
}

int cmdCsv(Context& c) {
    c.out << renderCsv(calculate(loadReturn(c)));
    return 0;
}

int cmdPdf(Context& c) {
    const TaxReturn r = loadReturn(c);
    const Result result = calculate(r);
    std::string out = c.option("out").value_or("");
    if (out.empty() && !c.args.empty()) out = c.args[0];
    if (out.empty()) out = returnPdfFileName(r);
    std::error_code ec;
    if (fs::exists(fs::u8path(out), ec) && !c.flag("force")) throw Error("'" + out + "' already exists (use --force)");
    ReturnPdfOptions options;
    options.explanations = !c.flag("no-explanations");
    options.worksheets = !c.flag("no-worksheets");
    const std::string bytes = returnPdf(r, result, options);
    std::ofstream file(fs::u8path(out), std::ios::binary | std::ios::trunc);
    if (!file) throw Error("cannot write '" + out + "'");
    file << bytes;
    if (!file) throw Error("failed while writing '" + out + "'");
    c.out << "Wrote " << out << "\n";
    if (result.hasErrors()) c.err << "warning: the return has errors; run 'opentax check'\n";
    return 0;
}

int cmdHelp(Context& c) {
    c.out << "OpenTax " << kVersion << " - free, open source federal income tax preparation (tax year 2025)\n\n"
          << "Usage: opentax [-f FILE] COMMAND [ARGS]\n\n"
          << "The return file defaults to $OPENTAX_FILE, then ./" << kDefaultFile << "\n\n"
          << "Entering your information\n"
          << "  new [status=mfj] [first=Jane last=Doe dob=1985-04-02]   start a return\n"
          << "  set SECTION key=value...        e.g. set spouse first=Sam dob=1984-09-30\n"
          << "  add FORM key=value...           e.g. add w2 employer=Acme wages=85000 fedwh=9100\n"
          << "  edit FORM N key=value...        change entry N\n"
          << "  remove FORM N                   delete entry N\n"
          << "  list [FORM|SECTION]             show what you've entered\n"
          << "  fields FORM|SECTION             the keys a form or section accepts\n\n"
          << "Results\n"
          << "  summary                         refund or amount owed, and the main figures (default)\n"
          << "  check                           errors, warnings and notes (exit 1 on errors)\n"
          << "  forms                           the forms, schedules and worksheets on this return\n"
          << "  form NAME [--explain]           one form line by line, e.g. form 1040, form sch 1\n"
          << "  explain FORM LINE               how a line was figured, e.g. explain 1040 16\n"
          << "  pdf [FILE] [--force] [--no-explanations] [--no-worksheets]\n"
          << "  csv                             every line of every form as CSV\n\n"
          << recordNames();
    return 0;
}

}  // namespace

int runCli(const std::vector<std::string>& argsIn, std::ostream& out, std::ostream& err) {
    std::string path;
    if (const char* env = std::getenv("OPENTAX_FILE"); env && *env) path = env;
    std::vector<std::string> positional;
    std::vector<std::string> flags;
    for (std::size_t i = 0; i < argsIn.size(); ++i) {
        const std::string& a = argsIn[i];
        if (a == "-f" || a == "--file") {
            if (i + 1 >= argsIn.size()) {
                err << "error: " << a << " needs a file name\n";
                return 2;
            }
            path = argsIn[++i];
        } else if (a == "--version" || a == "-v") {
            out << "opentax " << kVersion << "\n";
            return 0;
        } else if (a == "--help" || a == "-h") {
            positional.insert(positional.begin(), "help");
        } else if (startsWith(a, "--")) {
            flags.push_back(a.substr(2));
        } else {
            positional.push_back(a);
        }
    }
    if (path.empty()) path = kDefaultFile;
    const std::string command = positional.empty() ? "summary" : positional[0];
    Context c{path, out, err, std::vector<std::string>(positional.begin() + (positional.empty() ? 0 : 1), positional.end()), flags};

    using Handler = int (*)(Context&);
    const std::pair<const char*, Handler> commands[] = {
        {"new", cmdNew},       {"set", cmdSet},     {"add", cmdAdd},           {"edit", cmdEdit},
        {"remove", cmdRemove}, {"list", cmdList},   {"fields", cmdFields},     {"summary", cmdSummary},
        {"forms", cmdForms},   {"form", cmdForm},   {"explain", cmdExplain},   {"check", cmdCheck},
        {"csv", cmdCsv},       {"pdf", cmdPdf},     {"help", cmdHelp},
    };
    for (const auto& [name, handler] : commands) {
        if (command != name) continue;
        try {
            return handler(c);
        } catch (const std::exception& e) {
            err << "error: " << e.what() << "\n";
            return 1;
        }
    }
    err << "error: unknown command '" << command << "'. Try 'opentax help'.\n";
    return 2;
}

}  // namespace ot
