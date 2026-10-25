/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef RUN_PLOT_ITEMS_H
#define RUN_PLOT_ITEMS_H

#include "qwt_plot_item.h"
#include "timeseries_qwt.h"

// Translucent background bands for the included runs on a normal time-series plot.
class RunBandItem : public QwtPlotItem
{
public:
  RunBandItem();

  void setTimeOffset(const double* time_offset)
  {
    _time_offset = time_offset;
  }

  int rtti() const override
  {
    return QwtPlotItem::Rtti_PlotZone;
  }

  void draw(QPainter* painter, const QwtScaleMap& xMap, const QwtScaleMap& yMap,
            const QRectF& canvasRect) const override;

private:
  const double* _time_offset = nullptr;
};

// A view of one source series restricted to [t_start, t_end] and re-zeroed so
// x = 0 is the run start. Used by Compare tabs, one instance per run per signal.
class RunWindowTimeseries : public QwtTimeseries
{
public:
  RunWindowTimeseries(const PlotData* source_data, double t_start, double t_end);

  void updateCache(bool reset_old_data) override;

  const PlotData* sourceData() const
  {
    return _src_data;
  }

private:
  PlotData _dst_data;
  const PlotData* _src_data;
  double _t_start;
  double _t_end;
};

#endif  // RUN_PLOT_ITEMS_H
