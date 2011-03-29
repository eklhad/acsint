/* Acsint - Accessibility intercepter.
 * Originally written by Saqib Shaikh in 2005.
 * Modified by Karl Dahlke in 2011 to use notifiers.
 */

#include <linux/kernel.h>
#include <linux/notifier.h>
#include <linux/keyboard.h>
#include <linux/kbd_kern.h>
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
#include <linux/ttyclicks.h>
#include <linux/acsint.h>


#define ACSINT_DEVICE "/dev/acsint"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Karl Dahlke - eklhad@gmail.com");
MODULE_DESCRIPTION("Accessibility intercepter - pass keystroke and tty events to user space");

static int major = 0;
module_param(major, int, 0);
MODULE_PARM_DESC(major,
		 "major number for /dev/acsint, default is dynamic allocation through misc_register");


// For various critical sections of code
static DEFINE_RAW_SPINLOCK(acslock);
// When we must disable interrupts
static unsigned long irqflags;


struct cbuf { // circular buffer
char area[TTYLOGSIZE1];
char *start, *end, *head, *tail, *mark;
};
static struct cbuf cbuf_tty[NUMVIRTUALCONSOLES];
// point to my area, for the active tty
static struct cbuf *cb;

/*********************************************************************
In this paragraph I'm going to try to convince myself that I
don't have to turn interrupts off every time I touch the circular buffer.
The vt notifier adds characters to the circular buffer.
It runs at process level.
It's not part of an interrupt handler.
The read function passes characters from the circular buffer into user space -
characters that you haven't seen before.
It does this because there is a keystroke event,
or some other action that means you might be reading text.
This is also at process level.
So you hit a key, the adapter swaps in, I am copying the new chars
to the adapter, when suddenly it swaps out and a process,
running on the foreground tty, generates more tty output.
Or ... a program spewing output is preempted by the adapter,
which wants to see the latest tty characters.
Under PREEMPT_NONE this can't happen,
but if your kernel has the highest level of preemption I suppose it could.
So I guess circular buffer operations are considered
critical code after all.   (sigh)
I hate messing with spinlocks, cause I don't know what Im doing!
Well I don't think I need an irq lock here, just something threadsafe.
*********************************************************************/

static void
cb_reset(void)
{
cb->start = cb->head = cb->tail = cb->mark = cb->area;
cb->end = cb->area + TTYLOGSIZE1;
} // cb_reset

// put a character on the end of the circular buffer
static void
cb_append(char c)
{
*cb->head = c;
++cb->head;
if(cb->head == cb->end) cb->head = cb->start;
if(cb->head == cb->tail) {
// buffer full, drop the last character
if(cb->tail == cb->mark) cb->mark = 0;
++cb->tail;
if(cb->tail == cb->end) cb->tail = cb->start;
}
} // cb_append


/*********************************************************************
Indicate which keys should be passed on to userspace.
Each element should be set to the appropriate shiftstate(s)
as defined in acsint.h.
Relay keys in those designated shift states.
Otherwise they go on to the console.
Set to -1 if the key is discarded entirely.
*********************************************************************/

static int acsint_keys[ACS_NUM_KEYS];
static char key_divert, key_bypass, key_echo;
static bool mustrefresh = false;

static void clear_keys(void)
{
int i;
for (i=0; i<ACS_NUM_KEYS; i++)
acsint_keys[i]=0;
} // clear_keys


// The array "rbuf" is used for passing key/tty events to user space
#define RBUF_LEN 1024
static char rbuf[RBUF_LEN];
static const char *rbuf_end=rbuf+RBUF_LEN;
// Despite the names head and tail, it's not a circular buffer.
// The process has to read the data before we reach rbuf_end, or data is lost.
static char *rbuf_tail, *rbuf_head;
static int rbuf_len;

// Wait until this driver has some data to read.
DECLARE_WAIT_QUEUE_HEAD(wq);

static int in_use; // only one process opens this device at a time
static int last_fgc; // last foreground console

