/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "plot_cursors.h"

#include <algorithm>
#include <cmath>
#include <QDateTime>
#include <QPen>
#include <QSettings>

#include "qwt_plot.h"
#include "qwt_plot_marker.h"
#include "qwt_plot_textlabel.h"
#include "qwt_scale_map.h"
#include "qwt_text.h"

namespace
{

const QColor kColorX(230, 126, 34);  // orange: vertical lines
const QColor kColorY(155, 89, 182);  // purple: horizontal lines
constexpr double kLineZ = 40.0;
constexpr double kReadoutZ = 41.0;

bool darkTheme()
{
  QSettings settings;
  return settings.value("StyleSheet::theme", "light").toString() == "dark";
}

int precision()
{
  QSettings settings;
  return settings.value("Preferences::precision", 3).toInt();
}

QString formatValue(double value, int prec)
{
  return QString::number(value, 'f', prec);
}

QString formatDateTime(double seconds)
{
  return QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(std::llround(seconds * 1000.0)))
      .toString("hh:mm:ss.zzz");
}

QwtText styledText(const QString& str, const QColor& color, bool dark)
{
  QwtText text(str);
  QFont font;
  font.setPointSize(9);
  text.setFont(font);
  text.setColor(color);
  text.setBackgroundBrush(QBrush(dark ? QColor(30, 30, 30, 210) : QColor(255, 255, 255, 220)));
  text.setBorderRadius(2);
  return text;
}

}  // namespace

PlotCursors::PlotCursors(QwtPlot* plot) : _plot(plot)
{
  _readout = new QwtPlotTextLabel();
  _readout->setZ(kReadoutZ);
  _readout->setMargin(6);
  _readout->attach(_plot);
  _readout->setVisible(false);
}

PlotCursors::~PlotCursors()
{
  // Must run while the QwtPlot is still alive (PlotWidget destroys this member
  // before its child QwtPlot), otherwise the plot would delete the items too.
  for (auto& line : _lines)
  {
    line.marker->detach();
    delete line.marker;
  }
  _readout->detach();
  delete _readout;
}

void PlotCursors::setVisible(bool visible)
{
  _visible = visible;
  for (auto& line : _lines)
  {
    line.marker->setVisible(visible);
  }
  refresh();
}

void PlotCursors::setXDateTime(bool enable)
{
  if (_x_date_time != enable)
  {
    _x_date_time = enable;
    refresh();
  }
}

void PlotCursors::shiftX(double delta)
{
  bool changed = false;
  for (auto& line : _lines)
  {
    if (line.axis == AXIS_X)
    {
      line.value += delta;
      applyValue(line);
      changed = true;
    }
  }
  if (changed)
  {
    refresh();
  }
}

bool PlotCursors::canAdd(Axis axis) const
{
  return freeSlot(axis) > 0;
}

const PlotCursors::Line* PlotCursors::find(Axis axis, int slot) const
{
  for (const auto& line : _lines)
  {
    if (line.axis == axis && line.slot == slot)
    {
      return &line;
    }
  }
  return nullptr;
}

int PlotCursors::freeSlot(Axis axis) const
{
  for (int slot = 1; slot <= kMaxPerAxis; slot++)
  {
    if (!find(axis, slot))
    {
      return slot;
    }
  }
  return -1;
}

int PlotCursors::add(Axis axis, double value)
{
  const int slot = freeSlot(axis);
  if (slot < 0 || !std::isfinite(value))
  {
    return -1;
  }
  return insert(axis, slot, value);
}

void PlotCursors::applyValue(Line& line)
{
  if (line.axis == AXIS_X)
  {
    line.marker->setXValue(line.value);
  }
  else
  {
    line.marker->setYValue(line.value);
  }
}

int PlotCursors::insert(Axis axis, int slot, double value)
{
  auto marker = new QwtPlotMarker();
  const QColor color = (axis == AXIS_X) ? kColorX : kColorY;
  marker->setLinePen(QPen(color, 1.5, Qt::DashLine));
  marker->setRenderHint(QwtPlotItem::RenderAntialiased, true);
  marker->setAxes(QwtPlot::xBottom, QwtPlot::yLeft);
  if (axis == AXIS_X)
  {
    marker->setLineStyle(QwtPlotMarker::VLine);
    marker->setLabelAlignment(Qt::AlignTop | Qt::AlignRight);
  }
  else
  {
    marker->setLineStyle(QwtPlotMarker::HLine);
    marker->setLabelAlignment(Qt::AlignTop | Qt::AlignLeft);
  }
  marker->setSpacing(4);
  marker->setZ(kLineZ);
  marker->setVisible(_visible);
  marker->attach(_plot);

  _lines.push_back({ axis, slot, value, marker });
  applyValue(_lines.back());
  // Keep a stable order: X1, X2, Y1, Y2.
  std::sort(_lines.begin(), _lines.end(), [](const Line& a, const Line& b) {
    return a.axis != b.axis ? a.axis < b.axis : a.slot < b.slot;
  });
  refresh();

  for (int i = 0; i < count(); i++)
  {
    if (_lines[i].axis == axis && _lines[i].slot == slot)
    {
      return i;
    }
  }
  return -1;
}

