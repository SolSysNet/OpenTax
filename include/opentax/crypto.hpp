#pragma once

// Password-based encryption for return files.
//
// A password is stretched into a 256-bit key with Argon2id (memory-hard, so guessing is
// expensive), and the return is encrypted with XChaCha20-Poly1305, which also authenticates
// it: a wrong password, a damaged file and any tampering (including with the header) are all
// detected instead of producing garbage. Both algorithms come from Monocypher
// (third_party/monocypher), a small audited library; nothing here implements cryptography.
//
// An encrypted file is still text, so it is recognizable and survives copy/paste:
//
//   OPENTAX-ENCRYPTED 1
//   kdf=argon2id memory=262144 passes=3 lanes=1 salt=<32 hex digits>
//   cipher=xchacha20-poly1305 nonce=<48 hex digits>
//   <base64 of ciphertext and 16-byte tag, 76 characters per line>
//
// The three header lines are authenticated as associated data.

#include "opentax/model.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace ot {

// Thrown when a file is encrypted and no password was given.
struct PasswordRequired : Error {
    PasswordRequired() : Error("this return is encrypted; a password is required") {}
};

// Thrown when decryption fails: a wrong password, or a damaged or altered file.
struct WrongPassword : Error {
    WrongPassword() : Error("wrong password, or the file is damaged") {}
};

struct KdfParams {
    std::uint32_t memoryKiB = 262144;  // 256 MiB
    std::uint32_t passes = 3;
    std::uint32_t lanes = 1;
};

// A key derived from a password, with the salt and parameters that produced it. Keeping the
// key (not the password) lets a program save many times without re-deriving it. The key is
// wiped when the object is destroyed.
class PasswordKey {
public:
    PasswordKey() = default;
    PasswordKey(const PasswordKey&) = default;
    PasswordKey& operator=(const PasswordKey&) = default;
    ~PasswordKey();

    // Derives a key with a fresh random salt. Throws ot::Error if the password is too weak
    // (see passwordProblem).
    static PasswordKey fromNewPassword(std::string_view password, KdfParams params = {});
    // Derives the key for an existing salt (used when opening a file).
    static PasswordKey derive(std::string_view password, const std::array<std::uint8_t, 16>& salt, KdfParams params);

    const std::array<std::uint8_t, 32>& key() const { return key_; }
    const std::array<std::uint8_t, 16>& salt() const { return salt_; }
    const KdfParams& params() const { return params_; }

private:
    std::array<std::uint8_t, 32> key_{};
    std::array<std::uint8_t, 16> salt_{};
    KdfParams params_;
};

// Why a new password isn't acceptable, or empty if it is. (At least 8 characters.)
std::string passwordProblem(std::string_view password);

bool isEncryptedText(std::string_view text);

// Encrypts `plaintext` into the file format above, with a fresh random nonce.
std::string encryptText(std::string_view plaintext, const PasswordKey& key);

// Decrypts a file. Throws WrongPassword when the password is wrong or the file was altered,
// and ot::Error when the file isn't in the expected format. On success, `keyOut` (if given)
// receives the key, so the file can be saved again with the same password.
std::string decryptText(std::string_view fileText, std::string_view password, PasswordKey* keyOut = nullptr);

// Fills `out` with bytes from the operating system's secure random number generator.
void secureRandom(std::uint8_t* out, std::size_t size);

// Overwrites a string's contents (for passwords) before clearing it.
void wipeString(std::string& s);

}  // namespace ot
