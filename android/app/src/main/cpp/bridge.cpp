// JNI bridge between the Android app (Kotlin) and the OpenTax engine.
//
// The Kotlin side holds an opaque handle to a native return. Everything crosses the bridge
// as text: the field schema, record values and calculation results as JSON, field values in
// the same text form the file format uses. Strings are converted between UTF-16 (Java) and
// UTF-8 (engine) here, rather than with JNI's "modified UTF-8" helpers, so passwords and names
// with any characters behave exactly as they do on the desktop.

#include "opentax/calc.hpp"
#include "opentax/crypto.hpp"
#include "opentax/model.hpp"
#include "opentax/report.hpp"
#include "opentax/return_pdf.hpp"

#include <jni.h>

#include <optional>
#include <string>
#include <type_traits>
#include <variant>

namespace {

using namespace ot;

struct Handle {
    TaxReturn ret;
    std::optional<PasswordKey> key;
};

Handle* handle(jlong h) { return reinterpret_cast<Handle*>(h); }

// ------------------------------------------------------------ strings

std::string utf8(JNIEnv* env, jstring s) {
    if (!s) return {};
    const jsize n = env->GetStringLength(s);
    const jchar* chars = env->GetStringChars(s, nullptr);
    std::string out;
    out.reserve(static_cast<std::size_t>(n));
    for (jsize i = 0; i < n; ++i) {
        std::uint32_t c = chars[i];
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < n && chars[i + 1] >= 0xDC00 && chars[i + 1] <= 0xDFFF) {
            c = 0x10000 + ((c - 0xD800) << 10) + (chars[i + 1] - 0xDC00);
            ++i;
        } else if (c >= 0xD800 && c <= 0xDFFF) {
            c = 0xFFFD;  // unpaired surrogate
        }
        if (c < 0x80) {
            out += static_cast<char>(c);
        } else if (c < 0x800) {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            out += static_cast<char>(0xE0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (c >> 18));
            out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    env->ReleaseStringChars(s, chars);
    return out;
}

jstring jstr(JNIEnv* env, const std::string& s) {
    std::u16string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        const auto b = static_cast<unsigned char>(s[i]);
        std::uint32_t c = 0xFFFD;
        std::size_t len = 1;
        if (b < 0x80) {
            c = b;
        } else if ((b >> 5) == 0x6 && i + 1 < s.size()) {
            c = ((b & 0x1F) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3F);
            len = 2;
        } else if ((b >> 4) == 0xE && i + 2 < s.size()) {
            c = ((b & 0x0F) << 12) | ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 6) |
                (static_cast<unsigned char>(s[i + 2]) & 0x3F);
            len = 3;
        } else if ((b >> 3) == 0x1E && i + 3 < s.size()) {
            c = ((b & 0x07) << 18) | ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 12) |
                ((static_cast<unsigned char>(s[i + 2]) & 0x3F) << 6) | (static_cast<unsigned char>(s[i + 3]) & 0x3F);
            len = 4;
        }
        if (c >= 0x10000) {
            c -= 0x10000;
            out += static_cast<char16_t>(0xD800 + (c >> 10));
            out += static_cast<char16_t>(0xDC00 + (c & 0x3FF));
        } else {
            out += static_cast<char16_t>(c);
        }
        i += len;
    }
    return env->NewString(reinterpret_cast<const jchar*>(out.data()), static_cast<jsize>(out.size()));
}

jbyteArray bytes(JNIEnv* env, const std::string& s) {
    jbyteArray a = env->NewByteArray(static_cast<jsize>(s.size()));
    env->SetByteArrayRegion(a, 0, static_cast<jsize>(s.size()), reinterpret_cast<const jbyte*>(s.data()));
    return a;
}

std::string fromBytes(JNIEnv* env, jbyteArray a) {
    const jsize n = env->GetArrayLength(a);
    std::string s(static_cast<std::size_t>(n), '\0');
    env->GetByteArrayRegion(a, 0, n, reinterpret_cast<jbyte*>(s.data()));
    return s;
}

