import QtQuick
import QtQuick.Effects
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia
import Hype 1.0
import "Markdown.js" as Markdown

ApplicationWindow {
    id: win
    width: 1400; height: 900; minimumWidth: 900; minimumHeight: 600
    visible: true
    title: deck.title + (deck.dirty ? " •" : "") + " — Hype"
    AppTheme { id: appTheme }
    readonly property var ui: appTheme.colors
    color: ui.background
    palette.window: win.ui.panel; palette.base: win.ui.background; palette.text: win.ui.foreground
    palette.placeholderText: win.ui.muted
    palette.windowText: win.ui.foreground; palette.button: win.ui.button; palette.buttonText: win.ui.foreground
    palette.highlight: win.ui.selection; palette.highlightedText: win.ui.selectionText
    palette.mid: win.ui.hover; palette.light: win.ui.border; palette.dark: win.ui.button
    property bool markdown: false
    property bool syncingEditor: false
    property bool editingSlide: false
    property bool presenting: false
    readonly property bool popupOpen: pasteDialog.visible || compressionDialog.visible ||
        historyDialog.visible || closeDialog.visible || themes.popup.visible || fonts.popup.visible ||
        slideMenu.visible || mediaMenu.visible || exportMenu.visible
    property var compressionReturnFocus: null
    property bool allowClose: false
    property int lastSelected: -1
    property int dragIndex: -1
    property int dropIndex: -1
    property real dragY: 0
    property real dragX: 0
    property int dragScroll: 0
    function togglePresent() {
        presenting = !presenting
        if (presenting) { win.showFullScreen(); stage.forceActiveFocus() }
        else { player.stop(); win.showNormal() }
    }
    function toggleVideo() {
        if (player.playbackState === MediaPlayer.PlayingState) player.pause()
        else {
            if (player.mediaStatus === MediaPlayer.EndOfMedia) player.position = 0
            player.play()
        }
    }
    function scrollEditor(flick, event) {
        let delta = event.angleDelta.y ? event.angleDelta.y / 120 * 180 : event.pixelDelta.y
        if (!delta) { event.accepted = false; return }
        flick.cancelFlick()
        flick.contentY = Math.max(0, Math.min(Math.max(0, flick.contentHeight - flick.height + flick.bottomMargin), flick.contentY - delta))
        event.accepted = true
    }
    function switchEditingFocus() {
        if (slideEditor.activeFocus || sourceEditor.activeFocus) thumbnails.forceActiveFocus()
        else if (markdown) revealSource(false, sourceFlick.contentY)
        else slideEditor.forceActiveFocus()
    }
    function editorKey(editor, flick, event) {
        if ((event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) && !(event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))) {
            event.accepted = true
            switchEditingFocus()
            return
        }
        if (event.matches(StandardKey.Paste) && deck.pasteMedia()) {
            event.accepted = true
            return
        }
        let control = event.modifiers & Qt.ControlModifier
        if (!win.markdown && editor === slideEditor && event.modifiers === Qt.NoModifier &&
            (event.key === Qt.Key_Home || event.key === Qt.Key_End)) {
            event.accepted = true
            deck.select(event.key === Qt.Key_Home ? 0 : deck.count - 1)
            return
        }
        if (control && event.key === Qt.Key_Z) {
            event.accepted = true
            event.modifiers & Qt.ShiftModifier ? deck.redo() : deck.undo()
            return
        }
        let position = editor.cursorPosition
        let scroll = flick.contentY
        let page = event.key === Qt.Key_PageDown || event.key === Qt.Key_PageUp
        if (page) {
            let amount = Math.max(40, flick.height - 40) * (event.key === Qt.Key_PageDown ? 1 : -1)
            let rect = editor.positionToRectangle(position)
            position = editor.positionAt(rect.x, Math.max(0, rect.y + rect.height / 2 + amount))
            scroll += amount
        } else if (event.key === Qt.Key_Home) {
            position = control || position === 0 ? 0 : editor.text.lastIndexOf("\n", position - 1) + 1
        } else if (event.key === Qt.Key_End) {
            let end = editor.text.indexOf("\n", position)
            position = control || end < 0 ? editor.length : end
        } else return
        event.accepted = true
        if (event.modifiers & Qt.ShiftModifier) editor.moveCursorSelection(position, TextEdit.SelectCharacters)
        else editor.cursorPosition = position
        if (page) flick.contentY = Math.max(0, Math.min(Math.max(0, flick.contentHeight - flick.height + flick.bottomMargin), scroll))
    }
    function formatSlide(kind) {
        const edit = Markdown.format(slideEditor.text, slideEditor.selectionStart, slideEditor.selectionEnd, kind)
        slideEditor.forceActiveFocus()
        deck.editSlide(edit.text)
        slideEditor.select(edit.start, edit.end)
    }
    function focusMarkdown() {
        if (markdown) revealSource(false, sourceFlick.contentY)
        else slideEditor.forceActiveFocus()
    }
    function openMarkdown() { setMarkdownMode(true) }
    function setMarkdownMode(value) {
        markdown = value
        if (markdown) alignSource()
        else stage.forceActiveFocus()
    }
    function addSlide() {
        let previousY = sourceFlick.contentY
        deck.addSlide()
        if (markdown) revealSource(false, previousY)
        else slideEditor.forceActiveFocus()
    }
    function alignSource(focus = true) { revealSource(true, sourceFlick.contentY, focus) }
    function revealSource(atTop, previousY, focus = true) {
        syncingEditor = true
        sourceEditor.cursorPosition = deck.sourcePosition()
        syncingEditor = false
        if (focus) sourceEditor.forceActiveFocus()
        Qt.callLater(function() {
            let rect = sourceEditor.positionToRectangle(deck.sourcePosition())
            let viewportHeight = win.contentItem.height
            let nextY = previousY
            if (atTop || rect.y < previousY + sourceEditor.topPadding)
                nextY = rect.y - sourceEditor.topPadding
            else if (rect.y + rect.height > previousY + viewportHeight - sourceEditor.bottomPadding)
                nextY = rect.y + rect.height + sourceEditor.bottomPadding - viewportHeight
            // Query the document directly: Flickable's contentHeight can still be
            // from the hidden editor until the next layout pass.
            let end = sourceEditor.positionToRectangle(sourceEditor.length)
            let maxY = Math.max(0, end.y + end.height + sourceEditor.bottomPadding - viewportHeight)
            sourceFlick.contentY = Math.max(0, Math.min(maxY, nextY))
        })
    }
    function syncEditors() {
        syncingEditor = true
        if (sourceEditor.text !== deck.source) sourceEditor.text = deck.source
        if (!editingSlide && slideEditor.text !== deck.slideText) slideEditor.text = deck.slideText
        syncingEditor = false
        if (lastSelected !== deck.selected) {
            slideEditor.cursorPosition = 0
            Qt.callLater(function() { slideScroll.contentItem.contentY = 0 })
        }
    }
    Component.onCompleted: { syncEditors(); lastSelected = deck.selected; Qt.callLater(thumbnails.revealSelection) }
    Connections {
        target: deck
        function onCompressingImageChanged() {
            if (deck.compressingImage) win.compressionReturnFocus = win.activeFocusItem
            else compressionDialog.close()
        }
        function onPasteRequested(name, extension, video) {
            pasteDialog.returnFocus = win.compressionReturnFocus || win.activeFocusItem
            win.compressionReturnFocus = null
            pasteDialog.extension = extension
            pasteDialog.isVideo = video
            pasteDialog.error = ""
            pasteName.text = name
            pasteDialog.open()
        }
        function onChanged() {
            win.syncEditors()
            if (win.dragIndex < 0 && win.lastSelected !== deck.selected) Qt.callLater(thumbnails.revealSelection)
            const nextVideo = deck.media.video ? deck.media.url.toString() : ""
            const changedVideo = win.lastSelected !== deck.selected || player.source.toString() !== nextVideo
            win.lastSelected = deck.selected
            if (changedVideo) {
                player.stop()
                video.clearOutput()
                player.source = nextVideo
                if (win.presenting && deck.media.video && deck.media.autoplay) player.play()
            }
        }
        function onModelAboutToBeReset() {
            thumbnails.cancelFlick()
            thumbnails.stopWheel()
            thumbnails.scrollBeforeReset = thumbnails.contentY - thumbnails.originY
        }
        function onModelReset() { Qt.callLater(thumbnails.revealSelection) }
    }
    onPresentingChanged: { if (presenting && deck.media.video && deck.media.autoplay) player.play() }
    onClosing: function(close) {
        if (!allowClose && !deck.flushAutosave() && deck.dirty) {
            close.accepted = false
            closeDialog.open()
        }
    }
    Dialog {
        id: closeDialog; title: "Unsaved changes"; modal: true; anchors.centerIn: parent
        standardButtons: Dialog.Discard | Dialog.Cancel
        Label { text: "Changes could not be backed up. Discard them and quit?" }
        onDiscarded: { win.allowClose = true; win.close() }
    }
    Dialog {
        id: historyDialog; objectName: "historyDialog"
        parent: Overlay.overlay; anchors.centerIn: parent
        modal: true; focus: true; title: "Version history"
        width: Math.min(460, win.width - 48); height: Math.min(480, win.height - 80)
        standardButtons: Dialog.Cancel
        property var versions: []
        onOpened: { deck.flushAutosave(); versions = deck.recoveryVersions() }
        contentItem: ColumnLayout {
            Label { text: "Choose a version to restore. Your current version stays in history."; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            ListView {
                id: versionList
                Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                model: historyDialog.versions
                ScrollBar.vertical: ScrollBar {}
                delegate: ItemDelegate {
                    required property var modelData
                    width: ListView.view.width
                    text: modelData.label
                    onClicked: if (deck.restoreVersion(modelData.name)) historyDialog.close()
                }
                Label { parent: versionList; anchors.centerIn: parent; visible: versionList.count === 0; text: "No earlier versions yet"; color: win.ui.muted }
            }
        }
    }
    Timer {
        interval: 1000; running: deck.compressingImage
        onTriggered: if (deck.compressingImage) compressionDialog.open()
    }
    Popup {
        id: compressionDialog; objectName: "compressionDialog"
        parent: Overlay.overlay
        popupType: Popup.Item
        modal: true; focus: true; padding: 24
        closePolicy: Popup.CloseOnEscape
        width: Math.min(360, parent.width - 48)
        x: Math.max(16, Math.min(parent.width - width - 16, pasteDialog.center.x - width / 2))
        y: Math.max(16, Math.min(parent.height - height - 16, pasteDialog.center.y - height / 2))
        onClosed: if (deck.compressingImage) deck.cancelPaste()
        background: Rectangle { color: win.ui.panel; border.color: win.ui.border; radius: 8 }
        Overlay.modal: Rectangle { color: "#660b0d14" }
        contentItem: ColumnLayout {
            spacing: 18
            Label { text: "Compressing image…"; color: win.ui.foreground; font.pixelSize: 20; font.bold: true }
            ProgressBar {
                Layout.fillWidth: true
                indeterminate: true
                palette.highlight: win.ui.accent
                palette.dark: win.ui.border
                Accessible.name: "Compressing image"
            }
            Button {
                Layout.alignment: Qt.AlignRight
                text: "Cancel"; onClicked: compressionDialog.close()
            }
        }
    }
    Popup {
        id: pasteDialog; objectName: "pasteDialog"
        parent: Overlay.overlay
        popupType: Popup.Item
        modal: true; focus: true; padding: 24
        closePolicy: Popup.CloseOnEscape
        width: Math.min(420, parent.width - 48)
        property string extension: "png"
        property bool isVideo: false
        property string error: ""
        property var returnFocus: null
        property Item target: stage.visible ? slideFrame : sourceScroll
        property point center: {
            // mapToItem does not subscribe to ancestor geometry changes.
            let revision = win.width + win.height + workspace.x + workspace.y +
                stage.x + stage.y + slideFrame.x + slideFrame.y + sourceScroll.x + sourceScroll.y
            return target.mapToItem(parent, target.width / 2, target.height / 2)
        }
        x: Math.max(16, Math.min(parent.width - width - 16, center.x - width / 2))
        y: Math.max(16, Math.min(parent.height - height - 16, center.y - height / 2))
        function save() {
            error = deck.savePastedMedia(pasteName.text)
            if (!error) close()
            else pasteName.forceActiveFocus()
        }
        onOpened: { pasteName.forceActiveFocus(); pasteName.selectAll() }
        onClosed: { deck.cancelPaste(); if (returnFocus) returnFocus.forceActiveFocus() }
        background: Rectangle { color: win.ui.panel; border.color: win.ui.border; radius: 8 }
        Overlay.modal: Rectangle { color: "#660b0d14" }
        contentItem: ColumnLayout {
            spacing: 18
            ColumnLayout {
                spacing: 6
                Label { text: pasteDialog.isVideo ? "Paste video" : "Paste image"; color: win.ui.foreground; font.pixelSize: 20; font.bold: true }
                Label { text: "Save to " + (pasteDialog.isVideo ? "videos/" : "images/"); color: win.ui.muted; font.pixelSize: 14 }
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 10
                TextField {
                    id: pasteName; objectName: "pasteName"
                    Accessible.name: "Filename"
                    Layout.fillWidth: true; implicitHeight: 44
                    font.pixelSize: 16; color: win.ui.foreground
                    selectionColor: win.ui.selection; selectedTextColor: win.ui.selectionText
                    leftPadding: 12; rightPadding: 12
                    background: Rectangle { color: win.ui.background; radius: 4; border.color: pasteName.activeFocus ? win.ui.accent : win.ui.border }
                    onTextEdited: pasteDialog.error = ""
                    onAccepted: if (text.trim()) pasteDialog.save()
                }
                Label { text: "." + pasteDialog.extension; color: win.ui.muted; font.pixelSize: 16 }
            }
            Label {
                Layout.fillWidth: true; visible: text.length > 0
                text: pasteDialog.error; color: win.ui.error; font.pixelSize: 13; wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 10
                Item { Layout.fillWidth: true }
                Button {
                    id: cancelPasteButton; text: "Cancel"; implicitHeight: 38; implicitWidth: 84
                    contentItem: Text { text: parent.text; color: win.ui.foreground; font.pixelSize: 14; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    background: Rectangle { color: cancelPasteButton.hovered ? win.ui.hover : win.ui.button; radius: 4; border.color: cancelPasteButton.activeFocus ? win.ui.accent : "transparent" }
                    onClicked: pasteDialog.close()
                }
                Button {
                    id: savePasteButton; text: "Save " + (pasteDialog.isVideo ? "video" : "image")
                    implicitHeight: 38; implicitWidth: 112; enabled: pasteName.text.trim().length > 0
                    contentItem: Text { text: parent.text; color: win.ui.accentText; font.pixelSize: 14; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    background: Rectangle { color: !savePasteButton.enabled ? win.ui.border : savePasteButton.hovered ? win.ui.accentHover : win.ui.accent; radius: 4; border.color: savePasteButton.activeFocus ? win.ui.foreground : "transparent" }
                    onClicked: pasteDialog.save()
                }
            }
        }
    }
    Shortcut { sequences: ["Tab", "Shift+Tab"]; enabled: !win.popupOpen && !deck.compressingImage && !win.presenting && (stage.activeFocus || thumbnails.activeFocus); onActivated: win.switchEditingFocus() }
    Shortcut { enabled: !win.popupOpen && !deck.compressingImage; sequences: [StandardKey.Open]; onActivated: deck.openDialog() }
    Shortcut { enabled: !win.popupOpen && !deck.compressingImage; sequences: [StandardKey.Save]; onActivated: deck.save() }
    Shortcut { enabled: !win.popupOpen && !deck.compressingImage; sequences: [StandardKey.SaveAs]; onActivated: deck.saveAs() }
    Shortcut { sequence: "Ctrl+E"; enabled: !win.popupOpen && !deck.compressingImage && (!win.presenting); onActivated: win.setMarkdownMode(!win.markdown) }
    Shortcut { enabled: !win.popupOpen && !deck.compressingImage; sequence: "Ctrl+N"; onActivated: deck.newDeck() }
    Shortcut { enabled: !win.popupOpen && !deck.compressingImage; sequences: ["F5", "Ctrl+Space"]; autoRepeat: false; onActivated: win.togglePresent() }
    Shortcut { sequence: "Escape"; enabled: !win.popupOpen && !deck.compressingImage && (win.presenting); onActivated: win.togglePresent() }
    Shortcut { sequence: "Ctrl+Z"; enabled: !win.popupOpen && !deck.compressingImage && (!slideEditor.activeFocus && !sourceEditor.activeFocus); onActivated: deck.undo() }
    Shortcut { sequence: "Ctrl+Shift+Z"; enabled: !win.popupOpen && !deck.compressingImage && (!slideEditor.activeFocus && !sourceEditor.activeFocus); onActivated: deck.redo() }
    Shortcut { sequence: "Ctrl+D"; enabled: !win.popupOpen && !deck.compressingImage && (!slideEditor.activeFocus && !sourceEditor.activeFocus); onActivated: deck.duplicateSlide() }
    Shortcut { enabled: !win.popupOpen && !deck.compressingImage; sequence: "Ctrl+Return"; onActivated: { win.addSlide() } }
    Shortcut { sequence: "Delete"; enabled: !win.popupOpen && !deck.compressingImage && (!slideEditor.activeFocus && !sourceEditor.activeFocus); onActivated: deck.deleteSlide() }
    Shortcut { sequences: ["Right", "Down"]; enabled: !win.popupOpen && !deck.compressingImage && (win.presenting || (!slideEditor.activeFocus && !sourceEditor.activeFocus)); onActivated: deck.select(deck.selected + 1) }
    Shortcut { sequences: ["Left", "Up"]; enabled: !win.popupOpen && !deck.compressingImage && (win.presenting || (!slideEditor.activeFocus && !sourceEditor.activeFocus)); onActivated: deck.select(deck.selected - 1) }
    Shortcut { sequences: ["Ctrl+Up", "Ctrl+Left"]; enabled: !win.popupOpen && !deck.compressingImage && !win.presenting && !slideEditor.activeFocus && !sourceEditor.activeFocus; onActivated: { deck.moveSelection(-1); if (win.markdown) win.alignSource(false) } }
    Shortcut { sequences: ["Ctrl+Down", "Ctrl+Right"]; enabled: !win.popupOpen && !deck.compressingImage && !win.presenting && !slideEditor.activeFocus && !sourceEditor.activeFocus; onActivated: { deck.moveSelection(1); if (win.markdown) win.alignSource(false) } }
    Shortcut { sequences: ["Shift+Up", "Shift+Left"]; enabled: !win.popupOpen && !deck.compressingImage && !win.presenting && !slideEditor.activeFocus && !sourceEditor.activeFocus; onActivated: { deck.extendSelection(deck.selected - 1); if (win.markdown) win.alignSource(false) } }
    Shortcut { sequences: ["Shift+Down", "Shift+Right"]; enabled: !win.popupOpen && !deck.compressingImage && !win.presenting && !slideEditor.activeFocus && !sourceEditor.activeFocus; onActivated: { deck.extendSelection(deck.selected + 1); if (win.markdown) win.alignSource(false) } }
    Shortcut { sequence: "PgDown"; enabled: !win.popupOpen && !deck.compressingImage && (win.presenting || (!slideEditor.activeFocus && !sourceEditor.activeFocus)); onActivated: deck.select(deck.selected + 5) }
    Shortcut { sequence: "PgUp"; enabled: !win.popupOpen && !deck.compressingImage && (win.presenting || (!slideEditor.activeFocus && !sourceEditor.activeFocus)); onActivated: deck.select(deck.selected - 5) }
    Shortcut { sequence: "Home"; enabled: !win.popupOpen && !deck.compressingImage && (win.presenting || (!win.markdown && !slideEditor.activeFocus)); onActivated: deck.select(0) }
    Shortcut { sequence: "End"; enabled: !win.popupOpen && !deck.compressingImage && (win.presenting || (!win.markdown && !slideEditor.activeFocus)); onActivated: deck.select(deck.count - 1) }
    Shortcut { sequence: "Space"; enabled: !win.popupOpen && !deck.compressingImage && win.presenting && (deck.media.video || animation.active); autoRepeat: false; onActivated: { if (animation.item) animation.item.paused = !animation.item.paused; else win.toggleVideo() } }
    Shortcut { sequence: "Ctrl+V"; enabled: !win.popupOpen && !deck.compressingImage && (!slideEditor.activeFocus && !sourceEditor.activeFocus); onActivated: deck.pasteMedia() }
    component ToolbarIconButton: ToolButton {
        required property string iconName
        required property string description
        Layout.preferredWidth: 40; Layout.preferredHeight: 40
        padding: 0
        Accessible.name: description
        ToolTip.visible: hovered; ToolTip.text: description
        contentItem: Item {
            AppIcon { anchors.centerIn: parent; width: 22; height: 22; name: iconName; color: win.ui.foreground; opacity: enabled ? 1 : 0.4 }
        }
        background: Rectangle { color: parent.hovered || parent.down ? win.ui.hover : win.ui.button; radius: 3 }
    }
    component EditorIconButton: ToolbarIconButton {
        Layout.preferredWidth: 32; Layout.preferredHeight: 32
        focusPolicy: Qt.NoFocus
        background: Rectangle { color: parent.hovered || parent.down ? win.ui.hover : "transparent"; radius: 3 }
    }
    header: ToolBar {
        id: topBar
        visible: !win.presenting; height: visible ? 60 : 0
        background: Rectangle { color: win.ui.panel; border.color: win.ui.border }
        Label {
            id: logo; objectName: "hypeLogo"
            anchors.left: parent.left; anchors.leftMargin: 20; anchors.verticalCenter: parent.verticalCenter
            text: "Hype"; font.pixelSize: 22; font.bold: true; color: win.ui.accent
        }
        Label {
            objectName: "deckSummary"
            anchors.centerIn: parent
            width: Math.max(0, topBar.width - 2 * (Math.max(toolbarActions.width + 18, logo.width + 20) + 20))
            text: deck.title + "  ·  " + deck.count + (deck.count === 1 ? " slide" : " slides") + "  ·  " + deck.sizeLabel
            elide: Text.ElideMiddle; horizontalAlignment: Text.AlignHCenter; color: win.ui.foreground
            ToolTip.visible: summaryHover.hovered && truncated; ToolTip.text: text
            HoverHandler { id: summaryHover }
        }
        RowLayout {
            id: toolbarActions; objectName: "toolbarActions"
            anchors.right: parent.right; anchors.rightMargin: 18; anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            ComboBox {
                id: themes; objectName: "themePicker"
                Layout.preferredWidth: 40; Layout.preferredHeight: 40
                padding: 0; indicator: null
                contentItem: Item {
                    AppIcon { anchors.centerIn: parent; width: 22; height: 22; name: "theme"; color: win.ui.foreground }
                }
                Accessible.name: "Theme: " + currentText
                ToolTip.visible: hovered; ToolTip.text: "Theme: " + currentText
                delegate: ItemDelegate {
                    required property string modelData
                    required property int index
                    width: themes.popup.availableWidth
                    text: modelData
                    highlighted: themes.highlightedIndex === index
                    contentItem: Text { text: modelData; color: win.ui.foreground; font: themes.font; verticalAlignment: Text.AlignVCenter }
                    background: Rectangle { color: parent.highlighted ? win.ui.hover : win.ui.panel }
                }
                background: Rectangle { color: themes.hovered ? win.ui.hover : win.ui.button; radius: 3 }
                model: deck.themeNames
                currentIndex: Math.max(0, deck.themeNames.indexOf(deck.themeName))
                onActivated: deck.chooseTheme(currentText)
                popup: Popup {
                    y: themes.height + 4; width: 240; padding: 6
                    height: Math.min(contentItem.implicitHeight + 12, 420, win.height - 100)
                    background: Rectangle { color: win.ui.panel; border.color: win.ui.border; radius: 3 }
                    contentItem: ListView {
                        clip: true; implicitHeight: contentHeight
                        model: themes.popup.visible ? themes.delegateModel : null
                        currentIndex: themes.highlightedIndex
                        ScrollBar.vertical: ScrollBar {}
                    }
                    onOpened: contentItem.positionViewAtIndex(themes.currentIndex, ListView.Contain)
                }
            }
            ComboBox {
                id: fonts; objectName: "fontPicker"
                property bool showNotoVariants: false
                readonly property var visibleFonts: deck.fontNames.filter(function(name) {
                    return showNotoVariants || !name.startsWith("Noto ") ||
                        ["Noto Sans", "Noto Serif", "Noto Sans Mono", deck.fontName].indexOf(name) >= 0
                })
                model: visibleFonts.concat([showNotoVariants ? "Fewer Noto fonts" : "More Noto fonts…"])
                currentIndex: visibleFonts.indexOf(deck.fontName)
                displayText: deck.fontName
                onActivated: function(index) {
                    if (index === visibleFonts.length) {
                        showNotoVariants = !showNotoVariants
                        currentIndex = Qt.binding(function() { return fonts.visibleFonts.indexOf(deck.fontName) })
                        Qt.callLater(function() { fonts.popup.open() })
                    } else deck.chooseFont(visibleFonts[index])
                }
                Layout.preferredWidth: 40; Layout.preferredHeight: 40
                padding: 0; indicator: null
                contentItem: Item {
                    AppIcon { anchors.centerIn: parent; width: 22; height: 22; name: "font"; color: win.ui.foreground }
                }
                Accessible.name: "Font: " + deck.fontName
                delegate: ItemDelegate {
                    required property string modelData
                    required property int index
                    width: fonts.popup.availableWidth; text: modelData
                    highlighted: fonts.highlightedIndex === index
                    contentItem: Text { text: modelData; color: win.ui.foreground; font: fonts.font; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter }
                    background: Rectangle { color: parent.highlighted ? win.ui.hover : win.ui.panel }
                }
                background: Rectangle { color: fonts.hovered ? win.ui.hover : win.ui.button; radius: 3 }
                popup: Popup {
                    y: fonts.height + 4; width: 320; padding: 6
                    height: Math.min(contentItem.implicitHeight + 12, 420, win.height - 100)
                    background: Rectangle { color: win.ui.panel; border.color: win.ui.border; radius: 3 }
                    contentItem: ListView {
                        clip: true; implicitHeight: contentHeight
                        model: fonts.popup.visible ? fonts.delegateModel : null
                        currentIndex: fonts.highlightedIndex
                        ScrollBar.vertical: ScrollBar {}
                    }
                    onOpened: contentItem.positionViewAtIndex(fonts.currentIndex, ListView.Contain)
                }
                ToolTip.visible: hovered; ToolTip.text: "Font: " + deck.fontName
            }
            ToolbarIconButton {
                objectName: "modeButton"
                iconName: win.markdown ? "markdown" : "visual"
                description: (win.markdown ? "Markdown mode · Switch to Visual" : "Visual mode · Switch to Markdown") + " (Ctrl+E)"
                onClicked: win.setMarkdownMode(!win.markdown)
            }
            ToolbarIconButton {
                objectName: "presentButton"; iconName: "present"; description: "Present (Ctrl+Space)"
                onClicked: win.togglePresent()
            }
            ToolbarIconButton {
                objectName: "exportButton"; iconName: "export"; description: "Export"
                enabled: !deck.exporting
                onClicked: exportMenu.open()
                Menu {
                    id: exportMenu
                    y: parent.height + 4
                    MenuItem { text: "PDF"; onTriggered: deck.exportDialog("pdf") }
                    MenuItem { text: "PowerPoint"; onTriggered: deck.exportDialog("pptx") }
                }
            }
        }
    }
    footer: ToolBar {
        visible: !win.presenting; height: visible ? 34 : 0
        background: Rectangle { color: win.ui.panel }
        RowLayout { anchors.fill: parent; anchors.leftMargin: 20; anchors.rightMargin: 20
            Label { text: deck.selectionCount > 1 ? deck.selectionCount + " slides selected" : "Slide " + (deck.selected+1) + " of " + deck.count; font.pixelSize: 12 }
            Label {
                text: deck.exportStatus || deck.status
                elide: Text.ElideRight; Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter
                font.pixelSize: 12; color: deck.exportFailed ? win.ui.error : win.ui.muted
                ToolTip.visible: statusHover.hovered; ToolTip.text: text
                HoverHandler { id: statusHover }
            }
            ProgressBar {
                visible: deck.exporting; Layout.preferredWidth: 140
                value: deck.exportProgress; palette.highlight: win.ui.accent
                Accessible.name: "Export progress"
            }
            Label { visible: deck.exporting; text: Math.floor(deck.exportProgress * 100) + "%"; font.pixelSize: 12; color: win.ui.muted }
            ToolButton { visible: deck.exporting; text: "Cancel"; onClicked: deck.cancelExport() }
            ToolButton { text: "History"; onClicked: historyDialog.open(); ToolTip.visible: hovered; ToolTip.text: "Restore an earlier version" }
            ToolButton {
                objectName: "saveButton"; Layout.preferredWidth: 28; Layout.preferredHeight: 28
                Accessible.name: deck.dirty ? "Save unsaved changes" : "Save presentation"
                contentItem: Item { AppIcon { anchors.centerIn: parent; width: 16; height: 16; name: "save"; color: deck.dirty ? win.ui.accent : win.ui.muted } }
                background: Rectangle { color: parent.hovered ? win.ui.hover : "transparent"; radius: 3 }
                ToolTip.visible: hovered; ToolTip.text: (deck.dirty ? "Save changes" : "Saved") + " (Ctrl+S)"
                onClicked: deck.save()
            }
            ToolButton {
                objectName: "openButton"; Layout.preferredWidth: 28; Layout.preferredHeight: 28
                Accessible.name: "Open presentation"
                contentItem: Item { AppIcon { anchors.centerIn: parent; width: 16; height: 16; name: "open"; color: win.ui.muted } }
                background: Rectangle { color: parent.hovered ? win.ui.hover : "transparent"; radius: 3 }
                ToolTip.visible: hovered; ToolTip.text: "Open (Ctrl+O)"
                onClicked: deck.openDialog()
            }
        }
    }
    RowLayout {
        anchors.fill: parent; spacing: 0
        Rectangle {
            visible: !win.presenting; Layout.preferredWidth: 235; Layout.fillHeight: true; color: win.ui.panel
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 12; spacing: 10
                ListView {
                    id: thumbnails; objectName: "thumbnails"; Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                    model: deck; spacing: 10; currentIndex: deck.selected
                    cacheBuffer: height * 2
                    highlightFollowsCurrentItem: false
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AlwaysOff }
                    property int wheelDirection: 0
                    property real wheelRemainder: 0
                    readonly property real thumbnailHeight: 108
                    readonly property real slideStep: thumbnailHeight + spacing
                    property real scrollBeforeReset: -1
                    function revealSelection() {
                        forceLayout()
                        if (scrollBeforeReset >= 0) {
                            const maximum = Math.max(0, count * slideStep - spacing - height)
                            contentY = originY + Math.min(scrollBeforeReset, maximum)
                            scrollBeforeReset = -1
                        }
                        positionViewAtIndex(deck.selected, ListView.Contain)
                    }
                    function stopWheel() {
                        wheelDirection = 0
                        wheelRemainder = 0
                    }
                    function scrollWheel(event) {
                        if (slideDrag.pressed) { event.accepted = true; return }
                        let pixels = event.pixelDelta.y
                        let delta = event.angleDelta.y || pixels
                        if (!delta) { event.accepted = false; return }
                        cancelFlick()
                        let direction = Math.sign(delta)
                        if (direction !== wheelDirection) wheelRemainder = 0
                        wheelDirection = direction
                        // Accumulate high-resolution input, but always move whole slides.
                        // Prefer wheel angles even when Qt also supplies a pixel delta.
                        wheelRemainder += event.angleDelta.y ? event.angleDelta.y / 120 : pixels / slideStep
                        let steps = Math.trunc(wheelRemainder)
                        if (!steps) { event.accepted = true; return }
                        wheelRemainder -= steps
                        thumbnails.forceActiveFocus()
                        deck.select(deck.selected - steps)
                        if (win.markdown) win.alignSource(false)
                        event.accepted = true
                    }
                    WheelHandler {
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                        target: null
                        onWheel: function(event) { thumbnails.scrollWheel(event) }
                    }
                    function updateDrop(x, y) {
                        win.dragX = x
                        win.dragY = y
                        if (x < 0 || x > width) {
                            win.dropIndex = -1
                            win.dragScroll = 0
                            return
                        }
                        win.dragScroll = y < 36 ? -1 : (y > height - 36 ? 1 : 0)
                        // Drop between slides, using each thumbnail's midpoint.
                        win.dropIndex = Math.max(0, Math.min(deck.count,
                            Math.floor((y + contentY - originY + slideStep / 2) / slideStep)))
                    }
                    function cancelDrag() {
                        win.dragIndex = -1
                        win.dropIndex = -1
                        win.dragScroll = 0
                    }
                    onMovementStarted: stopWheel()
                    delegate: Item {
                        id: thumbnail; required property int index; required property int number
                        width: thumbnails.width; height: thumbnails.thumbnailHeight
                        property bool selected: index >= deck.selectionFirst && index <= deck.selectionLast
                        opacity: win.dragIndex >= 0 && selected ? 0.4 : 1
                        Rectangle {
                            x: 22; width: parent.width - 24; height: parent.height; color: deck.background
                            border.width: deck.selected === thumbnail.index ? 3 : thumbnail.selected ? 2 : 1
                            border.color: thumbnail.selected ? win.ui.accent : win.ui.border; radius: 3
                            Image { anchors.fill: parent; anchors.margins: 3; source: "image://slides/" + (deck.revision, deck.renderId(thumbnail.index)); asynchronous: true; retainWhileLoading: true; cache: true; sourceSize.width: 340; sourceSize.height: 192; fillMode: Image.PreserveAspectFit }
                        }
                        Label { text: thumbnail.number; width: 18; y: 4; color: thumbnail.selected ? win.ui.accent : win.ui.muted; font.pixelSize: 11 }
                    }
                    Rectangle {
                        parent: thumbnails; z: 2
                        visible: win.dragIndex >= 0 && win.dropIndex >= 0
                        x: 22; width: thumbnails.width - 24; height: 3
                        y: Math.max(0, Math.min(thumbnails.height - height,
                            thumbnails.originY + win.dropIndex * thumbnails.slideStep - thumbnails.contentY - thumbnails.spacing / 2))
                        color: win.ui.accent
                    }
                    MouseArea {
                        id: slideDrag; parent: thumbnails; anchors.fill: parent; z: 1
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        preventStealing: true
                        cursorShape: win.dragIndex >= 0 ? Qt.ClosedHandCursor : Qt.ArrowCursor
                        property point pressPosition
                        property int pressedIndex: -1
                        property bool moved: false
                        property bool extending: false
                        onPressed: function(mouse) {
                            thumbnails.cancelFlick()
                            thumbnails.stopWheel()
                            pressPosition = Qt.point(mouse.x, mouse.y)
                            pressedIndex = thumbnails.indexAt(mouse.x, mouse.y + thumbnails.contentY)
                            moved = false
                            if (pressedIndex < 0) return
                            extending = !!(mouse.modifiers & Qt.ShiftModifier)
                            if (extending) deck.extendSelection(pressedIndex)
                            else if (pressedIndex < deck.selectionFirst || pressedIndex > deck.selectionLast) deck.select(pressedIndex)
                            thumbnails.forceActiveFocus()
                            if (mouse.button === Qt.RightButton) slideMenu.popup()
                        }
                        onPositionChanged: function(mouse) {
                            if (!(pressedButtons & Qt.LeftButton) || pressedIndex < 0) return
                            if (win.dragIndex < 0 && Math.hypot(mouse.x - pressPosition.x, mouse.y - pressPosition.y) > 8) {
                                win.dragIndex = pressedIndex
                                moved = true
                            }
                            if (win.dragIndex >= 0) thumbnails.updateDrop(mouse.x, mouse.y)
                        }
                        onReleased: function(mouse) {
                            if (win.dragIndex >= 0) {
                                thumbnails.updateDrop(mouse.x, mouse.y)
                                let slot = win.dropIndex
                                thumbnails.cancelDrag()
                                if (slot >= 0) deck.dropSelection(slot)
                                thumbnails.positionViewAtIndex(deck.selected, ListView.Contain)
                            }
                            if (!moved && !extending && mouse.button === Qt.LeftButton && pressedIndex >= 0) deck.select(pressedIndex)
                            if (win.markdown && pressedIndex >= 0) win.alignSource(false)
                            pressedIndex = -1
                        }
                        onCanceled: { thumbnails.cancelDrag(); pressedIndex = -1 }
                        onDoubleClicked: if (!moved) win.focusMarkdown()
                    }
                    Timer {
                        interval: 40; repeat: true; running: win.dragIndex >= 0 && win.dragScroll !== 0
                        onTriggered: {
                            thumbnails.contentY = Math.max(thumbnails.originY, Math.min(
                                thumbnails.originY + thumbnails.contentHeight - thumbnails.height,
                                thumbnails.contentY + win.dragScroll * 18))
                            thumbnails.updateDrop(win.dragX, win.dragY)
                        }
                    }
                    Menu { id: slideMenu
                        MenuItem { text: "New slide after this"; onTriggered: { win.addSlide() } }
                        MenuItem { text: deck.selectionCount > 1 ? "Duplicate slides" : "Duplicate slide"; onTriggered: deck.duplicateSlide() }
                        MenuItem { text: deck.selectionCount > 1 ? "Delete slides" : "Delete slide"; onTriggered: deck.deleteSlide() }
                    }
                }
            }
        }
        SplitView {
            id: workspace; orientation: Qt.Vertical
            visible: !win.markdown || win.presenting
            Layout.fillWidth: true; Layout.fillHeight: true
            handle: Rectangle {
                implicitHeight: win.presenting ? 0 : 6
                color: SplitHandle.hovered || SplitHandle.pressed ? win.ui.accent : win.ui.border
            }
            Item {
                id: stage; objectName: "stage"; SplitView.fillHeight: true; SplitView.minimumHeight: 160; focus: true
                property real margin: win.presenting ? 0 : 32
                property real slideWidth: Math.min(width-margin*2,(height-margin*2)*16/9)
                Item {
                    id: slideFrame; objectName: "slideFrame"; width: stage.slideWidth; height: width*9/16; anchors.centerIn: parent
                    Image { objectName: "slidePreview"; anchors.fill: parent; source: "image://slides/" + (deck.revision, deck.renderId(deck.selected)) + (animation.active ? "/background" : ""); asynchronous: true; retainWhileLoading: true; cache: true; sourceSize: Qt.size(1920, 1080) }
                    Loader {
                        id: animation; objectName: "animationLoader"
                        active: workspace.visible && !!deck.media.animated
                        x: deck.media.rect.x * slideFrame.width / 1920
                        y: deck.media.rect.y * slideFrame.height / 1080
                        width: deck.media.rect.width * slideFrame.width / 1920
                        height: deck.media.rect.height * slideFrame.height / 1080
                        sourceComponent: AnimatedImage {
                            objectName: "animatedMedia"
                            source: deck.media.url
                            asynchronous: true
                            cache: false // Decode the current frame without retaining an entire animation.
                            playing: true
                            property bool autoplay: deck.media.autoplay
                            paused: !autoplay
                            onAutoplayChanged: paused = !autoplay
                            fillMode: deck.media.span ? Image.PreserveAspectCrop : Image.PreserveAspectFit
                            clip: true
                            layer.enabled: !!deck.media.title
                            layer.effect: MultiEffect {
                                blurEnabled: true
                                blurMax: 4
                                blur: Math.min(1, 0.5 * slideFrame.width / 1920)
                                autoPaddingEnabled: false
                            }
                            onSourceChanged: paused = !autoplay
                            onStatusChanged: if (status === Image.Ready) paused = !autoplay
                        }
                    }
                    VideoOutput {
                        id: video; objectName: "videoOutput"
                        visible: deck.media.video && (player.playbackState !== MediaPlayer.StoppedState || player.mediaStatus === MediaPlayer.EndOfMedia)
                        endOfStreamPolicy: VideoOutput.KeepLastFrame
                        x: deck.media.rect.x * slideFrame.width / 1920
                        y: deck.media.rect.y * slideFrame.height / 1080
                        width: deck.media.rect.width * slideFrame.width / 1920
                        height: deck.media.rect.height * slideFrame.height / 1080
                        fillMode: deck.media.span ? VideoOutput.PreserveAspectCrop : VideoOutput.PreserveAspectFit
                    }
                    Image { anchors.fill: parent; source: visible ? "image://slides/" + (deck.revision, deck.renderId(deck.selected)) + "/overlay" : ""; asynchronous: true; retainWhileLoading: true; sourceSize: Qt.size(1920,1080); visible: animation.active || (video.visible && deck.media.span) }
                    Button { visible: deck.media.video && !win.presenting; anchors.centerIn: parent; text: player.playbackState === MediaPlayer.PlayingState ? "Pause" : "▶ Play"; onClicked: win.toggleVideo() }
                }
                DropArea { anchors.fill: parent; onDropped: function(drop) { if (drop.hasUrls) for (let i = 0; i < drop.urls.length; ++i) { if (!deck.importMedia(drop.urls[i], i > 0)) break } } }
            }
            ColumnLayout {
                id: editorPane; objectName: "editorPane"
                visible: !win.presenting; spacing: 0
                SplitView.preferredHeight: 250; SplitView.minimumHeight: 140
                SplitView.maximumHeight: workspace.height * 0.65
            ToolBar {
                Layout.fillWidth: true; Layout.preferredHeight: 42
                background: Rectangle { color: win.ui.panel }
                RowLayout {
                    anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16; spacing: 4
                    EditorIconButton { objectName: "boldButton"; iconName: "bold"; description: "Bold"; onClicked: win.formatSlide("bold") }
                    EditorIconButton { objectName: "italicButton"; iconName: "italic"; description: "Italic"; onClicked: win.formatSlide("italic") }
                    EditorIconButton { objectName: "headlineButton"; iconName: "headline"; description: "Headline"; onClicked: win.formatSlide("headline") }
                    EditorIconButton { objectName: "codeButton"; iconName: "markdown"; description: "Code block"; onClicked: win.formatSlide("code") }
                    EditorIconButton { objectName: "commentButton"; iconName: "comment"; description: "Comment (hidden on slide)"; onClicked: win.formatSlide("comment") }
                    Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 20; Layout.leftMargin: 8; Layout.rightMargin: 8; color: win.ui.border }
                    EditorIconButton { iconName: "media-add"; description: "Add image / video"; onClicked: deck.importDialog() }
                    EditorIconButton {
                        objectName: "mediaOptionsButton"; iconName: "adjust"; description: "Image / video options"
                        Layout.preferredWidth: 44
                        enabled: !!deck.media.url.toString()
                        contentItem: Item {
                            opacity: enabled ? 1 : 0.4
                            AppIcon { anchors.left: parent.left; anchors.leftMargin: 5; anchors.verticalCenter: parent.verticalCenter; width: 22; height: 22; name: "adjust"; color: win.ui.foreground }
                            AppIcon { anchors.right: parent.right; anchors.rightMargin: 2; anchors.verticalCenter: parent.verticalCenter; width: 12; height: 12; name: "chevron-down"; color: win.ui.foreground }
                        }
                        onClicked: mediaMenu.open()
                        Menu {
                            id: mediaMenu; objectName: "mediaMenu"; y: parent.height + 4
                            MenuItem { text: "Fit"; checkable: true; checked: !deck.media.span && !deck.media.side; onTriggered: deck.setMediaMode("fit") }
                            MenuItem { text: "Span"; checkable: true; checked: deck.media.span; onTriggered: deck.setMediaMode("span") }
                            MenuItem { text: "Left"; enabled: !deck.media.video; checkable: true; checked: deck.media.side === "left"; onTriggered: deck.setMediaMode("left") }
                            MenuItem { text: "Right"; enabled: !deck.media.video; checkable: true; checked: deck.media.side === "right"; onTriggered: deck.setMediaMode("right") }
                            MenuSeparator {}
                            MenuItem { text: "Match image edges"; checkable: true; checked: deck.media.background === "auto"; onTriggered: deck.matchImageBackground(true) }
                            MenuItem { text: deck.media.video ? "Blurred first frame" : "Blurred image"; checkable: true; checked: deck.media.background === "blur"; onTriggered: deck.setMediaBackground("blur") }
                            MenuItem { text: "Use theme color"; checkable: true; checked: deck.media.background === "theme"; onTriggered: deck.matchImageBackground(false) }
                        }
                    }
                    Item { Layout.fillWidth: true }
                }
            }
            ScrollView {
                id: slideScroll; objectName: "slideScroll"
                Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                WheelHandler {
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    target: null
                    onWheel: function(event) { win.scrollEditor(slideScroll.contentItem, event) }
                }
                TextArea {
                    id: slideEditor; objectName: "slideEditor"; persistentSelection: true; textFormat: TextEdit.PlainText; color: win.ui.foreground; selectionColor: win.ui.selection; selectedTextColor: win.ui.selectionText; font.family: "JetBrains Mono"; font.pixelSize: 16
                    wrapMode: TextEdit.Wrap; leftPadding: 24; topPadding: 16; placeholderText: "# Your headline"
                    onTextChanged: {
                        if (!win.syncingEditor && activeFocus) {
                            const previousCount = deck.count
                            win.editingSlide = true
                            deck.editSlide(text)
                            win.editingSlide = false
                            if (deck.count !== previousCount) win.syncEditors()
                        }
                    }
                    Keys.onPressed: function(event) { win.editorKey(slideEditor, slideScroll.contentItem, event) }
                }
            }
            }
        }
        ScrollView {
            id: sourceScroll; objectName: "sourceScroll"
            visible: win.markdown && !win.presenting
            Layout.fillWidth: true; Layout.fillHeight: true; clip: true
            Flickable {
                id: sourceFlick; objectName: "sourceFlick"
                clip: true; boundsBehavior: Flickable.StopAtBounds
                WheelHandler {
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    target: null
                    onWheel: function(event) { win.scrollEditor(sourceFlick, event) }
                }
                TextArea.flickable: TextArea {
                id: sourceEditor; objectName: "sourceEditor"
                textFormat: TextEdit.PlainText
                color: win.ui.foreground; selectionColor: win.ui.selection; selectedTextColor: win.ui.selectionText
                font.family: "JetBrains Mono"; font.pixelSize: 18
                wrapMode: TextEdit.NoWrap; leftPadding: 32; topPadding: 30; bottomPadding: 30
                onTextChanged: { if (!win.syncingEditor && activeFocus && text !== deck.source) deck.editSource(text) }
                onCursorPositionChanged: { if (!win.syncingEditor && activeFocus) deck.selectAt(cursorPosition) }
                Keys.onPressed: function(event) { win.editorKey(sourceEditor, sourceFlick, event) }
            }
            }
        }
    }
    MediaPlayer { id: player; objectName: "player"; videoOutput: video; audioOutput: AudioOutput { muted: deck.media.muted } loops: deck.media.loop ? MediaPlayer.Infinite : 1; onErrorOccurred: function(error,message) { deck.setStatus(message) } }
}
