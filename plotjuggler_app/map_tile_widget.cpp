/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "map_tile_widget.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include <QContextMenuEvent>
#include <QDir>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace
{
constexpr qreal TILE_SIZE = 256.0;
constexpr double MAX_MERCATOR_LATITUDE = 85.0511287798066;
constexpr int MAX_MEMORY_TILES = 384;

int wrappedTileX(int x, int count)
{
  return ((x % count) + count) % count;
}
}  // namespace

MapTileWidget::MapTileWidget(QWidget* parent)
  : QWidget(parent), _network(new QNetworkAccessManager(this)), _center(-98.0, 39.0)
{
  setMinimumSize(180, 140);
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
  setCursor(Qt::OpenHandCursor);

  auto* controls = new QVBoxLayout(this);
  controls->setContentsMargins(8, 8, 0, 0);
  controls->setSpacing(2);
  _zoom_in_button = new QPushButton("+", this);
  _zoom_out_button = new QPushButton("-", this);
  for (auto* button : { _zoom_in_button, _zoom_out_button })
  {
    button->setFixedSize(30, 30);
    button->setFocusPolicy(Qt::NoFocus);
    button->setStyleSheet(
        "QPushButton { background: white; border: 1px solid #9ca3af; border-radius: 2px; "
        "font-size: 18px; font-weight: bold; color: #1f2937; }"
        "QPushButton:hover { background: #f3f4f6; }");
  }
  controls->addWidget(_zoom_in_button, 0, Qt::AlignLeft | Qt::AlignTop);
  controls->addWidget(_zoom_out_button, 0, Qt::AlignLeft | Qt::AlignTop);
  controls->addStretch(1);
  connect(_zoom_in_button, &QPushButton::clicked, this,
          [this]() { zoomAt(QPointF(width() * 0.5, height() * 0.5), 1.0); });
  connect(_zoom_out_button, &QPushButton::clicked, this,
          [this]() { zoomAt(QPointF(width() * 0.5, height() * 0.5), -1.0); });

  auto* disk_cache = new QNetworkDiskCache(_network);
  const QString cache_path =
      QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/map-tiles";
  QDir().mkpath(cache_path);
  disk_cache->setCacheDirectory(cache_path);
  disk_cache->setMaximumCacheSize(128 * 1024 * 1024);
  _network->setCache(disk_cache);
}

void MapTileWidget::setTileSource(const QString& url_template, const QString& attribution,
                                  int max_native_zoom, int max_zoom)
{
  const int native_zoom = std::clamp(max_native_zoom, 1, 22);
  const int view_zoom = std::clamp(max_zoom, native_zoom, 22);
  if (_tile_url_template == url_template && _attribution == attribution &&
      _tile_max_zoom == native_zoom && _max_zoom == view_zoom)
  {
    return;
  }
  _tile_url_template = url_template;
  _attribution = attribution;
  _tile_max_zoom = native_zoom;
  _max_zoom = view_zoom;
  _tiles.clear();
  _failed_tiles.clear();
  update();
}

void MapTileWidget::setRoute(const QVector<QPointF>& route, const QVector<QPointF>& fit_points,
                             bool fit_bounds)
{
  _route = route;
  _fit_points = fit_points;
  if (fit_bounds)
  {
    fitToRoute();
  }
  else
  {
    update();
  }
}

void MapTileWidget::setMarker(const QPointF& position, bool visible)
{
  _marker = position;
  _marker_visible = visible;
  update();
}

void MapTileWidget::setCompareMode(bool enabled)
{
  _compare_mode = enabled;
  if (enabled)
  {
    _route.clear();
    _marker_visible = false;
  }
  else
  {
    _runs.clear();
  }
  update();
}

void MapTileWidget::setRuns(const QVector<RunOverlay>& runs, const QVector<QPointF>& fit_points,
                            bool fit_bounds)
{
  _runs = runs;
  _fit_points = fit_points;
  if (fit_bounds)
  {
    fitToRoute();
  }
  else
  {
    update();
  }
}

