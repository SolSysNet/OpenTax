#include "opentax/cli.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

namespace {

std::string narrow(const wchar_t* s, int length) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, s, length, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n > 0 ? n : 0), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, s, length, out.data(), n, nullptr, nullptr);
    return out;
}

// Windows hands narrow argv over in the ANSI code page; OpenTax works in UTF-8.
std::vector<std::string> utf8Arguments() {
    std::vector<std::string> args;
    int count = 0;
    wchar_t** wide = CommandLineToArgvW(GetCommandLineW(), &count);
    for (int i = 1; wide && i < count; ++i) args.push_back(narrow(wide[i], -1).c_str());
    LocalFree(wide);
    return args;
}

// Reads a line from the console without echoing it. ReadConsoleW returns UTF-16, so any
// characters in the password arrive as the same UTF-8 the desktop app uses.
std::string readHidden() {
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    if (!GetConsoleMode(in, &mode)) {  // not a console (redirected input): read a plain line
        std::string line;
        std::getline(std::cin, line);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        return line;
    }
    SetConsoleMode(in, (mode & ~static_cast<DWORD>(ENABLE_ECHO_INPUT)) | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
    std::wstring buffer(1024, L'\0');
    DWORD read = 0;
    const BOOL ok = ReadConsoleW(in, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr);
    SetConsoleMode(in, mode);
    std::string line = ok ? narrow(buffer.data(), static_cast<int>(read)) : std::string();
    SecureZeroMemory(buffer.data(), buffer.size() * sizeof(wchar_t));
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
    return line;
}

}  // namespace

#else
#include <termios.h>
#include <unistd.h>

namespace {

std::string readHidden() {
    termios old{};
    const bool tty = tcgetattr(STDIN_FILENO, &old) == 0;
    if (tty) {
        termios quiet = old;
        quiet.c_lflag &= ~static_cast<tcflag_t>(ECHO);
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &quiet);
    }
    std::string line;
    std::getline(std::cin, line);
    if (tty) tcsetattr(STDIN_FILENO, TCSAFLUSH, &old);
    return line;
}

}  // namespace
#endif

namespace {

// OPENTAX_PASSWORD (for scripts) or a hidden prompt on the terminal.
std::string askPassword(const std::string& prompt) {
    if (const char* env = std::getenv("OPENTAX_PASSWORD"); env && *env) return env;
    std::cerr << prompt << std::flush;
    std::string pw = readHidden();
    std::cerr << "\n";
    return pw;
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    (void)argc;
    (void)argv;
    SetConsoleOutputCP(CP_UTF8);
    const std::vector<std::string> args = utf8Arguments();
#else
    const std::vector<std::string> args(argv + 1, argv + argc);
#endif
    return ot::runCli(args, std::cout, std::cerr, askPassword);
}
