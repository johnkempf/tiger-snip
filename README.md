# Tiger Snip

Tiger Snip is a local Windows screenshot and annotation tool. Capture an area or the full desktop, add text and drawings, then copy the image or save it as a PNG or JPG.

## Features

- Pen, highlight, text, shapes, arrows, checks, and lines.
- Shapes includes **Redaction**: soft backdrop blur with rounded, feathered edges that blend gradually into light or dark content.
- Text supports **Left**, **Center**, and **Right** alignment, including editable text boxes.
- Solid, dashed, dotted, and measurement lines with perpendicular end caps (`|-----|`).
- Crop, undo/redo, zoom, and ten recent editable captures.
- Crop dropdown with **Select area**: lift screenshot pixels and annotations, move or resize them, and leave white behind.
- Paste images or copied image files with **Ctrl+V**, preserving PNG transparency; drag to move or resize.
- Top toolbars or side panels, with Purple, Blue, Teal or a custom UI color.
- Light or Dark appearance in either layout.
- Stroke presets plus an adjustable pixel slider, and per-annotation opacity.
- Optional borders, Samtec logos, and capture shortcuts.

## Installation

Open **dist/Tiger Snip Setup.msi** and click **Install**. When setup confirms success, click **Finish** to launch Tiger Snip (or uncheck **Launch Tiger Snip**). You can also open it from the Start menu. Each user gets a local installation. For shared-drive distribution, place the MSI in the shared folder.

The standalone **dist/Tiger Snip.exe** can also run directly.

Fresh installations start with the new **Side panels** UI and the Purple light theme.
Existing users keep their saved layout and colors.
Saved selections of the retired Orange preset switch to Purple; saved custom colors are retained.

## Use

Click **New snip** and drag a rectangle. **Ctrl+C** copies a selected annotation; **Ctrl+V** pastes an editable duplicate. With nothing selected, **Ctrl+C** copies the whole image. The toolbar **Copy** button always copies the whole image. Press **Ctrl+S** to save.
**Ctrl+S**, **Ctrl+Shift+S**, and the top-right **Save as** button always open the modern
Windows save picker, even after you have saved the snip. It starts in your current
**Preferences → Saving & Export → Save location**, so you can choose another folder for each copy.
Choose **PNG** or **JPG** under **Preferences → Saving & Export → Default file type**. PNG is the default.
JPG disables Professional Border, including blur and rounded corners, because it does
not support transparency. Switching back to PNG restores your previous border choices.
**Save as type** also offers both formats. Choosing JPG there omits the border, blur,
and rounded corners for that file; choosing PNG uses your remembered border choices.
The dropdown does not change the default format in Preferences or the editor preview.
The capture icon on the welcome screen also starts a snip. **Recent** keeps the last ten captures;
Right-click the open snip or a Recent thumbnail for **Copy**, **Save**, and **Save As**.
Recent actions include annotations and export effects without reopening the capture.

**Ctrl+Alt+T** copies text from an area: drag over the text, release, then paste anywhere.
Recognition runs locally using the built-in Windows OCR engine and your installed
recognition language; no image is uploaded or separate OCR model bundled.
Tight selections are padded internally; small text is enlarged and its contrast adjusted
before recognition, so you do not need to select a large empty area around a label.
Complete compact lines use consistent recognition margins; field borders, icons, and
unrelated edge fragments are separated from the text on flat backgrounds.
Narrow lettering is widened internally; small clipped barcode fragments are separated
from complete digits before recognition.
On flat document and app backgrounds, rows with characters cut by a selection edge are
omitted. Leave a small margin around the complete line you want to copy.
A small **Text copied** popup previews the exact text for three seconds. Hover to keep it
visible; click to dismiss. Long previews are shortened, while the clipboard contains all
recognized lines. **Esc** cancels; a selection with no text leaves your clipboard unchanged.
The editor stays hidden after a text capture, including when it was open or minimized
beforehand. The current snip is preserved. Change or disable this shortcut under
**Preferences → Capture**. The capture menu
also offers **Copy text from an area**; **App menu → Copy text from snip** (Classic UI: **File**) extracts
text from the current screenshot. OCR can confuse small or blurry characters, so check
the preview for exact identifiers and zoom in before selecting when needed.

