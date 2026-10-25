/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "run_set.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace
{
// Qualitative palette that stays distinguishable for ~10 runs before repeating.
const std::vector<QColor>& runPalette()
{
  static const std::vector<QColor> palette = {
    QColor("#1f77b4"), QColor("#ff7f0e"), QColor("#2ca02c"), QColor("#d62728"),
    QColor("#9467bd"), QColor("#8c564b"), QColor("#e377c2"), QColor("#17becf"),
    QColor("#bcbd22"), QColor("#7f7f7f"),
  };
  return palette;
}
}  // namespace

RunSet& RunSet::instance()
{
  static RunSet run_set;
  return run_set;
}

std::vector<Run> RunSet::includedRuns() const
{
  std::vector<Run> result;
  for (const auto& run : _runs)
  {
    if (run.include && run.t_end > run.t_start)
    {
      result.push_back(run);
    }
  }
  return result;
}

const Run* RunSet::findRun(int id) const
{
  auto it = std::find_if(_runs.begin(), _runs.end(), [id](const Run& r) { return r.id == id; });
  return it == _runs.end() ? nullptr : &(*it);
}

Run* RunSet::mutableRun(int id)
{
  return const_cast<Run*>(findRun(id));
}

QColor RunSet::nextColor() const
{
  const auto& palette = runPalette();
  std::set<QRgb> used;
  for (const auto& run : _runs)
  {
    used.insert(run.color.rgb());
  }
  for (const auto& color : palette)
  {
    if (used.count(color.rgb()) == 0)
    {
      return color;
    }
  }
  return palette[_runs.size() % palette.size()];
}

int RunSet::addRun(double t_start, double t_end)
{
  if (t_start > t_end)
  {
    std::swap(t_start, t_end);
  }
  if (!std::isfinite(t_start) || !std::isfinite(t_end) || t_end <= t_start)
  {
    return -1;
  }

  Run run;
  run.id = _next_id++;
  run.name = QString("Run %1").arg(run.id);
  run.t_start = t_start;
  run.t_end = t_end;
  run.color = nextColor();
  run.is_reference = _runs.empty();
  _runs.push_back(run);
  emit runsChanged();
  return run.id;
}

void RunSet::removeRun(int id)
{
  auto it = std::find_if(_runs.begin(), _runs.end(), [id](const Run& r) { return r.id == id; });
  if (it == _runs.end())
  {
    return;
  }
  const bool was_reference = it->is_reference;
  _runs.erase(it);
  if (was_reference && !_runs.empty())
  {
    _runs.front().is_reference = true;
  }
  emit runsChanged();
}

void RunSet::clear()
{
  if (_runs.empty())
  {
    return;
  }
  _runs.clear();
  emit runsChanged();
}

void RunSet::setName(int id, const QString& name)
{
  if (auto run = mutableRun(id); run && run->name != name && !name.trimmed().isEmpty())
  {
    run->name = name.trimmed();
    emit runsChanged();
  }
}

void RunSet::setRange(int id, double t_start, double t_end)
{
  if (t_start > t_end)
  {
    std::swap(t_start, t_end);
  }
  if (t_end <= t_start)
  {
    return;
  }
  if (auto run = mutableRun(id))
  {
    run->t_start = t_start;
    run->t_end = t_end;
    emit runsChanged();
  }
}

void RunSet::setColor(int id, const QColor& color)
{
  if (auto run = mutableRun(id); run && color.isValid() && run->color != color)
  {
    run->color = color;
    emit runsChanged();
  }
}

void RunSet::setIncluded(int id, bool include)
{
  if (auto run = mutableRun(id); run && run->include != include)
  {
    run->include = include;
    emit runsChanged();
  }
}

void RunSet::setReference(int id)
{
  if (!findRun(id))
  {
    return;
  }
  for (auto& run : _runs)
  {
    run.is_reference = (run.id == id);
  }
  emit runsChanged();
}

QDomElement RunSet::xmlSaveState(QDomDocument& doc) const
{
  QDomElement runs_el = doc.createElement("runs");
  for (const auto& run : _runs)
  {
    QDomElement run_el = doc.createElement("run");
    run_el.setAttribute("name", run.name);
    run_el.setAttribute("start", QString::number(run.t_start, 'f', 9));
    run_el.setAttribute("end", QString::number(run.t_end, 'f', 9));
    run_el.setAttribute("color", run.color.name());
    run_el.setAttribute("include", run.include ? "true" : "false");
    run_el.setAttribute("reference", run.is_reference ? "true" : "false");
    runs_el.appendChild(run_el);
  }
  return runs_el;
}

void RunSet::xmlLoadState(const QDomElement& runs_element)
{
  _runs.clear();
  _next_id = 1;

  bool has_reference = false;
  for (auto run_el = runs_element.firstChildElement("run"); !run_el.isNull();
       run_el = run_el.nextSiblingElement("run"))
  {
    Run run;
    run.id = _next_id++;
    run.t_start = run_el.attribute("start").toDouble();
    run.t_end = run_el.attribute("end").toDouble();
    if (run.t_start > run.t_end)
    {
      std::swap(run.t_start, run.t_end);
    }
    if (!(run.t_end > run.t_start))
    {
      continue;
    }
    run.name = run_el.attribute("name", QString("Run %1").arg(run.id));
    run.color = QColor(run_el.attribute("color"));
    if (!run.color.isValid())
    {
      run.color = nextColor();
    }
    run.include = run_el.attribute("include", "true") == "true";
    run.is_reference = !has_reference && run_el.attribute("reference") == "true";
    has_reference |= run.is_reference;
    _runs.push_back(run);
  }
  if (!has_reference && !_runs.empty())
  {
    _runs.front().is_reference = true;
  }
  emit runsChanged();
}
