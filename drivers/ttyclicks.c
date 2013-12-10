/* ttyclicks.c: generate clicks as output is sent to the screen.
 * If you are old enough, this will remind you of the sounds of a mechanical
 * teletype running at 1200 baud.
 * Why would you want such a thing?
 * It provides valuable audio feedback to blind users.
 * They know when the computer responds to a command,
 * and they can discern the quantity and format of that response,
 * before the speech synthesizer has uttered a word.
 * It also throttles the output, which would otherwise fly by the screen
 * faster than any blind person could even so much as hit control s.
 *
 * Copyright (C) Karl Dahlke, 2011.
 * This software may be freely distributed under the GPL,
 * general public license, as articulated by the Free Software Foundation.
 *
 * This module uses notifiers, and will not work with kernels prior to 2.6.26.
 * Type `uname -r` to find your kernel version.
 *
 * Compile this as a lkernel module on your system.
 * Then run insmod on the resulting kernel object.
 */

#include <linux/notifier.h>
#include <linux/keyboard.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <linux/vt_kern.h>	/* for fg_console and speaker sounds */
#include <linux/ctype.h>
#include <linux/console.h>
#include <linux/module.h>

#include <asm/io.h>		/* for inb() outb() */
#include <asm/delay.h>

#include "ttyclicks.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Karl Dahlke - eklhad@gmail.com");
MODULE_DESCRIPTION
    ("Console output generates clicks, resembling a mechanical teletype.");

static int enabled = 1;
module_param(enabled, int, 0);
MODULE_PARM_DESC(enabled,
		 "sounds are enabled as soon as this module is loaded, default = 1 (yes)");

static int fgtty = 1;
module_param(fgtty, int, 0);
MODULE_PARM_DESC(fgtty,
		 "foreground tty generates clicks and chirps, default = 1 (yes)");

static int cursormotion;
module_param(cursormotion, int, 0);
MODULE_PARM_DESC(cursormotion,
		 "generate clicks for output characters that move the cursor or set screen attributes; default is 0 (no)");

static int kmsg = 1;
module_param(kmsg, int, 0);
MODULE_PARM_DESC(kmsg,
		 "kernel warning/error message generates a sequence of tones to get your attension, default = 1 (yes)");

static int sleep;
module_param(sleep, int, 0);
MODULE_PARM_DESC(sleep, "sleep between the clicks of the output characters,\n\
rather than a CPU busy loop.\n\
Default is 0 (no).\n\
This does not work unless you patch vt.c.");

/* Define NO_KDS if your kernel does not yet support kd_mkpulse,
 * kd_mkswoop, and kd_mknotes.
 * Thus far, sadly, I have had no success getting these sounds built
 * into the official kernel. So let's just assume they're not there. */
#define NO_KDS

/*
 * Here are some symbols that we export to other modules
 * so they can turn clicks on and off.
 * They correspond (roughly) to the module parameters given above.
 */

bool ttyclicks_on = true;
EXPORT_SYMBOL_GPL(ttyclicks_on);
bool ttyclicks_tty = true;
EXPORT_SYMBOL_GPL(ttyclicks_tty);
bool ttyclicks_kmsg = true;
EXPORT_SYMBOL_GPL(ttyclicks_kmsg);

/*
 * Don't click the escape sequences that move the cursor around the screen.
 * However, esc [ ... H moves the cursor to a new line, and might be worth
 * a cr sound. Not sure about that one.
 */

static char escState;

/*
 * Kernel alert messages don't go through tty, and you won't hear any clicks.
 * You don't want clicks anyways; you want something more.
 * You want an unusual noise that alerts you -
 * there is a kernel message that requires your attention!
 * When a newline comes through printk,
 * I make a sound with up and down tones.
 */

static const short printk_sound[] = {
	730, 7, 760, 7, 790, 7, 760, 7, 730, 7, -1, 7, 0, 0
};

static void my_printk(struct console *cons, const char *msg, unsigned int len)
{
	char c;
	while (len--) {
		c = *msg++;
		if (c == '\n' && ttyclicks_on & ttyclicks_kmsg)
			ttyclicks_notes(printk_sound);
	}
}				/* my_printk */

