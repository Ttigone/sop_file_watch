// Copyright (c) 2026 SopFileWatch Authors. All rights reserved.
// Use of this source code is governed by a MIT-style license.
//
// file_watcher.h
// Recursively scans a directory for XLS/XLSX files, wraps
// QFileSystemWatcher and emits signals when files change.

#pragma once

#include <QDateTime>
#include <QFileSystemWatcher>
#include <QObject>
#include <QString>
#include <QVector>

namespace sop {

// State of the file relative to the last known snapshot.
enum class FileChangeState {
  kUnchanged,  // Not yet modified since scan or last acknowledgment.
  kModified,   // External modification detected.
};

// Describes one watched XLS/XLSX file entry.
struct FileEntry {
  QString abs_path;     // Absolute path (UTF-8 / UTF-16 safe).
  QString rel_path;     // Relative path from the watch root.
  QString file_name;    // Bare filename including extension.
  QDateTime last_modified;   // Last known modification time.
  FileChangeState state{FileChangeState::kUnchanged};
  bool generate_pdf{true};   // Whether the user wants a PDF generated.
  QString pdf_output_name;   // Derived output PDF filename (ASCII only).
};

// FileWatcher owns a QFileSystemWatcher and a list of FileEntry objects.
// It provides helpers to scan, start/stop watching and query the entry list.
class FileWatcher : public QObject {
  Q_OBJECT

 public:
  explicit FileWatcher(QObject* parent = nullptr);
  ~FileWatcher() override;

  // Scan |root_dir| recursively for *.xls and *.xlsx files and begin watching.
  // Emits scanCompleted() when done.
  void ScanAndWatch(const QString& root_dir);

  // Stop watching and clear the entry list.
  void StopAndClear();

  const QVector<FileEntry>& entries() const { return entries_; }
  const QString& root_dir() const { return root_dir_; }

  // Set the generate_pdf flag for the entry at |index|.
  void SetGeneratePdf(int index, bool enabled);

 signals:
  // Emitted when a new scan has finished.
  void ScanCompleted(int file_count);

  // Emitted when a specific file has been changed externally.
  // |abs_path| is the absolute path of the modified file.
  void FileChanged(const QString& abs_path);

 private slots:
  void OnFileChanged(const QString& path);
  void OnDirectoryChanged(const QString& path);

 private:
  void AddFile(const QString& abs_path);

  QString root_dir_;
  QVector<FileEntry> entries_;
  QFileSystemWatcher* fs_watcher_{nullptr};
};

}  // namespace sop