// Push characters onto the input tty for the foreground console.
static void
tty_pushstring(const char *cp, int len)
{
char c;
	struct tty_struct *tty;
	struct vc_data *d = vc_cons[last_fgc-1].d;
if(!d) return;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,36)
tty = d->vc_tty;
#else
tty = d->port.tty;
#endif
if(!tty) return;

while(len) {
get_user(c, cp);
		tty_insert_flip_char(tty, c, 0);
		cp++;
--len;
	}

	con_schedule_flip(tty);
} /* tty_pushstring */

// File operations for /dev/acsint.

static int device_open(struct inode *inode, struct file *file)
{
int j;

if (in_use) return -EBUSY;

cb = cbuf_tty;
for(j=0; j<NUMVIRTUALCONSOLES; ++j, ++cb)
cb_reset();

clear_keys();

in_use = 1;
key_divert = key_bypass = key_echo = 0;

// At startup we tell the process which virtual console it is on.
// Place this directive in rbuf to be read.
rbuf[0] = ACSINT_FGC;
last_fgc = fg_console+1;
if(last_fgc <= 0 || last_fgc > 6)
last_fgc = 1; // should never happen
rbuf[1] = last_fgc;
rbuf_tail=rbuf;
rbuf_head=rbuf + 2;
rbuf_len=2;
return 0;
} // device_open

static int device_close(struct inode *inode, struct file *file)
{
in_use=0;
rbuf_len = 0;
return 0;
} // device_close

static ssize_t device_read(struct file *file, char *buf, size_t len, loff_t *offset)
{
int bytes_read=0;
int ccbytes;
int mino, minor;
bool catchup;
char cubuf[4]; // for the catch up command
int culen; // catch up length
int retval = wait_event_interruptible(wq, (rbuf_len>0));

if (retval==ERESTARTSYS) {
return 0;
}

// you can only read on behalf of the foreground console
minor = last_fgc;
mino = minor - 1;

// rbuf contains at most one fgc command, at the front.
// If so, we catch up right after that.
// Otherwise catch up just before the first keystroke.
// No need to catch up if there is just a MORECHARS command,
// because the adapter hasn't shown any interest yet.
// But the adapter might have issued a refresh command, and then we must.

	raw_spin_lock_irqsave(&acslock, irqflags);
catchup = mustrefresh;
mustrefresh = false;

cb = cbuf_tty + mino;
if(catchup || cb->head != cb->mark) {

if(*rbuf_tail == ACSINT_FGC && len >= 2) {
ccbytes = copy_to_user(buf, rbuf_tail, 2);
rbuf_len-=2;
rbuf_tail+=2;
bytes_read += 2;
buf += 2;
len -= 2;
catchup = true;
}

// MORECHARS is a single byte, anything beyond that is keystrokes
if(rbuf_len > 1) catchup = true;

if(catchup) {
if(cb->mark == 0) cb->mark = cb->tail;
// There just has to be enough room, or I'm not even going to try
if(cb->head >= cb->mark) culen = cb->head - cb->mark;
else culen = (cb->end - cb->mark) + (cb->head - cb->start);
if(len >= culen + 4) {
cubuf[0] = ACSINT_TTY_NEWCHARS;
cubuf[1] = minor;
cubuf[2] = culen;
cubuf[3] = (culen >> 8);
ccbytes = copy_to_user(buf, cubuf, 4);
bytes_read += 4;
buf += 4;
len -= 4;
// one clump or two
if(cb->head >= cb->mark) {
if(culen) ccbytes = copy_to_user(buf, cb->mark, culen);
bytes_read += culen;
buf += culen;
len -= culen;
} else {
culen = cb->end - cb->mark;
ccbytes = copy_to_user(buf, cb->mark, culen);
bytes_read += culen;
buf += culen;
len -= culen;
culen = cb->head - cb->start;
if(culen) ccbytes = copy_to_user(buf, cb->start, culen);
bytes_read += culen;
buf += culen;
len -= culen;
}
} // room to catch up

cb->mark = cb->head;
} // catching up
} // new characters not seen

// Now pass down the rest of the events.
// Could be nothing, if all we had was FGC.
if (rbuf_len<=len) {
if(rbuf_len)
ccbytes = copy_to_user(buf, rbuf_tail, rbuf_len);
rbuf_head = rbuf_tail=rbuf;
bytes_read+=rbuf_len;
rbuf_len=0;
} else {
// This really shouldn't ever happen.
// The reading buffer should be large enough to catch up,
// plus dozens of keystroke events.
ccbytes = copy_to_user(buf, rbuf_tail, len);
rbuf_len-=len;
rbuf_tail+=len;
bytes_read+=len;
}
	raw_spin_unlock_irqrestore(&acslock, irqflags);

*offset += bytes_read;
return bytes_read;
} // device_read

