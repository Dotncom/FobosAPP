#include "appsettingsutils.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

QString persistentSettingsFilePath() {
    static const QString settingsPath = []() {
        QString configDirectory =
            QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        if (configDirectory.isEmpty()) {
            configDirectory = QDir(
                QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
                                  .filePath(QStringLiteral("FobosAPP"));
        }
        if (configDirectory.isEmpty()) {
            configDirectory = QCoreApplication::applicationDirPath();
        }

        QDir directory(configDirectory);
        if (!directory.exists() && !QDir().mkpath(configDirectory)) {
            qWarning() << "[Settings] could not create per-user settings directory"
                       << configDirectory;
        }

        const QString userPath = directory.filePath(QStringLiteral("FobosAPP.ini"));
        const QString legacyPath = QDir(QCoreApplication::applicationDirPath())
                                       .filePath(QStringLiteral("FobosAPP.ini"));
        if (!QFileInfo::exists(userPath) && QFileInfo::exists(legacyPath) &&
            QFileInfo(userPath).absoluteFilePath() != QFileInfo(legacyPath).absoluteFilePath()) {
            if (QFile::copy(legacyPath, userPath)) {
                qInfo() << "[Settings] migrated settings to per-user storage"
                        << userPath;
            } else {
                qWarning() << "[Settings] could not migrate legacy settings"
                           << legacyPath << "to" << userPath;
            }
        }
        return userPath;
    }();
    return settingsPath;
}
