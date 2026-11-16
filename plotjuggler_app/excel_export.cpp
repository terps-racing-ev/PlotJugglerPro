/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "excel_export.h"
#include "plot_docker.h"
#include "plotwidget.h"
#include "xlsx_writer.h"

#include <QDateTime>
#include <QFileInfo>
#include <cmath>
#include <limits>

#include "qwt_plot_curve.h"
#include "qwt_text.h"

namespace
{
bool isExportablePlot(PlotWidget* plot)
{
  return plot && !plot->isXYPlot() && !plot->isEmpty();
}

QString plotSheetName(PlotDocker* docker, int index, int sheet_number)
{
  auto* dock = dynamic_cast<DockWidget*>(docker->dockArea(index)->currentDockWidget());
  const QString title = dock ? dock->name().trimmed() : QString();
  if (title.isEmpty() || title == "...")
  {
    return QString("Plot %1").arg(sheet_number);
  }
  return title;
}

// Index of the last sample with x <= t, or -1 if every sample is after t.
long lastSampleAtOrBefore(const QwtSeriesData<QPointF>* series, double t)
{
  long lo = 0;
  long hi = static_cast<long>(series->size());
  while (lo < hi)
  {
    const long mid = (lo + hi) / 2;
    if (series->sample(static_cast<size_t>(mid)).x() <= t)
    {
      lo = mid + 1;
    }
    else
    {
      hi = mid;
    }
  }
  return lo - 1;
}
}  // namespace

namespace PJ
{

std::optional<Range> excelExportRange(PlotDocker* docker)
{
  for (int index = 0; index < docker->plotCount(); index++)
  {
    if (auto* plot = docker->plotAt(index); isExportablePlot(plot))
    {
      return plot->currentTimeViewport();
    }
  }
  return std::nullopt;
}

int excelExportSheetCount(PlotDocker* docker)
{
  int count = 0;
  for (int index = 0; index < docker->plotCount(); index++)
  {
    if (isExportablePlot(docker->plotAt(index)))
    {
      count++;
    }
  }
  return count;
}

qint64 excelExportRowCount(Range range, double sample_rate_hz)
{
  if (sample_rate_hz <= 0 || range.max < range.min)
  {
    return 0;
  }
  return static_cast<qint64>(std::floor((range.max - range.min) * sample_rate_hz + 1e-9)) + 1;
}

QByteArray buildExcelWorkbook(PlotDocker* docker, double sample_rate_hz,
                              const QStringList& source_files, QString* error)
{
  const auto range = excelExportRange(docker);
  if (!range)
  {
    *error = QObject::tr("This tab has no time-series plots to export.");
    return {};
  }
  const qint64 rows = excelExportRowCount(*range, sample_rate_hz);
  if (rows <= 0)
  {
    *error = QObject::tr("The sample rate must be greater than zero.");
    return {};
  }
  if (rows >= XlsxWriter::MAX_ROWS)
  {
    *error = QObject::tr("%1 rows exceeds Excel's limit of %2. Zoom in or lower the sample rate.")
                 .arg(rows)
                 .arg(XlsxWriter::MAX_ROWS - 1);
    return {};
  }

  const double nan = std::numeric_limits<double>::quiet_NaN();
  XlsxWriter writer;
  int sheet_number = 0;

  for (int index = 0; index < docker->plotCount(); index++)
  {
    PlotWidget* plot = docker->plotAt(index);
    if (!isExportablePlot(plot))
    {
      continue;
    }

    QStringList header = { QObject::tr("Time (s)") };
    std::vector<const QwtSeriesData<QPointF>*> series;
    for (const auto& info : plot->curveList())
    {
      header.push_back(info.curve->title().text());
      series.push_back(info.curve->data());
    }

    const int sheet = writer.addSheet(plotSheetName(docker, index, ++sheet_number), header);

    std::vector<long> cursor(series.size());
    for (size_t c = 0; c < series.size(); c++)
    {
      cursor[c] = lastSampleAtOrBefore(series[c], range->min);
    }

    std::vector<double> row(series.size() + 1);
    for (qint64 k = 0; k < rows; k++)
    {
      const double t = range->min + static_cast<double>(k) / sample_rate_hz;
      row[0] = t;
      for (size_t c = 0; c < series.size(); c++)
      {
        const auto* data = series[c];
        const long size = static_cast<long>(data->size());
        while (cursor[c] + 1 < size && data->sample(static_cast<size_t>(cursor[c] + 1)).x() <= t)
        {
          cursor[c]++;
        }
        // Hold the last value only inside the curve's own data span.
        const bool in_span =
            cursor[c] >= 0 && t <= data->sample(static_cast<size_t>(size - 1)).x();
        row[c + 1] = in_span ? data->sample(static_cast<size_t>(cursor[c])).y() : nan;
      }
      writer.appendRow(sheet, row);
    }
  }

  QStringList source_names;
  for (const QString& path : source_files)
  {
    source_names.push_back(QFileInfo(path).fileName());
  }

  const int info = writer.addSheet("Info", { QObject::tr("Field"), QObject::tr("Value") });
  writer.appendRow(info, { QObject::tr("Tab"), docker->name() });
  writer.appendRow(info, { QObject::tr("Source files"), source_names.join(", ") });
  writer.appendRow(info, { QObject::tr("Exported at"),
                           QDateTime::currentDateTime().toString(Qt::ISODate) });
  writer.appendRow(info, { QObject::tr("Time column"),
                           docker->isCompareMode() ? QObject::tr("Seconds since run start") :
                                                     QObject::tr("Seconds, as on the plot X axis") });
  writer.appendRow(info, { QObject::tr("Range start (s)"), QString::number(range->min, 'g', 12) });
  writer.appendRow(info, { QObject::tr("Range end (s)"), QString::number(range->max, 'g', 12) });
  writer.appendRow(info, { QObject::tr("Sample rate (Hz)"), QString::number(sample_rate_hz) });
  writer.appendRow(info, { QObject::tr("Resampling"),
                           QObject::tr("Last known value; blank outside a signal's data") });

  return writer.toByteArray();
}

}  // namespace PJ
