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

#include <linux/ttyclicks.h>

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

static int cursormotion = 0;
module_param(cursormotion, int, 0);
MODULE_PARM_DESC(cursormotion,
		 "generate clicks for output characters that move the cursor or set screen attributes; default is 0 (no)");

static int kmsg = 1;
module_param(kmsg, int, 0);
MODULE_PARM_DESC(kmsg,
		 "kernel warning/error message generates a sequence of tones to get your attension, default = 1 (yes)");

static int sleep = 0;
module_param(sleep, int, 0);
MODULE_PARM_DESC(sleep,
		 "sleep between the clicks of the output characters, (rather than a CPU busy loop), default is 0 (no); this does not work unless you patch vt.c");
// Implementing sleep by stopping the tty doesn't work.  Not sure why.
// Too bad; that means I have to modify and recompile the kernel
// for this feature to run properly.
#define SLEEP_STOP_TTY 0

/* Define KDS if your kernel has the patch with kd_mkpulse and kd_mkswoop. */
#define KDS 0

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
		if (c == '\n' && ttyclicks_on&ttyclicks_kmsg)
			ttyclicks_notes(printk_sound);
	}
}				/* my_printk */

static struct console clickconsole = {
name:	"pc clicks",
write:	my_printk,
flags:	CON_ENABLED,
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

#define MAXKEYPENDING 24
static char inkeybuffer[MAXKEYPENDING];
static unsigned long inkeytime[MAXKEYPENDING];
static short nkeypending;	/* number of keys pending */
/* minor number of tty where the keystrokes came from */
static int keyminor;
#define ECHOEXPIRE 3		/* in seconds */
static DEFINE_RAW_SPINLOCK(keybuflock);

/* Drop remembered keystrokes prior to mark */
static void dropKeysPending(int mark)
{
	int i, j;

	for (i = 0, j = mark; j < nkeypending; ++i, ++j) {
		inkeybuffer[i] = inkeybuffer[j];
		inkeytime[i] = inkeytime[j];
	}
	nkeypending -= mark;
}				/* dropKeysPending */

/* char is displayed on screen; is it echo? */
static int charIsEcho(char c)
{
	int rc = 0, j;
unsigned long flags;
	char d;

/* Do the high runner case first. */
	if (!nkeypending)
		return 0;

/*
 * The other access to this lock, shown below,
 * is part of the keyboard interrupt handler.
 * If it starts spinning, it never gets swapped out to let this routine
 * release the lock; and all linux shuts down.
 * Thus I have to disable interrupts.
 */

	raw_spin_lock_irqsave(&keybuflock, flags);

	for (j = 0; j < nkeypending; ++j) {
		d = inkeybuffer[j];
		if (d == c && inkeytime[j] + HZ * ECHOEXPIRE >= jiffies)
			break;
	}

	if (j < nkeypending) {
		rc = 1;
		++j;
	}

	dropKeysPending(j);
	raw_spin_unlock_irqrestore(&keybuflock, flags);
	return rc;
}				/* charIsEcho */

static int
keystroke(struct notifier_block *this_nb, unsigned long type, void *data)
{
	struct keyboard_notifier_param *param = data;
	char key = param->value;
	struct vc_data *vc = param->vc;
	int minor = vc->vc_num + 1;
	int downflag = param->down;
	int shiftstate = param->shift;

	raw_spin_lock(&keybuflock);

	/* Sorry, this doesn't work in unicode. */
	if (type != KBD_KEYSYM)
		goto done;

/* Only the key down events */
	if (downflag == 0)
		goto done;
	if (key == 0)
		goto done;

/* no control or alt keys */
	if (shiftstate & 0xe)
		goto done;

/* If we changed consoles, clear the pending keystrokes */
	if (minor != keyminor) {
		nkeypending = 0;
		keyminor = minor;
	}

/* make sure there's room, then push the key */
	if (nkeypending == MAXKEYPENDING) {
		int j;
		for (j = 1; j < nkeypending; ++j) {
			inkeybuffer[j - 1] = inkeybuffer[j];
			inkeytime[j - 1] = inkeytime[j];
		}
		--nkeypending;
	}
	inkeybuffer[nkeypending] = key;
	inkeytime[nkeypending] = jiffies;
	++nkeypending;

done:
	raw_spin_unlock(&keybuflock);
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
	.priority = -100
};

/* Here are some routines to click the speaker, or make tones, etc. */

/* intervals, measured in microseconds */
#define TICKS_CLICK 600
#define TICKS_CHARWAIT 4000
#define TICKS_TOPCR 260
#define TICKS_BOTCR 60
#define TICKS_INCCR -2

#define PORT_SPEAKER 0x61
#define PORT_TIMERVAL 0x42
#define PORT_TIMER2 0x43

static DEFINE_RAW_SPINLOCK(speakerlock);

#if ! KDS
/* togle the inbuilt speaker */
static void spk_toggle(void)
{
	unsigned char c;
	unsigned long flags;

/* Some other program might turn the tone on in the midst of our toggle,
 * or otherwise mess with the io port, so I have to irq. */
	raw_spin_lock_irqsave(&speakerlock, flags);
	c = inb(PORT_SPEAKER);
	/* cannot interrupt a tone with a click */
	if ((c & 3) != 3) {
		c &= ~1;
		c ^= 2;
		outb(c, PORT_SPEAKER);
	}
	raw_spin_unlock_irqrestore(&speakerlock, flags);
}				/* spk_toggle */
#endif

/* the sound of a character click */
void ttyclicks_click(void)
{
if(!ttyclicks_on) return;
#if KDS
kd_mkpulse(TICKS_CLICK);
#else
	spk_toggle();
	udelay(TICKS_CLICK);
	spk_toggle();
#endif
}				/* ttyclicks_click */
EXPORT_SYMBOL_GPL(ttyclicks_click);

void ttyclicks_cr(void)
{
	int i;

if(!ttyclicks_on) return;

#if KDS
kd_mkswoop(TICKS_TOPCR, TICKS_BOTCR, TICKS_INCCR);
#else
	for (i = TICKS_TOPCR; i > TICKS_BOTCR; i += TICKS_INCCR) {
		spk_toggle();
		udelay(i);
	}
#endif
}				/* ttyclicks_cr */
EXPORT_SYMBOL_GPL(ttyclicks_cr);

/*
 * Push notes onto a sound fifo and play them via an asynchronous thread.
 */

#define SF_LEN 32		/* length of sound fifo */
static short sf_fifo[SF_LEN];
static short sf_head, sf_tail;

/* Pop the next sound out of the sound fifo. */
static void popfifo(unsigned long);
static DEFINE_TIMER(note_timer, popfifo, 0, 0);
static void popfifo(unsigned long notUsed)
{
	unsigned long flags;
	short i, freq, duration;
int jifpause;

	raw_spin_lock_irqsave(&speakerlock, flags);

	del_timer(&note_timer);

	if ((i = sf_tail) == sf_head) {
		/* turn off singing speaker */
#if KDS
kd_mksound(0, 0);
#else
		outb(inb_p(PORT_SPEAKER) & 0xFC, PORT_SPEAKER);
#endif
		goto done;	/* sound fifo is empty */
	}

	/* First short holds the frequency */
	freq = sf_fifo[i++];
	if (i == SF_LEN)
		i = 0;		/* wrap around */
	duration = sf_fifo[i++];
	if (i == SF_LEN)
		i = 0;
	sf_tail = i;

jifpause = msecs_to_jiffies(duration*10);
	mod_timer(&note_timer, jiffies + jifpause);

	if (freq < 0) {
/* This is a rest between notes */
#if KDS
kd_mksound(0, 0);
#else
		outb(inb_p(PORT_SPEAKER) & 0xFC, PORT_SPEAKER);
#endif
	} else {
#if KDS
kd_mksound(freq, jifpause);
#else
		duration = 1193182 / freq;
		outb_p(inb_p(PORT_SPEAKER) | 3, PORT_SPEAKER);
		/* set command for counter 2, 2 byte write */
		outb_p(0xB6, PORT_TIMER2);
		outb_p(duration & 0xff, PORT_TIMERVAL);
		outb((duration >> 8) & 0xff, PORT_TIMERVAL);
#endif
	}

done:
	raw_spin_unlock_irqrestore(&speakerlock, flags);
}				/* popfifo */

/* Put a string of notes into the sound fifo. */
void ttyclicks_notes(const short *p)
{
	short i;

if(!ttyclicks_on) return;

	raw_spin_lock(&speakerlock);

	i = sf_head;
	/* Copy shorts into the fifo, until the terminating zero. */
	while (*p) {
		sf_fifo[i++] = *p++;
		if (i == SF_LEN)
			i = 0;	/* wrap around */
		if (i == sf_tail) {
/* fifo is full */
			raw_spin_unlock(&speakerlock);
			return;
		}
	}
	sf_head = i;

	raw_spin_unlock(&speakerlock);

	/* first sound,  get things started. */
	if (!timer_pending(&note_timer))
		popfifo(0);
}				/* ttyclicks_notes */

EXPORT_SYMBOL_GPL(ttyclicks_notes);

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
	int isecho = charIsEcho(c);
	static const short capnotes[] = {
		3000, 3, 0, 0
	};

/* are sounds disabled? */
	if (!(ttyclicks_on&ttyclicks_tty))
		return 0;

/* sound the bell on background screens, but nothing else */
	if (c != '\07' && minor != fg_console + 1)
		return 0;

