/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "update_checker.h"
#include "pj_release_tag.h"

#include <QDebug>
#include <QDesktopServices>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QSettings>
#include <QSslSocket>
#include <QUrl>
#include <QVersionNumber>
#include <QWidget>

namespace
{
constexpr const char* kCheckEnabledKey = "Preferences::check_updates";
constexpr const char* kSkippedTagKey = "Updates::skipped_tag";
constexpr int kTimeoutMs = 8000;

// Release tags are dates, optionally followed by one letter for extra releases
// on the same day: 2026.11.17 < 2026.11.17a < 2026.11.17b < 2026.11.18.
struct ReleaseVersion
{
  QVersionNumber number;
  QChar revision;  // null for no letter

  bool isNull() const
  {
    return number.isNull();
  }

  bool operator>(const ReleaseVersion& other) const
  {
    const int cmp = QVersionNumber::compare(number, other.number);
    if (cmp != 0)
    {
      return cmp > 0;
    }
    return revision.unicode() > other.revision.unicode();
  }
};

ReleaseVersion tagToVersion(QString tag)
{
  tag = tag.trimmed();
  if (tag.startsWith('v') || tag.startsWith('V'))
  {
    tag.remove(0, 1);
  }
  int suffix_index = 0;
  ReleaseVersion version;
  version.number = QVersionNumber::fromString(tag, &suffix_index);
  const QString suffix = tag.mid(suffix_index).toLower();

  if (suffix.isEmpty())
  {
    return version;
  }
  if (suffix.size() == 1 && suffix[0] >= 'a' && suffix[0] <= 'z')
  {
    version.revision = suffix[0];
    return version;
  }
  // Anything else (e.g. "2026.10.26-rc1") is not a recognized release tag.
  return ReleaseVersion();
}

// Installer/DMG names produced by tools/package_*_release.*
QString platformAssetSuffix()
{
#if defined(Q_OS_WIN)
  return "-Windows-x64-Installer.exe";
#elif defined(Q_OS_MACOS)
  return "-macOS-arm64.dmg";
#else
  return QString();
#endif
}

struct ReleaseInfo
{
  QString tag;
  QString name;
  QUrl page_url;
  QUrl download_url;  // platform asset if present, otherwise the release page
};

ReleaseInfo parseRelease(const QJsonObject& obj)
{
  ReleaseInfo info;
  info.tag = obj["tag_name"].toString();
  info.name = obj["name"].toString();
  if (info.name.isEmpty())
  {
    info.name = info.tag;
  }
  info.page_url = QUrl(obj["html_url"].toString());
  info.download_url = info.page_url;

  const QString suffix = platformAssetSuffix();
  if (!suffix.isEmpty())
  {
    for (const auto& value : obj["assets"].toArray())
    {
      const QJsonObject asset = value.toObject();
      if (asset["name"].toString().endsWith(suffix, Qt::CaseInsensitive))
      {
        info.download_url = QUrl(asset["browser_download_url"].toString());
        break;
      }
    }
  }
  return info;
}
}  // namespace

UpdateChecker::UpdateChecker(QWidget* parent_window)
  : QObject(parent_window), _parent_window(parent_window), _network(new QNetworkAccessManager(this))
{
}

QString UpdateChecker::currentReleaseTag()
{
  return QString(PJ_RELEASE_TAG);
}

bool UpdateChecker::startupCheckEnabled()
{
  return QSettings().value(kCheckEnabledKey, true).toBool();
}

void UpdateChecker::checkOnStartup()
{
  if (!startupCheckEnabled() || tagToVersion(currentReleaseTag()).isNull())
  {
    return;
  }
  startRequest(false);
}

void UpdateChecker::checkInteractive()
{
  startRequest(true);
}

