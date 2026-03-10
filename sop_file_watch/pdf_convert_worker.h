// Copyright (c) 2026 SopFileWatch Authors. All rights reserved.
// Use of this source code is governed by a MIT-style license.
//
// pdf_convert_worker.h
// Background QObject that performs XLS->PDF conversions on a worker thread.
// Usage:
//   auto* worker = new PdfConvertWorker(tasks);
//   auto* thread = new QThread;
//   worker->moveToThread(thread);
//   connect(thread, &QThread::started,  worker, &PdfConvertWorker::Run);
//   connect(worker, &PdfConvertWorker::Finished, thread, &QThread::quit);
//   connect(worker, &PdfConvertWorker::Finished, worker, &QObject::deleteLater);
//   connect(thread, &QThread::finished, thread, &QObject::deleteLater);
//   thread->start();

#pragma once

#include <atomic>

#include <QObject>
#include <QStringList>
#include <QVector>

namespace sop {

// PdfConvertWorker performs one or more XLS->PDF conversions on a worker
// thread.  All signals are emitted from the worker thread; Qt's queued
// connection mechanism delivers them safely to the main-thread receivers.
class PdfConvertWorker : public QObject {
  Q_OBJECT

 public:
  struct Task {
    QString abs_path;   // Source XLS/XLSX absolute path.
    QString pdf_path;   // Destination PDF absolute path.
    QString file_name;  // Display name used for progress reporting.
  };

  explicit PdfConvertWorker(QVector<Task> tasks, QObject* parent = nullptr);
  ~PdfConvertWorker() override = default;

  // Thread-safe cancellation request.  Safe to call from any thread.
  void RequestCancel() {
    cancelled_.store(true, std::memory_order_relaxed);
  }

 public slots:
  // Main entry point; invoked by QThread::started.
  void Run();

 signals:
  // Emitted just before converting file number |current| (1-based) of |total|.
  void Progress(int current, int total, const QString& file_name);

  // Emitted after each individual task completes.
  void TaskDone(const QString& abs_path, bool success,
                const QString& error_message);

  // Emitted when all tasks have been processed (or cancelled).
  void Finished(const QStringList& errors);

 private:
  QVector<Task> tasks_;
  std::atomic<bool> cancelled_{false};
};

}  // namespace sop
