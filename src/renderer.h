#pragma once
#include "deck.h"
#include <QQuickImageProvider>
#include <QQuickPaintedItem>
struct Media {
    QString file, path, poster, error, side, background;
    bool video = false, span = false, loop = false, muted = false, autoplay = true;
    double overlay = 0;
    QString text;
};
QRectF mediaRect(const Media &media);
QString withMedia(const QString &source, const QString &reference);
Media parseMedia(const QString &source, const QString &base);
QString ensurePoster(const QString &video, const QString &base);
QStringList slideProblems(const QString &source, const QString &base);
void paintSlide(QPainter *painter, const QRectF &target, const QString &source, const QString &base,
                const QVariantMap &palette, QString *warning = nullptr, bool overlayOnly = false,
                bool backgroundOnly = false);
class SlideItem : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(Deck *deck READ deck WRITE setDeck NOTIFY deckChanged)
    Q_PROPERTY(bool overlayOnly MEMBER m_overlayOnly NOTIFY deckChanged)
  public:
    explicit SlideItem(QQuickItem *parent = nullptr);
    Deck *deck() const { return m_deck; }
    void setDeck(Deck *deck);
    void paint(QPainter *painter) override;
  signals:
    void deckChanged();

  private:
    Deck *m_deck = nullptr;
    bool m_overlayOnly = false;
};
class Thumbnails : public QQuickImageProvider {
  public:
    explicit Thumbnails(Deck *) : QQuickImageProvider(Image, ForceAsynchronousImageLoading) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};
