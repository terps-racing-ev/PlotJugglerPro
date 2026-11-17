/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <QObject>
#include <QPointer>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;
class QWidget;

// Checks the PlotJugglerPro GitHub repo for a newer published release.
//
// Releases are tagged by date (e.g. 2026.10.26). The tag this build came from
// is baked in at build time (pj_release_tag.h); a build without a tag (no git,
// no tags) never notifies on startup.
class UpdateChecker : public QObject
{
  Q_OBJECT

public:
  explicit UpdateChecker(QWidget* parent_window);

  // Release tag this build came from, or an empty string for untagged builds.
  static QString currentReleaseTag();

  static bool startupCheckEnabled();

  // Quiet check: respects the preference and "Skip This Version", and reports
  // only a newer release (through updateAvailable). Errors are ignored.
  void checkOnStartup();

  // User-requested check from the Help menu: always reports the result.
  void checkInteractive();

signals:
  // Rich-text message suitable for a toast.
  void updateAvailable(const QString& message);

private:
  void startRequest(bool interactive);
  void handleReply(QNetworkReply* reply, bool interactive);

  QPointer<QWidget> _parent_window;
  QNetworkAccessManager* _network;
  bool _in_flight = false;
};
