#ifndef _LINUX_TTYCLICKS_H
#define _LINUX_TTYCLICKS_H

/* External prototypes for ttyclicks.c */
/* See Documentation/accessibility/ttyclicks.txt for documentation. */

extern bool ttyclicks_on;	/* speaker sounds activated */
extern bool ttyclicks_tty;	/* speaker sounds from tty */
extern bool ttyclicks_kmsg;	/* speaker sounds from kernel message */

void ttyclicks_click(void);
void ttyclicks_cr(void);
void ttyclicks_bell(void);
void ttyclicks_notes(const short *notelist);
void ttyclicks_steps(int start, int end, int step, int duration);

#endif
