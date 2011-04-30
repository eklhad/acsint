/* Acsint - Accessibility intercepter.
 * Originally written by Saqib Shaikh in 2005.
 * Modified by Karl Dahlke in 2011 to use notifiers.
 */

#include <linux/kernel.h>
#include <linux/notifier.h>
#include <linux/kbd_kern.h>
#include <linux/ctype.h>
#include <linux/slab.h>		/* malloc and free */
#include <linux/vt.h>
#include <linux/vt_kern.h>	/* for fg_console */
#include <linux/console.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/tty_flip.h>
#include <linux/miscdevice.h>
#include <linux/version.h>
#include <linux/poll.h>

#include "ttyclicks.h"
#include "acsint.h"

#define ACSINT_DEVICE "/dev/acsint"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Karl Dahlke - eklhad@gmail.com");
MODULE_DESCRIPTION
    ("Accessibility intercepter - pass keystroke and tty events to user space");

static int major = 0;
module_param(major, int, 0);
MODULE_PARM_DESC(major,
		 "major number for /dev/acsint, default is dynamic allocation through misc_register");

/* For various critical sections of code. */
static DEFINE_RAW_SPINLOCK(acslock);

/* circular buffer of output characters received from the tty */
struct cbuf {
	unsigned int area[TTYLOGSIZE1];
	unsigned int *start, *end;
	unsigned int *head, *tail;
/* mark the place where we last copied data to user space */
	unsigned int *mark;
/* new output characters, not yet copied to user space */
	unsigned int *output;
};

/* These are allocated, one per console, as needed. */
static struct cbuf *cbuf_tty[MAX_NR_CONSOLES];

/* in case we can't malloc a buffer */
static const char cb_nomem_message[] =
    "Kernel cannot allocate space for this console";

/* set to 1 if you have sent the above nomem message down to the user */
static unsigned char cb_nomem_refresh[MAX_NR_CONSOLES];

/* set to 1 if you have tried to allocate */
static unsigned char cb_nomem_alloc[MAX_NR_CONSOLES];

/* Staging area to copy tty data down to user space */
/* This is a snapshot of the circular buffer. */
static unsigned int cb_staging[TTYLOGSIZE];

/* Initialize / reset the variables in the circular buffer. */
static void cb_reset(struct cbuf *cb)
{
	if (!cb)
		return;		/* never allocated */
	cb->start = cb->area;
	cb->end = cb->area + TTYLOGSIZE1;
	cb->head = cb->start;
	cb->tail = cb->start;
	cb->mark = cb->start;
	cb->output = cb->start;
}

/* check to see if the circular buffer was allocated. */
/* If never attempted, try to allocate it. */
/* mino is minor-1, a 0 based index into arrays, similar to fg_console. */
static void checkAlloc(int mino, bool from_vt)
{
	struct cbuf *cb = cbuf_tty[mino];
	if (cb)
		return;		/* already allocated */
	if (cb_nomem_alloc[mino])
		return;		/* already tried to allocate */
	cb_nomem_alloc[mino] = 1;
	cb = kmalloc(sizeof(struct cbuf), (from_vt ? GFP_ATOMIC : GFP_KERNEL));
	if (!cb)
		return;
	cb_reset(cb);
	cbuf_tty[mino] = cb;
}

/* Put a character on the end of the circular buffer.
 * Drop the oldest character if the buffer is full.
 * This is called under a spinlock, so we don't have to worry about the reader
 * draining characters while this routine adds characters on. */
static void cb_append(struct cbuf *cb, unsigned int c)
{
	if (!cb)
		return;		/* should never happen */
	*cb->head = c;
	++cb->head;
	if (cb->head == cb->end)
		cb->head = cb->start;
	if (cb->head == cb->tail) {
		/* buffer full, drop the last character */
		if (cb->tail == cb->mark)
			cb->mark = 0;
		if (cb->tail == cb->output)
			cb->output = 0;
		++cb->tail;
		if (cb->tail == cb->end)
			cb->tail = cb->start;
	}
}

/* Indicate which keys, by key code, are meta.  For example,
 * shift, alt, numlock, etc.  These are the state changing keys. */
static bool ismeta[ACS_NUM_KEYS];

/* Indicate which keys should be passed on to userspace.
 * Each element should be set to the appropriate shiftstate(s)
 * as defined in acsint.h.
 * Relay keys in those designated shift states.
 * Otherwise they go on to the console.
 * Set to -1 if the key is discarded entirely.
 */

