#pragma once
#include <QImage>
#include <QByteArray>

QSize imageSizeForCanvas(QSize original, QSize canvas, bool span);
QImage readSizedImage(const QString &path, QSize canvas, bool span);
QByteArray compressedImage(const QImage &image, QString *extension);
