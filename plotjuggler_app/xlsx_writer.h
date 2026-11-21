/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef XLSX_WRITER_H
#define XLSX_WRITER_H

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <vector>

namespace PJ
{

// Minimal data-only .xlsx writer: one header row per sheet, numeric or string
// cells, frozen bold header. Uses Qt's bundled zlib, so no extra dependency.
class XlsxWriter
{
public:
  // Excel's hard limit, including the header row.
  static constexpr qint64 MAX_ROWS = 1048576;

  // Returns the index of the new sheet. The name is sanitized and made unique.
  int addSheet(const QString& name, const QStringList& header);

  void appendRow(int sheet, const QStringList& cells);

  // NaN values are written as empty cells.
  void appendRow(int sheet, const std::vector<double>& cells);

  QByteArray toByteArray() const;

private:
  struct Sheet
  {
    QString name;
    int columns = 0;
    qint64 rows = 0;
    QByteArray data;
  };
  std::vector<Sheet> _sheets;

  void appendStringRow(Sheet& sheet, const QStringList& cells, bool bold);
};

}  // namespace PJ

#endif  // XLSX_WRITER_H