static char acsint_keys[ACS_NUM_KEYS];

static void clear_keys(void)
{
	int i;
	for (i = 0; i < ACS_NUM_KEYS; i++)
		acsint_keys[i] = 0;
}

/* divert all keys to user space, to grab the next key or build a string. */
static bool key_divert;

/* Monitor keystrokes that are passed to the console, by sending
 * them to user space as well. This is like /usr/bin/tee. */
static bool key_monitor;

/* pass the next key through to the console. */
static bool key_bypass;

/* The array "rbuf" is used for passing key/tty events to user space.
 * A reading buffer of sorts.  See device_read() below.
 * Despite the names head and tail, it's not a true circular buffer.
 * The process has to read the data before rbuf_head reaches rbuf_end,
 * or data is lost.
 * That's not a problem, because you just can't type faster
 * than the daemon can gather up those keystrokes.
 * Every event is 4 bytes, except echo, which is 8.
 * Thus everything stays 4 byte aligned.
 * This is necessary to pass down unicodes.
 */

#define RBUF_LEN 400
static char rbuf[RBUF_LEN];
static const char *rbuf_end = rbuf + RBUF_LEN;
static char *rbuf_tail, *rbuf_head;

/* Wait until this driver has some data to read. */
DECLARE_WAIT_QUEUE_HEAD(wq);

static bool in_use;		/* only one process opens this device at a time */
static int last_fgc;		/* last fg_console */

/* Push characters onto the input queue of the foreground tty.
 * This is for macros, or cut&paste. */
static void tty_pushstring(const char *cp, int len)
{
	char c;
	struct tty_struct *tty;
	struct vc_data *d = vc_cons[fg_console].d;

	if (!d)
		return;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,36)
	tty = d->vc_tty;
#else
	tty = d->port.tty;
#endif
	if (!tty)
		return;

	while (len) {
		get_user(c, cp);
		tty_insert_flip_char(tty, c, 0);
		cp++;
		--len;
	}

	con_schedule_flip(tty);
}				/* tty_pushstring */

/* File operations for /dev/acsint. */

static int device_open(struct inode *inode, struct file *file)
{
	int j;

/* A theoretical race condition here; too unlikely for me to worry about. */
	if (in_use)
		return -EBUSY;

	for (j = 0; j < MAX_NR_CONSOLES; ++j) {
		cb_reset(cbuf_tty[j]);
		cb_nomem_refresh[j] = 0;
		cb_nomem_alloc[j] = 0;
	}

	clear_keys();
	key_divert = false;
	key_monitor = false;
	key_bypass = false;

/* Set certain keys as meta, consistent with the standard key map */
	for (j = 0; j < ACS_NUM_KEYS; ++j)
		ismeta[j] = false;
/* These all have to be less than ACS_NUM_KEYS */
	ismeta[KEY_LEFTCTRL] = true;
	ismeta[KEY_RIGHTCTRL] = true;
	ismeta[KEY_LEFTSHIFT] = true;
	ismeta[KEY_RIGHTSHIFT] = true;
	ismeta[KEY_LEFTALT] = true;
	ismeta[KEY_RIGHTALT] = true;
	ismeta[KEY_CAPSLOCK] = true;
	ismeta[KEY_NUMLOCK] = true;
	ismeta[KEY_SCROLLLOCK] = true;

/* At startup we tell the process which virtual console it is on.
 * Place this directive in rbuf to be read. */
	rbuf[0] = ACSINT_FGC;
	rbuf[1] = fg_console + 1;	/* minor number */
	rbuf_tail = rbuf;
	rbuf_head = rbuf + 4;
	last_fgc = fg_console;
	checkAlloc(fg_console, false);

	in_use = true;
	return 0;
}

static int device_close(struct inode *inode, struct file *file)
{
	in_use = false;
	rbuf_head = rbuf_tail = rbuf;
	return 0;
}

