/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef RUN_SET_H
#define RUN_SET_H

#include <QColor>
#include <QDomDocument>
#include <QObject>
#include <QString>
#include <vector>

// A run (lap) is a named time window of the loaded data, stored in absolute time
// so it does not move when the "remove time offset" option is toggled.
struct Run
{
  int id = 0;
  QString name;
  double t_start = 0.0;
  double t_end = 0.0;
  QColor color;
  bool include = true;
  bool is_reference = false;

  double duration() const
  {
    return t_end - t_start;
  }
};

// Application-wide list of runs. Compare tabs overlay the included runs, and
// normal time-series plots draw them as background bands.
class RunSet : public QObject
{
  Q_OBJECT

public:
  static RunSet& instance();

  const std::vector<Run>& runs() const
  {
    return _runs;
  }

  std::vector<Run> includedRuns() const;

  const Run* findRun(int id) const;

  int addRun(double t_start, double t_end);
  void removeRun(int id);
  void clear();

  void setName(int id, const QString& name);
  void setRange(int id, double t_start, double t_end);
  void setColor(int id, const QColor& color);
  void setIncluded(int id, bool include);
  void setReference(int id);

  QDomElement xmlSaveState(QDomDocument& doc) const;
  void xmlLoadState(const QDomElement& runs_element);

signals:
  void runsChanged();

private:
  RunSet() = default;

  Run* mutableRun(int id);
  QColor nextColor() const;

  std::vector<Run> _runs;
  int _next_id = 1;
};

#endif  // RUN_SET_H
