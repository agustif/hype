#include "apptheme.h"
#include "deck.h"
#include "renderer.h"
#include "syntax.h"
#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QImage>
#include <QMimeData>
#include <QPainter>
#include <QPdfDocument>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QtTest>

class HypeTests : public QObject {
    Q_OBJECT
    QTemporaryDir settingsDirectory;
    static void write(const QString &path, const QString &content) {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(content.toUtf8());
    }
  private slots:
    void initTestCase() {
        QVERIFY(settingsDirectory.isValid());
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    }
    void followsDesktopTheme() {
        QTemporaryDir files;
        const QString current = files.path() + "/current";
        const QString dark = files.path() + "/dark";
        const QString light = files.path() + "/light";
        QVERIFY(QDir().mkpath(current));
        QVERIFY(QDir().mkpath(dark));
        QVERIFY(QDir().mkpath(light));
        write(dark + "/colors.toml", "background = '#101020'\nforeground = '#eeeeee'\naccent = '#7788ff'\n");
        write(light + "/colors.toml", "background = '#ffffff'\nforeground = '#111111'\naccent = '#224488'\n");
        QVERIFY(QFile::link(dark, current + "/theme"));
        AppTheme theme(current);
        Deck deck;
        const QString source = deck.source();
        const auto palette = deck.palette();
        QCOMPARE(theme.colors().value("background").value<QColor>(), QColor("#101020"));
        QVERIFY(QFile::remove(current + "/theme"));
        QVERIFY(QFile::link(light, current + "/theme"));
        QTRY_COMPARE(theme.colors().value("background").value<QColor>(), QColor("#ffffff"));
        QCOMPARE(theme.colors().value("foreground").value<QColor>(), QColor("#111111"));
        QCOMPARE(theme.colors().value("accentText").value<QColor>(), QColor("#ffffff"));
        // Atomic file replacement must work repeatedly after a theme switch.
        for (const auto &color : {"#112233", "#334455"}) {
            QSaveFile file(light + "/colors.toml");
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(QByteArray("background = '") + color + "'\n");
            QVERIFY(file.commit());
            QTRY_COMPARE(theme.colors().value("background").value<QColor>(), QColor(color));
        }
        QCOMPARE(deck.source(), source);
        QCOMPARE(deck.palette(), palette);
    }
    void presentationSize() {
        QTemporaryDir files;
        QVERIFY(QDir().mkpath(files.path() + "/images"));
        write(files.path() + "/images/picture.png", "image bytes");
        const QString source = "# Héllo\n---\n![](picture.png)\n---\n![](picture.png)\n---\n![](missing.png)\n";
        write(files.path() + "/talk.md", source);
        Deck deck;
        QVERIFY(deck.loadPath(files.path() + "/talk.md"));
        QCOMPARE(deck.totalBytes(), source.toUtf8().size() + qint64(11));
        deck.select(1);
        QCOMPARE(deck.totalBytes(), source.toUtf8().size() + qint64(11));
        deck.editSource("# Text only\n");
        QCOMPARE(deck.totalBytes(), qint64(12));
        QVERIFY(!deck.sizeLabel().isEmpty());
    }
    void remembersPresentationDirectory() {
        QTemporaryDir files;
        const QString opened = files.path() + "/opened";
        const QString saved = files.path() + "/saved";
        QVERIFY(QDir().mkpath(opened));
        QVERIFY(QDir().mkpath(saved));
        write(opened + "/talk.md", "# Hello\n");
        Deck first;
        QVERIFY(first.loadPath(opened + "/talk.md"));
        Deck next;
        QCOMPARE(next.dialogDirectory(), opened);
        QVERIFY(first.savePath(saved + "/copy.md"));
        QCOMPARE(next.dialogDirectory(), saved);
        QVERIFY(!first.loadPath(files.path() + "/missing/talk.md"));
        QVERIFY(!first.savePath(files.path() + "/missing/copy.md"));
        QCOMPARE(next.dialogDirectory(), saved);
        QSettings persisted(QSettings::IniFormat, QSettings::UserScope, "hype", "hype");
        QCOMPARE(persisted.value("files/lastDirectory").toString(), saved);
        QVERIFY(QFile::exists(persisted.fileName()));
        QVERIFY(QDir(saved).removeRecursively());
        QVERIFY(next.dialogDirectory() != saved);
        QVERIFY(QDir(next.dialogDirectory()).exists());
    }
    void reopensLastPresentation() {
        QSettings settings(QSettings::IniFormat, QSettings::UserScope, "hype", "hype");
        settings.remove("files/lastPresentation");
        Deck first;
        const QString initial = first.source();
        QVERIFY(!first.reopenLastPresentation());
        QCOMPARE(first.source(), initial);

        QTemporaryDir files;
        const QString original = files.path() + "/original.md";
        const QString copy = files.path() + "/saved copy.md";
        write(original, "# Last presentation\n");
        QVERIFY(first.loadPath(original));
        Deck reopened;
        QVERIFY(reopened.reopenLastPresentation());
        QCOMPARE(reopened.path(), original);
        QCOMPARE(reopened.source(), first.source());
        QVERIFY(!reopened.dirty());

        first.editSlide("# Saved copy\n");
        QVERIFY(first.savePath(copy));
        QVERIFY(!first.loadPath(files.path() + "/missing.md"));
        QVERIFY(!first.savePath(files.path() + "/missing/copy.md"));
        Deck saved;
        QVERIFY(saved.reopenLastPresentation());
        QCOMPARE(saved.path(), copy);
        QCOMPARE(saved.source(), first.source());

        QVERIFY(QFile::remove(copy));
        Deck missing;
        QVERIFY(!missing.reopenLastPresentation());
        QVERIFY(missing.path().isEmpty());
        QCOMPARE(missing.source(), initial);
    }
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
        QCOMPARE(d.slide(0).trimmed(), third.trimmed());
        QCOMPARE(d.slide(2).trimmed(), second.trimmed());
        d.undo();
        QCOMPARE(d.source(), original);
        d.redo();
        QCOMPARE(d.slide(0).trimmed(), third.trimmed());
        d.select(2);
        d.duplicateSlide();
        QCOMPARE(d.count(), 4);
        QCOMPARE(d.slide(2).trimmed(), d.slide(3).trimmed());
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
    void singleSlideEditorPadding() {
        Deck d;
        const QString original =
            "# First\n\n---\n\n\n    indented  \n\nparagraph  \n\n\n---\n\n# Last\n";
        d.editSource(original);
        d.select(1);
        const QString content = "    indented  \n\nparagraph  ";
        QCOMPARE(d.slideText(), content);
        QCOMPARE(d.source(), original); // Viewing a slide doesn't rewrite the document.
        d.editSlide(content + "more");
        QCOMPARE(d.slideText(), content + "more");
        QCOMPARE(d.source(),
                 "# First\n\n---\n\n    indented  \n\nparagraph  more\n\n---\n\n# Last\n");
        QCOMPARE(d.count(), 3);
        d.undo();
        QCOMPARE(d.source(), original);
        d.editSlide("");
        QCOMPARE(d.slideText(), QString());
        QCOMPARE(d.source(), "# First\n\n---\n\n---\n\n# Last\n");
        QCOMPARE(d.count(), 3);
        d.editSource("---\r\ntitle: CRLF\r\n---\r\n\r\n# Title\r\n\r\n---\r\n\r\n# Next\r\n");
        d.select(0);
        QCOMPARE(d.slideText(), "# Title");
        d.editSource("# One slide\n");
        QCOMPARE(d.slideText(), "# One slide");
        d.editSlide("# Changed\n\n");
        QCOMPARE(d.source(), "# Changed\n");
    }
    void slideRangeOperations() {
        Deck d;
        const QString original = "---\ntitle: Ranges\n---\n# A\n---\n# B\n![](photo.png)\n"
                                 "---\n# C\n```text\n---\n```\n---\n# D\n---\n# E\n";
        d.editSource(original);
        const QString b = d.slide(1), c = d.slide(2);
        d.select(1);
        d.extendSelection(3);
        QCOMPARE(d.selectionCount(), 3);
        d.extendSelection(2);
        QCOMPARE(d.selectionCount(), 2);
        QCOMPARE(d.source(), original);
        d.moveSelection(1);
        QCOMPARE(d.selectionFirst(), 2);
        QCOMPARE(d.selectionLast(), 3);
        QCOMPARE(d.selected(), 3);
        QCOMPARE(d.slide(2).trimmed(), b.trimmed());
        QCOMPARE(d.slide(3).trimmed(), c.trimmed());
        d.undo();
        QCOMPARE(d.source(), original);
        QCOMPARE(d.selectionFirst(), 1);
        QCOMPARE(d.selectionLast(), 2);
        d.redo();
        QCOMPARE(d.selectionFirst(), 2);
        d.dropSelection(0);
        QCOMPARE(d.slide(0).trimmed(), b.trimmed());
        QCOMPARE(d.slide(1).trimmed(), c.trimmed());
        QCOMPARE(d.selectionCount(), 2);
        const QString atStart = d.source();
        d.moveSelection(-1);
        d.dropSelection(1);
        QCOMPARE(d.source(), atStart);

        d.select(1);
        d.extendSelection(0); // Preserve the active end of a backward range.
        d.dropSelection(d.count());
        QCOMPARE(d.selected(), 3);
        QCOMPARE(d.selectionLast(), 4);
        QCOMPARE(d.slide(3).trimmed(), b.trimmed());
        QCOMPARE(d.slide(4).trimmed(), c.trimmed());
        d.duplicateSlide();
        QCOMPARE(d.count(), 7);
        QCOMPARE(d.selectionFirst(), 5);
        QCOMPARE(d.selectionLast(), 6);
        QCOMPARE(d.slide(5).trimmed(), b.trimmed());
        QCOMPARE(d.slide(6).trimmed(), c.trimmed());
        d.deleteSlide();
        QCOMPARE(d.count(), 5);
        d.undo();
        QCOMPARE(d.selectionCount(), 2);
        QCOMPARE(d.count(), 7);
        d.select(0);
        d.extendSelection(d.count() - 1);
        d.deleteSlide();
        QCOMPARE(d.count(), 1);
        QCOMPARE(d.selectionCount(), 1);
        QVERIFY(d.slideSource().trimmed().isEmpty());
        d.undo();
        QCOMPARE(d.selectionCount(), 7);
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
    void pasteNamedMedia() {
        QTemporaryDir tmp;
        Deck d;
        d.editSource("# One\n\n---\n\n# Two\n");
        QVERIFY(d.savePath(tmp.path() + "/talk.md"));
        d.select(1);
        const QString before = d.source(), first = d.slide(0);
        QImage image(20, 12, QImage::Format_RGB32);
        image.fill(Qt::red);
        QApplication::clipboard()->setImage(image);
        QSignalSpy request(&d, &Deck::pasteRequested);
        QVERIFY(d.pasteMedia());
        QCOMPARE(request.size(), 1);
        d.cancelPaste();
        QCOMPARE(d.source(), before);
        QVERIFY(!QDir(tmp.path() + "/images").exists());
        QVERIFY(d.pasteMedia());
        QVERIFY(d.savePastedMedia("City at night.png").isEmpty());
        QCOMPARE(QImage(tmp.path() + "/images/City at night.png"), image);
        QCOMPARE(d.slide(0), first);
        QCOMPARE(d.selected(), 1);
        QVERIFY(d.slideSource().contains("# Two"));
        QCOMPARE(parseMedia(d.slideSource(), tmp.path()).file, "City at night.png");
        d.undo();
        QCOMPARE(d.source(), before);

        write(tmp.path() + "/original.webm", "video file bytes");
        auto files = new QMimeData;
        files->setUrls({QUrl::fromLocalFile(tmp.path() + "/original.webm")});
        files->setImageData(image); // File-manager thumbnails must not replace the video.
        QApplication::clipboard()->setMimeData(files);
        QVERIFY(d.pasteMedia());
        QVERIFY(d.savePastedMedia("Demo").isEmpty());
        QFile video(tmp.path() + "/videos/Demo.webm");
        QVERIFY(video.open(QIODevice::ReadOnly));
        QCOMPARE(video.readAll(), QByteArray("video file bytes"));
        QVERIFY(parseMedia(d.slideSource(), tmp.path()).video);
        auto raw = new QMimeData;
        raw->setData("video/mp4", "raw video bytes");
        QApplication::clipboard()->setMimeData(raw);
        QVERIFY(d.pasteMedia());
        QVERIFY(d.savePastedMedia("Second demo").isEmpty());
        QFile rawVideo(tmp.path() + "/videos/Second demo.mp4");
        QVERIFY(rawVideo.open(QIODevice::ReadOnly));
        QCOMPARE(rawVideo.readAll(), QByteArray("raw video bytes"));
        QVERIFY(!d.slideSource().contains("Demo.webm"));
        QCOMPARE(parseMedia(d.slideSource(), tmp.path()).file, "Second demo.mp4");
        QApplication::clipboard()->setText("ordinary text");
        QVERIFY(!d.pasteMedia());
    }
    void pastedSlidesKeepBalancedSpacing() {
        QTemporaryDir tmp;
        Deck d;
        d.editSource("# Start\n");
        QVERIFY(d.savePath(tmp.path() + "/talk.md"));
        QImage image(10, 10, QImage::Format_RGB32);
        image.fill(Qt::red);
        QApplication::clipboard()->setImage(image);
        d.addSlide();
        QVERIFY(d.pasteMedia());
        QVERIFY(d.savePastedMedia("first").isEmpty());
        d.addSlide();
        QVERIFY(d.pasteMedia());
        QVERIFY(d.savePastedMedia("second").isEmpty());
        d.addSlide();
        d.editSlide("# End");
        const QString expected = "# Start\n\n---\n\n![](<first.png>)\n\n---\n\n"
                                 "![](<second.png>)\n\n---\n\n# End\n";
        QCOMPARE(d.source(), expected);
        d.moveSlide(1, 2);
        QCOMPARE(d.source(), "# Start\n\n---\n\n![](<second.png>)\n\n---\n\n"
                             "![](<first.png>)\n\n---\n\n# End\n");
        d.undo();
        QCOMPARE(d.source(), expected);
        d.select(1);
        QCOMPARE(d.slideText(), "![](<first.png>)");
        d.addSlide();
        QCOMPARE(d.count(), 5);
        QCOMPARE(d.slideText(), QString());
        QVERIFY(d.source().contains("![](<first.png>)\n\n---\n\n---\n\n![](<second.png>)"));
    }
    void pasteValidatesNames() {
        QTemporaryDir tmp;
        Deck d;
        QVERIFY(d.savePath(tmp.path() + "/talk.md"));
        QVERIFY(QDir().mkpath(tmp.path() + "/images"));
        write(tmp.path() + "/images/existing.png", "keep existing");
        QImage image(10, 10, QImage::Format_RGB32);
        image.fill(Qt::blue);
        QApplication::clipboard()->setImage(image);
        QVERIFY(d.pasteMedia());
        QVERIFY(d.savePastedMedia("../outside").contains("without folders"));
        QVERIFY(d.savePastedMedia("existing").contains("already exists"));
        QVERIFY(d.savePastedMedia("new name").isEmpty());
        QCOMPARE(QImage(tmp.path() + "/images/new name.png"), image);
        QVERIFY(!QFile::exists(tmp.path() + "/outside.png"));
        QFile existing(tmp.path() + "/images/existing.png");
        QVERIFY(existing.open(QIODevice::ReadOnly));
        QCOMPARE(existing.readAll(), QByteArray("keep existing"));
        QVERIFY(d.pasteMedia());
        d.editSlide("# Changed while naming media");
        QVERIFY(d.savePastedMedia("wrong slide").contains("slide changed"));
        QVERIFY(!QFile::exists(tmp.path() + "/images/wrong slide.png"));
        d.cancelPaste();
    }
    void replaceSlideMedia() {
        const QString examples = "<!-- ![](comment.png) -->\n```md\n![](example.png)\n```\n"
                                 "Inline `![](inline.png)`\n# Title\n";
        const QString replacement = "![](<new.png>)";
        QCOMPARE(withMedia(examples + "![fit](old.png)\nCaption", replacement),
                 examples + replacement + "\nCaption");
        QCOMPARE(withMedia(examples, replacement), examples + "\n" + replacement + "\n");
    }
    void animatedImages() {
        if (!qEnvironmentVariableIsSet("HYPE_GUI_TESTS"))
            QSKIP("Set HYPE_GUI_TESTS=1 with local multimedia access");
        QTemporaryDir files;
        QVERIFY(QDir().mkpath(files.path() + "/images"));
        QVERIFY(QFile::copy(QFINDTESTDATA("fixtures/animated.webp"),
                            files.path() + "/images/demo.webp"));
        QImage still(64, 48, QImage::Format_RGB32);
        still.fill(Qt::green);
        QVERIFY(still.save(files.path() + "/images/still.webp"));
        write(files.path() + "/talk.md",
              "![fit background=#123456](demo.webp)\n\n---\n\n![](still.webp)\n");
        Deck d;
        QVERIFY(d.loadPath(files.path() + "/talk.md"));
        QVERIFY(d.media()["animated"].toBool());
        // Animation's transparent frames must reveal the background, not its first frame.
        Thumbnails provider(&d);
        const QImage background =
            provider.requestImage(d.renderId(0) + "/background", nullptr, QSize(192, 108));
        QCOMPARE(background.pixelColor(96, 54), QColor("#123456"));
        const QImage thumbnail = provider.requestImage(d.renderId(0), nullptr, QSize(192, 108));
        QCOMPARE(thumbnail.pixelColor(96, 54), QColor(Qt::red));
        QQuickStyle::setStyle("Basic");
        qmlRegisterType<SlideItem>("Hype", 1, 0, "SlideCanvas");
        qmlRegisterType<AppTheme>("Hype", 1, 0, "AppTheme");
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("deck", &d);
        engine.addImageProvider("slides", new Thumbnails(&d));
        engine.load(QUrl("qrc:/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects()[0]);
        auto loader = window->findChild<QObject *>("animationLoader");
        QVERIFY(loader);
        QTRY_VERIFY(loader->property("item").value<QObject *>());
        auto animation = loader->property("item").value<QObject *>();
        QTRY_COMPARE(animation->property("frameCount").toInt(), 3);
        const int frame = animation->property("currentFrame").toInt();
        QTRY_VERIFY(animation->property("currentFrame").toInt() != frame);
        animation->setProperty("paused", true);
        const int paused = animation->property("currentFrame").toInt();
        QTest::qWait(450);
        QCOMPARE(animation->property("currentFrame").toInt(), paused);
        window->setProperty("markdown", true);
        QTRY_VERIFY(!loader->property("active").toBool());
        QTRY_VERIFY(!loader->property("item").value<QObject *>());
        window->setProperty("markdown", false);
        QTRY_VERIFY(loader->property("item").value<QObject *>());
        d.select(1);
        QVERIFY(!d.media()["animated"].toBool());
        QTRY_VERIFY(!loader->property("active").toBool());
        QTRY_VERIFY(!loader->property("item").value<QObject *>());
        d.select(0);
        d.editSlide("![right autoplay=false](demo.webp)\n\n# Caption");
        QTRY_VERIFY(loader->property("item").value<QObject *>());
        animation = loader->property("item").value<QObject *>();
        QTRY_VERIFY(animation->property("paused").toBool());
        QCOMPARE(d.media()["side"].toString(), QString("right"));
        window->setProperty("presenting", true);
        QTest::keyClick(window, Qt::Key_Space);
        QTRY_VERIFY(!animation->property("paused").toBool());
        QTest::keyClick(window, Qt::Key_Space);
        QTRY_VERIFY(animation->property("paused").toBool());
    }
    void visualOperations() {
        if (!qEnvironmentVariableIsSet("HYPE_GUI_TESTS"))
            QSKIP("Set HYPE_GUI_TESTS=1 with local multimedia access");
        QQuickStyle::setStyle("Basic");
        qmlRegisterType<SlideItem>("Hype", 1, 0, "SlideCanvas");
        qmlRegisterType<AppTheme>("Hype", 1, 0, "AppTheme");
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
        const double slideStep = list->property("slideStep").toDouble();
        auto a = list->mapToScene(QPointF(100, 50)).toPoint(),
             b = list->mapToScene(QPointF(100, 2 * slideStep + 90)).toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, a);
        for (int step = 1; step <= 20; ++step)
            QTest::mouseMove(window, a + (b - a) * step / 20, 10);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, b);
        QCOMPARE(d.selected(), 2);
        QVERIFY(d.slide(2).contains("# One"));
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, b);
        for (int step = 1; step <= 20; ++step)
            QTest::mouseMove(window, b + (a - b) * step / 20, 10);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, a);
        QCOMPARE(d.selected(), 0);
        QVERIFY(d.slide(0).contains("# One"));
        d.undo();
        QCOMPARE(d.selected(), 2);
        QTest::keyClick(window, Qt::Key_D, Qt::ControlModifier);
        QCOMPARE(d.count(), 4);
        QVERIFY(d.slide(3).contains("# One"));
        QTest::keyClick(window, Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(d.count(), 5);
        QVERIFY(d.slideSource().trimmed().isEmpty());
        d.undo();
        QCOMPARE(d.count(), 4);
        d.undo();
        QCOMPARE(d.count(), 3);
        d.undo();
        QVERIFY(d.slide(0).contains("# One"));
        QTemporaryDir pasted;
        QVERIFY(d.savePath(pasted.path() + "/talk.md"));
        QImage clipboardImage(12, 12, QImage::Format_RGB32);
        clipboardImage.fill(Qt::green);
        QApplication::clipboard()->setImage(clipboardImage);
        auto pasteTarget = window->findChild<QQuickItem *>("stage");
        pasteTarget->forceActiveFocus();
        auto pasteDialog = window->findChild<QObject *>("pasteDialog");
        auto pasteName = window->findChild<QQuickItem *>("pasteName");
        QVERIFY(pasteDialog && pasteName);
        auto submitName = [&](const QString &name) {
            QTRY_VERIFY(pasteDialog->property("opened").toBool());
            QVERIFY(pasteName->hasActiveFocus());
            pasteName->setProperty("text", name);
            QTest::keyClick(window, Qt::Key_Return);
            QTRY_VERIFY(!pasteDialog->property("visible").toBool());
        };
        QTest::keyClick(window, Qt::Key_V, Qt::ControlModifier);
        QTRY_VERIFY(pasteDialog->property("opened").toBool());
        auto frame = window->findChild<QQuickItem *>("slideFrame");
        auto popupItem = pasteDialog->property("contentItem").value<QQuickItem *>();
        QVERIFY(frame && popupItem);
        const QPointF frameCenter =
            frame->mapToScene(QPointF(frame->width() / 2, frame->height() / 2));
        const QPointF popupCenter =
            popupItem->mapToScene(QPointF(popupItem->width() / 2, popupItem->height() / 2));
        QVERIFY(QLineF(frameCenter, popupCenter).length() < 2);
        const int pasteSlide = d.selected();
        QTest::keyClick(window, Qt::Key_PageDown);
        QCOMPARE(d.selected(), pasteSlide);
        QTest::keyClick(window, Qt::Key_E, Qt::ControlModifier);
        QVERIFY(!window->property("markdown").toBool());
        if (qEnvironmentVariableIsSet("HYPE_PASTE_SCREENSHOT")) {
            QTest::qWait(100);
            QVERIFY(window->grabWindow().save(qEnvironmentVariable("HYPE_PASTE_SCREENSHOT")));
        }
        submitName("canvas");
        QCOMPARE(parseMedia(d.slideSource(), pasted.path()).file, "canvas.png");
        QTRY_VERIFY(pasteTarget->hasActiveFocus());
        const QString beforeCancel = d.source();
        QTest::keyClick(window, Qt::Key_V, Qt::ControlModifier);
        QTRY_VERIFY(pasteDialog->property("opened").toBool());
        pasteName->setProperty("text", "canvas");
        QTest::keyClick(window, Qt::Key_Return);
        QVERIFY(pasteDialog->property("visible").toBool());
        QVERIFY(pasteDialog->property("error").toString().contains("already exists"));
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!pasteDialog->property("visible").toBool());
        QTRY_VERIFY(pasteTarget->hasActiveFocus());
        QCOMPARE(d.source(), beforeCancel);
        auto slideText = window->findChild<QQuickItem *>("slideEditor");
        slideText->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_V, Qt::ControlModifier);
        submitName("slide editor");
        QCOMPARE(parseMedia(d.slideSource(), pasted.path()).file, "slide editor.png");
        QVERIFY(QMetaObject::invokeMethod(window, "openMarkdown"));
        QTest::qWait(50);
        QTest::keyClick(window, Qt::Key_V, Qt::ControlModifier);
        submitName("document editor");
        QCOMPARE(parseMedia(d.slideSource(), pasted.path()).file, "document editor.png");
        auto documentText = window->findChild<QQuickItem *>("sourceEditor");
        documentText->forceActiveFocus();
        QApplication::clipboard()->setText("ordinary paste");
        QTest::keyClick(window, Qt::Key_V, Qt::ControlModifier);
        QVERIFY(d.source().contains("ordinary paste"));
        window->requestActivate();
        QTRY_VERIFY(window->isActive());
        QTest::keyClick(window, Qt::Key_E, Qt::ControlModifier);
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
            many += (i ? "\n\n---\n\n" : "") + QString("# Slide %1").arg(i);
        many += "\n";
        d.editSource(many);
        d.select(0);
        QTest::qWait(100);
        const auto dragStart = list->mapToScene(QPointF(100, 50)).toPoint();
        const auto dragEnd = list->mapToScene(QPointF(100, list->height() - 10)).toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, dragStart);
        for (int step = 1; step <= 20; ++step)
            QTest::mouseMove(window, dragStart + (dragEnd - dragStart) * step / 20, 10);
        QTRY_VERIFY_WITH_TIMEOUT(list->property("contentY").toDouble() > 900, 5000);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, dragEnd);
        QVERIFY(d.selected() > 5);
        QCOMPARE(d.slideSource().trimmed(), "# Slide 0");
        QCOMPARE(d.count(), 40);
        d.undo();
        QCOMPARE(d.source(), many);
        QCOMPARE(d.selected(), 0);
        auto stage = window->findChild<QQuickItem *>("stage");
        stage->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_Down, Qt::ControlModifier);
        QCOMPARE(d.selected(), 1);
        QCOMPARE(d.slideSource().trimmed(), "# Slide 0");
        QTest::keyClick(window, Qt::Key_Right, Qt::ControlModifier);
        QCOMPARE(d.selected(), 2);
        QCOMPARE(d.slideSource().trimmed(), "# Slide 0");
        QTest::keyClick(window, Qt::Key_Up, Qt::ControlModifier);
        QCOMPARE(d.selected(), 1);
        QTest::keyClick(window, Qt::Key_Left, Qt::ControlModifier);
        QCOMPARE(d.selected(), 0);
        QCOMPARE(d.source(), many);
        QTest::keyClick(window, Qt::Key_Up, Qt::ControlModifier);
        QCOMPARE(d.selected(), 0);
        QCOMPARE(d.source(), many);
        d.select(d.count() - 1);
        QTest::keyClick(window, Qt::Key_Down, Qt::ControlModifier);
        QCOMPARE(d.selected(), d.count() - 1);
        QCOMPARE(d.source(), many);
        d.select(0);
        QTest::keyClick(window, Qt::Key_Down, Qt::ShiftModifier);
        QCOMPARE(d.selectionCount(), 2);
        QTest::keyClick(window, Qt::Key_Right, Qt::ShiftModifier);
        QCOMPARE(d.selectionCount(), 3);
        QCOMPARE(d.selected(), 2);
        QTest::keyClick(window, Qt::Key_Up, Qt::ShiftModifier);
        QCOMPARE(d.selectionCount(), 2);
        QTest::keyClick(window, Qt::Key_Left, Qt::ShiftModifier);
        QCOMPARE(d.selectionCount(), 1);
        QCOMPARE(d.source(), many);
        QTest::qWait(100);
        const auto thirdSlide = list->mapToScene(QPointF(100, 2 * slideStep + 50)).toPoint();
        QTest::mouseClick(window, Qt::LeftButton, Qt::ShiftModifier, thirdSlide);
        QCOMPARE(d.selectionFirst(), 0);
        QCOMPARE(d.selectionLast(), 2);
        QCOMPARE(d.source(), many);
        QVERIFY(list->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_Down, Qt::ControlModifier);
        QCOMPARE(d.selectionFirst(), 1);
        QCOMPARE(d.selectionLast(), 3);
        QCOMPARE(d.slide(1).trimmed(), "# Slide 0");
        d.undo();
        QCOMPARE(d.source(), many);
        QCOMPARE(d.selectionCount(), 3);
        const auto groupStart = list->mapToScene(QPointF(100, slideStep + 50)).toPoint();
        const auto groupEnd = list->mapToScene(QPointF(100, 4 * slideStep - 30)).toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, groupStart);
        for (int step = 1; step <= 20; ++step)
            QTest::mouseMove(window, groupStart + (groupEnd - groupStart) * step / 20, 10);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, groupEnd);
        QCOMPARE(d.selectionFirst(), 1);
        QCOMPARE(d.selectionLast(), 3);
        QCOMPARE(d.slide(1).trimmed(), "# Slide 0");
        QCOMPARE(d.slide(2).trimmed(), "# Slide 1");
        QCOMPARE(d.slide(3).trimmed(), "# Slide 2");
        d.undo();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, groupStart);
        QCOMPARE(d.selectionCount(), 1);
        QCOMPARE(d.selected(), 1);
        d.select(0);
        QTest::keyClick(window, Qt::Key_Right);
        QCOMPARE(d.selected(), 1);
        QTest::keyClick(window, Qt::Key_Left);
        QCOMPARE(d.selected(), 0);
        QTest::keyClick(window, Qt::Key_Down);
        QCOMPARE(d.selected(), 1);
        QTest::keyClick(window, Qt::Key_Up);
        QCOMPARE(d.selected(), 0);
        QTest::keyClick(window, Qt::Key_PageDown);
        QCOMPARE(d.selected(), 5);
        QTest::keyClick(window, Qt::Key_PageUp);
        QCOMPARE(d.selected(), 0);
        d.select(2);
        QTest::keyClick(window, Qt::Key_PageUp);
        QCOMPARE(d.selected(), 0);
        d.select(d.count() - 3);
        QTest::keyClick(window, Qt::Key_PageDown);
        QCOMPARE(d.selected(), d.count() - 1);
        QTest::keyClick(window, Qt::Key_End);
        QCOMPARE(d.selected(), d.count() - 1);
        QTest::keyClick(window, Qt::Key_PageDown);
        QCOMPARE(d.selected(), d.count() - 1);
        QTest::keyClick(window, Qt::Key_Home);
        QCOMPARE(d.selected(), 0);
        QTest::qWait(100);
        auto point = list->mapToScene(QPointF(100, 70));
        auto wheel = [&](int angle, int pixels = 0) {
            QWheelEvent event(point, window->mapToGlobal(point.toPoint()), QPoint(0, pixels),
                              QPoint(0, angle), Qt::NoButton, Qt::NoModifier,
                              pixels ? Qt::ScrollUpdate : Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(window, &event);
        };
        QPointingDevice touchpad(
            "Test touchpad", 1001, QInputDevice::DeviceType::TouchPad,
            QPointingDevice::PointerType::Finger,
            QInputDevice::Capability::Position | QInputDevice::Capability::Scroll, 5, 0);
        QWheelEvent touchpadWheel(point, window->mapToGlobal(point.toPoint()),
                                  QPoint(0, -qRound(slideStep)), QPoint(0, -120), Qt::NoButton,
                                  Qt::NoModifier, Qt::ScrollUpdate, false,
                                  Qt::MouseEventSynthesizedBySystem, &touchpad);
        QCoreApplication::sendEvent(window, &touchpadWheel);
        QCOMPARE(d.selected(), 1);
        QWheelEvent pixelWheel(point, window->mapToGlobal(point.toPoint()),
                               QPoint(0, -qRound(slideStep)), QPoint(), Qt::NoButton,
                               Qt::NoModifier, Qt::ScrollUpdate, false,
                               Qt::MouseEventSynthesizedBySystem, &touchpad);
        QCoreApplication::sendEvent(window, &pixelWheel);
        QCOMPARE(d.selected(), 2);
        d.select(0);
        QTest::qWait(60);
        wheel(-120);
        QCOMPARE(d.selected(), 1);
        wheel(-120);
        wheel(-120);
        wheel(-60);
        QCOMPARE(d.selected(), 3);
        wheel(-60);
        QCOMPARE(d.selected(), 4);
        wheel(120);
        QCOMPARE(d.selected(), 3);
        wheel(-120, -17);
        QCOMPARE(d.selected(), 4);
        wheel(12000);
        QCOMPARE(d.selected(), 0);
        wheel(-12000);
        QCOMPARE(d.selected(), d.count() - 1);
        QTest::qWait(60);
        QVERIFY(list->property("contentY").toDouble() > 0);
        d.select(20);
        auto editor = window->findChild<QQuickItem *>("slideEditor");
        QVERIFY(editor && editor->isVisible() && stage->isVisible());
        editor->forceActiveFocus();
        QVERIFY(editor->hasActiveFocus());
        const QString beforeJump = d.source();
        QTest::keyClick(window, Qt::Key_End);
        QCOMPARE(d.selected(), d.count() - 1);
        QTest::keyClick(window, Qt::Key_Home);
        QCOMPARE(d.selected(), 0);
        QCOMPARE(d.source(), beforeJump);
        d.select(20);
        const QString beforeTab = d.source();
        QTest::keyClick(window, Qt::Key_Tab);
        QVERIFY(list->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_Tab);
        QVERIFY(editor->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_Tab, Qt::ShiftModifier);
        QVERIFY(list->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_Tab, Qt::ShiftModifier);
        QVERIFY(editor->hasActiveFocus());
        QCOMPARE(d.source(), beforeTab);
        QCOMPARE(editor->property("text").toString(), d.slideText());
        QVERIFY(editor->mapToScene(QPointF()).y() >=
                stage->mapToScene(QPointF(0, stage->height())).y());
        QString before = d.slideText();
        QTest::keyClick(window, Qt::Key_End, Qt::ControlModifier);
        QTest::keyClick(window, Qt::Key_X);
        QCOMPARE(d.slideSource().trimmed(), (before + "x").trimmed());
        QTest::keyClick(window, Qt::Key_Return);
        QTest::keyClick(window, Qt::Key_Return);
        QCOMPARE(editor->property("text").toString(), before + "x\n\n");
        QTest::keyClick(window, Qt::Key_Y);
        QCOMPARE(editor->property("text").toString(), before + "x\n\ny");
        QCOMPARE(d.slideText(), before + "x\n\ny");
        QCOMPARE(editor->property("cursorPosition").toInt(), d.slideText().size());
        QString edited = d.source();
        d.select(21); // Selection while editing must not write the new slide over the old one.
        QCOMPARE(d.source(), edited);
        QCOMPARE(editor->property("text").toString(), d.slideText());
        QCOMPARE(editor->property("cursorPosition").toInt(), 0);
        int selected = d.selected();
        const QString beforeShift = d.source();
        QTest::keyClick(window, Qt::Key_Right, Qt::ShiftModifier);
        QCOMPARE(d.selected(), selected);
        QCOMPARE(d.source(), beforeShift);
        QVERIFY(editor->property("selectedText").toString().size() > 0);
        QTest::keyClick(window, Qt::Key_Right);
        QCOMPARE(d.selected(), selected);
        QTest::keyClick(window, Qt::Key_Down);
        QCOMPARE(d.selected(), selected);
        QTest::keyClick(window, Qt::Key_Up);
        QCOMPARE(d.selected(), selected);
        QTest::keyClick(window, Qt::Key_PageDown);
        QCOMPARE(d.selected(), selected);
        QTest::keyClick(window, Qt::Key_PageUp);
        QCOMPARE(d.selected(), selected);
        QString beforeToggle = d.source();
        QTest::keyClick(window, Qt::Key_E, Qt::ControlModifier);
        QVERIFY(window->property("markdown").toBool());
        auto source = window->findChild<QQuickItem *>("sourceEditor");
        QVERIFY(source && source->isVisible() && source->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_Tab);
        QVERIFY(list->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_Tab);
        QVERIFY(source->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_Tab, Qt::ShiftModifier);
        QVERIFY(list->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_Tab, Qt::ShiftModifier);
        QVERIFY(source->hasActiveFocus());
        QCOMPARE(d.source(), beforeToggle);
        QVERIFY(!stage->isVisible());
        QCOMPARE(source->property("text").toString(), d.source());
        QCOMPARE(source->property("cursorPosition").toInt(), d.sourcePosition());
        QTest::qWait(60);
        auto scroll = window->findChild<QObject *>("sourceScroll");
        auto flick = scroll->property("contentItem").value<QObject *>();
        QRectF rect;
        QVERIFY(QMetaObject::invokeMethod(source, "positionToRectangle", Q_RETURN_ARG(QRectF, rect),
                                          Q_ARG(int, d.sourcePosition())));
        QVERIFY(qAbs(flick->property("contentY").toDouble() - rect.y() +
                     source->property("topPadding").toDouble()) < 2);
        QCOMPARE(d.source(), beforeToggle);
        QTest::keyClick(window, Qt::Key_Home, Qt::ControlModifier);
        QCOMPARE(source->property("cursorPosition").toInt(), 0);
        QTest::keyClick(window, Qt::Key_PageDown);
        QVERIFY(source->property("cursorPosition").toInt() > 0);
        QVERIFY(flick->property("contentY").toDouble() > 0);
        QTest::keyClick(window, Qt::Key_PageUp);
        QCOMPARE(source->property("cursorPosition").toInt(), 0);
        QTest::keyClick(window, Qt::Key_Right);
        QTest::keyClick(window, Qt::Key_End);
        QCOMPARE(source->property("cursorPosition").toInt(), d.source().indexOf('\n'));
        QTest::keyClick(window, Qt::Key_Home);
        QCOMPARE(source->property("cursorPosition").toInt(), 0);
        QTest::keyClick(window, Qt::Key_End, Qt::ControlModifier);
        QCOMPARE(source->property("cursorPosition").toInt(), d.source().size());
        QTest::keyClick(window, Qt::Key_Home, Qt::ControlModifier);
        QTest::qWait(60);
        auto sourceFlick = window->findChild<QQuickItem *>("sourceFlick");
        QPointF sourcePoint = sourceFlick->mapToScene(QPointF(100, 100));
        double initialScroll = flick->property("contentY").toDouble();
        QWheelEvent textWheel(sourcePoint, window->mapToGlobal(sourcePoint.toPoint()), QPoint(),
                              QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase,
                              false);
        QCoreApplication::sendEvent(window, &textWheel);
        QCOMPARE(flick->property("contentY").toDouble(), initialScroll + 180);
        QCOMPARE(d.source(), beforeToggle);

        QTest::keyClick(window, Qt::Key_E, Qt::ControlModifier);
        QVERIFY(!window->property("markdown").toBool());
        QVERIFY(stage->isVisible() && editor->isVisible());
        QVERIFY(stage->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_Space, Qt::ControlModifier);
        QVERIFY(window->property("presenting").toBool());
        QVERIFY(!window->findChild<QQuickItem *>("editorPane")->isVisible());
        QVERIFY(stage->isVisible());
        QTest::keyClick(window, Qt::Key_Space, Qt::ControlModifier);
        QVERIFY(!window->property("presenting").toBool());
        QVERIFY(editor->isVisible());
        QString trial2025 = QFINDTESTDATA("../trials/rails-world-2025/presentation.md");
        if (!trial2025.isEmpty()) {
            QVERIFY(d.loadPath(trial2025));
            for (int slide : {1, 60, 121}) {
                d.select(slide);
                QVERIFY(QMetaObject::invokeMethod(window, "openMarkdown"));
                QTest::qWait(100);
                QCOMPARE(source->property("text").toString(), d.source());
                QImage screenshot = window->grabWindow();
                auto sourceFlick = window->findChild<QQuickItem *>("sourceFlick");
                QVERIFY(sourceFlick);
                QRect area(sourceFlick->mapToScene(QPointF(40, 30)).toPoint(), QSize(700, 300));
                int textPixels = 0;
                for (int y = area.top(); y < area.bottom(); ++y)
                    for (int x = area.left(); x < area.right(); ++x) {
                        QColor pixel = screenshot.pixelColor(x, y);
                        if (pixel.red() > 120 && pixel.green() > 120 && pixel.blue() > 120)
                            ++textPixels;
                    }
                QVERIFY2(textPixels > 200,
                         "Scrolled Markdown viewport must draw source text, not a blank pane");
            }
        }
        d.editSource("# Short document\n");
        QVERIFY(QMetaObject::invokeMethod(window, "openMarkdown"));
        QTest::qWait(60);
        for (int i = 0; i < 3; ++i) {
            QTest::keyClick(window, Qt::Key_Return, Qt::ControlModifier);
            QTest::qWait(60);
            QCOMPARE(d.count(), i + 2);
            QCOMPARE(flick->property("contentY").toDouble(), 0.0);
            QVERIFY(flick->property("contentHeight").toDouble() <
                    flick->property("height").toDouble());
        }
        d.editSource(many);
        d.select(10);
        QVERIFY(QMetaObject::invokeMethod(window, "openMarkdown"));
        QTest::qWait(60);
        double middleY = flick->property("contentY").toDouble();
        QVERIFY(QMetaObject::invokeMethod(window, "addSlide"));
        QTest::qWait(60);
        QCOMPARE(flick->property("contentY").toDouble(), middleY);
        d.select(d.count() - 1);
        QVERIFY(QMetaObject::invokeMethod(window, "openMarkdown"));
        QTest::qWait(60);
        double bottomY = flick->property("contentY").toDouble();
        QVERIFY(QMetaObject::invokeMethod(window, "addSlide"));
        QTest::qWait(60);
        double addedScroll = flick->property("contentY").toDouble() - bottomY;
        QVERIFY(addedScroll >= 0 && addedScroll < 150);
        QVERIFY(QMetaObject::invokeMethod(source, "positionToRectangle", Q_RETURN_ARG(QRectF, rect),
                                          Q_ARG(int, d.sourcePosition())));
        double cursorBottom = rect.bottom() - flick->property("contentY").toDouble();
        QVERIFY(cursorBottom > 0 && cursorBottom <= flick->property("height").toDouble());
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
    void fontSelection() {
        Deck d;
        QVERIFY(!d.fontNames().isEmpty());
        const QString original = d.source();
        QString family = d.fontNames().first();
        if (family == d.fontName())
            family = d.fontNames().last();
        d.chooseFont(family);
        QCOMPARE(d.fontName(), family);
        QCOMPARE(d.palette()["font"].toString(), family);
        QString changed = d.source();
        d.chooseFont("No such installed font");
        QCOMPARE(d.source(), changed);
        QTemporaryDir tmp;
        QVERIFY(d.savePath(tmp.path() + "/font.md"));
        Deck reopened;
        QVERIFY(reopened.loadPath(tmp.path() + "/font.md"));
        QCOMPARE(reopened.fontName(), family);
        d.undo();
        QCOMPARE(d.source(), original);
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
    void blurredImageBackground() {
        QTemporaryDir tmp;
        QVERIFY(QDir().mkpath(tmp.path() + "/images"));
        QImage portrait(80, 160, QImage::Format_ARGB32_Premultiplied);
        portrait.fill(Qt::red);
        {
            QPainter painter(&portrait);
            painter.fillRect(0, 80, 80, 80, Qt::blue);
        }
        QVERIFY(portrait.save(tmp.path() + "/images/portrait.png"));
        write(tmp.path() + "/talk.md", "![fit background=theme](portrait.png)\n");
        Deck deck;
        QVERIFY(deck.loadPath(tmp.path() + "/talk.md"));
        const QString original = deck.source();
        deck.setImageBackground("blur");
        const auto media = parseMedia(deck.slideSource(), deck.baseDir());
        QVERIFY(media.error.isEmpty());
        QCOMPARE(media.background, QString("blur"));
        QVERIFY(!media.span);
        Thumbnails provider(&deck);
        const auto rendered = provider.requestImage(deck.renderId(0), nullptr, QSize(320, 180));
        QCOMPARE(rendered.pixelColor(0, 0), QColor(Qt::red));
        QCOMPARE(rendered.pixelColor(0, 179), QColor(Qt::blue));
        const auto transition = rendered.pixelColor(0, 89);
        QVERIFY(transition.red() > 80 && transition.blue() > 80);
        // Only the stretched background is blurred; the foreground remains sharp.
        QCOMPARE(rendered.pixelColor(160, 80), QColor(Qt::red));
        QCOMPARE(rendered.pixelColor(160, 100), QColor(Qt::blue));
        const auto background = provider.requestImage(deck.renderId(0) + "/background", nullptr,
                                                       QSize(320, 180));
        QVERIFY(background.pixelColor(160, 80).blue() > 20);
        QImage large(3840, 2160, QImage::Format_ARGB32_Premultiplied);
        {
            QPainter painter(&large);
            paintSlide(&painter, large.rect(), deck.slideSource(), deck.baseDir(), deck.palette());
        }
        QCOMPARE(large.pixelColor(1920, 960), QColor(Qt::red));
        QVERIFY(large.pixelColor(0, 1074).blue() > 80);
        deck.matchImageBackground(false);
        QCOMPARE(parseMedia(deck.slideSource(), deck.baseDir()).background, QString("theme"));
        deck.undo();
        QCOMPARE(parseMedia(deck.slideSource(), deck.baseDir()).background, QString("blur"));
        deck.undo();
        QCOMPARE(deck.source(), original);
        // Transparent artwork should reveal the theme, without black blur fringes.
        portrait.fill(Qt::transparent);
        QVERIFY(portrait.save(tmp.path() + "/images/transparent.png"));
        deck.editSlide("![fit background=blur](transparent.png)");
        QCOMPARE(provider.requestImage(deck.renderId(0), nullptr, QSize(320, 180)).pixelColor(0, 0),
                 deck.background());
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
    void syntaxColors() {
        Deck deck;
        QTextDocument doc;
        doc.setMarkdown("# Ruby\n\n```ruby\nclass Post\n  # café\n  puts "
                        "\"héllo\"\nend\n```\n\nPlain text\n\n```javascript\nconst n = 42;\n```\n");
        QString original = doc.toPlainText();
        highlightCode(doc, deck.palette());
        QCOMPARE(doc.toPlainText(), original);
        auto colorAt = [&](QString token) {
            QTextCursor cursor(&doc);
            cursor.setPosition(original.indexOf(token));
            cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
            return cursor.charFormat().foreground().color();
        };
        QCOMPARE(colorAt("class"), QColor(deck.palette()["magenta"].toString()));
        QCOMPARE(colorAt("héllo"), QColor(deck.palette()["green"].toString()));
        QCOMPARE(colorAt("const"), QColor(deck.palette()["magenta"].toString()));
        QCOMPARE(colorAt("42"), QColor(deck.palette()["red"].toString()));
        auto palette = deck.palette();
        palette["green"] = "#123456";
        highlightCode(doc, palette);
        QCOMPARE(colorAt("héllo"), QColor("#123456"));
        QTextDocument plain;
        plain.setMarkdown("```unknownlanguage\nclass Post\n```\n\n```\nclass Plain\n```");
        original = plain.toPlainText();
        highlightCode(plain, palette);
        QCOMPARE(plain.toPlainText(), original);
    }
    void plainLineBreaks() {
        Deck deck;
        auto render = [&](const QString &source) {
            QImage image(960, 540, QImage::Format_ARGB32_Premultiplied);
            QPainter painter(&image);
            paintSlide(&painter, image.rect(), source, "/tmp", deck.palette());
            return image;
        };
        QString cities = "San Clarita\nChicago\nVirginia\nCopenhagen\nAmsterdam\nSingapore\nSydney";
        QString explicitBreaks = cities;
        explicitBreaks.replace("\n", "\\\n");
        QCOMPARE(render(cities), render(explicitBreaks));
        QString crlf = cities;
        crlf.replace("\n", "\r\n");
        QCOMPARE(render(crlf), render(explicitBreaks));
        QString cr = cities;
        cr.replace("\n", "\r");
        QCOMPARE(render(cr), render(explicitBreaks));
        QCOMPARE(render("`one`\n`two`"), render("`one`\\\n`two`"));
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