static ssize_t device_read(struct file *file, char *buf, size_t len,
			   loff_t * offset)
{
	int bytes_read = 0;
	struct cbuf *cb;
	bool catchup;
/* catch up length - how many characters to copy down to user space */
	int culen = 0;
	char cu_cmd[4];		/* the catch up command */
	char *temp_head, *temp_tail, *t;
	int j, j2;
	int retval;
	unsigned long irqflags;

	if (!in_use)
		return 0;	/* should never happen */

	retval = wait_event_interruptible(wq, (rbuf_head > rbuf_tail));
	if (retval)
		return retval;

/* you can only read on behalf of the foreground console */
	cb = cbuf_tty[fg_console];

/* Use temp pointers, more keystrokes could be appended while
 * we're doing this; that's ok. */
	temp_head = rbuf_head;
	temp_tail = rbuf_tail;

/* Skip ahead to the last FGC event, if present. */
	for (t = temp_tail; t < temp_head; t += 4) {
		if (*t == ACSINT_FGC)
			temp_tail = t;
		if (*t == ACSINT_TTY_MORECHARS)
			t += 4;
	}

	raw_spin_lock_irqsave(&acslock, irqflags);

	catchup = false;
	if ((!cb && !cb_nomem_refresh[fg_console]) || cb->head != cb->mark) {
		/* MORECHARS doesn't force us to catch up, but anything else does. */
		for (t = temp_tail; t < temp_head; t += 4) {
			if (*t == ACSINT_TTY_MORECHARS) {
				t += 4;
				continue;
			}
			catchup = true;
			break;
		}
	}

	if (catchup) {
		if (cb) {
			if (cb->mark == 0)
				cb->mark = cb->tail;
			if (cb->head >= cb->mark)
				culen = cb->head - cb->mark;
			else
				culen =
				    (cb->end - cb->mark) + (cb->head -
							    cb->start);
		} else {
			culen = sizeof(cb_nomem_message) - 1;
		}

		cu_cmd[0] = ACSINT_TTY_NEWCHARS;
/* Put in the minor number here, though I don't think we need it. */
		cu_cmd[1] = fg_console + 1;
		cu_cmd[2] = culen;
		cu_cmd[3] = (culen >> 8);

		if (cb) {
			/* One clump or two. */
			if (cb->head >= cb->mark) {
				if (culen)
					memcpy(cb_staging, cb->mark, culen * 4);
			} else {
				j = cb->end - cb->mark;
				memcpy(cb_staging, cb->mark, j * 4);
				j2 = cb->head - cb->start;
				if (j2)
					memcpy(cb_staging + j, cb->start,
					       j2 * 4);
			}
			cb->mark = cb->head;
			cb->output = cb->head;
		} else {
			for (j = 0; j < culen; ++j)
				cb_staging[j] = cb_nomem_message[j];
			cb_nomem_refresh[fg_console] = 1;
		}
	}
	/* catching up */
	raw_spin_unlock_irqrestore(&acslock, irqflags);

/* Now pass down the events. */
/* First fgc, then catch up, then the rest. */
	if (*temp_tail == ACSINT_FGC && len >= 4) {
		if (copy_to_user(buf, temp_tail, 4))
			return -EFAULT;
		temp_tail += 4;
		bytes_read += 4;
		buf += 4;
		len -= 4;
	}

	if (catchup && len >= (culen + 1) * 4) {
		if (copy_to_user(buf, cu_cmd, 4))
			return -EFAULT;
		if (culen && copy_to_user(buf + 4, cb_staging, culen * 4))
			return -EFAULT;
		bytes_read += (culen + 1) * 4;
		buf += (culen + 1) * 4;
		len -= (culen + 1) * 4;
	}

/* And the rest of the events. */
	j = temp_head - temp_tail;
	if (j > len)
		j = len;	/* should never happen */
	if (j) {
		if (copy_to_user(buf, temp_tail, j))
			return -EFAULT;
		temp_tail += j;
		buf += j;
		bytes_read += j;
		len -= j;
	}

/* Pull the pointers back to start. */
/* This should happen almost every time. */
	raw_spin_lock_irqsave(&acslock, irqflags);
	rbuf_tail = temp_tail;
	if (rbuf_head == rbuf_tail)
		rbuf_head = rbuf_tail = rbuf;
	raw_spin_unlock_irqrestore(&acslock, irqflags);

	*offset += bytes_read;
	return bytes_read;
}				/* device_read */