static struct console clickconsole = {
	.name = "tty clicks",
	.write = my_printk,
	.flags = CON_ENABLED,
	/* hope everything else is ok being zero or null */
};

/*
 * Now for something subtle.
 * I often hit caps lock by accident.  Don't we all?
 * But I can't tell the difference, until I've typed an entire paragraph
 * in upper case.  Isn't that frustrating?
 * So it is helpful to hear a high beep every time a capital letter is echoed.
 * It's easy to test for upper case (at least in English).
 * But when does an output character represent an echo of an input character?
 * Don't count on tty cooked mode to tell you;
 * lots of editors put the tty in raw mode,
 * and then there's ssh (when you're working remotely), and so on.
 * No - we need a more general approach.
 * I watch the keystrokes as they come in, and give them time stamps.
 * If an output letter matches an input letter, and not too much time
 * has gone by, I call it an echo character.
 * That is the only reason to monitor keystrokes.
 * If I didn't want echo capital letters to sound different,
 * I wouldn't need a keyboard notifier at all.
 * What follows is kinda complicated, but it actually works.
 */

#define ECHOEXPIRE 3		/* in seconds */
#define ECHOMAX 16

static struct {
	char inkey;
	unsigned long intime;
} echochars[ECHOMAX];
static int echead, ectail;

/* char is displayed on screen; is it echo? */
static int charIsEcho(char c)
{
	int j = ectail;
	char d;

	while (j != echead) {
		if ((long)jiffies - (long)echochars[j].intime > HZ * ECHOEXPIRE) {
			if (++j == ECHOMAX)
				j = 0;
			ectail = j;
			continue;
		}
		d = echochars[j].inkey;
		if (++j == ECHOMAX)
			j = 0;
		if (d != c) continue;
		ectail = j;
		return 1;
	}

	return 0;
}				/* charIsEcho */

static int
keystroke(struct notifier_block *this_nb, unsigned long type, void *data)
{
	struct keyboard_notifier_param *param = data;
	char key = param->value;
	int downflag = param->down;
	int shiftstate = param->shift;
	int j;

	/* if no sounds, then no need to do anything. */
	/* Also, no unicode, no control keys, and only down events. */
	if (!(ttyclicks_on & ttyclicks_tty) ||
	type != KBD_KEYSYM ||
	param->vc->vc_mode == KD_GRAPHICS ||
	downflag == 0 ||
	key == 0 ||
	shiftstate & 0xe)
		return NOTIFY_DONE;

	j = echead;
	if (++j == ECHOMAX)
		j = 0;
	if (j != ectail) {
		echochars[echead].inkey = key;
		echochars[echead].intime = jiffies;
		echead = j;
	}

	return NOTIFY_DONE;
}				/* keystroke */

/*
 * Here's the deal about notifier priority.
 * All this module does is make clicks as characters appear on screen.
 * Any other module is probably going to be more important.
 * In fact other modules may want to eat events before this one
 * makes its noises. So give this module a low priority.
 * 0 is default, so select a negative number.
 */

static struct notifier_block nb_key = {
	.notifier_call = keystroke,
	.priority = -70
};

/* Here are some routines to click the speaker, or make tones, etc. */

/* intervals, measured in microseconds */
#define TICKS_CLICK 600
#define TICKS_CHARWAIT 4000

#ifdef NO_KDS

/* This stuff belongs in drivers/input/misc/pcspkr.c */

/* Use the global PIT lock ! */
#include <linux/i8253.h>

/* Toggle the speaker, but not if a tone is sounding */
static void speaker_toggle(void)
{
	char c;
	unsigned long flags;

	raw_spin_lock_irqsave(&i8253_lock, flags);
	c = inb_p(0x61);
	if ((c & 3) != 3) {
		c &= 0xfe;
		c ^= 2;		/* toggle */
		outb(c, 0x61);
	}
	raw_spin_unlock_irqrestore(&i8253_lock, flags);
}

