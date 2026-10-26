/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "map_dock_panel.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSizePolicy>
#include <QSettings>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include "map_tile_widget.h"
#include "PlotJuggler/svg_util.h"
#include "run_set.h"

namespace
{
QObject* findDockWidget(QObject* object)
{
  QObject* current = object;
  while (current)
  {
    if (current->inherits("DockWidget"))
    {
      return current;
    }
    current = current->parent();
  }
  return nullptr;
}
}

MapDockPanel::MapDockPanel(PJ::PlotDataMapRef& plot_data, QWidget* parent)
  : QWidget(parent), _plot_data(plot_data)
{
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(6, 6, 6, 6);
  layout->setSpacing(6);

  auto* row = new QHBoxLayout();
  row->setSpacing(6);

  auto* lat_label = new QLabel("Lat", this);
  _lat_combo = new QComboBox(this);
  _lat_combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  _lat_combo->setMinimumContentsLength(8);
  _lat_combo->setMinimumWidth(70);
  _lat_combo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);

  auto* lon_label = new QLabel("Lon", this);
  _lon_combo = new QComboBox(this);
  _lon_combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  _lon_combo->setMinimumContentsLength(8);
  _lon_combo->setMinimumWidth(70);
  _lon_combo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);

  auto* fit_button = new QPushButton("Fit to View", this);

  {
    QSettings settings;
    const QString theme = settings.value("StyleSheet::theme", "light").toString();
    fit_button->setIcon(LoadSvg(":/resources/svg/zoom_max.svg", theme));
  }

  row->addWidget(fit_button);
  row->addWidget(lat_label);
  row->addWidget(_lat_combo, 1);
  row->addWidget(lon_label);
  row->addWidget(_lon_combo, 1);

  auto* trail_row = new QHBoxLayout();
  trail_row->setSpacing(6);

  auto* trail_label = new QLabel("Trail", this);
  _trail_length_slider = new QSlider(Qt::Horizontal, this);
  _trail_length_slider->setRange(0, 60);
  _trail_length_slider->setSingleStep(1);
  _trail_length_slider->setPageStep(10);
  _trail_length_slider->setTickInterval(10);
  _trail_length_slider->setTickPosition(QSlider::TicksBelow);
  _trail_length_slider->setValue(0);
  _trail_length_label = new QLabel(this);
  _trail_length_label->setMinimumWidth(68);

  trail_row->addWidget(trail_label);
  trail_row->addWidget(_trail_length_slider, 1);
  trail_row->addWidget(_trail_length_label);

  _status_label = new QLabel(this);
  _status_label->setTextInteractionFlags(Qt::TextSelectableByMouse);

  layout->addLayout(row);
  layout->addLayout(trail_row);
  layout->addWidget(_status_label);

  _map_widget = new MapTileWidget(this);
  QString tile_url = qEnvironmentVariable("PJ_MAP_TILES_URL");
  QString tile_attribution = qEnvironmentVariable("PJ_MAP_ATTRIBUTION");
  const bool using_default_tiles = tile_url.isEmpty();
  if (using_default_tiles)
  {
    tile_url = QStringLiteral(
        "https://basemap.nationalmap.gov/arcgis/rest/services/USGSImageryTopo/"
        "MapServer/tile/{z}/{y}/{x}");
  }
  if (tile_attribution.isEmpty())
  {
    tile_attribution = QStringLiteral("USGS The National Map");
  }
  int tile_max_zoom = qEnvironmentVariableIntValue("PJ_MAP_MAX_ZOOM");
  if (tile_max_zoom <= 0)
  {
    tile_max_zoom = using_default_tiles ? 16 : 19;
  }
  _map_widget->setTileSource(tile_url, tile_attribution, tile_max_zoom,
                             std::max(tile_max_zoom, 21));
  connect(_map_widget, &MapTileWidget::contextMenuRequested, this,
          &MapDockPanel::showContextMenu);
  layout->addWidget(_map_widget, 1);

  connect(_lat_combo, &QComboBox::currentTextChanged, this,
          [this](const QString&) { selectionChanged(); });
  connect(_lon_combo, &QComboBox::currentTextChanged, this,
          [this](const QString&) { selectionChanged(); });
  connect(fit_button, &QPushButton::clicked, this, [this]() {
    loadDetectedCurves();
    selectionChanged();
    zoomToRoute();
  });
  connect(_trail_length_slider, &QSlider::valueChanged, this, [this](int) {
    updateTrailLengthLabel();
    selectionChanged();
  });

  setStatus("Select latitude and longitude curves");

  connect(&RunSet::instance(), &RunSet::runsChanged, this, [this]() {
    if (_compare_mode)
    {
      _route_dirty = true;
      updateRouteOnMap();
    }
  });

  refreshCurveCombos();
  updateTrailLengthLabel();
}

