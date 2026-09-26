#pragma once

// A printable PDF of the calculated return: a summary page, every form, schedule and
// worksheet line by line (with explanations), and the review notes. It is a computation
// record for transcribing onto the official IRS forms, not a substitute for them.

#include "opentax/calc.hpp"

#include <string>

namespace ot {

struct ReturnPdfOptions {
    bool explanations = true;  // print each line's "how" note under it
    bool worksheets = true;    // include worksheets (not only forms you file)
};

std::string returnPdf(const TaxReturn& r, const Result& result, const ReturnPdfOptions& options = {});

// A safe default file name, e.g. "2025 Federal Return - Jane Doe.pdf".
std::string returnPdfFileName(const TaxReturn& r);

}  // namespace ot
