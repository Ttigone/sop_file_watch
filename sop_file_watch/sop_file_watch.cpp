// Copyright (c) 2026 SopFileWatch Authors. All rights reserved.
// Use of this source code is governed by a MIT-style license.
//
// sop_file_watch.cpp
//
// Source encoding: UTF-8 (compiled with /utf-8; tr() used for all UI strings).

#include "stdafx.h"
#include "sop_file_watch.h"
#include "ui_sop_file_watch.h"

#include <QAction>
#include <QCloseEvent>
#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QByteArray>
#include <QEvent>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidgetItem>
#include <QWidget>
#include <QDebug>

#include "file_name_helper.h"

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

SopFileWatch::SopFileWatch(QWidget* parent)
    : QMainWindow(parent), ui_(new Ui::SopFileWatchClass) {
  ui_->setupUi(this);

  // Prepare file watcher.
  watcher_ = new sop::FileWatcher(this);
  connect(watcher_, &sop::FileWatcher::ScanCompleted,
          this, &SopFileWatch::OnScanCompleted);
  connect(watcher_, &sop::FileWatcher::FileChanged,
          this, &SopFileWatch::OnFileChanged);

  // Table column sizing.
  ui_->table_files->horizontalHeader()->setSectionResizeMode(
      kColFileName, QHeaderView::ResizeToContents);
  ui_->table_files->horizontalHeader()->setSectionResizeMode(
      kColRelPath, QHeaderView::Stretch);
  ui_->table_files->horizontalHeader()->setSectionResizeMode(
      kColChanged, QHeaderView::ResizeToContents);
  ui_->table_files->horizontalHeader()->setSectionResizeMode(
      kColLastModified, QHeaderView::ResizeToContents);
    ui_->table_files->horizontalHeader()->setSectionResizeMode(
      kColLastGenerated, QHeaderView::ResizeToContents);
  ui_->table_files->horizontalHeader()->setSectionResizeMode(
      kColPdfStatus, QHeaderView::ResizeToContents);
  ui_->table_files->horizontalHeader()->setSectionResizeMode(
      kColPdfOutputName, QHeaderView::ResizeToContents);
  ui_->table_files->horizontalHeader()->setSectionResizeMode(
      kColAction, QHeaderView::ResizeToContents);

  // Button connections.
  connect(ui_->btn_browse_watch_dir, &QPushButton::clicked,
          this, &SopFileWatch::OnBrowseWatchDir);
  connect(ui_->btn_browse_output_dir, &QPushButton::clicked,
          this, &SopFileWatch::OnBrowseOutputDir);
  connect(ui_->btn_start_watch, &QPushButton::clicked,
          this, &SopFileWatch::OnStartWatch);
  connect(ui_->btn_stop_watch, &QPushButton::clicked,
          this, &SopFileWatch::OnStopWatch);
  connect(ui_->btn_generate_all, &QPushButton::clicked,
          this, &SopFileWatch::OnGenerateAll);
  connect(ui_->btn_cancel_convert, &QPushButton::clicked,
          this, &SopFileWatch::OnCancelConvert);

  SetupTrayIcon();
  LoadSettings();

  ui_->label_status->setText(tr("就绪"));
}

SopFileWatch::~SopFileWatch() {
  delete ui_;
}

// ---------------------------------------------------------------------------
// Protected overrides
// ---------------------------------------------------------------------------

void SopFileWatch::changeEvent(QEvent* event) {
  if (event->type() == QEvent::WindowStateChange) {
    if (isMinimized() && tray_icon_ && tray_icon_->isVisible()) {
      hide();
      tray_icon_->showMessage(
          tr("SOP 文件监控"),
          tr("程序已最小化到系统托盘，继续监控文件变化"),
          QSystemTrayIcon::Information, 3000);
      event->ignore();
      return;
    }
  }
  QMainWindow::changeEvent(event);
}

void SopFileWatch::closeEvent(QCloseEvent* event) {
  // Cancel any in-progress conversion and wait briefly for the thread to stop.
  if (is_converting_ && convert_worker_) {
    convert_worker_->RequestCancel();
    if (convert_thread_ && convert_thread_->isRunning()) {
      convert_thread_->wait(3000);
    }
  }
  SaveSettings();
  tray_icon_->hide();
  event->accept();
}

// ---------------------------------------------------------------------------
// Private slots: buttons
// ---------------------------------------------------------------------------

