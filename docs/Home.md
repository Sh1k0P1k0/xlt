# XLT developer docs

These pages are for anyone who wants to read or change XLT's code: how each part works, the firmware
calls it relies on, what was measured on hardware, and how it is tested. For what XLT is and how to use it,
see the [README](../README.md).

They are written to work as a GitHub wiki too: copy the `.md` files into the wiki unchanged.

| Page | What it covers |
|---|---|
| [Look and sound](Look-and-Sound.md) | The logo, the XMB icon, background and preview (and the PMF muxer), themes, the two layouts (Classic and nMB), interface sounds |
| [Start-up](Startup.md) | The boot screen and library check, safe start |
| [How XLT is built](Architecture.md) | Threads, the atlas, drawing, memory, the VRAM layout |
| [Library and games](Library-and-Games.md) | Scanning and the index, launching through ARK, the options sheet, folders, removed entries, the home menu order, the UMD drive |
| [System screens](System.md) | Settings (ARK, plugins, the registry, free space, update check, button test), the HOME dialog, the control center, volume and AVLS, USB mode, USB sync, screenshots and play time in games (the xltshot plugin), the screen recorder, the screensaver, the power manager, internet over USB |
| [Crash log](Crash-Log.md) | The xltcrash plugin: what an entry holds, how it catches a crash, its limits |
| [XLT Cast](Cast.md) | The xltcast plugin: turning it on, its USB device, copying frames after the GE, the game's sound, what was measured |
| [USB sync, in depth](USB-Sync.md) | XLT's own USB device: the protocol, why it differs from USB mode, Windows and Linux access, updating XLT over sync, speeds |
| [Music, video and pictures](Media.md) | The music player (MP3, AAC, FLAC, levelling, equalizer), the video player (MP4, PMF, subtitles, the weighted-prediction stripper), pictures, live previews |
| [Files, save data, reader, web and search](Files-and-Apps.md) | The file manager, ZIP, save data, the reader (text, PDF) and text editor, the web browser, search |
| [Building and testing](Building-and-Tests.md) | Building, the host tests and screen previews, the demo reel |
| [License and credits](Credits.md) | License, code adapted from other projects, libraries and references |
