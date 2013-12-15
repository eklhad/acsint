/*********************************************************************

hq_user.c: modify the keyboard for one-handed typing.
Copyright Chris Brannon and Karl Dahlke, 2008.
This software is GPL.

A loadable module version of this program exists
in project acsint, drivers/hq_kern.c.

*********************************************************************/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <syslog.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <linux/vt.h>

/*********************************************************************
Here are the mappings for the half-qwerty keyboard.
Note: only one key differs between the lowercase and uppercase tables.
That is, lowercode[1] is KEY_MINUS, and uppercode[1] is KEY_EQUAL.  
KEY_EQUAL generates the plus character when shifted.
The symbolic key names from <linux/input.h> refer to the unshifted version of
the key.
*********************************************************************/

static const unsigned int lowercode[] = {
    KEY_SPACE, KEY_MINUS, KEY_0, KEY_9, KEY_8, KEY_7,
    KEY_6, KEY_5, KEY_4, KEY_3, KEY_2, KEY_1,
    KEY_GRAVE, KEY_ESC,
    KEY_SPACE,
    KEY_LEFTBRACE, KEY_P, KEY_O, KEY_I, KEY_U, KEY_Y, KEY_T, KEY_R, KEY_E,
    KEY_W, KEY_Q, KEY_TAB, KEY_SPACE, KEY_SPACE, KEY_SPACE,
    KEY_SEMICOLON, KEY_L, KEY_K, KEY_J, KEY_H, KEY_G, KEY_F, KEY_D, KEY_S,
    KEY_A,
    KEY_SPACE, KEY_SPACE, KEY_SPACE, KEY_SPACE,
    KEY_SLASH, KEY_DOT, KEY_COMMA, KEY_M, KEY_N, KEY_B, KEY_V, KEY_C, KEY_X,
    KEY_Z
};

static const unsigned int uppercode[] = {
    KEY_SPACE, KEY_EQUAL, KEY_0, KEY_9, KEY_8, KEY_7,
    KEY_6, KEY_5, KEY_4, KEY_3, KEY_2, KEY_1,
    KEY_GRAVE, KEY_ESC,
    KEY_SPACE,
    KEY_LEFTBRACE, KEY_P, KEY_O, KEY_I, KEY_U, KEY_Y, KEY_T, KEY_R, KEY_E,
    KEY_W, KEY_Q, KEY_TAB, KEY_SPACE, KEY_SPACE, KEY_SPACE,
    KEY_SEMICOLON, KEY_L, KEY_K, KEY_J, KEY_H, KEY_G, KEY_F, KEY_D, KEY_S,
    KEY_A,
    KEY_SPACE, KEY_SPACE, KEY_SPACE, KEY_SPACE,
    KEY_SLASH, KEY_DOT, KEY_COMMA, KEY_M, KEY_N, KEY_B, KEY_V, KEY_C, KEY_X,
    KEY_Z
};

typedef enum {
    FALSE = 0, TRUE = 1
} bool_t;
typedef void (*sighandler_t) (int);


const unsigned short NO_SUCH_VT = 0xffff;
int keyboard_fd = -1;		/* Source of keyboard events. */
int uinput_fd = -1;		/* File descriptor of uinput device. */
int console_fd = -1;		/* So we can get keyboard shift state. */
/* mustQuit set by signal handler, so needs to be volatile. */
volatile sig_atomic_t mustQuit = 0;

void install_signal_handlers(void);
void open_input_subsystem(void);
void close_input_subsystem(void);
void keyboard_event_loop(void);
void handle_keystroke(struct input_event *key, unsigned char shiftState);
int open_console(void);
void bail(const char *message, bool_t display_errno);
void handle_user_abort(int signum);
unsigned char get_shift_state(void);
unsigned short get_active_vt(void);

int
main(int argc, char **argv)
{
    openlog("halfqwerty", LOG_NDELAY, LOG_USER);
    syslog(LOG_INFO, "started.");
    install_signal_handlers();
    open_input_subsystem();
    daemon(0, 0);		/* Become a daemon process. */
    keyboard_event_loop();	/* This is where the action is. */
    close_input_subsystem();
    syslog(LOG_INFO, "Terminating at user request.");
    closelog();
    return 0;
}


/* Install SIGINT, SIGTERM, and  SIGQUIT handlers. */
void
install_signal_handlers(void)
{
    signal(SIGTERM, handle_user_abort);
    signal(SIGQUIT, handle_user_abort);
    signal(SIGINT, handle_user_abort);
    signal(SIGHUP, SIG_IGN);
}

/* Open keyboard and uinput devices. */
/* Is /dev/input/event0 the politically correct name for the keyboard device? */
void
open_input_subsystem(void)
{
    struct uinput_user_dev uinp;
    int ret = -1, i = 0;
    console_fd = open_console();
    if(console_fd < 0)
	bail("Unable to open console", TRUE);	/* uninformative */
    keyboard_fd = open("/dev/input/event0", O_RDONLY);
    if(keyboard_fd < 0)
	bail("Unable to open keyboard device (is the evdev module loaded?)",
	   TRUE);
    uinput_fd = open("/dev/input/uinput", O_RDWR);
    if(uinput_fd < 0)
	bail("Unable to open uinput device (is the uinput module loaded?)",
	   TRUE);

/*
    Initialize uinput device.  This code was copied from the following source:
    http://www.mail-archive.com/linux-input@vger.kernel.org/msg00063.html
*/
    memset(&uinp, 0, sizeof (uinp));
    strncpy(uinp.name, "halfqwerty", 10);
    uinp.id.version = 1;	/* what does this do? */
    uinp.id.bustype = BUS_I8042;	/* and this? */

/* We generate keypresses, possibly with repetition. */
    ioctl(uinput_fd, UI_SET_EVBIT, EV_KEY);
    ioctl(uinput_fd, UI_SET_EVBIT, EV_REP);

/* We want to inject all possible keys: */
    for(i = 0; i < KEY_MAX; i++)
	ioctl(uinput_fd, UI_SET_KEYBIT, i);

    ret = write(uinput_fd, &uinp, sizeof (uinp));
    if(ret == -1)
	bail("Error initializing uinput device", TRUE);
    ret = (ioctl(uinput_fd, UI_DEV_CREATE));
    if(ret)
	bail("Error initializing uinput device", TRUE);

/* Grab exclusive access to keyboard device. */
    ioctl(keyboard_fd, EVIOCGRAB, 1);
}

