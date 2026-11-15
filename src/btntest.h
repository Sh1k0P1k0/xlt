/* XLT - Settings > XLT > Button test: every button, the analog stick and the
 * switches, drawn on a PSP as they are pressed. Buttons pressed once stay
 * marked; the stick shows where it is and how far it has gone. Every press is
 * the tester's: holding ○ for 1.5 s leaves. */
#ifndef BTNTEST_H
#define BTNTEST_H

#include "input.h"
#include "system.h"

void btntest_open(void);
/* Every frame, before ui_update(): reads the pad while it is up and clears
 * in->pressed and in->held, so nothing else reacts. 1 when it needs a redraw. */
int btntest_update(Input *in);
int btntest_is_open(void);  /* showing or fading */
int btntest_covers(void);   /* fully opaque: nothing underneath needs drawing */
void btntest_draw(const SysStatus *st);

#endif
