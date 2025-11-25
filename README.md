# OTUI Editor

![](https://img.shields.io/github/stars/Oen44/OTUIEditor) ![](https://img.shields.io/github/forks/Oen44/OTUIEditor) ![](https://img.shields.io/github/downloads/oen44/otuieditor/total) ![](https://img.shields.io/github/issues/Oen44/OTUIEditor)

## About

Visual OTUI designer with .OTUI file generator.
[YouTube Video](https://www.youtube.com/watch?v=CQBn6jFqhlI)

## Build

- Download and install Qt 5.13.0+
- Open Qt Creator
- Import project file
- Build

## Recent Updates

- Parser now respects template inheritance chains, including styles pulled from `data/styles`, so widgets match the in-game layout.
- Template definitions without explicit ids stay hidden, preventing placeholder controls (for example the options category buttons) from rendering in the editor view.
- Scoped style cache loads shared `.otui` definitions once and reuses them for any module referencing those styles, just like the real client.
- Local template bindings mirror OTClient `base_style` resolution, so nested templates and mixins stop breaking when files depend on each other.
- Text widgets support fonts, offsets, wrapping rules, alignment, and automatic size adjustments, mirroring OTClient behavior.
- Label and button painters now use Qt font metrics for clipping, wrapping, and offset handling, so text renders exactly like in-game panes.
- Image sources resolve with smarter fallbacks (extension guessing and module-relative paths), reducing missing texture placeholders in previews.
- Margin, padding, anchor, phantom, and color properties cascade exactly like OTClient, which keeps complex UIs (Enter Game, Options) visually accurate out-of-the-box.

## TODO List

- [x] Base UI design
- [x] Base OTUI widget class
- [x] Rendering images with OpenGL Widget
- [x] OTUI widgets position and scale manipulation
- [x] Image border (9-slice) functionality
- [X] OTUI::MainWindow class
- [X] OTUI::Label class
- [X] OTUI::Button class
- [ ] OTUI::Image class
- [x] Adding and removing widgets
- [ ] Selected widget properties window
- [ ] Loading images from a directory
- [ ] OTUI file I/O