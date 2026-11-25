# System screens

[← Developer docs](Home.md)

## Settings

The **Settings** card has a row like the others: the three sections used last (the one you go into moves to
the front, saved as `settings_recent` in `cache/xlt.ini`), then **All**. ✕ on a section opens Settings right
in it; **All** opens it where you left off. Sections on the left, their settings on the right, and what the focused one does
underneath. →/✕ enter a section, ←/→ change a value (✕ steps it forward or runs an action), L/R switch
section, ○ goes back (`src/settings.c`). Off/On settings are drawn as a switch (← off, → on, ✕ flips it, the
knob slides); an ARK setting that is on for some software only shows that next to the switch ("games").

| Section | Settings | Stored in |
|---|---|---|
| XLT | Theme, Arrange home menu, Play the next video, Start XLT at boot, default ISO driver, removed entries, rescan, clear play history, button test | ARK's `launcher` key; `cache/xlt.ini` |
| Games | CPU speed (Auto…333, ARK-5 up to 443 MHz), extra memory, Memory Stick cache, Inferno ISO cache, skip logos, UMD region, Disable Pause Game (Go) | `SETTINGS.TXT` |
| Plugins | Every global line of ARK's `PLUGINS.TXT` files: Off, or where it loads (Always, All games, PSP games, Homebrew, PS1 games, XMB, Launcher, as arkMenu's runlevels); old plugin support (Go) | `PLUGINS.TXT` |
| Power & Display | Screensaver (Off, 1, 2, 5, 10 min) and Show screensaver, USB charging, backlight auto-off, auto sleep, LEDs off, keep the paused game (Go) | `cache/xlt.ini`, `SETTINGS.TXT`, registry |
| Sound & Controls | Interface sounds, equalizer, even out music volume, AVLS, enter button (✕/○), disable analog stick | registry, `SETTINGS.TXT` |
| Network | WPA2, ad hoc channel, Wi-Fi power save, hide MAC address | `SETTINGS.TXT`, registry |
| System | Language, date format, time format, time zone, summer time, disable UMD drive | registry, `SETTINGS.TXT` |
| About | Model, system software, ARK version, nickname, check for updates, Memory Stick free and used space | `cache/storage.txt` |

- **ARK settings** are read and written like ARK's own arkMenu (`src/arkcfg.c`, host-tested in
  `tests/test_arkcfg.c`), in ARK-5's or ARK-4's dialect, and also to `flash1:/SETTINGS.TXT` when ARK-5 is
  installed there. A setting keeps the runlevel it was on (shown as e.g. "On (games)"). Per-game lines,
  comments and unknown keys are kept. They're written when Settings closes; ARK applies them the next time a
  game starts.
- **Left out on purpose:** settings that only change the XMB (the XMB's CPU speed, hidden game pictures and
  DLC, QA flags, the XMB region, Vita-only keys). Their lines are written back unchanged.
