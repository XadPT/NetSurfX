# The NetSurfX journey

How a 2001 console with a 733 MHz Pentium III and 64 MB of shared RAM ended up rendering CSS grid over TLS 1.3 — and every wall we hit along the way.

## Choosing the engine

Before writing any code we compared the options. LinksBoks (a Links port) already existed on the Xbox, but it is a text-mode browser without HTTPS. NetSurf was chosen because it is written in portable C, has a framebuffer front end, renders real CSS and runs on machines far weaker than the Xbox (RISC OS, Atari, Amiga).

## Phase 1–2: getting NetSurf to compile

No Microsoft XDK is used: everything is built with **nxdk** (clang, lld-link, pdclib, a Win32 subset, lwIP, SDL2).

- NetSurf and its libraries (libcss, libdom, hubbub, libnsfb, …) assume POSIX. A shim layer (`nsshim/`, `src/xbox_posix.c`) provides file descriptors, directories, `stat`, time, `iconv` and the gzip API on top of the Win32/NT calls nxdk offers.
- `gperf` was not available, so `tools/gen_element_type.py` regenerates hubbub's element lookup table.
- nxdk's libpng is fixed-point only, so the floating-point gamma API is wrapped.
- Lessons from other NetSurf ports were applied up front: on the 3DS the `:` resource-path separator breaks with drive letters, every font face must exist, and presenting the screen after each tiny change is slow; on the Switch, emitting pointer moves without a position change stalls the toolkit loop.

## Phase 3: network and HTTPS

- A background thread reads the dashboard network configuration, brings up the NIC and applies the manual DNS server (needed for Insignia users).
- curl was replaced by a dedicated fetcher (`src/fetch_xbox.c`): one worker thread per request, non-blocking connects, chunked/gzip decoding, cookies, redirects and authentication delivered back to the NetSurf core on the main thread.
- mbedTLS 3.6 needed custom threading (critical sections), entropy (TSC jitter) and time glue. "HTTP 200 / HTTPS 200" on the console's screen was the first big milestone.

## v0.4: a real browser

The framebuffer front end was adapted for a TV and a gamepad: an analogue pointer, smooth scrolling on the right stick, an on-screen keyboard that opens when NetSurf places a caret in a field, overscan margins, an optional 720p mode and an in-game reset back to the dashboard.

A host test harness was built at this point and became the backbone of the project: the same code compiled for Linux, with a fake framebuffer that saves PNG screenshots, a scripted controller, a local HTTP/HTTPS server with its own test CA, and AddressSanitizer/ThreadSanitizer builds.

## v0.5: modern CSS

The base was moved to the netsurf-switch fork, which adds CSS grid, custom properties, gradients, shadows and more. Porting it surfaced:

- two flexbox bugs inherited from upstream (inline flex items were not blockified, and `flex: 1` was clamped to the min-content width), fixed in `tools/patch_netsurf.py`;
- SVG needed an XML parser, so expat was vendored;
- most web fonts are served as WOFF, which the nxdk FreeType 2.4 cannot read, so a small WOFF→SFNT converter was written, with memory caps and eviction for a 64 MB machine.

## v0.6: speed and robustness

- **Only four sockets.** nxdk's lwIP keeps the default `MEMP_NUM_NETCONN = 4`, so pages with many images silently lost requests. `tools/patch_nxdk.py` raises it to 16 and the fetcher reserves sockets explicitly.
- **Byte-by-byte memcpy.** pdclib copies memory one byte at a time, and nxdk builds with `-fno-builtin`. `src/xbox_fastmem.c` replaces the hot functions with `rep movsd`/`stosd` versions.
- **Keep-alive.** A connection pool reuses TCP and TLS sessions, transparently retrying when a server has closed an idle connection.
- **Disk cache.** NetSurf's filesystem backing store was enabled on the FATX disk.
- **Lost cookies.** Every exit goes through an in-game reset, which bypassed the code that saved cookies. All exit paths now save cookies, history and the cache index.
- USB keyboards and mice were added by registering nxdk's HID driver before SDL starts the USB stack.

## v0.7: a console-native UX

D-pad spatial navigation (in the style of the Wii and 3DS browsers), streaming downloads that write directly to the HDD (NetSurf's cache would otherwise keep a whole download in RAM), and user-agent "site version" modes.

## v0.7.1–0.7.2: hunting freezes on real hardware

A Google search froze the console with `Assertion failed: '0' in function 'strtof'`. Investigating it revealed a whole class of problems:

- several nxdk C library functions (`strtod`, `strtof`, `lroundf`, wide-character conversions…) are placeholders that call `assert(0)`, and an nxdk assert halts the CPU;
- pdclib's `printf` does not implement `%f`/`%e`/`%g` and does not consume the argument, shifting everything after it.

NetSurfX now ships its own implementations, verified with a link-time audit against every nxdk stub, and an "Xbox libc" variant of the host build runs the full regression suite with them.

A second freeze after a Google search left no log at all. Deep research into the nxdk runtime explained why: `fflush` only hands the data to the kernel cache, and the network receive path runs at DPC level, where a memory-allocation failure trips an lwIP assertion. Fixing this properly — structured exception handling on every thread, a DPC-safe network heap, logs that really reach the disk, and an on-screen crash display — is the top priority in the [roadmap](ROADMAP.md).
