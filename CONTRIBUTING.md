# Contributing to OpenTax

Thanks for helping. People file their taxes with OpenTax, and a wrong number costs them money or brings
IRS letters. So this project cares more about **correct**, **traceable** and **boring** than clever. These
guidelines explain what that means in practice.

## Ground rules

These five rules are not negotiable. A change that breaks one won't be merged, however useful it is.

1. **Every number traces to an IRS source.** Each constant cites where it comes from: a form line, the
   instructions, a revenue procedure or notice, or the statute. Each computed line follows the official
   form's line numbers and explains itself. If you can't cite it, it doesn't go in.
2. **Money is never floating point.** Use `ot::Money` (integer cents) and `ot::Decimal` (rates and
   ratios). Round only where the IRS form or worksheet says to, and in the way it says.
3. **Nothing talks to the network.** No sockets, HTTP, update checks or telemetry, and no build-time
   downloads (`FetchContent`, `ExternalProject`, package managers). See [SECURITY.md](SECURITY.md).
   Proposals that need the network (such as e-filing) start as a design discussion in an issue and must
   be opt-in.
4. **Collect as little personal data as possible.** OpenTax doesn't ask for Social Security numbers or
   bank account numbers, because it doesn't need them to compute a return. Don't add fields the
   calculation doesn't use.
5. **Don't guess.** When a return needs something OpenTax doesn't support, say so with an error
   diagnostic instead of computing a plausible-looking wrong answer. Where the IRS hasn't published
   final guidance yet, follow the statute and mark the result as provisional.

## Getting started

You need CMake 3.16+ and a C++17 compiler (GCC 9+, Clang 10+ or MSVC 2019+).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

- The desktop app builds by default. On Linux, install GLFW (`libglfw3-dev`); on macOS, use
  `brew install glfw`. Use `-DOPENTAX_BUILD_GUI=OFF` to build only the engine and CLI.
- Do GUI work in a **Debug** build. Dear ImGui's assertions catch real layout bugs there that a Release
  build silently hides.
- **Never test with your real return.** Use [examples/rivera-family.otx](examples/rivera-family.otx),
  or create a throwaway file with `opentax -f scratch.otx new first=Test`.

See the README's *Project layout* section for a map of the code.

### The Android app

Open `android/` in Android Studio, or run `./gradlew assembleDebug` there. You need the NDK and CMake from
the SDK Manager (the versions are set in `android/app/build.gradle.kts`). The app compiles the engine from
the repository root, so engine changes need no Android-specific work unless they add a record type or field
the UI should show specially: forms are generated from the engine's schema through the JNI bridge
(`android/app/src/main/cpp/bridge.cpp`).

- Keep the app free of permissions. Adding one, especially INTERNET, needs an issue first.
- Android dependencies are pinned by SHA-256 in `android/gradle/verification-metadata.xml`. When you add or
  update one, regenerate the file with
  `./gradlew --write-verification-metadata sha256 assembleDebug assembleRelease lintDebug`, and review the diff
  so that only the artifacts you meant to change are added.
- Run `./gradlew lintDebug` before sending a change; it should report no issues.

## Making a change

1. **Open an issue first** for anything larger than a small fix, so we can agree on the approach before
   you write it. New forms, new tax years, file format changes, dependencies and anything
   security-related always need an issue.
2. **Branch from `main`**, named `feature/<topic>` or `fix/<topic>`.
3. **Keep it focused.** Make one logical change per pull request, split into commits that each build
   and pass the tests.