void MapTileWidget::fitToRoute()
{
  const QVector<QPointF>& points = _fit_points.isEmpty() ? _route : _fit_points;
  if (points.isEmpty())
  {
    return;
  }
  if (points.size() == 1)
  {
    _center = points.front();
    _zoom = std::min(16, _max_zoom);
    update();
    return;
  }

  double min_x = 1.0;
  double max_x = 0.0;
  double min_y = 1.0;
  double max_y = 0.0;
  for (const auto& point : points)
  {
    const QPointF projected = project(point, 0.0) / TILE_SIZE;
    min_x = std::min(min_x, projected.x());
    max_x = std::max(max_x, projected.x());
    min_y = std::min(min_y, projected.y());
    max_y = std::max(max_y, projected.y());
  }

  _center = unproject(QPointF((min_x + max_x) * 0.5, (min_y + max_y) * 0.5) * TILE_SIZE,
                      0.0);
  const double available_width = std::max(1, width() - 48);
  const double available_height = std::max(1, height() - 48);
  const double span_x = std::max(1e-9, max_x - min_x);
  const double span_y = std::max(1e-9, max_y - min_y);
  const double fit_zoom = std::floor(std::min(std::log2(available_width / (TILE_SIZE * span_x)),
                                               std::log2(available_height / (TILE_SIZE * span_y))));
  _zoom = std::clamp(fit_zoom, 1.0, static_cast<double>(_max_zoom));
  update();
}

QPointF MapTileWidget::project(const QPointF& lon_lat, double zoom)
{
  const double latitude = std::clamp(lon_lat.y(), -MAX_MERCATOR_LATITUDE,
                                     MAX_MERCATOR_LATITUDE);
  const double scale = TILE_SIZE * std::pow(2.0, zoom);
  const double x = (lon_lat.x() + 180.0) / 360.0 * scale;
  const double radians = latitude * M_PI / 180.0;
  const double y = (1.0 - std::asinh(std::tan(radians)) / M_PI) * 0.5 * scale;
  return QPointF(x, y);
}

QPointF MapTileWidget::unproject(const QPointF& pixel, double zoom)
{
  const double scale = TILE_SIZE * std::pow(2.0, zoom);
  const double lon = pixel.x() / scale * 360.0 - 180.0;
  const double mercator = M_PI * (1.0 - 2.0 * pixel.y() / scale);
  const double lat = std::atan(std::sinh(mercator)) * 180.0 / M_PI;
  return QPointF(lon, lat);
}

QPointF MapTileWidget::toScreen(const QPointF& lon_lat) const
{
  const double world_size = TILE_SIZE * std::pow(2.0, _zoom);
  QPointF point = project(lon_lat, _zoom);
  const QPointF center = project(_center, _zoom);
  double dx = point.x() - center.x();
  if (dx > world_size * 0.5)
  {
    dx -= world_size;
  }
  else if (dx < -world_size * 0.5)
  {
    dx += world_size;
  }
  return QPointF(width() * 0.5 + dx, height() * 0.5 + point.y() - center.y());
}

QString MapTileWidget::tileKey(int zoom, int x, int y) const
{
  const int count = 1 << zoom;
  return QStringLiteral("%1/%2/%3").arg(zoom).arg(wrappedTileX(x, count)).arg(y);
}

QUrl MapTileWidget::tileUrl(int zoom, int x, int y) const
{
  const int count = 1 << zoom;
  QString url = _tile_url_template;
  url.replace(QStringLiteral("{z}"), QString::number(zoom));
  url.replace(QStringLiteral("{x}"), QString::number(wrappedTileX(x, count)));
  url.replace(QStringLiteral("{y}"), QString::number(y));
  return QUrl(url);
}

void MapTileWidget::requestTile(int zoom, int x, int y)
{
  const int count = 1 << zoom;
  if (y < 0 || y >= count)
  {
    return;
  }

  const QString key = tileKey(zoom, x, y);
  if (_tiles.contains(key) || _pending_tiles.contains(key) || _failed_tiles.contains(key))
  {
    return;
  }

  QNetworkRequest request(tileUrl(zoom, x, y));
  request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
  request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, true);
  request.setRawHeader("User-Agent", "PlotJugglerPro/1.0");
  QNetworkReply* reply = _network->get(request);
  reply->setProperty("tileKey", key);
  _pending_tiles.insert(key);

  connect(reply, &QNetworkReply::finished, this, [this, reply, key]() {
    _pending_tiles.remove(key);
    if (reply->error() == QNetworkReply::NoError)
    {
      QPixmap tile;
      if (tile.loadFromData(reply->readAll()))
      {
        _tiles.insert(key, tile);
        while (_tiles.size() > MAX_MEMORY_TILES)
        {
          _tiles.erase(_tiles.begin());
        }
      }
      else
      {
        _failed_tiles.insert(key);
      }
    }
    else
    {
      _failed_tiles.insert(key);
    }
    reply->deleteLater();
    update();
  });
}

