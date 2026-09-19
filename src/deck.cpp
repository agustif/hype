#include "deck.h"
#include "animationexport.h"
#include "pptx.h"
#include "renderer.h"
#include "images.h"
#include <QApplication>
#include <QClipboard>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include "filedialog.h"
#include <QFileInfo>
#include <QFontDatabase>
#include <QFutureWatcher>
#include <QtConcurrentRun>
#include <QImageReader>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPdfWriter>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <memory>
#include <csignal>
#include <cstdio>
#include <cerrno>
#include <cstring>

ParsedDeck parseDeck(const QString &source) {
    ParsedDeck result;
    int contentStart = source.startsWith(QChar(0xfeff)) ? 1 : 0;
    static const QRegularExpression lineEnd("\\r?\\n");
    auto lineAt = [&](int offset, int *next) {
        int end = source.indexOf('\n', offset);
        *next = end < 0 ? source.size() : end + 1;
        QString line = source.mid(offset, (end < 0 ? source.size() : end) - offset);
        if (line.endsWith('\r'))
            line.chop(1);
        return line;
    };
    int next = 0;
    if (lineAt(contentStart, &next) == "---") {
        int offset = next;
        bool closed = false;
        while (offset < source.size()) {
            QString line = lineAt(offset, &next);
            if (line == "---") {
                contentStart = next;
                closed = true;
                break;
            }
            offset = next;
        }
        if (!closed) {
            result.error = "Front matter needs a closing ---";
            contentStart = 0;
        }
    }
    result.header = source.left(contentStart);
    int start = contentStart, pos = start, fenceLength = 0;
    QChar fence;
    static const QRegularExpression fenceRe("^ {0,3}(`{3,}|~{3,})(.*)$");
    while (pos < source.size()) {
        QString line = lineAt(pos, &next);
        auto match = fenceRe.match(line);
        if (match.hasMatch()) {
            QString run = match.captured(1);
            if (fenceLength == 0) {
                fence = run[0];
                fenceLength = run.size();
            } else if (run[0] == fence && run.size() >= fenceLength &&
                       match.captured(2).trimmed().isEmpty())
                fenceLength = 0;
        } else if (fenceLength == 0 && line == "---") {
            result.slides.append({source.mid(start, pos - start), start, pos});
            start = next;
        }
        pos = next;
    }
    result.slides.append({source.mid(start), start, int(source.size())});
    if (fenceLength)
        result.error = "Unclosed code fence";
    return result;
}
QString scalar(const QString &header, const QString &key, const QString &fallback) {
    QRegularExpression re("^" + QRegularExpression::escape(key) + ":\\s*([^\\r\\n]*)$",
                          QRegularExpression::MultilineOption);
    auto m = re.match(header);
    if (!m.hasMatch())
        return fallback;
    QString value = m.captured(1).trimmed();
    if (value.startsWith('"') && value.endsWith('"')) {
        auto doc = QJsonDocument::fromJson(("[" + value + "]").toUtf8());
        if (doc.isArray() && doc.array().size())
            return doc.array()[0].toString();
    }
    if (value.startsWith('\'') && value.endsWith('\''))
        return value.mid(1, value.size() - 2).replace("''", "'");
    return value;
}
QString setScalar(QString header, const QString &key, const QString &value) {
    QByteArray json = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    QString line = key + ": " + QString::fromUtf8(json.mid(1, json.size() - 2));
    QRegularExpression re("^" + QRegularExpression::escape(key) + ":[^\\r\\n]*",
                          QRegularExpression::MultilineOption);
    auto match = re.match(header);
    if (match.hasMatch())
        header.replace(match.capturedStart(), match.capturedLength(), line);
    else if (header.isEmpty())
        header = "---\n" + line + "\n---\n";
    else {
        int end = header.lastIndexOf("---");
        header.insert(end, line + "\n");
    }
    return header;
}
Deck::Deck(QObject *parent, const QString &exportProgram) : QAbstractListModel(parent),
    m_exportProgram(exportProgram.isEmpty() ? QCoreApplication::applicationFilePath() : exportProgram) {
    discoverThemes();
    m_source = "---\ntitle: Untitled\ntheme: tokyo-night\n---\n\n# Your next idea\n";
    m_parsed = parseDeck(m_source);
    m_saved = m_source;
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] {
        QFile file(m_path);
        if (!file.open(QIODevice::ReadOnly)) {
            m_externalChange = true;
            setStatus("Presentation changed or was removed outside Hype.");
            return;
        }
        const QString disk = QString::fromUtf8(file.readAll());
        if (disk != m_saved) {
            if (!dirty()) {
                loadPath(m_path);
                setStatus("Reloaded external changes.");
            } else {
                m_externalChange = true;
                setStatus("Changed on disk. Save a copy or reopen to reload.");
            }
        }
        watch();
    });
}
int Deck::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : count(); }
QVariant Deck::data(const QModelIndex &index, int role) const {
    if (index.row() < 0 || index.row() >= count())
        return {};
    if (role == NumberRole)
        return index.row() + 1;
    if (role == TitleRole) {
        QString text = slide(index.row());
        text.remove(QRegularExpression("<!--[\\s\\S]*?-->"));
        for (QString line : text.split('\n')) {
            line = line.trimmed();
            if (line.isEmpty())
                continue;
            line.remove(QRegularExpression("^#+\\s*"));
            if (line.startsWith("!["))
                return QString("Image / video");
            return line.left(70);
        }
        return "Empty slide";
    }
    return {};
}
QHash<int, QByteArray> Deck::roleNames() const {
    return {{TitleRole, "slideTitle"}, {NumberRole, "number"}};
}
QString Deck::slideSource() const { return slide(m_selected); }
static QString withoutSlidePadding(QString text) {
    // Strip boundary lines, not indentation or Markdown hard-break spaces.
    text.remove(QRegularExpression("^(?:[ \\t]*\\r?\\n)+"));
    text.remove(QRegularExpression("(?:\\r?\\n[ \\t]*)+$"));
    return text.trimmed().isEmpty() ? QString() : text;
}
QString Deck::slideText() const { return withoutSlidePadding(slideSource()); }
QString Deck::slide(int i) const {
    return i >= 0 && i < count() ? m_parsed.slides[i].source : QString();
}
QString Deck::baseDir() const {
    return m_path.isEmpty() ? QDir::currentPath() : QFileInfo(m_path).absolutePath();
}
QString Deck::title() const {
    return scalar(m_parsed.header, "title",
                  m_path.isEmpty() ? "Untitled" : QFileInfo(m_path).completeBaseName());
}
void Deck::setStatus(const QString &s) {
    m_status = s;
    if (!m_exporting && !m_exportStatus.isEmpty()) {
        m_exportStatus.clear();
        m_exportFailed = false;
        emit exportChanged();
    }
    emit statusChanged();
}
void Deck::apply(const QString &source, int selected, bool history, int anchor,
                 const ParsedDeck *structure) {
    if (source == m_source) {
        m_anchor = qBound(0, anchor < 0 ? selected : anchor, count() - 1);
        m_selected = qBound(0, selected, count() - 1);
        emit changed();
        return;
    }
    if (history) {
        m_undo.append({m_source, m_selected, m_anchor, m_parsed});
        if (m_undo.size() > 200)
            m_undo.removeFirst();
        m_redo.clear();
    }
    auto parsed = structure ? *structure : parseDeck(source);
    if (structure) {
        parsed.error.clear();
        for (const auto &slide : parsed.slides) {
            // A slide body cannot contain front matter. Prefix a plain line so
            // a leading --- is interpreted as a slide break instead.
            const auto body = parseDeck("Slide\n" + slide.source);
            if (!body.error.isEmpty()) {
                parsed.error = body.error;
                break;
            }
        }
    }
    const bool reset = parsed.slides.size() != count();
    QVector<int> modified;
    if (!reset)
        for (int i = 0; i < count(); ++i)
            if (parsed.slides[i].source != m_parsed.slides[i].source)
                modified.append(i);
    if (reset)
        beginResetModel();
    m_source = source;
    m_parsed = parsed;
    m_selected = qBound(0, selected, count() - 1);
    m_anchor = qBound(0, anchor < 0 ? selected : anchor, count() - 1);
    ++m_revision;
    if (reset)
        endResetModel();
    else
        for (int i : modified)
            emit dataChanged(index(i), index(i));
    emit changed();
    if (!m_parsed.error.isEmpty())
        setStatus(m_parsed.error);
}
void Deck::select(int index) {
    index = qBound(0, index, count() - 1);
    if (m_selected == index && m_anchor == index)
        return;
    m_selected = m_anchor = index;
    emit changed();
}
void Deck::extendSelection(int index) {
    index = qBound(0, index, count() - 1);
    if (m_selected == index)
        return;
    m_selected = index;
    emit changed();
}
void Deck::moveSelection(int direction) {
    if (direction < 0 && selectionFirst() > 0)
        dropSelection(selectionFirst() - 1);
    else if (direction > 0 && selectionLast() < count() - 1)
        dropSelection(selectionLast() + 2);
}
void Deck::dropSelection(int slot) {
    const int first = selectionFirst(), last = selectionLast(), length = selectionCount();
    if (slot < 0 || slot > count() || (slot >= first && slot <= last + 1))
        return;
    QStringList slides;
    for (const auto &slide : m_parsed.slides)
        slides << slide.source;
    const QStringList moving = slides.mid(first, length);
    for (int i = 0; i < length; ++i)
        slides.removeAt(first);
    const int destination = slot > last ? slot - length : slot;
    for (int i = 0; i < length; ++i)
        slides.insert(destination + i, moving[i]);
    const int offset = destination - first;
    replaceSlides(slides, m_selected + offset, m_anchor + offset);
}
void Deck::selectAt(int position) {
    for (int i = count() - 1; i >= 0; --i)
        if (position >= m_parsed.slides[i].start) {
            select(i);
            return;
        }
}
int Deck::sourcePosition() const { return m_parsed.slides.value(m_selected).start; }
void Deck::editSource(const QString &s) { apply(s, m_selected); }
void Deck::editSlide(const QString &s) {
    const auto range = m_parsed.slides.value(m_selected);
    QString body = withoutSlidePadding(s);
    if (!body.isEmpty()) {
        if (range.start > 0)
            body.prepend('\n');
        body += m_selected < count() - 1 ? "\n\n" : "\n";
    } else {
        body = "\n";
    }
    QString edited = m_source;
    edited.replace(range.start, range.end - range.start, body);
    // Reparse only the edited slide. An unfinished fence must never extend this
    // editor's replacement range into the following slides on the next keystroke.
    const QString prefix = "Slide\n";
    auto fragment = parseDeck(prefix + body);
    auto parsed = m_parsed;
    const int shift = body.size() - (range.end - range.start);
    for (int i = m_selected + 1; i < parsed.slides.size(); ++i) {
        parsed.slides[i].start += shift;
        parsed.slides[i].end += shift;
    }
    parsed.slides.removeAt(m_selected);
    for (int i = 0; i < fragment.slides.size(); ++i) {
        auto slide = fragment.slides[i];
        const int start = qMax(0, slide.start - int(prefix.size()));
        const int end = slide.end - prefix.size();
        parsed.slides.insert(m_selected + i,
                             {body.mid(start, end - start), range.start + start, range.start + end});
    }
    apply(edited, m_selected, true, -1, &parsed);
}
void Deck::replaceSlides(const QStringList &slides, int selected, int anchor) {
    QString out = m_parsed.header;
    ParsedDeck parsed;
    parsed.header = m_parsed.header;
    for (int i = 0; i < slides.size(); ++i) {
        if (i)
            out += "---\n";
        const int start = out.size();
        const QString body = withoutSlidePadding(slides[i]);
        if (!body.isEmpty()) {
            if (!out.isEmpty())
                out += '\n';
            out += body + '\n';
        }
        if (i < slides.size() - 1 || body.isEmpty())
            out += '\n';
        parsed.slides.append({out.mid(start), start, int(out.size())});
    }
    apply(out, selected, true, anchor, &parsed);
}
void Deck::replaceHeader(const QString &header) {
    auto parsed = m_parsed;
    const int shift = header.size() - parsed.header.size();
    parsed.header = header;
    for (auto &slide : parsed.slides) {
        slide.start += shift;
        slide.end += shift;
    }
    apply(header + m_source.mid(m_parsed.header.size()), m_selected, true, -1, &parsed);
}
void Deck::moveSlide(int from, int to) {
    if (from < 0 || from >= count() || to < 0 || to >= count() || from == to)
        return;
    QStringList list;
    for (auto &s : m_parsed.slides)
        list << s.source;
    list.move(from, to);
    replaceSlides(list, to);
}
void Deck::addSlide() {
    QStringList list;
    for (auto &s : m_parsed.slides)
        list << s.source;
    const int next = selectionLast() + 1;
    list.insert(next, "\n\n");
    replaceSlides(list, next);
}
void Deck::duplicateSlide() {
    QStringList list;
    for (auto &s : m_parsed.slides)
        list << s.source;
    const int length = selectionCount(), next = selectionLast() + 1;
    const QStringList copies = list.mid(selectionFirst(), length);
    for (int i = 0; i < length; ++i)
        list.insert(next + i, copies[i]);
    replaceSlides(list, m_selected + length, m_anchor + length);
}
void Deck::deleteSlide() {
    QStringList list;
    for (auto &s : m_parsed.slides)
        list << s.source;
    const int first = selectionFirst();
    for (int i = 0; i < selectionCount(); ++i)
        list.removeAt(first);
    if (list.isEmpty())
        list << "\n";
    replaceSlides(list, qMin(first, int(list.size()) - 1));
}
void Deck::undo() {
    if (m_undo.isEmpty())
        return;
    auto state = m_undo.takeLast();
    m_redo.append({m_source, m_selected, m_anchor, m_parsed});
    apply(state.source, state.selected, false, state.anchor, &state.parsed);
}
void Deck::redo() {
    if (m_redo.isEmpty())
        return;
    auto state = m_redo.takeLast();
    m_undo.append({m_source, m_selected, m_anchor, m_parsed});
    apply(state.source, state.selected, false, state.anchor, &state.parsed);
}
void Deck::discoverThemes() {
    QString root = qEnvironmentVariable("OMARCHY_PATH", QDir::homePath() + "/.local/share/omarchy");
    QStringList roots{root + "/themes", QDir::homePath() + "/omarchy/themes",
                      QDir::homePath() + "/.config/omarchy/themes"};
    for (auto &r : roots)
        for (auto &name : QDir(r).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            QString path = r + "/" + name + "/colors.toml";
            if (QFile::exists(path))
                m_themes[name] = path;
        }
}
qint64 Deck::totalBytes() const {
    const QString base = baseDir();
    if (m_totalBytes >= 0 && m_sizeSource == m_source && m_sizeBase == base)
        return m_totalBytes;
    qint64 bytes = m_source.toUtf8().size();
    QSet<QString> files;
    for (const auto &slide : m_parsed.slides) {
        const auto media = parseMedia(slide.source, base);
        for (const auto &path : {media.path, media.poster}) {
            if (path.isEmpty())
                continue;
            const QFileInfo file(path);
            if (!file.isFile())
                continue;
            const QString identity = file.canonicalFilePath();
            if (!files.contains(identity)) {
                files.insert(identity);
                bytes += file.size();
            }
        }
    }
    m_sizeSource = m_source;
    m_sizeBase = base;
    m_totalBytes = bytes;
    return bytes;
}
QString Deck::sizeLabel() const {
    return QLocale().formattedDataSize(totalBytes(), 0, QLocale::DataSizeSIFormat);
}
QStringList Deck::fontNames() const { return QFontDatabase::families(); }
QString Deck::fontName() const { return scalar(m_parsed.header, "font", "JetBrains Mono"); }
void Deck::chooseFont(const QString &family) {
    if (!fontNames().contains(family) || family == fontName())
        return;
    const QString header = setScalar(m_parsed.header, "font", family);
    replaceHeader(header);
}
QStringList Deck::themeNames() const { return m_themes.keys(); }
QString Deck::themeName() const { return scalar(m_parsed.header, "theme", "tokyo-night"); }
QVariantMap Deck::palette() const {
    if (!m_paletteCache.isEmpty() && m_paletteHeader == m_parsed.header)
        return m_paletteCache;
    QVariantMap colors{{"background", "#1a1b26"}, {"foreground", "#c0caf5"}, {"accent", "#7aa2f7"},
                       {"green", "#9ece6a"},      {"red", "#f7768e"},        {"yellow", "#e0af68"},
                       {"magenta", "#bb9af7"},    {"cyan", "#7dcfff"}};
    QFile f(m_themes.value(themeName()));
    if (f.open(QIODevice::ReadOnly)) {
        QRegularExpression re("^([a-z_]+)\\s*=\\s*\"(#[0-9a-fA-F]{6})\"",
                              QRegularExpression::MultilineOption);
        auto matches = re.globalMatch(QString::fromUtf8(f.readAll()));
        while (matches.hasNext()) {
            auto m = matches.next();
            colors[m.captured(1)] = m.captured(2);
        }
    }
    for (const auto &key : colors.keys()) {
        QString v = scalar(m_parsed.header, "color_" + key);
        if (QColor(v).isValid())
            colors[key] = v;
    }
    colors["font"] = scalar(m_parsed.header, "font", "JetBrains Mono");
    m_paletteHeader = m_parsed.header;
    m_paletteCache = colors;
    return colors;
}
QColor Deck::background() const { return QColor(palette()["background"].toString()); }
QColor Deck::foreground() const { return QColor(palette()["foreground"].toString()); }
QColor Deck::accent() const { return QColor(palette()["accent"].toString()); }
void Deck::chooseTheme(const QString &name) {
    QString header = setScalar(m_parsed.header, "theme", name);
    // Remove old palette before reading the newly selected installed theme.
    header.remove(
        QRegularExpression("^color_[a-z_]+:[^\\n]*\\n", QRegularExpression::MultilineOption));
    QFile file(m_themes.value(name));
    if (file.open(QIODevice::ReadOnly)) {
        QRegularExpression re("^([a-z_]+)\\s*=\\s*\"(#[0-9a-fA-F]{6})\"",
                              QRegularExpression::MultilineOption);
        auto matches = re.globalMatch(QString::fromUtf8(file.readAll()));
        while (matches.hasNext()) {
            auto m = matches.next();
            header = setScalar(header, "color_" + m.captured(1), m.captured(2));
        }
    }
    replaceHeader(header);
}
QVariantMap Deck::media() const {
    const QString source = slideSource(), base = baseDir();
    if (!m_mediaCache.isEmpty() && m_mediaSource == source && m_mediaBase == base)
        return m_mediaCache;
    m_mediaSource = source;
    m_mediaBase = base;
    auto m = parseMedia(source, base);
    const bool title = !m.text.trimmed().isEmpty();
    QImageReader reader(m.path);
    const bool animated =
        !m.path.isEmpty() && !m.video && reader.supportsAnimation() && reader.imageCount() > 1;
    m_mediaCache = {{"url", QUrl::fromLocalFile(m.path)},
                    {"video", m.video},
                    {"animated", animated},
                    {"side", m.side},
                    {"span", m.span},
                    {"loop", m.loop},
                    {"muted", m.muted},
                    {"autoplay", m.autoplay},
                    {"title", title},
                    {"overlay", m.overlay}};
    return m_mediaCache;
}
void Deck::watch() {
    if (!m_watcher.files().isEmpty())
        m_watcher.removePaths(m_watcher.files());
    if (QFile::exists(m_path))
        m_watcher.addPath(m_path);
}
QString Deck::dialogDirectory() const {
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "hype", "hype");
    const QString last = settings.value("files/lastDirectory").toString();
    if (!last.isEmpty() && QDir(last).exists())
        return last;
    if (!m_path.isEmpty() && QDir(baseDir()).exists())
        return baseDir();
    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return !documents.isEmpty() && QDir(documents).exists() ? documents : QDir::homePath();
}
static void rememberPresentation(const QString &path) {
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "hype", "hype");
    settings.setValue("files/lastDirectory", QFileInfo(path).absolutePath());
    settings.setValue("files/lastPresentation", path);
}
bool Deck::reopenLastPresentation() {
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "hype", "hype");
    const QString path = settings.value("files/lastPresentation").toString();
    return !path.isEmpty() && QFileInfo(path).isFile() && loadPath(path);
}
bool Deck::loadPath(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        setStatus(f.errorString());
        return false;
    }
    QString s = QString::fromUtf8(f.readAll());
    m_path = QFileInfo(path).absoluteFilePath();
    apply(s, 0, false);
    m_saved = s;
    m_undo.clear();
    m_redo.clear();
    m_externalChange = false;
    watch();
    emit changed();
    setStatus("Opened " + title());
    rememberPresentation(m_path);
    return true;
}
bool Deck::savePath(const QString &path) {
    const auto parsed = parseDeck(m_source);
    bool sameSlides = parsed.slides.size() == count();
    for (int i = 0; sameSlides && i < count(); ++i)
        sameSlides = parsed.slides[i].source == m_parsed.slides[i].source;
    if (!parsed.error.isEmpty() || !m_parsed.error.isEmpty() || !sameSlides) {
        const QString problem = !m_parsed.error.isEmpty() ? m_parsed.error :
                                !parsed.error.isEmpty() ? parsed.error : "Unfinished slide boundaries";
        setStatus("Cannot save: " + problem + ". Finish the Markdown first; your changes are still in the editor.");
        return false;
    }
    if (QFileInfo(path).absoluteFilePath() == m_path && m_externalChange) {
        setStatus("File changed on disk. Use Save As to keep both versions.");
        return false;
    }
    QByteArray previous;
    if (QFile::exists(path)) {
        QFile disk(path);
        if (!disk.open(QIODevice::ReadOnly)) {
            setStatus("Cannot read the existing presentation: " + disk.errorString());
            return false;
        }
        previous = disk.readAll();
        if (disk.error() != QFile::NoError) {
            setStatus("Cannot read the existing presentation: " + disk.errorString());
            return false;
        }
        if (QFileInfo(path).absoluteFilePath() == m_path && QString::fromUtf8(previous) != m_saved) {
            m_externalChange = true;
            setStatus("File changed on disk. Save a copy or reopen.");
            return false;
        }
    }
    const QByteArray next = m_source.toUtf8();
    const QFileInfo target(path);
    const QString backupPrefix = target.fileName() + ".";
    QDir backups(target.absolutePath() + "/.hype-backups");
    if (!previous.isEmpty() && previous != next) {
        const QString stamp = QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss-zzz");
        const QString hash = QString::fromLatin1(QCryptographicHash::hash(previous, QCryptographicHash::Sha256).toHex().left(16));
        QSaveFile backup(backups.filePath(backupPrefix + stamp + "-" + hash + ".bak"));
        if (!QDir().mkpath(backups.absolutePath()) || !backup.open(QIODevice::WriteOnly) ||
            backup.write(previous) != previous.size() || !backup.commit()) {
            setStatus("Could not create a recovery backup. The saved presentation was left untouched.");
            return false;
        }
    }
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(next) != next.size() || !f.commit()) {
        setStatus(f.errorString());
        return false;
    }
    // Keep the last twenty saved versions of this file, independent of Dropbox.
    QStringList versions;
    const QRegularExpression backupName("^" + QRegularExpression::escape(backupPrefix) +
                                       "[0-9]{8}-[0-9]{6}-[0-9]{3}-[a-f0-9]{16}\\.bak$");
    for (const auto &name : backups.entryList(QDir::Files, QDir::Name))
        if (backupName.match(name).hasMatch()) versions.append(name);
    while (versions.size() > 20) backups.remove(versions.takeFirst());
    m_path = QFileInfo(path).absoluteFilePath();
    m_saved = m_source;
    m_externalChange = false;
    watch();
    emit changed();
    setStatus("Saved");
    rememberPresentation(m_path);
    return true;
}
bool Deck::confirmDiscard() {
    return !dirty() || QMessageBox::question(nullptr, "Unsaved changes", "Discard unsaved changes?",
                                             QMessageBox::Discard | QMessageBox::Cancel) ==
                           QMessageBox::Discard;
}
void Deck::openDialog() {
    if (!confirmDiscard())
        return;
    QString error;
    const QString p = FileDialog::choose(false, dialogDirectory(), "Markdown", {"*.md"}, &error);
    if (!error.isEmpty()) setStatus(error);
    if (!p.isEmpty())
        loadPath(p);
}
void Deck::save() {
    if (m_path.isEmpty())
        saveAs();
    else
        savePath(m_path);
}
void Deck::saveAs() {
    QString error;
    const QString p = FileDialog::choose(true,
        QDir(dialogDirectory())
            .filePath(m_path.isEmpty() ? "presentation.md" : QFileInfo(m_path).fileName()),
        "Markdown", {"*.md"}, &error);
    if (!error.isEmpty()) setStatus(error);
    if (p.isEmpty())
        return;
    if (!m_path.isEmpty() && QFileInfo(p).absolutePath() != baseDir()) {
        // Save As to another directory must carry all media with it.
        for (const QString &kind : {QString("images"), QString("videos")}) {
            QDir src(baseDir() + "/" + kind);
            if (!src.exists())
                continue;
            QDir().mkpath(QFileInfo(p).absolutePath() + "/" + kind);
            for (const auto &name : src.entryList(QDir::Files)) {
                const QString dst = QFileInfo(p).absolutePath() + "/" + kind + "/" + name;
                if (QFile::exists(dst)) {
                    QFile a(src.filePath(name)), b(dst);
                    if (!a.open(QIODevice::ReadOnly) || !b.open(QIODevice::ReadOnly)) {
                        setStatus("Could not read media while copying");
                        return;
                    }
                    if (a.readAll() != b.readAll()) {
                        setStatus("Save As media collision: " + name);
                        return;
                    }
                } else if (!QFile::copy(src.filePath(name), dst)) {
                    setStatus("Could not copy " + name);
                    return;
                }
            }
        }
    }
    savePath(p);
}
void Deck::newDeck() {
    if (!confirmDiscard())
        return;
    m_path.clear();
    m_saved.clear();
    m_externalChange = false;
    apply("---\ntitle: Untitled\ntheme: tokyo-night\n---\n\n# Your next idea\n", 0);
    watch();
}
void Deck::importDialog() {
    QString error;
    const QString p = FileDialog::choose(false, baseDir(), "Media",
        {"*.png", "*.jpg", "*.jpeg", "*.webp", "*.gif", "*.svg",
         "*.mp4", "*.mov", "*.mkv", "*.webm", "*.m4v"}, &error);
    if (!error.isEmpty()) setStatus(error);
    if (!p.isEmpty())
        importMedia(QUrl::fromLocalFile(p));
}
void Deck::importMedia(const QUrl &url) {
    if (!url.isLocalFile()) {
        setStatus("Choose a local image or video.");
        return;
    }
    if (m_path.isEmpty()) {
        saveAs();
        if (m_path.isEmpty())
            return;
    }
    QFileInfo info(url.toLocalFile());
    if (!info.isFile())
        return;
    bool video = QStringList{"mp4", "mov", "mkv", "webm", "m4v"}.contains(info.suffix().toLower());
    QString dir = baseDir() + (video ? "/videos" : "/images");
    QDir().mkpath(dir);
    QString name = info.fileName(), dest = dir + "/" + name;
    int suffix = 2;
    QFile input(info.absoluteFilePath());
    if (!input.open(QIODevice::ReadOnly)) {
        setStatus(input.errorString());
        return;
    }
    const auto hash = QCryptographicHash::hash(input.readAll(), QCryptographicHash::Sha256);
    while (QFile::exists(dest)) {
        QFile old(dest);
        if (!old.open(QIODevice::ReadOnly)) {
            setStatus("Cannot read existing media: " + name);
            return;
        }
        if (QCryptographicHash::hash(old.readAll(), QCryptographicHash::Sha256) == hash)
            break;
        name = info.completeBaseName() + "-" + QString::number(suffix++) + "." + info.suffix();
        dest = dir + "/" + name;
    }
    if (!QFile::exists(dest) && !QFile::copy(info.absoluteFilePath(), dest)) {
        setStatus("Could not import " + name);
        return;
    }
    if (video)
        ensurePoster(dest, baseDir());
    editSlide(slideSource() + "\n![](<" + name + ">)\n");
    setStatus("Added " + name);
}
bool Deck::pasteMedia() {
    if (m_compressingImage)
        return true;
    const QMimeData *clipboard = QApplication::clipboard()->mimeData();
    if (!clipboard)
        return false;
    QString source, extension, suggested = "image";
    bool video = false;
    QImage image;
    QByteArray mediaData;
    // Prefer copied files to thumbnail image data supplied by file managers.
    for (const QUrl &url : clipboard->urls()) {
        if (!url.isLocalFile())
            continue;
        QFileInfo file(url.toLocalFile());
        const QString suffix = file.suffix().toLower();
        bool isVideo = QStringList{"mp4", "mov", "mkv", "webm", "m4v"}.contains(suffix);
        if (!file.isFile() || (!isVideo && !QImageReader(file.absoluteFilePath()).canRead()))
            continue;
        source = file.absoluteFilePath();
        extension = suffix;
        suggested = file.completeBaseName();
        video = isVideo;
        break;
    }
    if (source.isEmpty()) {
        const QMap<QString, QString> formats{{"video/mp4", "mp4"},
                                             {"video/webm", "webm"},
                                             {"video/quicktime", "mov"},
                                             {"video/x-matroska", "mkv"}};
        for (auto it = formats.cbegin(); it != formats.cend(); ++it) {
            if (!clipboard->hasFormat(it.key()))
                continue;
            mediaData = clipboard->data(it.key());
            if (mediaData.isEmpty())
                continue;
            video = true;
            extension = it.value();
            suggested = "video";
            break;
        }
        if (!video) {
            image = QApplication::clipboard()->image();
            if (image.isNull())
                return false;
            extension = "png";
        }
    }
    if (m_path.isEmpty()) {
        saveAs();
        if (m_path.isEmpty())
            return true;
    }
    m_paste = {source, extension, m_path, m_source, mediaData, video, m_selected};
    QImageReader reader(source);
    const bool compress = !video && (source.isEmpty() ||
        (!(reader.supportsAnimation() && reader.imageCount() != 1) &&
         reader.format() != "svg" && reader.format() != "svgz"));
    if (!compress) {
        emit pasteRequested(suggested, extension, video);
        return true;
    }
    const auto media = parseMedia(withMedia(slideSource(), "![](<paste.png>)"), baseDir());
    const auto generation = ++m_pasteGeneration;
    m_compressingImage = true;
    emit compressingImageChanged();
    using Result = std::pair<PendingPaste, QString>;
    auto *watcher = new QFutureWatcher<Result>(this);
    connect(watcher, &QFutureWatcher<Result>::finished, this, [this, watcher, generation, suggested] {
        auto result = watcher->result();
        watcher->deleteLater();
        if (generation != m_pasteGeneration)
            return; // Cancelled work must not reopen the naming dialog.
        m_compressingImage = false;
        emit compressingImageChanged();
        if (!result.second.isEmpty()) {
            m_paste = {};
            setStatus(result.second);
            return;
        }
        if (m_path != result.first.path || m_source != result.first.document ||
            m_selected != result.first.selected) {
            m_paste = {};
            setStatus("The slide changed. Paste the image again.");
            return;
        }
        m_paste = std::move(result.first);
        emit pasteRequested(suggested, m_paste.extension, false);
    });
    // Clipboard access stays on the UI thread; decoding, scaling and both lossless
    // encoders work on an independent snapshot without touching the document.
    watcher->setFuture(QtConcurrent::run([pending = m_paste, image, span = media.span]() mutable -> Result {
        const QSize canvas(3840, 2160);
        QSize original;
        if (!pending.source.isEmpty()) {
            QImageReader reader(pending.source);
            original = reader.size();
            if (reader.transformation() & QImageIOHandler::TransformationRotate90)
                original.transpose();
            image = readSizedImage(pending.source, canvas, span);
        } else {
            original = image.size();
            const QSize target = imageSizeForCanvas(original, canvas, span);
            if (target != original)
                image = image.scaled(target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        }
        if (image.isNull())
            return {{}, "Could not read the pasted image."};
        QString optimizedExtension;
        QByteArray encoded = compressedImage(image, &optimizedExtension);
        if (encoded.isEmpty())
            return {{}, "Could not compress the pasted image."};
        // Keep an already-small JPEG/WebP when it beats the lossless rewrite.
        if (pending.source.isEmpty() || image.size() != original ||
            encoded.size() < QFileInfo(pending.source).size()) {
            pending.source.clear();
            pending.extension = optimizedExtension;
            pending.data = std::move(encoded);
        }
        return {std::move(pending), {}};
    }));
    return true;
}
void Deck::cancelPaste() {
    ++m_pasteGeneration;
    m_paste = {};
    if (m_compressingImage) {
        m_compressingImage = false;
        emit compressingImageChanged();
    }
}
QString Deck::savePastedMedia(const QString &value) {
    if (m_compressingImage)
        return "The image is still being compressed.";
    if (m_paste.extension.isEmpty())
        return "Paste an image or video first.";
    if (m_path != m_paste.path || m_source != m_paste.document || m_selected != m_paste.selected)
        return "The slide changed. Cancel and paste again.";
    QString stem = value.trimmed();
    if (stem.endsWith("." + m_paste.extension, Qt::CaseInsensitive))
        stem.chop(m_paste.extension.size() + 1);
    if (stem.isEmpty() || stem == "." || stem == ".." ||
        stem.contains(QRegularExpression(R"([/\\<>\x00-\x1f])")))
        return "Use a filename without folders or special characters.";
    const QString name = stem + "." + m_paste.extension;
    const QString directory = QDir(baseDir()).filePath(m_paste.video ? "videos" : "images");
    const QString destination = QDir(directory).filePath(name);
    if (QFile::exists(destination))
        return name + " already exists. Choose another name.";
    if (!QDir().mkpath(directory))
        return "Could not create " + directory;
    bool saved = false;
    if (!m_paste.source.isEmpty())
        saved = QFile::copy(m_paste.source, destination);
    else {
        QFile output(destination);
        if (output.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            saved = output.write(m_paste.data) == m_paste.data.size();
            saved = output.flush() && saved;
            output.close();
            if (!saved)
                output.remove();
        }
    }
    if (!saved)
        return "Could not save " + name;
    cancelPaste();
    editSlide(withMedia(slideSource(), "![](<" + name + ">)"));
    setStatus("Added " + name);
    return {};
}
QString Deck::renderId(int index) const {
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream << slide(index) << baseDir() << palette();
    auto media = parseMedia(slide(index), baseDir());
    for (const QString &path : {media.path, media.poster}) {
        QFileInfo file(path);
        stream << file.lastModified().toMSecsSinceEpoch() << file.size();
    }
    return QString::fromLatin1(
        bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}
void Deck::matchImageBackground(bool enabled) {
    setMediaBackground(enabled ? "auto" : "theme");
}
void Deck::setMediaBackground(const QString &mode) {
    if (!QStringList{"auto", "theme", "blur"}.contains(mode))
        return;
    auto media = parseMedia(slideSource(), baseDir());
    if (media.file.isEmpty())
        return;
    const bool fittedBackground = mode == "blur" || (media.video && mode == "auto");
    const QString source = slideSource();
    const QString marker = "\x01HYPE_MEDIA\x01";
    const QString marked = withMedia(source, marker);
    const int start = marked.indexOf(marker);
    const int length = source.size() - marked.size() + marker.size();
    if (start < 0 || length <= 0)
        return;
    QString reference = source.mid(start, length);
    const int end = reference.indexOf("](");
    if (end < 2)
        return;
    QString flags = reference.mid(2, end - 2).trimmed();
    const QRegularExpression tokens(R"re(([a-z]+)(?:=("(?:[^"\\]|\\.)*"|[^\s]+))?)re");
    const auto first = tokens.match(flags);
    const bool directives = first.hasMatch() && first.capturedStart() == 0 &&
        (QStringList{"fit", "span", "left", "right", "loop", "muted"}.contains(first.captured(1)) ||
         !first.captured(2).isEmpty());
    QStringList kept;
    if (directives) {
        auto matches = tokens.globalMatch(flags);
        while (matches.hasNext()) {
            const auto token = matches.next();
            const QString key = token.captured(1);
            if (key != "background" && !(fittedBackground && (key == "span" || key == "fit")))
                kept << token.captured();
        }
    } else if (!flags.isEmpty()) {
        kept << "alt=\"" + flags.replace("\\", "\\\\").replace("\"", "\\\"") + "\"";
    }
    // A spanning foreground would hide the chosen background entirely.
    if (fittedBackground && media.side.isEmpty())
        kept.prepend("fit");
    kept << "background=" + mode;
    reference.replace(2, end - 2, kept.join(' '));
    editSlide(withMedia(source, reference));
}
void Deck::setMediaMode(const QString &mode) {
    QRegularExpression re("!\\[([^\\]]*)\\]\\(");
    auto m = re.match(slideSource());
    if (!m.hasMatch())
        return;
    QString flags = m.captured(1);
    flags.remove(QRegularExpression("\\b(span|fit|left|right)\\b\\s*"));
    if (!flags.trimmed().isEmpty() && !flags.contains('=') && !flags.contains("loop") &&
        !flags.contains("muted"))
        flags = "alt=\"" + flags.replace('"', "\\\"") + "\"";
    flags = (mode + " " + flags).trimmed();
    QString s = slideSource();
    s.replace(m.capturedStart(1), m.capturedLength(1), flags);
    editSlide(s);
}
void Deck::exportDialog(const QString &format) {
    if (m_exporting)
        return;
    QString error;
    const QString p = FileDialog::choose(true, baseDir() + "/" + title() + "." + format,
                                         format.toUpper(), {"*." + format}, &error);
    if (!error.isEmpty()) setStatus(error);
    if (p.isEmpty())
        return;
    startExport(format, p);
}
static void stopExport(QProcess *process) {
    if (process && process->processId() > 0) {
        // Include any FFmpeg child processes in cancellation.
        ::kill(-process->processId(), SIGKILL);
        process->kill();
    }
}
Deck::~Deck() {
    if (m_exportProcess) {
        disconnect(m_exportProcess, nullptr, this, nullptr);
        if (m_exportProcess->state() == QProcess::Starting)
            m_exportProcess->waitForStarted(1000);
        stopExport(m_exportProcess);
        m_exportProcess->waitForFinished(1000);
    }
}
void Deck::cancelExport() {
    if (!m_exporting) return;
    m_exportCancelled = true;
    stopExport(m_exportProcess);
}
bool Deck::loadExportSnapshot(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setStatus(file.errorString());
        return false;
    }
    const auto snapshot = QJsonDocument::fromJson(file.readAll()).object();
    if (!snapshot["source"].isString() || !snapshot["path"].isString()) {
        setStatus("Invalid export snapshot.");
        return false;
    }
    m_path = snapshot["path"].toString();
    m_source = snapshot["source"].toString();
    m_parsed = parseDeck(m_source);
    m_paletteHeader = m_parsed.header;
    m_paletteCache = snapshot["palette"].toObject().toVariantMap();
    return true;
}
void Deck::startExport(const QString &format, const QString &path) {
    if (m_exporting || path.isEmpty() || !QStringList{"pdf", "pptx"}.contains(format))
        return;
    auto temporary = std::make_shared<QTemporaryDir>();
    const QString destination = QFileInfo(path).absoluteFilePath();
    auto staged = std::make_shared<QTemporaryDir>(QFileInfo(destination).absolutePath() + "/.hype-export-XXXXXX");
    QFile snapshot(temporary->filePath("presentation.json"));
    const QByteArray data = QJsonDocument(QJsonObject{
        {"source", m_source},
        {"path", m_path.isEmpty() ? baseDir() + "/Untitled.md" : m_path},
        {"palette", QJsonObject::fromVariantMap(palette())}}).toJson();
    if (!temporary->isValid() || !staged->isValid() || !snapshot.open(QIODevice::WriteOnly) || snapshot.write(data) != data.size()) {
        m_exportFailed = true;
        m_exportStatus = "Could not prepare the export.";
        emit exportChanged();
        emit exportFinished(false);
        return;
    }
    snapshot.close();
    m_exporting = true;
    m_exportCancelled = false;
    m_exportFailed = false;
    m_exportProgress = 0;
    m_exportStatus = "Preparing " + format.toUpper() + " export…";
    emit exportChanged();
    auto *process = new QProcess(this);
    m_exportProcess = process;
    process->setUnixProcessParameters(QProcess::UnixProcessFlag::CreateNewSession);
    connect(process, &QProcess::started, this, [this, process] {
        if (m_exportCancelled) stopExport(process);
    });
    // A separate renderer keeps Qt painting, compression and video conversion
    // away from the editor. It reads an immutable snapshot, including unsaved edits.
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("QT_QPA_PLATFORM", "offscreen");
    environment.insert("QT_QPA_PLATFORMTHEME", "generic");
    environment.insert("TMPDIR", temporary->path());
    process->setProcessEnvironment(environment);
    auto pending = std::make_shared<QByteArray>();
    auto failure = std::make_shared<QString>();
    auto diagnostics = std::make_shared<QByteArray>();
    auto readProgress = [this, process, pending, failure] {
        pending->append(process->readAllStandardOutput());
        int end;
        while ((end = pending->indexOf('\n')) >= 0) {
            const auto event = QJsonDocument::fromJson(pending->left(end)).object();
            pending->remove(0, end + 1);
            if (event.contains("error")) *failure = event["error"].toString();
            if (!event.contains("progress")) continue;
            m_exportProgress = qBound(0.0, event["progress"].toDouble(), 1.0);
            m_exportStatus = event["message"].toString();
            emit exportChanged();
        }
    };
    connect(process, &QProcess::readyReadStandardOutput, this, readProgress);
    connect(process, &QProcess::readyReadStandardError, this, [process, diagnostics] {
        *diagnostics = (*diagnostics + process->readAllStandardError()).right(8192);
    });
    auto completed = std::make_shared<bool>(false);
    auto finish = [this, process, temporary, staged, completed, destination](bool success, QString message) {
        if (*completed) return;
        *completed = true;
        success = success && !m_exportCancelled;
        if (success && ::rename(QFile::encodeName(staged->filePath("output")).constData(),
                                QFile::encodeName(destination).constData()) != 0) {
            success = false;
            message = "Could not save the export: " + QString::fromLocal8Bit(std::strerror(errno));
        }
        m_exporting = false;
        m_exportProcess = nullptr;
        m_exportFailed = !success && !m_exportCancelled;
        if (success) m_exportProgress = 1;
        m_exportStatus = m_exportCancelled ? "Export cancelled" : success ? "Exported " + QFileInfo(destination).fileName() : "Export failed: " + message;
        process->deleteLater();
        emit exportChanged();
        emit exportFinished(success);
    };
    connect(process, &QProcess::finished, this,
        [process, readProgress, finish, failure, diagnostics](int code, QProcess::ExitStatus exitStatus) {
            readProgress();
            const bool success = exitStatus == QProcess::NormalExit && code == 0;
            QString error = *failure;
            if (error.isEmpty()) error = QString::fromUtf8(*diagnostics + process->readAllStandardError()).trimmed();
            if (error.isEmpty()) error = "The export process stopped unexpectedly.";
            finish(success, error);
        });
    connect(process, &QProcess::errorOccurred, this, [process, finish](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) finish(false, process->errorString());
    });
    process->start(m_exportProgram, {"--export-snapshot", snapshot.fileName(), "--" + format,
                                    staged->filePath("output")});
}
bool Deck::exportPdf(const QString &path) {
    for (int i = 0; i < count(); ++i) {
        auto errors = slideProblems(slide(i), baseDir());
        if (!errors.isEmpty()) {
            setStatus(QString("Slide %1: %2").arg(i + 1).arg(errors.join("; ")));
            return false;
        }
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        setStatus(file.errorString());
        return false;
    }
    {
        QPdfWriter writer(&file);
        writer.setPageSize(QPageSize(QSizeF(338.6667, 190.5), QPageSize::Millimeter));
        writer.setPageMargins(QMarginsF(0, 0, 0, 0));
        writer.setResolution(288); // 3840 × 2160 raster budget; text remains vector.
        writer.setTitle(title());
        QPainter painter(&writer);
        painter.setRenderHint(QPainter::LosslessImageRendering);
        if (!painter.isActive()) {
            setStatus("Could not initialize PDF painter");
            return false;
        }
        for (int i = 0; i < count(); ++i) {
            emit exportAdvanced(double(i) / count(), QString("Exporting PDF · slide %1 of %2").arg(i + 1).arg(count()));
            if (i && !writer.newPage()) {
                setStatus("Could not create PDF page");
                return false;
            }
            paintSlide(&painter, QRectF(0, 0, writer.width(), writer.height()), slide(i), baseDir(),
                       palette());
        }
        painter.end();
    }
    if (!file.commit()) {
        setStatus(file.errorString());
        return false;
    }
    setStatus("Exported " + path);
    return true;
}
bool Deck::renderImages(const QString &directory, int width, bool convertAnimations) {
    QDir().mkpath(directory);
    QJsonArray slides;
    for (int i = 0; i < count(); ++i) {
        const double portion = convertAnimations ? 0.8 : 1.0;
        auto progress = [this, i, portion](double fraction, const QString &stage) {
            emit exportAdvanced(portion * (i + fraction) / count(),
                QString("%1 · slide %2 of %3").arg(stage).arg(i + 1).arg(count()));
        };
        progress(0, "Rendering");
        auto errors = slideProblems(slide(i), baseDir());
        if (!errors.isEmpty()) {
            setStatus(QString("Slide %1: %2").arg(i + 1).arg(errors.join("; ")));
            return false;
        }
        QImage image(width, width * 9 / 16, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter p(&image);
        QString warning;
        paintSlide(&p, image.rect(), slide(i), baseDir(), palette(), &warning);
        p.end();
        QString name = QString("slide-%1.png").arg(i + 1, 3, 10, QChar('0'));
        if (!image.save(directory + "/" + name)) {
            setStatus("Could not save rendered slide");
            return false;
        }
        auto media = parseMedia(slide(i), baseDir());
        QJsonObject entry{{"image", name}, {"warning", warning}};
        if (media.video) {
            entry["video"] = media.path;
            if (convertAnimations) {
                QString error;
                const auto movie = preparePowerPointVideo(media.path,
                    QString("%1/video-%2.mp4").arg(directory).arg(i + 1), &error,
                    [&](double fraction) { progress(fraction, "Converting video"); });
                if (movie.isEmpty()) {
                    setStatus(QString("Slide %1: %2").arg(i + 1).arg(error));
                    return false;
                }
                entry["video"] = movie;
            }
            entry["poster"] =
                media.poster.isEmpty() ? ensurePoster(media.path, baseDir()) : media.poster;
            entry["span"] = media.span;
            entry["title"] = !media.text.trimmed().isEmpty();
            entry["autoplay"] = media.autoplay;
            entry["loop"] = media.loop;
            entry["muted"] = media.muted;
        }
        if (convertAnimations && !media.video && !media.path.isEmpty()) {
            QImageReader reader(media.path);
            if (reader.supportsAnimation() && reader.imageCount() > 1) {
                const QString movie = QString("animation-%1.mp4").arg(i + 1);
                int repeats = 1;
                QString error;
                progress(0, "Converting animation");
                if (!exportAnimation(slide(i), baseDir(), palette(), directory + "/" + movie, width,
                                     &repeats, &error,
                                     [&](double fraction) { progress(fraction, "Converting animation"); })) {
                    setStatus(QString("Slide %1: %2").arg(i + 1).arg(error));
                    return false;
                }
                // Composite the complete slide to preserve crop, side layouts, and alpha.
                entry["video"] = movie;
                entry["poster"] = name;
                entry["span"] = true;
                entry["autoplay"] = media.autoplay;
                entry["loop"] = repeats < 0;
                entry["repeatCount"] = repeats;
                entry["muted"] = true;
            }
        }
        if (media.video && media.span) {
            QImage overlay(width, width * 9 / 16, QImage::Format_ARGB32_Premultiplied);
            overlay.fill(Qt::transparent);
            QPainter op(&overlay);
            paintSlide(&op, overlay.rect(), slide(i), baseDir(), palette(), nullptr, true);
            op.end();
            QString name = QString("overlay-%1.png").arg(i + 1);
            if (!overlay.save(directory + "/" + name)) {
                setStatus("Could not save video overlay");
                return false;
            }
            entry["overlay_image"] = name;
        }
        slides.append(entry);
    }
    QFile manifest(directory + "/slides.json");
    if (!manifest.open(QIODevice::WriteOnly))
        return false;
    manifest.write(QJsonDocument(QJsonObject{{"title", title()}, {"slides", slides}}).toJson());
    return true;
}
bool Deck::exportPptx(const QString &path) {
    QTemporaryDir temp;
    if (!renderImages(temp.path(), 3840, true))
        return false;
    QString error;
    emit exportAdvanced(0.8, "Packaging PowerPoint…");
    if (!writePptx(temp.path() + "/slides.json", path, &error,
        [this](double fraction) { emit exportAdvanced(0.8 + 0.2 * fraction, "Packaging PowerPoint…"); })) {
        setStatus("PowerPoint export failed: " + error);
        return false;
    }
    setStatus("Exported " + path);
    return true;
}
