/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef EXCEL_EXPORT_H
#define EXCEL_EXPORT_H

#include <QByteArray>
#include <QStringList>
#include <optional>

#include "PlotJuggler/plotdatabase.h"

class PlotDocker;

namespace PJ
{

// Visible X range of the tab's time-series plots, in plot coordinates (the
// values shown on the X axis). Empty when the tab has no time-series curves.
std::optional<Range> excelExportRange(PlotDocker* docker);

int excelExportSheetCount(PlotDocker* docker);

qint64 excelExportRowCount(Range range, double sample_rate_hz);

// One sheet per time-series plot: a shared time column resampled at
// sample_rate_hz over the visible range, plus one column per curve holding its
// last known value. XY plots and map panels are skipped. Ends with an Info sheet.
QByteArray buildExcelWorkbook(PlotDocker* docker, double sample_rate_hz,
                              const QStringList& source_files, QString* error);

}  // namespace PJ

#endif  // EXCEL_EXPORT_H
