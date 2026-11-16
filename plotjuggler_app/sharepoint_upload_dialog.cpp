/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "sharepoint_upload_dialog.h"
#include "excel_export.h"
#include "plot_docker.h"
#include "xlsx_writer.h"

#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>

namespace
{
constexpr const char* RATE_SETTING = "SharePointUpload.sampleRateHz";
constexpr const char* SAVE_DIR_SETTING = "SharePointUpload.lastSaveDirectory";

QString safeFileName(QString name)
{
  static const QRegularExpression bad_chars(R"([<>:"/\\|?*#%]+)");
  name.replace(bad_chars, "_");
  return name.trimmed();
}

// The SharePoint date folder a downloaded log came from, if any.
QString sharePointFolderOf(const QStringList& source_files)
{
  for (const QString& path : source_files)
  {
    QFile metadata(path + ".pjsharepoint.json");
    if (metadata.open(QFile::ReadOnly))
    {
      const QString folder =
          QJsonDocument::fromJson(metadata.readAll()).object().value("folder").toString();
      if (!folder.isEmpty())
      {
        return folder;
      }
    }
  }
  return {};
}
}  // namespace

namespace PJ
{

SharePointUploadDialog::SharePointUploadDialog(PlotDocker* docker, const QStringList& source_files,
                                               QWidget* parent)
  : QDialog(parent)
  , _docker(docker)
  , _source_files(source_files)
  , _source_folder(sharePointFolderOf(source_files))
  , _client(this)
{
  setWindowTitle(tr("Export Tab to Excel"));
  resize(560, 560);

  auto* main_layout = new QVBoxLayout(this);

  const auto range = excelExportRange(_docker);
  const int sheets = excelExportSheetCount(_docker);
  main_layout->addWidget(new QLabel(
      tr("Tab \"%1\": %2 time-series plot(s), visible range %3 s.\n"
         "Each plot becomes a sheet. XY plots and maps are not exported.")
          .arg(_docker->name())
          .arg(sheets)
          .arg(range ? QString::number(range->max - range->min, 'g', 6) : QString("0")),
      this));

  auto* form = new QFormLayout;
  _rate_spin = new QDoubleSpinBox(this);
  _rate_spin->setRange(0.1, 10000.0);
  _rate_spin->setDecimals(1);
  _rate_spin->setSuffix(" Hz");
  _rate_spin->setValue(QSettings().value(RATE_SETTING, 100.0).toDouble());
  form->addRow(tr("Sample rate"), _rate_spin);

  _rows_label = new QLabel(this);
  form->addRow(tr("Rows per sheet"), _rows_label);

  const QString base = _source_files.isEmpty() ? QString("PlotJuggler") :
                                                 QFileInfo(_source_files.front()).completeBaseName();
  _file_name_edit = new QLineEdit(
      safeFileName(QString("%1_%2_%3.xlsx")
                       .arg(base, _docker->name(),
                            QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"))),
      this);
  form->addRow(tr("File name"), _file_name_edit);
  main_layout->addLayout(form);

  main_layout->addWidget(new QLabel(tr("SharePoint folder"), this));
  _folder_list = new QListWidget(this);
  main_layout->addWidget(_folder_list, 1);

  auto* buttons = new QDialogButtonBox(this);
  auto* refresh_button = buttons->addButton(tr("Refresh Folders"), QDialogButtonBox::ActionRole);
  _save_button = buttons->addButton(tr("Save Locally..."), QDialogButtonBox::ActionRole);
  _upload_button = buttons->addButton(tr("Upload"), QDialogButtonBox::AcceptRole);
  buttons->addButton(QDialogButtonBox::Cancel);
  main_layout->addWidget(buttons);

  connect(_rate_spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
          [this] { updateState(); });
  connect(_file_name_edit, &QLineEdit::textChanged, this, [this] { updateState(); });
  connect(_folder_list, &QListWidget::currentRowChanged, this, [this] { updateState(); });
  connect(refresh_button, &QPushButton::clicked, this, [this] { refreshFolders(); });
  connect(_save_button, &QPushButton::clicked, this, [this] { saveLocally(); });
  connect(_upload_button, &QPushButton::clicked, this, [this] { upload(); });
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  updateState();
  // Let the dialog appear before a Microsoft sign-in prompt can show up.
  QTimer::singleShot(0, this, [this] { refreshFolders(); });
}

bool SharePointUploadDialog::rowCountValid() const
{
  const auto range = excelExportRange(_docker);
  const qint64 rows = range ? excelExportRowCount(*range, _rate_spin->value()) : 0;
  return rows > 0 && rows < XlsxWriter::MAX_ROWS;
}

void SharePointUploadDialog::updateState()
{
  const auto range = excelExportRange(_docker);
  const qint64 rows = range ? excelExportRowCount(*range, _rate_spin->value()) : 0;
  if (!range)
  {
    _rows_label->setText(tr("No time-series data in this tab"));
  }
  else if (rows >= XlsxWriter::MAX_ROWS)
  {
    _rows_label->setText(
        tr("<span style='color:#c0392b'>%L1 exceeds Excel's limit. Zoom in or lower the rate.</span>")
            .arg(rows));
  }
  else
  {
    _rows_label->setText(tr("%L1").arg(rows));
  }

  const bool exportable = rowCountValid() && !fileName().isEmpty();
  _save_button->setEnabled(exportable);
  _upload_button->setEnabled(exportable && _folder_list->currentRow() >= 0);
}

void SharePointUploadDialog::refreshFolders()
{
  _folder_list->clear();
  _folders.clear();
  updateState();

  QString error;
  if (!_client.prepare(&error))
  {
    showError(error);
    return;
  }
  _folders = _client.listDateFolders(&error);
  if (_folders.isEmpty())
  {
    if (!error.isEmpty())
    {
      showError(error);
    }
    return;
  }

  int selected = 0;
  for (int i = 0; i < _folders.size(); i++)
  {
    _folder_list->addItem(_folders[i].name);
    if (_folders[i].name == _source_folder)
    {
      selected = i;
    }
  }
  _folder_list->setCurrentRow(selected);
}

QString SharePointUploadDialog::fileName() const
{
  QString name = safeFileName(_file_name_edit->text());
  if (name.isEmpty())
  {
    return {};
  }
  if (!name.endsWith(".xlsx", Qt::CaseInsensitive))
  {
    name += ".xlsx";
  }
  return name;
}

QByteArray SharePointUploadDialog::buildWorkbook()
{
  QSettings().setValue(RATE_SETTING, _rate_spin->value());
  QApplication::setOverrideCursor(Qt::WaitCursor);
  QString error;
  const QByteArray workbook =
      buildExcelWorkbook(_docker, _rate_spin->value(), _source_files, &error);
  QApplication::restoreOverrideCursor();
  if (workbook.isEmpty())
  {
    showError(error);
  }
  return workbook;
}

void SharePointUploadDialog::upload()
{
  const int row = _folder_list->currentRow();
  if (row < 0 || row >= _folders.size())
  {
    return;
  }
  const QByteArray workbook = buildWorkbook();
  if (workbook.isEmpty())
  {
    return;
  }

  QString error;
  const SharePointDriveItem uploaded =
      _client.uploadFile(_folders[row], fileName(), workbook, &error);
  if (uploaded.id.isEmpty())
  {
    showError(error);
    return;
  }

  QMessageBox msg(this);
  msg.setIcon(QMessageBox::Information);
  msg.setWindowTitle(tr("Uploaded to SharePoint"));
  msg.setText(tr("Uploaded %1 to %2.").arg(uploaded.name, _folders[row].name));
  if (!uploaded.webUrl.isEmpty())
  {
    msg.setInformativeText(QString("<a href=\"%1\">%1</a>").arg(uploaded.webUrl.toHtmlEscaped()));
  }
  msg.setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
  QPushButton* open_button =
      uploaded.webUrl.isEmpty() ? nullptr : msg.addButton(tr("Open in Browser"), QMessageBox::AcceptRole);
  msg.addButton(QMessageBox::Close);
  msg.exec();
  if (open_button && msg.clickedButton() == open_button)
  {
    QDesktopServices::openUrl(QUrl(uploaded.webUrl));
  }
  accept();
}

void SharePointUploadDialog::saveLocally()
{
  QSettings settings;
  const QString directory = settings.value(SAVE_DIR_SETTING, QDir::homePath()).toString();
  const QString path = QFileDialog::getSaveFileName(
      this, tr("Save Excel File"), directory + "/" + fileName(), tr("Excel Workbook (*.xlsx)"));
  if (path.isEmpty())
  {
    return;
  }
  const QByteArray workbook = buildWorkbook();
  if (workbook.isEmpty())
  {
    return;
  }

  QSaveFile file(path);
  if (!file.open(QFile::WriteOnly) || file.write(workbook) != workbook.size() || !file.commit())
  {
    showError(tr("Cannot save %1: %2").arg(path, file.errorString()));
    return;
  }
  settings.setValue(SAVE_DIR_SETTING, QFileInfo(path).absolutePath());
  accept();
}

void SharePointUploadDialog::showError(const QString& message)
{
  QMessageBox msg(this);
  msg.setIcon(QMessageBox::Warning);
  msg.setWindowTitle(tr("Export Tab to Excel"));
  msg.setText(message.isEmpty() ? tr("SharePoint request failed.") : message);
  msg.setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard |
                              Qt::LinksAccessibleByMouse | Qt::LinksAccessibleByKeyboard);
  msg.exec();
}

}  // namespace PJ