static ssize_t device_write(struct file *file, const char *buf, size_t len,
			    loff_t * offset)
{
	char c;
	const char *p = buf;
	int j, key, shiftstate, bytes_write;
	int nn;			/* number of notes */
	short notes[2 * (10 + 1)];
	int isize;		/* size of input to inject */
	unsigned long irqflags;

	if (!in_use)
		return 0;	/* should never happen */

	while (len) {
		get_user(c, p++);
		len--;

		switch (c) {
		case ACSINT_CLEAR_KEYS:
			clear_keys();
			break;

		case ACSINT_SET_KEY:
			if (len < 2)
				break;
			get_user(key, p++);
			key = (unsigned char)key;
			len--;
			get_user(shiftstate, p++);
			len--;
			if (key < ACS_NUM_KEYS)
				acsint_keys[key] = shiftstate;
			break;

		case ACSINT_UNSET_KEY:
			if (len < 1)
				break;
			get_user(key, p++);
			key = (unsigned char)key;
			len--;
			if (key < ACS_NUM_KEYS)
				acsint_keys[key] = 0;
			break;

		case ACSINT_CLICK:
			ttyclicks_click();
			break;

		case ACSINT_CR:
			ttyclicks_cr();
			break;

		case ACSINT_SOUNDS:
			if (len < 1)
				break;
			get_user(c, p++);
			len--;
			ttyclicks_on = c;
			break;

		case ACSINT_SOUNDS_TTY:
			if (len < 1)
				break;
			get_user(c, p++);
			len--;
			ttyclicks_tty = c;
			break;

		case ACSINT_SOUNDS_KMSG:
			if (len < 1)
				break;
			get_user(c, p++);
			len--;
			ttyclicks_kmsg = c;
			break;

		case ACSINT_NOTES:
			if (len < 1)
				break;
			get_user(nn, p++);
			len--;
			for (j = 0; j < nn && j < 10 && len >= 3; ++j, len -= 3) {
				get_user(c, p++);
				notes[2 * j] = (unsigned char)c;
				get_user(c, p++);
				notes[2 * j] |= ((short)c << 8);
				get_user(c, p++);
				notes[2 * j + 1] = (unsigned char)c;
			}
			notes[2 * j] = 0;
			if (j)
				ttyclicks_notes(notes);
			for (; j < nn && len >= 3; ++j, len -= 3) {
				p += 3;
			}
			break;

		case ACSINT_BYPASS:
			key_bypass = true;
			break;

		case ACSINT_DIVERT:
			if (len < 1)
				break;
			get_user(c, p++);
			len--;
			key_divert = (c != 0);
			break;

		case ACSINT_MONITOR:
			if (len < 1)
				break;
			get_user(c, p++);
			len--;
			key_monitor = (c != 0);
			break;

		case ACSINT_REFRESH:
			raw_spin_lock_irqsave(&acslock, irqflags);
			if (rbuf_head <= rbuf_end - 4) {
				*rbuf_head = ACSINT_REFRESH;
				if (rbuf_head == rbuf_tail)
					wake_up_interruptible(&wq);
				rbuf_head += 4;
			}
			raw_spin_unlock_irqrestore(&acslock, irqflags);
			break;

		case ACSINT_PUSH_TTY:
			if (len < 2)
				break;
			get_user(c, p++);
			isize = (unsigned char)c;
			get_user(c, p++);
			isize |= ((unsigned short)c << 8);
			len -= 2;
			if (len < isize)
				break;
			tty_pushstring(p, isize);
			p += isize, len -= isize;
			break;

		}		/* switch */
	}			/* loop processing config instructions */

	bytes_write = p - buf;
	*offset += bytes_write;
	return bytes_write;
}				/* device_write */

static unsigned int device_poll(struct file *fp, poll_table * pt)
{
	unsigned int mask = 0;
	if (!in_use)
		return 0;	/* should never happen */
/* we don't support poll writing. How to figure if the buffer is not full? */
	if (rbuf_head > rbuf_tail)
		mask = POLLIN | POLLRDNORM;
	poll_wait(fp, &wq, pt);
	return mask;
}

static struct file_operations fops = {
owner:	THIS_MODULE,
open:	device_open,
release:device_close,
read:	device_read,
write:	device_write,
poll:	device_poll,
};

static struct miscdevice acsint_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "acsint",
	.fops = &fops,
};

/* Variables to remember the entered keystrokes, to watch for echo.
 * If these keys reappear on screen in a timely fashion,
 * they are considered echo chars. */

