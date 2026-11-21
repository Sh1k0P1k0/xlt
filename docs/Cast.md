# XLT Cast

XLT Cast sends the game being played on the PSP, picture and sound, to
[XLT Tool](https://github.com/boubounokefalos/xlt-tool) on the PC over the USB cable, where it plays live in a
window. The PSP side is **xltcast**, a game-runlevel kernel plugin (`plugin/xltcast/`) that XLT carries inside
its EBOOT and installs, like xltshot. XLT Tool's side is described in its
[XLT Cast](https://github.com/boubounokefalos/xlt-tool/blob/main/docs/Cast.md) page.

## Turning it on

**Settings → XLT → XLT Cast** (`xlt_cast` in `xlt.ini`, off at first). At every start and when the setting
changes, XLT writes or updates `SEPLUGINS/xltcast.prx` (`src/castplug.c`, from `build/xltcast_data.h`) and
adds or switches its lines in `SEPLUGINS/PLUGINS.TXT` (`plugtext_switch_named`, host-tested in
`tests/test_plugtext.c`). ARK copies plugins at boot, so a new plugin takes over after XLT has started twice.

In a game, the plugin waits with USB off. **L+R+SELECT** turns its USB device on and casts; L+R+SELECT again
turns it off (SELECT is hidden from the game while L+R are held). **HOME** turns it off at once, because USB must be
off before a game quits: with it on, quitting can hang at "Please wait". `sceKernelExitGame` and
`sceKernelExitGameWithStatus` are patched to turn it off first too, since a quitting game never calls the
plugin's `module_stop`. It doesn't start in XLT itself or in ARK's launcher.

## The USB device

The same IDs as XLT's USB sync (054C:01CB), so the Linux udev rule and Windows' set-up cover both, but its own
interface: subclass 0x58, protocol `C` (0x43), where sync is `S` (0x53). The PC clients check the protocol before
opening the device. MS OS 1.0 and 2.0 descriptors (`bcdUSB` 0x201, interface GUID
`{6F1C2E3A-9B4D-4E57-8A60-C7D2B5E19F48}`) make Windows give it WinUSB by itself. The bus driver functions are
found by NID (`sctrlHENFindFunction`), as in the xltsync plugin.

Every message starts with a 32-byte header (`XLTC`, op, status, sequence, length, four values), protocol
version 3. The PC sends `START` (mode, every K-th frame) and `STOP`, and can ask for the stats (frames, flips,
copy and send times, waits).

## Frames

A watcher thread waits for each vblank and reads the display's frame buffer (`sceDisplayGetFrameBuf`): a new
address is a new frame (a flip). It runs only while casting, at priority 0x20 (0x16 in homebrew), and sleeps a
frame if a wait returns in under 4 ms (once a game quits, the wait can return at once and a loop on it starves
the quit). The sender thread (0x30, 0x18 in homebrew) then:

1. waits until the GE says it has finished drawing (`sceGeDrawSync(1)`: only a question, nothing is queued),
   for at most 12 ms. Some games show a buffer before the GE is done with it: without this, about one frame in
   14 of NFS Most Wanted came out with only the road drawn. With it there were none, and the game kept 29.9 of
   30 fps (40 waits of 1.2 ms in 20 s);
2. copies the whole frame to a 255 KB buffer, reading VRAM through the cache with its lines dropped first:
   4.5 ms for a 16-bit frame, 10 to 13 ms for 32-bit, which is turned into 565 on the way (USB 2.0 can't carry
   32-bit frames at 30 fps). Reading uncached took 19 ms;
3. drops the frame if the game showed another meanwhile (it may have drawn over this one), else sends it.

If 255 KB can't be had, it copies and sends in 64-row strips instead. Copy memory is taken only while casting.

What didn't work, and must not be tried again: the USB controller reading the frame buffer itself (the bus driver
refuses VRAM addresses, 80243002), `sceDmacMemcpy` from VRAM, and a GE transfer job queued beside the game's
(both froze the PSP at once). Pacing the frames evenly made no visible difference.

## Sound

`plugin/xltcast/audio.c` hooks the game's 15 `sceAudio` calls (`sctrlHENPatchSyscall`) from the game's start,
noting each channel's set-up. While casting, it mixes what every channel plays into a ring of 8192 stereo
frames at 44.1 kHz on the PSP's clock, and sends it as `AUDIO` messages of up to 2048 frames beside the
picture. The game's own sound is untouched.

## Homebrew

Homebrew ports that use the whole PSP (NFSU2VIEW, 32-bit colour) starved the USB stack's own threads, so in
homebrew the watcher and sender run at higher priorities while casting (about 20 fps there).

## Measured on a PSP-E1004

| Game | Colour | Copy | PC | The game on the PSP |
|---|---|---|---|---|
| Need for Speed Most Wanted, race | 565 | 4.5 ms | 30 fps | 30 → 29.9 fps |
| God of War: Ghost of Sparta | 8888 | 9.9 ms | 30 fps | 33 → 30 fps |
| NFSU2VIEW (homebrew) | 8888 | | ~20 fps | |

A PSP-3000 casts at 30 fps with sound too. Games that use USB themselves (camera, GPS) can't be cast. PS1 (POPS) games aren't supported yet: the plugin
is only listed for PSP games and homebrew (`game`), not for POPS (`pops`).

## Testing it from the PC

`tools/xltcast.py` (copied into XLT Tool, keep the two the same): `test` compares the game's fps with and
without casting and prints the stats, `frames` saves frames as PNGs, `record` saves the sound as a WAV, and
`view` is a bare viewer. The plugin writes `SEPLUGINS/xltcast.log`.
