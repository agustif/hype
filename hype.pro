QT += core gui qml quick quickcontrols2 multimedia concurrent
linux: QT += dbus
macx: QT += widgets
# Like Qt's own modules, Hype never throws or catches. Without unwinding tables and with
# link-time optimization, the installed binary is about a quarter smaller.
CONFIG += c++17 release ltcg exceptions_off
TARGET = hype
TEMPLATE = app
HEADERS += src/deck.h src/renderer.h
SOURCES += src/main.cpp src/deck.cpp src/renderer.cpp
RESOURCES += src/resources.qrc

SOURCES += src/syntax.cpp
HEADERS += src/syntax.h
SOURCES += src/pptx.cpp
HEADERS += src/pptx.h

linux {
    LIBS += -lz -lwebpdemux -lwebp
}

macx {
    # Homebrew paths for Apple Silicon
    HOMEBREW_PREFIX = $$system(brew --prefix)
    isEmpty(HOMEBREW_PREFIX) {
        # Fallback to default Homebrew paths
        exists(/opt/homebrew/bin/brew) {
            HOMEBREW_PREFIX = /opt/homebrew
        } else:exists(/usr/local/bin/brew) {
            HOMEBREW_PREFIX = /usr/local
        }
    }
    
    INCLUDEPATH += $$HOMEBREW_PREFIX/include
    LIBS += -L$$HOMEBREW_PREFIX/lib -lz -lwebpdemux -lwebp
    
    # App bundle configuration
    QMAKE_INFO_PLIST = macos/Info.plist
    ICON = macos/hype.icns
    
    # macdeployqt needs this
    QMAKE_POST_LINK += install_name_tool -add_rpath @executable_path/../Frameworks $(TARGET)
}

SOURCES += src/animationexport.cpp
HEADERS += src/animationexport.h

SOURCES += src/apptheme.cpp
HEADERS += src/apptheme.h
SOURCES += src/images.cpp
HEADERS += src/images.h
SOURCES += src/filedialog.cpp
HEADERS += src/filedialog.h
SOURCES += src/recovery.cpp
SOURCES += src/cli.cpp
HEADERS += src/cli.h
