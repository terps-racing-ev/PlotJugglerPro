/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef SHAREPOINT_UPLOAD_DIALOG_H
#define SHAREPOINT_UPLOAD_DIALOG_H

#include "sharepoint_graph_client.h"

#include <QDialog>
#include <QStringList>

class PlotDocker;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace PJ
{

// Exports the visible range of one tab as an .xlsx workbook and uploads it to a
// SharePoint date folder, or saves it locally.
class SharePointUploadDialog : public QDialog
{
public:
  SharePointUploadDialog(PlotDocker* docker, const QStringList& source_files,
                         QWidget* parent = nullptr);

private:
  PlotDocker* _docker;
  QStringList _source_files;
  QString _source_folder;
  SharePointGraphClient _client;
  QList<SharePointDriveItem> _folders;

  QDoubleSpinBox* _rate_spin = nullptr;
  QLabel* _rows_label = nullptr;
  QLineEdit* _file_name_edit = nullptr;
  QListWidget* _folder_list = nullptr;
  QPushButton* _upload_button = nullptr;
  QPushButton* _save_button = nullptr;

  bool rowCountValid() const;
  void updateState();
  void refreshFolders();
  QString fileName() const;
  QByteArray buildWorkbook();
  void upload();
  void saveLocally();
  void showError(const QString& message);
};

}  // namespace PJ

#endif  // SHAREPOINT_UPLOAD_DIALOG_H
