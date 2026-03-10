// Copyright (c) 2026 SopFileWatch Authors. All rights reserved.
// Use of this source code is governed by a MIT-style license.
//
// file_name_helper.cpp

#include "stdafx.h"
#include "file_name_helper.h"

#include <QFileInfo>
#include <QRegularExpression>

namespace sop {

QString ExtractAsciiPdfName(const QString& source_name) {
  // Strip directory components and extension.
  QFileInfo info(source_name);
  QString base = info.baseName();

  // Keep only ASCII letters (A-Z, a-z) and digits (0-9).
  QString result;
  result.reserve(base.size());
  for (const QChar& ch : base) {
    if ((ch >= QLatin1Char('A') && ch <= QLatin1Char('Z')) ||
        (ch >= QLatin1Char('a') && ch <= QLatin1Char('z')) ||
        (ch >= QLatin1Char('0') && ch <= QLatin1Char('9'))) {
      result.append(ch);
    }
  }

  if (result.isEmpty()) {
    // Fallback: use "output" if nothing extractable.
    result = QStringLiteral("output");
  }

  result.append(QStringLiteral(".pdf"));
  return result;
}

}  // namespace sop