void MapTileWidget::drawPolyline(QPainter& painter, const QVector<QPointF>& points,
                                 const QColor& color, qreal width, bool wrap) const
{
  if (points.size() < 2)
  {
    return;
  }
  QPainterPath path;
  QPointF previous;
  bool started = false;
  const double world_size = TILE_SIZE * std::pow(2.0, _zoom);
  for (const auto& point : points)
  {
    QPointF screen = toScreen(point);
    if (started && wrap)
    {
      const double delta = screen.x() - previous.x();
      if (delta > world_size * 0.5)
      {
        screen.rx() -= world_size;
      }
      else if (delta < -world_size * 0.5)
      {
        screen.rx() += world_size;
      }
      if (std::abs(screen.x() - previous.x()) > this->width() * 2.0)
      {
        started = false;
      }
    }
    if (!started)
    {
      path.moveTo(screen);
      started = true;
    }
    else
    {
      path.lineTo(screen);
    }
    previous = screen;
  }
  painter.setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  painter.setBrush(Qt::NoBrush);
  painter.drawPath(path);
}

void MapTileWidget::drawMarker(QPainter& painter, const QPointF& position, const QColor& color,
                               qreal radius) const
{
  const QPointF screen = toScreen(position);
  painter.setPen(QPen(Qt::white, 2.0));
  painter.setBrush(color);
  painter.drawEllipse(screen, radius, radius);
}

void MapTileWidget::paintEvent(QPaintEvent*)
{
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.fillRect(rect(), QColor("#e5e7eb"));

  const int tile_zoom = std::min(static_cast<int>(std::floor(_zoom)), _tile_max_zoom);
  const double tile_scale = std::pow(2.0, _zoom - tile_zoom);
  const double tile_display_size = TILE_SIZE * tile_scale;
  const int tile_count = 1 << tile_zoom;
  const QPointF center_at_tile_zoom = project(_center, tile_zoom);
  const double half_width = width() * 0.5 / tile_scale;
  const double half_height = height() * 0.5 / tile_scale;
  const int first_x = static_cast<int>(std::floor((center_at_tile_zoom.x() - half_width) /
                                                  TILE_SIZE));
  const int last_x = static_cast<int>(std::floor((center_at_tile_zoom.x() + half_width) /
                                                 TILE_SIZE));
  const int first_y = static_cast<int>(std::floor((center_at_tile_zoom.y() - half_height) /
                                                  TILE_SIZE));
  const int last_y = static_cast<int>(std::floor((center_at_tile_zoom.y() + half_height) /
                                                 TILE_SIZE));

  painter.save();
  painter.setClipRect(rect());
  for (int tile_x = first_x; tile_x <= last_x; ++tile_x)
  {
    for (int tile_y = first_y; tile_y <= last_y; ++tile_y)
    {
      if (tile_y < 0 || tile_y >= tile_count)
      {
        continue;
      }
      const QString key = tileKey(tile_zoom, tile_x, tile_y);
      const QPointF top_left(width() * 0.5 +
                                 (tile_x * TILE_SIZE - center_at_tile_zoom.x()) * tile_scale,
                             height() * 0.5 +
                                 (tile_y * TILE_SIZE - center_at_tile_zoom.y()) * tile_scale);
      const QRectF target(top_left, QSizeF(tile_display_size, tile_display_size));
      const auto tile = _tiles.constFind(key);
      if (tile != _tiles.cend())
      {
        painter.drawPixmap(target, tile.value(), tile.value().rect());
      }
      else
      {
        painter.fillRect(target, QColor("#d1d5db"));
        requestTile(tile_zoom, tile_x, tile_y);
      }
    }
  }
  painter.restore();

  if (_compare_mode)
  {
    QVector<const RunOverlay*> ordered;
    ordered.reserve(_runs.size());
    for (const auto& run : _runs)
    {
      ordered.push_back(&run);
    }
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const RunOverlay* a, const RunOverlay* b) {
                       return !a->reference && b->reference;
                     });
    for (const auto* run : ordered)
    {
      QColor trail_color = run->color;
      trail_color.setAlpha(run->reference ? 242 : 191);
      drawPolyline(painter, run->trail, trail_color, run->reference ? 5.0 : 3.0);
    }
    for (const auto* run : ordered)
    {
      if (run->has_position)
      {
        drawMarker(painter, run->position, run->color, run->reference ? 8.0 : 6.0);
      }
    }
  }
  else
  {
    drawPolyline(painter, _route, QColor("#2563eb"), 3.0);
    if (_marker_visible)
    {
      drawMarker(painter, _marker, QColor("#dc2626"), 7.0);
    }
  }

  if (!_attribution.isEmpty())
  {
    const QFontMetrics metrics(font());
    const QRect text_rect = metrics.boundingRect(_attribution).adjusted(-5, -2, 5, 2);
    const QRect bounds(width() - text_rect.width() - 8, height() - text_rect.height() - 8,
                       text_rect.width(), text_rect.height());
    painter.fillRect(bounds, QColor(255, 255, 255, 210));
    painter.setPen(QColor("#333333"));
    painter.drawText(bounds, Qt::AlignCenter, _attribution);
  }

  if (!_failed_tiles.isEmpty())
  {
    const QString message = QStringLiteral("Map tiles unavailable");
    const QRect bounds(8, 8, QFontMetrics(font()).horizontalAdvance(message) + 12, 24);
    painter.fillRect(bounds, QColor(255, 255, 255, 220));
    painter.setPen(QColor("#7f1d1d"));
    painter.drawText(bounds, Qt::AlignCenter, message);
  }
}