static void speaker_sing(unsigned int freq)
{
	unsigned long flags;
	raw_spin_lock_irqsave(&i8253_lock, flags);
	if (freq) {
		unsigned int count = PIT_TICK_RATE / freq;
		/* set command for counter 2, 2 byte write */
		outb_p(0xB6, 0x43);
		/* select desired HZ */
		outb_p(count & 0xff, 0x42);
		outb((count >> 8) & 0xff, 0x42);
		/* enable counter 2 */
		outb_p(inb_p(0x61) | 3, 0x61);
	} else {
		/* disable counter 2 */
		outb(inb_p(0x61) & 0xFC, 0x61);
	}
	raw_spin_unlock_irqrestore(&i8253_lock, flags);
}

static void my_mksteps(int f1, int f2, int step, int duration) ;

#endif

/* the sound of a character click */
void ttyclicks_click(void)
{
	if (!ttyclicks_on)
		return;
#ifndef NO_KDS
	kd_mkpulse(TICKS_CLICK);
#else
	speaker_toggle();
	udelay(TICKS_CLICK);
	speaker_toggle();
#endif
}				/* ttyclicks_click */

EXPORT_SYMBOL_GPL(ttyclicks_click);

void ttyclicks_cr(void)
{
	if (!ttyclicks_on)
		return;

/* If I could do this as a continuous swoop, it would look like this.
 *	for (i = 260; i > 60; i -= 2) { speaker_toggle(); udelay(i); }
 * but this just takes up too much cpu, so we have to settle for choppy steps. */

#ifndef NO_KDS
	kd_mksteps(2900, 3600, 10, 10);
#else
	my_mksteps(2900, 3600, 10, 10);
#endif
}				/* ttyclicks_cr */

EXPORT_SYMBOL_GPL(ttyclicks_cr);

#ifdef NO_KDS

/* This stuff belongs in drivers/tty/vt/keyboard.c */

/*
 * Push notes onto a sound fifo and play them via an asynchronous thread.
 * kd_mksound is a single tone, but kd_mknotes is a series of notes.
 * this is used primarily by the accessibility modules, to sound
 * various alerts and conditions for blind users.
 * This is particularly helpful when the adapter is not working,
 * for whatever reason.  These functions are central to the kernel,
 * and do not depend on sound cards, loadable modules, etc.
 * These notes can also alert a system administrator to conditions
 * that warrant immediate attention.
 * Each note is specified by 2 shorts.  The first is the frequency in hurtz,
 * and the second is the duration in hundredths of a second.
 * A frequency of -1 is a rest.
 * A frequency of 0 ends the list of notes.
 */

#define SF_LEN 64		/* length of sound fifo */
static short sf_fifo[SF_LEN];
static int sf_head, sf_tail;
static DEFINE_RAW_SPINLOCK(soundfifo_lock);

/* Pop the next sound out of the sound fifo. */
static void pop_soundfifo(unsigned long);

static DEFINE_TIMER(kd_mknotes_timer, pop_soundfifo, 0, 0);

static void pop_soundfifo(unsigned long notUsed)
{
	unsigned long flags;
	int freq, duration;
	int i;
	long jifpause;

	raw_spin_lock_irqsave(&soundfifo_lock, flags);

	i = sf_tail;
	if (i == sf_head) {
		freq = 0;
		duration = 0;
	} else {
		freq = sf_fifo[i];
		duration = sf_fifo[i + 1];
		i += 2;
		if (i == SF_LEN)
			i = 0;
		sf_tail = i;
	}

	raw_spin_unlock_irqrestore(&soundfifo_lock, flags);

	if (freq == 0) {
		/* turn off singing speaker */
		speaker_sing(0);
		del_timer(&kd_mknotes_timer);
		return;
	}

	jifpause = msecs_to_jiffies(duration);
	/* not sure of the rounding, if duration < HZ */
	if (jifpause == 0)
		jifpause = 1;
	mod_timer(&kd_mknotes_timer, jiffies + jifpause);

	if (freq < 0) {
		/* This is a rest between notes */
		speaker_sing(0);
	} else {
		speaker_sing(freq);
	}
}

