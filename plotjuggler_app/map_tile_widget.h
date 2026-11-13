/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef MAP_TILE_WIDGET_H
#define MAP_TILE_WIDGET_H

#include <QHash>
#include <QColor>
#include <QPointF>
#include <QPixmap>
#include <QSet>
#include <QWidget>
#include <QVector>
#include <QUrl>

class QNetworkAccessManager;
class QPushButton;
class QPainter;
class QContextMenuEvent;
class QMouseEvent;
class QWheelEvent;

class MapTileWidget : public QWidget
{
  Q_OBJECT

public:
  struct RunOverlay
  {
    QColor color;
    bool reference = false;
    QVector<QPointF> trail;
    bool has_position = false;
    QPointF position;
  };

  explicit MapTileWidget(QWidget* parent = nullptr);

  void setTileSource(const QString& url_template, const QString& attribution,
                     int max_native_zoom, int max_zoom);
  void setRoute(const QVector<QPointF>& route, const QVector<QPointF>& fit_points,
                bool fit_bounds);
  void setMarker(const QPointF& position, bool visible);
  void setCompareMode(bool enabled);
  void setRuns(const QVector<RunOverlay>& runs, const QVector<QPointF>& fit_points,
               bool fit_bounds);
  void fitToRoute();

signals:
  void contextMenuRequested(const QPoint& pos);

protected:
  void paintEvent(QPaintEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void contextMenuEvent(QContextMenuEvent* event) override;

private:
  static QPointF project(const QPointF& lon_lat, double zoom);
  static QPointF unproject(const QPointF& pixel, double zoom);
  QPointF toScreen(const QPointF& lon_lat) const;
  void zoomAt(const QPointF& anchor_pos, double zoom_delta);
  void requestTile(int zoom, int x, int y);
  QString tileKey(int zoom, int x, int y) const;
  QUrl tileUrl(int zoom, int x, int y) const;
  void drawPolyline(QPainter& painter, const QVector<QPointF>& points, const QColor& color,
                    qreal width, bool wrap = true) const;
  void drawMarker(QPainter& painter, const QPointF& position, const QColor& color,
                  qreal radius) const;

  QNetworkAccessManager* _network = nullptr;
  QPushButton* _zoom_in_button = nullptr;
  QPushButton* _zoom_out_button = nullptr;
  QString _tile_url_template;
  QString _attribution;
  QHash<QString, QPixmap> _tiles;
  QSet<QString> _pending_tiles;
  QSet<QString> _failed_tiles;
  QVector<QPointF> _route;
  QVector<QPointF> _fit_points;
  QVector<RunOverlay> _runs;
  QPointF _marker;
  QPointF _center;
  QPoint _last_mouse_pos;
  int _tile_max_zoom = 16;
  int _max_zoom = 21;
  double _zoom = 4.0;
  bool _marker_visible = false;
  bool _compare_mode = false;
  bool _dragging = false;
};

#endif  // MAP_TILE_WIDGET_H
