# OTUIEditor → Mehah Format Adaptation

## Current Baseline (OTUIeditor-main)
- Entry: `main.cpp` loads `stylesheet.css`, kicks off `StartupWindow`, then instantiates `CoreWindow` once a project is chosen.
- `corewindow.cpp` owns the project/session state (recent projects, project settings dialog, data path), the widget tree (`QStandardItemModel`) and the `OpenGLWidget` canvas.
- Canvas: `openglwidget.cpp` subclasses `QOpenGLWidget`, renders each `OTUI::Widget`, handles drag/resize, and now exposes `setWidgets()` so parser outputs can replace the current scene.
- Parser scaffolding: `otui/parser.*` already hooked into File → Open; it still emits placeholder windows, so the next steps focus on implementing real Mehah parsing and serialization.
- Property editing UI from the legacy fork is absent here; we will need a new inspector once the parser can load Mehah attributes.

## Shared Asset Data Plan
- The real client assets live under `data/` at the repo root. To let the editor preview sprites and load `#file` includes (`10-buttons.otui`, `10-comboboxes.otui`, etc.), keep an up-to-date copy of `z:/Backup_F/OTX/OTC-Crystal/data` inside `OTUIEditor-master/data`.
- `ProjectSettings` already tracks a data path. For convenience, create editor projects that point to `../data` so `ImageSourceBrowser` and future parser code resolve `images/...` and `styles/...` relative to the copied folder.
- Needed subtrees: `data/images/**`, `data/styles/**`, plus `data/fonts` for `font:` references. Copying the full tree keeps `#file:data/...` references valid without inventing a parallel asset structure.
- Long term we can add a settings toggle allowing users to point to any OTC install path, but mirroring the repository `data` directory unblocks parsing immediately.

## Reference OTUI Samples
- Legacy demo (useful for regression): `tools/otui_editor/styles/10-windows.otui`.
- Target Mehah files: `data/styles/50-uieditor.otui`, `modules/client_options/styles/interface/HUD.otui`, and recent protocol 15.11 HUD snippets under `src 1511x`.

## Format Differences to Support
- Syntax: Mehah widgets are declared as explicit tags (`UIWidget`, `OptionCheckBox`, `CyclopediaProficiencyWindow`) instead of `Alias < Base`. Blocks can mix property tokens, child widgets, and inline scripts.
- Prefixed properties: `!` for localized text, `@` for event bindings, `&` for numeric state, plus dashed keys (`image-source`, `background-color`). The parser must preserve unknown keys verbatim.
- Anchors: each edge uses `anchors.left/right/top/bottom` with optional target ids. Margins/padding use dashed keys; there is no single `anchors.centerIn` helper anymore.
- Scripts and includes: inline Lua after `@event:` and `#file:` directives that pull in other style files; we must resolve relative to the data path above.

## Parser + Serializer Backlog
1. Replace the placeholder parser logic with an OTML-compatible reader (reuse otc `OTMLDocumentPtr` once we wire the dependency) so Mehah files load without data loss.
2. Emit the widget hierarchy into `OTUI::Widget` instances, storing unknown attributes inside a `QVariantMap` that round-trips on save.
3. Handle `#file:` includes by reading the referenced file (paths resolved against the project data path). Detect cycles to avoid infinite recursion.
4. Implement `Parser::saveToFile` so it writes Mehah-compliant ordering (id → anchors → layout props → prefixed keys → script blocks) and keeps inline Lua untouched.
5. Surface parser errors (missing include, malformed anchor) via `CoreWindow::ShowError` with context (file + line) so users know how to fix the source `.otui`.

## Widget Model Roadmap
- Add anchor + margin structs on `OTUI::Widget` matching otc `UIWidget`. Include fields for target id, alignment flags, and percentages so the canvas can preview layout constraints later.
- Introduce a `GenericWidget` subclass able to render a placeholder rectangle while still storing all arbitrary properties for widget types not yet mapped.
- Build a widget factory that matches Mehah tag names to either specific subclasses (`MainWindow`, `Button`, `Label`) or `GenericWidget` fallback.
- Record prefixed properties and script bodies even if we do not edit them yet. This protects data when saving.

## UI/UX Tasks
- Reintroduce a property inspector (Qt model or dock widget) that binds to the selected widget, showing at least id/size/position plus raw property list for debugging.
- Add a data-path selector in Project Settings pointing to the copied `data/` folder, with validation and helpful error text when sprites are missing.
- Extend the tree context menu so users can import Mehah widgets (maybe by pasting raw OTUI snippets) once parsing is robust.

## Validation Loop
- Load `data/styles/50-uieditor.otui`, ensure all children appear in the tree and canvas.
- Edit a geometry value, save, and diff against the original to confirm only the edited property changed.
- Run otc client with the saved file to confirm there are no syntax regressions.

## Documentation & Tooling
- Capture the data-folder expectations and parser limitations inside `README.md` (editor section) after the first end-to-end load works.
- Keep this TODO updated with completed milestones so future contributors know the new baseline (`OTUIeditor-main`).
