NetSurfX 0.8.0 (test build) - the NetSurf web browser for the original Xbox
============================================================================

This is a TEST build: diagnostics are switched on (see DIAGNOSTICS below).
Project page: https://github.com/XadPT/NetSurfX

NEW IN 0.8.0
  Stability
  - Fixed the most likely cause of the total freeze while browsing (e.g. on
    Google): when memory ran out, the network driver wrote through a null
    pointer inside an interrupt handler. It now drops the packet instead,
    and TCP simply sends it again.
  - Network receive buffers resized for a browser (128 buffers, 34 KB TCP
    window): busy connections can no longer starve all the others.
  - Crash handler: a crash now shows a screen with the details, saves them
    to NSX-CRASH.LOG and returns to the dashboard after 30 seconds.
  - Hang detector: if the browser stops responding for 8 s a notice appears
    on screen and NSX-HANG.LOG is written; after 3 minutes the console
    returns to the dashboard on its own.
  - Logs now really reach the disk every 2 s (before, a freeze or a power
    cycle lost them), so NETSURF.OLD always tells what happened last time.
  - Internal checks that fail in the network interrupt path no longer touch
    the disk from there (that could itself freeze the console).
  - Fixed two small memory leaks in find-in-page.
  Google
  - Google Search has needed JavaScript since January 2025. Google searches
    (and the Google home page) now open DuckDuckGo's HTML version with the
    same query. Switch it off in Start > Settings > Google searches.
  Speed
  - Secure (HTTPS) connections: faster key exchange (P-256 first) and TLS 1.3
    session resumption, so the second page of a site connects much faster.
  - Network debug code removed from the network stack.
  - Memory cache sized from the RAM free at start-up (3 to 8 MB).
  English
  - The whole browser is now in English: menus, settings, help, on-screen
    keyboard, home page, messages and logs. Sites are asked for English
    pages (Accept-Language: en-US). USB keyboards default to the US layout
    (Portuguese is still available in Settings).
  - Default search engine: DuckDuckGo (HTML). Also: DuckDuckGo Lite,
    Wikipedia, Mojeek, FrogFind.

INSTALL
  Copy the whole NetSurfX folder to the Xbox hard disk over FTP, for example
  E:\Apps\NetSurfX\ (default.xbe must stay next to the "res" folder).
  Start default.xbe from your dashboard.
  The network cable must be plugged in. The dashboard's network settings
  are used (automatic or manual IP, manual DNS if you have one).
  Upgrading: replace default.xbe and the "res" folder; keep "data" (your
  cookies, history and bookmarks).

CONTROLLER
  Left stick ............. pointer
  A ...................... click / follow link / edit a field
  Right stick ............ scroll the page
  L / R triggers ......... page up / page down
  D-pad .................. jump between links and fields
  B ...................... back                White ... forward
  X ...................... reload              Y ....... address or search
  Black .................. bookmark this page
  Start .................. menu (bookmarks, history, find, settings)
  Back ................... help
  Stick clicks ........... zoom out / zoom in
  L + R + Back + Black ... return to the dashboard (IGR)

ON-SCREEN KEYBOARD
  Opens with Y, or when you select the address bar or a text field.
  A key, B delete, X space, Y shift, White symbols/accents, Black clear,
  L/R move the cursor, Start go/submit, Back cancel.
  Words typed in the address bar become a search.

USB KEYBOARD AND MOUSE (experimental)
  Connect them with a "Xbox controller port to USB" adapter before starting
  NetSurfX. Typing anywhere opens the address bar.
  Ctrl+L / F6 address      F5 reload            Esc stop
  Alt+Left / Alt+Right     back / forward       Backspace back
  Ctrl+F find, F3 next                          Ctrl+D bookmark
  Ctrl+H history           Ctrl+B bookmarks     Ctrl + / - / 0 zoom
  F10 menu                 F1 help
  Mouse: left button clicks, right button opens the menu, wheel scrolls.

FILES
  res\            program resources (do not edit), certificates, fonts
  data\           your data: Cookies, history (URLs), bookmarks
  cache\          disk cache (safe to delete at any time)
  data\Choices    options (see data\Choices.example)
  DASH.TXT        (optional) path of your dashboard for the IGR combo,
                  e.g. C:\xboxdash.xbe

DIAGNOSTICS (test builds)
  NETSURF.LOG     log of this session
  NETSURF.OLD     log of the previous session (the useful one after a
                  freeze or a crash)
  NSX-CRASH.LOG   written when the browser crashes
  NSX-HANG.LOG    written when the browser stops responding for 8 s
  NSX-ERROR.LOG   written when an internal check fails
  NODEBUG.TXT     create an empty file with this name next to default.xbe
                  to switch the logs off
  If something goes wrong, please send these files (and a photo of the
  screen, if there was a message) to the developer.

LIMITS
  No JavaScript, so very dynamic modern sites (YouTube, social networks)
  do not work. Simple sites, text-only news, Wikipedia, forums and
  DuckDuckGo work well (see the home page).
  If the Xbox clock is far off, some HTTPS sites may show a certificate
  warning (you can continue with "Proceed").
  No video or audio.

LICENCES
  NetSurf (GPLv2), mbedTLS (Apache 2.0), expat (MIT), libwebp (BSD),
  stb_sprintf (public domain), nxdk (MIT), DejaVu fonts. See "licenses".
  NetSurfX port by XadPT.
