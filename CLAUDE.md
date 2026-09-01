# CLAUDE.md

## Project overview

GTK 3 (gtkmm-3.0) GUI tool for comparing HTTP/HTTPS responses from two
SmartMet server instances side by side.  Requests can be fetched from a
server's admin log or loaded from a text file (one request per line, no
host part).

## Build

```bash
./autogen.sh    # generate configure (only needed from git checkout)
./configure     # detect dependencies
make            # builds ./smartmet-server-compare
make install    # installs to $prefix/bin
make distcheck  # build + test a release tarball
```

Uses GNU Autoconf / Automake.  All `.cpp` files under `compare/` are
listed in `Makefile.am`.  No SmartMet-specific build tooling is required
— only standard `pkg-config` and packages available on common Linux
distributions.

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
- **Helpers**: `HttpClient` (parallel HTTP/HTTPS via libcurl, optional
  persistent connections — see below),
  `ContentHandler` (content-type detection + formatting),
  `ImageCompare` (PSNR + structural anti-aliasing-aware diff — ported from
  smartmet-library-regression, no dependency on it — + diff image via Magick++),
  `UrlUtils`, `Settings` (JSON persistence in
  `~/.local/share/smartmet-server-compare/history.json`), `Types.h`.

## Partial re-runs ("Rerun filtered")

"Compare all" and "Rerun filtered" both go through
`MainWindow::start_compare(indices, filtered)`; they differ only in the
row set they hand it.  A partial re-run **does not** rebuild the query
list: it resets and re-sends only the rows visible through the active
filter and leaves every other row's `CompareResult` untouched.  That is
what makes the intended workflow possible — re-run just the differing
queries to see whether e.g. cache content explains them, then hit
"Compare all" again on the still-complete list, possibly against
different servers.

Because the subset handed to `CompareRunner::start()` is renumbered
0..k-1, the runner also takes an `indices` vector mapping each submitted
query back to its position in `MainWindow::queries_`, and reports that
original index in `CompareResult::index`.  An empty `indices` means the
identity mapping.  Everything downstream (`results_[index]`,
`RequestListView::update_status`) therefore keeps addressing rows by
their original index.

`MainWindow::run_indices_` / `run_filtered_` remember what the current
(or most recent) run covered.  They are cleared whenever the query list
itself changes (fetch, file load, query edit), since the indices would
otherwise dangle.

## Status-line statistics

`MainWindow::update_status_line()` rewrites the status line from
`collect_stats()` after every completed result, not just at the end, so
the tally grows as the run proceeds:

```
Comparing filtered 12/57…  Equal: 8  Different: 3  Error: 1   |   All 500 — Equal: 300  Different: 150  Error: 50
```

The whole-list tally is appended only for a partial re-run that does not
cover every row; "Not run: n" appears when some rows are still pending
(a subset stopped early, or rows a partial run never touched).  The
keep-alive "Reused connections" figure counts the current run's requests
only.

## Persistent connections (HTTP keep-alive)

Off by default, toggled by the "Reuse connections (HTTP keep-alive)"
checkbox in `InputBar` row 3 and persisted as the `keep_alive` setting.
`MainWindow` passes it to `CompareRunner::start()`, which passes it to
every `HttpClient` it constructs.

libcurl keeps its pool of live connections in the **multi** handle, not
in the easy handles: removing an easy handle returns its connection to
the multi's cache, where the next transfer to the same origin picks it
up.  `HttpClient::execute()` therefore creates a private multi handle
per call when keep-alive is off (nothing survives) and uses a
`thread_local` one when it is on.  Per thread rather than global,
because a `CURLM` is not thread safe — each `CompareRunner` worker
thread gets its own cache, needs no locking, and releases its
connections via `curl_multi_cleanup()` when the thread exits at the end
of a run.  `HttpClient::close_idle_connections()` drops them earlier if
a server is restarted underneath a live worker.

`Response::connection_reused` (from `CURLINFO_NUM_CONNECTS == 0`) says
whether a request actually reused a connection.  `CompareRunner` sums it
into `CompareResult::connections_reused`, and `MainWindow` appends
"Reused connections: n/m" to the final status line when the option is
on — useful for confirming that the server under test really honours
keep-alive.  Expect `m` minus (threads × servers), since the first
request per thread and origin has to open its connection.

A server that answers `Connection: close` — including an older
smartmet-server, which replies `HTTP/1.0` with no `Connection` header at
all — is never cached in the first place, so mixing a keep-alive server
and a non-keep-alive one in the same comparison works unchanged.

### Recovering from a dropped connection

A server may close a persistent connection at any moment and the client
cannot tell a connection that is about to close from a live one; it finds
out only when the request it just wrote goes unanswered.  libcurl retries
by itself while nothing has been received yet, but *not* once part of a
response has arrived — so a server dropping a reused connection
mid-response surfaces as a hard error (`Transferred a partial file`,
`Recv failure: Connection reset by peer`, `Server returned nothing`).

`HttpClient::execute()` therefore runs `perform()` twice: any request
that (a) went over a connection taken from the cache and (b) failed with
one of those errors is retried once with `CURLOPT_FRESH_CONNECT`.  A
failure on a connection libcurl had just opened is a real server problem
and is reported as-is, which also means the retry cannot loop.  Every
request is a GET, so repeating one is safe.

## Conventions

- C++17 or later (compatible with gcc 8+)
- No unit tests; verify changes by building and running the GUI.
- Adwaita theme is force-set at startup for consistent rendering.
- `Glib::Dispatcher` bridges background threads to the GTK main loop;
  never touch GTK widgets from worker threads.
