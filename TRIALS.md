# Rails World trials

The first build represents all 317 slides from the three latest local Rails World decks. They are working Markdown trial decks, not exact visual reproductions of every original composition. All three use the installed Tokyo Night palette, saved into their front matter. The original files were not changed.

| Deck | Slides | Slides with native, themeable text/code | Artwork-only slides | Videos | Blank |
| --- | ---: | ---: | ---: | ---: | ---: |
| Rails World 2023 | 89 | 44 | 43 | 2 | 0 |
| Rails World 2024 | 106 | 43 | 62 | 0 | 1 |
| Rails World 2025 | 122 | 64 | 58 | 0 | 0 |

Native text includes mixed slides with an image and separate text. Artwork-only includes screenshots with text baked into their pixels. Seven slides contain combined image layers: their artwork is flattened into a local PNG, while any native text stays in Markdown. Photos, logos, memes, and screenshot colors are preserved. Slide backgrounds and native text follow the theme.

## Open and inspect

```sh
./build/hype trials/rails-world-2023/presentation.md
./build/hype trials/rails-world-2024/presentation.md
./build/hype trials/rails-world-2025/presentation.md
```

Each directory contains `presentation.md`, `images/`, `videos/`, `import-report.json`, rendered PNGs, a PDF, and a rendered PowerPoint. The private source material and derived artifacts are excluded from Git under `trials/`.

[Working editor screenshot](trials/previews/editor.png) · [Representative slides from all three years](trials/previews/three-years.jpg)

## What the trials established

- Large headlines, line stacks, subtitles, lists, quotes, and comparison tables can be native Markdown, with new colors and fonts.
- Rails World 2025's Bootstrap Budget became a native Markdown table. The Alan Kay and HTTP quotes use quote/attribution layout. The `ProcessImportJob` slide uses actual Ruby from the companion `code.rb` instead of a screenshot.
- Rails World 2024's three server-phobia slides needed artwork beside text. The resulting `![left](image.png)` / `![right](image.png)` directives keep this simple layout native; no arbitrary coordinates are stored.
- Full-slide artwork and contained screenshots remain local images. A theme cannot recolor their baked-in text without recreating the artwork.
- Slide operations edit the actual source. A GUI test dragged a slide with the mouse, clicked Duplicate and New Slide, and verified document order and undo.

## Recovery details

The 2023 archive lives at `2023/Rails World/Rails World 2024.key`, despite the filename. It contains the Rails Foundation, Rails 7.1-era material, and assets dated 2023. LibreOffice converted its text/images but omitted two videos and an SVG. Reading the original Keynote structure with [keynote-parser](https://github.com/psobot/keynote-parser) recovered the exact slide associations:

- Slide 35: original SVG artwork.
- Slide 49: `refresh-comparison.mp4`.
- Slide 51: `hgnJfPFUbGlucFegEEtl.mp4`.

The 2024 PDF's slide 90 is black/blank. Its Hype equivalent intentionally uses the theme background. The separate `RW Demo Final.mp4` is copied into `videos/` and exposed as `trials/rails-world-2024/video-demo.md`; the archived PPTX does not establish which slide owns it, so it has not been inserted into the numbered deck by guesswork.

## Validation and remaining limits

The Qt tests cover fenced separators, Unicode, empty slides, source-preserving moves/duplicates, undo, save/reopen, external-file protection, media path/directive parsing, import filename collisions, palette snapshots, rendering, PDF page counts, and protecting existing output on export failure. The GUI test additionally plays the recovered 2023 video and confirms it stops when leaving the slide.

All three complete decks render without missing-media or undersized-text diagnostics. Their PDFs and PowerPoint files contain 89, 106, and 122 slides respectively. The 2023 PowerPoint includes both MP4s inside the package, with no external video link. A focused export test verifies the movie relationship, loop/mute/autoplay metadata, and that failed exports preserve an existing destination.

LibreOffice opened all three final PowerPoint files and exported them back to PDFs with matching 89/106/122 page counts. Actual Microsoft PowerPoint video playback still requires checking on PowerPoint; package validation alone does not prove player behavior. Source-to-Hype comparisons were sampled rather than pixel-reviewed for every slide. Review the per-slide import reports before using a trial for a live talk: original animation, precise text placement, and some decorative shapes are not reproduced. Multiple-image composites also need crop/group-transform review.

## Reproduce the conversion

```sh
./bin/build
python -m venv build/python
build/python/bin/python -m pip install -r tools/requirements-trials.txt
./bin/prepare-trials
```

`prepare-trials` requires LibreOffice and ImageMagick, reads the original Dropbox archive, and refuses to overwrite existing trial Markdown. Conversion and refinement scripts are development fixtures, not a promised general-purpose importer.
