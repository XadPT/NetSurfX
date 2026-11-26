NetSurfX 0.9.3 (test build) - the NetSurf web browser for the original Xbox
============================================================================

This is a TEST build: diagnostics are switched on (see DIAGNOSTICS below).
Project page: https://github.com/XadPT/NetSurfX

NEW IN 0.9.3
  - Pages you have left are now freed within 2 seconds (it used to take
    up to 20). Clicking quickly through a heavy site kept several old
    pages in memory at once.
  - JavaScript: memory used by the scripts of pages you have left is now
    reclaimed. Before, after 4 or 5 pages with scripts the JavaScript
    memory filled up, scripts failed with "out of memory" and pages got
    slow. On the PC test: before, 44 pages in 2.5 minutes and scripts
    failing; now 135 pages, all scripts working, memory steady.

NEW IN 0.9.2
  - Fix: after heavy pages the free memory shown stayed very low (e.g.
    2.5 MB) even on light pages, and every link was stopped with "Low
    memory". Memory freed by earlier pages stays inside the browser for
    reuse and was not being counted. It now is, and when memory is low
    the browser also hands unused memory back to the system.
  - Fix: if D:\NETSURF.LOG cannot be created, the log goes to
    D:\NETSURF2.LOG (or E:\NETSURF.LOG), and NSX-ERROR.LOG says why.

NEW IN 0.9.1
  - Reader mode (Start menu): shows the page without the site's own
    styles, as plain readable text. Handy on cluttered or broken pages.
    It lasts until you turn it off or restart.
  - Dark mode (Start > Settings): light text on a dark background on
    every site. It uses res\dark.css in place of res\user.css.
  - JavaScript per site (Start > Settings > "JavaScript on <site>"):
    default, always on or always off for the site you are on, whatever
    the main JavaScript setting says. Kept in data\js-sites.txt.
  - Removing bookmarks on the console: press Black on a page that is
    already a bookmark, or on the bookmarks page to pick one from a list.
  - Screenshots (Start menu): saved as BMP files in E:\Screenshots (the
    drive of your download folder), or D:\Screenshots if that fails.
  - Screensaver: after 5 idle minutes the screen goes black with a
    slowly moving name; any button brings the page back (the button does
    nothing else). Downloads and page loads carry on. Start > Settings
    sets 2, 5, 10 or 20 minutes, or off.
  - Big JPEG photos are decoded at 1/2, 1/4 or 1/8 of their size when
    that still leaves at least 800x600 pixels: a 12-megapixel photo now
    takes about 3 MB of memory instead of 48 MB, and decodes faster. It
    is still shown at the size the page asks for.
  - Secure sites: ChaCha20-Poly1305 is offered first. The Xbox processor
    has no AES instructions and runs it much faster than AES-GCM, so
    HTTPS pages take less processor time (sites without it still use AES).
  - JavaScript: the browser's own script library is compiled once per
    session instead of on every page (about 10 times quicker per page).

NEW IN 0.9.0 - JAVASCRIPT (EXPERIMENTAL)
  - JavaScript engine (QuickJS-ng) with the page bindings of the NetSurf
    Nintendo Switch port: document changes, click and form events,
    timers, JSON, regular expressions and modern JavaScript syntax.
  - OFF by default. Turn it on in Start > Settings > JavaScript; the
    current page reloads.
  - Safety limits: each page's scripts may use at most 6 MB of memory
    (data\Choices: xbox_js_memory) and a script that runs for more than
    5 seconds is stopped; the page carries on without it.
  - Setting up scripts costs each page about 1 MB and a fraction of a
    second. Simple sites (menus, tabs, forms, content loaded by script)
    gain the most; big sites (YouTube, social networks, Google) remain
    too heavy for this console.
  - Fixed: a <button> outside a form no longer shows "form can not be
    submitted"; two small memory leaks in the disk cache and one in
    pages that scripts change.
  - Maths functions missing from the Xbox C library (cbrt, log2, expm1,
    log1p, the hyperbolic functions and others) added: they would have
    frozen the console.

NEW IN 0.8.4
  - Memory guard: below 12 MB free the unused cache is dropped; below 7 MB
    the page stops loading (what arrived stays on screen) and a message
    suggests the site's lite or mobile version. Very heavy sites such as
    the full CNN page could take the console down to 7 MB before.
  - History view (the tree button): the page that was still loading no
    longer paints over it, the page is redrawn when it closes, and any
    controller button closes it.
  - A web address whose name is a single word ("https://google/") is
    searched for instead of failing with a DNS error.

NEW IN 0.8.3
  - Fixed a crash when returning to the dashboard (L+R+Back+Black) in a
    session where NETSURF.LOG could not be opened: the log was closed
    without existing. If the log cannot be opened, NSX-ERROR.LOG now says
    so, with the reason.
  - Fixed a timing value shared between the network threads and the main
    thread without a lock (found by the thread checker on the PC).

NEW IN 0.8.2
  - Fixed the crash on DuckDuckGo (and other pages laid out with CSS grid):
    the grid code sorted its items with a comparison that was not a valid
    ordering, and the Xbox C library's sort then ran off the end of the
    list. The comparison is fixed, and the sort itself was replaced by one
    that can never leave the list, whatever it is given.
  - Settings menu no longer flickers when an option is changed.
  - Start-up screen with the NetSurf logo ("Original Xbox Edition") while
    the browser loads; any button skips it.

NEW IN 0.8.1
  Fixes brought in from the official NetSurf project (newer than the
  Nintendo Switch port this browser is based on):
  - CSS: a calc() dividing by zero crashed the browser; two reads past the
    end of a style sheet's data; vh and vw units were swapped; sizes are
    now rounded to the nearest pixel; flex shorthand parsed correctly;
    inline style priority and relative positioning fixes.
  - GIF: decoder off-by-one, and a crash when the previous frame could not
    be allocated (low memory).
  - BMP and SVG: bad files no longer misdecode or leak memory.
  - https://site:443/ is now the same site as https://site/ (cookies,
    cache and connections).
  - A small buffer overrun in the working-folder function.

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
  Black .................. bookmark this page (on a bookmarked page or on
                           the bookmarks page: remove a bookmark)
  Start .................. menu (bookmarks, history, find, reader mode,
                           screenshot, settings)
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
  JavaScript is experimental and off by default, and very dynamic modern
  sites (YouTube, social networks, Google) do not work even with it.
  Simple sites, text-only news, Wikipedia, forums and DuckDuckGo work
  well (see the home page).
  If the Xbox clock is far off, some HTTPS sites may show a certificate
  warning (you can continue with "Proceed").
  No video or audio.

LICENCES
  NetSurf (GPLv2), mbedTLS (Apache 2.0), expat (MIT), libwebp (BSD),
  QuickJS-ng (MIT), stb_sprintf (public domain), nxdk (MIT), DejaVu
  fonts. See "licenses".
  NetSurfX port by XadPT.