	if (c == '\07') {
		ttyclicks_bell();
		return 0;
	}

	if (c == '\n') {
		ttyclicks_cr();
		return 0;
	}

	if (isecho && c >= 'A' && c <= 'Z') {
		ttyclicks_notes(capnotes);
		return TICKS_CHARWAIT;
	}

/* You need this if you're trying to get sleep to work, You need this if you're trying to get sleep to work,
 * and you don't want to run stty -onlcr  */
#if SLEEP_STOP_TTY
	if (c == '\r')
		return 0;
#endif

/* I don't know what to do with nonprintable characters. */
/* I'll just pause, like they are spaces. */
	if (c >= 0 && c <= ' ') {
		return TICKS_CHARWAIT;
	}

/* regular printable character */
	ttyclicks_click();
	return TICKS_CHARWAIT - TICKS_CLICK;
}				/* soundFromChar */

/* timer to wake up the terminal after this module has put it to sleep. */

#if SLEEP_STOP_TTY
static struct tty_struct *hang_tty;
static void restart_output(unsigned long);
static DEFINE_TIMER(restart_timer, restart_output, 0, 0);

static void restart_output(unsigned long dummy)
{
	del_timer(&restart_timer);
	start_tty(hang_tty);
	hang_tty = 0;
}				/* restart_output */
#endif