void MapDockPanel::loadDetectedCurves()
{
  refreshCurveCombos();
  autoDetectCurves(/*force_overwrite=*/true);
  requestSelectedCurveLoad();
  refreshCurveCombos();
}

void MapDockPanel::refreshAfterDataReload()
{
  refreshCurveCombos();
  requestSelectedCurveLoad();
  refreshCurveCombos();
}

QDomElement MapDockPanel::xmlSaveState(QDomDocument& doc) const
{
  auto el = doc.createElement("map_panel");
  el.setAttribute("lat_curve", _lat_combo->currentText());
  el.setAttribute("lon_curve", _lon_combo->currentText());
  el.setAttribute("trail_length_sec", _trail_length_slider->value());
  return el;
}

bool MapDockPanel::xmlLoadState(const QDomElement& element)
{
  _pending_lat_curve = element.attribute("lat_curve");
  _pending_lon_curve = element.attribute("lon_curve");
  if (element.hasAttribute("trail_length_sec"))
  {
    _trail_length_slider->setValue(element.attribute("trail_length_sec").toInt());
    updateTrailLengthLabel();
  }
  applyPendingSelection();
  selectionChanged();
  return true;
}

void MapDockPanel::setCompareMode(bool enable)
{
  if (_compare_mode == enable)
  {
    return;
  }
  _compare_mode = enable;
  // Absolute and run-relative times are not interchangeable.
  _has_time = false;
  _last_time = 0.0;
  applyCompareModeToMap();
  selectionChanged();
}

void MapDockPanel::onRunTimeUpdated(double run_time)
{
  if (!_compare_mode)
  {
    return;
  }
  _has_time = true;
  _last_time = run_time;
  _route_dirty = true;
  updateRouteOnMap();
}

void MapDockPanel::onTimeUpdated(double absolute_time)
{
  if (_compare_mode)
  {
    return;
  }
  _has_time = true;
  _last_time = absolute_time;
  _route_dirty = true;
  updateMarkerOnMap(absolute_time);
}

void MapDockPanel::refreshCurveCombos()
{
  const auto current_lat = _lat_combo->currentText();
  const auto current_lon = _lon_combo->currentText();

  QStringList series_names;
  series_names.reserve(static_cast<int>(_plot_data.numeric.size()));

  for (const auto& [name, _] : _plot_data.numeric)
  {
    series_names.push_back(QString::fromStdString(name));
  }

  std::sort(series_names.begin(), series_names.end(),
            [](const QString& a, const QString& b) { return a.toLower() < b.toLower(); });

  {
    const QSignalBlocker b1(_lat_combo);
    const QSignalBlocker b2(_lon_combo);
    _lat_combo->clear();
    _lon_combo->clear();
    _lat_combo->addItems(series_names);
    _lon_combo->addItems(series_names);
  }

  auto restoreCombo = [](QComboBox* combo, const QString& value) {
    if (value.isEmpty())
    {
      return;
    }
    auto idx = combo->findText(value);
    if (idx >= 0)
    {
      combo->setCurrentIndex(idx);
    }
  };

  restoreCombo(_lat_combo, current_lat);
  restoreCombo(_lon_combo, current_lon);

  applyPendingSelection();

  autoDetectCurves(/*force_overwrite=*/false);

  selectionChanged();
}

