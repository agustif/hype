#include "apptheme.h"
#include "deck.h"
#include "renderer.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <cstdio>
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setApplicationName("hype");
    app.setApplicationVersion("0.2.0");
    app.setDesktopFileName(qEnvironmentVariable("HYPE_DESKTOP_FILE", "hype"));
    QCommandLineParser args;
    args.addHelpOption();
    args.addVersionOption();
    args.addPositionalArgument("presentation", "Markdown presentation");
    args.addOption({"pdf", "Export PDF and exit", "file"});
    args.addOption({"pptx", "Export rendered PowerPoint and exit", "file"});
    args.addOption({"render", "Render slide PNGs and manifest and exit", "directory"});
    args.addOption({"theme", "Apply installed theme", "name"});
    args.addOption({"save", "Save changes (for theme snapshots)"});
    args.addOption({"slide", "Select a slide (1-based)", "number"});
    args.addOption({"markdown", "Start in full-document Markdown mode"});
    args.addOption({"screenshot", "Save editor screenshot and exit", "file"});
    QCommandLineOption snapshotOption("export-snapshot", "Internal export snapshot", "file");
    snapshotOption.setFlags(QCommandLineOption::HiddenFromHelp);
    args.addOption(snapshotOption);
    args.process(app);
    Deck deck;
    const bool exportWorker = args.isSet(snapshotOption);
    auto report = [](const QJsonObject &event) {
        const auto line = QJsonDocument(event).toJson(QJsonDocument::Compact);
        fprintf(stdout, "%s\n", line.constData());
        fflush(stdout);
    };
    if (exportWorker) {
        if (!args.isSet("pdf") && !args.isSet("pptx")) return 1;
        if (!deck.loadExportSnapshot(args.value(snapshotOption))) {
            report({{"error", deck.status()}});
            return 1;
        }
        QObject::connect(&deck, &Deck::exportAdvanced, &app, [report](double progress, const QString &message) {
            report({{"progress", progress}, {"message", message}});
        });
    }
    auto positional = args.positionalArguments();
    if (!positional.isEmpty() && !deck.loadPath(positional[0])) {
        fprintf(stderr, "%s\n", qPrintable(deck.status()));
        return 1;
    }
    if (positional.isEmpty() && !exportWorker)
        deck.reopenLastPresentation();
    if (args.isSet("theme"))
        deck.chooseTheme(args.value("theme"));
    if (args.isSet("save"))
        deck.save();
    if (args.isSet("slide"))
        deck.select(args.value("slide").toInt() - 1);
    bool success = true, headless = false;
    for (const QString &option : {QString("pdf"), QString("pptx"), QString("render")})
        if (args.isSet(option)) {
            headless = true;
            success = success && (option == "pdf"    ? deck.exportPdf(args.value(option))
                                  : option == "pptx" ? deck.exportPptx(args.value(option))
                                                     : deck.renderImages(args.value(option)));
        }
    if (headless) {
        if (exportWorker) {
            if (success) report({{"progress", 1.0}, {"message", deck.status()}});
            else report({{"error", deck.status()}});
            return success ? 0 : 1;
        }
        fprintf(success ? stdout : stderr, "%s\n", qPrintable(deck.status()));
        return success ? 0 : 1;
    }
    QQuickStyle::setStyle("Basic");
    qmlRegisterType<SlideItem>("Hype", 1, 0, "SlideCanvas");
    qmlRegisterType<AppTheme>("Hype", 1, 0, "AppTheme");
    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlEngine::warnings, [](const QList<QQmlError> &errors) {
        for (const auto &error : errors)
            fprintf(stderr, "%s\n", qPrintable(error.toString()));
    });
    engine.rootContext()->setContextProperty("deck", &deck);
    engine.addImageProvider("slides", new Thumbnails(&deck));
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;
    if (args.isSet("markdown"))
        QMetaObject::invokeMethod(engine.rootObjects().first(), "openMarkdown");
    if (args.isSet("screenshot")) {
        QTimer::singleShot(1800, &app, [&] {
            auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
            bool ok = window && window->grabWindow().save(args.value("screenshot"));
            app.exit(ok ? 0 : 1);
        });
    }
    return app.exec();
}
