// Copyright (c) 2026 SopFileWatch Authors. All rights reserved.
// Use of this source code is governed by a MIT-style license.
//
// file_watcher.cpp

#include "stdafx.h"
#include "file_watcher.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QDebug>

#include "file_name_helper.h"

namespace sop {

FileWatcher::FileWatcher(QObject* parent)
    : QObject(parent), fs_watcher_(new QFileSystemWatcher(this)) {
  connect(fs_watcher_, &QFileSystemWatcher::fileChanged,
          this, &FileWatcher::OnFileChanged);
  connect(fs_watcher_, &QFileSystemWatcher::directoryChanged,
          this, &FileWatcher::OnDirectoryChanged);
}

FileWatcher::~FileWatcher() = default;

void FileWatcher::ScanAndWatch(const QString& root_dir) {
  StopAndClear();
  root_dir_ = root_dir;

  QDirIterator it(root_dir,
                  {QStringLiteral("*.xls"), QStringLiteral("*.xlsx")},
                  QDir::Files,
                  QDirIterator::Subdirectories);
  while (it.hasNext()) {
    AddFile(it.next());
  }

  // Also watch all sub-directories so that newly-created files are noticed.
  fs_watcher_->addPath(root_dir);
  QDirIterator dir_it(root_dir, QDir::Dirs | QDir::NoDotAndDotDot,
                      QDirIterator::Subdirectories);
  while (dir_it.hasNext()) {
    fs_watcher_->addPath(dir_it.next());
  }

  qDebug() << "[FileWatcher] 扫描完成，共发现" << entries_.size() << "个文件";
  emit ScanCompleted(entries_.size());
}

void FileWatcher::StopAndClear() {
  if (!fs_watcher_->files().isEmpty()) {
    fs_watcher_->removePaths(fs_watcher_->files());
  }
  if (!fs_watcher_->directories().isEmpty()) {
    fs_watcher_->removePaths(fs_watcher_->directories());
  }
  entries_.clear();
  root_dir_.clear();
}

void FileWatcher::SetGeneratePdf(int index, bool enabled) {
  if (index < 0 || index >= entries_.size()) return;
  entries_[index].generate_pdf = enabled;
}

void FileWatcher::AddFile(const QString& abs_path) {
  QFileInfo fi(abs_path);
  FileEntry entry;
  entry.abs_path = fi.absoluteFilePath();
  entry.file_name = fi.fileName();
  // Compute relative path from root.
  QDir root(root_dir_);
  entry.rel_path = root.relativeFilePath(entry.abs_path);
  entry.last_modified = fi.lastModified();
  entry.state = FileChangeState::kUnchanged;
  entry.generate_pdf = true;
  entry.pdf_output_name = ExtractAsciiPdfName(entry.file_name);

  entries_.append(entry);
  fs_watcher_->addPath(entry.abs_path);
}

void FileWatcher::OnFileChanged(const QString& path) {
  for (auto& entry : entries_) {
    if (entry.abs_path == path) {
      entry.state = FileChangeState::kModified;
      entry.last_modified = QFileInfo(path).lastModified();
      qDebug() << "[FileWatcher] 文件变更:" << path;
      emit FileChanged(path);
      return;
    }
  }
}

void FileWatcher::OnDirectoryChanged(const QString& path) {
  qDebug() << "[FileWatcher] 目录变更，重新扫描:" << path;
  // A file may have been added or removed; re-scan from root.
  if (!root_dir_.isEmpty()) {
    ScanAndWatch(root_dir_);
  }
}

}  // namespace sop
