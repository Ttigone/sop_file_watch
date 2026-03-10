// Copyright (c) 2026 SopFileWatch Authors. All rights reserved.
// Use of this source code is governed by a MIT-style license.
//
// xls_to_pdf_converter.h
// Converts all sheets in an XLS/XLSX file to a single A4 PDF.
// Strategy: Windows -> QAxObject (Excel/WPS COM reused session);
//           fallback -> LibreOffice headless CLI (multi-file batch).

#pragma once

#include <functional>

#include <QString>
#include <QVector>

namespace sop {

// Describes the result of a single file conversion.
struct ConvertResult {
  bool success{false};
  QString error_message;
};

// Margin settings in millimetres.
// Defaults are "narrow" margins optimised for SOP content on A4.
struct PageMarginMm {
  double top{10.0};     // Paper top edge -> content area
  double bottom{10.0};
  double left{10.0};
  double right{10.0};
  double header{5.0};   // Paper top edge -> header (must be <= top)
  double footer{5.0};   // Paper bottom edge -> footer (must be <= bottom)
};

// Describes one file to convert in a batch.
struct BatchTask {
  QString xls_path;  // Source XLS/XLSX absolute path.
  QString pdf_path;  // Destination PDF absolute path.
};

// Single-file convenience wrapper (opens/closes Excel per call).
ConvertResult ConvertXlsToPdf(const QString& xls_path,
                              const QString& pdf_path,
                              const PageMarginMm& margin = PageMarginMm{});

// Batch conversion: reuses ONE Excel/LibreOffice session for all tasks.
// This is significantly faster than calling ConvertXlsToPdf N times.
//
// |on_before|: called before each task (0-based index).  Return false to abort.
// |on_after|:  called after each task with the result.
void ConvertBatchXlsToPdf(
    const QVector<BatchTask>& tasks,
    const PageMarginMm& margin,
    std::function<bool(int)> on_before,
    std::function<void(int, const ConvertResult&)> on_after);

}  // namespace sop