4. **Add tests** (see [Testing](#testing)). When behavior they describe changes, also update the
   README (including the *What's supported* and *Limits* sections), the CLI `help` text and SECURITY.md.
5. **Open a pull request** using the checklist below.

### Commit messages

- Write the subject line in the imperative, 72 characters or fewer, with no trailing period ("Add
  Schedule E", "Fix SALT cap for married filing separately").
- Use the body to explain **why**, and what a reviewer can't see from the diff: the IRS source for a
  rule, the root cause of a bug, the alternatives you rejected.

## Engine guidelines (`src/`, `include/opentax/`)

### Follow the form

- **One function per form or worksheet, in IRS order.** Each builds its lines with
  `Builder::add(number, label, amount, how)`, using the official line numbers and a label close to the
  form's wording.
- **Every line explains itself.** Write the `how` text for someone who has never seen the form:
  "Smaller of line 9 or $10,000", "50% of $1,200 business meals". The desktop app, the PDF and
  `opentax explain` all show it.
- **Reproduce the worksheet, not a shortcut.** If the IRS worksheet rounds a ratio to three places or
  rounds up to the next $10, do the same, even when a simpler formula gives nearly the same answer.
  Tax preparation software is judged against the forms to the dollar.
- **`calculate()` is a pure function** of the `TaxReturn`. It never touches files, the clock or global
  state, so the app, the CLI and the tests always agree.

### Constants belong in the rule tables

- **Every tax-year figure lives in `src/rules_<year>.cpp`, never in `calc.cpp`.** If you need a new one,
  add a field to `Rules` in `include/opentax/rules.hpp` and set it in **every** year's table (zero or
  `false` for years where it doesn't apply).
- **Cite the source** in the table's header comment, or next to the value when it comes from somewhere
  unusual.
- **Handle year differences through the rules**, not with scattered `if (year == 2026)` checks. Use a
  rule value or flag (`rules_.mortgageInsurance`), `rules_.formsYear` for line numbering, and
  `lineIds(year)` for Form 1040 lines that moved.

### Diagnostics

- **`Severity::Error`** means the return is wrong or can't be computed as entered (for example head of
  household with no qualifying person, or a situation OpenTax doesn't support).
- **`Severity::Warning`** means "check this": a limit was applied, or an entry looks unusual.
- **`Severity::Info`** covers notes, including provisional results.
- **Write messages for taxpayers, not programmers.** Name the topic ("Form W-2 Acme", "Student loan
  interest"), say what's wrong, and say what to do about it.

### Adding fields and changing the file format

Each input field is declared once, in its record's schema in `src/model.cpp`: a key, a label as printed
on the source form ("Box 12  Elective deferrals") and help text. That one declaration drives the file
format, the CLI (`opentax fields w2`) and the desktop app's form editors.

Returns are plain text files (`OPENTAX 1` header, one `key=value` record per line). People keep them for
years, so:

- **Never rename or reuse a key.** Once a key has shipped, its meaning is fixed.
- **New fields need safe defaults.** Blank fields aren't written, so a new field must default to
  "doesn't apply" (zero, `false` or empty). Older files then load unchanged.
- **Unknown keys and records are errors on purpose.** An older OpenTax refuses a newer file it can't
  fully read, rather than silently dropping data. If a change would make an older release *misread* a
  file (not just refuse it), bump the header version and keep reading the old format.
- **Test both directions:** a serialize → parse → serialize round trip must produce identical text, and
  files written before your change must still load.

### Adding a tax year

1. Copy the latest `src/rules_<year>.cpp` and update every value from the new revenue procedure, the
   retirement-plan notice, the Social Security wage base and the draft forms. Note each source in the
   header comment.
2. Register the year in `src/rules.cpp` (`rulesFor` and `isSupportedYear`) and add the file to
   `CMakeLists.txt`.
3. Compare the draft Form 1040 and schedules against the previous year and handle any renumbering
   (`formsYear`, `lineIds`). Read the forms line by line: moved lines are easy to miss.
4. Add the year to the tax-year choices in `gui/app.cpp` and `gui/app_screens.cpp`.
5. Add tests (see below). At a minimum, the rate schedules must reproduce the tax amounts printed in the
   revenue procedure at every bracket boundary.
6. Until the IRS publishes the final instructions and tables, mark anything that depends on unpublished
   worksheets as provisional. Replace it once they're out, and add the new Tax Table as a fixture in
   `tests/data/`.

### Adding a form or schedule

- Add input records or fields to the schema only if the form needs new information.
- Compute it in `calc.cpp` in the correct place in the flow, give it an `Order` so it sorts correctly, and
  only emit it when it applies.
- Update the README's *What's supported* and *Limits* sections, and remove any diagnostic that said the
  form was unsupported.
- The PDF and the forms browser pick up new forms automatically.

### Encryption

- **Never implement cryptography yourself.** Use the Monocypher functions already wrapped in
  `src/crypto.cpp`, and keep that file small enough to review in one sitting.
- **Changes to the encrypted file format or its defaults need an issue first.** Don't weaken the key derivation
  defaults. New settings must still read older files, which is why each file records its own.
- **Never let plaintext outlive encryption.** Code that writes a return (or anything derived from it) must not
  leave an unencrypted copy next to an encrypted one. Wipe passwords with `wipeString()` once used.
- **Test the failures:** wrong passwords, modified bytes, modified headers and truncated files must all be
  refused (see the `crypto_*` tests).

### Other engine rules

- **Errors are user-facing.** Throw `ot::Error` with a message a person can act on: lowercase, no
  trailing period, naming the field ("wages: 'abc' is not an amount").
- **The engine has no UI.** `opentax_core` must not depend on the CLI or GUI.
- **Keep dependencies at zero.** The engine uses only the C++17 standard library.

## Desktop app guidelines (`gui/`)

- **Screens edit the working return directly** through bound widgets (`ui::MoneyInput`,
  `ui::RecordEditor` and so on). When a widget reports a change, call `changed()`. Recalculation and
  autosave happen automatically, so don't write the file yourself.
- **Prefer `ui::RecordEditor` over hand-built field lists,** so labels and help stay in the schema.
- **Don't hard-code tax-year details.** Use `rules()` for amounts, `yearText()` for years, and
  `lineIds()` or the `Summary` fields for Form 1040 lines that differ by year.
- **Pass by value when the source might change.** `openReturn(recent_.front())` once passed a reference
  into the very list it reordered. That corrupted the config file and crashed the next launch. Callbacks
  that run later (such as delete confirmations) should re-check indexes before using them.
- **Treat files on disk as untrusted.** Validate config and return files when loading, and don't let a
  bad value reach a function that throws.
- **Platform code stays in `platform_*.cpp`.** Never use `system()`, `popen()` or a shell, and only open
  local files the app wrote itself.
- **Match the look.** Use the `ui::` widgets and theme colors (`colorAccent()`, `colorPositive()` and so on)
  rather than hard-coded colors, and check both the light and dark themes.

## Code style

Match the surrounding code. Specifically:

- C++17, 4-space indent, braces on the same line, lines up to about 120 columns.
- `CamelCase` types, `camelCase` functions and variables, `member_` for private members, `kConstant`
  for constants. Engine code is in namespace `ot`, GUI code in `otgui`.
- Comments explain *why* (a tax rule, a trade-off, a trap), not what the next line does. When code
  implements a specific rule, name it: "Sec. 163(h)(3)(E)", "Form 8812, Part II-B".
- **No new warnings.** The project builds cleanly with `-Wall -Wextra -Wpedantic` (and `/W4` on MSVC).
- Prefer small free functions in an anonymous namespace over new classes.

## Testing

Tests live in `tests/test_main.cpp` and use a tiny built-in framework (`TEST`, `CHECK`, `CHECK_EQ`,
`CHECK_THROWS`), so no test framework dependency is needed. Run a subset with
`opentax_tests <name-substring>`.

- **Expected values are worked by hand from the IRS form, not copied from OpenTax's output.** Put the
  arithmetic in a comment next to the check (`// 40,000 - 30% of 20,000`), so a reviewer can verify it
  against the form.
- **Use IRS tables as oracles where they exist.** `tests/data/tax_table_2025.txt` is the complete 2025
  Tax Table, and every row is checked. Spot-check published tables (such as the EIC Table) too.
- **Cover the edges of every rule:** just below, at and just above each threshold and phase-out, each
  filing status that's treated differently, and each year when the rule differs by year.
- **Test the refusals too:** invalid input, unsupported situations (they must produce an error
  diagnostic), and married filing separately restrictions.
- **PDF output** must stay free of active content (see `return_pdf_is_safe_and_complete`).
- **Bug fixes** come with a test that fails without the fix whenever the bug is testable in the engine or
  CLI.

The GUI has no automated tests yet, so describe how you checked a GUI change in the PR. At a minimum:

- Exercise it in a Debug build.
- Check both themes, and both supported tax years.
- Start the app **both with and without** a file argument. Startup with no argument reopens the most
  recent return, and that path has had hidden bugs before.

## Pull request checklist

Copy this into your PR description:

```markdown
## What and why

## Sources
<!-- IRS forms, instructions, revenue procedures or statute sections this change follows -->

## How I tested it
- [ ] `ctest` passes (N checks)
- [ ] Builds with no new warnings (Debug and Release)
- [ ] GUI: Debug build, light + dark theme, both tax years, started with and without a file (if applicable)

## Checklist
- [ ] Every new constant is in `rules_<year>.cpp` with its source, set for every supported year
- [ ] Computed lines use the official line numbers and have a plain-language explanation
- [ ] Expected test values were worked by hand from the forms
- [ ] Unsupported cases produce a diagnostic instead of a guess
- [ ] No network access, no new dependencies, no build-time downloads, no new personal data fields
- [ ] File format: existing keys unchanged; older files still load
- [ ] README / CLI help / SECURITY.md updated where behavior changed
```

## Dependencies

New third-party code needs a strong reason and an issue first. If it's accepted:

- Vendor it into `third_party/` from an official release, unmodified.
- Record its version, source URL and the release archive's **SHA-256** in the table in SECURITY.md, in
  the same commit.
- Make sure its license is compatible with MIT.

## Reporting bugs

Open an issue with:

- your OS and OpenTax version (`opentax --version`), and the tax year of the return,
- what you did, what you expected, and what happened,
- for a wrong number: which form and line, what OpenTax shows, and what you believe it should be, with
  the IRS form, instruction or publication that says so,
- a minimal `.otx` file that shows the problem, if you can make one.

**Never attach your real return.** It contains your income, family details and address. Recreate the
problem in a scratch file with made-up names and round numbers instead.

**Security vulnerabilities** must not be reported in public issues. Follow [SECURITY.md](SECURITY.md).

**Issues are for the software, not tax advice.** We can't tell you how to handle your own situation; for
that, see IRS.gov or a tax professional.

## Conduct

Be respectful and assume good faith. Critique code, not people. We want contributing to be pleasant for
tax people and programmers alike.

## License

OpenTax is MIT-licensed. By submitting a contribution you agree that it's licensed under the
[MIT License](LICENSE) and that you have the right to submit it.