/* Push a string of notes into the sound fifo. */
static void my_mknotes(const short *p)
{
	int i;
	bool wake = false;
	unsigned long flags;

	if (*p == 0)
		return;		/* empty list */

	raw_spin_lock_irqsave(&soundfifo_lock, flags);

	i = sf_head;
	if (i == sf_tail)
		wake = true;

	/* Copy shorts into the fifo, until the terminating zero. */
	while (*p) {
		sf_fifo[i++] = *p++;
		sf_fifo[i++] = (*p++) * 10;
		if (i == SF_LEN)
			i = 0;	/* wrap around */
		if (i == sf_tail) {
			/* fifo is full */
			goto done;
		}
		sf_head = i;
	}

	/* try to add on a rest, to carry the last note through */
	sf_fifo[i++] = -1;
	sf_fifo[i++] = 10;
	if (i == SF_LEN)
		i = 0;		/* wrap around */
	if (i != sf_tail)
		sf_head = i;

done:
	raw_spin_unlock_irqrestore(&soundfifo_lock, flags);

	/* first sound,  get things started. */
	if (wake)
		pop_soundfifo(0);
}

/* Push an ascending or descending sequence of notes into the sound fifo.
 * Step is a geometric factor on frequency, increase by x percent.
 * 100% goes up by octaves, -50% goes down by octaves.
 * 12% is a wholetone scale, while 6% is a chromatic scale.
 * Duration is in milliseconds, for very fast frequency sweeps.  But this
 * is based on jiffies timing, so is subject to the resolution of HZ. */
static void my_mksteps(int f1, int f2, int step, int duration)
{
	int i;
	bool wake = false;
	unsigned long flags;

	/* are the parameters in range? */
	if (step != (char)step)
		return;
	if (duration <= 0 || duration > 2000)
		return;
	if (f1 < 50 || f1 > 8000)
		return;
	if (f2 < 50 || f2 > 8000)
		return;

	/* avoid infinite loops */
	if (step == 0 || (f1 < f2 && step < 0) || (f1 > f2 && step > 0))
		return;

	raw_spin_lock_irqsave(&soundfifo_lock, flags);

	i = sf_head;
	if (i == sf_tail)
		wake = true;

	/* Copy shorts into the fifo, until start reaches end */
	while ((step > 0 && f1 < f2) || (step < 0 && f1 > f2)) {
		sf_fifo[i++] = f1;
		sf_fifo[i++] = duration;
		if (i == SF_LEN)
			i = 0;	/* wrap around */
		if (i == sf_tail) {
			/* fifo is full */
			goto done;
		}
		sf_head = i;
		f1 = f1 * (100 + step) / 100;
		if (f1 < 50 || f1 > 8000)
			break;
	}

	/* try to add on a rest, to carry the last note through */
	sf_fifo[i++] = -1;
	sf_fifo[i++] = 10;
	if (i == SF_LEN)
		i = 0;		/* wrap around */
	if (i != sf_tail)
		sf_head = i;

done:
	raw_spin_unlock_irqrestore(&soundfifo_lock, flags);

	/* first sound,  get things started. */
	if (wake)
		pop_soundfifo(0);
}

#endif

/* Put a string of notes into the sound fifo. */
void ttyclicks_notes(const short *p)
{
	if (!ttyclicks_on)
		return;
#ifdef NO_KDS
	my_mknotes(p);
#else
	kd_mknotes(p);
#endif
}				/* ttyclicks_notes */

EXPORT_SYMBOL_GPL(ttyclicks_notes);

void ttyclicks_steps(int f1, int f2, int step, int duration)
{
	if (!ttyclicks_on)
		return;
#ifdef NO_KDS
	my_mksteps(f1, f2, step, duration);
#else
	kd_mksteps(f1, f2, step, duration);
#endif
}				/* ttyclicks_steps */

EXPORT_SYMBOL_GPL(ttyclicks_steps);

void ttyclicks_bell(void)
{
	static const short notes[] = {
		1800, 10, 0, 0
	};
	ttyclicks_notes(notes);
}				/* ttyclicks_bell */

EXPORT_SYMBOL_GPL(ttyclicks_bell);

