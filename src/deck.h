#pragma once
#include <QAbstractListModel>
#include <QColor>
#include <QFileSystemWatcher>
#include <QImage>
#include <QUrl>
#include <QVariantMap>

struct Slide {
    QString source;
    int start = 0;
    int end = 0;
};
struct ParsedDeck {
    QString header;
    QVector<Slide> slides;
    QString error;
};
ParsedDeck parseDeck(const QString &source);
QString scalar(const QString &header, const QString &key, const QString &fallback = {});
QString setScalar(QString header, const QString &key, const QString &value);

class Deck : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString source READ source NOTIFY changed)
    Q_PROPERTY(QString slideSource READ slideSource NOTIFY changed)
    Q_PROPERTY(QString slideText READ slideText NOTIFY changed)
    Q_PROPERTY(int selected READ selected WRITE select NOTIFY changed)
    Q_PROPERTY(int selectionFirst READ selectionFirst NOTIFY changed)
    Q_PROPERTY(int selectionLast READ selectionLast NOTIFY changed)
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(QString path READ path NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString sizeLabel READ sizeLabel NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QStringList fontNames READ fontNames CONSTANT)
    Q_PROPERTY(QString fontName READ fontName NOTIFY changed)
    Q_PROPERTY(QStringList themeNames READ themeNames CONSTANT)
    Q_PROPERTY(QString themeName READ themeName NOTIFY changed)
    Q_PROPERTY(QColor background READ background NOTIFY changed)
    Q_PROPERTY(QColor foreground READ foreground NOTIFY changed)
    Q_PROPERTY(QColor accent READ accent NOTIFY changed)
    Q_PROPERTY(QVariantMap media READ media NOTIFY changed)
  public:
    explicit Deck(QObject *parent = nullptr);
    enum { TitleRole = Qt::UserRole + 1, NumberRole };
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    QString source() const { return m_source; }
    QString slideSource() const;
    QString slideText() const;
    int selected() const { return m_selected; }
    int selectionFirst() const { return qMin(m_anchor, m_selected); }
    int selectionLast() const { return qMax(m_anchor, m_selected); }
    int selectionCount() const { return selectionLast() - selectionFirst() + 1; }
    int count() const { return m_parsed.slides.size(); }
    int revision() const { return m_revision; }
    bool dirty() const { return m_source != m_saved; }
    QString path() const { return m_path; }
    QString title() const;
    qint64 totalBytes() const;
    QString sizeLabel() const;
    QString status() const { return m_status; }
    QStringList fontNames() const;
    QString fontName() const;
    Q_INVOKABLE void chooseFont(const QString &family);
    QStringList themeNames() const;
    QString themeName() const;
    QColor background() const;
    QColor foreground() const;
    QColor accent() const;
    QVariantMap palette() const;
    QVariantMap media() const;
    QString baseDir() const;
    QString dialogDirectory() const;
    QString slide(int index) const;
    bool loadPath(const QString &path);
    bool reopenLastPresentation();
    bool savePath(const QString &path);
    bool exportPdf(const QString &path);
    bool exportPptx(const QString &path);
    bool renderImages(const QString &directory, int width = 1920, bool convertAnimations = false);
    Q_INVOKABLE void select(int index);
    Q_INVOKABLE void extendSelection(int index);
    Q_INVOKABLE void moveSelection(int direction);
    Q_INVOKABLE void dropSelection(int slot);
    Q_INVOKABLE void selectAt(int position);
    Q_INVOKABLE int sourcePosition() const;
    Q_INVOKABLE void editSource(const QString &value);
    Q_INVOKABLE void editSlide(const QString &value);
    Q_INVOKABLE void moveSlide(int from, int to);
    Q_INVOKABLE void addSlide();
    Q_INVOKABLE void duplicateSlide();
    Q_INVOKABLE void deleteSlide();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void chooseTheme(const QString &name);
    Q_INVOKABLE void openDialog();
    Q_INVOKABLE void save();
    Q_INVOKABLE void saveAs();
    Q_INVOKABLE void newDeck();
    Q_INVOKABLE void importDialog();
    Q_INVOKABLE void importMedia(const QUrl &url);
    Q_INVOKABLE bool pasteMedia();
    Q_INVOKABLE QString savePastedMedia(const QString &name);
    Q_INVOKABLE void cancelPaste();
    Q_INVOKABLE void exportDialog(const QString &format);
    Q_INVOKABLE QString renderId(int index) const;
    Q_INVOKABLE void matchImageBackground(bool enabled);
    Q_INVOKABLE void setImageBackground(const QString &mode);
    Q_INVOKABLE void setMediaMode(const QString &mode);
    Q_INVOKABLE void setStatus(const QString &status);
  signals:
    void changed();
    void statusChanged();
    void pasteRequested(const QString &name, const QString &extension, bool video);

  private:
    struct State {
        QString source;
        int selected;
        int anchor;
    };
    mutable QString m_paletteHeader, m_mediaSource, m_mediaBase;
    mutable QString m_sizeSource, m_sizeBase;
    mutable qint64 m_totalBytes = -1;
    mutable QVariantMap m_mediaCache;
    mutable QVariantMap m_paletteCache;
    QString m_source, m_saved, m_path, m_status;
    ParsedDeck m_parsed;
    int m_selected = 0, m_anchor = 0, m_revision = 0;
    QVector<State> m_undo, m_redo;
    QMap<QString, QString> m_themes;
    QFileSystemWatcher m_watcher;
    bool m_externalChange = false;
    struct PendingPaste {
        QString source, extension, path, document;
        QImage image;
        QByteArray data;
        bool video = false;
        int selected = 0;
    } m_paste;
    void apply(const QString &source, int selected, bool history = true, int anchor = -1);
    void replaceSlides(const QStringList &slides, int selected, int anchor = -1);
    bool confirmDiscard();
    void discoverThemes();
    void watch();
};
