# USB sync: how it works, and how it differs from USB mode

XLT can connect to a computer over USB in two ways:

- **USB mode** (the control center's USB tile): the PSP's own mass storage mode,
  the one the XMB has. The computer sees the Memory Stick as a USB drive.
- **USB sync** (Settings > XLT > USB sync): XLT's own USB device. The computer
  sends, renames and deletes files through XLT, which keeps running.

Both use the same cable and the same port, but they work in opposite ways.

## The short version

| | USB mode (mass storage) | USB sync |
|---|---|---|
| What the computer sees | a USB drive (sectors) | a device that takes file commands |
| Who writes the Memory Stick | the **computer's** FAT driver | the **PSP's** FAT driver, always |
| XLT meanwhile | a full screen "USB connected", nothing else works | keeps running: music, video, games list, everything |
| The library | rescanned when USB mode ends | rescanned by itself 1.5 s after the last change |
| File names | the computer writes them its way (a name with a character the PSP can't hold becomes unreachable) | always written so the PSP can hold them (Shift-JIS) |
| What the computer can touch | the whole stick, including system folders | only XLT's folders: ISO, VIDEO, MUSIC, PICTURE, BOOKS, DOWNLOADS, PSP/GAME, and save folders in PSP/SAVEDATA |
| A file cut off halfway | a half-written file is left | nothing is left: the file only appears once it is complete and its checksum matches |
| Software on the computer | none: every OS mounts USB drives | XLT Tool, or `tools/xltlink.py` (and on Linux, one udev rule) |
| Speed, writing 64 MB | 9.6 MB/s | 8.3 MB/s |

## Why USB mode has to take the stick away

A USB drive is a block device: the computer reads and writes numbered sectors and
keeps its own picture of the file system (the FAT tables, which clusters are free,
which directory entries exist). The PSP's FAT driver keeps such a picture too.

If both wrote to the same stick at once, each would overwrite the other's changes
without knowing it: two "free" clusters handed out twice, a directory entry
written over another. That corrupts the file system. So while the computer has the
stick, the PSP must not touch it at all, and XLT can't either: no library, no
music from the stick, no thumbnails, no screenshots. That is why USB mode is one
full screen, in the XMB as in XLT.

## Why sync doesn't need to

With sync, the computer never sees sectors. It sends requests such as "here is a
file called VIDEO/film.mp4, 700 MB" or "delete MUSIC/old.mp3", and XLT carries
them out with the PSP's normal file functions (`sceIoOpen`, `sceIoWrite`,
`sceIoRename`...). The PSP's FAT driver stays the only thing that writes the
stick, so there is nothing to conflict with, and XLT goes on using the stick as
before, while the computer sends to it.

The price is that the computer needs a program that speaks XLT's protocol (XLT Tool,
or `tools/xltlink.py`), and that the PSP's own CPU does the file writing, which is
a little slower than letting the computer write sectors (below).

## The pieces

```
  computer                          PSP
  ─────────                         ───────────────────────────────────────────
  XLT Tool / xltlink.py  ──USB──▶  xltsync.prx (kernel)  ──sceIo──▶  Memory Stick
  (pyusb + libusb, WinUSB)          │  status (xltsyncStatus)
                                    ▼
                                   XLT (src/usbsync.c): icon, progress, rescan
```

- **`plugin/xltsync/`**: a kernel module XLT carries inside itself, writes to its
  folder and loads while sync is on. It has to be kernel code: the PSP's USB bus
  driver API (`sceUsbbdRegister`, `sceUsbbdReqSend`, `sceUsbbdReqRecv`) is only
  available there. Its structure follows PSPLINK's `usbhostfs` driver (BSD
  licence).
- **`src/usbsync.c`**: XLT's side. It loads the module, starts and stops the USB
  driver, reads the module's state six times a second, and turns that into the
  status bar icon, the "From PC 45%" progress and the automatic rescan.
- **`tools/xltlink.py`**: the computer's side, a single Python file using pyusb.
  XLT Tool carries a copy (`xlttool/xltlink.py`).

## The USB device