void SopFileWatch::OnBrowseWatchDir() {
  QString dir = QFileDialog::getExistingDirectory(
      this,
      tr("选择监控文件夹"),
      ui_->edit_watch_dir->text().isEmpty() ? QDir::homePath()
                                            : ui_->edit_watch_dir->text(),
      QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
  if (!dir.isEmpty()) {
    ui_->edit_watch_dir->setText(dir);
    SaveSettings();  // Persist immediately; don't rely solely on closeEvent.
  }
}

void SopFileWatch::OnBrowseOutputDir() {
  QString dir = QFileDialog::getExistingDirectory(
      this,
      tr("选择 PDF 输出目录"),
      ui_->edit_output_dir->text().isEmpty() ? QDir::homePath()
                                             : ui_->edit_output_dir->text(),
      QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
  if (!dir.isEmpty()) {
    ui_->edit_output_dir->setText(dir);
    SaveSettings();  // Persist immediately; don't rely solely on closeEvent.
  }
}

void SopFileWatch::OnStartWatch() {
  QString dir = ui_->edit_watch_dir->text().trimmed();
  if (dir.isEmpty()) {
    QMessageBox::warning(this, tr("提示"), tr("请先选择监控文件夹"));
    return;
  }
  if (!QDir(dir).exists()) {
    QMessageBox::warning(this, tr("提示"), tr("指定的文件夹不存在"));
    return;
  }
  watcher_->ScanAndWatch(dir);
  ui_->btn_start_watch->setEnabled(false);
  ui_->btn_stop_watch->setEnabled(true);
  ui_->label_status->setText(tr("正在监控: ") + dir);
}

void SopFileWatch::OnStopWatch() {
  watcher_->StopAndClear();
  path_to_row_.clear();
  ui_->table_files->setRowCount(0);
  ui_->btn_start_watch->setEnabled(true);
  ui_->btn_stop_watch->setEnabled(false);
  ui_->label_status->setText(tr("已停止监控"));
  ui_->label_file_count->setText(tr("文件数：0"));
}

void SopFileWatch::OnGenerateAll() {
  if (is_converting_) return;
  if (watcher_->entries().isEmpty()) {
    QMessageBox::information(this, tr("提示"),
                             tr("暂无监控文件，请先开始监控"));
    return;
  }
  QString out_dir = OutputDir();
  if (out_dir.isEmpty()) return;

  QVector<sop::PdfConvertWorker::Task> tasks;
  for (const auto& e : watcher_->entries()) {
    if (!e.generate_pdf) continue;
    tasks.append({e.abs_path,
                  out_dir + QLatin1Char('/') + e.pdf_output_name,
                  e.file_name});
  }
  if (tasks.isEmpty()) return;

  StartConvertWorker(std::move(tasks));
}

// ---------------------------------------------------------------------------
// Private slots: watcher signals
// ---------------------------------------------------------------------------

void SopFileWatch::OnScanCompleted(int file_count) {
  RefreshTable();
  ui_->label_file_count->setText(tr("文件数：") + QString::number(file_count));

  // Auto-regenerate files whose last-generated time is older than modified.
  QString out_dir = ui_->edit_output_dir->text().trimmed();
  const bool can_convert = !out_dir.isEmpty();
  if (can_convert) QDir().mkpath(out_dir);

  QVector<sop::PdfConvertWorker::Task> tasks;
  const auto& entries = watcher_->entries();
  for (const auto& e : entries) {
    const QDateTime last_generated = LastGeneratedFor(e.abs_path);
    if (!last_generated.isValid()) continue;
    if (last_generated < e.last_modified) {
      // Mark status as "待重新生成" in the table.
      int row = path_to_row_.value(e.abs_path, -1);
      if (row >= 0) {
        auto* status_item = ui_->table_files->item(row, kColPdfStatus);
        if (!status_item) {
          status_item = new QTableWidgetItem();
          ui_->table_files->setItem(row, kColPdfStatus, status_item);
        }
        status_item->setText(tr("待重新生成"));
        status_item->setForeground(QColor(Qt::darkYellow));
      }

      if (!can_convert) continue;

      if (is_converting_) {
        pending_auto_convert_.insert(e.abs_path);
      } else {
        tasks.append({e.abs_path,
                      out_dir + QLatin1Char('/') + e.pdf_output_name,
                      e.file_name});
      }
    }
  }

  if (!is_converting_ && !tasks.isEmpty()) {
    StartConvertWorker(std::move(tasks));
  }
}

void SopFileWatch::OnFileChanged(const QString& abs_path) {
  int row = path_to_row_.value(abs_path, -1);
  if (row < 0) return;

  UpdateRowState(row);

  // Tray notification (only when window is hidden/minimized).
  if (isHidden() || isMinimized()) {
    QFileInfo fi(abs_path);
    ShowTrayMessage(
        tr("SOP 文件已修改"),
        fi.fileName() + tr(" 已被外部修改"),
        QSystemTrayIcon::Warning);
  }

  // Auto-regenerate: delete the stale PDF and start (or queue) a conversion.
  // Silently skip if no output directory has been configured yet.
  QString out_dir = ui_->edit_output_dir->text().trimmed();
  if (out_dir.isEmpty()) return;
  QDir().mkpath(out_dir);

  const auto& entries = watcher_->entries();
  for (const auto& e : entries) {
    if (e.abs_path != abs_path) continue;

    QString pdf_path = out_dir + QLatin1Char('/') + e.pdf_output_name;

    // Remove the stale PDF so it is not mistaken for an up-to-date one.
    if (QFileInfo::exists(pdf_path)) QFile::remove(pdf_path);

    // Mark status cell as "待重新生成".
    auto* status_item = ui_->table_files->item(row, kColPdfStatus);
    if (!status_item) {
      status_item = new QTableWidgetItem();
      ui_->table_files->setItem(row, kColPdfStatus, status_item);
    }
    status_item->setText(tr("待重新生成"));
    status_item->setForeground(QColor(Qt::darkYellow));

    if (!is_converting_) {
      QVector<sop::PdfConvertWorker::Task> tasks;
      tasks.append({e.abs_path, pdf_path, e.file_name});
      StartConvertWorker(std::move(tasks));
    } else {
      // Busy — queue and process when the current worker finishes.
      pending_auto_convert_.insert(abs_path);
    }
    break;
  }
}

// ---------------------------------------------------------------------------
// Private slots: tray
// ---------------------------------------------------------------------------

void SopFileWatch::OnTrayActivated(QSystemTrayIcon::ActivationReason reason) {
  if (reason == QSystemTrayIcon::DoubleClick ||
      reason == QSystemTrayIcon::Trigger) {
    showNormal();
    activateWindow();
    raise();
  }
}

// ---------------------------------------------------------------------------
// Private slots: background convert worker
// ---------------------------------------------------------------------------

void SopFileWatch::OnConvertProgress(int current, int total,
                                     const QString& file_name) {
  ui_->progress_bar->setMaximum(total);
  ui_->progress_bar->setValue(current);
  ui_->label_status->setText(
      tr("转换 (%1/%2): ").arg(current).arg(total) + file_name);
}

void SopFileWatch::OnConvertTaskDone(const QString& abs_path, bool success,
                                     const QString& /*error_message*/) {
  int row = path_to_row_.value(abs_path, -1);
  if (row < 0) return;

  auto* item = ui_->table_files->item(row, kColPdfStatus);
  if (!item) {
    item = new QTableWidgetItem();
    ui_->table_files->setItem(row, kColPdfStatus, item);
  }
  if (success) {
    item->setText(tr("已生成"));
    item->setForeground(QColor(Qt::darkGreen));
    const QDateTime now = QDateTime::currentDateTime();
    UpdateLastGenerated(abs_path, now);
    auto* gen_item = ui_->table_files->item(row, kColLastGenerated);
    if (!gen_item) {
      gen_item = new QTableWidgetItem();
      ui_->table_files->setItem(row, kColLastGenerated, gen_item);
    }
    gen_item->setText(now.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
  } else {
    item->setText(tr("失败"));
    item->setForeground(QColor(Qt::red));
  }
}

void SopFileWatch::OnConvertFinished(const QStringList& errors) {
  // Worker and thread will self-delete via deleteLater; null our pointers.
  convert_worker_ = nullptr;
  convert_thread_ = nullptr;

  SetConvertingState(false);

  if (errors.isEmpty()) {
    ui_->label_status->setText(tr("完成 — 所有 PDF 已生成成功"));
    if (isHidden() || isMinimized()) {
      ShowTrayMessage(tr("PDF 生成完成"),
                      tr("所有文件已成功转换为 PDF"));
    }
  } else {
    ui_->label_status->setText(
        tr("完成 — %1 个文件失败").arg(errors.size()));
    QMessageBox::warning(
        this,
        tr("部分失败"),
        tr("以下文件转换失败：\n") + errors.join(QLatin1Char('\n')));
  }

  // Process any files that changed while this conversion was running.
  DrainPendingAutoConvert();
}

void SopFileWatch::OnCancelConvert() {
  if (convert_worker_) {
    convert_worker_->RequestCancel();
    ui_->label_status->setText(tr("正在取消…"));
    ui_->btn_cancel_convert->setEnabled(false);
  }
}

// ---------------------------------------------------------------------------
// Private slots: per-row action button
// ---------------------------------------------------------------------------

void SopFileWatch::OnGeneratePdfForFile(const QString& abs_path) {
  if (is_converting_) return;

  // Look up the entry.
  const auto& entries = watcher_->entries();
  const sop::FileEntry* entry = nullptr;
  for (const auto& e : entries) {
    if (e.abs_path == abs_path) { entry = &e; break; }
  }
  if (!entry) return;

  QString out_dir = OutputDir();
  if (out_dir.isEmpty()) return;

  QVector<sop::PdfConvertWorker::Task> tasks;
  tasks.append({entry->abs_path,
                out_dir + QLatin1Char('/') + entry->pdf_output_name,
                entry->file_name});
  StartConvertWorker(std::move(tasks));
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void SopFileWatch::RefreshTable() {
  ui_->table_files->setUpdatesEnabled(false);
  ui_->table_files->setSortingEnabled(false);
  ui_->table_files->setRowCount(0);
  path_to_row_.clear();

  const auto& entries = watcher_->entries();
  ui_->table_files->setRowCount(entries.size());

  for (int i = 0; i < entries.size(); ++i) {
    const auto& e = entries.at(i);
    path_to_row_[e.abs_path] = i;

    ui_->table_files->setItem(i, kColFileName,
                              new QTableWidgetItem(e.file_name));
    ui_->table_files->setItem(i, kColRelPath,
                              new QTableWidgetItem(e.rel_path));
    ui_->table_files->setItem(
        i, kColLastModified,
        new QTableWidgetItem(
            e.last_modified.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
    const QDateTime last_generated = LastGeneratedFor(e.abs_path);
    ui_->table_files->setItem(
      i, kColLastGenerated,
      new QTableWidgetItem(
        last_generated.isValid()
          ? last_generated.toString(
            QStringLiteral("yyyy-MM-dd HH:mm:ss"))
          : QStringLiteral("-")));
    ui_->table_files->setItem(
        i, kColPdfOutputName,
        new QTableWidgetItem(e.pdf_output_name));

    UpdateRowState(i);
    SetupRowActionButton(i, e.abs_path);
  }

  ui_->table_files->setSortingEnabled(true);
  ui_->table_files->setUpdatesEnabled(true);
}

void SopFileWatch::UpdateRowState(int row) {
  const auto& entries = watcher_->entries();
  if (row < 0 || row >= entries.size()) return;
  const auto& e = entries.at(row);

  // "是否被修改" column.
  auto* changed_item = ui_->table_files->item(row, kColChanged);
  if (!changed_item) {
    changed_item = new QTableWidgetItem();
    ui_->table_files->setItem(row, kColChanged, changed_item);
  }
  if (e.state == sop::FileChangeState::kModified) {
    changed_item->setText(tr("✔ 已修改"));
    changed_item->setForeground(QColor(Qt::red));
    // Also update the modification time column.
    auto* mod_item = ui_->table_files->item(row, kColLastModified);
    if (mod_item) {
      mod_item->setText(
          e.last_modified.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    }
  } else {
    changed_item->setText(tr("无变化"));
    changed_item->setForeground(QColor(Qt::darkGreen));
  }

  // "PDF 状态" column — only create if it doesn't exist yet.
  auto* pdf_item = ui_->table_files->item(row, kColPdfStatus);
  if (!pdf_item) {
    pdf_item = new QTableWidgetItem(tr("未生成"));
    pdf_item->setForeground(QColor(Qt::gray));
    ui_->table_files->setItem(row, kColPdfStatus, pdf_item);
  }
}

void SopFileWatch::SetupRowActionButton(int row, const QString& abs_path) {
  // Embed a QPushButton inside a centred container widget.
  auto* container = new QWidget();
  auto* layout = new QHBoxLayout(container);
  layout->setContentsMargins(2, 2, 2, 2);
  layout->setAlignment(Qt::AlignCenter);

  auto* btn = new QPushButton(tr("生成 PDF"));
  btn->setFixedHeight(22);
  layout->addWidget(btn);
  ui_->table_files->setCellWidget(row, kColAction, container);

  // Capture abs_path (not row index) so sorting cannot invalidate it.
  connect(btn, &QPushButton::clicked, this, [this, abs_path]() {
    OnGeneratePdfForFile(abs_path);
  });
}

QString SopFileWatch::OutputDir() const {
  QString dir = ui_->edit_output_dir->text().trimmed();
  if (dir.isEmpty()) {
    QMessageBox::warning(
        const_cast<SopFileWatch*>(this),
        tr("提示"),
        tr("请先设置 PDF 输出目录"));
    return {};
  }
  QDir().mkpath(dir);
  return dir;
}

void SopFileWatch::ShowTrayMessage(const QString& title, const QString& body,
                                   QSystemTrayIcon::MessageIcon icon,
                                   int duration_ms) {
  if (tray_icon_ && QSystemTrayIcon::supportsMessages()) {
    tray_icon_->showMessage(title, body, icon, duration_ms);
  }
}

void SopFileWatch::StartConvertWorker(
    QVector<sop::PdfConvertWorker::Task> tasks) {
  // Create worker (no parent: it will self-delete via deleteLater).
  convert_worker_ = new sop::PdfConvertWorker(std::move(tasks));
  convert_thread_ = new QThread(this);

  convert_worker_->moveToThread(convert_thread_);

  // Lifecycle wiring.
  connect(convert_thread_, &QThread::started,
          convert_worker_, &sop::PdfConvertWorker::Run);
  connect(convert_worker_, &sop::PdfConvertWorker::Finished,
          convert_thread_, &QThread::quit);
  connect(convert_worker_, &sop::PdfConvertWorker::Finished,
          convert_worker_, &QObject::deleteLater);
  connect(convert_thread_, &QThread::finished,
          convert_thread_, &QObject::deleteLater);

  // Progress + result wiring (auto = queued across threads).
  connect(convert_worker_, &sop::PdfConvertWorker::Progress,
          this, &SopFileWatch::OnConvertProgress);
  connect(convert_worker_, &sop::PdfConvertWorker::TaskDone,
          this, &SopFileWatch::OnConvertTaskDone);
  connect(convert_worker_, &sop::PdfConvertWorker::Finished,
          this, &SopFileWatch::OnConvertFinished);

  SetConvertingState(true);
  convert_thread_->start();
}

void SopFileWatch::SetConvertingState(bool converting) {
  is_converting_ = converting;

  // Progress bar visibility.
  ui_->progress_bar->setVisible(converting);
  ui_->btn_cancel_convert->setVisible(converting);
  ui_->btn_cancel_convert->setEnabled(converting);

  if (!converting) {
    ui_->progress_bar->setValue(0);
  }

  // Disable controls that must not run concurrently.
  ui_->btn_generate_all->setEnabled(!converting);
  ui_->btn_browse_watch_dir->setEnabled(!converting);
  ui_->btn_browse_output_dir->setEnabled(!converting);
  // "开始监控" is only enabled when not watching and not converting.
  ui_->btn_start_watch->setEnabled(!converting && watcher_->root_dir().isEmpty());
  // "停止监控" is only enabled when watching and not converting.
  ui_->btn_stop_watch->setEnabled(!converting && !watcher_->root_dir().isEmpty());

  if (converting) {
    ui_->label_status->setText(tr("准备转换…"));
  }
}

void SopFileWatch::SetupTrayIcon() {
  tray_icon_ = new QSystemTrayIcon(this);
  // Use the application icon; fall back to the standard information icon.
  QIcon app_icon = QApplication::windowIcon();
  if (app_icon.isNull()) {
    app_icon = style()->standardIcon(QStyle::SP_FileDialogDetailedView);
  }
  tray_icon_->setIcon(app_icon);
  tray_icon_->setToolTip(tr("SOP 文件监控"));

  tray_menu_ = new QMenu(this);
  auto* restore_action = tray_menu_->addAction(tr("恢复界面"));
  connect(restore_action, &QAction::triggered, this, [this]() {
    showNormal();
    activateWindow();
    raise();
  });
  tray_menu_->addSeparator();
  auto* quit_action = tray_menu_->addAction(tr("退出"));
  connect(quit_action, &QAction::triggered, qApp, &QApplication::quit);

  tray_icon_->setContextMenu(tray_menu_);
  connect(tray_icon_, &QSystemTrayIcon::activated,
          this, &SopFileWatch::OnTrayActivated);
  tray_icon_->show();
}

// ---------------------------------------------------------------------------
// Auto-convert helper
// ---------------------------------------------------------------------------

void SopFileWatch::DrainPendingAutoConvert() {
  if (pending_auto_convert_.isEmpty()) return;

  QString out_dir = ui_->edit_output_dir->text().trimmed();
  if (out_dir.isEmpty()) {
    pending_auto_convert_.clear();
    return;
  }
  QDir().mkpath(out_dir);

  QVector<sop::PdfConvertWorker::Task> tasks;
  const auto& entries = watcher_->entries();
  for (const auto& e : entries) {
    if (!pending_auto_convert_.contains(e.abs_path)) continue;
    tasks.append({e.abs_path,
                  out_dir + QLatin1Char('/') + e.pdf_output_name,
                  e.file_name});
  }
  pending_auto_convert_.clear();

  if (!tasks.isEmpty()) {
    StartConvertWorker(std::move(tasks));
  }
}

// ---------------------------------------------------------------------------
// Settings persistence
// ---------------------------------------------------------------------------

static QString SettingsPath() {
  return QApplication::applicationDirPath() +
         QStringLiteral("/sop_watch_config.ini");
}

static QString EncodeIniKey(const QString& abs_path) {
  return QString::fromUtf8(
      abs_path.toUtf8().toBase64(QByteArray::Base64UrlEncoding |
                                 QByteArray::OmitTrailingEquals));
}

void SopFileWatch::LoadSettings() {
  QSettings s(SettingsPath(), QSettings::IniFormat);
  s.setIniCodec("UTF-8");  // Support Chinese directory paths.

  const QString watch_dir = s.value(QStringLiteral("paths/watch_dir")).toString();
  const QString output_dir = s.value(QStringLiteral("paths/output_dir")).toString();

  if (!watch_dir.isEmpty())  ui_->edit_watch_dir->setText(watch_dir);
  if (!output_dir.isEmpty()) ui_->edit_output_dir->setText(output_dir);

  LoadLastGenerated(s);

  qDebug() << "[Settings] Loaded from" << SettingsPath();
}

void SopFileWatch::SaveSettings() {
  QSettings s(SettingsPath(), QSettings::IniFormat);
  s.setIniCodec("UTF-8");

  s.setValue(QStringLiteral("paths/watch_dir"),
             ui_->edit_watch_dir->text().trimmed());
  s.setValue(QStringLiteral("paths/output_dir"),
             ui_->edit_output_dir->text().trimmed());
  s.sync();

  qDebug() << "[Settings] Saved to" << SettingsPath();
}

void SopFileWatch::LoadLastGenerated(QSettings& settings) {
  last_generated_map_.clear();
  settings.beginGroup(QStringLiteral("last_generated"));
  const auto keys = settings.childKeys();
  for (const auto& key : keys) {
    const QString iso = settings.value(key).toString();
    QDateTime dt = QDateTime::fromString(iso, Qt::ISODate);
    if (!dt.isValid()) continue;

    const QByteArray decoded = QByteArray::fromBase64(
        key.toUtf8(), QByteArray::Base64UrlEncoding);
    const QString abs_path = QString::fromUtf8(decoded);
    if (abs_path.isEmpty()) continue;

    last_generated_map_.insert(abs_path, dt);
  }
  settings.endGroup();
}

QDateTime SopFileWatch::LastGeneratedFor(const QString& abs_path) const {
  return last_generated_map_.value(abs_path);
}

void SopFileWatch::UpdateLastGenerated(const QString& abs_path,
                                       const QDateTime& when) {
  if (abs_path.isEmpty() || !when.isValid()) return;

  last_generated_map_.insert(abs_path, when);

  QSettings s(SettingsPath(), QSettings::IniFormat);
  s.setIniCodec("UTF-8");
  s.beginGroup(QStringLiteral("last_generated"));
  s.setValue(EncodeIniKey(abs_path), when.toString(Qt::ISODate));
  s.endGroup();
  s.sync();
}
