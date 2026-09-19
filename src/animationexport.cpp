#include "animationexport.h"
#include "renderer.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QPainter>
#include <QProcess>
#include <QTemporaryDir>
#include <memory>
#include <webp/demux.h>

bool exportAnimation(const QString &source, const QString &base, const QVariantMap &palette,
                     const QString &output, int width, int *repeats, QString *error,
                     const std::function<void(double)> &progress) {
    const auto media = parseMedia(source, base);
    QImageReader reader(media.path);
    int frames = reader.imageCount();
    *repeats = media.loop || reader.loopCount() < 0 ? -1 : reader.loopCount() + 1;
    QByteArray webpBytes;
    std::unique_ptr<WebPAnimDecoder, decltype(&WebPAnimDecoderDelete)> webp(nullptr,
                                                                            WebPAnimDecoderDelete);
    WebPAnimInfo info{};
    if (reader.format() == "webp") {
        // libwebp composites disposal-to-background frames correctly, including
        // fully transparent replacements which Qt's WebP reader can retain.
        QFile file(media.path);
        if (!file.open(QIODevice::ReadOnly)) {
            *error = file.errorString();
            return false;
        }
        webpBytes = file.readAll();
        const WebPData data{reinterpret_cast<const uint8_t *>(webpBytes.constData()),
                            size_t(webpBytes.size())};
        webp.reset(WebPAnimDecoderNew(&data, nullptr));
        if (!webp || !WebPAnimDecoderGetInfo(webp.get(), &info)) {
            *error = "Cannot decode WebP animation";
            return false;
        }
        frames = int(info.frame_count);
        *repeats = media.loop || info.loop_count == 0 ? -1 : int(info.loop_count);
    }
    int previousTimestamp = 0;
    QTemporaryDir temp(QFileInfo(output).absolutePath() + "/animation-XXXXXX");
    if (!temp.isValid()) {
        *error = "Cannot create temporary animation directory";
        return false;
    }
    const QSize size(width, width * 9 / 16);
    QImage background(size, QImage::Format_RGB32);
    QImage overlay(size, QImage::Format_ARGB32_Premultiplied);
    overlay.fill(Qt::transparent);
    {
        QPainter p(&background);
        paintSlide(&p, background.rect(), source, base, palette, nullptr, false, true);
        QPainter op(&overlay);
        paintSlide(&op, overlay.rect(), source, base, palette, nullptr, true);
    }
    const QRectF rect = mediaRect(media);
    QByteArray concat("ffconcat version 1.0\n");
    qint64 duration = 0;
    for (int i = 0; i < frames; ++i) {
        if (progress) progress(0.5 * i / frames);
        QImage frame;
        int delay;
        if (webp) {
            uint8_t *pixels = nullptr;
            int timestamp = 0;
            if (!WebPAnimDecoderGetNext(webp.get(), &pixels, &timestamp)) {
                *error = "Cannot decode WebP frame " + QString::number(i + 1);
                return false;
            }
            frame = QImage(pixels, int(info.canvas_width), int(info.canvas_height),
                           QImage::Format_RGBA8888);
            delay = qMax(1, timestamp - previousTimestamp);
            previousTimestamp = timestamp;
        } else {
            frame = reader.read();
            delay = qMax(1, reader.nextImageDelay());
        }
        if (frame.isNull()) {
            *error = "Cannot decode animation frame " + QString::number(i + 1) + ": " +
                     reader.errorString();
            return false;
        }
        QImage slide = background.copy();
        {
            QPainter p(&slide);
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            p.save();
            p.scale(width / 1920.0, size.height() / 1080.0);
            QSizeF scaled = frame.size();
            scaled.scale(rect.size(),
                         media.span ? Qt::KeepAspectRatioByExpanding : Qt::KeepAspectRatio);
            p.setClipRect(rect);
            p.drawImage(
                QRectF(rect.center() - QPointF(scaled.width() / 2, scaled.height() / 2), scaled),
                media.text.trimmed().isEmpty() ? frame : softenedImage(frame, scaled));
            p.restore();
            p.drawImage(0, 0, overlay);
        }
        const QString name = QString("frame-%1.png").arg(i);
        if (!slide.save(temp.filePath(name))) {
            *error = "Cannot save animation frame";
            return false;
        }
        duration += delay;
        concat += "file '" + name.toUtf8() + "'\noption framerate 1000\nduration " +
                  QByteArray::number(delay / 1000.0, 'f', 3) + "\n";
    }
    // The terminal duplicate preserves the final frame hold in the concat input.
    concat += "file 'frame-" + QByteArray::number(frames - 1) + ".png'\noption framerate 1000\n";
    QFile list(temp.filePath("frames.ffconcat"));
    if (!list.open(QIODevice::WriteOnly) || list.write(concat) != concat.size()) {
        *error = "Cannot write animation frame timings";
        return false;
    }
    list.close();
    QProcess encoder;
    encoder.setWorkingDirectory(temp.path());
    encoder.start("ffmpeg", {"-v",
                             "error",
                             "-nostdin",
                             "-y",
                             "-f",
                             "concat",
                             "-safe",
                             "0",
                             "-i",
                             "frames.ffconcat",
                             "-an",
                             "-c:v",
                             "libx264",
                             "-threads", "2",
                             "-progress", "pipe:1",
                             "-preset",
                             "fast",
                             "-crf",
                             "18",
                             "-pix_fmt",
                             "yuv420p",
                             "-vf",
                             "fps=60",
                             "-fps_mode",
                             "cfr",
                             "-t",
                             QString::number(duration / 1000.0, 'f', 3),
                             "-movflags",
                             "+faststart",
                             QFileInfo(output).absoluteFilePath()});
    if (!encoder.waitForStarted()) {
        *error = "Cannot start ffmpeg for animation conversion: " + encoder.errorString();
        return false;
    }
    QByteArray pending;
    do {
        encoder.waitForFinished(200);
        pending += encoder.readAllStandardOutput();
        int end;
        while ((end = pending.indexOf('\n')) >= 0) {
            const QByteArray line = pending.left(end);
            pending.remove(0, end + 1);
            if (progress && duration > 0 && line.startsWith("out_time_us="))
                progress(0.5 + 0.5 * qBound(0.0, line.mid(12).toDouble() / 1000 / duration, 1.0));
        }
    } while (encoder.state() != QProcess::NotRunning);
    if (encoder.exitStatus() != QProcess::NormalExit || encoder.exitCode() != 0) {
        *error = "Animation conversion failed: " +
                 QString::fromUtf8(encoder.readAllStandardError()).trimmed();
        return false;
    }
    return true;
}
