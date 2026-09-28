#include "opentax/crypto.hpp"

#include "monocypher.h"
#include "opentax/util.hpp"

#include <cstring>
#include <memory>
#include <new>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#elif defined(__APPLE__) || defined(__OpenBSD__)
#include <sys/random.h>
#include <unistd.h>
#else
#include <cerrno>
#include <sys/random.h>
#endif

namespace ot {
namespace {

constexpr const char* kMagic = "OPENTAX-ENCRYPTED 1";
constexpr std::size_t kMacSize = 16;
constexpr std::size_t kNonceSize = 24;

// Limits on parameters read from a file: enough headroom for stronger settings later, while
// refusing files that would demand absurd memory or time to open.
constexpr std::uint32_t kMinMemoryKiB = 8 * 1024;        // 8 MiB
constexpr std::uint32_t kMaxMemoryKiB = 1024 * 1024;     // 1 GiB
constexpr std::uint32_t kMaxPasses = 10;
constexpr std::uint32_t kMaxLanes = 8;

std::string hex(const std::uint8_t* data, std::size_t size) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    for (std::size_t i = 0; i < size; ++i) {
        out += digits[data[i] >> 4];
        out += digits[data[i] & 15];
    }
    return out;
}

bool unhex(std::string_view text, std::uint8_t* out, std::size_t size) {
    if (text.size() != size * 2) return false;
    auto value = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < size; ++i) {
        const int hi = value(text[2 * i]);
        const int lo = value(text[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = static_cast<std::uint8_t>(hi * 16 + lo);
    }
    return true;
}

constexpr const char* kBase64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64(const std::vector<std::uint8_t>& data) {
    std::string out;
    std::size_t i = 0;
    for (; i + 2 < data.size(); i += 3) {
        const std::uint32_t v = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
        out += kBase64[v >> 18];
        out += kBase64[(v >> 12) & 63];
        out += kBase64[(v >> 6) & 63];
        out += kBase64[v & 63];
    }
    if (i < data.size()) {
        const std::uint32_t v = (data[i] << 16) | (i + 1 < data.size() ? data[i + 1] << 8 : 0);
        out += kBase64[v >> 18];
        out += kBase64[(v >> 12) & 63];
        out += i + 1 < data.size() ? kBase64[(v >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

bool unbase64(std::string_view text, std::vector<std::uint8_t>& out) {
    std::string clean;
    for (char c : text) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        clean += c;
    }
    if (clean.size() % 4 != 0) return false;
    out.clear();
    for (std::size_t i = 0; i < clean.size(); i += 4) {
        std::uint32_t v = 0;
        int pad = 0;
        for (std::size_t j = 0; j < 4; ++j) {
            const char c = clean[i + j];
            int d;
            if (c == '=') {
                if (i + 4 != clean.size() || j < 2) return false;  // padding only at the very end
                d = 0;
                ++pad;
            } else {
                if (pad) return false;
                const char* p = std::strchr(kBase64, c);
                if (!p || !*p) return false;
                d = static_cast<int>(p - kBase64);
            }
            v = (v << 6) | static_cast<std::uint32_t>(d);
        }
        out.push_back(static_cast<std::uint8_t>(v >> 16));
        if (pad < 2) out.push_back(static_cast<std::uint8_t>((v >> 8) & 255));
        if (pad < 1) out.push_back(static_cast<std::uint8_t>(v & 255));
    }
    return true;
}

// The canonical header, rebuilt from its values. It is the associated data, so the key
// derivation settings and the nonce are authenticated along with the contents.
std::string header(const KdfParams& p, const std::array<std::uint8_t, 16>& salt, const std::uint8_t* nonce) {
    return std::string(kMagic) + "\n" + "kdf=argon2id memory=" + std::to_string(p.memoryKiB) +
           " passes=" + std::to_string(p.passes) + " lanes=" + std::to_string(p.lanes) + " salt=" +
           hex(salt.data(), salt.size()) + "\n" + "cipher=xchacha20-poly1305 nonce=" + hex(nonce, kNonceSize) + "\n";
}

// Parses "key=value" fields from one header line, in the given order.
std::vector<std::string> fields(std::string_view line, std::initializer_list<const char*> keys) {
    const auto parts = split(trim(line), ' ');
    if (parts.size() != keys.size()) throw Error("encrypted file: unexpected header");
    std::vector<std::string> values;
    std::size_t i = 0;
    for (const char* key : keys) {
        const std::string prefix = std::string(key) + "=";
        if (!startsWith(parts[i], prefix)) throw Error("encrypted file: unexpected header");
        values.push_back(parts[i].substr(prefix.size()));
        ++i;
    }
    return values;
}

std::uint32_t parseBounded(const std::string& text, std::uint32_t min, std::uint32_t max) {
    const auto v = parseInt(text);
    if (!v || *v < static_cast<long long>(min) || *v > static_cast<long long>(max))
        throw Error("encrypted file: unsupported key derivation settings");
    return static_cast<std::uint32_t>(*v);
}

}  // namespace

// ------------------------------------------------------------------ keys

PasswordKey::~PasswordKey() { crypto_wipe(key_.data(), key_.size()); }

std::string passwordProblem(std::string_view password) {
    if (displayWidth(password) < 8) return "Use at least 8 characters. A few unrelated words make a strong, memorable password.";
    return {};
}

PasswordKey PasswordKey::fromNewPassword(std::string_view password, KdfParams params) {
    const std::string problem = passwordProblem(password);
    if (!problem.empty()) throw Error(problem);
    std::array<std::uint8_t, 16> salt{};
    secureRandom(salt.data(), salt.size());
    return derive(password, salt, params);
}

PasswordKey PasswordKey::derive(std::string_view password, const std::array<std::uint8_t, 16>& salt, KdfParams params) {
    if (params.memoryKiB < kMinMemoryKiB || params.memoryKiB > kMaxMemoryKiB || params.passes < 1 ||
        params.passes > kMaxPasses || params.lanes < 1 || params.lanes > kMaxLanes)
        throw Error("encrypted file: unsupported key derivation settings");
    const std::size_t workSize = static_cast<std::size_t>(params.memoryKiB) * 1024;
    std::unique_ptr<std::uint8_t[]> work(new (std::nothrow) std::uint8_t[workSize]);
    if (!work) throw Error("not enough memory to derive the encryption key");

    PasswordKey k;
    k.salt_ = salt;
    k.params_ = params;
    crypto_argon2_config config{CRYPTO_ARGON2_ID, params.memoryKiB, params.passes, params.lanes};
    crypto_argon2_inputs inputs{reinterpret_cast<const std::uint8_t*>(password.data()), salt.data(),
                                static_cast<std::uint32_t>(password.size()), static_cast<std::uint32_t>(salt.size())};
    crypto_argon2(k.key_.data(), static_cast<std::uint32_t>(k.key_.size()), work.get(), config, inputs, crypto_argon2_no_extras);
    crypto_wipe(work.get(), workSize);
    return k;
}

// -------------------------------------------------------------- encrypt

bool isEncryptedText(std::string_view text) {
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF) text.remove_prefix(3);  // BOM
    return startsWith(text, kMagic);
}

std::string encryptText(std::string_view plaintext, const PasswordKey& key) {
    std::uint8_t nonce[kNonceSize];
    secureRandom(nonce, sizeof nonce);
    const std::string head = header(key.params(), key.salt(), nonce);
    std::vector<std::uint8_t> sealed(plaintext.size() + kMacSize);
    crypto_aead_lock(sealed.data(), sealed.data() + plaintext.size(), key.key().data(), nonce,
                     reinterpret_cast<const std::uint8_t*>(head.data()), head.size(),
                     reinterpret_cast<const std::uint8_t*>(plaintext.data()), plaintext.size());
    const std::string body = base64(sealed);
    std::string out = head;
    for (std::size_t i = 0; i < body.size(); i += 76) out += body.substr(i, 76) + "\n";
    return out;
}

std::string decryptText(std::string_view fileText, std::string_view password, PasswordKey* keyOut) {
    if (!isEncryptedText(fileText)) throw Error("not an encrypted OpenTax file");
    if (static_cast<unsigned char>(fileText[0]) == 0xEF) fileText.remove_prefix(3);
    // Split off the three header lines.
    std::string_view rest = fileText;
    std::string_view lines[3];
    for (auto& line : lines) {
        const auto nl = rest.find('\n');
        if (nl == std::string_view::npos) throw Error("encrypted file: truncated header");
        line = rest.substr(0, nl);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        rest.remove_prefix(nl + 1);
    }
    if (lines[0] != kMagic) throw Error("encrypted file: unknown format version");
    const auto kdf = fields(lines[1], {"kdf", "memory", "passes", "lanes", "salt"});
    if (kdf[0] != "argon2id") throw Error("encrypted file: unsupported key derivation method '" + kdf[0] + "'");
    KdfParams params;
    params.memoryKiB = parseBounded(kdf[1], kMinMemoryKiB, kMaxMemoryKiB);
    params.passes = parseBounded(kdf[2], 1, kMaxPasses);
    params.lanes = parseBounded(kdf[3], 1, kMaxLanes);
    std::array<std::uint8_t, 16> salt{};
    if (!unhex(kdf[4], salt.data(), salt.size())) throw Error("encrypted file: bad salt");
    const auto cipher = fields(lines[2], {"cipher", "nonce"});
    if (cipher[0] != "xchacha20-poly1305") throw Error("encrypted file: unsupported cipher '" + cipher[0] + "'");
    std::uint8_t nonce[kNonceSize];
    if (!unhex(cipher[1], nonce, sizeof nonce)) throw Error("encrypted file: bad nonce");
    std::vector<std::uint8_t> sealed;
    if (!unbase64(rest, sealed) || sealed.size() < kMacSize) throw Error("encrypted file: damaged contents");

    const PasswordKey key = PasswordKey::derive(password, salt, params);
    const std::string head = header(params, salt, nonce);
    const std::size_t size = sealed.size() - kMacSize;
    std::string plain(size, '\0');
    const int failed = crypto_aead_unlock(reinterpret_cast<std::uint8_t*>(plain.data()), sealed.data() + size, key.key().data(), nonce,
                                          reinterpret_cast<const std::uint8_t*>(head.data()), head.size(), sealed.data(), size);
    if (failed) {
        wipeString(plain);
        throw WrongPassword();
    }
    if (keyOut) *keyOut = key;
    return plain;
}

// --------------------------------------------------------------- random

void secureRandom(std::uint8_t* out, std::size_t size) {
#if defined(_WIN32)
    if (!BCRYPT_SUCCESS(BCryptGenRandom(nullptr, out, static_cast<ULONG>(size), BCRYPT_USE_SYSTEM_PREFERRED_RNG)))
        throw Error("the system random number generator failed");
#elif defined(__APPLE__) || defined(__OpenBSD__)
    while (size > 0) {
        const std::size_t chunk = size < 256 ? size : 256;  // getentropy's limit
        if (getentropy(out, chunk) != 0) throw Error("the system random number generator failed");
        out += chunk;
        size -= chunk;
    }
#else
    while (size > 0) {
        const ssize_t n = getrandom(out, size, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            throw Error("the system random number generator failed");
        }
        out += n;
        size -= static_cast<std::size_t>(n);
    }
#endif
}

void wipeString(std::string& s) {
    if (!s.empty()) crypto_wipe(s.data(), s.size());
    s.clear();
}

}  // namespace ot
