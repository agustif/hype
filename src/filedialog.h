#pragma once
#include <QEventLoop>
#include <QObject>
#include <QStringList>
#include <QVariantMap>

// Desktop-portal dialogs, with the same calling convention as Qt's static pickers.
// The nested event loop keeps rendering and background jobs running while choosing.
class FileDialog : public QObject {
    Q_OBJECT
  public:
    static QString choose(bool save, const QString &location, const QString &label,
                          const QStringList &patterns, QString *error);
  private slots:
    void response(uint result, const QVariantMap &values);
  private:
    bool eventFilter(QObject *object, QEvent *event) override;
    bool listen(const QString &path);
    void disconnectRequest();
    QEventLoop m_loop;
    QString m_request, m_file, m_error;
    bool m_done = false;
};