static int soundFromChar(char c, int minor)
{
	static const short capnotes[] = {
		3000, 3, 0, 0
	};

/* are sounds disabled? */
	if (!ttyclicks_on)
		return 0;

	if (c == '\07') {
		ttyclicks_bell();
		return 0;
	}

	if (!ttyclicks_tty)
		return 0;

/* Don't click for background screens */
	if (minor != fg_console + 1)
		return 0;

	if (c == '\n') {
		ttyclicks_cr();
		return 0;
	}

	if (charIsEcho(c) && c >= 'A' && c <= 'Z') {
		ttyclicks_notes(capnotes);
		return TICKS_CHARWAIT;
	}

/* I don't know what to do with nonprintable characters. */
/* I'll just pause, like they are spaces. */
	if (c >= 0 && c <= ' ')
		return TICKS_CHARWAIT;

/* regular printable character */
	ttyclicks_click();
	return TICKS_CHARWAIT - TICKS_CLICK;
}				/* soundFromChar */

/* Get char from the console, and make the sound. */
static int
vt_out(struct notifier_block *this_nb, unsigned long type, void *data)
{
	struct vt_notifier_param *param = data;
	struct vc_data *vc = param->vc;
	int minor = vc->vc_num + 1;
	int msecs = 0, usecs = 0;
	long jifpause;
	int unicode = param->c;
	char c = param->c;

	if (type != VT_PREWRITE)
		goto done;

	if (param->vc->vc_mode == KD_GRAPHICS)
		goto done;
	if (unicode >= 128) {
		escState = 0;
		goto done;
	}

	if (escState == 2) {
		if (isalpha(c)) {
			/* a letter indicates end of escape sequence. */
			escState = 0;
/* perhaps check for c == 'H' here? */
		}
		goto done;
	}

	if (!isdigit(c) && c != '?' && c != '#' && c != ';')
		escState = 0;

	if (escState == 1) {
		if (c == '[') {
			escState = 2;
			goto done;
		}
	} else if (c == '\33' && !cursormotion) {
		escState = 1;
		goto done;
	}

	usecs = soundFromChar(c, minor);

/*
 * If it's the bell, I make the beep, not the console.
 * This is the only char that this module eats.
 * Although eating char events is facilitated by VT_PREWRITE,
 * which requires kernel 2.6.26 or higher.
 */

	if (c == 7)
		return NOTIFY_STOP;

	if (!usecs)
		goto done;

/* Tell the process to sleep. */
	msecs = (usecs + 800) / 1000;
	jifpause = msecs_to_jiffies(msecs);
/* jifpause should always be positive */
	if (sleep && jifpause > 0) {

		/*
		 * This magical line of code only works if you edit vt.c
		 * and recompile the kernel.
		 * A patch for 2.3.37 can be found here.
		 * http://www.eklhad.net/linux/click-sleep-2.6.37.patch
		 */

		param->c = 0xac97;

	} else {

/*
 * no sleep, spin in a cpu cycle.
 * It's easy, and works,
 * but will suspend the music you are playing in the background.
 * You won't here the rest of Hey Jude until output has ceased.
 */

		udelay(usecs);
	}

done:
	return NOTIFY_DONE;
}				/* vt_out */

static struct notifier_block nb_vt = {
	.notifier_call = vt_out,
	.priority = -70
};

/* Load and unload the module. */

static int __init click_init(void)
{
	int rc;

	ttyclicks_on = enabled;
	ttyclicks_tty = fgtty;
	ttyclicks_kmsg = kmsg;

	rc = register_vt_notifier(&nb_vt);
	if (rc)
		return rc;

	rc = register_keyboard_notifier(&nb_key);
	if (rc) {
		unregister_vt_notifier(&nb_vt);
		return rc;
	}

	register_console(&clickconsole);

	return rc;
}				/* click_init */

static void __exit click_exit(void)
{
	unregister_console(&clickconsole);
	unregister_keyboard_notifier(&nb_key);
	unregister_vt_notifier(&nb_vt);

	ttyclicks_on = 0;
#ifdef NO_KDS
/* possible race conditions here with timers hanging around */
	sf_head = sf_tail = 0;
	pop_soundfifo(0);
#endif
}				/* click_exit */

module_init(click_init);
module_exit(click_exit);
