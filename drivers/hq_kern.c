/*********************************************************************

hq_kern.c: modify the keyboard for one-handed typing.
There is a user space version of this program in the jupiter project.
It is a daemon using the uinput system, rather than a loadable module.
It is called hq_user.c, as opposed to this module called hq_kern.c.

This module reflects the keyboard through the gh line
when the space bar is held down,
to facilitate one-handed typing.
This is also known as half qwerty.
So far, this could be done by loadkeys, but we add an additional constraint.
Hitting space bar alone enters a space.
That can't be done by loadkeys,
because you have to monitor the release (key up) codes.
But this module can do it.

Copyright (C) Karl Dahlke, 2008.
This software may be freely distributed under the GPL, general public license,
as articulated by the Free Software Foundation.

This module uses notifiers, and will not work with kernels prior to 2.6.24.
Type `uname -a` to find your kernel version.

Compile this as a lkernel module on your system.
Then run insmod on the resulting kernel object.

*********************************************************************/

#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/keyboard.h>
#include <linux/kbd_kern.h>
#include <linux/kd.h>
#include <linux/console_struct.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/vt_kern.h>	/* for fg_console */
#include <linux/version.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Karl Dahlke - eklhad@gmail.com");
MODULE_DESCRIPTION("Half qwerty keyboard for the one-handed typist.");

/* Pass characters to the tty, when changing the keystrokes. */
static void
tty_pushchar(int minor, int ch)
{
	struct vc_data *d = vc_cons[fg_console].d;

	if (!d)
		return;

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 9, 11)
	tty_insert_flip_char(d->port.tty, ch, 0);
	tty_flip_buffer_push(d->port.tty);
#else
	tty_insert_flip_char(&d->port, ch, 0);
	tty_flip_buffer_push(&d->port);
#endif
}				/* tty_pushchar */

/* Reverse the keyboard, with and without shift */
static const char lowercode[] =
   " -0987654321`\033 [poiuytrewq\t   ;lkjhgfdsa    /.,mnbvcxz";
static const char uppercode[] =
   " +)(*&^%$#@!~\033 {POIUYTREWQ\t   :LKJHGFDSA    ?><MNBVCXZ";

static int
keystroke(struct notifier_block *this_nb, unsigned long type, void *data)
{
    struct keyboard_notifier_param *param = data;
    unsigned int key = param->value;
    struct vc_data *vc = param->vc;
    int minor = vc->vc_num + 1;
    int downflag = param->down;
    int shiftstate = param->shift;
    static char spaceDown = 0;
    static char spaceUsed = 0;
    unsigned char is_shift = (shiftstate & 0x01) != 0;
    unsigned char is_alt = (shiftstate & 0x0a) != 0;
    unsigned char is_ctrl = (shiftstate & 0x04) != 0;
    const char *remap;
    char new_c;

    /* Sorry, this doesn't work in unicode. */
    if(type != KBD_KEYCODE)
	return NOTIFY_DONE;

    /* Handle space bar first, that's the strange one. */
    if(key == 0x39) {
	if(downflag == 0) {
	    if(spaceDown && !spaceUsed)
		tty_pushchar(minor, ' ');
	    spaceDown = 0;
	} else if(downflag == 1) {
	    spaceDown = 1, spaceUsed = 0;
	}
/* downflag = 2 means autorepeat, but we don't have to do anything here. */
	return NOTIFY_STOP;
    }

    /* We don't remap control or alt characters, yet. */
    if(is_ctrl | is_alt)
	return NOTIFY_DONE;

    /* Don't change function keys, and other high keys. */
    if(key > 0x35)
	return NOTIFY_DONE;
    if(!downflag)
	return NOTIFY_DONE;

    spaceUsed = 1;
    remap = (is_shift ? uppercode : lowercode);
    new_c = remap[key];
    /* We only need worry about the remapped keys. */
    if(new_c == ' ')
	return NOTIFY_DONE;
    if(!spaceDown)
	return NOTIFY_DONE;
    tty_pushchar(minor, new_c);

/*
Watch here; I am discarding a keyboard event, so I can push my own character.
But later, I don't discard the release of that key.
So an up event comes along with no down event.
Fortunately, the kernel tolerates this, or I'd have to do
a lot more bookkeeping.
*/

    return NOTIFY_STOP;
}				/* keystroke */

/* We want a high priority here, comparable to or perhaps higher than
 * other adapters that might be running.
 * There are good reasons to make hq higher than acsint,
 * and good reasons to make it lower.
 * The two aren't entirely compatible, e.g. you can't use
 * hq to enter text into the search function of acsint
 * by typing one handed - it won't work.
 * More annoying, acsint considers an "other side" character
 * to be computer generated output, rather than en echo of input,
 * because it's not the same letter you typed.
 * So it reads half the characters you type.
 * I don't have a solution for this one yet.
 * Anyways, back to priority; it probably doesn't matter which one is higher.
 * Acsint is 20; I'll make hq 25. */

static struct notifier_block nb = {
    .notifier_call = keystroke,
	.priority = 25
};

static int __init
checkinit(void)
{
    return register_keyboard_notifier(&nb);
}				/* checkinit */

static void __exit
checkexit(void)
{
    unregister_keyboard_notifier(&nb);
}				/* checkexit */

module_init(checkinit);
module_exit(checkexit);
