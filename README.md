# NetSurfX

**A modern-CSS web browser for the original Xbox (2001)**, based on [NetSurf](https://www.netsurf-browser.org/) and built with the open-source [nxdk](https://github.com/XboxDev/nxdk) toolchain — no Microsoft XDK required.

> **Status: active development / test builds.** NetSurfX runs on real hardware (tested on a retail console over wired Ethernet). The current builds (v0.7.x) ship with diagnostics enabled by default. A hard freeze after a Google search is under investigation — see [Known issues](#known-issues) and [docs/ROADMAP.md](docs/ROADMAP.md).

As far as we know, NetSurfX is the most capable browser the original Xbox has had: the only other documented one, LinksBoks (a Links port), has no HTTPS.

---

## Features

**Web engine**
- NetSurf core with the modern-CSS work from the [netsurf-switch](https://github.com/etonedemid/netsurf-switch) fork: CSS grid, flexbox, custom properties (`var()`), `calc()`, gradients, box shadows, rounded corners, opacity.
- Web fonts (`@font-face`) in TTF/OTF and **WOFF** (an in-house WOFF→SFNT converter, because the nxdk FreeType 2.4 cannot read WOFF). WOFF2 is not supported yet.
- Images: PNG, JPEG, GIF, BMP, **SVG** (via libsvgtiny + expat) and **WebP** (libwebp decoder).
- HTML forms, cookies, redirects, history (persisted between sessions), bookmarks.

**Networking**
- Custom Xbox fetcher (`src/fetch_xbox.c`) instead of curl: one worker thread per request over lwIP.
- **HTTPS** with mbedTLS 3.6 (TLS 1.2/1.3, certificate verification, NetSurf's "Proceed anyway" warning page, TLS session resumption).
- HTTP/1.1 **keep-alive connection pool**, in-app **DNS cache**, gzip/deflate, chunked encoding.
- Uses the dashboard network settings, including manual DNS (e.g. for Insignia).
- lwIP socket table raised from 4 to 16 (the stock limit made image-heavy pages fail).
- **Disk cache** (`D:\cache`, 64 MB) — pages and images already seen load from the HDD.
- **Streaming downloads** straight to disk (`E:\Downloads` by default), so large files never sit in RAM.

**Console UX**
- Left stick = pointer, right stick = smooth scroll, triggers = page up/down, A = click.
- **D-pad spatial navigation**: jumps between links and form fields (Wii/3DS style), scrolls when there is nothing in that direction.
- On-screen keyboard for the URL bar and page fields (with accents and shortcut keys).
- **Start menu**: home, bookmarks, history, find in page, zoom, settings, downloads, help, exit.
- Settings: search engine, "site version" (normal / mobile / light / text user-agent), D-pad mode, images on/off, ad blocking, 720p, USB keyboard layout, download folder, on-screen stats, clear browsing data.
- **USB keyboard and mouse** (through a controller-port-to-USB adapter), with shortcuts (Ctrl+L, F5, Alt+←, Ctrl+F, …).
- In-game reset (L + R + Back + Black) returns to the dashboard; cookies, history and the cache index are saved on every exit path.
- 480p by default, optional 720p, overscan margins.

**Diagnostics (test builds)**
- `NETSURF.LOG`: per-request network timings (`[REDE]`), per-page time and free memory (`[PERF]`), slow redraws, a 30-second summary. The previous session is kept as `NETSURF.OLD`.
- `NSX-BLOQUEIO.LOG`: written by a watchdog thread if the main loop stalls for more than 8 seconds.
- `NSX-ERRO.LOG`: failed internal assertions are logged instead of halting the console.
- Optional on-screen stats in the status bar.

## Install

1. Download the latest `NetSurfX-vX.Y-xbox.zip` from the Releases page.
2. Copy the `NetSurfX` folder to the Xbox (e.g. `E:\Apps\NetSurfX`) via FTP.
3. Launch `default.xbe` from your dashboard. Keep the `res` folder next to it; your data lives in `data`.

When updating, replace everything except the `data` folder to keep cookies, history and bookmarks.

## Building

NetSurfX is cross-compiled on Linux with nxdk (clang/lld). The build is currently scripted around a fixed directory layout; making it path-independent is on the roadmap.

| Component | Source |
|---|---|
| nxdk | `git clone --recursive https://github.com/XboxDev/nxdk.git` (patched by `tools/patch_nxdk.py`: 16 lwIP sockets) |
| NetSurf | [etonedemid/netsurf-switch](https://github.com/etonedemid/netsurf-switch) (core + libraries), patched by `tools/patch_netsurf.py` — see `BASE.txt` |
| mbedTLS | 3.6.2 |
| Vendored | `third_party/`: expat 2.6.4, libwebp 1.4.0 (decoder only), stb_sprintf |

Outline:

1. `python3 tools/patch_nxdk.py <nxdk>` and `python3 tools/patch_netsurf.py <netsurf-tree>` (idempotent).
2. Build the NetSurf libraries natively once (generates sources), then cross-compile with the nxdk environment active (`eval "$(nxdk/bin/activate -s)"`):
   `tools/build_libs.py`, `tools/build_mbedtls.py`, `tools/build_expat.sh`, `tools/build_webp.sh`, `tools/build_ns.py`.
3. `make` → `bin/default.xbe`, then copy it into `dist/NetSurfX/`.

`gperf` is not needed: `tools/gen_element_type.py` generates hubbub's element table.

### Host test harness

`tools/hosttest/` compiles the exact same browser code for Linux with simulated hardware: an in-memory framebuffer with PNG screenshots, a scripted gamepad/keyboard/mouse, POSIX sockets and a local HTTP/HTTPS test server (`srv/server.py`, with a throwaway test CA). AddressSanitizer, ThreadSanitizer and an "Xbox libc" variant (our `strtod`/`printf` replacements) are available. Every release is regression-tested with these scripts before it goes to hardware.

## Known issues

- **Hard freeze after a Google search, with no log written** (v0.7.2). Google has required JavaScript since January 2025, so its pages cannot work anyway. The most likely cause is memory exhaustion inside the network driver's DPC path. The capture and fix plan is in [docs/ROADMAP.md](docs/ROADMAP.md).
- No JavaScript (planned: QuickJS, off by default).
- No video or audio playback.
- 720p/1080i depend on the console region and AV pack; PAL consoles may be limited to 480 lines.
- The UI is currently in Portuguese; it is being switched to English for the next release.

## Documentation

- [CHANGELOG.md](CHANGELOG.md) — version history.
- [docs/JOURNEY.md](docs/JOURNEY.md) — how the port was built, the problems hit and how they were solved.
- [docs/ROADMAP.md](docs/ROADMAP.md) — prioritised plan.
- [docs/research/](docs/research/) — technical research on the Xbox's potential and limits (Portuguese).

## Credits

- **XadPT** — project author.
- The [NetSurf](https://www.netsurf-browser.org/) developers, and etonedemid for the [netsurf-switch](https://github.com/etonedemid/netsurf-switch) modern-CSS work.
- The [XboxDev / nxdk](https://github.com/XboxDev/nxdk) contributors.
- mbedTLS, lwIP, FreeType, expat, libwebp, stb_sprintf, DejaVu fonts.

## License

NetSurfX is a derivative of NetSurf and is distributed under the **GNU General Public License v2** (see [LICENSE](LICENSE)). Third-party components keep their own licences (see `dist/NetSurfX/licencas/` and `third_party/`).