// ------------------------------------------------------------ errors

void throwJava(JNIEnv* env, const char* cls, const std::string& message) {
    if (env->ExceptionCheck()) return;
    jclass c = env->FindClass(cls);
    if (c) env->ThrowNew(c, message.c_str());  // messages are ASCII
}

// Runs `f`, turning engine exceptions into Java exceptions.
template <class F>
auto guarded(JNIEnv* env, F&& f, decltype(f()) fallback) -> decltype(f()) {
    try {
        return f();
    } catch (const PasswordRequired& e) {
        throwJava(env, "org/opentax/app/engine/PasswordRequiredException", e.what());
    } catch (const WrongPassword& e) {
        throwJava(env, "org/opentax/app/engine/WrongPasswordException", e.what());
    } catch (const std::exception& e) {
        throwJava(env, "org/opentax/app/engine/OpenTaxException", e.what());
    }
    return fallback;
}

// ------------------------------------------------------------ JSON

std::string q(const std::string& s) {
    std::string out = "\"";
    for (char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        switch (ch) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    static const char* hex = "0123456789abcdef";
                    out += "\\u00";
                    out += hex[c >> 4];
                    out += hex[c & 15];
                } else {
                    out += ch;
                }
        }
    }
    return out + "\"";
}

template <class T>
std::string fieldsJson(const Schema<T>& s) {
    static const T blank{};
    std::string out = "[";
    bool first = true;
    for (const auto& f : s.fields) {
        std::string kind;
        std::string choiceList;
        std::visit(
            [&](auto member) {
                using V = std::decay_t<decltype(blank.*member)>;
                if constexpr (std::is_same_v<V, std::string>) kind = "text";
                else if constexpr (std::is_same_v<V, Money>) kind = "money";
                else if constexpr (std::is_same_v<V, std::optional<Date>>) kind = "date";
                else if constexpr (std::is_same_v<V, bool>) kind = "bool";
                else if constexpr (std::is_same_v<V, int>) kind = "int";
                else {
                    kind = "choice";
                    bool firstChoice = true;
                    for (const auto& ch : choices(V{})) {
                        choiceList += std::string(firstChoice ? "" : ",") + "{\"key\":" + q(ch.key) + ",\"label\":" + q(ch.label) + "}";
                        firstChoice = false;
                    }
                }
            },
            f.member);
        out += std::string(first ? "" : ",") + "{\"key\":" + q(f.key) + ",\"label\":" + q(f.label) + ",\"help\":" + q(f.help) +
               ",\"kind\":" + q(kind) + ",\"choices\":[" + choiceList + "]}";
        first = false;
    }
    return out + "]";
}

template <class T>
std::string recordJson(const T& record, const Schema<T>& s) {
    std::string out = "{";
    bool first = true;
    for (const auto& f : s.fields) {
        out += std::string(first ? "" : ",") + q(f.key) + ":" + q(getField(record, f));
        first = false;
    }
    return out + "}";
}

std::string money(Money m) { return q(m.str()); }