- Vendor ID 054C (Sony, filled in by the PSP's USB stack), product ID **01CB**.
  The PSP's stack names it `"PSP" Type D` after that ID.
- One interface (class FF, subclass 58, protocol 53) with two bulk endpoints:
  0x81 (PSP to computer) and 0x02 (computer to PSP), 512-byte packets at USB 2.0
  high speed, 64 at full speed.
- **Windows** needs no driver to be installed: the device answers Microsoft's OS
  descriptor requests, so Windows binds its own WinUSB driver by itself.
  - *MS OS 2.0* (Windows 8.1 and later): the device says USB 2.01 and gives a BOS
    descriptor naming vendor request 0x58; that request returns the compatible ID
    `WINUSB` and the interface GUID `{AE1513B4-0AA3-4365-B951-3261C67CE994}`.
  - *MS OS 1.0* (Windows 7): string descriptor 0xEE and the compatible ID, as a
    fallback. Its GUID request can't be answered: the PSP's stack refuses
    interface requests (bmRequestType 0xC1) itself.
  - Requests the module doesn't know are refused at once by returning -1 from the
    control callback (`sceUsbbdStall` on endpoint 0 has no effect: the computer
    would wait for its timeout).
- **Linux** needs one udev rule (`tools/60-xltsync.rules`) so the logged-in user
  may open the device; XLT Tool offers to install it with one password prompt.
- **macOS** needs nothing.

## The protocol

Every command is a 20-byte header in its own transfer, then its data, if any, in
the next transfer(s). The PSP answers with the same header (bit 0x8000 set in the
command number), then data. All little-endian, as the PSP is.

```
magic "XLTS" (u32) · command (u16) · status (u16) · sequence (u32) · length (u32) · argument (u32)
```

| Command | Does |
|---|---|
| HELLO | protocol version (4), model, which storage there is (ms0:, a Go's ef0:), transfer size |
| LIST / STAT | a folder's entries, or one file's: name, size, date |
| MKDIR | a folder, with its parents |
| PUT, then PUT_END | send a file (below) |
| GET | read a file (logs, screenshots; and the PSP's fonts under `flash0:/font`, read only) |
| READ | a piece of a file, up to 256 KB (protocol 3): a game's PARAM.SFO and icons inside an ISO or EBOOT, without the whole file |
| POWER | the battery: percent, charging, on the charger, time left (protocol 4), for XLT Tool's status line |
| DELETE / RENAME | a file or an empty folder; a rename within the same device |
| DELETE_TREE | a folder and everything in it, done by the PSP in one command (a game's folder, a save) |
| DF | free space: the PSP driver's count, then kept for a minute and moved with sync's own changes (below) |
| ECHO, SINK, SOURCE, CTLLOG | tests: data integrity, raw speed both ways, the control requests seen |

The full definition is `plugin/xltsync/xltsync.h`.

### Sending a file

1. The computer sends PUT with the path, size and date. The PSP checks the path,
   makes the folders, opens **`NAME.xltpart`**, and answers with the transfer size
   to use (128 KB).
2. The computer sends the file in 128 KB transfers. On the PSP the module's buffer
   (256 KB of kernel memory) is two halves: while one fills from USB, the other is
   written to the stick in the background (`sceIoWriteAsync`) and a CRC-32 is
   taken of it.
3. The PSP answers with the CRC-32 of what it wrote and how long writing took.
4. The computer compares that with its own CRC and sends PUT_END: keep, or throw
   away. Kept, the `.xltpart` gets the file's date and is renamed into place
   (replacing an older file only if the computer asked for that).

Nothing else ever sees a half-written file: until step 4 it isn't under its name.
If the cable comes out, the computer stops, or anything fails, the `.xltpart` is
deleted.

### What the computer may change

Only XLT's folders: `ISO`, `VIDEO`, `MUSIC`, `PICTURE`, `BOOKS`, `DOWNLOADS` and
`PSP/GAME` (so XLT itself can be updated over sync), never the folders themselves.

**Save data** (protocol 2), so XLT Tool can back saves up and put them back with their
game: in `PSP/SAVEDATA` only a save folder (`PSP/SAVEDATA/ULUS10041DATA00`) and the files
directly in it. No files beside the save folders, no folders inside one, and nothing moves
in or out of `PSP/SAVEDATA`. A save folder can be renamed, which is how a save is replaced
without ever leaving half of one: the new one is sent under another name, the old one
renamed aside, the new one renamed into place, then the old one deleted.

**DELETE_TREE** refuses XLT's own folder (the one its log is in) and anything holding it.

`SEPLUGINS`, `PSP/SYSTEM` and everything else stay out of reach. Paths
with `..`, empty parts, characters FAT can't hold or broken Shift-JIS are refused
by the PSP itself, whatever the computer program does. Anything on the stick may
be listed and read.

### File names

The PSP's FAT driver takes and gives names in **Shift-JIS**, not UTF-8. A name in
UTF-8 is stored garbled (é becomes ﾃｩ on the computer), and a name with a
character Shift-JIS lacks comes back cut off at that character, so the file can't
be opened at all. That happens to files copied in USB mode too.

Over sync, names are always converted: characters Shift-JIS has are kept (Greek,
Cyrillic, Japanese, many symbols), the others become the nearest plain one
(`Café` → `Cafe`, `Καλημέρα` → `Καλημερα`, `–` → `-`). XLT itself makes names the
same way (`src/fsname.c`, tables from `tools/bake_sjis.py`, which takes its
replacements from `tools/xltlink.py`), so XLT and XLT Tool can never disagree.

The rules are `plugin/xltsync/syncpath.c`, checked on the PC by `tests/test_syncpath.c`.

### Free space

Free space is the PSP driver's own count (devctl 0x02425818). It walks the whole FAT
inside the driver and holds the PSP for ~12 s on a 128 GB card the first time after XLT
starts, then answers at once. The count is kept for 60 s and moved with every file sync
keeps or deletes; DF with argument 1 counts again. The reply's argument says which it
was (1 kept, 2 the driver's count; 0, counted from the FAT, only comes from older XLT).

The module used to read the FAT itself from the raw partition (`msstor0p1:`), about a
second instead of 12. That was removed (2026-11-21): reading the raw partition beside
the FAT driver's own writes wrote a copy of the boot sector over a folder's entries.

## What XLT does while sync is on

- **Status bar**: a USB icon while the computer has set the device up; lit while a
  program has talked to the PSP in the last 5 s. "From PC 45%" next to the title
  while a file comes in.
- **Library**: 1.5 s after the computer's last change (a file kept, deleted or
  renamed, a folder made), XLT rescans and says "Files from the PC".
- **USB mode** still works: turning it on pauses sync (one driver at a time on the
  port), turning it off brings sync back.
- **A cable that doesn't come up** (seen when sync starts with the cable already
  in): after 3 s the device is presented again; if that doesn't help either, XLT
  asks for a replug.
- **Sleep**: sync starts again on waking. **Games, restart, exit**: sync stops
  first; the game gets a USB port with nothing on it.
- **Logs**: the module writes `xltsync.log` in XLT's folder, XLT notes sync events
  in `cache/scan.log`, both with millisecond times on the same clock.
  `xltlink.py log` reads both over the cable.

## Updating XLT over sync

XLT Tool's menu has **Update XLT on the PSP…**: it reads the PSP's version from `cache/version.txt` in XLT's
folder (XLT writes it at start: the running version, and `next <version>` once one is installed), asks GitHub for
the latest XLT release, and sends either that (checked against GitHub's size and SHA-256, its EBOOT.PBP taken out
of the zip) or an EBOOT.PBP or release zip chosen on the computer:

```
PSP/GAME/XLT/UPDATE/RELEASE.TXT   the version on the first line, then the notes
PSP/GAME/XLT/UPDATE/XLT.PBP       the new EBOOT.PBP, sent last
```

XLT looks for `XLT.PBP` once a second while sync is on (and once at start). Since a file only appears once it is
complete and its CRC matches, finding it means it is all there. XLT then opens Settings → About with the update
dialog ("XLT 1.4.0 from XLT Tool"), or, with something full-screen in front, says so in a toast and on the About
row. Install does what the Wi-Fi update does (`src/update.c`): the running XLT is kept as `EBOOT-<version>.PBP`,
the new one takes its place, ARK's start-up copy follows. Any version is taken, the same or an older one too.
Installed or failed, `UPDATE/` is removed; **Later** leaves it for the next time.

## Speed

Measured on a PSP-E1004 (USB 2.0 high speed), 64 MB:

| | |
|---|---|
| USB alone, computer to PSP (nothing written) | 42 MB/s |
| USB alone, PSP to computer | 15 MB/s |
| Sync, writing a file | 8.3 MB/s |
| USB mode, writing the same file | 9.6 MB/s |

The Memory Stick is what limits both, not USB. Sync is a little slower because the
PSP's CPU runs the FAT driver and takes the checksum itself, where in USB mode the
computer writes sectors and the PSP only passes them on. Writing in the background
while taking the checksum brought sync from 6.8 to 8.3 MB/s.

## Limits

- The computer needs XLT Tool or `xltlink.py`; an ordinary file manager can't use
  sync. USB mode is there for that.
- One program at a time can hold the device (XLT Tool or `xltlink.py`, not both).
- Files of 4 GB or more can't be sent: FAT32 can't hold them anyway.
- On a PSP Go, the USB charging workaround USB mode uses is applied before sync
  starts too; this is untested, as is sleep while a file is coming in.
