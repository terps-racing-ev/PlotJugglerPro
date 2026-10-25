/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef RUN_PANEL_H
#define RUN_PANEL_H

#include <QWidget>
#include <functional>
#include <optional>

class QLabel;
class QTableWidget;
class QTableWidgetItem;
class QPushButton;

// Compact list of runs under the curve tree: mark, rename, recolor, include,
// choose the reference run, and open a Compare tab.
class RunPanel : public QWidget
{
  Q_OBJECT

public:
  struct Callbacks
  {
    std::function<double()> tracker_time;  // absolute time
    std::function<double()> time_offset;
  };

  RunPanel(Callbacks callbacks, QWidget* parent = nullptr);

public slots:
  void markIn();
  void markOut();
  void refresh();

signals:
  void compareRequested();
  void zoomToRunRequested(double t_start, double t_end);
  void undoableChange();

private:
  void onItemChanged(QTableWidgetItem* item);
  void onCellDoubleClicked(int row, int column);
  int selectedRunId() const;
  int runIdAtRow(int row) const;
  void updatePendingLabel();

  Callbacks _callbacks;
  QTableWidget* _table = nullptr;
  QLabel* _pending_label = nullptr;
  QPushButton* _button_compare = nullptr;
  std::optional<double> _pending_start;
  bool _refreshing = false;
};

#endif  // RUN_PANEL_H
