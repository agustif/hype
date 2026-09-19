#pragma once
#include <QString>
#include <QVariantMap>
#include <functional>

// Render one animation cycle; PowerPoint timing handles repetitions.
bool exportAnimation(const QString &source, const QString &base, const QVariantMap &palette,
                     const QString &output, int width, int *repeats, QString *error,
                     const std::function<void(double)> &progress = {});
