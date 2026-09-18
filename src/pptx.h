#pragma once
#include <QString>

// Packages Hype's rendered-slide manifest into a self-contained PowerPoint file.
// Failure leaves any existing destination untouched and returns a readable error.
bool writePptx(const QString &manifestPath, const QString &destination, QString *error = nullptr);
