/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "run_plot_items.h"
#include <QPainter>
#include "qwt_painter.h"
#include "qwt_scale_map.h"
#include "run_set.h"

RunBandItem::RunBandItem()
{
  setZ(1.0);  // above the background colormap item, below the grid and curves
  setItemAttribute(QwtPlotItem::Legend, false);
  setItemAttribute(QwtPlotItem::AutoScale, false);
}

void RunBandItem::draw(QPainter* painter, const QwtScaleMap& xMap, const QwtScaleMap&,
                       const QRectF& canvasRect) const
{
  const double offset = _time_offset ? *_time_offset : 0.0;

  painter->save();
  QFont font = painter->font();
  font.setPointSizeF(std::max(7.0, font.pointSizeF() - 1.0));
  painter->setFont(font);

  for (const auto& run : RunSet::instance().includedRuns())
  {
    double x1 = xMap.transform(run.t_start - offset);
    double x2 = xMap.transform(run.t_end - offset);
    if (x1 > x2)
    {
      std::swap(x1, x2);
    }
    if (x2 < canvasRect.left() || x1 > canvasRect.right())
    {
      continue;
    }

    QColor fill = run.color;
    fill.setAlpha(run.is_reference ? 40 : 26);
    QwtPainter::fillRect(painter, QRectF(x1, canvasRect.top(), x2 - x1, canvasRect.height()),
                         fill);

    QColor edge = run.color;
    edge.setAlpha(140);
    painter->setPen(QPen(edge, 1.0, Qt::DotLine));
    painter->drawLine(QPointF(x1, canvasRect.top()), QPointF(x1, canvasRect.bottom()));
    painter->drawLine(QPointF(x2, canvasRect.top()), QPointF(x2, canvasRect.bottom()));

    const double label_x = std::max(x1, canvasRect.left()) + 3.0;
    if (x2 - label_x > 20.0)
    {
      painter->setPen(run.color);
      painter->drawText(QRectF(label_x, canvasRect.bottom() - 16.0, x2 - label_x, 14.0),
                        Qt::AlignLeft | Qt::AlignVCenter, run.name);
    }
  }
  painter->restore();
}

RunWindowTimeseries::RunWindowTimeseries(const PlotData* source_data, double t_start,
                                         double t_end)
  : QwtTimeseries(&_dst_data)
  , _dst_data(source_data->plotName(), {})
  , _src_data(source_data)
  , _t_start(t_start)
  , _t_end(t_end)
{
  updateCache(true);
}

void RunWindowTimeseries::updateCache(bool)
{
  _dst_data.clear();
  _dst_data.attributes() = _src_data->attributes();

  const int size = static_cast<int>(_src_data->size());
  int index = _src_data->getIndexFromX(_t_start);
  if (index < 0)
  {
    return;
  }
  while (index > 0 && _src_data->at(index - 1).x >= _t_start)
  {
    index--;
  }
  while (index < size && _src_data->at(index).x < _t_start)
  {
    index++;
  }
  for (; index < size; index++)
  {
    const auto& p = _src_data->at(index);
    if (p.x > _t_end)
    {
      break;
    }
    _dst_data.pushBack({ p.x - _t_start, p.y });
  }
}
