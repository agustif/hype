QT += core gui qml quick quickcontrols2 multimedia widgets
CONFIG += c++17 release
TARGET = hype
TEMPLATE = app
HEADERS += src/deck.h src/renderer.h
SOURCES += src/main.cpp src/deck.cpp src/renderer.cpp
RESOURCES += src/resources.qrc

SOURCES += src/syntax.cpp
HEADERS += src/syntax.h
