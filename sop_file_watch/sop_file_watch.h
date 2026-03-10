// Copyright (c) 2026 SopFileWatch Authors. All rights reserved.
// Use of this source code is governed by a MIT-style license.
//
// sop_file_watch.h
// Main window: file list display, tray icon, PDF generation dispatch.

#pragma once

#include <QMainWindow>
#include <QMenu>
#include <QSystemTrayIcon>
#include <QThread>

#include "file_watcher.h"
#include "pdf_convert_worker.h"
#include "xls_to_pdf_converter.h"

QT_BEGIN_NAMESPACE
namespace Ui { class SopFileWatchClass; }
QT_END_NAMESPACE

// Column indices for the file table.
enum TableColumn {
  kColFileName = 0,
  kColRelPath,
  kColChanged,
  kColLastModified,
  kColPdfStatus,
  kColPdfOutputName,
  kColAction,
  kColCount
};

class SopFileWatch : public QMainWindow {
  Q_OBJECT

 public:
  explicit SopFileWatch(QWidget* parent = nullptr);
  ~SopFileWatch() override;

 protected:
  void changeEvent(QEvent* event) override;
  void closeEvent(QCloseEvent* event) override;

 private slots:
  // Toolbar / button handlers.
  void OnBrowseWatchDir();
  void OnBrowseOutputDir();
  void OnStartWatch();
  void OnStopWatch();
  void OnGenerateAll();

  // FileWatcher signals.
  void OnScanCompleted(int file_count);
  void OnFileChanged(const QString& abs_path);

  // Tray icon interaction.
  void OnTrayActivated(QSystemTrayIcon::ActivationReason reason);

  // Per-row action button (stores abs_path, not row index).
  void OnGeneratePdfForFile(const QString& abs_path);

  // PdfConvertWorker signals (queued, delivered on main thread).
  void OnConvertProgress(int current, int total, const QString& file_name);
  void OnConvertTaskDone(const QString& abs_path, bool success,
                         const QString& error_message);
  void OnConvertFinished(const QStringList& errors);

  // Cancel button.
  void OnCancelConvert();

 private:
  // Build / refresh the table from the current entries list.
  void RefreshTable();

  // Update a single row's state columns.
  void UpdateRowState(int row);

  // Insert a "生成 PDF" button widget into the action column of |row|.
  // |abs_path| is stored in the button so it remains valid after re-sorting.
  void SetupRowActionButton(int row, const QString& abs_path);

  // Return the output directory from the UI (creates it if needed).
  QString OutputDir() const;

  // Show a tray notification balloon.
  void ShowTrayMessage(const QString& title, const QString& body,
                       QSystemTrayIcon::MessageIcon icon =
                           QSystemTrayIcon::Information,
                       int duration_ms = 5000);

  // Setup the system tray icon and context menu.
  void SetupTrayIcon();

  // Kick off a background conversion with |tasks|.
  // Disables relevant UI and shows the progress bar.
  void StartConvertWorker(QVector<sop::PdfConvertWorker::Task> tasks);

  // Re-enable UI controls after conversion finishes.
  void SetConvertingState(bool converting);

  // Persist / restore UI paths via an INI file next to the executable.
  void LoadSettings();
  void SaveSettings();

  // Start a conversion worker for any paths queued in pending_auto_convert_.
  // Called from OnConvertFinished after each worker completes.
  void DrainPendingAutoConvert();

  Ui::SopFileWatchClass* ui_{nullptr};
  sop::FileWatcher* watcher_{nullptr};
  QSystemTrayIcon* tray_icon_{nullptr};
  QMenu* tray_menu_{nullptr};

  // Background conversion state (raw pointers, both deleted via deleteLater).
  sop::PdfConvertWorker* convert_worker_{nullptr};
  QThread* convert_thread_{nullptr};
  bool is_converting_{false};

  // Map abs_path -> row index for fast lookup.
  QMap<QString, int> path_to_row_;

  // Paths that changed while a conversion was running; drained after it ends.
  QSet<QString> pending_auto_convert_;
};

