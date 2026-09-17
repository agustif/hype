import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia
import Hype 1.0

ApplicationWindow {
    id: win
    width: 1400; height: 900; minimumWidth: 900; minimumHeight: 600
    visible: true
    title: deck.title + (deck.dirty ? " •" : "") + " — Hype"
    color: "#11131b"
    palette.window: "#171923"; palette.base: "#11131b"; palette.text: "#c0caf5"
    palette.windowText: "#c0caf5"; palette.button: "#24283b"; palette.buttonText: "#c0caf5"
    palette.highlight: deck.accent; palette.highlightedText: "#11131b"
    palette.mid: "#33467c"; palette.light: "#414868"; palette.dark: "#24283b"
    property var document: deck
    property bool markdown: false
    property bool editing: false
    property bool presenting: false
    property bool allowClose: false
    property int lastSelected: -1
    property int dragIndex: -1
    property int dropIndex: -1
    property real dragY: 0
    property int dragScroll: 0
    function togglePresent() {
        presenting = !presenting
        if (presenting) { win.showFullScreen(); stage.forceActiveFocus() }
        else { player.stop(); win.showNormal() }
    }
    function alignSource() {
        sourceEditor.cursorPosition = deck.sourcePosition()
        sourceEditor.forceActiveFocus()
        Qt.callLater(function() {
            let rect = sourceEditor.positionToRectangle(deck.sourcePosition())
            sourceScroll.contentItem.contentY = Math.max(0, rect.y - sourceEditor.topPadding)
        })
    }
    function syncEditors() {
        if (sourceEditor.text !== deck.source) sourceEditor.text = deck.source
        if (slideEditor.text !== deck.slideSource) slideEditor.text = deck.slideSource
    }
    Component.onCompleted: { syncEditors(); Qt.callLater(function() { thumbnails.positionViewAtIndex(deck.selected, ListView.Contain) }) }
    Connections {
        target: deck
        function onChanged() {
            win.syncEditors()
            if (win.dragIndex < 0 && win.lastSelected !== deck.selected) Qt.callLater(function() { thumbnails.positionViewAtIndex(deck.selected, ListView.Contain) })
            win.lastSelected = deck.selected
            if (player.source.toString() !== deck.media.url.toString()) player.stop()
            player.source = deck.media.video ? deck.media.url : ""
            if (win.presenting && deck.media.video && deck.media.autoplay) player.play()
        }
    }
    onPresentingChanged: { if (presenting && deck.media.video && deck.media.autoplay) player.play() }
    onClosing: function(close) { if (deck.dirty && !allowClose) { close.accepted = false; closeDialog.open() } }
    Dialog {
        id: closeDialog; title: "Unsaved changes"; modal: true; anchors.centerIn: parent
        standardButtons: Dialog.Discard | Dialog.Cancel
        Label { text: "Discard unsaved changes and quit?" }
        onDiscarded: { win.allowClose = true; win.close() }
    }
    Shortcut { sequences: [StandardKey.Open]; onActivated: deck.openDialog() }
    Shortcut { sequences: [StandardKey.Save]; onActivated: deck.save() }
    Shortcut { sequences: [StandardKey.SaveAs]; onActivated: deck.saveAs() }
    Shortcut { sequence: "Ctrl+N"; onActivated: deck.newDeck() }
    Shortcut { sequence: "F5"; onActivated: win.togglePresent() }
    Shortcut { sequence: "Escape"; enabled: win.presenting; onActivated: win.togglePresent() }
    Shortcut { sequence: "Ctrl+Z"; enabled: !sourceEditor.activeFocus && !slideEditor.activeFocus; onActivated: deck.undo() }
    Shortcut { sequence: "Ctrl+Shift+Z"; enabled: !sourceEditor.activeFocus && !slideEditor.activeFocus; onActivated: deck.redo() }
    Shortcut { sequence: "Ctrl+D"; enabled: !sourceEditor.activeFocus && !slideEditor.activeFocus; onActivated: deck.duplicateSlide() }
    Shortcut { sequence: "Ctrl+Return"; onActivated: { deck.addSlide(); win.editing = true; slideEditor.forceActiveFocus() } }
    Shortcut { sequence: "Delete"; enabled: !sourceEditor.activeFocus && !slideEditor.activeFocus; onActivated: deck.deleteSlide() }
    Shortcut { sequence: "Right"; enabled: win.presenting || (!sourceEditor.activeFocus && !slideEditor.activeFocus); onActivated: deck.select(deck.selected + 1) }
    Shortcut { sequence: "Left"; enabled: win.presenting || (!sourceEditor.activeFocus && !slideEditor.activeFocus); onActivated: deck.select(deck.selected - 1) }
    Shortcut { sequence: "Space"; enabled: win.presenting && deck.media.video; onActivated: player.playbackState === MediaPlayer.PlayingState ? player.pause() : player.play() }
    Shortcut { sequence: "Ctrl+V"; enabled: !sourceEditor.activeFocus && !slideEditor.activeFocus; onActivated: deck.pasteImage() }
    header: ToolBar {
        visible: !win.presenting; height: visible ? 60 : 0
        background: Rectangle { color: "#171923"; border.color: "#292e42" }
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 20; anchors.rightMargin: 18; spacing: 12
            Label { text: "Hype"; font.pixelSize: 22; font.bold: true; color: deck.accent }
            Label { text: "·  " + deck.title; elide: Text.ElideRight; Layout.maximumWidth: 280; color: "#a9b1d6" }
            ToolButton { text: "Open"; onClicked: deck.openDialog() }
            ToolButton { text: deck.dirty ? "Save •" : "Save"; onClicked: deck.save() }
            Item { Layout.fillWidth: true }
            Button { text: "Visual"; highlighted: !win.markdown; onClicked: { win.markdown = false; stage.forceActiveFocus() } }
            Button { text: "Markdown"; highlighted: win.markdown; onClicked: { win.markdown = true; win.alignSource() } }
            Item { Layout.fillWidth: true }
            ComboBox { id: themes;
                delegate: ItemDelegate {
                    required property string modelData
                    required property int index
                    width: themes.width
                    text: modelData
                    highlighted: themes.highlightedIndex === index
                    contentItem: Text { text: modelData; color: "#c0caf5"; font: themes.font; verticalAlignment: Text.AlignVCenter }
                    background: Rectangle { color: parent.highlighted ? "#33467c" : "#171923" }
                }
                background: Rectangle { color: themes.hovered ? "#33467c" : "#24283b"; radius: 3 }
 model: deck.themeNames; currentIndex: Math.max(0, deck.themeNames.indexOf(deck.themeName)); onActivated: deck.chooseTheme(currentText); Layout.preferredWidth: 165 }
            Button { text: "▶ Present"; onClicked: win.togglePresent() }
            Button { text: "Export"; onClicked: exportMenu.open(); Menu { id: exportMenu; MenuItem { text: "PDF"; onTriggered: deck.exportDialog("pdf") } MenuItem { text: "PowerPoint"; onTriggered: deck.exportDialog("pptx") } } }
        }
    }
    footer: ToolBar {
        visible: !win.presenting; height: visible ? 34 : 0
        background: Rectangle { color: "#171923" }
        RowLayout { anchors.fill: parent; anchors.leftMargin: 20; anchors.rightMargin: 20
            Label { text: "Slide " + (deck.selected+1) + " of " + deck.count; font.pixelSize: 12 }
            Label { text: deck.status; elide: Text.ElideRight; Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; font.pixelSize: 12; color: "#7f89ac" }
            Label { text: deck.dirty ? "Unsaved" : "Saved"; font.pixelSize: 12 }
        }
    }
    RowLayout {
        anchors.fill: parent; spacing: 0
        Rectangle {
            visible: !win.presenting; Layout.preferredWidth: 235; Layout.fillHeight: true; color: "#171923"
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 12; spacing: 10
                ListView {
                    id: thumbnails; objectName: "thumbnails"; Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                    model: deck; spacing: 10; currentIndex: deck.selected
                    ScrollBar.vertical: ScrollBar {}
                    WheelHandler {
                        onWheel: function(event) {
                            let delta = event.pixelDelta.y || event.angleDelta.y / 120 * 100
                            thumbnails.contentY = Math.max(0, Math.min(Math.max(0, thumbnails.contentHeight - thumbnails.height), thumbnails.contentY - delta))
                            event.accepted = true
                        }
                    }
                    delegate: Item {
                        id: thumbnail; required property int index; required property string slideTitle; required property int number
                        width: thumbnails.width; height: 136
                        Rectangle {
                            x: 22; width: parent.width - 24; height: 108; color: deck.background
                            border.width: deck.selected === thumbnail.index ? 2 : 1
                            border.color: deck.selected === thumbnail.index ? deck.accent : "#34384b"; radius: 3
                            Image { anchors.fill: parent; anchors.margins: 3; source: "image://slides/" + (deck.revision, deck.renderId(thumbnail.index)); asynchronous: true; cache: true; sourceSize.width: 340; sourceSize.height: 192; fillMode: Image.PreserveAspectFit }
                        }
                        Label { text: thumbnail.number; width: 18; y: 4; color: deck.selected === thumbnail.index ? deck.accent : "#7f89ac"; font.pixelSize: 11 }
                        Label { x: 24; y: 115; width: parent.width - 28; text: thumbnail.slideTitle; elide: Text.ElideRight; font.pixelSize: 11; color: "#7f89ac" }
                        Rectangle { visible: win.dragIndex >= 0 && win.dropIndex === thumbnail.index; height: 3; width: parent.width; color: deck.accent; y: -5 }
                        MouseArea {
                            anchors.fill: parent; acceptedButtons: Qt.LeftButton | Qt.RightButton
                            property real pressY: 0
                            onPressed: function(mouse) {
                                deck.select(thumbnail.index); stage.forceActiveFocus(); pressY = mouse.y
                                if (mouse.button === Qt.RightButton) slideMenu.popup()
                            }
                            onPositionChanged: function(mouse) {
                                if (!(pressedButtons & Qt.LeftButton)) return
                                if (win.dragIndex < 0 && Math.abs(mouse.y - pressY) > 8) win.dragIndex = thumbnail.index
                                if (win.dragIndex >= 0) {
                                    let pos = mapToItem(thumbnails, mouse.x, mouse.y)
                                    win.dragY = pos.y
                                    win.dragScroll = pos.y < 30 ? -1 : (pos.y > thumbnails.height - 30 ? 1 : 0)
                                    win.dropIndex = Math.max(0, Math.min(deck.count-1, Math.floor((pos.y + thumbnails.contentY)/146)))
                                }
                            }
                            onReleased: { if (win.dragIndex >= 0) { let from=win.dragIndex; let to=win.dropIndex; win.dragIndex=-1; win.dropIndex=-1; deck.moveSlide(from,to) } }
                            onCanceled: { win.dragIndex=-1; win.dropIndex=-1 }
                            onClicked: { if (win.markdown) win.alignSource() }
                            onDoubleClicked: { win.editing=true; slideEditor.forceActiveFocus() }
                        }
                    }
                    Timer {
                        interval: 40; repeat: true; running: win.dragIndex >= 0 && win.dragScroll !== 0
                        onTriggered: {
                            thumbnails.contentY = Math.max(0, Math.min(thumbnails.contentHeight - thumbnails.height, thumbnails.contentY + win.dragScroll*18))
                            win.dropIndex = Math.max(0, Math.min(deck.count-1, Math.floor((win.dragY + thumbnails.contentY)/146)))
                        }
                    }
                    Menu { id: slideMenu
                        MenuItem { text: "New slide after this"; onTriggered: { deck.addSlide(); win.editing=true; slideEditor.forceActiveFocus() } }
                        MenuItem { text: "Duplicate slide"; onTriggered: deck.duplicateSlide() }
                        MenuItem { text: "Delete slide"; onTriggered: deck.deleteSlide() }
                    }
                }
                Button { objectName: "newSlideButton"; text: "+ New slide"; Layout.fillWidth: true; onClicked: { deck.addSlide(); win.markdown=false; win.editing=true; slideEditor.forceActiveFocus() } }
                RowLayout { Layout.fillWidth: true
                    Button { objectName: "duplicateButton"; text: "Duplicate"; Layout.fillWidth: true; onClicked: deck.duplicateSlide() }
                    Button { text: "Undo"; onClicked: deck.undo() }
                }
            }
        }
        ColumnLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0
            Item {
                id: stage; objectName: "stage"; Layout.fillWidth: true; Layout.fillHeight: true; focus: true
                visible: !win.markdown || win.presenting
                property real margin: win.presenting ? 0 : 32
                property real slideWidth: Math.min(width-margin*2,(height-margin*2)*16/9)
                Item {
                    id: slideFrame; width: stage.slideWidth; height: width*9/16; anchors.centerIn: parent
                    Image { anchors.fill: parent; source: "image://slides/" + (deck.revision, deck.renderId(deck.selected)); asynchronous: true; cache: true; sourceSize: Qt.size(1920, 1080) }
                    VideoOutput {
                        id: video; visible: deck.media.video && (player.playbackState !== MediaPlayer.StoppedState)
                        x: deck.media.span ? 0 : (deck.media.title ? 100 : 70)*slideFrame.width/1920
                        y: deck.media.span ? 0 : (deck.media.title ? 280 : 50)*slideFrame.height/1080
                        width: deck.media.span ? slideFrame.width : (deck.media.title ? 1720 : 1780)*slideFrame.width/1920
                        height: deck.media.span ? slideFrame.height : (deck.media.title ? 730 : 980)*slideFrame.height/1080
                        fillMode: deck.media.span ? VideoOutput.PreserveAspectCrop : VideoOutput.PreserveAspectFit
                    }
                    Image { anchors.fill: parent; source: visible ? "image://slides/" + (deck.revision, deck.renderId(deck.selected)) + "/overlay" : ""; asynchronous: true; sourceSize: Qt.size(1920,1080); visible: video.visible && win.document.media.span }
                    Button { visible: deck.media.video && !win.presenting; anchors.centerIn: parent; text: player.playbackState === MediaPlayer.PlayingState ? "Pause" : "▶ Play"; onClicked: player.playbackState === MediaPlayer.PlayingState ? player.pause() : player.play() }
                }
                DropArea { anchors.fill: parent; onDropped: function(drop) { if(drop.hasUrls) for(let url of drop.urls) deck.importMedia(url) } }
            }
            ScrollView {
                id: sourceScroll; objectName: "sourceScroll"
                visible: win.markdown && !win.presenting; Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                TextArea {
                    id: sourceEditor; objectName: "sourceEditor"; text: ""; color: "#c0caf5"; selectionColor: "#33467c"
                    font.family: "JetBrains Mono"; font.pixelSize: 18; wrapMode: TextEdit.NoWrap; leftPadding: 32; topPadding: 30
                    onTextChanged: { if(activeFocus && text !== deck.source) deck.editSource(text) }
                    onCursorPositionChanged: { if(activeFocus) deck.selectAt(cursorPosition) }
                    Keys.onPressed: function(event) { if(event.modifiers & Qt.ControlModifier && event.key === Qt.Key_Z) { event.accepted=true; event.modifiers & Qt.ShiftModifier ? deck.redo() : deck.undo() } }
                }
            }
            ToolBar {
                visible: !win.markdown && !win.presenting; Layout.fillWidth: true
                background: Rectangle { color: "#171923" }
                RowLayout { anchors.fill: parent; anchors.leftMargin: 20; spacing: 12
                    Button { text: win.editing ? "Hide text" : "Edit text"; onClicked: {win.editing=!win.editing; if(win.editing)slideEditor.forceActiveFocus()} }
                    Button { text: "+ Image / video"; onClicked: deck.importDialog() }
                    Button { text: "Fit"; onClicked: deck.setMediaMode("fit") }
                    Button { text: "Span"; onClicked: deck.setMediaMode("span") }
                    Button { text: "Left"; enabled: !deck.media.video; onClicked: deck.setMediaMode("left") }
                    Button { text: "Right"; enabled: !deck.media.video; onClicked: deck.setMediaMode("right") }
                    Button { text: "Background"; enabled: !deck.media.video; onClicked: backgroundMenu.open(); Menu { id: backgroundMenu; MenuItem { text: "Match image edges"; onTriggered: deck.matchImageBackground(true) } MenuItem { text: "Use theme color"; onTriggered: deck.matchImageBackground(false) } } }
                    Item { Layout.fillWidth: true }
                }
            }
            ScrollView {
                visible: win.editing && !win.markdown && !win.presenting; Layout.fillWidth: true; Layout.preferredHeight: 190; clip: true
                TextArea {
                    id: slideEditor; objectName: "slideEditor"; color: "#c0caf5"; font.family: "JetBrains Mono"; font.pixelSize: 16
                    wrapMode: TextEdit.Wrap; leftPadding: 24; topPadding: 16; placeholderText: "# Your headline"
                    onTextChanged: { if(activeFocus && text !== deck.slideSource) deck.editSlide(text) }
                    Keys.onPressed: function(event) { if(event.modifiers & Qt.ControlModifier && event.key === Qt.Key_Z) { event.accepted=true; event.modifiers & Qt.ShiftModifier ? deck.redo() : deck.undo() } }
                }
            }
        }
    }
    MediaPlayer { id: player; objectName: "player"; videoOutput: video; audioOutput: AudioOutput { muted: deck.media.muted } loops: deck.media.loop ? MediaPlayer.Infinite : 1; onErrorOccurred: function(error,message) { deck.setStatus(message) } }
}
