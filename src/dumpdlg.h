/* XLT - the "Copy to Memory Stick" dialog for the UMD (umddump.c does the work).
 * While it is up it takes every press: ○ asks to stop, ✕ closes it once the copy
 * finished or failed. A finished copy makes the library rescan. */
#ifndef DUMPDLG_H
#define DUMPDLG_H

#include "entry.h"
#include "input.h"

void dumpdlg_open(const Entry *disc); /* starts the copy and shows the dialog */
int dumpdlg_update(Input *in);        /* main loop, every frame; 1 if it needs drawing */
int dumpdlg_is_open(void);
void dumpdlg_draw(void);

#endif