void UpdateChecker::startRequest(bool interactive)
{
  const QString repo = QString(PJ_UPDATE_REPO);
  if (repo.isEmpty())
  {
    if (interactive)
    {
      QMessageBox::information(_parent_window, tr("Check for Updates"),
                               tr("Update checks are disabled in this build."));
    }
    return;
  }

  if (!QSslSocket::supportsSsl())
  {
    if (interactive)
    {
      QMessageBox::warning(_parent_window, tr("Check for Updates"),
                           tr("Cannot check for updates: SSL support is not available "
                              "(OpenSSL libraries are missing)."));
    }
    return;
  }

  if (_in_flight)
  {
    return;
  }
  _in_flight = true;

  QNetworkRequest request(QUrl(QString("https://api.github.com/repos/%1/releases/latest").arg(repo)));
  request.setRawHeader("Accept", "application/vnd.github+json");
  request.setRawHeader("User-Agent", "PlotJugglerPro-UpdateCheck");
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);
  request.setTransferTimeout(kTimeoutMs);

  QNetworkReply* reply = _network->get(request);
  connect(reply, &QNetworkReply::finished, this,
          [this, reply, interactive]() { handleReply(reply, interactive); });
}

void UpdateChecker::handleReply(QNetworkReply* reply, bool interactive)
{
  reply->deleteLater();
  _in_flight = false;

  const int http_status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

  if (reply->error() != QNetworkReply::NoError)
  {
    qDebug() << "Update check failed:" << http_status << reply->errorString();
    if (interactive)
    {
      QString detail = reply->errorString();
      if (http_status == 404)
      {
        detail = tr("No published release was found on GitHub.");
      }
      else if (http_status == 403 || http_status == 429)
      {
        detail = tr("GitHub rate limit reached. Try again later.");
      }
      QMessageBox::warning(_parent_window, tr("Check for Updates"),
                           tr("Could not check for updates.\n\n%1").arg(detail));
    }
    return;
  }

  const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
  const ReleaseInfo latest = parseRelease(obj);
  const ReleaseVersion latest_version = tagToVersion(latest.tag);
  const QString current_tag = currentReleaseTag();
  const ReleaseVersion current_version = tagToVersion(current_tag);

  qDebug() << "Update check: current" << current_tag << "latest" << latest.tag;

  if (latest_version.isNull())
  {
    if (interactive)
    {
      QMessageBox::warning(_parent_window, tr("Check for Updates"),
                           tr("The latest GitHub release has an unrecognized tag: \"%1\".")
                               .arg(latest.tag));
    }
    return;
  }

  const bool is_newer = current_version.isNull() || latest_version > current_version;

  if (!interactive)
  {
    QSettings settings;
    if (!is_newer || settings.value(kSkippedTagKey).toString() == latest.tag)
    {
      return;
    }
    emit updateAvailable(tr("PlotJugglerPro <b>%1</b> is available (you have %2).<br>"
                            "<a href=\"%3\">Download</a> &nbsp;&middot;&nbsp; "
                            "<a href=\"%4\">Release notes</a>")
                             .arg(latest.name.toHtmlEscaped(), current_tag.toHtmlEscaped(),
                                  latest.download_url.toString(QUrl::FullyEncoded),
                                  latest.page_url.toString(QUrl::FullyEncoded)));
    return;
  }

  if (!is_newer)
  {
    QMessageBox::information(_parent_window, tr("Check for Updates"),
                             tr("You are up to date.\n\nThis build: %1\nLatest release: %2")
                                 .arg(current_tag, latest.tag));
    return;
  }

  QMessageBox box(_parent_window);
  box.setWindowTitle(tr("Check for Updates"));
  box.setIcon(QMessageBox::Information);
  box.setText(tr("PlotJugglerPro %1 is available.").arg(latest.name));
  box.setInformativeText(
      tr("This build: %1\nLatest release: %2")
          .arg(current_version.isNull() ? tr("untagged development build") : current_tag,
               latest.tag));
  QPushButton* download_button = box.addButton(tr("Download"), QMessageBox::AcceptRole);
  QPushButton* notes_button = box.addButton(tr("Release Notes"), QMessageBox::HelpRole);
  QPushButton* skip_button = nullptr;
  if (!current_version.isNull())
  {
    skip_button = box.addButton(tr("Skip This Version"), QMessageBox::DestructiveRole);
  }
  box.addButton(tr("Later"), QMessageBox::RejectRole);
  box.setDefaultButton(download_button);
  box.exec();

  QSettings settings;
  if (box.clickedButton() == download_button)
  {
    QDesktopServices::openUrl(latest.download_url);
  }
  else if (box.clickedButton() == notes_button)
  {
    QDesktopServices::openUrl(latest.page_url);
  }
  else if (skip_button && box.clickedButton() == skip_button)
  {
    settings.setValue(kSkippedTagKey, latest.tag);
  }
}
