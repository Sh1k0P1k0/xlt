#!/bin/sh
# Fixtures for tests/test_gapless.c: songs of known length (in frames per channel)
# from LAME and ffmpeg, whose padding info has to give that length back.
set -e
out=${1:-build/gapless}
mkdir -p "$out"
cd "$out"
python3 -I - <<'PY'
import math, struct
def wav(name, rate, ch, n):
    d = b''.join(struct.pack('<' + 'h' * ch, *([int(8000 * math.sin(i * 0.05))] * ch)) for i in range(n))
    open(name, 'wb').write(b'RIFF' + struct.pack('<I', 36 + len(d)) + b'WAVEfmt ' +
                           struct.pack('<IHHIIHH', 16, 1, ch, rate, rate * ch * 2, ch * 2, 16) +
                           b'data' + struct.pack('<I', len(d)) + d)
wav('a44.wav', 44100, 2, 44100 + 777)
wav('a48.wav', 48000, 2, 48000 * 2 + 31)
wav('a22.wav', 22050, 1, 22050 + 5)
PY
lame --quiet -b 128 a44.wav lame44.mp3
lame --quiet -V 2 a48.wav lame48v.mp3
lame --quiet -m m -b 64 a22.wav lame22mono.mp3
lame --quiet -m m -b 32 a22.wav lame22tiny.mp3
lame --quiet -b 128 --tt Song --ta Artist --add-id3v2 a44.wav lametag.mp3
ffmpeg -loglevel error -y -i a44.wav -c:a libmp3lame -b:a 128k ffmpeg44.mp3
ffmpeg -loglevel error -y -i a44.wav -c:a aac -b:a 128k ffmpeg44.m4a
ffmpeg -loglevel error -y -i a48.wav -c:a aac -b:a 128k ffmpeg48.m4a
rm -f a44.wav a48.wav a22.wav