#define MAXKEYPENDING 8
/* This holds unicodes */
static unsigned int inkeybuffer[MAXKEYPENDING];
static unsigned long inkeytime[MAXKEYPENDING];
static short nkeypending;	/* number of keys pending */
/* Key echo states:
 * 0 nothing special
 * 1 tab or ^i match space, more spaces coming
* 2 return matches cr, lf coming back
 * 3 control char matches ^, letter coming back
 * 4 delete or ^h matches ^h, space is next
 * 5 ^h expected, move back to 0
*/
static short keyechostate;
#define flushInKeyBuffer() (nkeypending = keyechostate = 0)
#define ECHOEXPIRE 3		/* in seconds */

static void dropKeysPending(int mark)
{
	int i, j;
	for (i = 0, j = mark; j < nkeypending; ++i, ++j) {
		inkeybuffer[i] = inkeybuffer[j];
		inkeytime[i] = inkeytime[j];
	}
	nkeypending -= mark;
	if (!nkeypending)
		flushInKeyBuffer();
}				/* dropKeysPending */

/* char is displayed on screen; is it echo? */
/* This is run from within a spinlock */
static int isEcho(unsigned int c)
{
	unsigned int d;
	int j;

/* when echo is based only on states */
	if (keyechostate == 1 && c == ' ')
		return 2;
	if (keyechostate == 2 && c == '\n') {
		keyechostate = 0;
		return 2;
	}
	if (keyechostate == 3 && c < 256 && isalpha(c)) {
		keyechostate = 0;
		return 2;
	}
	if (keyechostate == 4 && c == ' ') {
		keyechostate = 5;
		return 2;
	}
	if (keyechostate == 5 && c == '\b') {
		keyechostate = 0;
		return 2;
	}

	keyechostate = 0;
	if (!nkeypending)
		return 0;

/* drop old keys */
	for (j = 0; j < nkeypending; ++j)
		if (inkeytime[j] + HZ * ECHOEXPIRE >= jiffies)
			break;
	if (j) {
		dropKeysPending(j);
		if (!nkeypending)
			return 0;
	}

/* to jump into the state machine we need to match on the first character */
	d = inkeybuffer[0];
	if (d == '\t' && c == ' ') {
		dropKeysPending(1);
		keyechostate = 1;
		return 2;
	}
	if ((d == '\r' || d == '\n') && c == '\r') {
		dropKeysPending(1);
		keyechostate = 2;
		return 1;
	}
	if (d < ' ' && c == '^') {
		dropKeysPending(1);
		keyechostate = 3;
		return 2;
	}
	if ((d == '\b' || d == 0x7f) && c == '\b') {
		dropKeysPending(1);
		keyechostate = 4;
		return 2;
	}

	for (j = 0; j < nkeypending; ++j) {
		if (inkeybuffer[j] != c)
			continue;
/* straight echo match */
		dropKeysPending(j + 1);
		return 1;
	}

	return 0;
}				/* isEcho */

/* Push a character onto the tty log.
 * Called from the vt notifyer and from my printk console. */
static void pushlog(unsigned int c, int mino, bool from_vt)
{
	unsigned long irqflags;
	bool wake = false;
	bool athead = false;	/* output is at the head */
	int echo = 0;
	struct cbuf *cb = cbuf_tty[mino];

	if (!cb)
		return;

	raw_spin_lock_irqsave(&acslock, irqflags);

	if (mino == fg_console) {
		if (from_vt)
			echo = isEcho(c);
		if (cb->output == cb->head)
			athead = true;
	}

	if ((athead || echo) && rbuf_head <= rbuf_end - 8) {
		/* throw the "more stuff" event */
		if (rbuf_head == rbuf_tail)
			wake = true;
		rbuf_head[0] = ACSINT_TTY_MORECHARS;
		rbuf_head[1] = echo;
		if (echo) {
			*(unsigned int *)(rbuf_head + 4) = c;
		} else {
			*(unsigned int *)(rbuf_head + 4) = 0;
		}
		rbuf_head += 8;
	}

	cb_append(cb, c);

/* If you were caught up before, and you receive an echo char,
 * then you're still caught up. */
	if (athead && echo)
		cb->output = cb->head;

	if (wake)
		wake_up_interruptible(&wq);
	raw_spin_unlock_irqrestore(&acslock, irqflags);
}				/* pushlog */

/*
 * We need a console to capture printk() text
 * and push it onto the buffer.
 * It didn't come from the tty, but we want to read it nonetheless.
 */

