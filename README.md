# OpenTax

**Free, open source federal income tax preparation in modern C++. An alternative to TurboTax.**

OpenTax walks you through your 2025 or 2026 federal return: who you are, your dependents, income, deductions, credits
and payments. It fills in Form 1040 and every schedule, form and worksheet you need, and shows your refund
update live as you type. Every line on every form comes with a plain-English note on how it was figured.
Your return lives in one plain-text file on your computer. There's no account, no cloud, no upsell and no
dependency beyond a C++17 compiler.

> Status: 0.1, tax years 2025 and 2026, federal only. OpenTax prepares a complete line-by-line computation of your
> return; it does **not** e-file. You copy the amounts onto the official forms (for example with IRS Free File
> Fillable Forms) and file them yourself. See [what's supported](#whats-supported) and [limits](#limits).

## Desktop app

`opentax-gui` is a native desktop app built with [Dear ImGui](https://github.com/ocornut/imgui), running on
Win32 + Direct3D 11 on Windows and GLFW + OpenGL 3 on Linux and macOS.

- **Guided interview:** Tax home, About you, Dependents, Income, Deductions, Credits, Payments, Review, and
  Forms & PDF, each a screen of plain questions.
- **Refund meter:** your refund or balance due, AGI, taxable income, total tax, effective rate and marginal
  rate stay pinned at the top and update as you type.
- **Form entry that matches the paper:** W-2, 1099-INT, 1099-DIV, 1099-B, 1099-R, SSA-1099 and 1099-G are
  entered box by box, with help on every field.
- **Smart comparisons:** standard vs. itemized deduction is chosen for you and explained; credits are listed
  with the reason for each amount.
- **Review:** errors, things to check and notes, each with a "Go there" link, plus a tax summary that walks
  from total income to your refund.
- **Forms browser:** every form and worksheet, line by line. Select a line to see exactly how it was calculated.
- **Output:** a printable PDF of the whole return (summary, forms, schedules, worksheets and explanations), and
  CSV export of every line.

Everything saves automatically. Open a return with `opentax-gui path\to\return.otx`, or use File > Open.

**Password protection.** A return can be encrypted with a password when you start it, or later from
File > Protect with Password (`opentax encrypt` on the command line). It uses Argon2id and
XChaCha20-Poly1305; see [SECURITY.md](SECURITY.md) for exactly what that protects and what it doesn't.

## Tax years

**2025** is complete and checked against the final 2025 IRS forms, instructions and tables.

**2026** uses the IRS's 2026 inflation adjustments (Rev. Proc. 2025-32, Notice 2025-67) and the draft 2026
forms, including the new layout of Form 1040 (lines 12f, 13a/13b, 24a-c and 32a-c), Schedules 1-A, 2 and 3-A,
and Form 8959. New for 2026:

- A charitable deduction of up to $1,000 ($2,000 joint) for people who don't itemize (line 12f).
- Itemized charitable gifts count only above 0.5% of AGI, and itemized deductions are trimmed by 2/37 for
  income in the 37% bracket.
- Mortgage insurance premiums are deductible again.
- The dependent care credit starts at 50% and the benefit exclusion rises to $7,500.
- A $400 minimum QBI deduction for active businesses with at least $1,000 of QBI.
- The AMT exemption phases out twice as fast, starting at $500,000 ($1,000,000 joint).
- Schedule 3-A, which limits refundable credits for filers who aren't citizens, nationals or qualified aliens.

The official 2026 instructions come out in early 2027. Until then, two worksheets that exist only in the
instructions (the charitable contribution limits and the itemized deduction limitation) follow the law as
written, and OpenTax marks them **provisional** on the Review screen when they affect your return. The 2026 Tax
and EIC Tables use the same midpoint method as every year's tables. Switch a
return's year on the About you screen, or with `opentax set info year=2026`.

## What's supported

| Area | Coverage |
|---|---|
| **Filing status** | Single, married filing jointly, married filing separately, head of household, qualifying surviving spouse, including dependent filers and the 65+/blind standard deduction |
| **Income** | Wages (W-2), interest (1099-INT, Schedule B), dividends and capital gain distributions (1099-DIV), sales of stocks, funds and crypto (1099-B, Form 8949, Schedule D, capital loss carryovers in and out), self-employment (Schedule C with the simplified home office and 50% meals), retirement distributions (1099-R, rollovers, early-distribution tax), Social Security (full taxable-benefits worksheet), unemployment (1099-G), other income |
| **Adjustments** | Educator expenses, HSA, deductible half of SE tax, SEP/SIMPLE, SE health insurance, early withdrawal penalty, traditional IRA (with the workplace-plan phase-outs), student loan interest |
| **New for 2025 (Schedule 1-A)** | No tax on tips, no tax on overtime, car loan interest, and the enhanced deduction for seniors, with each phase-out exactly as on the form |
| **Deductions** | Standard deduction, or Schedule A: medical over 7.5%, state and local taxes with the 2025 $40,000 cap and its phase-down, mortgage interest, investment interest, charity (with AGI limits); QBI deduction (Form 8995) |
| **Tax** | Tax Table and Tax Computation Worksheet, Qualified Dividends and Capital Gain Tax Worksheet, alternative minimum tax (Form 6251), self-employment tax (Schedule SE), Additional Medicare Tax (Form 8959), net investment income tax (Form 8960), 10%/25% early distribution tax |
| **Credits** | Child tax credit and credit for other dependents with the additional child tax credit (Schedule 8812, including Part II-B), earned income credit, child and dependent care credit with employer benefits (Form 2441), American opportunity and lifetime learning credits (Form 8863), saver's credit (Form 8880), excess social security withholding |
| **Payments** | Withholding (including Additional Medicare Tax withholding), estimated payments, prior-year overpayment, extension payment, refund applied to next year |

## How it stays correct

- **The numbers come from the IRS.** Every constant is in [src/rules_2025.cpp](src/rules_2025.cpp) or
  [src/rules_2026.cpp](src/rules_2026.cpp) with its source, and each worksheet step in
  [src/calc.cpp](src/calc.cpp) was checked against the IRS forms and instructions (and P.L. 119-21 for the new
  deductions).
- **The IRS tables are the tests.** The test suite checks the tax calculation against **every row of the 2025
  Tax Table** for every filing status ([tests/data/tax_table_2025.txt](tests/data/tax_table_2025.txt), extracted
  from the Form 1040 instructions), and the EIC calculation against rows of the IRS EIC Table.
- **2026 checked against the Rev. Proc.** The 2026 rate tables must reproduce the tax amounts printed in
  Rev. Proc. 2025-32 at every bracket boundary, and 20 more tests cover the 2026 changes.
- **Scenarios worked by hand.** About 40 more tests are complete returns with expected values computed
  independently from the forms: self-employment with QBI, capital gains at 0/15/20%, loss carryovers, Social
  Security, seniors, phase-outs of every credit and deduction, and more.
- **Money is never floating point.** All amounts are exact integer cents, as in OpenBooks.

## Building

You need CMake 3.16+ and a C++17 compiler (GCC 9+, Clang 10+, or MSVC 2019+).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

This builds `opentax` (command line), `opentax-gui` (desktop app) and `opentax_tests`. With Visual Studio
generators they land in `build\Release\`. On MinGW the executables are linked statically, so they need no DLLs.

The desktop app uses the vendored Dear ImGui in `third_party/imgui`, so Windows needs nothing extra. On
Linux, install GLFW first (`sudo apt install libglfw3-dev`); on macOS, `brew install glfw`. To build only the
engine and command line, add `-DOPENTAX_BUILD_GUI=OFF`.

## Command line

Everything the app does is scriptable. The return file defaults to `$OPENTAX_FILE`, then `./return.otx`;
use `-f FILE` for another.

```bash
opentax new status=mfj first=Jordan last=Rivera dob=1987-03-14    # add year=2026 for a 2026 return
opentax set spouse first=Casey dob=1988-11-02
opentax add w2 employer=Northwind wages=78000 fedwh=6400 sswages=78000 sswh=4836 medwages=78000 medwh=1131 overtime=4200
opentax add w2 owner=spouse employer="Contoso Cafe" wages=31000 fedwh=1500 sstips=6000 tips=6000
opentax add dependent first=Mia last=Rivera dob=2017-06-01 care=4000
opentax add 1099int payer="Ally Bank" interest=820

opentax                        # summary: refund or amount owed, and the main figures
opentax check                  # errors, warnings and notes (exit code 1 on errors)
opentax forms                  # the forms, schedules and worksheets on this return
opentax form 1040 --explain    # one form, line by line, with how each line was figured
opentax explain 1040 16        # just one line
opentax pdf                    # "2025 Federal Return - Jordan & Casey Rivera.pdf"
opentax csv > lines.csv
```

`opentax list` shows what you've entered, `opentax edit w2 2 wages=31500` and `opentax remove w2 2` change
entries, and `opentax fields w2` lists every field a form accepts. See
[examples/rivera-family.otx](examples/rivera-family.otx) for a complete sample return.

## File format

A `.otx` file is UTF-8 text: an `OPENTAX 1` header, then one record per line, a tag and tab-separated
`key=value` fields, with blank fields left out:

```
OPENTAX 1
INFO	status=mfj	street=41 Elm Street	city=Springfield	state=IL	zip=62701
TAXPAYER	first=Jordan	last=Rivera	dob=1987-03-14
W2	employer=Northwind	wages=78000.00	fedwh=6400.00	retplan=yes	overtime=4200.00
DEPENDENT	first=Mia	last=Rivera	dob=2017-06-01	care=4000.00
```

It's easy to read, diff and back up. Unknown records or fields are rejected rather than silently dropped.
Saves are atomic, and the previous version is kept as `.otx.bak`. A password-protected return starts with
`OPENTAX-ENCRYPTED 1` instead, followed by its encryption settings and the encrypted contents.

## Project layout

```
include/opentax/     public headers
  money.hpp, date.hpp   exact currency/decimal arithmetic and dates (from OpenBooks)
  model.hpp          the return: typed records plus a field schema that drives the file
                     format, the command line and the app's form editors
  rules.hpp          tax-year constants (src/rules_2025.cpp, src/rules_2026.cpp)
  calc.hpp           the calculation: forms, lines, explanations, diagnostics
  report.hpp         text and CSV output
  crypto.hpp         password protection (Argon2id + XChaCha20-Poly1305 via Monocypher)
  pdf.hpp            dependency-free PDF writer (from OpenBooks)
  return_pdf.hpp     the printable return
  cli.hpp            command line
gui/                 desktop app
  app.*              shell, persistence, sidebar, refund meter, welcome screen
  app_screens.cpp    the interview screens
  app_review.cpp     review, forms browser, PDF and CSV export
  widgets.*          bound inputs and the schema-driven form editor
tests/               self-contained test suite and IRS table fixtures
third_party/imgui/   Dear ImGui 1.92.9b (MIT)
third_party/monocypher/  Monocypher 4.0.3 (BSD-2-Clause or CC0)
```

The engine (`opentax_core`) doesn't depend on either front end, and `calculate()` is a pure function of the
return, so the app, the command line and the tests all get identical results.

## Limits

OpenTax tells you (on the Review screen and in `opentax check`) when your return needs something it
doesn't handle. Not yet supported:

- **E-filing.** Filing electronically requires IRS authorization as an e-file provider. Print the PDF and copy
  the amounts onto the official forms, or into IRS Free File Fillable Forms.
- **State returns.**
- **QBI deduction above $197,300 ($394,600 joint) of taxable income** (Form 8995-A); flagged as an error.
- Rental and pass-through income (Schedule E, K-1s), farm income, the premium tax credit (Form 8962),
  foreign income and tax credit, adoption and energy credits, the underpayment penalty (Form 2210), IRA basis
  (Form 8606), HSA details (Form 8889 beyond the deduction), 28% and unrecaptured section 1250 gains.
- Tax years other than 2025 and 2026.

## Roadmap

- [ ] Fill the official IRS PDF forms directly
- [ ] Schedule E (rentals, K-1s) and Form 8606
- [ ] Form 8995-A, premium tax credit, underpayment penalty
- [ ] State returns (starting with flat-tax states)
- [ ] Import W-2/1099 data from CSV and prior-year returns
- [x] 2026 tax year
- [ ] Replace the provisional 2026 worksheets once the IRS publishes the 2026 instructions

## Contributing

Contributions are welcome. See [CONTRIBUTING.md](CONTRIBUTING.md) for the ground rules, how to add a form or
a tax year, and the pull request checklist. The rule to keep: **every number must be traceable to an IRS
source, and money is never a `double`.**

## Disclaimer

OpenTax is not tax, legal or financial advice. It is provided as is, without warranty. Review your return
carefully; you are responsible for what you file.

## License

MIT. See [LICENSE](LICENSE). Money, date, PDF and GUI foundations come from OpenBooks (MIT).
