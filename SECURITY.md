# Security

A tax return is some of the most sensitive data a person has. The design goal is simple: **nothing in
OpenTax talks to the network.** Your return stays in one local file that you control.

## Network posture

- **No network code.** Neither the engine, the command line nor the desktop app opens sockets, makes HTTP
  requests, checks for updates, or sends telemetry. None of this is planned without a separate, opt-in design
  review. (That's also why OpenTax doesn't e-file.)
- **No build-time downloads.** CMake never fetches anything (`FetchContent`, `ExternalProject` and package
  managers are not used). A build needs only a C++17 compiler, CMake and the files in this repository. On
  Linux/macOS the desktop app also needs the system GLFW package.
- **No URL launching.** Dear ImGui is compiled with `IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS`, which removes its
  built-in "open link" handler. OpenTax's only shell call opens a PDF it has just written to the temp folder
  (Windows "Preview PDF"). It refuses anything that is not an existing local file, so it can't open URLs.

## Less data to protect

- **No Social Security numbers.** OpenTax never asks for SSNs, bank account or routing numbers. It doesn't need
  them to compute your return; write them on the official forms when you file.
- The return file holds names, birth dates, an address and income figures. Treat it like a paper return.

## PDF output

The printable return is rendered by OpenTax's own small PDF writer (`src/pdf.cpp`). It has no third-party code
and no API for anything but text, lines and rectangles:

- Only the standard Helvetica fonts are referenced. No fonts are embedded or downloaded.
- No JavaScript, actions, links, forms, attachments or external references can be written. The test suite
  checks every generated PDF for these keys.
- All text (names, payers, descriptions...) is converted to WinAnsi and fully escaped, so data can't inject PDF
  operators. There is a regression test for exactly this.
- Output is deterministic: no timestamps or unique ids that leak when or where a file was made.
- Suggested file names are sanitized, so a name can't produce a path outside the chosen folder.

## Data at rest

- Returns are UTF-8 text files (`.otx`). Saves write a temporary file and rename it over the original,
  keeping the previous version as `.otx.bak`. Every load is validated; unknown records or fields are errors.
- `.gitignore` excludes `*.otx` so a personal return isn't committed to a fork by accident.

### Password protection

A return can be protected with a password: when starting it, from **File > Protect with Password** in the app,
or with `opentax encrypt`. It is off by default.

- **How it works.** The password is stretched into a 256-bit key with **Argon2id** (256 MiB of memory,
  3 passes, a random 16-byte salt), and the return is encrypted with **XChaCha20-Poly1305** under a fresh
  random 24-byte nonce on every save. Both come from Monocypher (see below); OpenTax implements no cryptography
  of its own. Random numbers come from the operating system (`BCryptGenRandom`, `getrandom`, `getentropy`).
- **Tampering is detected.** The authentication tag covers the contents and the header (the algorithm names,
  key derivation settings, salt and nonce). A wrong password, a damaged file and any modification are all
  refused; nothing is ever decrypted into garbage. Files whose key derivation settings are outside sane limits
  (under 8 MiB or over 1 GiB of memory, more than 10 passes) are rejected before any work is done.
- **The file format** is text: an `OPENTAX-ENCRYPTED 1` line, the key derivation and cipher settings, then the
  base64 ciphertext. The settings are stored in each file, so they can be strengthened later without breaking
  older files.
- **No recovery.** There is no back door and no reset. A forgotten password means the return can't be opened.
- **In memory.** The app and CLI keep the derived key, never the password, for as long as the return is open,
  so autosave doesn't have to ask again. OpenTax wipes its own copies of passwords and keys when they're no
  longer needed (the UI toolkit may briefly hold typed text in its own buffers). Decrypted data is in memory
  while you work, like any open document.

What password protection does **not** cover:

- **Exports.** PDFs and CSV files you save are not encrypted (OpenTax reminds you when you export).
- **Copies made before you set a password.** OpenTax deletes its own unencrypted `.bak` when a return is first
  encrypted, but it can't erase earlier copies: cloud sync or backup history, other copies you made, or the
  old data still physically present on the disk. For the strongest protection, set the password when you
  start a return, and keep returns on an encrypted disk (BitLocker, FileVault, LUKS) as well.
- **File names.** Encryption covers the contents, not the name (the default name includes yours).
- **`OPENTAX_PASSWORD`.** For scripts, the CLI reads the password from this environment variable. Other
  programs running as your user can read environment variables, so prefer the interactive prompt.

### App settings

The desktop app keeps UI preferences and a recent-files list in `%APPDATA%\OpenTax` (Windows),
`~/.config/opentax` (Linux) or `~/Library/Application Support/OpenTax` (macOS). This file holds no tax data,
but the recent list does record file names and folders. Turn off **View > Remember Recent Files** to stop
keeping it (which also clears it), or use **File > Open Recent > Clear Recent Files**. The settings file is not
encrypted: encrypting it would mean asking for a password every time the app starts, to protect only file
names.

## Third-party code

| Component | Version | Source | SHA-256 of release archive |
|---|---|---|---|
| Dear ImGui (MIT) | v1.92.9b | https://github.com/ocornut/imgui/archive/refs/tags/v1.92.9b.zip | `E1C46D676C2BCB7CED847BA27F50553E33A19DB97B3CADAEC7F8BE64449139F8` |
| Monocypher (BSD-2-Clause or CC0-1.0) | 4.0.3 | https://monocypher.org/download/monocypher-4.0.3.tar.gz | `8CC9BC341A66249016DB9BD70E9142D8D0AEF9945973744B1AC05DBC55D8EE66` |

Vendored files are copied unmodified:

- `third_party/imgui`: the core, `misc/cpp/imgui_stdlib`, and the Win32, DX11, GLFW and OpenGL3 backends.
- `third_party/monocypher`: `src/monocypher.c`, `src/monocypher.h` and `LICENCE.md`. The archive's SHA-512
  matched the checksum published at monocypher.org and the copy attached to the GitHub release, and the
  release's own test suite passed with the project's compiler. Cure53 audited Monocypher 3.1.1 in June 2020
  (see https://monocypher.org/quality-assurance/audit); version 4 came later and added the Argon2id variant
  OpenTax uses, which that audit did not cover.

To update, download the new release, verify its hash, replace the files and update this table in the same
commit.

## Reporting a vulnerability

Please report suspected vulnerabilities privately to the maintainers (for example through GitHub's private
vulnerability reporting on this repository) rather than in a public issue.
