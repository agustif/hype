#pragma once
#include <QString>
#include <QVariantMap>

// Render one animation cycle; PowerPoint timing handles repetitions.
bool exportAnimation(const QString &source, const QString &base, const QVariantMap &palette,
                     const QString &output, int width, int *repeats, QString *error);