/* Get char from the console, and make the sound. */

static int
vt_out(struct notifier_block *this_nb, unsigned long type, void *data)
{
	struct vt_notifier_param *param = data;
	struct vc_data *vc = param->vc;
#if SLEEP_STOP_TTY
	struct tty_struct *tty = vc->vc_tty;
Starting in version 2.6.36
	struct tty_struct *tty = vc->port.tty;
#endif
	int minor = vc->vc_num + 1;
	int msecs = 0, usecs = 0;
	long jifpause;
	int unicode = param->c;
	char c = param->c;

	if (type != VT_PREWRITE)
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
#if SLEEP_STOP_TTY
// This doesn't work.   Why?
if(tty && !hang_tty) {
		stop_tty(tty);
		hang_tty = tty;
		mod_timer(&restart_timer, jiffies + jifpause);
}
#else

/*********************************************************************
This magical line of code only works if you edit vt.c and recompile the kernel.
Add the following, around line 2219, just after
rescan_last_byte:  &param) == NOTIFY_STOP)   continue;
 
// Sleep for a while.  I would call msleep(),
// but that addds an extra jiffy.  Timing is tight, and I can't afford that.
// Results would be inconsistent, based on the value of HZ.  This is better.
		if(param.c == 0xac97) {
			unsigned long timeout = msecs_to_jiffies(4);
			while (timeout)
				timeout = schedule_timeout_uninterruptible(timeout);
		}

*********************************************************************/

		param->c = 0xac97;
#endif

	} else {
// no sleep, spin in a cpu cycle.
// It's easy, and works, but will suspend the music you are playing in the background.
// You won't here the rest of Hey Jude until output has ceased.
		udelay(usecs);
	}

done:
	return NOTIFY_DONE;
}				/* vt_out */

static struct notifier_block nb_vt = {
	.notifier_call = vt_out,
	.priority = -100
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
/* possible race conditions here with timers hanging around */
	sf_head = sf_tail = 0;
	popfifo(0);

#if SLEEP_STOP_TTY
	if (hang_tty)
		restart_output(0);
#endif
}				/* click_exit */

module_init(click_init);
module_exit(click_exit);
