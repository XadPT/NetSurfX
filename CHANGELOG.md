# Changelog

All dates are 2026.

## v0.7.2 (test build) — 13 Nov
- Fixed a hard freeze ("Assertion failed … strtof") on pages with modern CSS: nxdk's `strtod`/`strtof`/`strtold`/`atof` are `assert(0)` stubs. NetSurfX now ships its own (tested against glibc on 200,000 inputs), plus `lroundf`/`llroundf` used by SVG rendering.
- The nxdk/pdclib `printf` family ignores `%f/%e/%g` without consuming the argument, which shifted every following argument. Replaced with stb_sprintf (public domain).
- Link-time audit: no `assert(0)` stub from nxdk is referenced any more.
- `_xbox_assert` overridden: failed assertions are logged to `NSX-ERRO.LOG` instead of halting the CPU.

## v0.7.1 (test build) — 13 Nov
- Diagnostics on by default: `NETSURF.LOG` (flushed every second, capped at 8 MB, previous session kept as `NETSURF.OLD`), `[REDE]` per-request timings, `[PERF]` page times, heap and free-memory stats, slow-redraw reports.
- Watchdog thread writes `NSX-BLOQUEIO.LOG` when the main loop stalls for more than 8 s.
- Optional on-screen stats in the status bar.

## v0.7 — 13 Nov
- D-pad spatial navigation between links and form fields.
- Streaming downloads straight to disk (memory-safe for large files), with progress, cancel, redirect following and FATX-safe file names; E:/F:/G: partitions mounted.
- "Site version" setting: normal, mobile, light (Opera Mini) or text (Links) user-agent.

## v0.6 — 13 Nov
- HTTP keep-alive connection pool with dead-connection retry; in-app DNS cache.
- Disk cache in `D:\cache` (64 MB); a failing cache no longer prevents startup.
- Fast `memcpy`/`memmove`/`memset`/`memcmp`/`strlen` (pdclib's versions are byte-by-byte).
- lwIP socket table raised from 4 to 16 — image-heavy pages were silently failing.
- Cookies, history and the cache index are now saved on every exit path, including in-game reset.
- Start menu, persisted history, find in page, settings screen, clear browsing data.
- USB keyboard (Portuguese/US layouts, dead keys) and mouse support.

## v0.5 — 13 Nov
- Rebased on the netsurf-switch fork: CSS grid, custom properties, `calc()`, gradients, shadows, rounded corners, `<details>`/`<dialog>`/`[hidden]`.
- Two upstream flexbox fixes (inline children are now blockified; `flex: 1` distributes space equally).
- Web fonts (TTF/OTF and WOFF via an in-house converter) with memory caps and eviction.
- SVG (libsvgtiny + vendored expat) and WebP (vendored libwebp decoder).

## v0.4.x — 12 Nov
- First complete browser: framebuffer frontend for the Xbox, on-screen keyboard, help screen, bookmarks, zoom, 720p option, overscan, in-game reset.
- v0.4.1: the on-screen keyboard no longer reopens after typing.
- v0.4.2: hid the duplicate scrollbars and fixed the toolbar icons disappearing.

## Phase 3 — 12 Nov
- Networking through lwIP using the dashboard configuration (including manual DNS).
- HTTPS with mbedTLS 3.6 (custom threading, entropy and time glue), confirmed on real hardware.

## Phase 2 — 12 Nov
- NetSurf 3.11 core and libraries cross-compiled with nxdk; POSIX/Win32 shims (files, directories, time, iconv, gzip).

## Phase 1 — 12 Nov
- nxdk bootstrap: a minimal XBE with video output and controller input.
