// Copyright (c) 2026 SopFileWatch Authors. All rights reserved.
// Use of this source code is governed by a MIT-style license.
//
// pdf_convert_worker.cpp

#include "stdafx.h"
#include "pdf_convert_worker.h"

#include "xls_to_pdf_converter.h"

#ifdef Q_OS_WIN
// Excel COM requires an STA apartment on every thread that calls it.
// Qt does NOT initialise COM on worker threads.
#  include <objbase.h>
#endif

namespace sop {

PdfConvertWorker::PdfConvertWorker(QVector<Task> tasks, QObject* parent)
    : QObject(parent), tasks_(std::move(tasks)) {}

void PdfConvertWorker::Run() {
#ifdef Q_OS_WIN
  ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
#endif

  const int total = tasks_.size();
  QStringList errors;

  // Build BatchTask list (only path info; display name kept in tasks_).
  QVector<sop::BatchTask> batch;
  batch.reserve(total);
  for (const auto& t : tasks_) {
    batch.append({t.abs_path, t.pdf_path});
  }

  // Use batch converter: opens Excel/LibreOffice ONCE for all files.
  sop::ConvertBatchXlsToPdf(
      batch,
      sop::PageMarginMm{},
      // on_before: check cancellation and announce the file that is starting.
      // Emit index (= files completed so far) so the bar does NOT yet show
      // 100% – it will advance to index+1 only after on_after runs.
      [this, total](int index) -> bool {
        if (cancelled_.load(std::memory_order_relaxed)) return false;
        emit Progress(index, total, tasks_.at(index).file_name);
        return true;
      },
      // on_after: record result then advance the bar by 1.
      // For the last file this emits Progress(total, total) = 100 %,
      // which happens AFTER Excel/LO has finished writing the PDF.
      [this, &errors, total](int index, const sop::ConvertResult& r) {
        if (!r.success) {
          errors << tasks_.at(index).file_name +
                    QStringLiteral(": ") + r.error_message;
        }
        emit TaskDone(tasks_.at(index).abs_path, r.success, r.error_message);
        emit Progress(index + 1, total, tasks_.at(index).file_name);
      });

  emit Finished(errors);

#ifdef Q_OS_WIN
  ::CoUninitialize();
#endif
}

}  // namespace sop
