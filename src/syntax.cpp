#include "syntax.h"
#include <QCache>
#include <QProcess>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>

static QString highlightedHtml(const QString &source, QString language) {
    language = language.toLower().section(' ', 0, 0);
    static const QMap<QString, QString> aliases{
        {"shell", "sh"}, {"shellscript", "sh"}, {"c++", "cpp"}, {"yml", "yaml"},
        {"rust", "rs"}};
    language = aliases.value(language, language);
    if (!QRegularExpression("^[a-z0-9+#_-]+$").match(language).hasMatch())
        return {};
    static thread_local QCache<QString, QString> cache(4 * 1024 * 1024);
    const QString key = language + '\n' + source;
    if (auto html = cache.object(key))
        return *html;
    QProcess process;
    process.start("source-highlight", {"--src-lang=" + language, "--out-format=html-css"});
    QString html;
    if (process.waitForStarted(1000)) {
        process.write(source.toUtf8());
        process.closeWriteChannel();
        if (process.waitForFinished(3000) && process.exitStatus() == QProcess::NormalExit &&
            process.exitCode() == 0)
            html = QString::fromUtf8(process.readAllStandardOutput());
        else {
            process.kill();
            process.waitForFinished();
        }
    }
    cache.insert(key, new QString(html), qMax(1, int((key.size() + html.size()) * 2)));
    return html;
}

void highlightCode(QTextDocument &document, const QVariantMap &palette) {
    QString css;
    const QMap<QString, QString> colors{
        {"keyword", "magenta"},  {"type", "yellow"},     {"classname", "yellow"},
        {"string", "green"},     {"regexp", "green"},    {"specialchar", "cyan"},
        {"number", "red"},       {"function", "accent"}, {"preproc", "cyan"},
        {"symbol", "cyan"},      {"variable", "red"},    {"comment", "dark_foreground"},
        {"normal", "foreground"}};
    for (auto it = colors.cbegin(); it != colors.cend(); ++it)
        css +=
            QString("span.%1 { color: %2; }")
                .arg(it.key(), palette.value(it.value(), palette.value("foreground")).toString());
    for (QTextBlock block = document.begin(); block.isValid();) {
        const QString language = block.blockFormat().stringProperty(QTextFormat::BlockCodeLanguage);
        if (language.isEmpty()) {
            block = block.next();
            continue;
        }
        const int start = block.position();
        QString source = block.text();
        block = block.next();
        while (block.isValid() &&
               block.blockFormat().stringProperty(QTextFormat::BlockCodeLanguage) == language) {
            source += '\n' + block.text();
            block = block.next();
        }
        const QString html = highlightedHtml(source, language);
        if (html.isEmpty())
            continue;
        QTextDocument highlighted;
        highlighted.setDefaultStyleSheet(css);
        highlighted.setHtml(html.trimmed());
        // Refuse to apply offsets if the formatter changed whitespace or Unicode.
        if (highlighted.toPlainText() != source && highlighted.toPlainText() != source + '\n')
            continue;
        for (auto b = highlighted.begin(); b.isValid(); b = b.next()) {
            for (auto it = b.begin(); !it.atEnd(); ++it) {
                const auto fragment = it.fragment();
                if (!fragment.isValid() || fragment.position() >= source.size())
                    continue;
                QTextCursor cursor(&document);
                cursor.setPosition(start + fragment.position());
                cursor.setPosition(
                    start + qMin(int(source.size()), fragment.position() + fragment.length()),
                    QTextCursor::KeepAnchor);
                QTextCharFormat format;
                format.setForeground(fragment.charFormat().foreground());
                cursor.mergeCharFormat(format);
            }
        }
    }
}
