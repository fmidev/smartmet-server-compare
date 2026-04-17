# CLAUDE.md

## Project overview

GTK 3 (gtkmm-3.0) GUI tool for comparing HTTP/HTTPS responses from two
SmartMet server instances side by side.  Requests can be fetched from a
server's admin log or loaded from a text file (one request per line, no
host part).

## Build

```bash
make            # builds ./smartmet-server-compare
make clean      # removes obj/ and the binary
make rpm        # builds an RPM from the tarball
```

All `.cpp` files under `compare/` are picked up automatically by the
Makefile wildcard.  No SmartMet-specific build tooling is required — only
standard `pkg-config` and packages available on common Linux distributions.

### Dependencies

Build: `gtkmm30-devel`, `ImageMagick-c++-devel`, `libcurl-devel`,
`tinyxml2-devel`, `jsoncpp-devel`, `gcc-c++`, `make`.

Runtime: the matching `-libs` packages plus `adwaita-icon-theme`.

## Source layout

All source lives in `compare/`:

- **Entry point**: `main.cpp`
- **Orchestration**: `MainWindow` composes the panels and wires
  `QueryFetcher` / `CompareRunner` to the UI.
- **UI panels**: `InputBar`, `RequestListView`, `ResultPanel`, `StatusPanel`.
- **Result viewers**: `ResultViewer` (abstract), `ImageDiffViewer` (image
  comparison with animation/diff modes, PSNR via Magick++),
  `TextDiffViewer` (catch-all text/binary viewer wrapping `DiffView`).
  Register new viewers in `ResultPanel` (most-specific first) via
  `add_viewer()`.
- **Background workers**: `CompareRunner` (multi-threaded comparison),
  `QueryFetcher` (async log fetch).
- **Helpers**: `HttpClient` (parallel HTTP/HTTPS via libcurl),
  `ContentHandler` (content-type detection + formatting),
  `ImageCompare` (PSNR + diff image via Magick++),
  `UrlUtils`, `Settings` (JSON persistence in
  `~/.local/share/smartmet-server-compare/history.json`), `Types.h`.

## Conventions

- C++17 or later (compatible with gcc 8+)
- No unit tests; verify changes by building and running the GUI.
- Adwaita theme is force-set at startup for consistent rendering.
- `Glib::Dispatcher` bridges background threads to the GTK main loop;
  never touch GTK widgets from worker threads.
