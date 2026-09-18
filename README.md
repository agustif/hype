# Hype

Simple presentations, written in Markdown. Big headlines, images, video, and code—with a visual editor to put everything in order.

Hype is a native app for Omarchy. Your presentation is a Markdown file with its media alongside it. Choose an installed Omarchy theme, pick a font, and export to PDF or PowerPoint.

## Install

Install Hype from the [Omarchy Package Repository (OPR)](https://github.com/omacom/omarchy-pkgs):

```sh
sudo pacman -S hype
```

Then open **Hype** from the app launcher, or run `hype` in a terminal.

## Make a presentation

Open Hype from your app launcher. It reopens your last presentation; use **Ctrl+N** to start a new one, or **Open** to choose a Markdown file.

In **Visual** mode, select a slide in the sidebar and write its Markdown below the preview. Changes appear as you type. Drag the divider to give the preview or editor more room. Switch to **Markdown** with **Ctrl+E** to edit the whole presentation.

Use **+ New slide** to add a slide after the selection. Drag slides to rearrange them—their Markdown moves with them. Hold a dragged slide near the sidebar’s top or bottom to scroll further. Select several slides with **Shift+click** or **Shift+arrows** to move, duplicate, or delete them together.

Save with **Ctrl+S**. Hype remembers the last directory you opened or saved to.

## Write your slides

Separate slides with `---`, with a blank line on either side:

````markdown
# A big idea

---

# Keep it simple

- Write in Markdown
- Put your slides in order
- Tell your story

---

> Make something wonderful.

— Your closing thought

---

# Show the code

```ruby
class Presentation
  def next_slide
    slides.next
  end
end
```
````

Headlines are big by default. Quotes, lists, tables, and inline `code` work too. Ordinary line breaks stay visible on the slide. Code blocks fit the slide and use syntax highlighting when you specify a language, such as `ruby`, `javascript`, `bash`, or `json`.

The single-slide editor hides the blank lines around slide separators, leaving just your content to edit.

## Add images and video

Paste an image or a copied image/video file with **Ctrl+V**. Hype asks for a name, saves the file, and adds it to the selected slide. Pasting onto a slide that already has media replaces that media while keeping the text. You can also drag a file onto the preview or use **+ Image / video**.

Media lives beside the Markdown file:

```text
my-talk/
  presentation.md
  images/
    city.jpg
    diagram.png
  videos/
    demo.mp4
```

Use just the filename; Hype finds the right directory:

```markdown
![](diagram.png)

---

![](city.jpg)

# A headline over a background

---

![](demo.mp4)
```

A lone image fits without cropping. An image with a headline becomes a background. Videos fit the slide and play once when you reach them during a presentation.

Use the buttons below the preview or put layout options inside the brackets:

| Markdown | Result |
| --- | --- |
| `![fit](photo.jpg)` | Show the whole image, with any headline above it |
| `![span](photo.jpg)` | Fill the slide, cropping as needed |
| `![left](photo.jpg)` | Put the image beside text on the left |
| `![right](photo.jpg)` | Put the image beside text on the right |
| `![loop muted](demo.mp4)` | Loop a video without sound |
| `![autoplay=false](demo.mp4)` | Wait for Space to play the video |

For images that leave space around them, Hype matches the background to the image’s edge color when possible. Choose **Background → Use theme color** to override it, or specify a color: `![fit background=#ffffff](diagram.png)`.

Each slide supports one image or video. Copy the whole presentation folder when sharing or moving it.

## Choose your look

The toolbar lets you choose an installed Omarchy theme and a presentation font. Theme colors apply to text, code, and slide backgrounds; your images keep their original colors. Code stays monospaced.

Colors and the font choice are saved in the Markdown file. Install the same font on another computer to keep the typography consistent.

## Present and export

Click **Present** or press **F5** to go fullscreen. Use the arrows to navigate, Space to play or pause video, and Escape to return to editing.

Choose **Export → PDF** or **PowerPoint** to share your presentation. Both exports are built into Hype. PowerPoint slides preserve the rendered appearance rather than exposing editable text and shapes; videos are embedded. PDF captures still slides.

For PowerPoint video, use H.264 MP4 with optional AAC audio. Use `fit` for videos that aren’t 16:9. Video autoplay and looping may vary between presentation apps; playback in Microsoft PowerPoint has not yet been verified.

## Keyboard shortcuts

Slide navigation and selection shortcuts apply when the sidebar or preview has focus. Inside the Markdown editor, arrows and Shift+arrows move the cursor and select text.

| Shortcut | Action |
| --- | --- |
| Ctrl+N / Ctrl+O | New presentation / open file |
| Ctrl+S / Ctrl+Shift+S | Save / save as |
| Ctrl+E | Switch Visual / Markdown |
| Tab / Shift+Tab | Switch between sidebar and Markdown input |
| Arrow keys | Previous / next slide |
| Page Up / Page Down | Jump five slides |
| Home / End | First / last slide |
| Ctrl+Up or Ctrl+Left | Move selected slides earlier |
| Ctrl+Down or Ctrl+Right | Move selected slides later |
| Shift+arrows / Shift+click | Extend the slide selection |
| Ctrl+Enter | Add a slide |
| Ctrl+D | Duplicate selected slides |
| Delete | Delete selected slides |
| Ctrl+V | Paste text or add and name media |
| Ctrl+Z / Ctrl+Shift+Z | Undo / redo |
| F5 / Escape | Start / leave presentation |
| Space | Play / pause video while presenting |

The mouse wheel over the sidebar selects the previous or next slide. In the Markdown editor, Page Up/Down scrolls a page; Ctrl+Home/End goes to the start/end of the document.

## Run from source

To build Hype yourself, install a C++17 compiler, make, Qt 6.8 or newer, FFmpeg, and GNU source-highlight; see [the package definition](pkgbuild/PKGBUILD) for dependencies. Then:

```sh
./bin/build
./build/hype examples/welcome.md
```

For a launcher entry that rebuilds this checkout when opened, run `./bin/install-dev` and choose **Hype (Development)**.
