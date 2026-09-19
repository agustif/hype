#pragma once
#include <QString>
#include <functional>

// Packages Hype's rendered-slide manifest into a self-contained PowerPoint file.
// Failure leaves any existing destination untouched and returns a readable error.
bool writePptx(const QString &manifestPath, const QString &destination, QString *error = nullptr,
               const std::function<void(double)> &progress = {});
QString preparePowerPointVideo(const QString &source, const QString &output, QString *error,
                              const std::function<void(double)> &progress = {});