std::string resultJson(const TaxReturn& r) {
    const Result result = calculate(r);
    const Summary& s = result.summary;
    std::string out = "{\"summary\":{";
    const std::pair<const char*, Money> fields[] = {
        {"totalIncome", s.totalIncome},       {"agi", s.agi},
        {"deduction", s.deduction},           {"standardDeduction", s.standardDeduction},
        {"itemizedDeduction", s.itemizedDeduction}, {"nonItemizerCharity", s.nonItemizerCharity},
        {"qbiDeduction", s.qbiDeduction},     {"schedule1A", s.schedule1A},
        {"seniorDeduction", s.seniorDeduction}, {"taxableIncome", s.taxableIncome},
        {"incomeTax", s.incomeTax},           {"amt", s.amt},
        {"credits", s.credits},               {"otherTaxes", s.otherTaxes},
        {"totalTax", s.totalTax},             {"withholding", s.withholding},
        {"estimatedPayments", s.estimatedPayments}, {"adjustments", s.adjustments},
        {"refundableCredits", s.refundableCredits}, {"totalPayments", s.totalPayments},
        {"overpaid", s.overpaid},             {"refund", s.refund},
        {"owed", s.owed},
    };
    for (const auto& [key, value] : fields) out += q(key) + ":" + money(value) + ",";
    out += "\"itemized\":" + std::string(s.itemized ? "true" : "false") + "}";
    out += ",\"marginal\":" + q(isSupportedYear(r.info.year) ? marginalRate(r).str() : "0");
    const LineIds& ids = lineIds(r.info.year);
    out += ",\"lineIds\":{\"charity\":" + q(ids.charity) + ",\"qbi\":" + q(ids.qbi) + ",\"schedule1A\":" + q(ids.schedule1A) +
           ",\"totalTax\":" + q(ids.totalTax) + ",\"refundable\":" + q(ids.refundable) + "}";
    out += ",\"forms\":[";
    for (std::size_t i = 0; i < result.forms.size(); ++i) {
        const FormResult& f = result.forms[i];
        out += std::string(i ? "," : "") + "{\"id\":" + q(f.id) + ",\"title\":" + q(f.title) +
               ",\"worksheet\":" + (f.worksheet ? "true" : "false") + ",\"lines\":[";
        for (std::size_t j = 0; j < f.lines.size(); ++j) {
            const Line& l = f.lines[j];
            out += std::string(j ? "," : "") + "{\"n\":" + q(l.number) + ",\"label\":" + q(l.label) + ",\"amount\":" + money(l.amount) +
                   ",\"text\":" + q(l.text) + ",\"how\":" + q(l.how) + "}";
        }
        out += "]}";
    }
    out += "],\"diagnostics\":[";
    for (std::size_t i = 0; i < result.diagnostics.size(); ++i) {
        const Diagnostic& d = result.diagnostics[i];
        const char* sev = d.severity == Severity::Error ? "error" : d.severity == Severity::Warning ? "warning" : "info";
        out += std::string(i ? "," : "") + "{\"severity\":\"" + sev + "\",\"topic\":" + q(d.topic) + ",\"message\":" + q(d.message) + "}";
    }
    return out + "]}";
}

// Calls f(schema, record) for the record `section`/`index` of the return; false if none.
template <class F>
bool withRecord(TaxReturn& r, const std::string& section, int index, F&& f) {
    bool found = false;
    forEachSingle(r, [&](const char*, const char* command, const auto& s, auto& record) {
        if (!found && section == command) {
            found = true;
            f(s, record);
        }
    });
    forEachList(r, [&](const auto& s, auto& items) {
        if (!found && section == s.command) {
            found = true;
            if (index < 0 || static_cast<std::size_t>(index) >= items.size()) throw Error("no " + section + " entry " + std::to_string(index + 1));
            f(s, items[static_cast<std::size_t>(index)]);
        }
    });
    return found;
}

template <class F>
bool withList(TaxReturn& r, const std::string& section, F&& f) {
    bool found = false;
    forEachList(r, [&](const auto& s, auto& items) {
        if (!found && section == s.command) {
            found = true;
            f(items);
        }
    });
    return found;
}

}  // namespace

#define OT_FN(ret, name) extern "C" JNIEXPORT ret JNICALL Java_org_opentax_app_engine_Native_##name

OT_FN(jstring, schema)(JNIEnv* env, jobject) {
    TaxReturn r;
    std::string singles, lists;
    forEachSingle(r, [&](const char*, const char* command, const auto& s, auto&) {
        singles += std::string(singles.empty() ? "" : ",") + "{\"section\":" + q(command) + ",\"title\":" + q(s.title) +
                   ",\"fields\":" + fieldsJson(s) + "}";
    });
    forEachList(r, [&](const auto& s, auto&) {
        lists += std::string(lists.empty() ? "" : ",") + "{\"section\":" + q(s.command) + ",\"title\":" + q(s.title) +
                 ",\"fields\":" + fieldsJson(s) + "}";
    });
    return jstr(env, "{\"singles\":[" + singles + "],\"lists\":[" + lists + "]}");
}

