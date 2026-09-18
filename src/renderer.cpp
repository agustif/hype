#include "renderer.h"
#include "syntax.h"
#include <QAbstractTextDocumentLayout>
#include <QCache>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QPainter>
#include <QProcess>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextTable>

static QRegularExpression mediaRe(R"(!\[([^\]]*)\]\((?:<([^>]+)>|([^\s)]+))\))");
static QString outsideCode(QString source, bool maskInline = true) {
    int position = 0, fenceLength = 0;
    QChar fence;
    QRegularExpression marker("^ {0,3}(`{3,}|~{3,})(.*)$");
    while (position < source.size()) {
        int end = source.indexOf('\n', position);
        if (end < 0)
            end = source.size();
        QString line = source.mid(position, end - position);
        auto match = marker.match(line);
        bool protectedLine = fenceLength > 0;
        if (match.hasMatch()) {
            QString run = match.captured(1);
            protectedLine = true;
            if (!fenceLength) {
                fence = run[0];
                fenceLength = run.size();
            } else if (run[0] == fence && run.size() >= fenceLength &&
                       match.captured(2).trimmed().isEmpty())
                fenceLength = 0;
        }
        if (protectedLine || line.startsWith("    "))
            source.replace(position, end - position, QString(end - position, ' '));
        position = end + 1;
    }
    if (!maskInline)
        return source;
    QRegularExpression inlineCode("(`+)([^`]|`(?!`))*?\\1");
    auto matches = inlineCode.globalMatch(source);
    QVector<QPair<int, int>> ranges;
    while (matches.hasNext()) {
        auto m = matches.next();
        ranges.append({m.capturedStart(), m.capturedLength()});
    }
    for (auto range : ranges)
        source.replace(range.first, range.second, QString(range.second, ' '));
    return source;
}
static QString withoutComments(QString source) {
    auto matches = QRegularExpression("<!--[\\s\\S]*?-->").globalMatch(outsideCode(source));
    QVector<QPair<int, int>> ranges;
    while (matches.hasNext()) {
        auto m = matches.next();
        ranges.append({m.capturedStart(), m.capturedLength()});
    }
    for (auto it = ranges.crbegin(); it != ranges.crend(); ++it)
        source.remove(it->first, it->second);
    return source;
}
static QString assetPath(const QString &base, QString file, bool video) {
    if (QFileInfo(file).isAbsolute())
        return file;
    return QDir(base).filePath(file.startsWith("images/") || file.startsWith("videos/")
                                   ? file
                                   : (video ? "videos/" : "images/") + file);
}
Media parseMedia(const QString &source, const QString &base) {
    Media result;
    result.text = withoutComments(source);
    auto m = mediaRe.match(outsideCode(result.text));
    if (!m.hasMatch())
        return result;
    result.file = m.captured(2).isEmpty() ? m.captured(3) : m.captured(2);
    result.video = QStringList{"mp4", "m4v", "mov", "webm", "mkv"}.contains(
        QFileInfo(result.file).suffix().toLower());
    result.path = assetPath(base, result.file, result.video);
    result.text.remove(m.capturedStart(), m.capturedLength());
    result.span =
        !result.video &&
        result.text.contains(QRegularExpression("^# ", QRegularExpression::MultilineOption));
    QString flags = m.captured(1).trimmed();
    QRegularExpression tokens(R"re(([a-z]+)(?:=("(?:[^"\\]|\\.)*"|[^\s]+))?)re");
    auto first = tokens.match(flags);
    bool directives =
        first.hasMatch() && first.capturedStart() == 0 &&
        (QStringList{"fit", "span", "left", "right", "loop", "muted"}.contains(first.captured(1)) ||
         !first.captured(2).isEmpty());
    QString explicitOverlay;
    bool fit = false, span = false;
    if (directives) {
        auto it = tokens.globalMatch(flags);
        int consumed = 0;
        while (it.hasNext()) {
            auto token = it.next();
            if (!flags.mid(consumed, token.capturedStart() - consumed).trimmed().isEmpty())
                result.error = "Invalid media directive";
            consumed = token.capturedEnd();
            QString key = token.captured(1), value = token.captured(2);
            if (value.startsWith('"'))
                value = value.mid(1, value.size() - 2).replace("\\\"", "\"").replace("\\\\", "\\");
            if (key == "span") {
                result.span = true;
                span = true;
            } else if (key == "fit") {
                result.span = false;
                fit = true;
            } else if (key == "left" || key == "right") {
                result.side = key;
                result.span = false;
            } else if (key == "loop")
                result.loop = value != "false";
            else if (key == "muted")
                result.muted = value != "false";
            else if (key == "autoplay")
                result.autoplay = value != "false";
            else if (key == "overlay")
                explicitOverlay = value;
            else if (key == "background") {
                result.background = value;
                if (value != "auto" && value != "theme" && !QColor(value).isValid())
                    result.error = "Invalid background color";
            } else if (key == "poster")
                result.poster = assetPath(base, value, false);
            else if (key != "alt")
                result.error = "Unknown media directive: " + key;
        }
        if (!flags.mid(consumed).trimmed().isEmpty())
            result.error = "Invalid media directive";
    }
    if (!result.side.isEmpty() && result.video)
        result.error = "Side placement currently supports images only";
    if (!result.side.isEmpty() && span)
        result.error = "Choose side placement or span";
    if (fit && span)
        result.error = "Choose either span or fit";
    result.overlay = result.span && !result.text.trimmed().isEmpty() ? 0.25 : 0;
    if (!explicitOverlay.isEmpty()) {
        bool ok;
        double opacity = explicitOverlay.toDouble(&ok);
        if (!ok || opacity < 0 || opacity > 1)
            result.error = "Overlay must be between 0 and 1";
        else
            result.overlay = opacity;
    }
    return result;
}
QString ensurePoster(const QString &video, const QString &base) {
    QFileInfo info(video);
    if (!info.exists())
        return {};
    static thread_local QHash<QString, QString> hashes;
    QString identity = info.absoluteFilePath() + QString::number(info.size()) +
                       QString::number(info.lastModified().toMSecsSinceEpoch());
    QString digest = hashes.value(identity);
    if (digest.isEmpty()) {
        QFile file(video);
        if (!file.open(QIODevice::ReadOnly))
            return {};
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!hash.addData(&file))
            return {};
        digest = QString::fromLatin1(hash.result().toHex().left(16));
        hashes[identity] = digest;
    }
    QString path = base + "/images/.hype-poster-" + digest + ".jpg";
    if (QFile::exists(path))
        return path;
    QDir().mkpath(base + "/images");
    QProcess ffmpeg;
    ffmpeg.start("ffmpeg", {"-v", "error", "-y", "-i", video, "-frames:v", "1", "-vf",
                            "scale=1280:-2", path});
    if (!ffmpeg.waitForFinished(30000) || ffmpeg.exitCode() != 0) {
        ffmpeg.kill();
        ffmpeg.waitForFinished();
        return {};
    }
    return path;
}
QStringList slideProblems(const QString &source, const QString &base) {
    QStringList errors;
    auto media = parseMedia(source, base);
    if (!media.error.isEmpty())
        errors << media.error;
    if (!media.file.isEmpty() && !QFileInfo::exists(media.path))
        errors << "Missing media: " + media.file;
    if (!media.file.isEmpty() && !media.video && QFileInfo::exists(media.path) &&
        !QImageReader(media.path).canRead())
        errors << "Cannot decode image: " + media.file;
    if (!media.poster.isEmpty() && !QFileInfo::exists(media.poster))
        errors << "Missing poster";
    auto matches = mediaRe.globalMatch(outsideCode(withoutComments(source)));
    int count = 0;
    while (matches.hasNext()) {
        matches.next();
        ++count;
    }
    if (count > 1)
        errors << "Use one media item per slide (combine artwork before importing)";
    return errors;
}
static QImage loadedImage(const QString &path) {
    static thread_local QCache<QString, QImage> cache(256 * 1024);
    const QFileInfo info(path);
    QString key = path + QString::number(info.lastModified().toMSecsSinceEpoch()) + ":" +
                  QString::number(info.size());
    if (auto *image = cache.object(key))
        return *image;
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QSize size = reader.size();
    if (size.width() > 2560 || size.height() > 2560)
        reader.setScaledSize(size.scaled(2560, 2560, Qt::KeepAspectRatio));
    QImage image = reader.read();
    if (!image.isNull())
        cache.insert(key, new QImage(image), qMax(1, int(image.sizeInBytes() / 1024)));
    return image;
}
static QString slideProperty(const QString &source, const QString &key) {
    QRegularExpression re("<!--\\s*hype:[\\s\\S]*?\\b" + key + "=\"([^\"]*)\"[\\s\\S]*?-->");
    return re.match(source).captured(1);
}
static QString preserveLineBreaks(QString markdown) {
    markdown.replace("\r\n", "\n").replace('\r', '\n');
    const QStringList visible = outsideCode(markdown, false).split('\n');
    QStringList lines = markdown.split('\n');
    for (int i = 0; i + 1 < lines.size(); ++i) {
        if (!visible[i].trimmed().isEmpty() && !lines[i].endsWith("  ") && !lines[i].endsWith('\\'))
            lines[i] += "  ";
    }
    return lines.join('\n');
}
static void textDocument(QTextDocument &doc, const QString &markdown, const QVariantMap &palette,
                         qreal fontSize, qreal width, bool centered, bool code) {
    QFont font(code ? QString("JetBrains Mono")
                    : palette.value("font", "JetBrains Mono").toString());
    font.setPixelSize(qRound(fontSize));
    font.setHintingPreference(QFont::PreferNoHinting);
    doc.setDefaultFont(font);
    doc.setDocumentMargin(0);
    QTextOption option;
    option.setUseDesignMetrics(true);
    option.setWrapMode(code ? QTextOption::NoWrap : QTextOption::WrapAtWordBoundaryOrAnywhere);
    doc.setDefaultTextOption(option);
    doc.setDefaultStyleSheet(
        QString("body { color: %1; } a { color: %2; } pre { white-space: pre; }")
            .arg(palette["foreground"].toString(), palette["accent"].toString()));
    doc.setMarkdown(preserveLineBreaks(markdown), QTextDocument::MarkdownDialectGitHub);
    for (QTextBlock block = doc.begin(); block.isValid(); block = block.next()) {
        QTextCursor cursor(block);
        QTextBlockFormat bf = block.blockFormat();
        int level = bf.headingLevel();
        bf.setAlignment(centered ? Qt::AlignHCenter : Qt::AlignLeft);
        bf.setTopMargin(level ? fontSize * 0.15 : 0);
        bf.setBottomMargin(fontSize * 0.22);
        bf.setLineHeight(115, QTextBlockFormat::ProportionalHeight);
        if (code) {
            bf.setBottomMargin(0);
            bf.setTopMargin(0);
            bf.setLineHeight(120, QTextBlockFormat::ProportionalHeight);
        }
        cursor.setBlockFormat(bf);
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            auto fragment = it.fragment();
            if (!fragment.isValid())
                continue;
            QTextCursor text(&doc);
            text.setPosition(fragment.position());
            text.setPosition(fragment.position() + fragment.length(), QTextCursor::KeepAnchor);
            QTextCharFormat cf;
            QFont f = font;
            f.setPixelSize(qRound(fontSize * (level == 1 ? 1.8 : level ? 1.25 : 1.0)));
            cf.setProperty(QTextFormat::FontPixelSize, f.pixelSize());
            cf.setFontFamilies({font.family()});
            QColor color(palette["foreground"].toString());
            if (fragment.charFormat().fontWeight() >= QFont::Bold && !level)
                color = QColor(palette["accent"].toString());
            if (block.text().startsWith(QString::fromUtf8("—"))) {
                cf.setProperty(QTextFormat::FontPixelSize, qRound(fontSize * 0.7));
            }
            cf.setForeground(color);
            text.mergeCharFormat(cf);
        }
    }
    for (auto it = doc.rootFrame()->begin(); !it.atEnd(); ++it)
        if (auto table = qobject_cast<QTextTable *>(it.currentFrame())) {
            auto fmt = table->format();
            fmt.setBorder(0);
            fmt.setCellPadding(fontSize * 0.2);
            fmt.setCellSpacing(fontSize * 0.1);
            fmt.setWidth(QTextLength(QTextLength::PercentageLength, 100));
            table->setFormat(fmt);
        }
    doc.setTextWidth(width);
}
void paintSlide(QPainter *p, const QRectF &target, const QString &source, const QString &base,
                const QVariantMap &inputPalette, QString *warning, bool overlayOnly) {
    p->save();
    p->setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing |
                      QPainter::SmoothPixmapTransform);
    p->translate(target.topLeft());
    p->scale(target.width() / 1920.0, target.height() / 1080.0);
    QVariantMap palette = inputPalette;
    QString bg = slideProperty(source, "background"), fg = slideProperty(source, "foreground");
    if (QColor(bg).isValid())
        palette["background"] = bg;
    if (QColor(fg).isValid())
        palette["foreground"] = fg;
    if (!overlayOnly)
        p->fillRect(QRectF(0, 0, 1920, 1080), QColor(palette["background"].toString()));
    auto media = parseMedia(source, base);
    QString text = media.text.trimmed();
    auto problems = slideProblems(source, base);
    QRectF area(130, 90, 1660, 900);
    if (!media.file.isEmpty()) {
        QString path =
            media.video ? (media.poster.isEmpty() ? ensurePoster(media.path, base) : media.poster)
                        : media.path;
        QImage image = loadedImage(path);
        if (!overlayOnly && !media.video && !media.span && media.background != "theme" &&
            (bg.isEmpty() || !media.background.isEmpty())) {
            QColor color(media.background);
            if ((media.background.isEmpty() || media.background == "auto") && !image.isNull()) {
                // Quantized edge votes ignore transparent pixels and tolerate compression noise.
                QMap<int, QVector<QColor>> votes;
                for (int i = 0; i < 64; ++i) {
                    int x = i * (image.width() - 1) / 63, y = i * (image.height() - 1) / 63;
                    for (QPoint point : {QPoint(x, 0), QPoint(x, image.height() - 1), QPoint(0, y),
                                         QPoint(image.width() - 1, y)}) {
                        QColor c = image.pixelColor(point);
                        if (c.alpha() < 240)
                            continue;
                        votes[(c.red() / 16) * 256 + (c.green() / 16) * 16 + c.blue() / 16].append(
                            c);
                    }
                }
                QVector<QColor> best;
                for (auto it = votes.cbegin(); it != votes.cend(); ++it)
                    if (it.value().size() > best.size())
                        best = it.value();
                if (best.size() >= 128) {
                    int r = 0, g = 0, b = 0;
                    for (const QColor &c : best) {
                        r += c.red();
                        g += c.green();
                        b += c.blue();
                    }
                    color = QColor(r / best.size(), g / best.size(), b / best.size());
                }
            }
            if (color.isValid()) {
                p->fillRect(QRectF(0, 0, 1920, 1080), color);
                if (fg.isEmpty()) {
                    QString ink = (color.redF() * 0.2126 + color.greenF() * 0.7152 +
                                   color.blueF() * 0.0722) > .55
                                      ? "#161616"
                                      : "#ffffff";
                    palette["foreground"] = ink;
                    palette["accent"] = ink;
                }
            }
        }

        QRectF rect =
            media.span ? QRectF(0, 0, 1920, 1080)
                       : (text.isEmpty() ? QRectF(70, 50, 1780, 980) : QRectF(100, 280, 1720, 730));
        if (!media.side.isEmpty()) {
            rect = QRectF(media.side == "left" ? 60 : 980, 60, 880, 960);
            area = QRectF(media.side == "left" ? 1040 : 100, 90, 780, 900);
        }
        if (!overlayOnly && !image.isNull()) {
            QSizeF scaled = image.size();
            scaled.scale(rect.size(),
                         media.span ? Qt::KeepAspectRatioByExpanding : Qt::KeepAspectRatio);
            QRectF dest(QPointF(rect.center().x() - scaled.width() / 2,
                                rect.center().y() - scaled.height() / 2),
                        scaled);
            p->save();
            p->setClipRect(rect);
            p->drawImage(dest, image);
            p->restore();
        } else if (!overlayOnly) {
            p->setPen(QColor(palette["accent"].toString()));
            p->setFont(QFont("sans", 24));
            p->drawText(rect, Qt::AlignCenter, "Missing media\n" + media.file);
        }
        if (media.span) {
            p->fillRect(QRectF(0, 0, 1920, 1080), QColor(0, 0, 0, qRound(media.overlay * 255)));
            if (fg.isEmpty() && !text.isEmpty())
                palette["foreground"] = "#ffffff";
        } else if (!text.isEmpty() && media.side.isEmpty())
            area = QRectF(130, 40, 1660, 205);
    }
    if (!text.isEmpty()) {
        bool code = text.contains(
            QRegularExpression("^ {0,3}(`{3,}|~{3,})", QRegularExpression::MultilineOption));
        bool quote = text.startsWith('>');
        bool list = text.contains(
            QRegularExpression("^\\s*(?:[-*+] |[0-9]+[.)] )", QRegularExpression::MultilineOption));
        bool table = text.contains(QRegularExpression("\\|[ :|-]+\\|"));
        bool centered = !(code || quote || list || table || !media.side.isEmpty());
        const QString alignment = slideProperty(source, "alignment");
        if (alignment == "left")
            centered = false;
        if (alignment == "center")
            centered = true;
        bool stack = (text.contains('\n') || text.contains('\r')) && !text.startsWith('#') &&
                     !quote && !list && !code;
        qreal low = 8, high = code ? 56 : quote ? 64 : list ? 72 : table ? 60 : stack ? 128 : 76;
        if (!media.file.isEmpty() && !media.span && media.side.isEmpty())
            high = 48;
        QTextDocument doc;
        textDocument(doc, text, palette, high, area.width(), centered, code);
        bool fits = doc.size().height() <= area.height() && doc.idealWidth() <= area.width() + 1;
        if (fits)
            low = high;
        for (int iteration = 0; !fits && iteration < 9; ++iteration) {
            qreal size = (low + high) / 2;
            textDocument(doc, text, palette, size, area.width(), centered, code);
            if (doc.size().height() <= area.height() && doc.idealWidth() <= area.width() + 1)
                low = size;
            else
                high = size;
        }
        textDocument(doc, text, palette, low, area.width(), centered, code);
        highlightCode(doc, palette);
        if (low < 24 && warning)
            *warning = "Text fits below 24px on a 1080p slide";
        p->save();
        p->translate(area.x(), area.y() + qMax(0.0, (area.height() - doc.size().height()) / 2));
        QAbstractTextDocumentLayout::PaintContext context;
        context.palette.setColor(QPalette::Text, QColor(palette["foreground"].toString()));
        doc.documentLayout()->draw(p, context);
        p->restore();
    }
    if (!problems.isEmpty()) {
        p->setPen(Qt::white);
        p->fillRect(QRectF(0, 1000, 1920, 80), QColor("#9b3030"));
        p->setFont(QFont("sans", 16));
        p->drawText(QRectF(30, 1005, 1860, 70), Qt::AlignVCenter, problems.join(" · "));
        if (warning)
            *warning = problems.join("; ");
    }
    p->restore();
}
SlideItem::SlideItem(QQuickItem *parent) : QQuickPaintedItem(parent) { setAntialiasing(true); }
void SlideItem::setDeck(Deck *deck) {
    if (m_deck)
        disconnect(m_deck, nullptr, this, nullptr);
    m_deck = deck;
    if (deck)
        connect(deck, &Deck::changed, this, [this] { update(); });
    emit deckChanged();
    update();
}
void SlideItem::paint(QPainter *p) {
    if (m_deck)
        paintSlide(p, boundingRect(), m_deck->slideSource(), m_deck->baseDir(), m_deck->palette(),
                   nullptr, m_overlayOnly);
}
QImage Thumbnails::requestImage(const QString &id, QSize *size, const QSize &requested) {
    QByteArray bytes =
        QByteArray::fromBase64(id.section('/', 0, 0).toLatin1(), QByteArray::Base64UrlEncoding);
    QDataStream stream(bytes);
    QString source, base;
    QVariantMap palette;
    stream >> source >> base >> palette;
    if (stream.status() != QDataStream::Ok)
        return {};
    QSize dimensions = requested.isValid() ? requested : QSize(320, 180);
    static thread_local QCache<QString, QImage> renders(128 * 1024);
    QString key =
        id + QString::number(dimensions.width()) + "x" + QString::number(dimensions.height());
    if (auto cached = renders.object(key)) {
        if (size)
            *size = cached->size();
        return *cached;
    }
    QImage image(dimensions, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter p(&image);
    paintSlide(&p, image.rect(), source, base, palette, nullptr, id.endsWith("/overlay"));
    p.end();
    renders.insert(key, new QImage(image), qMax(1, int(image.sizeInBytes() / 1024)));
    if (size)
        *size = image.size();
    return image;
}
