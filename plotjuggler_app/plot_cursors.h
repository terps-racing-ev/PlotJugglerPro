/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef PLOT_CURSORS_H
#define PLOT_CURSORS_H

#include <vector>
#include <QDomDocument>
#include <QDomElement>
#include <QPoint>
#include <QString>

class QwtPlot;
class QwtPlotMarker;
class QwtPlotTextLabel;

// Oscilloscope-style reference lines for time-series plots: up to two vertical
// lines (X1, X2) and two horizontal lines (Y1, Y2) on the left Y axis. Each line
// is labelled with its value, and a readout shows ΔX, 1/ΔX and ΔY when both
// lines of a pair exist. Values are in plot coordinates (X relative to the
// current time offset).
class PlotCursors
{
public:
  enum Axis
  {
    AXIS_X,  // vertical line at a fixed X (time) value
    AXIS_Y   // horizontal line at a fixed Y value
  };

  static constexpr int kMaxPerAxis = 2;

  explicit PlotCursors(QwtPlot* plot);
  ~PlotCursors();

  PlotCursors(const PlotCursors&) = delete;
  PlotCursors& operator=(const PlotCursors&) = delete;

  void setVisible(bool visible);

  // Format X labels as wall-clock time (matches the date-time X axis).
  void setXDateTime(bool enable);

  // Move every X line by `delta` (used when the time offset changes, so the
  // lines stay pinned to the same data).
  void shiftX(double delta);

  bool empty() const
  {
    return _lines.empty();
  }
  int count() const
  {
    return static_cast<int>(_lines.size());
  }
  bool canAdd(Axis axis) const;

  // Returns the index of the new line, or -1 if that axis is full.
  int add(Axis axis, double value);
  void remove(int index);
  void clear();

  Axis axis(int index) const;
  double value(int index) const;
  void setValue(int index, double value);
  QString name(int index) const;

  // Index of the line within `tolerance_px` of a canvas position, or -1.
  int hitTest(const QPoint& canvas_pos, int tolerance_px = 5) const;

  // Value under a canvas position for the given axis.
  double valueAt(Axis axis, const QPoint& canvas_pos) const;

  // Re-reads theme/precision settings and refreshes labels and readout.
  void refresh();

  void xmlSave(QDomDocument& doc, QDomElement& parent) const;
  void xmlLoad(const QDomElement& parent);

private:
  struct Line
  {
    Axis axis;
    int slot;  // 1 or 2
    double value;
    QwtPlotMarker* marker;
  };

  QwtPlot* _plot;
  std::vector<Line> _lines;
  QwtPlotTextLabel* _readout;
  bool _visible = false;
  bool _x_date_time = false;

  const Line* find(Axis axis, int slot) const;
  int freeSlot(Axis axis) const;
  int insert(Axis axis, int slot, double value);
  void applyValue(Line& line);
};

#endif  // PLOT_CURSORS_H