static ssize_t device_write(struct file *file, const char *buf, size_t len, loff_t *offset)
{
char c;
const char *p = buf;
int j, key, shiftstate, bytes_read;
int nn; // number of notes
short notes[2*(10+1)];
int isize; // size of input to inject

while(len) {
get_user(c, p++);
len--;

switch (c) {
case ACSINT_CLEAR_KEYS:
clear_keys();
break;

case ACSINT_SET_KEY:
if(len < 2) break;
get_user(key, p++);
key = (unsigned char)key;
len--;
get_user(shiftstate, p++);
len--;
if(key < ACS_NUM_KEYS)
acsint_keys[key]=shiftstate;
break;

case ACSINT_UNSET_KEY:
if(len < 1) break;
get_user(key, p++);
key = (unsigned char)key;
len--;
if(key < ACS_NUM_KEYS)
acsint_keys[key]=0;
break;

case ACSINT_CLICK:
ttyclicks_click();
break;

case ACSINT_CR:
ttyclicks_cr();
break;

case ACSINT_SOUNDS:
if(len < 1) break;
get_user(c, p++);
len--;
ttyclicks_on = c;
break;

case ACSINT_SOUNDS_TTY:
if(len < 1) break;
get_user(c, p++);
len--;
ttyclicks_tty = c;
break;

case ACSINT_SOUNDS_KMSG:
if(len < 1) break;
get_user(c, p++);
len--;
ttyclicks_kmsg = c;
break;

case ACSINT_NOTES:
if(len < 1) break;
get_user(nn, p++);
len--;
for(j=0; j<nn && j < 10 && len >= 3; ++j, len-=3) {
get_user(c, p++);
notes[2*j] = (unsigned char) c;
get_user(c, p++);
notes[2*j] |= ((short)c << 8);
get_user(c, p++);
notes[2*j+1] = (unsigned char)c;
}
notes[2*j] = 0;
if(j) ttyclicks_notes(notes);
for(; j<nn && len >= 3; ++j, len-=3) {
p += 3;
}
break;

case ACSINT_BYPASS:
key_bypass = 1;
break;

case ACSINT_DIVERT:
if(len < 1) break;
get_user(c, p++);
len--;
key_divert = c;
break;

case ACSINT_ECHO:
if(len < 1) break;
get_user(c, p++);
len--;
key_echo = c;
break;

case ACSINT_REFRESH:
	raw_spin_lock_irqsave(&acslock, irqflags);
if(rbuf_len) {
mustrefresh = true;
} else if(rbuf_head < rbuf_end) {
*rbuf_head++ = ACSINT_REFRESH;
++rbuf_len;
mustrefresh = true;
if(rbuf_len == 1) wake_up_interruptible(&wq);
}
	raw_spin_unlock_irqrestore(&acslock, irqflags);
break;

case ACSINT_PUSH_TTY:
if(len < 2) break;
get_user(c, p++);
isize = (unsigned char)c;
get_user(c, p++);
isize |= ((unsigned short)c <<8);
len -= 2;
if(len < isize) break;
tty_pushstring(p, isize);
p += isize, len -= isize;
break;

} // switch
} // loop processing config instructions

bytes_read=p-buf;
*offset+=bytes_read;
return bytes_read;
} // device_write