void PlotCursors::remove(int index)
{
  if (index < 0 || index >= count())
  {
    return;
  }
  _lines[index].marker->detach();
  delete _lines[index].marker;
  _lines.erase(_lines.begin() + index);
  refresh();
}

void PlotCursors::clear()
{
  for (auto& line : _lines)
  {
    line.marker->detach();
    delete line.marker;
  }
  _lines.clear();
  refresh();
}

PlotCursors::Axis PlotCursors::axis(int index) const
{
  return _lines.at(index).axis;
}

double PlotCursors::value(int index) const
{
  return _lines.at(index).value;
}

void PlotCursors::setValue(int index, double value)
{
  if (index < 0 || index >= count() || !std::isfinite(value))
  {
    return;
  }
  _lines[index].value = value;
  applyValue(_lines[index]);
  refresh();
}

QString PlotCursors::name(int index) const
{
  const auto& line = _lines.at(index);
  return QString("%1%2").arg(line.axis == AXIS_X ? "X" : "Y").arg(line.slot);
}

int PlotCursors::hitTest(const QPoint& canvas_pos, int tolerance_px) const
{
  if (!_visible)
  {
    return -1;
  }
  const QwtScaleMap x_map = _plot->canvasMap(QwtPlot::xBottom);
  const QwtScaleMap y_map = _plot->canvasMap(QwtPlot::yLeft);

  int best = -1;
  double best_dist = tolerance_px + 0.5;
  for (int i = 0; i < count(); i++)
  {
    const auto& line = _lines[i];
    const double dist = (line.axis == AXIS_X) ?
                            std::abs(x_map.transform(line.value) - canvas_pos.x()) :
                            std::abs(y_map.transform(line.value) - canvas_pos.y());
    if (dist < best_dist)
    {
      best_dist = dist;
      best = i;
    }
  }
  return best;
}

double PlotCursors::valueAt(Axis axis, const QPoint& canvas_pos) const
{
  return (axis == AXIS_X) ? _plot->invTransform(QwtPlot::xBottom, canvas_pos.x()) :
                            _plot->invTransform(QwtPlot::yLeft, canvas_pos.y());
}

void PlotCursors::refresh()
{
  const bool dark = darkTheme();
  const int prec = precision();

  for (int i = 0; i < count(); i++)
  {
    const auto& line = _lines[i];
    const QColor color = (line.axis == AXIS_X) ? kColorX : kColorY;
    const QString value_str = (line.axis == AXIS_X && _x_date_time) ?
                                  formatDateTime(line.value) :
                                  formatValue(line.value, prec);
    line.marker->setLabel(styledText(QString("%1 = %2").arg(name(i), value_str), color, dark));
  }

  QStringList rows;
  const Line* x1 = find(AXIS_X, 1);
  const Line* x2 = find(AXIS_X, 2);
  if (x1 && x2)
  {
    const double dx = x2->value - x1->value;
    rows << QString::fromUtf8("ΔX = %1").arg(formatValue(dx, prec));
    if (std::abs(dx) > 0.0)
    {
      rows << QString::fromUtf8("1/ΔX = %1").arg(formatValue(1.0 / std::abs(dx), prec));
    }
  }
  const Line* y1 = find(AXIS_Y, 1);
  const Line* y2 = find(AXIS_Y, 2);
  if (y1 && y2)
  {
    rows << QString::fromUtf8("ΔY = %1").arg(formatValue(y2->value - y1->value, prec));
  }

  if (rows.isEmpty() || !_visible)
  {
    _readout->setVisible(false);
    return;
  }

  QwtText text =
      styledText(rows.join("\n"), dark ? QColor(230, 230, 230) : QColor(30, 30, 30), dark);
  text.setRenderFlags(Qt::AlignBottom | Qt::AlignRight);
  text.setBorderPen(QPen(dark ? QColor(120, 120, 120) : QColor(160, 160, 160)));
  _readout->setText(text);
  _readout->setVisible(true);
}

void PlotCursors::xmlSave(QDomDocument& doc, QDomElement& parent) const
{
  if (_lines.empty())
  {
    return;
  }
  QDomElement lines_el = doc.createElement("reference_lines");
  for (const auto& line : _lines)
  {
    QDomElement line_el = doc.createElement("line");
    line_el.setAttribute("axis", line.axis == AXIS_X ? "x" : "y");
    line_el.setAttribute("slot", line.slot);
    line_el.setAttribute("value", QString::number(line.value, 'g', 17));
    lines_el.appendChild(line_el);
  }
  parent.appendChild(lines_el);
}

void PlotCursors::xmlLoad(const QDomElement& parent)
{
  clear();
  QDomElement lines_el = parent.firstChildElement("reference_lines");
  for (QDomElement line_el = lines_el.firstChildElement("line"); !line_el.isNull();
       line_el = line_el.nextSiblingElement("line"))
  {
    const Axis axis = (line_el.attribute("axis") == "y") ? AXIS_Y : AXIS_X;
    bool ok = false;
    const double value = line_el.attribute("value").toDouble(&ok);
    if (!ok || !std::isfinite(value))
    {
      continue;
    }
    int slot = line_el.attribute("slot").toInt();
    if (slot < 1 || slot > kMaxPerAxis || find(axis, slot))
    {
      slot = freeSlot(axis);
    }
    if (slot > 0)
    {
      insert(axis, slot, value);
    }
  }
}
