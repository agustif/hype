#pragma once
#include <QStringList>
// Headless commands for scripts and agents: hype check, render, export, and so on.
bool isCliCommand(const QString &word);
QString cliSummary();
int runCli(const QStringList &arguments);