static unsigned int device_poll(struct file *fp, poll_table *pt)
{
unsigned int mask = 0;
// we don't support poll writing. How to figure if the buffer is not full?
if (rbuf_len > 0)
mask = POLLIN | POLLRDNORM;
poll_wait(fp, &wq, pt);
return mask;
} // device_poll

static struct file_operations fops={
owner: THIS_MODULE,
open: device_open,
release: device_close,
read: device_read,
write: device_write,
poll: device_poll,
};

static struct miscdevice acsint_dev = {
.minor = MISC_DYNAMIC_MINOR,
.name = "acsint",
.fops = &fops,
};


// Push a character onto the tty log.
// Called from the vt notifyer and from my printk console.
static void
pushlog(char c, int minor)
{
int mino = minor - 1;
bool wake = false;

	raw_spin_lock_irqsave(&acslock, irqflags);
cb = cbuf_tty + mino;
if(cb->mark == cb->head && minor == last_fgc && rbuf_head < rbuf_end) {
// throw the "more stuff" event
if(!rbuf_len) wake = true;
*rbuf_head++ = ACSINT_TTY_MORECHARS;
++rbuf_len;
}

cb_append(c);

if(wake) wake_up_interruptible(&wq);
	raw_spin_unlock_irqrestore(&acslock, irqflags);
} // pushlog

/*********************************************************************
And now we need a console, to capture printk() text
and push it onto the buffer.
It didn't come from the tty, but we want to read it nonetheless.
*********************************************************************/

static void my_printk(struct console *cons, const char *msg, unsigned int len)
{
	char c;
if(!in_use) return;
	while (len--) {
		c = *msg++;
pushlog(c, last_fgc);
	}
}				// my_printk

static struct console acsintconsole = {
name:	"acsint",
write:	my_printk,
flags:	CON_ENABLED,
// hope everything else is ok being zero or null
};


/*********************************************************************
Notifiers: keyboard events and tty events.
*********************************************************************/

static int
vt_out(struct notifier_block *this_nb, unsigned long type, void *data)
{
	struct vt_notifier_param *param = data;
	struct vc_data *vc = param->vc;
	int minor = vc->vc_num + 1;
	int unicode = param->c;
	char c = param->c;
int new_fgc;
bool wake = false;

if(!in_use)
goto done;

if (type == VT_UPDATE) {
new_fgc = fg_console+1;
//temporary hack
if (new_fgc > 6) new_fgc = 1;
if (new_fgc != last_fgc) {
last_fgc = new_fgc;
	raw_spin_lock_irqsave(&acslock, irqflags);
// This command displaces all others.
// We'll catch up when it is read.
if(!rbuf_len) wake = true;
rbuf_head = rbuf_tail = rbuf;
*rbuf_head++=ACSINT_FGC;
*rbuf_head++=new_fgc;
rbuf_len=2;
if(wake) wake_up_interruptible(&wq);
	raw_spin_unlock_irqrestore(&acslock, irqflags);
} // we switched
} //vt_update

	if (type != VT_PREWRITE)
		goto done;

// I don't log, or pass back to the adapter, null bytes in the output stream
if(unicode == 0)
goto done;

	if (unicode >= 256) {
/*********************************************************************
I don't handle international chars beyond ISO8859-1.
thus unicode beyond 256 is discarded.
If you are using another character set,
then you have to map those unicodes back to bytes,
and this is the place to do it.
That means we need a setlocale command.
None of this is implemented yet.
*********************************************************************/
		goto done;
	}

pushlog(c, minor);

done:
	return NOTIFY_DONE;
}				// vt_out

static struct notifier_block nb_vt = {
	.notifier_call = vt_out,
.priority = 20
};

