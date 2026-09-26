#pragma once

// Plain-text and CSV renderings of a calculated return, for the command line.

#include "opentax/calc.hpp"

#include <string>

namespace ot {

// Refund/owed headline plus the main 1040 figures.
std::string renderSummary(const TaxReturn& r, const Result& result);

// One form or worksheet, line by line. With `explain`, each line's explanation follows it.
std::string renderForm(const FormResult& form, bool explain = false);

std::string renderDiagnostics(const Result& result);

// Every line of every form: form,line,label,amount
std::string renderCsv(const Result& result);

// "Refund $1,234.00" / "Amount owed $56.00" / "No refund or balance due"
std::string headline(const Summary& s);

std::string formatUsd(Money m);

}  // namespace ot
