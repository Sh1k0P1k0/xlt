# Crash log

When a game, an app, a plugin or XLT itself crashes, the `xltcrash` plugin writes
what the PSP knew at that moment to `SEPLUGINS/xltcrash.log`. Nothing is written
while nothing crashes.

Settings › XLT › Crash log, on by default. XLT carries the plugin, writes it to
`SEPLUGINS/xltcrash.prx` and adds two PLUGINS.TXT lines, `game` (games and
homebrew, XLT too) and `pops` (PS1 games), at every start (`src/crashplug.c`).
Off switches the lines off. Not loaded in the XMB.

## What an entry holds

```
=== crash 2026-11-24 08:10:31 ===
running: ms0:/PSP/GAME/XLTCRASH/EBOOT.PBP (apitype 0x141)
thread: crash test thread (0x04a1c2b3, user)
what: address error on store (a bad or unaligned pointer) (exception 5)
at: 0x08804420 = XLTCrashTest+0x420 (the branch before it: the fault is in its delay slot)
bad address: 0x00000000
called from (ra): 0x08804450 = XLTCrashTest+0x450
zr 00000000  at ...   (all 32 registers, then hi, lo, status, cause)
calls on the stack (maybe also older ones):
  0x08804478 XLTCrashTest+0x478
```

- **at**: the module and the offset in its code. For XLT or one of its plugins,
  `psp-objdump -d xlt.elf` (or the plugin's `.elf`) at that offset shows the place.
- **calls on the stack**: values on the crashed thread's stack that are return
  addresses of calls. Old values left on the stack can show up too.
- The log is renamed to `xltcrash.old.log` past 64 KB.

## How it works (`plugin/xltcrash`)

`catch.S` is registered as the kernel's default exception handler, which runs
when nothing else handles an exception. In the exception it saves the registers
in one of four slots, then moves the crashed thread into kernel mode the way
PSPLINK does: on the thread's kernel stack, with the system-call frame the thread
manager expects, interrupts off. Then it calls `xltcrash_trap()`, which turns
interrupts back on, wakes the writer thread and puts the crashed thread to sleep
for good. (The first try, returning with `eret` into C on a stack of its own with
no system-call frame, froze the PSP at the next thread switch until it switched
itself off.) The crashed thread stops where it would have stopped anyway,
and the rest of the system goes on as before. The writer formats the entry and
appends it with ordinary file calls (never the raw Memory Stick).

Limits:
- Four crashes per load. After that, crashes go to the handler that was there
  before.
- A crash while the crashed thread holds the Memory Stick driver's lock can't be
  written (the writer would wait for that lock forever).
- If the handler can't be registered, that is the one thing written at start.

## Testing

`tests/psp_crashtest`: `make`, then copy `EBOOT.PBP` to `PSP/GAME/XLTCRASH/`. It
crashes on purpose in a thread of its own (X, □, △, ○: four kinds) and shows the
end of the log a second and a half later. SELECT crashes its main thread instead.