void MapTileWidget::wheelEvent(QWheelEvent* event)
{
  const int delta = event->angleDelta().y();
  if (delta == 0)
  {
    event->ignore();
    return;
  }
  zoomAt(event->posF(), delta > 0 ? 1.0 : -1.0);
  event->accept();
}

void MapTileWidget::zoomAt(const QPointF& anchor_pos, double zoom_delta)
{
  const QPointF old_center = project(_center, _zoom);
  const QPointF anchor_world = old_center + anchor_pos - QPointF(width() * 0.5, height() * 0.5);
  const QPointF anchor_coordinate = unproject(anchor_world, _zoom);
  _zoom = std::clamp(_zoom + zoom_delta, 1.0, static_cast<double>(_max_zoom));
  const QPointF new_anchor = project(anchor_coordinate, _zoom);
  const QPointF new_center = new_anchor - anchor_pos + QPointF(width() * 0.5, height() * 0.5);
  _center = unproject(new_center, _zoom);
  update();
}

void MapTileWidget::mousePressEvent(QMouseEvent* event)
{
  if (event->button() == Qt::LeftButton)
  {
    _dragging = true;
    _last_mouse_pos = event->pos();
    setCursor(Qt::ClosedHandCursor);
    event->accept();
    return;
  }
  QWidget::mousePressEvent(event);
}

void MapTileWidget::mouseMoveEvent(QMouseEvent* event)
{
  if (_dragging)
  {
    const QPoint delta = event->pos() - _last_mouse_pos;
    _last_mouse_pos = event->pos();
    const QPointF center_px = project(_center, _zoom) - QPointF(delta);
    _center = unproject(center_px, _zoom);
    update();
    event->accept();
    return;
  }
  QWidget::mouseMoveEvent(event);
}

void MapTileWidget::mouseReleaseEvent(QMouseEvent* event)
{
  if (event->button() == Qt::LeftButton && _dragging)
  {
    _dragging = false;
    setCursor(Qt::OpenHandCursor);
    event->accept();
    return;
  }
  QWidget::mouseReleaseEvent(event);
}

void MapTileWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
  if (event->button() == Qt::LeftButton)
  {
    zoomAt(event->pos(), 1.0);
    event->accept();
    return;
  }
  QWidget::mouseDoubleClickEvent(event);
}

void MapTileWidget::contextMenuEvent(QContextMenuEvent* event)
{
  emit contextMenuRequested(event->pos());
  event->accept();
}
