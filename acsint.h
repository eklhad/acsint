#ifndef _LINUX_ACSINT_H
#define _LINUX_ACSINT_H

/* Prototypes and definitions for the acsint device driver. */

/* input.h has symbolic constants for all the keys on the keyboard; let's use those. */
#include <linux/input.h>
#include <linux/kd.h>
#include <linux/keyboard.h>


/* The size of the tty log; store this many characters of tty output.
 * Has to be between 30K and 64K */
#define TTYLOGSIZE 50000
#define TTYLOGSIZE1 (TTYLOGSIZE+1)
#define TTYLOGSIZE2 (TTYLOGSIZE+2)

/* Commands that Acsint sends or receives */
enum acs_command {
ACSINT_NULL,
/* configuration commands */
ACSINT_CLEAR_KEYS,
ACSINT_SET_KEY,
ACSINT_UNSET_KEY,
ACSINT_PUSH_TTY,
/* Configure which sounds are made automatically */
ACSINT_SOUNDS, /* on or off */
ACSINT_SOUNDS_TTY,
ACSINT_SOUNDS_KMSG,
/* ask the driver to make specific sounds for you */
ACSINT_CLICK,
ACSINT_CR,
ACSINT_NOTES, /* series of notes */
/* Request to bring the tty log up to date */
ACSINT_REFRESH,
/* should keys go to the adapter or console or both */
ACSINT_BYPASS, /* send next char through */
ACSINT_MONITOR, /* to monitor keystrokes as you type */
ACSINT_DIVERT, /* adapter grabs a string of text */
/* events coming back */
ACSINT_KEYSTROKE,
ACSINT_TTY_NEWCHARS, /* the ones you haven't seen yet */
ACSINT_TTY_MORECHARS, /* there are more chars pending */
ACSINT_FGC, /* foreground console */
ACSINT_PRINTK,
};

/* Here is a bound; you can't capture keys at or beyond this point. */
#define ACS_NUM_KEYS 128

/* Symbolic constants for the keys are in input.h */

/* Symbolic constants for the led states are in kd.h */

/* Symbolic constants for the shift states are derived from keyboard.h,
 * but then I add a couple of my own. */
#define ACS_SS_SHIFT (1<<KG_SHIFT)
#define ACS_SS_RALT (1<<KG_ALTGR)
#define ACS_SS_CTRL (1<<KG_CTRL)
#define ACS_SS_LALT (1<<KG_ALT)
#define ACS_SS_ALT (ACS_SS_LALT|ACS_SS_RALT)
/* and these are new */
#define ACS_SS_PLAIN 0x10
#define ACS_SS_ALL 0x20

#endif