static int
keystroke(struct notifier_block *this_nb, unsigned long type, void *data)
{
	struct keyboard_notifier_param *param = data;
	char key = param->value;
	int downflag = param->down;
	int ss = param->shift;
char action;
static char ischort[] = {
0,0,0,1,0,1,1,1,0,1,1,1,1,1,1,1,0};
bool wake = false, keep = false, send = false;
char divert, echo, bypass;

if(!in_use) goto done;

	if (type != KBD_KEYCODE)
		goto done;

/* Only the key down events */
	if (downflag == 0)
		goto done;

	if (key == 0 || key >= 128)
		goto done;

ss &= 0xf;
if(!ss) ss = ACS_SS_PLAIN;

action = 0;
if(key < ACS_NUM_KEYS)
action = acsint_keys[(int)key];
if(action < 0) goto stop;

divert = key_divert;
echo = key_echo;
bypass = key_bypass;
// But we don't redirect the meta keys
if(divert|echo|bypass) {
if(key == KEY_LEFTCTRL ||
key == KEY_RIGHTCTRL ||
key == KEY_LEFTSHIFT ||
key == KEY_RIGHTSHIFT ||
key == KEY_LEFTALT ||
key == KEY_RIGHTALT ||
key == KEY_CAPSLOCK ||
key == KEY_NUMLOCK ||
key == KEY_SCROLLLOCK)
divert = echo = bypass = 0;
}

if(divert | echo) keep = true;
if(bypass) {key_bypass = 0; send = true; goto event; }

// keypad is assumed to be numbers with numlock on,
// perhaps speech functions otherwise.
if(param->ledstate & ACS_LEDS_NUMLOCK &&
key >= KEY_KP7 && key <= KEY_KPDOT &&
key != KEY_KPMINUS && key != KEY_KPPLUS)
goto regular;

if(action != ACS_SS_ALL) {
if(!(action&ss)) goto regular;
// only one of shift, lalt, ralt, or control
if(ischort[ss]) goto regular;
}
keep = true;
goto event;

regular:
// Just a regular key.
if(!divert) send = true;

event:
if (keep && rbuf_head<=rbuf_end-4) {
if(!rbuf_len) wake = true;
*rbuf_head++=ACSINT_KEYSTROKE;
*rbuf_head++=key;
*rbuf_head++=ss;
*rbuf_head++=param->ledstate;
rbuf_len+=4;
if(wake) wake_up_interruptible(&wq);
}

if(send) goto done;

stop:
return NOTIFY_STOP;

done:
	return NOTIFY_DONE;
}				// keystroke

static struct notifier_block nb_key = {
	.notifier_call = keystroke,
.priority = 20
};


// Load and unload the module.

static int __init acsint_init(void)
{
	int rc;

in_use=0;
clear_keys();

if(major == 0)
rc = misc_register(&acsint_dev);
else
rc = register_chrdev(major, ACSINT_DEVICE, &fops);
if(rc)
return rc;
if(major == 0)
printk(KERN_NOTICE "registered acsint, major %d minor %d\n",
MISC_MAJOR, acsint_dev.minor);

	rc = register_vt_notifier(&nb_vt);
	if (rc) {
if(major == 0)
misc_deregister(&acsint_dev);
else
unregister_chrdev(major, ACSINT_DEVICE);
		return rc;
}

	rc = register_keyboard_notifier(&nb_key);
	if (rc) {
		unregister_vt_notifier(&nb_vt);
if(major == 0)
misc_deregister(&acsint_dev);
else
unregister_chrdev(major, ACSINT_DEVICE);
		return rc;
	}

	register_console(&acsintconsole);

	return 0;
}				// acsint_init

static void __exit acsint_exit(void)
{
	unregister_console(&acsintconsole);
	unregister_keyboard_notifier(&nb_key);
	unregister_vt_notifier(&nb_vt);
if(major == 0)
misc_deregister(&acsint_dev);
else
unregister_chrdev(major, ACSINT_DEVICE);
}				/* acsint_exit */

module_init(acsint_init);
module_exit(acsint_exit);
