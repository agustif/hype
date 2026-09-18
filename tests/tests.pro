QT += core gui qml quick quickcontrols2 multimedia widgets testlib pdf
CONFIG += c++17 testcase
TEMPLATE = app
TARGET = hype-tests
INCLUDEPATH += ../src
SOURCES += tests.cpp ../src/deck.cpp ../src/renderer.cpp
HEADERS += ../src/deck.h ../src/renderer.h
RESOURCES += ../src/resources.qrc

SOURCES += ../src/syntax.cpp
HEADERS += ../src/syntax.h
