# Hype

A small native Markdown presentation editor for Omarchy. The first working build uses Qt Quick and C++17, with the same qmake/build-script approach as Omacut.

```sh
./bin/build
./build/hype examples/welcome.md
```

The visual editor has slides on the left and a preview on the right. Drag thumbnails to reorder their Markdown blocks. In Visual mode, the selected slide’s Markdown is always visible below the preview; drag the divider to resize it. Add, duplicate, delete, undo, and edit without switching modes. Switch to Markdown for the full document; the selected slide’s source opens at the top. Images and videos are copied beside the document into `images/` and `videos/`.

```markdown
# A big idea

---

![span](city.jpg)

# A headline over a background

---

> A quote worth sharing.

— Its author

---

![loop muted](demo.mp4)
```

A filename resolves in `images/` or `videos/` according to its extension. Empty brackets choose the defaults. `fit` preserves the complete image, `span` fills the slide, and `left`/`right` places an image beside text. A heading with an image makes it a background unless `fit` is explicit. Videos play once on entering the slide in presentation mode; `autoplay=false` waits for Space.

Choose an installed Omarchy theme and presentation font from the toolbar. The font applies to slide text; code stays monospaced. Hype saves its colors in the Markdown front matter so the presentation keeps its palette when moved. Photos, logos, and screenshot pixels retain their original colors. Text, slide backgrounds, emphasis, quotes, and tables use the selected palette.

## Trial presentations

The private local `trials/` directory is excluded from Git. It contains Rails World 2023, 2024, and 2025 converted from `~/Dropbox/Documents/Presentations/`, with a slide-by-slide import report. See [the trial findings](TRIALS.md).

```sh
./build/hype trials/rails-world-2023/presentation.md
./build/hype trials/rails-world-2024/presentation.md
./build/hype trials/rails-world-2025/presentation.md
```

## Export

PDF uses the same painter as the canvas. PowerPoint packages rendered slides and embeds video. Install its helper dependencies locally:

```sh
./bin/setup-export
```

For headless rendering, use `QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME=generic QT_STYLE_OVERRIDE=Fusion` if your desktop platform plugin is unavailable.

```sh
./build/hype presentation.md --pdf talk.pdf
./build/hype presentation.md --pptx talk.pptx
./build/hype presentation.md --render rendered-slides
./build/hype presentation.md --theme nord --save
```

PowerPoint video requires H.264 video and optional AAC audio in MP4. For now, spanning video must be 16:9; use `fit` for other ratios. Automatic playback/loop metadata is written, but actual Microsoft PowerPoint playback still needs verification on a machine with PowerPoint. LibreOffice compatibility is checked locally.

Contained images automatically extend their dominant edge color into the surrounding slide background. No directive is needed. Override it with **Background → Use theme color**, or an explicit color:

```markdown
![fit](diagram.png)
![fit background=theme](photo.jpg)
![left background=#ffffff](illustration.png)
```

Automatic matching uses the dominant opaque edge color. Transparent or varied edges fall back to the theme. Text switches to dark or white for readability; the image itself stays unchanged. Choose **Match image edges** to restore automatic matching. This applies to the canvas and rendered exports.

Plain line breaks are preserved on slides: put each point or city on its own line without adding backslashes or trailing spaces.

## Keys

- Ctrl+E: toggle Visual / full-document Markdown.
- Sidebar wheel: select the next/previous slide and update the preview.
- Markdown: Page Up/Down moves a page; Home/End moves to the line boundaries; Ctrl+Home/End moves to the document boundaries. Shift extends the selection.
- Ctrl+O: open. Ctrl+S: save. Ctrl+Shift+S: save as.
- Ctrl+Enter: new slide. Ctrl+D: duplicate with the slide list/canvas focused.
- Delete: remove the selected slide with the slide list/canvas focused.
- Ctrl+Z / Ctrl+Shift+Z: document undo/redo.
- Ctrl+V: paste an image with the canvas focused.
- Left/Right or Page Up/Down: previous/next slide in the visual editor or presentation. Home/End: first/last slide. Text fields keep their text-navigation behavior.
- F5: present. Space: play/pause video. Escape: leave presentation.

## Development

Build dependencies: a C++17 compiler, make, `qt6-base`, `qt6-declarative`, `qt6-multimedia`. Runtime image support uses `qt6-svg`; video probing/posters use ffmpeg; code highlighting uses `source-highlight`. Tests additionally use Qt PDF. PowerPoint export uses Python and python-pptx (the local setup script pins 1.0.2). The trial conversion tools additionally use ImageMagick, PyYAML, and keynote-parser for recovering the 2023 archive; those are not app dependencies.

```sh
./bin/test
HYPE_GUI_TESTS=1 ./bin/test visualOperations
build/python/bin/python tests/test_export.py
```

The GUI test needs access to local multimedia services. It performs actual mouse dragging and clicks New Slide and Duplicate, then checks the source and undo. The other tests run offscreen without a desktop connection.

`./bin/install` builds the Arch package. `python-pptx` currently needs an AUR or Omarchy package; the local virtual environment is enough to run from this checkout. No package has been installed system-wide.

## First-build boundaries

This is a working prototype. Text editing happens in the per-slide Markdown pane below the preview. Code is fitted and monospaced; language-tagged fences have theme-colored syntax highlighting. There is one media item per slide; existing collages can be imported as images. Interactive slides and thumbnails render asynchronously with a bounded image cache. Export remains synchronous, so export progress/cancellation remains to do. There is no general Keynote/PowerPoint import command in the app: the audited migration scripts are development tools. Installed themes, external-file conflict protection, save-as media copying, and source-based slide operations work, but this has not yet been used for a live talk.

The full product plan is in [plans/hype.md](plans/hype.md).

Use a language name on fenced code blocks, for example `ruby`, `javascript`, `bash`, or `json`. Unlabelled and unsupported languages remain plain. Highlight colors follow the selected theme in previews, PDF, and PowerPoint.
