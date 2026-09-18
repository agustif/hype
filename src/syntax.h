#pragma once
#include <QTextDocument>
#include <QVariantMap>

// Apply colors only: preserve the Markdown document's text, fonts and layout.
void highlightCode(QTextDocument &document, const QVariantMap &palette);