- **Plugins:** only lines for a runlevel (`always`, `game`, `vsh`, `pops`, …) are listed; per-game lines are
  left as they are. Only the runlevel and on/off fields of a line are rewritten (Off keeps the line's
  runlevel), and only if the line still holds the same plugin when Settings closes. A line whose file isn't on
  the stick is not shown (here or in a game's options), looked up as ARK does: a path with a device as written,
  `/...` on `ms0:`, a bare name like `leda.prx` in ARK's folder. The file itself is left as it is, so the entry
  comes back with its plugin.
- **PSP settings** live in the system registry and are read and written through the kernel's `sceReg`
  functions (found by ARK, called through kubridge; `src/psreg.c`). A change is written at once and read back;
  most apply fully after a restart (AVLS at once). A setting whose registry key can't be read isn't shown.
- **Start XLT at boot (ARK's launcher):** ARK's `launcher` setting starts whatever is at
  `<ARK folder>/VBOOT.PBP`, which is ARK's own menu unless something replaces it. So while the setting is on,
  XLT makes itself that file (`src/arklauncher.c`): ARK's menu is kept as `VBOOT_ARKMENU.PBP` (another launcher
  that was there as `VBOOT_OLD.PBP`), XLT's EBOOT is copied in through a temporary file, and `VBOOT_XLT.TXT`
  notes where the XLT it came from lives and its size and date, so an updated XLT is copied again and an
  unchanged one costs two file dates at start. That check runs only after the boot screen, so even a copy
  can't stall the animation. Started as `VBOOT.PBP`, XLT keeps using its own folder for its library and
  settings, and doesn't list itself as homebrew. Turned off, ARK's menu goes back.
- **Leaving to the XMB as the launcher:** ARK would only start XLT again, so XLT first turns the setting off
  (and ARK's copy in memory, when it is set) and leaves `cache/launcher.paused`; at XLT's next start it is
  turned back on (psp5's approach). Until then the PSP boots into the XMB. Checked on ARK-5, where only
  `SETTINGS.TXT` counts. `cache/scan.log` says how XLT was started and what it did.
- **Free and used space (About):** the count is the PSP driver's own. It walks the whole FAT inside the driver
  and holds the system for ~12 s on a 128 GB card the first time after XLT starts; the driver keeps it, so
  later counts are instant. So the first time About's rows are entered, a dialog asks "Measure free space?" and
  says how long the PSP will pause (estimated from the card's size): ✕ Measure, ○ Not now. While it scans, a
  "Scanning the Memory Stick, please wait" dialog stays on screen, then what it found shows (✕ OK, or it goes
  by itself). The Memory Stick row measures again with ✕. Scrolling past About doesn't ask; it shows the saved
  values. `cache/scan.log` says how long it took.

  Up to 1.3.0, XLT read the FAT itself from the raw partition (`msstor0p1:`) to keep running meanwhile. Doing
  that while the PSP's FAT driver was writing (a screen recording being saved) once wrote a copy of the boot
  sector over `VIDEO`'s entries, so 1.3.1 removed it: the raw partition is never read while the stick is in use.
- **HOME:** XLT shows its own "Quit XLT?" dialog in place of the system's (`src/quitdlg.c`): Yes (where the cursor
  starts, so HOME then ✕ quits as before) leaves to the XMB the way the system's Yes did, No, ○ or HOME again
  closes it. The system's dialog is switched off with `sceImposeSetHomePopup` (6.60+ NID `0xC08C41EF`) and HOME is
  read through the kernel. The main loop tells a watchdog thread it is alive every frame: if XLT stops answering
  for 3 s the system's dialog comes back, so HOME still quits a stuck XLT. It is turned back on before a game or
  app starts, before a restart and when XLT quits. If it can't be turned off, the system's dialog stays and
  XLT's never opens (`cache/scan.log`: `HOME: XLT's own dialog ...`). The button test keeps HOME as a button.
- **Button test (XLT):** a PSP drawn on screen (`src/btntest.c`) where every button lights up while held and
  stays marked once pressed ("9 of 17 buttons pressed": the d-pad, the face buttons, L, R, SELECT, START, HOME,
  the volume keys, NOTE and SCREEN), with the analog stick's position, the box it has reached so far and its
  raw X/Y (128 is the centre: a stick at rest far from it drifts), and the HOLD switch, Wi-Fi switch (not on the
  E1000), headphones and UMD (not on the Go). HOME, NOTE, SCREEN and the volume keys are read through the
  kernel (`sysctl_kernel_buttons`). Every press stays in the tester; holding ○ for 1.5 s leaves it.
- **Check for updates (About):** ✕ joins Wi-Fi (the connection used last) and asks GitHub's API for the latest
  release of this repository (`src/update.c`). If its tag is newer than `src/version.h`, the dialog shows the
  version, the download size and the release notes (↑/↓ scroll); drafts and pre-releases are never offered.
  **Install** downloads the release zip to `cache/update.zip`, checks it against the SHA-256 GitHub lists for the
  file, unpacks `PSP/GAME/XLT/EBOOT.PBP` from it as `EBOOT.NEW` (it has to be a PBP of the size the zip says),
  renames the running `EBOOT.PBP` to `EBOOT-<old version>.PBP` and the new one to `EBOOT.PBP`, and refreshes
  ARK's `VBOOT.PBP` copy when XLT starts at boot. **Restart now** starts the new XLT; **Later** keeps the old one
  running until the next start (the row says "Restart to finish"). ○ stops a check or a download; nothing on the
  stick changes until the download is complete and checked. Going back is renaming `EBOOT-<old version>.PBP` to
  `EBOOT.PBP`. Each step goes to `cache/web.log` (`update: ...`). The JSON parsing, version order and notes
  are in `src/updparse.c` (host-tested).

## Control center (SELECT)

A sheet drops from the top, over a dimmed screen. Tiles that are on are filled blue and have a white dot;
the focused one has a ring.

- **Wi-Fi:** shows the hardware switch. It can't be switched in software.
- **USB:** USB mass storage. It stops the scanner and the thumbnail loader first, so nothing touches the Memory Stick
  while the PC has it. Turning USB off rescans the library.
- **Capture:** closes the sheet, then saves the screen as a PNG in `PICTURE/` (`src/shotpng.c`). L + R + START
  does the same from anywhere in XLT, without opening the sheet.
- **Sleep**, and **Power** (Shut down / Restart / Exit to XMB).
- **Now playing:** a card with the cover, title, artist · album and progress (the times sit in fixed slots, so
  the bar doesn't move). ✕ pauses, ←/→ change track, L/R seek 10 s, △ opens Now Playing and □ sets the sleep
  timer. With nothing playing it is a slim row: ✕ carries on with the last song, or opens the Music list if
  there is none. While a video is open the row says "Music is off while a video plays" and starts nothing,
  because the video player has the decoder and the sound.
- **Volume:** the system volume, 0–30, the same one the volume buttons change. ←/→ (or L/R) adjust and unmute,
  ✕ mutes. It is read and written through the kernel's `sceImposeGetParam`/`sceImposeSetParam` (found by ARK,
  called through kubridge), and read back after each change, because AVLS can cap it. While the sheet is open
  the slider follows the volume buttons. While AVLS is on (`sceImpose` param `0x10`) the level stops at 20. The pill
  and the slider look as always; volume up at 20 (the button, or → on the slider, which stops there too) shows
  the note "AVLS is on: the volume can't go past 20". The button changes nothing there, so XLT watches it
  itself (`sceCtrl_driver`, a kernel call, as psp5 reads HOME).
- **Brightness:** 0–100 % in 5 % steps (←/→, held to repeat). It maps linearly onto the panel's usable
  levels 25–99, so 0 % is dim but still lit, and 99 is the practical maximum on a stock PSP-1000/2000
  screen (see [BetterBright](https://github.com/hobbo91/BetterBright)). Saved as `brightness_pct` in
  `cache/xlt.ini` and restored at boot. Like BetterBright, XLT re-applies the level for a few seconds
  after boot and after waking from sleep, because the firmware briefly puts back its own stock level.

The hints at the bottom follow the focused row, and the screen's own hint bar is hidden while the sheet is
open.

Kernel calls (brightness, loading the USB modules) go through ARK's `sctrlHENFindFunction` and kubridge, the
same way psp5 and arkMenu do them.

## Volume OSD

Pressing the volume buttons (or mute) pops up a pill under the status bar with the level, for 1.5 s after the
last change (`src/osd.c`, polled every other frame). It doesn't appear for changes made with the control
center's slider, or while the control center is open.

## USB mode

(For sending files while XLT keeps running, see USB sync below.) Turning USB on (control center) hands the
Memory Stick to the computer, so XLT shows one full screen
instead of the launcher, as the XMB does: "USB mode / Connect the USB cable" until the PC mounts it, then
"USB connected" (polled twice a second). Nothing underneath is drawn, no icons or previews are loaded, and
every button except ○ is ignored (SELECT says to leave USB mode first). ○ turns USB off and the library is
rescanned. Screenshots (L+R+START) are refused while USB is on, since writing the PNG could corrupt the
stick (`src/cc.c`, `cc_usb_*`). A screen recording is saved before USB mode starts, and none can be started
while it's on.

## USB sync

Settings > XLT > USB sync lets a computer send, rename and delete files while XLT keeps running: XLT's own
USB device (the kernel module `plugin/xltsync`, loaded by `src/usbsync.c`) takes file commands from XLT Tool
or `tools/xltlink.py`, and the PSP's own FAT driver does the writing, so the stick never leaves XLT. Files
arrive as `NAME.xltpart` and are renamed only once complete and their CRC-32 matches; only XLT's folders can
be changed (and save folders in `PSP/SAVEDATA`, for XLT Tool's save backups); names are written in Shift-JIS, as
the PSP keeps them. Whole folders can be deleted in one command, and free space comes from the PSP driver's own count (kept
for a minute and moved with sync's own changes). The library rescans by itself after the
last change, a USB icon and "From PC 45%" show in the status bar, and USB mode pauses sync while it is on.
64 MB writes at 8.3 MB/s (USB mode: 9.6). How it works, and why it differs from USB mode:
[USB sync, in depth](USB-Sync.md).

## Power manager

XLT keeps the PLL and the bus at 222/111 MHz and changes only the CPU's clock (`scePowerSetCpuClockFrequency`,
11 µs, no frame lost; a PLL change stalls a whole frame, which made the screen jump). The CPU runs at 111 MHz
after a second with nothing to draw (screensaver redraws don't count) and at 222 as soon as something moves or
work runs. The music player asks for 222 only when it needs it: it times every block it makes against its
playing time, and a block over half of it (or the output running dry) asks for ~5 s at 222, a start, skip or seek
for ~1 s. MP3 and AAC play at 111 with the screen idle; FLAC stays at 222 by this rule. The video player keeps 333.
`cache/scan.log` gets a line every 5 minutes.

An ARK `cpuclock` line such as `game, cpuclock:333, on` also covers homebrew and would lock XLT's clock, so XLT adds
an off line of the same speed for itself at start and after Settings writes `SETTINGS.TXT` (ARK-5 clears a clock
only with an off line of that speed).

Wi-Fi: after 2 minutes with no page, picture or download, the connection is dropped and the network modules
unloaded; the next page joins again.

## Internet over USB

**Settings → Network → Internet over USB** (`xlt_net` in `xlt.ini`, from XLT's next start) uses
[USBNet](https://github.com/chriopter/pspkit-usbnet) by chriopter (MIT): the PSP's network goes through the cable,
with XLT Tool running USBNet's gateway on the PC, and the PSP connects to **Hi-Speed USB**. XLT doesn't carry
USBNet itself (`src/netplug.c`):
- a USBNet already installed (PSPDX's `seplugins/usbnet/usbnet.prx`, or `seplugins/usbnet.prx`) is never
  overwritten; its line is switched in place the way PSPDX does (`off` ↔ `on `);
- otherwise XLT Tool sends USBNet's release over sync to `PSP/GAME/XLT/USBNET/` (sync can't write `SEPLUGINS`),
  and XLT installs it where PSPDX puts it, with PSPDX's line.

Sync and USBNet share the cable: a connection over Hi-Speed USB holds sync off first, sync never stops the bus
while USBNet's driver is on it (that froze the PSP at exit), and closing the browser drops a USBNet connection
so sync comes back.

## Screenshots in games

Inside XLT, L+R+START is XLT's own screenshot. In games it is **xltshot**, a small kernel plugin
(`plugin/xltshot/`) that XLT carries inside its EBOOT and installs itself (`src/shotplug.c`):

- **Installing:** at every start, on the background worker, XLT writes `SEPLUGINS/xltshot.prx` (on `ms0:`, or a
  PSP Go's `ef0:` without a card) if it is missing or isn't the one this XLT carries, so updating XLT updates
  the plugin. If no `PLUGINS.TXT` mentions it yet, other screenshot plugins that load in games are turned off
  (recognised by name: `screenshot`, `sshot`, `prxshot`, `screencap`, …; XMB-only ones are left on) and
  `game, ms0:/SEPLUGINS/xltshot.prx, on` is added to `SEPLUGINS/PLUGINS.TXT`, with a note on screen. From then on
  the lists are the user's: turned off in Settings → Plugins, it stays off. The files' other lines stay byte for
  byte (`src/plugtext.c`, `tests/test_plugtext.c`).
- **Taking one:** the plugin watches the buttons about 50 times a second (`sceCtrl_driver`). While L and R are
  held it hides START from the game (`sceCtrlSetButtonIntercept`), so L+R+START doesn't also pause it. On the
  press it suspends the game's user threads, reads the frame on show (`sceDisplayGetFrameBuf`; 565, 5551, 4444
  or 8888), writes `PICTURE/<game ID>_<date>_<time>.png`, draws a white edge for a moment and lets the game go on.
- **Memory:** the PNG writer (`plugin/xltshot/pngout.c`, host-tested by `tests/test_pngout.c`) streams rows
  through zlib with a 4 KB window, in a 48 KB block taken only for the shot (kernel partition, else user).
- **Inside XLT:** started from the XMB, XLT may count as homebrew and get the plugin too; it sees XLT's module
  and stays out of the way.
- **Play time:** it also counts how long each game runs, leaving out sleep (a gap of more than 2 s between its
  once-a-second looks), into `SEPLUGINS/xltplay.bin`: one record per session of 30 s or more, its seconds
  rewritten every 30 s, so a crash or a power-off loses at most that (`plugin/xltshot/xltplay.h`). At its next
  start XLT adds the records to its play history (`cache/history.bin`, games started from the XMB too) and deletes
  the file. The details line then reads "12 h 40 min · played yesterday", and Games and Homebrew can be sorted
  by **Most played**. PS1 games count too: XLT adds a `pops` line for the plugin to
  `SEPLUGINS/PLUGINS.TXT` once (turned off or removed later, it stays that way); there it only counts, and
  as the PSP doesn't tell a plugin which PS1 EBOOT runs, XLT credits the time to the game it launched just
  before (a PS1 game started from the XMB isn't counted). xltshot's log is kept across loads, up to 16 KB.
- **For XLT Tool:** `cache/games.txt` lists the games and apps with their play history (kind, path, ID, last
  played, seconds played, favorite, title; `src/gamesfile.h`), written at start and after a scan that changes
  the library. XLT Tool reads it over USB sync.
- **Log:** `SEPLUGINS/xltshot.log` (what it found at start, each shot with its time and frame format); XLT's
  `cache/scan.log` says what it installed or turned off.

## Screen recorder

L+R+SELECT, from anywhere in XLT, starts recording what XLT shows; L+R+SELECT again stops it. The file goes
to `VIDEO/xlt_YYYYMMDD_HHMMSS.avi` on the stick XLT runs from: picture only (no sound), 480×272 MJPEG at up to
30 frames a second, which plays on a PC as it is (XLT's Video card doesn't list `.avi`). While it records,
a pill at the top right shows a red dot and the elapsed time (it is in the recording too).

- **Cheap when nothing moves:** the UI thread copies a frame from the display only when a new one was drawn and
  the encoder is free. A thread below the UI turns it into a JPEG (libjpeg, quality 70) and writes it; the
  frames in between are written as empty "same picture" chunks. Time is counted in vblanks, so a busy screen
  lowers the frame rate but never the speed: a minute stays a minute.
- **Saved, not lost:** the recording is finished and closed before a game or homebrew starts, before USB mode,
  when XLT exits, and when the PSP goes to sleep (the power callback runs before the Memory Stick goes away). A
  toast says where it went and how long it is.
- **Limits:** it stops by itself after an hour or before the file reaches 1000 MB, and if the stick fills up
  (the index is still written if it fits).
- **Video too:** a video playing full screen is recorded like anything else XLT shows. A JPEG takes about
  95 ms on the PSP, so a moving screen gets about 10 new pictures a second: the time stays right, the motion
  is choppier than the original.

Each recording adds a line to `cache/video.log` with its length, pictures and frames, the average and slowest
JPEG time and the slowest write (`src/recorder.c`, `src/avi.c`, `src/recjpeg.c`).

## Screensaver

After 2 minutes without a button press (**Settings → Power & Display → Screensaver**: Off, 1, 2, 5 or 10 minutes,
saved as `saver_minutes` in `cache/xlt.ini`), a lock-screen-style clock fades in over whatever is open
(`src/saver.c`, drawn by `src/saverdraw.c`). **Show screensaver** in the same section starts it at once.

- **What it shows:** the date over a large clock, in the PSP's own time format (24- or 12-hour, with AM/PM) and
  date order (month first for MM/DD/YYYY), both read from the registry each time it starts; the playing song in a
  pill under the clock (dimmed while paused); the battery in the status bar's corner. "Press any button" shows for
  the first few seconds.
- **Animation:** the theme's background with two soft lights (the accent blue and a violet mixed from it) drifting
  on slow Lissajous paths (40–90 s loops) and breathing a little; the clock's colon breathes over two seconds.
  It fades in over 0.8 s with the face settling from a few pixels below, and on wake fades out in a quarter of a
  second while lifting away.
- **Waking:** any button. That press, and its auto-repeat until every button is up, isn't passed to the screen
  underneath. Waking from sleep also closes it. The volume buttons only show the volume pill over it.
- **Power:** it redraws 30 times a second (nothing underneath is drawn) at the UI's 222 MHz; previews pause. Once
  it is fully in, the backlight eases down to half your brightness level over a second and comes straight back
  on wake, except when the firmware's backlight auto-off is due by then (setting a level could light the panel
  again). It doesn't start during full-screen video, and the time spent watching doesn't count as idle.
- **The firmware's own dimming:** the PSP dims the panel itself after about a minute idle (level 32), turns it
  off at the backlight auto-off, and lights it again at 32 on a press it keeps from XLT. The screensaver doesn't
  dim a panel the firmware has already dimmed, wakes when the firmware relights the panel, and holds your level
  for a few seconds after any wake (also from sleep). A press outside the screensaver puts your level back after
  the firmware's dim, and a new dim never starts from a level an earlier one left behind
  (`sysctl_brightness_last_set()`).
- **Drawing:** `tools/bake_saver.py` bakes the digits (Noto Sans Light, 84 px, tabular, so the clock never shifts)
  into a 256×128 4444 texture, a 64×64 glow into an 8888 one (4-bit alpha showed rings when stretched over the
  screen), and a sine table, so the animation uses no floats. Both textures are read by the GE from main RAM.
