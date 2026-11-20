/* xltcast - the game's sound, mixed for the PC (roadmap step C3). See audio.c. */
#ifndef XLTCAST_AUDIO_H
#define XLTCAST_AUDIO_H

#define AUDIO_TAKE_MAX 2048 /* stereo frames per audio message at most (8 KB) */

/* At the plugin's start, in games: patches sceAudio's calls. Returns how many (15 when all). */
int audio_hooks_install(void);
int audio_hooked(void);

/* Casting on: the mix's memory, the play hooks start adding into it. 0, or -1. */
int audio_start(void);
void audio_stop(void);

/* The sender: what is ready (44.1 kHz, 16-bit stereo), up to AUDIO_TAKE_MAX frames.
 * *first = the first frame's number on the PSP's timeline. Returns the count. */
int audio_take(const short **out, unsigned *first);

unsigned audio_blocks(void); /* blocks mixed since casting began */

#endif