void
keyboard_event_loop(void)
{
unsigned short active_vt = NO_SUCH_VT;
    struct input_event keyEvent;
    unsigned char shiftState = 0;

    while(mustQuit == 0) {
	ssize_t numWritten;
	ssize_t numRead = read(keyboard_fd, &keyEvent, sizeof (keyEvent));
	if(numRead != sizeof (keyEvent))
	    bail("Error reading keyboard event", TRUE);


/* Discard events that aren't keystrokes. */
	if(keyEvent.type != 1)
	    continue;

active_vt = get_active_vt(); /* used for side-effect, return val unused. */
	shiftState = get_shift_state();
	handle_keystroke(&keyEvent, shiftState);
    }
}


/* handle_keystroke implements the rules for half-qwerty. */
void
handle_keystroke(struct input_event *key, unsigned char shiftState)
{
    const unsigned int *remap;
    bool_t is_shift = (shiftState & 0x01) != 0;
    bool_t is_alt = (shiftState & 0x0a) != 0;
    bool_t is_ctrl = (shiftState & 0x04) != 0;
    static bool_t spaceDown = FALSE, spaceUsed = FALSE;

/* Handle the spacebar first. */
    if(key->code == KEY_SPACE) {
	if(key->value == 0) {	/* space released. */
	    if(spaceDown && !spaceUsed) {
/* Generate space-depress and space-release events. */
		key->value = 1;	/* depress */
		write(uinput_fd, key, sizeof (struct input_event));
		key->value = 0;	/* release */
		gettimeofday(&(key->time), NULL);	/* don't generate 2 events simultaneously */
/* But could this ever be the same time as the original event? */
		write(uinput_fd, key, sizeof (struct input_event));
	    }
	    spaceDown = FALSE;
	} else if(key->value == 1)	/* depress */
	    spaceDown = TRUE, spaceUsed = FALSE;
	else			/* autorepeat.  Just ignore it. */
	    ;			/* empty else.  */
	return;
    }

/*
control and alt chords aren't handled yet.  Just pass through.  Function keys
and similar aren't modified.  Finally, we pass through if spacebar is released.
*/
    if(is_ctrl || is_alt || (key->code > 0x35) || (!spaceDown)) {
	write(uinput_fd, key, sizeof (struct input_event));
	return;
    }

    spaceUsed = TRUE;

    remap = (is_shift ? uppercode : lowercode);
    if(remap[key->code] != KEY_SPACE)
	key->code = remap[key->code];
/* empty else clause.   do nothing.  Don't remap, and just pass it through. */
    write(uinput_fd, key, sizeof (struct input_event));
}

void
close_input_subsystem(void)
{
/* We might be called from "bail".  (x == -1) tests aren't redundant. */

    if(keyboard_fd != -1) {
/* Release exclusive access to keyboard device. */
	ioctl(keyboard_fd, EVIOCGRAB, 0);
	close(keyboard_fd);
    }

    if(uinput_fd != -1)
	close(uinput_fd);
}

int
open_console(void)
{
    const char consoleName[] = "/dev/console";
    return open(consoleName, O_RDONLY);
}

/* Begin miscellaneous utility functions. */

/*
Exit after writing a message to syslog.  If this was an OS error, then
this function displays "message: errordesc" where errordesc describes
the current value of errno.
Side-effect: closes the input subsystem devices.
*/

void
bail(const char *message, bool_t display_errno)
{
    close_input_subsystem();
    if(isatty(2)) {
	if(display_errno)
	    fprintf(stderr, "%s: %s", message, strerror(errno));
	else
	    fprintf(stderr, "%s", message);
	fprintf(stderr, "\n");
    }
    exit(1);
}

/* User aborted the program, via SIGQUIT or SIGINT.  Just set a flag. */
void
handle_user_abort(int signum)
{
    mustQuit = 1;
}

/*
get_shift_state: return the shift state of the keyboard.  Uses the TIOCLINUX
ioctl.
*/
unsigned char
get_shift_state(void)
{
    int ret = -1;
    char arg = 6;
    ret = ioctl(console_fd, TIOCLINUX, &arg);
    if(ret != 0)
	bail("Unable to get shift state of keyboard", TRUE);
    return arg;
}

/*
Return the number of the active VT.  On error, returns largest unsigned short.
Note: the VT_GETSTATE has a side-effect.  If the user switches consoles,
/dev/console VT_GETSTATE causes /dev/console to refer to the newly-active
console.  We'll exploit that side-effect, but it seems like bad style...
*/
unsigned short
get_active_vt(void)
{
    unsigned short cur_vt = 0;
    struct vt_stat vStat;
    int ret = ioctl(console_fd, VT_GETSTATE, &vStat);
    if(ret < 0)
	cur_vt = NO_SUCH_VT;
    else
	cur_vt = vStat.v_active;
    return cur_vt;
}