**Preferences → Appearance → Interface layout** switches immediately between **Top toolbars** (the original ribbon)
and **Side panels** (the tool rail and properties panel). The choice and each layout's visibility
settings are remembered. Switching keeps the current image, annotations, selection and undo history.

With Side panels, choose drawing tools on the left. The properties panel on the right shows color, pixel size,
style presets and opacity for the current tool or selected annotation. Use the small chevrons
on Shapes, Arrow, Check/X and Line to choose a preset, or use the visual style picker in the
properties panel. Pixel sizes have quick presets, a continuous slider and plus/minus controls.
Stroke and highlight sliders stop at 40 px for finer control; use plus/minus for larger sizes.
Font sizes retain their full slider range. Each slider drag is one undoable edit.
**Esc** cancels a slider drag.

Enable **Preferences → General → Keep annotation tool active** to place shapes, arrows,
lines, checks/X marks and text repeatedly. It is off by default, so new annotations normally
switch to Select for immediate moving/resizing. Pen and highlighter always stay active.
The **…** app menu next to Undo/Redo includes **Exit Tiger Snip**; Classic UI keeps **File → Exit**.

Selected text has side handles for independent box sizing. Drag left/right to rewrap text
at the same font size, or top/bottom to adjust height while keeping every line visible.
Corners scale the font. Resizing supports Undo/Redo and **Esc** cancellation.

The **Settings** gear (or **F10**) opens the full settings panel in either UI. In Top toolbars,
you can also use **Settings → Preferences...**. **Appearance → New UI** switches to Side panels;
**Classic UI** returns to Top toolbars. The panel includes appearance, capture,
saving/export and view options. Preferences save immediately. Choose **Purple**, **Blue**
or **Teal**, then **Light** or **Dark**; both choices work with either layout and affect only
the editor. **Custom color** opens the color spectrum with RGB and hex input for your own UI
accent. Apply saves it; Cancel keeps the previous theme. Capture buttons show the exact chosen
color in both layouts and appearances, with readable light or dark labels and subtle borders.
Small accent labels and icons adjust for readability. The chosen color is remembered when you
switch presets.
Capture shortcuts, auto copy, rendering, all border options and all six logo styles
remain available. The capture button's chevron
also offers an instant capture of all monitors. Zoom and Fit are in the bottom bar.
In Side panels, clicking the bottom-right Fit / 100% control switches between fitting
the image to the window and viewing it at actual size.
Top toolbars retains the native menu bar and the original controls above the image.
Classic controls use consistent neutral surfaces and outlines in every theme. The selected
drawing tool uses the accent color; Fit and other view modes use a neutral selected state.
Copy feedback combines a stronger white pulse with a clearly visible **Copied to clipboard**
confirmation for 1.4 seconds. The pulse follows image transparency; feedback never changes exports.

Capture shortcuts accept single keys such as **Print Screen**, **Page Up**, **Page Down**,
**F5** or **Pause**, and combinations with **Ctrl**, **Alt**, **Shift** or **Win**.
They work globally while Tiger Snip is running, so the assigned key takes over its usual
action in other apps (including typing if you choose a letter). **F12** is reserved by
Windows; other reserved or already registered shortcuts show an error and keep the previous
bindings. In shortcut fields, bare **Esc** cancels and **Backspace/Delete** disables;
the original dialog also uses **Tab** to move focus and **Enter** to save.
If Print Screen opens Windows Snipping Tool, turn off **Use the Print Screen button to
open screen snipping** in **Windows Settings → Accessibility → Keyboard**.

[Application information](APP-INFO.md) describes installation and data storage.

## Build

Using the toolchain described in [BUILD-TOOLCHAIN.md](BUILD-TOOLCHAIN.md):

```powershell
.\build.ps1 -Test
.\package.ps1
```

OCR uses Windows platform APIs; builds need no third-party OCR libraries or models.
The alternative bundled-engine implementation is preserved on `codex/bundled-ocr`
for personal use. `main` contains the Windows-only implementation.

## Support

Jack Kempf — jack.kempf@samtec.com.