void MapDockPanel::autoDetectCurves(bool force_overwrite)
{
  QString latitude_curve;
  QString longitude_curve;

  for (int i = 0; i < _lat_combo->count(); i++)
  {
    const QString name = _lat_combo->itemText(i);

    if (latitude_curve.isEmpty() && name.contains("Latitude", Qt::CaseInsensitive))
    {
      latitude_curve = name;
    }

    if (longitude_curve.isEmpty() && name.contains("Longitude", Qt::CaseInsensitive))
    {
      longitude_curve = name;
    }

    if (!latitude_curve.isEmpty() && !longitude_curve.isEmpty())
    {
      break;
    }
  }

  if (!latitude_curve.isEmpty())
  {
    if (force_overwrite || _lat_combo->currentText().isEmpty() ||
        !_lat_combo->currentText().contains("Latitude", Qt::CaseInsensitive))
    {
      _lat_combo->setCurrentText(latitude_curve);
    }
  }

  if (!longitude_curve.isEmpty())
  {
    if (force_overwrite || _lon_combo->currentText().isEmpty() ||
        !_lon_combo->currentText().contains("Longitude", Qt::CaseInsensitive))
    {
      _lon_combo->setCurrentText(longitude_curve);
    }
  }
}

void MapDockPanel::requestSelectedCurveLoad()
{
  QStringList curve_names;

  auto addIfValid = [&](const QString& curve_name) {
    if (curve_name.isEmpty() || curve_names.contains(curve_name))
    {
      return;
    }
    if (_plot_data.numeric.find(curve_name.toStdString()) != _plot_data.numeric.end())
    {
      curve_names.push_back(curve_name);
    }
  };

  addIfValid(_lat_combo->currentText());
  addIfValid(_lon_combo->currentText());

  if (!curve_names.isEmpty())
  {
    emit curvesLoadRequested(curve_names);
  }
}

void MapDockPanel::selectionChanged()
{
  _fit_route_once = true;
  _route_dirty = true;
  updateRouteOnMap();
  if (_has_time)
  {
    updateMarkerOnMap(_last_time);
  }
}

void MapDockPanel::zoomToRoute()
{
  if (_map_widget)
  {
    _map_widget->fitToRoute();
  }
}

void MapDockPanel::showContextMenu(const QPoint& pos)
{
  if (!_map_widget)
  {
    return;
  }

  QMenu menu(this);
  menu.setStyleSheet("QMenu::icon { width: 12px; }");
  auto* fit_action = menu.addAction("Fit to View");
  menu.addSeparator();
  auto* split_v_action = menu.addAction("Add Time-Series Chart");
  auto* split_map_action = menu.addAction("Add Map View");
  // Compare tabs do not offer XY plots.
  auto* split_h_action = _compare_mode ? nullptr : menu.addAction("Add X/Y Panel");

  {
    QSettings settings;
    const QString theme = settings.value("StyleSheet::theme", "light").toString();
    fit_action->setIcon(LoadSvg(":/resources/svg/zoom_max.svg", theme));
    if (split_h_action)
    {
      split_h_action->setIcon(LoadSvg(":/resources/svg/grid.svg", theme));
    }
    split_v_action->setIcon(LoadSvg(":/resources/svg/colored_charts.svg", theme));
    split_map_action->setIcon(LoadSvg(":/resources/svg/scatter.svg", theme));
  }

  auto* selected_action = menu.exec(_map_widget->mapToGlobal(pos));
  if (!selected_action)
  {
    return;
  }

  if (selected_action == fit_action)
  {
    loadDetectedCurves();
    selectionChanged();
    zoomToRoute();
    return;
  }

  QObject* dock = findDockWidget(this);
  if (!dock)
  {
    return;
  }

  if (split_h_action && selected_action == split_h_action)
  {
    QMetaObject::invokeMethod(dock, "splitHorizontal");
  }
  else if (selected_action == split_v_action)
  {
    QMetaObject::invokeMethod(dock, "splitVertical");
  }
  else if (selected_action == split_map_action)
  {
    QMetaObject::invokeMethod(dock, "createMapPanelSplit");
  }
}

void MapDockPanel::applyPendingSelection()
{
  auto setIfFound = [](QComboBox* combo, const QString& target) {
    if (target.isEmpty())
    {
      return;
    }
    const auto idx = combo->findText(target);
    if (idx >= 0)
    {
      combo->setCurrentIndex(idx);
    }
  };

  setIfFound(_lat_combo, _pending_lat_curve);
  setIfFound(_lon_combo, _pending_lon_curve);

  _pending_lat_curve.clear();
  _pending_lon_curve.clear();
}

