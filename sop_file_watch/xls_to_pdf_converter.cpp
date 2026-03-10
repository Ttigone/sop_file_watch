// Copyright (c) 2026 SopFileWatch Authors. All rights reserved.
// Use of this source code is governed by a MIT-style license.
//
// xls_to_pdf_converter.cpp

#include "stdafx.h"
#include "xls_to_pdf_converter.h"

#include <QAxObject>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QDebug>

namespace sop {

namespace {

// mm -> points (Excel PageSetup uses points).
// 1 inch = 25.4 mm = 72 points
double MmToPoints(double mm) { return mm / 25.4 * 72.0; }

// Apply A4 page setup (margins, fit-to-page-wide) to one worksheet.
void ApplyPageSetup(QAxObject* sheet, const PageMarginMm& m) {
  QAxObject* ps = sheet->querySubObject("PageSetup");
  if (!ps) return;
  // Switch to FitToPages mode first (must come before FitToPagesWide/Tall).
  ps->setProperty("Zoom",              false);
  ps->setProperty("PaperSize",          9);                   // xlPaperA4
  // Orientation is intentionally NOT overridden: each sheet keeps its own
  // portrait/landscape setting so all pages stay a consistent A4 size.
  ps->setProperty("TopMargin",          MmToPoints(m.top));
  ps->setProperty("BottomMargin",       MmToPoints(m.bottom));
  ps->setProperty("LeftMargin",         MmToPoints(m.left));
  ps->setProperty("RightMargin",        MmToPoints(m.right));
  ps->setProperty("HeaderMargin",       MmToPoints(m.header));
  ps->setProperty("FooterMargin",       MmToPoints(m.footer));
  // FitToPagesTall MUST be bool false (not int 0): Excel throws -2146827284
  // for integer 0. false means "no page-height limit" (auto).
  ps->setProperty("FitToPagesWide",    1);
  ps->setProperty("FitToPagesTall",    false);
  ps->setProperty("CenterHorizontally", true);
  delete ps;
}

// -----------------------------------------------------------------------
// Strategy A: Excel / WPS COM Automation (Windows only).
// Opens Excel ONCE, converts all files in a loop, closes Excel ONCE.
// -----------------------------------------------------------------------
bool TryComBatch(
    const QVector<BatchTask>& tasks,
    const PageMarginMm& margin,
    std::function<bool(int)> on_before,
    std::function<void(int, const ConvertResult&)> on_after) {
  // Prefer Excel; fall back to WPS (ket).
  static const char* kProgIds[] = {"Excel.Application",
                                    "ket.Application",
                                    nullptr};
  QAxObject* excel = nullptr;
  for (int i = 0; kProgIds[i] != nullptr; ++i) {
    excel = new QAxObject(QLatin1String(kProgIds[i]));
    if (!excel->isNull()) break;
    delete excel;
    excel = nullptr;
  }
  if (!excel) return false;  // Session could not be created.

  excel->setProperty("Visible",       false);
  excel->setProperty("DisplayAlerts", false);
  excel->setProperty("ScreenUpdating", false);
  // Disabling PrintCommunication prevents each PageSetup property write from
  // querying the printer driver for validation -- the single biggest source
  // of latency (can turn a 30 s conversion into ~2-3 s).
  excel->setProperty("PrintCommunication", false);

  QAxObject* workbooks = excel->querySubObject("Workbooks");
  if (!workbooks) {
    excel->dynamicCall("Quit()");
    delete excel;
    return false;
  }

  for (int i = 0; i < tasks.size(); ++i) {
    if (on_before && !on_before(i)) break;  // Cancelled.

    const BatchTask& task = tasks.at(i);
    ConvertResult result;

    QString native_xls = QDir::toNativeSeparators(task.xls_path);
    QString native_pdf = QDir::toNativeSeparators(task.pdf_path);

    QAxObject* workbook =
        workbooks->querySubObject("Open(const QString&)", native_xls);
    if (!workbook) {
      result = {false, QStringLiteral("COM: open failed: ") + task.xls_path};
    } else {
      // Apply page setup to every sheet (Sheets includes chart sheets too).
      QAxObject* sheets = workbook->querySubObject("Sheets");
      if (sheets) {
        int count = sheets->property("Count").toInt();
        for (int s = 1; s <= count; ++s) {
          QAxObject* sheet = sheets->querySubObject("Item(int)", s);
          if (sheet) {
            ApplyPageSetup(sheet, margin);
            delete sheet;
          }
        }
        delete sheets;
      }
      // Export all sheets as one PDF.
      // 0 = xlTypePDF, 0 = xlQualityStandard
      workbook->dynamicCall(
          "ExportAsFixedFormat(int,const QString&,int)",
          0, native_pdf, 0);
      workbook->dynamicCall("Close(bool)", false);
      delete workbook;
      result = {true, {}};
    }

    if (on_after) on_after(i, result);
  }

  excel->setProperty("PrintCommunication", true);  // Restore before quit.
  excel->dynamicCall("Quit()");
  delete workbooks;
  delete excel;
  return true;
}

// -----------------------------------------------------------------------
// Strategy B: LibreOffice headless CLI (cross-platform fallback).
// Passes ALL source files to one soffice invocation (one startup cost).
// -----------------------------------------------------------------------
bool TryLibreOfficeBatch(
    const QVector<BatchTask>& tasks,
    std::function<bool(int)> on_before,
    std::function<void(int, const ConvertResult&)> on_after) {
  QStringList candidates;
#ifdef Q_OS_WIN
  candidates << QStringLiteral("soffice.exe")
             << QStringLiteral("C:/Program Files/LibreOffice/program/soffice.exe")
             << QStringLiteral("C:/Program Files (x86)/LibreOffice/program/soffice.exe");
#else
  candidates << QStringLiteral("soffice")
             << QStringLiteral("/usr/bin/soffice")
             << QStringLiteral("/usr/local/bin/soffice")
             << QStringLiteral("/opt/libreoffice/program/soffice");
#endif

  QString soffice;
  for (const auto& c : candidates) {
    if (QFileInfo::exists(c)) { soffice = c; break; }
    QString found = QStandardPaths::findExecutable(
        QFileInfo(c).fileName());
    if (!found.isEmpty()) { soffice = found; break; }
  }
  if (soffice.isEmpty()) return false;

  // We need all files to go to the same directory (LibreOffice --outdir).
  // Group by output directory and fire one invocation per group.
  QMap<QString, QVector<int>> by_outdir;  // outdir -> task indices
  for (int i = 0; i < tasks.size(); ++i) {
    QString d = QFileInfo(tasks.at(i).pdf_path).absolutePath();
    QDir().mkpath(d);
    by_outdir[d].append(i);
  }

  for (auto it = by_outdir.begin(); it != by_outdir.end(); ++it) {
    const QString& out_dir = it.key();
    const QVector<int>& indices = it.value();

    // Check cancellation.
    bool aborted = false;
    for (int idx : indices) {
      if (on_before && !on_before(idx)) { aborted = true; break; }
    }
    if (aborted) break;

    QStringList args;
    args << QStringLiteral("--headless")
         << QStringLiteral("--convert-to") << QStringLiteral("pdf")
         << QStringLiteral("--outdir") << out_dir;
    for (int idx : indices) {
      args << tasks.at(idx).xls_path;
    }

    QProcess proc;
    proc.start(soffice, args);
    bool finished = proc.waitForFinished(300000);  // 5 min for a batch

    for (int idx : indices) {
      ConvertResult r;
      if (!finished || proc.exitCode() != 0) {
        r = {false, QStringLiteral("LibreOffice failed (exit=") +
                    QString::number(proc.exitCode()) + QLatin1Char(')') };
      } else {
        // LibreOffice names the PDF after the source base name.
        QFileInfo src(tasks.at(idx).xls_path);
        QString generated = out_dir + QLatin1Char('/') +
                            src.baseName() + QStringLiteral(".pdf");
        const QString& desired = tasks.at(idx).pdf_path;
        if (QFileInfo(generated).absoluteFilePath() !=
            QFileInfo(desired).absoluteFilePath()) {
          QFile::remove(desired);
          if (!QFile::rename(generated, desired)) {
            r = {false, QStringLiteral("LibreOffice: rename failed: ") + desired};
          } else {
            r = {true, {}};
          }
        } else {
          r = {true, {}};
        }
      }
      if (on_after) on_after(idx, r);
    }
  }
  return true;
}

}  // namespace

// -----------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------

void ConvertBatchXlsToPdf(
    const QVector<BatchTask>& tasks,
    const PageMarginMm& margin,
    std::function<bool(int)> on_before,
    std::function<void(int, const ConvertResult&)> on_after) {
  if (tasks.isEmpty()) return;

  // Ensure all output directories exist.
  for (const auto& t : tasks) {
    QDir().mkpath(QFileInfo(t.pdf_path).absolutePath());
  }

#ifdef Q_OS_WIN
  if (TryComBatch(tasks, margin, on_before, on_after)) {
    qDebug() << "[Converter] COM batch OK, files:" << tasks.size();
    return;
  }
  qDebug() << "[Converter] COM batch unavailable, trying LibreOffice...";
#endif

  if (!TryLibreOfficeBatch(tasks, on_before, on_after)) {
    // Neither backend available - report failure for all tasks.
    ConvertResult r{false,
                   QStringLiteral("No converter available "
                                  "(install Excel, WPS or LibreOffice)")};
    for (int i = 0; i < tasks.size(); ++i) {
      if (on_before && !on_before(i)) break;
      if (on_after) on_after(i, r);
    }
  }
}

ConvertResult ConvertXlsToPdf(const QString& xls_path,
                              const QString& pdf_path,
                              const PageMarginMm& margin) {
  if (!QFileInfo::exists(xls_path)) {
    return {false, QStringLiteral("Source not found: ") + xls_path};
  }
  ConvertResult result{false, QStringLiteral("No result yet")};
  QVector<BatchTask> tasks = {{xls_path, pdf_path}};
  ConvertBatchXlsToPdf(
      tasks, margin,
      nullptr,
      [&result](int, const ConvertResult& r) { result = r; });
  return result;
}

}  // namespace sop
