#pragma once
#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>
#include <QVariantMap>

class AppTheme : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap colors READ colors NOTIFY changed)
  public:
    explicit AppTheme(QObject *parent = nullptr);
    explicit AppTheme(const QString &currentDirectory, QObject *parent = nullptr);
    QVariantMap colors() const { return m_colors; }
  signals:
    void changed();

  private:
    void reload();
    QString m_currentDirectory;
    QVariantMap m_colors;
    QFileSystemWatcher m_watcher;
    QTimer m_reload;
};