void MapDockPanel::updateTrailLengthLabel()
{
  const int seconds = _trail_length_slider->value();
  if (seconds <= 0)
  {
    _trail_length_label->setText("All past");
    return;
  }

  _trail_length_label->setText(QString("%1 s").arg(seconds));
}

void MapDockPanel::updateRouteOnMap()
{
  if (!_map_widget)
  {
    return;
  }
  if (_compare_mode)
  {
    updateRunsOnMap();
    return;
  }

  const PJ::PlotData* lat_series = nullptr;
  const PJ::PlotData* lon_series = nullptr;
  if (!selectedSeries(lat_series, lon_series))
  {
    setStatus("Select valid latitude/longitude series");
    _map_widget->setRoute({}, {}, false);
    return;
  }

  QVector<QPointF> points;
  QVector<QPointF> fit_points;
  const auto sample_count = std::min(lat_series->size(), lon_series->size());
  points.reserve(static_cast<int>(sample_count));
  fit_points.reserve(static_cast<int>(sample_count));

  const double max_time =
      _has_time ? _last_time : std::numeric_limits<double>::infinity();
  const int trail_length_sec = _trail_length_slider->value();
  const double min_time =
      (_has_time && trail_length_sec > 0) ?
          (_last_time - static_cast<double>(trail_length_sec)) :
          -std::numeric_limits<double>::infinity();

  for (size_t i = 0; i < sample_count; i++)
  {
    const auto& lat_pt = (*lat_series)[i];
    const auto& lon_pt = (*lon_series)[i];
    const double point_time = std::max(lat_pt.x, lon_pt.x);

    if (!std::isfinite(point_time))
    {
      continue;
    }

    if (!std::isfinite(lat_pt.y) || !std::isfinite(lon_pt.y))
    {
      continue;
    }
    if (lat_pt.y < -90.0 || lat_pt.y > 90.0 || lon_pt.y < -180.0 || lon_pt.y > 180.0)
    {
      continue;
    }

    const QPointF lon_lat(lon_pt.y, lat_pt.y);
    fit_points.push_back(lon_lat);

    if (point_time <= max_time && point_time >= min_time)
    {
      points.push_back(lon_lat);
    }
  }

  _map_widget->setRoute(points, fit_points, _fit_route_once);
  _fit_route_once = false;
  _route_dirty = false;
}

void MapDockPanel::updateMarkerOnMap(double absolute_time)
{
  if (!_map_widget)
  {
    return;
  }

  if (_route_dirty)
  {
    updateRouteOnMap();
  }
  if (_compare_mode)
  {
    return;  // updateRunsOnMap() draws the per-run markers
  }

  const PJ::PlotData* lat_series = nullptr;
  const PJ::PlotData* lon_series = nullptr;
  if (!selectedSeries(lat_series, lon_series))
  {
    _map_widget->setMarker({}, false);
    return;
  }

  const int idx_lat = lat_series->getIndexFromX(absolute_time);
  const int idx_lon = lon_series->getIndexFromX(absolute_time);
  if (idx_lat < 0 || idx_lon < 0)
  {
    return;
  }

  const auto& lat_pt = (*lat_series)[idx_lat];
  const auto& lon_pt = (*lon_series)[idx_lon];

  if (!std::isfinite(lat_pt.y) || !std::isfinite(lon_pt.y))
  {
    return;
  }

  if (lat_pt.y < -90.0 || lat_pt.y > 90.0 || lon_pt.y < -180.0 || lon_pt.y > 180.0)
  {
    return;
  }

  _map_widget->setMarker(QPointF(lon_pt.y, lat_pt.y), true);
}

void MapDockPanel::applyCompareModeToMap()
{
  if (_map_widget)
  {
    _map_widget->setCompareMode(_compare_mode);
  }
}

