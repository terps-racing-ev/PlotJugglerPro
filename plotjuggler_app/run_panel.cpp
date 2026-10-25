/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "run_panel.h"
#include <QColorDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>
#include <cmath>
#include "run_set.h"

namespace
{
enum Column
{
  COL_INCLUDE = 0,
  COL_NAME,
  COL_START,
  COL_DURATION,
  COL_REFERENCE,
  COL_COUNT
};

constexpr int kRunIdRole = Qt::UserRole + 1;

// palette() keeps the borders and hover tint readable in both light and dark themes.
const char* kButtonStyle = R"(
QPushButton {
  border: 1px solid palette(mid);
  border-radius: 3px;
  padding: 2px 8px;
  background: transparent;
}
QPushButton:hover { background: rgba(128, 128, 128, 40); }
QPushButton:pressed { background: rgba(128, 128, 128, 80); }
QPushButton:disabled { color: palette(mid); }
QPushButton#compareButton {
  border: 2px solid palette(highlight);
  border-radius: 4px;
  padding: 5px 12px;
  font-weight: bold;
}
QPushButton#compareButton:hover { background: rgba(80, 140, 230, 50); }
QPushButton#compareButton:pressed { background: rgba(80, 140, 230, 90); }
QPushButton#compareButton:disabled { border-color: palette(mid); }
)";

QPushButton* makeButton(const QString& text, const QString& tooltip, QWidget* parent)
{
  auto button = new QPushButton(text, parent);
  button->setToolTip(tooltip);
  button->setFocusPolicy(Qt::NoFocus);
  button->setCursor(Qt::PointingHandCursor);
  return button;
}

QString formatSeconds(double value)
{
  return QString::number(value, 'f', 3);
}
}  // namespace