static void my_printk(struct console *cons, const char *msg, unsigned int len)
{
	char c;
	if (!in_use)
		return;
	while (len--) {
		c = *msg++;
		pushlog((unsigned char)c, fg_console, false);
	}
}

static struct console acsintconsole = {
name:	"acsint",
write:	my_printk,
flags:	CON_ENABLED,
};

/* Notifiers: keyboard events and tty events. */

static int
vt_out(struct notifier_block *this_nb, unsigned long type, void *data)
{
	struct vt_notifier_param *param = data;
	struct vc_data *vc = param->vc;
	int mino = vc->vc_num;
	unsigned int unicode = param->c;
	unsigned long irqflags;
	bool wake = false;

	if (!in_use)
		return NOTIFY_DONE;

	switch (type) {
	case VT_UPDATE:
		if (fg_console == last_fgc)
			break;	/* it's the same console */

		last_fgc = fg_console;
/* retry alloc on console switch */
		cb_nomem_alloc[fg_console] = 0;
		checkAlloc(fg_console, true);
		raw_spin_lock_irqsave(&acslock, irqflags);
		flushInKeyBuffer();
		if (rbuf_head <= rbuf_end - 4) {
			if (rbuf_head == rbuf_tail)
				wake = true;
			rbuf_head[0] = ACSINT_FGC;
			rbuf_head[1] = fg_console + 1;
			rbuf_head += 4;
			if (wake)
				wake_up_interruptible(&wq);
		}
		raw_spin_unlock_irqrestore(&acslock, irqflags);
		break;

	case VT_PREWRITE:
/* I don't log, or pass back, null bytes in the output stream. */
		if (unicode == 0)
			break;

		checkAlloc(mino, true);
		pushlog(unicode, mino, true);
	}			/* switch */

	return NOTIFY_DONE;
}				/* vt_out */

static struct notifier_block nb_vt = {
	.notifier_call = vt_out,
	.priority = 20
};

