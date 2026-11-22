# Roadmap

Priorities come from the research in [docs/research/](research/). Stability first, then memory, then speed.

## P0 — Stability (next release)

- [ ] Reproduce the Google freeze in xemu with the GDB stub (`-s`), read EIP and symbolise it (fixed image base, `llvm-symbolizer`).
- [ ] Logs that really reach the disk (`NtFlushBuffersFile`), plus an on-screen mirror of the last log lines that works even from a crashed state.
- [ ] Structured exception handling (`__try/__except`) around `main` and every thread entry, writing registers and a backtrace to a crash log.
- [ ] IRQL-aware assert handler: never "log and continue" from DPC level; drop the packet instead of asserting when the RX pool is exhausted.
- [ ] Dedicated, DPC-safe heap for lwIP so the network stack never competes with NetSurf for memory.
- [ ] Intercept Google search URLs (redirect to the configured engine); cap redirect and meta-refresh chains; limit CSS and document size.
- [ ] `SIGABRT` handler so heap corruption no longer reboots silently; bigger stacks for fetch (256 KB) and lwIP threads.
- [ ] Switch the UI, home page, help, README and defaults to English (`Accept-Language: en-US`, US keyboard layout by default, DuckDuckGo HTML as the default engine).

## P1 — Memory budget

- [ ] Derive cache sizes from `MmQueryStatistics` at startup (4–6 MiB memory cache, emergency reserve, out-of-memory dialog, image downscaling at decode time).
- [ ] Audit for remaining pdclib traps (`sscanf("%f")`, `errno` shared between threads).

## P2 — Speed

- [ ] Dirty-rectangle blitter with non-temporal stores, bypassing SDL's full-window copy.
- [ ] TLS: enable TLS 1.3 session tickets (`mbedtls_ssl_conf_tls13_enable_signal_new_session_tickets`), prefer P-256 and ChaCha20.
- [ ] Disk cache layout tuned for FATX 16 KB clusters (pack files for small objects, hashed subfolders).

## P3 — Reach

- [ ] GPU tile compositor on the NV2A via pbkit (scrolling and zoom at 60 fps in 720p).
- [ ] Zoom levels, single-column reading mode, "Open via WRP" (self-hosted rendering proxy for JavaScript-only sites).
- [ ] QuickJS, off by default, with a per-site allow list and a heap limit; brotli for WOFF2 with a memory cap.