OT_FN(jlong, create)(JNIEnv* env, jobject, jint year, jstring first, jstring last, jstring status) {
    return guarded(env, [&]() -> jlong {
        if (!isSupportedYear(year)) throw Error("tax year " + std::to_string(year) + " is not supported");
        auto* h = new Handle;
        h->ret.info.year = year;
        h->ret.taxpayer.first = utf8(env, first);
        h->ret.taxpayer.last = utf8(env, last);
        try {
            setField(h->ret.info, *findField<ReturnInfo>("status"), utf8(env, status));
        } catch (...) {
            delete h;
            throw;
        }
        return reinterpret_cast<jlong>(h);
    }, 0);
}

OT_FN(void, free)(JNIEnv*, jobject, jlong h) { delete handle(h); }

OT_FN(jboolean, isEncryptedFile)(JNIEnv* env, jobject, jstring path) {
    return guarded(env, [&]() -> jboolean { return TaxReturn::isEncryptedFile(utf8(env, path)) ? JNI_TRUE : JNI_FALSE; }, JNI_FALSE);
}

OT_FN(jboolean, isEncryptedData)(JNIEnv* env, jobject, jbyteArray data) {
    return isEncryptedText(fromBytes(env, data)) ? JNI_TRUE : JNI_FALSE;
}

OT_FN(jlong, load)(JNIEnv* env, jobject, jstring path, jstring password) {
    return guarded(env, [&]() -> jlong {
        std::string pw = utf8(env, password);
        PasswordKey key;
        TaxReturn r = TaxReturn::load(utf8(env, path), pw, &key);
        const bool encrypted = TaxReturn::isEncryptedFile(utf8(env, path));
        wipeString(pw);
        auto* h = new Handle{std::move(r), std::nullopt};
        if (encrypted) h->key = key;
        return reinterpret_cast<jlong>(h);
    }, 0);
}

// Reads a return from file contents (used when importing through the system file picker).
OT_FN(jlong, loadData)(JNIEnv* env, jobject, jbyteArray data, jstring password) {
    return guarded(env, [&]() -> jlong {
        std::string text = fromBytes(env, data);
        auto* h = new Handle;
        if (isEncryptedText(text)) {
            std::string pw = utf8(env, password);
            if (pw.empty()) {
                delete h;
                throw PasswordRequired();
            }
            PasswordKey key;
            std::string plain;
            try {
                plain = decryptText(text, pw, &key);
            } catch (...) {
                wipeString(pw);
                delete h;
                throw;
            }
            wipeString(pw);
            try {
                h->ret = TaxReturn::parse(plain);
            } catch (...) {
                wipeString(plain);
                delete h;
                throw;
            }
            wipeString(plain);
            h->key = key;
        } else {
            try {
                h->ret = TaxReturn::parse(text);
            } catch (...) {
                delete h;
                throw;
            }
        }
        return reinterpret_cast<jlong>(h);
    }, 0);
}

OT_FN(void, save)(JNIEnv* env, jobject, jlong h, jstring path) {
    guarded(env, [&]() -> int {
        handle(h)->ret.save(utf8(env, path), handle(h)->key ? &*handle(h)->key : nullptr);
        return 0;
    }, 0);
}

// The file contents to export: encrypted when the return has a password.
OT_FN(jbyteArray, exportData)(JNIEnv* env, jobject, jlong h) {
    return guarded(env, [&]() -> jbyteArray {
        Handle* p = handle(h);
        std::string text = p->ret.serialize();
        const std::string out = p->key ? encryptText(text, *p->key) : text;
        wipeString(text);
        return bytes(env, out);
    }, nullptr);
}

OT_FN(jstring, passwordProblem)(JNIEnv* env, jobject, jstring password) {
    std::string pw = utf8(env, password);
    const std::string problem = ot::passwordProblem(pw);
    wipeString(pw);
    return problem.empty() ? nullptr : jstr(env, problem);
}