void MapDockPanel::updateRunsOnMap()
{
  if (!_map_widget)
  {
    return;
  }

  const PJ::PlotData* lat_series = nullptr;
  const PJ::PlotData* lon_series = nullptr;
  if (!selectedSeries(lat_series, lon_series))
  {
    setStatus("Select valid latitude/longitude series");
    _map_widget->setRuns({}, {}, false);
    return;
  }

  auto valid = [](double lat, double lon) {
    return std::isfinite(lat) && std::isfinite(lon) && lat >= -90.0 && lat <= 90.0 &&
           lon >= -180.0 && lon <= 180.0;
  };

  const auto runs = RunSet::instance().includedRuns();
  const auto sample_count = std::min(lat_series->size(), lon_series->size());
  const int trail_length_sec = _trail_length_slider->value();

  // Only the reference run draws a trail; every run gets a marker. The view fits
  // the reference run, or all runs when the reference is not included.
  const bool reference_included =
      std::any_of(runs.begin(), runs.end(), [](const Run& run) { return run.is_reference; });

  QVector<MapTileWidget::RunOverlay> runs_overlay;
  QVector<QPointF> fit_points;
  runs_overlay.reserve(static_cast<int>(runs.size()));
  for (const auto& run : runs)
  {
    // The tracker is in seconds since run start; hold the marker at the run's ends.
    const double now =
        _has_time ? std::clamp(run.t_start + _last_time, run.t_start, run.t_end) : run.t_end;
    const double trail_start = (_has_time && trail_length_sec > 0) ?
                                   now - static_cast<double>(trail_length_sec) :
                                   run.t_start;
    const bool draw_trail = run.is_reference;
    const bool fit_run = run.is_reference || !reference_included;

    MapTileWidget::RunOverlay overlay;
    overlay.color = run.color;
    overlay.reference = run.is_reference;
    if (draw_trail)
    {
      overlay.trail.reserve(static_cast<int>(sample_count));
    }
    const int first = std::max(0, lat_series->getIndexFromX(run.t_start));
    for (size_t i = static_cast<size_t>(first); (draw_trail || fit_run) && i < sample_count; i++)
    {
      const auto& lat_pt = (*lat_series)[i];
      const auto& lon_pt = (*lon_series)[i];
      const double point_time = std::max(lat_pt.x, lon_pt.x);
      if (point_time > run.t_end)
      {
        break;
      }
      if (point_time < run.t_start || !valid(lat_pt.y, lon_pt.y))
      {
        continue;
      }
      const QPointF lon_lat(lon_pt.y, lat_pt.y);
      if (fit_run)
      {
        fit_points.push_back(lon_lat);
      }
      if (draw_trail && point_time >= trail_start && point_time <= now)
      {
        overlay.trail.push_back(lon_lat);
      }
    }

    const int idx_lat = lat_series->getIndexFromX(now);
    const int idx_lon = lon_series->getIndexFromX(now);
    if (idx_lat >= 0 && idx_lon >= 0)
    {
      const double lat = (*lat_series)[idx_lat].y;
      const double lon = (*lon_series)[idx_lon].y;
      if (valid(lat, lon))
      {
        overlay.position = QPointF(lon, lat);
        overlay.has_position = true;
      }
    }
    runs_overlay.push_back(std::move(overlay));
  }

  setStatus(runs.empty() ? QString("No included runs") :
                           QString("%1 run(s), colored as in the Runs panel").arg(runs.size()));

  _map_widget->setRuns(runs_overlay, fit_points, _fit_route_once);
  _fit_route_once = false;
  _route_dirty = false;
}

bool MapDockPanel::selectedSeries(const PJ::PlotData*& lat_series,
                                  const PJ::PlotData*& lon_series) const
{
  lat_series = nullptr;
  lon_series = nullptr;

  const auto lat_it = _plot_data.numeric.find(_lat_combo->currentText().toStdString());
  if (lat_it != _plot_data.numeric.end())
  {
    lat_series = &lat_it->second;
  }

  const auto lon_it = _plot_data.numeric.find(_lon_combo->currentText().toStdString());
  if (lon_it != _plot_data.numeric.end())
  {
    lon_series = &lon_it->second;
  }

  return (lat_series != nullptr && lon_series != nullptr);
}

void MapDockPanel::setStatus(const QString& text)
{
  _status_label->setText(text);
}