RunPanel::RunPanel(Callbacks callbacks, QWidget* parent)
  : QWidget(parent), _callbacks(std::move(callbacks))
{
  auto main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(0, 4, 0, 0);
  main_layout->setSpacing(4);
  setStyleSheet(kButtonStyle);

  auto header = new QHBoxLayout();
  header->setSpacing(4);
  auto title = new QLabel(tr("<b>Runs</b>"), this);
  header->addWidget(title);
  _pending_label = new QLabel(this);
  _pending_label->setStyleSheet("color: gray;");
  header->addWidget(_pending_label, 1);

  auto button_start =
      makeButton(tr("Mark Start"), tr("Mark run start at the tracker  ( [ )"), this);
  auto button_end = makeButton(tr("Mark End"), tr("Mark run end at the tracker  ( ] )"), this);
  header->addWidget(button_start);
  header->addWidget(button_end);
  main_layout->addLayout(header);

  _table = new QTableWidget(0, COL_COUNT, this);
  _table->setHorizontalHeaderLabels({ "", tr("Name"), tr("Start"), tr("Length"), tr("Ref") });
  _table->horizontalHeaderItem(COL_INCLUDE)->setToolTip(tr("Include in Compare tabs"));
  _table->horizontalHeaderItem(COL_START)->setToolTip(tr("Start time [s], same time base as plots"));
  _table->horizontalHeaderItem(COL_DURATION)->setToolTip(tr("Run length [s]"));
  _table->horizontalHeaderItem(COL_REFERENCE)->setToolTip(tr("Reference run, drawn on top"));
  _table->verticalHeader()->setVisible(false);
  _table->verticalHeader()->setDefaultSectionSize(20);
  _table->setSelectionBehavior(QAbstractItemView::SelectRows);
  _table->setSelectionMode(QAbstractItemView::SingleSelection);
  _table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
  _table->horizontalHeader()->setSectionResizeMode(COL_INCLUDE, QHeaderView::ResizeToContents);
  _table->horizontalHeader()->setSectionResizeMode(COL_NAME, QHeaderView::Stretch);
  _table->horizontalHeader()->setSectionResizeMode(COL_START, QHeaderView::ResizeToContents);
  _table->horizontalHeader()->setSectionResizeMode(COL_DURATION, QHeaderView::ResizeToContents);
  _table->horizontalHeader()->setSectionResizeMode(COL_REFERENCE, QHeaderView::ResizeToContents);
  _table->setMaximumHeight(170);
  _table->setToolTip(tr("Double-click the color swatch to recolor, or name/start/length to edit.\n"
                        "Right-click a run to zoom to it or delete it (or press Delete)."));
  _table->setContextMenuPolicy(Qt::CustomContextMenu);
  main_layout->addWidget(_table);

  _button_compare = makeButton(tr("Compare Runs"),
                               tr("Open a Compare tab overlaying the included runs"), this);
  _button_compare->setObjectName("compareButton");
  _button_compare->setMinimumHeight(30);
  main_layout->addWidget(_button_compare);

  connect(button_start, &QPushButton::clicked, this, &RunPanel::markIn);
  connect(button_end, &QPushButton::clicked, this, &RunPanel::markOut);
  connect(_button_compare, &QPushButton::clicked, this, &RunPanel::compareRequested);
  auto delete_shortcut = new QShortcut(QKeySequence::Delete, _table);
  delete_shortcut->setContext(Qt::WidgetShortcut);
  connect(delete_shortcut, &QShortcut::activated, this, [this]() {
    const int id = selectedRunId();
    if (id > 0)
    {
      RunSet::instance().removeRun(id);
      emit undoableChange();
    }
  });
  connect(_table, &QTableWidget::itemChanged, this, &RunPanel::onItemChanged);
  connect(_table, &QTableWidget::cellDoubleClicked, this, &RunPanel::onCellDoubleClicked);
  // Queued: runs change from inside table signals, and refresh() recreates the items.
  connect(&RunSet::instance(), &RunSet::runsChanged, this, &RunPanel::refresh,
          Qt::QueuedConnection);
  connect(_table, &QTableWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
    const int id = runIdAtRow(_table->rowAt(pos.y()));
    const Run* run = RunSet::instance().findRun(id);
    if (!run)
    {
      return;
    }
    QMenu menu(_table);
    auto zoom = menu.addAction(tr("Zoom current tab to run"));
    auto reference = menu.addAction(tr("Set as reference"));
    menu.addSeparator();
    auto remove = menu.addAction(tr("Delete run"));
    auto chosen = menu.exec(_table->viewport()->mapToGlobal(pos));
    if (chosen == zoom)
    {
      emit zoomToRunRequested(run->t_start, run->t_end);
    }
    else if (chosen == reference)
    {
      RunSet::instance().setReference(id);
      emit undoableChange();
    }
    else if (chosen == remove)
    {
      RunSet::instance().removeRun(id);
      emit undoableChange();
    }
  });

  refresh();
}

void RunPanel::markIn()
{
  if (!_callbacks.tracker_time)
  {
    return;
  }
  _pending_start = _callbacks.tracker_time();
  updatePendingLabel();
}

void RunPanel::markOut()
{
  if (!_callbacks.tracker_time)
  {
    return;
  }
  if (!_pending_start)
  {
    _pending_label->setText(tr("press Mark Start first"));
    return;
  }
  const double t_end = _callbacks.tracker_time();
  const double t_start = *_pending_start;
  _pending_start.reset();
  updatePendingLabel();
  if (RunSet::instance().addRun(t_start, t_end) > 0)
  {
    emit undoableChange();
  }
  else
  {
    _pending_label->setText(tr("empty run ignored"));
  }
}

void RunPanel::updatePendingLabel()
{
  if (_pending_start)
  {
    const double offset = _callbacks.time_offset ? _callbacks.time_offset() : 0.0;
    _pending_label->setText(tr("start @ %1").arg(formatSeconds(*_pending_start - offset)));
  }
  else
  {
    _pending_label->clear();
  }
}

