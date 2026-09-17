#include "deck.h"
#include "renderer.h"
#include <QApplication>
#include <QClipboard>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPainter>
#include <QPdfWriter>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>

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
Deck::Deck(QObject *parent) : QAbstractListModel(parent) {
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
    emit statusChanged();
}
void Deck::apply(const QString &source, int selected, bool history) {
    if (source == m_source) {
        select(selected);
        return;
    }
    if (history) {
        m_undo.append({m_source, m_selected});
        if (m_undo.size() > 200)
            m_undo.removeFirst();
        m_redo.clear();
    }
    auto parsed = parseDeck(source);
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
    if (m_selected == index)
        return;
    m_selected = index;
    emit changed();
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
    QString body = s;
    if (m_selected < count() - 1 && !body.endsWith('\n'))
        body += '\n';
    QString edited = m_source;
    edited.replace(range.start, range.end - range.start, body);
    apply(edited, m_selected);
}
void Deck::replaceSlides(const QStringList &slides, int selected) {
    QString out = m_parsed.header;
    for (int i = 0; i < slides.size(); ++i) {
        if (i) {
            if (!out.endsWith('\n'))
                out += '\n';
            out += "---\n";
        }
        out += slides[i];
    }
    apply(out, selected);
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
    list.insert(m_selected + 1, "\n\n");
    replaceSlides(list, m_selected + 1);
}
void Deck::duplicateSlide() {
    QStringList list;
    for (auto &s : m_parsed.slides)
        list << s.source;
    list.insert(m_selected + 1, slideSource());
    replaceSlides(list, m_selected + 1);
}
void Deck::deleteSlide() {
    QStringList list;
    for (auto &s : m_parsed.slides)
        list << s.source;
    list.removeAt(m_selected);
    if (list.isEmpty())
        list << "\n";
    replaceSlides(list, qMin(m_selected, int(list.size()) - 1));
}
void Deck::undo() {
    if (m_undo.isEmpty())
        return;
    auto state = m_undo.takeLast();
    m_redo.append({m_source, m_selected});
    apply(state.source, state.selected, false);
}
void Deck::redo() {
    if (m_redo.isEmpty())
        return;
    auto state = m_redo.takeLast();
    m_undo.append({m_source, m_selected});
    apply(state.source, state.selected, false);
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
    apply(header + m_source.mid(m_parsed.header.size()), m_selected);
}
QVariantMap Deck::media() const {
    const QString source = slideSource(), base = baseDir();
    if (!m_mediaCache.isEmpty() && m_mediaSource == source && m_mediaBase == base)
        return m_mediaCache;
    m_mediaSource = source;
    m_mediaBase = base;
    auto m = parseMedia(source, base);
    const bool title = !m.text.trimmed().isEmpty();
    m_mediaCache = {{"url", QUrl::fromLocalFile(m.path)},
                    {"video", m.video},
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
    return true;
}
bool Deck::savePath(const QString &path) {
    if (QFileInfo(path).absoluteFilePath() == m_path && m_externalChange) {
        setStatus("File changed on disk. Use Save As to keep both versions.");
        return false;
    }
    if (QFileInfo(path).absoluteFilePath() == m_path) {
        QFile disk(m_path);
        if (disk.open(QIODevice::ReadOnly) && QString::fromUtf8(disk.readAll()) != m_saved) {
            m_externalChange = true;
            setStatus("File changed on disk. Save a copy or reopen.");
            return false;
        }
    }
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(m_source.toUtf8()) < 0 || !f.commit()) {
        setStatus(f.errorString());
        return false;
    }
    m_path = QFileInfo(path).absoluteFilePath();
    m_saved = m_source;
    m_externalChange = false;
    watch();
    emit changed();
    setStatus("Saved");
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
    QString p =
        QFileDialog::getOpenFileName(nullptr, "Open presentation", baseDir(), "Markdown (*.md)");
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
    QString p = QFileDialog::getSaveFileName(
        nullptr, "Save presentation", m_path.isEmpty() ? baseDir() + "/presentation.md" : m_path,
        "Markdown (*.md)");
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
    QString p = QFileDialog::getOpenFileName(nullptr, "Add image or video", baseDir(),
                                             "Media (*.png *.jpg *.jpeg *.webp *.gif "
                                             "*.svg *.mp4 *.mov *.mkv *.webm *.m4v)");
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
void Deck::pasteImage() {
    QImage image = QApplication::clipboard()->image();
    if (image.isNull()) {
        setStatus("No image on the clipboard.");
        return;
    }
    QTemporaryDir dir;
    QString path = dir.path() + "/pasted-image.png";
    image.save(path);
    importMedia(QUrl::fromLocalFile(path));
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
    auto media = parseMedia(slideSource(), baseDir());
    if (media.file.isEmpty() || media.video)
        return;
    QString source = slideSource();
    QRegularExpression re("!\\[([^\\]]*)\\]\\(");
    auto match = re.match(source);
    if (!match.hasMatch())
        return;
    QString flags = match.captured(1);
    flags.remove(QRegularExpression("\\s*background=(?:\"[^\"]*\"|[^\\s]+)"));
    if (!flags.isEmpty() && !flags.contains('=') &&
        !QStringList{"fit", "left", "right", "span"}.contains(flags.trimmed()))
        flags = "alt=\"" + flags.replace('"', "\\\"") + "\"";
    flags += enabled ? " background=auto" : " background=theme";
    source.replace(match.capturedStart(1), match.capturedLength(1), flags.trimmed());
    editSlide(source);
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
    QString p = QFileDialog::getSaveFileName(nullptr, "Export " + format.toUpper(),
                                             baseDir() + "/" + title() + "." + format,
                                             format.toUpper() + " (*." + format + ")");
    if (p.isEmpty())
        return;
    setStatus("Exporting…");
    if (format == "pdf")
        exportPdf(p);
    else
        exportPptx(p);
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
        writer.setResolution(144);
        writer.setTitle(title());
        QPainter painter(&writer);
        if (!painter.isActive()) {
            setStatus("Could not initialize PDF painter");
            return false;
        }
        for (int i = 0; i < count(); ++i) {
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
bool Deck::renderImages(const QString &directory, int width) {
    QDir().mkpath(directory);
    QJsonArray slides;
    for (int i = 0; i < count(); ++i) {
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
            entry["poster"] =
                media.poster.isEmpty() ? ensurePoster(media.path, baseDir()) : media.poster;
            entry["span"] = media.span;
            entry["title"] = !media.text.trimmed().isEmpty();
            entry["autoplay"] = media.autoplay;
            entry["loop"] = media.loop;
            entry["muted"] = media.muted;
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
    if (!renderImages(temp.path()))
        return false;
    QString helper = QCoreApplication::applicationDirPath() + "/../tools/export_pptx.py";
    if (!QFile::exists(helper))
        helper = "/usr/share/hype/export_pptx.py";
    QString python = QCoreApplication::applicationDirPath() + "/python/bin/python";
    if (!QFile::exists(python))
        python = "python";
    QProcess process;
    process.start(python, {helper, temp.path() + "/slides.json", path});
    if (!process.waitForFinished(120000) || process.exitCode() != 0) {
        setStatus("PowerPoint export failed: " + QString::fromUtf8(process.readAllStandardError()));
        return false;
    }
    setStatus("Exported " + path);
    return true;
}
