// Copyright (c) 2026 SopFileWatch Authors. All rights reserved.
// Use of this source code is governed by a MIT-style license.
//
// file_name_helper.h
// Utility functions for extracting ASCII-only PDF output filenames
// from source XLS filenames.

#pragma once

#include <QString>

namespace sop {

// Extracts only ASCII letters and digits from |source_name|,
// appends ".pdf" and returns the result.
// Example: "HV400V 50A工业版SOP.xls" -> "HV400V50ASOP.pdf"
QString ExtractAsciiPdfName(const QString& source_name);

}  // namespace sop