// Sets (or, with null, removes) the password. Derives the key, which takes a moment.
OT_FN(void, setPassword)(JNIEnv* env, jobject, jlong h, jstring password) {
    guarded(env, [&]() -> int {
        if (!password) {
            handle(h)->key.reset();
            return 0;
        }
        std::string pw = utf8(env, password);
        try {
            handle(h)->key = PasswordKey::fromNewPassword(pw);
        } catch (...) {
            wipeString(pw);
            throw;
        }
        wipeString(pw);
        return 0;
    }, 0);
}

OT_FN(jboolean, hasPassword)(JNIEnv*, jobject, jlong h) { return handle(h)->key ? JNI_TRUE : JNI_FALSE; }

OT_FN(jstring, get)(JNIEnv* env, jobject, jlong h, jstring section, jint index) {
    return guarded(env, [&]() -> jstring {
        std::string json;
        const std::string sec = utf8(env, section);
        if (!withRecord(handle(h)->ret, sec, index, [&](const auto& s, auto& record) { json = recordJson(record, s); }))
            throw Error("unknown section '" + sec + "'");
        return jstr(env, json);
    }, nullptr);
}

// Sets one field from text. Returns null on success, or a message saying why the text isn't
// valid (the value is then left unchanged).
OT_FN(jstring, set)(JNIEnv* env, jobject, jlong h, jstring section, jint index, jstring key, jstring value) {
    const std::string sec = utf8(env, section);
    const std::string k = utf8(env, key);
    const std::string v = utf8(env, value);
    std::string problem;
    try {
        const bool found = withRecord(handle(h)->ret, sec, index, [&](const auto& s, auto& record) {
            using T = std::decay_t<decltype(record)>;
            const Field<T>* f = findField<T>(k);
            if (!f) throw Error("unknown field '" + k + "' in " + s.command);
            setField(record, *f, v);
        });
        if (!found) problem = "unknown section '" + sec + "'";
    } catch (const std::exception& e) {
        problem = e.what();
    }
    return problem.empty() ? nullptr : jstr(env, problem);
}

OT_FN(jint, count)(JNIEnv* env, jobject, jlong h, jstring section) {
    int n = 0;
    withList(handle(h)->ret, utf8(env, section), [&](auto& items) { n = static_cast<int>(items.size()); });
    return n;
}

OT_FN(jint, add)(JNIEnv* env, jobject, jlong h, jstring section) {
    int index = -1;
    withList(handle(h)->ret, utf8(env, section), [&](auto& items) {
        items.emplace_back();
        index = static_cast<int>(items.size()) - 1;
    });
    return index;
}

OT_FN(void, remove)(JNIEnv* env, jobject, jlong h, jstring section, jint index) {
    withList(handle(h)->ret, utf8(env, section), [&](auto& items) {
        if (index >= 0 && static_cast<std::size_t>(index) < items.size()) items.erase(items.begin() + index);
    });
}

OT_FN(jstring, calculate)(JNIEnv* env, jobject, jlong h) {
    return guarded(env, [&]() -> jstring { return jstr(env, resultJson(handle(h)->ret)); }, nullptr);
}

OT_FN(jstring, displayName)(JNIEnv* env, jobject, jlong h) { return jstr(env, handle(h)->ret.displayName()); }

OT_FN(jbyteArray, pdf)(JNIEnv* env, jobject, jlong h) {
    return guarded(env, [&]() -> jbyteArray {
        const TaxReturn& r = handle(h)->ret;
        return bytes(env, returnPdf(r, calculate(r)));
    }, nullptr);
}

OT_FN(jstring, pdfFileName)(JNIEnv* env, jobject, jlong h) { return jstr(env, returnPdfFileName(handle(h)->ret)); }

OT_FN(jstring, csv)(JNIEnv* env, jobject, jlong h) {
    return guarded(env, [&]() -> jstring { return jstr(env, renderCsv(calculate(handle(h)->ret))); }, nullptr);
}
