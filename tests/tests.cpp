#include "deck.h"
#include "renderer.h"
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QPdfDocument>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>

class HypeTests : public QObject {
    Q_OBJECT
    static void write(const QString &path, const QString &content) {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(content.toUtf8());
    }
  private slots:
    void fencesAndFrontMatter() {
        QString source = "---\ntitle: Test\n---\n\n# "
                         "One\n\n---\n\n````ruby\n---\n```\n````\n\n---\n\n~~~sh\n---\n~~~\n";
        auto parsed = parseDeck(source);
        QCOMPARE(parsed.slides.size(), 3);
        QVERIFY(parsed.error.isEmpty());
        QCOMPARE(scalar(parsed.header, "title"), "Test");
        QVERIFY(parsed.slides[1].source.contains("---"));
        for (auto &s : parsed.slides)
            QCOMPARE(source.mid(s.start, s.end - s.start), s.source);
    }
    void emptyBoundariesAndUnicode() {
        auto p = parseDeck("# Æble 🍎\n---\n\n---\n");
        QCOMPARE(p.slides.size(), 3);
        QCOMPARE(p.slides[0].source, QString::fromUtf8("# Æble 🍎\n"));
        QCOMPARE(p.slides[2].source, QString());
    }
    void reorderDuplicateUndoSave() {
        QTemporaryDir tmp;
        QString path = tmp.path() + "/talk.md";
        QString original = "---\ntitle: Test\n---\n\n# One\n\n---\n\n<!-- keep me "
                           "-->\n# Two\n\n---\n\n# Three";
        write(path, original);
        Deck d;
        QVERIFY(d.loadPath(path));
        QString third = d.slide(2), second = d.slide(1);
        d.moveSlide(2, 0);
        QCOMPARE(d.selected(), 0);
        QVERIFY(d.slide(0).startsWith(third));
        QCOMPARE(d.slide(2), second);
        d.undo();
        QCOMPARE(d.source(), original);
        d.redo();
        QVERIFY(d.slide(0).startsWith(third));
        d.select(2);
        d.duplicateSlide();
        QCOMPARE(d.count(), 4);
        QCOMPARE(d.slide(2), d.slide(3));
        d.addSlide();
        QCOMPARE(d.count(), 5);
        QVERIFY(d.slideSource().trimmed().isEmpty());
        d.editSlide("# New");
        QCOMPARE(d.count(), 5);
        QVERIFY(d.savePath(path));
        Deck reopened;
        QVERIFY(reopened.loadPath(path));
        QCOMPARE(reopened.source(), d.source());
        d.undo();
        QVERIFY(d.slideSource().trimmed().isEmpty());
        d.undo();
        QCOMPARE(d.count(), 4);
    }
    void editingCannotEatSeparator() {
        Deck d;
        d.editSource("# One\n---\n# Two\n");
        d.select(0);
        d.editSlide("# Changed");
        QCOMPARE(d.count(), 2);
        QCOMPARE(d.slide(1), QString("# Two\n"));
    }
    void deleteLastAndUndo() {
        Deck d;
        QString original = d.source();
        d.deleteSlide();
        QCOMPARE(d.count(), 1);
        QVERIFY(d.slideSource().trimmed().isEmpty());
        d.undo();
        QCOMPARE(d.source(), original);
    }
    void saveDetectsExternalChanges() {
        QTemporaryDir tmp;
        QString path = tmp.path() + "/talk.md";
        write(path, "# Original\n");
        Deck d;
        QVERIFY(d.loadPath(path));
        d.editSlide("# Local\n");
        write(path, "# External\n");
        QVERIFY(!d.savePath(path));
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("# External\n"));
    }
    void mediaDefaultsAndDirectives() {
        auto m = parseMedia("![](<City at night.JPG>)\n\n# Hello", "/tmp/deck");
        QCOMPARE(m.path, QString("/tmp/deck/images/City at night.JPG"));
        QVERIFY(m.span);
        QCOMPARE(m.overlay, .25);
        m = parseMedia("![fit](images/chart.png)\n\n# Chart", "/tmp/deck");
        QVERIFY(!m.span);
        QCOMPARE(m.path, QString("/tmp/deck/images/chart.png"));
        m = parseMedia("![span loop muted poster=\"demo still.jpg\"](demo.MP4)", "/tmp/deck");
        QVERIFY(m.video && m.span && m.loop && m.muted);
        QCOMPARE(m.poster, QString("/tmp/deck/images/demo still.jpg"));
        m = parseMedia("![autoplay=false](demo.mp4)", "/tmp/deck");
        QVERIFY(!m.autoplay);
        m = parseMedia("![span fit](photo.jpg)", "/tmp/deck");
        QVERIFY(!m.error.isEmpty());
        m = parseMedia("![left](photo.jpg)\n\n# Text", "/tmp/deck");
        QCOMPARE(m.side, QString("left"));
        QVERIFY(!m.span);
        m = parseMedia("![left span](photo.jpg)", "/tmp/deck");
        QVERIFY(!m.error.isEmpty());
    }
    void codeIsNotMedia() {
        QString source = "```markdown\n![](missing.png)\n<!-- Keep this code -->\n```";
        auto media = parseMedia(source, "/tmp");
        QVERIFY(media.file.isEmpty());
        QVERIFY(media.text.contains("<!-- Keep this code -->"));
        QVERIFY(slideProblems(source, "/tmp").isEmpty());
        QVERIFY(parseMedia("An inline `![](missing.png)` example.", "/tmp").file.isEmpty());
    }
    void visualOperations() {
        if (!qEnvironmentVariableIsSet("HYPE_GUI_TESTS"))
            QSKIP("Set HYPE_GUI_TESTS=1 with local multimedia access");
        QQuickStyle::setStyle("Basic");
        qmlRegisterType<SlideItem>("Hype", 1, 0, "SlideCanvas");
        Deck d;
        d.editSource("# One\n\n---\n\n# Two\n\n---\n\n# Three\n");
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("deck", &d);
        engine.addImageProvider("slides", new Thumbnails(&d));
        engine.load(QUrl("qrc:/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects()[0]);
        QVERIFY(window);
        QTest::qWait(300);
        auto list = window->findChild<QQuickItem *>("thumbnails");
        QVERIFY(list);
        auto a = list->mapToScene(QPointF(100, 50)).toPoint(),
             b = list->mapToScene(QPointF(100, 340)).toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, a);
        QTest::mouseMove(window, b, 100);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, b);
        QCOMPARE(d.selected(), 2);
        QVERIFY(d.slide(2).contains("# One"));
        auto duplicate = window->findChild<QQuickItem *>("duplicateButton");
        QVERIFY(duplicate);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          duplicate->mapToScene(QPointF(40, 20)).toPoint());
        QCOMPARE(d.count(), 4);
        QVERIFY(d.slide(3).contains("# One"));
        auto add = window->findChild<QQuickItem *>("newSlideButton");
        QVERIFY(add);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          add->mapToScene(QPointF(40, 20)).toPoint());
        QCOMPARE(d.count(), 5);
        QVERIFY(d.slideSource().trimmed().isEmpty());
        d.undo();
        QCOMPARE(d.count(), 4);
        d.undo();
        QCOMPARE(d.count(), 3);
        d.undo();
        QVERIFY(d.slide(0).contains("# One"));
        QString trial = QFINDTESTDATA("../trials/rails-world-2023/presentation.md");
        if (!trial.isEmpty()) {
            QVERIFY(d.loadPath(trial));
            d.select(48);
            auto player = window->findChild<QObject *>("player");
            QVERIFY(player);
            QVERIFY(QMetaObject::invokeMethod(player, "play"));
            QTRY_COMPARE_WITH_TIMEOUT(player->property("playbackState").toInt(), 1, 10000);
            d.select(49);
            QTRY_COMPARE(player->property("playbackState").toInt(), 0);
        }
        QString many;
        for (int i = 0; i < 40; ++i)
            many += (i ? "\n---\n" : "") + QString("# Slide %1\n").arg(i);
        d.editSource(many);
        d.select(0);
        auto stage = window->findChild<QQuickItem *>("stage");
        stage->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_Right);
        QCOMPARE(d.selected(), 1);
        QTest::keyClick(window, Qt::Key_Left);
        QCOMPARE(d.selected(), 0);
        QTest::qWait(100);
        auto point = list->mapToScene(QPointF(100, 70));
        QWheelEvent wheel(point, window->mapToGlobal(point.toPoint()), QPoint(), QPoint(0, -120),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(window, &wheel);
        QTRY_VERIFY(list->property("contentY").toReal() > 0);
        d.select(20);
        window->setProperty("markdown", true);
        QVERIFY(QMetaObject::invokeMethod(window, "alignSource"));
        QTest::qWait(100);
        auto editor = window->findChild<QQuickItem *>("sourceEditor");
        QCOMPARE(editor->property("cursorPosition").toInt(), d.sourcePosition());
        auto scroll = window->findChild<QObject *>("sourceScroll");
        auto flick = scroll->property("contentItem").value<QObject *>();
        QVERIFY(flick->property("contentY").toReal() > 100);
        QRectF rectangle;
        QVERIFY(QMetaObject::invokeMethod(editor, "positionToRectangle",
                                          Q_RETURN_ARG(QRectF, rectangle),
                                          Q_ARG(int, d.sourcePosition())));
        QVERIFY(qAbs(flick->property("contentY").toReal() -
                     (rectangle.y() - editor->property("topPadding").toReal())) < 2);
        int selected = d.selected();
        QTest::keyClick(window, Qt::Key_Right);
        QCOMPARE(d.selected(), selected);
        window->setProperty("allowClose", true);
        window->close();
    }
    void mediaImportCollisionAndPortability() {
        QTemporaryDir tmp;
        QDir().mkdir(tmp.path() + "/deck");
        QDir().mkdir(tmp.path() + "/source");
        Deck d;
        QVERIFY(d.savePath(tmp.path() + "/deck/presentation.md"));
        QImage a(10, 10, QImage::Format_RGB32);
        a.fill(Qt::red);
        a.save(tmp.path() + "/source/photo.png");
        d.importMedia(QUrl::fromLocalFile(tmp.path() + "/source/photo.png"));
        d.importMedia(QUrl::fromLocalFile(tmp.path() + "/source/photo.png"));
        QCOMPARE(QDir(tmp.path() + "/deck/images").entryList(QDir::Files).size(), 1);
        a.fill(Qt::blue);
        a.save(tmp.path() + "/source/photo.png");
        d.importMedia(QUrl::fromLocalFile(tmp.path() + "/source/photo.png"));
        QCOMPARE(QDir(tmp.path() + "/deck/images").entryList(QDir::Files).size(), 2);
        QVERIFY(d.source().contains("photo-2.png"));
    }
    void themeSnapshot() {
        Deck d;
        if (!d.themeNames().contains("tokyo-night"))
            QSKIP("Tokyo Night is not installed");
        d.chooseTheme("tokyo-night");
        QVERIFY(d.source().contains("color_background:"));
        QCOMPARE(d.background(), QColor("#1a1b26"));
        QString before = d.source();
        d.chooseTheme("nord");
        d.undo();
        QCOMPARE(d.source(), before);
    }
    void trialRenderingPerformance() {
        if (!qEnvironmentVariableIsSet("HYPE_BENCHMARK"))
            QSKIP("Set HYPE_BENCHMARK=1 for trial rendering timings");
        Deck d;
        QString path = QFINDTESTDATA("../trials/rails-world-2025/presentation.md");
        QVERIFY(d.loadPath(path));
        Thumbnails provider(&d);
        QVector<QString> ids;
        for (int i = 0; i < d.count(); ++i)
            ids.append(d.renderId(i));
        for (int pass = 0; pass < 2; ++pass) {
            QElapsedTimer timer;
            timer.start();
            for (const QString &id : ids) {
                QSize size;
                QVERIFY(!provider.requestImage(id, &size, QSize(340, 192)).isNull());
            }
            qInfo() << (pass ? "Cached" : "Cold") << ids.size()
                    << "trial thumbnails:" << timer.elapsed() << "ms";
        }
    }
    void backgroundAndCache() {
        QTemporaryDir tmp;
        QDir().mkpath(tmp.path() + "/images");
        QImage artwork(100, 100, QImage::Format_ARGB32);
        artwork.fill(QColor("#f8f5f2"));
        {
            QPainter painter(&artwork);
            painter.fillRect(QRect(5, 5, 90, 90), Qt::black);
        }
        QVERIFY(artwork.save(tmp.path() + "/images/art.png"));
        write(tmp.path() + "/talk.md", "![fit](art.png)");
        Deck d;
        QVERIFY(d.loadPath(tmp.path() + "/talk.md"));
        QString original = d.source();
        Thumbnails defaults(&d);
        QSize defaultSize;
        QCOMPARE(
            defaults.requestImage(d.renderId(0), &defaultSize, QSize(320, 180)).pixelColor(0, 0),
            QColor("#f8f5f2"));
        d.editSlide("![](art.png)");
        QCOMPARE(
            defaults.requestImage(d.renderId(0), &defaultSize, QSize(320, 180)).pixelColor(0, 0),
            QColor("#f8f5f2"));
        d.undo();
        d.matchImageBackground(true);
        QCOMPARE(parseMedia(d.slideSource(), d.baseDir()).background, QString("auto"));
        Thumbnails provider(&d);
        QSize size;
        QString id = d.renderId(0);
        auto image = provider.requestImage(id, &size, QSize(320, 180));
        QCOMPARE(image.pixelColor(0, 0), QColor("#f8f5f2"));
        QElapsedTimer timer;
        timer.start();
        for (int i = 0; i < 1000; ++i)
            QCOMPARE(provider.requestImage(id, &size, QSize(320, 180)), image);
        qInfo() << "1000 cached slide requests:" << timer.elapsed() << "ms";
        d.matchImageBackground(false);
        QCOMPARE(provider.requestImage(d.renderId(0), &size, QSize(320, 180)).pixelColor(0, 0),
                 d.background());
        d.undo();
        d.undo();
        QCOMPARE(d.source(), original);
        artwork.fill(Qt::transparent);
        QVERIFY(artwork.save(tmp.path() + "/images/art.png"));
        d.matchImageBackground(true);
        QCOMPARE(provider.requestImage(d.renderId(0), &size, QSize(320, 180)).pixelColor(0, 0),
                 d.background());
        QSignalSpy resets(&d, &QAbstractItemModel::modelReset);
        d.editSlide("# Updated");
        QCOMPARE(resets.count(), 0);
    }
    void renderAndPdf() {
        QTemporaryDir tmp;
        Deck d;
        d.editSource("# Theme\n\n---\n\n> A quote with **emphasis**.\n\n— "
                     "Author\n\n---\n\n```ruby\nclass Post\n  belongs_to "
                     ":author\nend\n```\n");
        QVERIFY(d.renderImages(tmp.path() + "/render"));
        QImage image(tmp.path() + "/render/slide-001.png");
        QCOMPARE(image.size(), QSize(1920, 1080));
        QCOMPARE(image.pixelColor(0, 0), d.background());
        QVERIFY(d.exportPdf(tmp.path() + "/talk.pdf"));
        QPdfDocument pdf;
        QCOMPARE(pdf.load(tmp.path() + "/talk.pdf"), QPdfDocument::Error::None);
        QCOMPARE(pdf.pageCount(), 3);
    }
    void missingMediaDoesNotOverwriteExport() {
        QTemporaryDir tmp;
        QString path = tmp.path() + "/talk.pdf";
        write(path, "existing");
        Deck d;
        d.editSource("![](does-not-exist.png)");
        QVERIFY(!d.exportPdf(path));
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("existing"));
    }
};
QTEST_MAIN(HypeTests)
#include "tests.moc"