void RunPanel::refresh()
{
  const QSignalBlocker blocker(_table);
  _refreshing = true;

  const int selected_id = selectedRunId();
  const double offset = _callbacks.time_offset ? _callbacks.time_offset() : 0.0;
  const auto& runs = RunSet::instance().runs();

  _table->setRowCount(static_cast<int>(runs.size()));
  int row = 0;
  for (const auto& run : runs)
  {
    auto include = new QTableWidgetItem();
    include->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
    include->setCheckState(run.include ? Qt::Checked : Qt::Unchecked);
    include->setData(Qt::DecorationRole, run.color);
    include->setData(kRunIdRole, run.id);
    _table->setItem(row, COL_INCLUDE, include);

    auto name = new QTableWidgetItem(run.name);
    name->setForeground(run.color);
    _table->setItem(row, COL_NAME, name);

    _table->setItem(row, COL_START, new QTableWidgetItem(formatSeconds(run.t_start - offset)));
    _table->setItem(row, COL_DURATION, new QTableWidgetItem(formatSeconds(run.duration())));

    auto reference = new QTableWidgetItem();
    reference->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
    reference->setCheckState(run.is_reference ? Qt::Checked : Qt::Unchecked);
    _table->setItem(row, COL_REFERENCE, reference);

    if (run.id == selected_id)
    {
      _table->selectRow(row);
    }
    row++;
  }

  _table->setVisible(!runs.empty());
  _button_compare->setEnabled(!RunSet::instance().includedRuns().empty());
  _refreshing = false;
}

int RunPanel::runIdAtRow(int row) const
{
  auto item = _table->item(row, COL_INCLUDE);
  return item ? item->data(kRunIdRole).toInt() : -1;
}

int RunPanel::selectedRunId() const
{
  auto selected = _table->selectionModel() ? _table->selectionModel()->selectedRows() :
                                             QModelIndexList();
  return selected.empty() ? -1 : runIdAtRow(selected.front().row());
}

void RunPanel::onItemChanged(QTableWidgetItem* item)
{
  if (_refreshing || !item)
  {
    return;
  }
  const int id = runIdAtRow(item->row());
  const Run* run = RunSet::instance().findRun(id);
  if (!run)
  {
    return;
  }

  auto& run_set = RunSet::instance();
  const double offset = _callbacks.time_offset ? _callbacks.time_offset() : 0.0;
  bool ok = true;

  switch (item->column())
  {
    case COL_INCLUDE:
      run_set.setIncluded(id, item->checkState() == Qt::Checked);
      break;
    case COL_NAME:
      run_set.setName(id, item->text());
      break;
    case COL_START: {
      const double start = item->text().toDouble(&ok) + offset;
      if (ok && std::isfinite(start))
      {
        run_set.setRange(id, start, start + run->duration());
      }
      break;
    }
    case COL_DURATION: {
      const double duration = item->text().toDouble(&ok);
      if (ok && duration > 0.0)
      {
        run_set.setRange(id, run->t_start, run->t_start + duration);
      }
      break;
    }
    case COL_REFERENCE:
      if (item->checkState() == Qt::Checked)
      {
        run_set.setReference(id);
      }
      break;
  }
  // Re-sync the table: rejected edits revert, accepted ones get normalized text.
  QMetaObject::invokeMethod(this, &RunPanel::refresh, Qt::QueuedConnection);
  emit undoableChange();
}

void RunPanel::onCellDoubleClicked(int row, int column)
{
  const int id = runIdAtRow(row);
  const Run* run = RunSet::instance().findRun(id);
  if (!run)
  {
    return;
  }
  if (column == COL_INCLUDE)
  {
    QColor color = QColorDialog::getColor(run->color, this, tr("Run color"));
    if (color.isValid())
    {
      RunSet::instance().setColor(id, color);
      emit undoableChange();
    }
  }
}
