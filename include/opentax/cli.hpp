#pragma once

#include <functional>
#include <iosfwd>
#include <string>
#include <vector>

namespace ot {

constexpr const char* kVersion = "0.1.0";

// Asks the user for a password, showing `prompt`. Returns what was typed (empty if nothing).
using PasswordPrompt = std::function<std::string(const std::string& prompt)>;

// Runs one OpenTax command line (argv without the program name). Returns the exit code.
// Without a prompt, encrypted returns use the OPENTAX_PASSWORD environment variable.
int runCli(const std::vector<std::string>& args, std::ostream& out, std::ostream& err, PasswordPrompt prompt = {});

}  // namespace ot
