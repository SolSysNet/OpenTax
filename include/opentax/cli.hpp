#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace ot {

constexpr const char* kVersion = "0.1.0";

// Runs one OpenTax command line (argv without the program name). Returns the exit code.
int runCli(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);

}  // namespace ot