static int
keystroke(struct notifier_block *this_nb, unsigned long type, void *data)
{
	struct keyboard_notifier_param *param = data;
	unsigned int key = param->value;
	int downflag = param->down;
	int ss = param->shift;
	char action;
	static char ischort[] = {
		0, 0, 0, 1, 0, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0
	};
	bool wake = false, keep = false, send = false;
	bool divert, monitor, bypass;
	unsigned long irqflags;

	if (!in_use)
		goto done;

	if (param->vc->vc_mode == KD_GRAPHICS)
		goto done;

/* Only the key down events */
	if (downflag == 0)
		goto done;

#if 0
/* This is how we should be putting keystrokes in the echo queue. */
/* But I don't get any unicode events from my keyboard. */
	if (type == KBD_UNICODE) {
		raw_spin_lock_irqsave(&acslock, irqflags);
		if (nkeypending == MAXKEYPENDING)
			dropKeysPending(1);
		inkeybuffer[nkeypending] = key;
		inkeytime[nkeypending] = jiffies;
		++nkeypending;
		raw_spin_unlock_irqrestore(&acslock, irqflags);
		goto done;
	}
#endif

	if (type != KBD_KEYCODE)
		goto done;

	ss &= 0xf;
	if (!ss)
		ss = ACS_SS_PLAIN;

	action = 0;
	if (key < ACS_NUM_KEYS)
		action = acsint_keys[key];
	if (action < 0)
		goto stop;

	divert = key_divert;
	monitor = key_monitor;
	bypass = key_bypass;
/* But we don't redirect the meta keys */
	if (divert || monitor || bypass) {
		if (key < ACS_NUM_KEYS && ismeta[key]) {
			divert = false;
			monitor = false;
			bypass = false;
		}
	}

	if (divert || monitor)
		keep = true;
	if (bypass) {
		key_bypass = 0;
		send = true;
		goto event;
	}

/* keypad is assumed to be numbers with numlock on,
 * perhaps speech functions otherwise. */
	if (param->ledstate & K_NUMLOCK &&
	    key >= KEY_KP7 && key <= KEY_KPDOT &&
	    key != KEY_KPMINUS && key != KEY_KPPLUS)
		goto regular;

	if (action != ACS_SS_ALL) {
		if (!(action & ss))
			goto regular;
/* only one of shift, lalt, ralt, or control */
		if (ischort[ss])
			goto regular;
	}
	keep = true;
	goto event;

regular:
/* Just a regular key. */
	if (!divert)
		send = true;

event:
	if (keep) {
		/* If this notifier is not called by an interrupt, then we need the spinlock */
		raw_spin_lock_irqsave(&acslock, irqflags);
		if (rbuf_head <= rbuf_end - 4) {
			if (rbuf_head == rbuf_tail)
				wake = true;
			rbuf_head[0] = ACSINT_KEYSTROKE;
			rbuf_head[1] = key;
			rbuf_head[2] = ss;
			rbuf_head[3] = param->ledstate;
			rbuf_head += 4;
			if (wake)
				wake_up_interruptible(&wq);
		}
		raw_spin_unlock_irqrestore(&acslock, irqflags);
	}

	if (send) {
		/*
		 * Remember this key, to check for echo.
		 * I should be responding to KBD_UNICODE, and storing the unicode,
		 * but my keyboard doesn't generate that event.  Don't know why.
		 * And trying to deal with KEYSYM is a nightmare.
		 * So that leaves KEYCODE, which I must (roughly) translate.
		 * If your keyboard isn't qwerty, we're screwed.
		 * Somebody help me with this one.
		 */
		char keychar;
		static const char lowercode[] =
		    " \0331234567890-=\177\tqwertyuiop[]\r asdfghjkl;'` \\zxcvbnm,./    ";
		static const char uppercode[] =
		    " \033!@#$%^&*()_+\177\tQWERTYUIOP{}\r ASDFGHJKL:\"~ |ZXCVBNM<>?    ";
		if (key == 96)
			key = 28;
/* pull keycode down to numbers if numlock numpad keys are hit */
/* not yet implemented */
		if (key > KEY_SPACE)
			goto done;
		keychar = (ss & ACS_SS_SHIFT) ? uppercode[key] : lowercode[key];
		if (keychar == ' ' && key != KEY_SPACE)
			goto done;
		if (keychar == '\r')
			ss = 0;
/* don't know how to echo alt keys */
		if (ss & ACS_SS_ALT)
			goto done;
		if (ss & ACS_SS_CTRL && isalpha(keychar))
			keychar = (keychar | 0x20) - ('a' - 1);
		raw_spin_lock_irqsave(&acslock, irqflags);
		if (nkeypending == MAXKEYPENDING)
			dropKeysPending(1);
		inkeybuffer[nkeypending] = keychar;
		inkeytime[nkeypending] = jiffies;
		++nkeypending;
		raw_spin_unlock_irqrestore(&acslock, irqflags);
		goto done;
	}

stop:
	return NOTIFY_STOP;

done:
	return NOTIFY_DONE;
}				/* keystroke */

static struct notifier_block nb_key = {
	.notifier_call = keystroke,
	.priority = 20
};

/* load and unload the module */

static int __init acsint_init(void)
{
	int rc;

	in_use = false;
	clear_keys();

	if (major == 0)
		rc = misc_register(&acsint_dev);
	else
		rc = register_chrdev(major, ACSINT_DEVICE, &fops);
	if (rc)
		return rc;
	if (major == 0)
		printk(KERN_NOTICE "registered acsint, major %d minor %d\n",
		       MISC_MAJOR, acsint_dev.minor);

	rc = register_vt_notifier(&nb_vt);
	if (rc) {
		if (major == 0)
			misc_deregister(&acsint_dev);
		else
			unregister_chrdev(major, ACSINT_DEVICE);
		return rc;
	}

	rc = register_keyboard_notifier(&nb_key);
	if (rc) {
		unregister_vt_notifier(&nb_vt);
		if (major == 0)
			misc_deregister(&acsint_dev);
		else
			unregister_chrdev(major, ACSINT_DEVICE);
		return rc;
	}

	register_console(&acsintconsole);

	return 0;
}

static void __exit acsint_exit(void)
{
	int j;

	unregister_console(&acsintconsole);
	unregister_keyboard_notifier(&nb_key);
	unregister_vt_notifier(&nb_vt);
	if (major == 0)
		misc_deregister(&acsint_dev);
	else
		unregister_chrdev(major, ACSINT_DEVICE);

	for (j = 0; j < MAX_NR_CONSOLES; ++j)
		if (cbuf_tty[j])
			kfree(cbuf_tty[j]);
}

module_init(acsint_init);
module_exit(acsint_exit);
