/*********************************************************************
File: acsbridge.c
Description: Encapsulates communication between userspace and kernel via
the /dev/acsint device.  This file implements the interface described
and declared in acsbridge.h.
*********************************************************************/

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <malloc.h>
#include <memory.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include <signal.h>

#include <linux/vt.h>

#include "acsbridge.h"

#define stringEqual !strcmp

#define MAX_ERRMSG_LEN 256 // description of error
#define MAXNOTES 10 // how many notes to play in one call
#define IOBUFSIZE (TTYLOGSIZE*4 + 2000) /* size of input buffer */
/* I assume the screen doesn't have more than 5000 characters,
 * and TTYLOGSIZE is at least 10000.
 * 40 rows by 120 columns is, for instance, 4800 */
#define ATTRIBOFFSET 5000
#define VCREADOFFSET 7000

int acs_fd = -1; // file descriptor for /dev/acsint
static int vcs_fd; // file descriptor for /dev/vcsa
static unsigned char vcs_header[4];
#define nrows (int)vcs_header[0]
#define ncols (int)vcs_header[1]
#define csr (int)vcs_header[3] // cursor row
#define csc (int)vcs_header[2] // cursor column
int acs_fgc = 1; // current foreground console
int acs_lang; /* language that the adapter is running in */
int acs_postprocess = 0xf; // postprocess the text from the tty
int acs_debug = 0;
static const char debuglog[] = "acslog";

// for debugging: save a message, without sending it to tty,
// which would just generate more events.
int acs_log(const char *msg, ...)
{
va_list args;
FILE *f;
va_start(args, msg);
if ((f = fopen(debuglog, "a")) == NULL)
return -1;
vfprintf(f, msg, args);
va_end(args);
fclose(f);
return 0;
} // acs_log

key_handler_t acs_key_h;
more_handler_t acs_more_h;
fgc_handler_t acs_fgc_h;
ks_echo_handler_t acs_ks_echo_h;

static int cerror; /* Indicate communications error. */
static char errorDesc[MAX_ERRMSG_LEN];

static unsigned char iobuf[IOBUFSIZE]; // input output buffer for acsint

// set, clear, and report errors

static void
setError(void)
{
const char *desc = strerror(errno);
cerror = errno;
strncpy(errorDesc, desc, MAX_ERRMSG_LEN);
errorDesc[MAX_ERRMSG_LEN - 1] = 0;
} // setError

static void
clearError(void)
{
cerror = 0;
errorDesc[0] = 0;
} // clearError

int
acs_errno(void)
{
return cerror;
} // acs_errno

const char *
acs_errordesc(void)
{
return errorDesc;
} // acs_errordesc

// Maintain the tty log for each virtual console.
static struct readingBuffer *tty_log[MAX_NR_CONSOLES];
static struct readingBuffer tty_nomem; /* in case we can't allocate */
static const char nomem_message[] = "Acsint bridge cannot allocate space for this console";
static struct readingBuffer *tl; // current tty log
static struct readingBuffer screenBuf;
static int screenmode; // 1 = screen, 0 = tty log
struct readingBuffer *rb; /* current reading buffer for the application */

static void screenSnap(void)
{
unsigned int *t;
unsigned char *a, *s;
int i, j;

lseek(vcs_fd, 0, 0);
read(vcs_fd, vcs_header, 4);

screenBuf.area[0] = 0;
screenBuf.start = t = screenBuf.area + 1;
screenBuf.attribs = a = (unsigned char *) (screenBuf.area + ATTRIBOFFSET);
s = (unsigned char *) (screenBuf.area + VCREADOFFSET);
read(vcs_fd, s, 2*nrows*ncols);

for(i=0; i<nrows; ++i) {
for(j=0; j<ncols; ++j) {
*t++ = *s++;
*a++ = *s++;
}
*t++ = '\n';
*a++ = 0; // should this be 7?
}
*t = 0;
screenBuf.end = t;

screenBuf.v_cursor = screenBuf.start + csr * (ncols+1) + csc;
} // screenSnap

/* check to see if a tty reading buffer has been allocated */
static void
checkAlloc(void)
{
rb = tty_log[acs_fgc - 1];
if(rb && rb != &tty_nomem)
return; /* already allocated */

rb = malloc(sizeof(struct readingBuffer));
if(!rb) rb = &tty_nomem;
tty_log[acs_fgc-1] = rb;
rb->start = rb->area + 1;
rb->area[0] = 0;
if(rb == &tty_nomem) {
int j;
for(j=0; nomem_message[j]; ++j)
rb->start[j] = nomem_message[j];
rb->start[j] = 0;
rb->end = rb->start + j;
} else {
rb->end = rb->start;
rb->area[1] = 0;
}

rb->cursor = rb->start;
rb->v_cursor = 0;
rb->attribs = 0;
} /* checkAlloc */

void
acs_screenmode(int enabled)
{
imark_start = 0;
/* If you issued this command at the call of a keystroke,
 * and that is what I am expecting / assuming,
 * then yes the buffer will be caught up.
 * If it is called by some other automated process, or at startup,
 * then you will want to call acs_refresh to bring the buffer up to date,
 * at least in line mode.  I call screenSnap for you in screen mode. */
if(enabled) {
screenmode = 1;
screenSnap();
rb = &screenBuf;
rb->cursor = rb->v_cursor;
memset(rb->marks, 0, sizeof(rb->marks));
} else {
screenmode = 0;
checkAlloc();
}
} /* acs_screenmode */


// Open and close the device.

int
acs_open(const char *devname)
{
if(acs_fd >= 0) {
// already open
errno = EEXIST;
setError();
return -1;
}

if(acs_debug) unlink(debuglog);

vcs_fd = open("/dev/vcsa", O_RDONLY);
if(vcs_fd < 0) {
setError();
return -1;
}

acs_fd = open(devname, O_RDWR);
if(acs_fd < 0) {
close(vcs_fd);
setError();
return -1;
}

clearError();
acs_reset_configure();

return acs_fd;
} // acs_open

int
acs_close(void)
{
int rc = 0;
clearError();
if(acs_fd < 0) return 0; // already closed
if(close(acs_fd) < 0) {
setError(); // should never happen
rc = -1;
}
// Close it regardless.
acs_fd = -1;
return rc;
} // acs_close

// Write a command to /dev/acsint.

static int
acs_write(int n)
{
clearError();
if(acs_fd < 0) {
errno = ENXIO;
setError();
return -1;
}
if(write(acs_fd, iobuf, n) < n) {
setError();
return -1;
}
return 0;
} // acs_write

// Which sounds are generated automatically?

int
acs_sounds(int enabled) 
{
iobuf[0] = ACSINT_SOUNDS;
iobuf[1] = enabled;
return acs_write(2);
} // acs_sounds

int
acs_tty_clicks(int enabled) 
{
iobuf[0] = ACSINT_SOUNDS_TTY;
iobuf[1] = enabled;
return acs_write(2);
} // acs_tty_clicks

int
acs_kmsg_tones(int enabled) 
{
iobuf[0] = ACSINT_SOUNDS_KMSG;
iobuf[1] = enabled;
return acs_write(2);
} // acs_kmsg_tones

// Various routines to make sounds through the pc speaker.

int
acs_click(void)
{
iobuf[0] = ACSINT_CLICK;
return acs_write(1);
} // acs_click

int
acs_cr(void)
{
iobuf[0] = ACSINT_CR;
return acs_write(1);
} // acs_cr

int
acs_notes(const short *notelist)
{
int j;
iobuf[0] = ACSINT_NOTES;
for(j=0; j<MAXNOTES; ++j) {
if(!notelist[2*j]) break;
iobuf[2+3*j] = notelist[2*j];
iobuf[2+3*j+1] = notelist[2*j]>>8;
iobuf[2+3*j+2] = notelist[2*j+1];
}
iobuf[1] = j;
return acs_write(2+3*j);
} // acs_notes

int acs_bell(void)
{
static const short bellsnd[] = {
        1800,10,0,0     };
return acs_notes(bellsnd);
} // acs_bell

int acs_buzz(void)
{
static const short buzzsnd[] = {
        120,50,0,0     };
return acs_notes(buzzsnd);
} // acs_buzz

int acs_highcap(void)
{
static const short capsnd[] = {
        3000,3,0,0     };
return acs_notes(capsnd);
} // acs_highcap

int acs_highbeeps(void)
{
static const short boundsnd[] = {
        3172,3,3775,3,0,0       };
return acs_notes(boundsnd);
} // acs_highbeeps

int
acs_tone_onoff(int enabled)
{
static const short offsnd[] = {
	270,8,0,0	};
static const short onsnd[] = {
	700,12,0,0	};
return acs_notes(enabled ? onsnd : offsnd);
} // acs_tone_onoff

// redirect keystrokes for capture, monitor, and bypass

int
acs_divert(int enabled) 
{
iobuf[0] = ACSINT_DIVERT;
iobuf[1] = enabled;
return acs_write(2);
} // acs_divert

int
acs_monitor(int enabled) 
{
iobuf[0] = ACSINT_MONITOR;
iobuf[1] = enabled;
return acs_write(2);
} // acs_monitor

int
acs_bypass(void)
{
iobuf[0] = ACSINT_BYPASS;
return acs_write(1);
} // acs_bypass

/* Use divert to swallow a string.
 * This is not unicode at present. */
static char *swallow_string;
static int swallow_max, swallow_len;
static int swallow_prop, swallow_rc;
static key_handler_t save_key_h;
static void swallow_key_h(int key, int ss, int leds);
static void swallow1_h(int key, int ss, int leds);

int acs_keystring(char *buf, int buflen, int properties)
{
if(buflen <= 0) {
errno = ENOMEM;
setError();
return -1;
}

if(buflen == 1) {
*buf = 0;
return 0;
}

swallow_string = buf;
swallow_max = buflen;
swallow_len = 0;
swallow_prop = properties;
if(acs_divert(1)) return -1;
save_key_h = acs_key_h;
acs_key_h = swallow_key_h;

while(acs_key_h == swallow_key_h)
acs_events();

// At this point the key handler has put everything back.
return swallow_rc;
} // acs_keystring

const char lowercode[] =
" \0331234567890-=\b qwertyuiop[]\r asdfghjkl;'` \\zxcvbnm,./    ";
const char uppercode[] =
" \033!@#$%^&*()_+\b QWERTYUIOP{}\r ASDFGHJKL:\"~ |ZXCVBNM<>?    ";

// special handler for keystring()
static void swallow_key_h(int key, int ss, int leds)
{
char keychar;

if(key > KEY_SPACE) goto bad;
if(ss&ACS_SS_ALT) goto bad;
if(ss&ACS_SS_CTRL) {
if(key != KEY_H) goto bad;
if(!(swallow_prop&ACS_KS_BACKUP)) goto bad;
backup:
if(swallow_len) --swallow_len;
if(swallow_prop&ACS_KS_GOODCLICK) acs_click();
if(acs_ks_echo_h) (*acs_ks_echo_h)('\b');
return;
}

if(key == KEY_BACKSPACE) goto backup;

if(key == KEY_ESC) {
if(swallow_prop&ACS_KS_ESCCR) acs_cr();
swallow_rc = -1;
swallow_len = 0;
goto cleanup;
}

if(key == KEY_ENTER) {
swallow_rc = 0;
goto cleanup;
}

// This only works on a qwerty layout; not sure how to fix this.
keychar = (ss&ACS_SS_SHIFT) ? uppercode[key] : lowercode[key];
if(keychar == ' ' && key != KEY_SPACE) goto bad;
if(leds & K_CAPSLOCK && isalpha(keychar))
keychar ^= 0x20;

// looks ok, but is there room?
if(swallow_len+1 == swallow_max) {
if(swallow_prop&ACS_KS_BOUNDARYBEEPS) acs_highbeeps();
if(!(swallow_prop&ACS_KS_BOUNDARYSTOP)) return;
swallow_rc = -1;
goto cleanup;
}

if(swallow_prop&ACS_KS_GOODCLICK) {
if(isupper(keychar)) acs_highcap();
else acs_click();
}
if(acs_ks_echo_h) (*acs_ks_echo_h)(keychar);
swallow_string[swallow_len++] = keychar;
return;

bad:
if(swallow_prop&ACS_KS_BADBELL) acs_bell();
if(!(swallow_prop&ACS_KS_BADSTOP)) return;
swallow_rc = -1;

cleanup:
swallow_string[swallow_len] = 0;
acs_key_h = save_key_h;
acs_divert(0);
} // swallow_key_h

static int key1key, key1ss;
int acs_get1key(int *key_p, int *ss_p)
{
if(acs_divert(1)) return -1;
save_key_h = acs_key_h;
acs_key_h = swallow1_h;
while(acs_key_h == swallow1_h)
acs_events();
*key_p = key1key;
*ss_p = key1ss;
return 0;
} // acs_get1key

// special handler for get1key()
static void swallow1_h(int key, int ss, int leds)
{
key1key = key;
key1ss = ss;
acs_key_h = save_key_h;
acs_divert(0);
} // swallow1_h

int acs_get1char(char *p)
{
int key, state;
char keychar;
*p = 0;
if(acs_get1key(&key, &state)) return -1;
if(key > KEY_SPACE ||
state&(ACS_SS_ALT|ACS_SS_CTRL)) return -1;
// This only works on a qwerty layout; not sure how to fix this.
keychar = (state&ACS_SS_SHIFT) ? uppercode[key] : lowercode[key];
if(!isalnum(keychar)) return -1;
*p = keychar;
return 0;
} // acs_get1char


// set and unset keys

int acs_setkey(int key, int ss)
{
iobuf[0] = ACSINT_SET_KEY;
iobuf[1] = key;
iobuf[2] = ss;
return acs_write(3);
} // acs_setkey

int acs_unsetkey(int key)
{
iobuf[0] = ACSINT_UNSET_KEY;
iobuf[1] = key;
return acs_write(2);
} // acs_unsetkey

int acs_clearkeys(void)
{
iobuf[0] = ACSINT_CLEAR_KEYS;
return acs_write(1);
} // acs_clearkeys

static void
postprocess(unsigned int *s)
{
unsigned int *t;

if(!acs_postprocess) return;

// in case we had part of an ansi escape code
s -= 20;
if(s < tl->start) s = tl->start;
t = s;

while(*s) {

// crlf
if(*s == '\r' && s[1] == '\n' &&
acs_postprocess&ACS_PP_CRLF) {
++s;
continue;
}

// ^H
if(s[0] == '\b' && acs_postprocess&ACS_PP_CTRL_H) {
if(t[-1]) --t;
++s;
continue;
}

// ansi escape sequences
if(*s == '\33' && s[1] == '[' && acs_postprocess&ACS_PP_STRIP_ESCB) {
int j;
for(j=2; s[j] && j<20; ++j)
if(s[j] < 256 && isalpha(s[j])) break;
if(j < 20 && s[j]) {
/* a letter indicates end of escape sequence.
 * If the letter is H, we are repositioning the cursor.
 * Most of the time we are starting a new line.
 * If not, we are at least starting a new word or phrase.
 * In either case I find it helpful to introduce a newline.
 * That deliniates a new block of text,
 * and most of the time said text is indeed on a new line. */
			if(*s == 'H') *t++ = '\n';
s += j+1;
continue;
		}
}

// control chars
if(*s < ' ' && *s != '\7' && *s != '\r' && *s != '\n' && *s != '\t' &&
acs_postprocess&ACS_PP_STRIP_CTRL) {
++s;
continue;
}

*t++ = *s++;
}

tl->end = t;
*t = 0;

if(tl->cursor && tl->cursor >= t)
tl->cursor = (t > tl->start ? t-1 : t);
} // postprocess

void acs_clearbuf(void)
{
if(screenmode) return;
imark_start = 0;
if(rb && rb != &tty_nomem) {
rb->end = rb->cursor = rb->start;
memset(rb->marks, 0, sizeof(rb->marks));
}
} // acs_clearbuf

// events coming back
int acs_events(void)
{
int nr; // number of bytes read
int i, j;
int culen; /* catch up length */
unsigned int *custart; // where does catch up start
int nlen; // length of new area
int diff;
int minor;
char refreshed = 0;
unsigned int d;

clearError();
if(acs_fd < 0) {
errno = ENXIO;
setError();
return -1;
}

nr = read(acs_fd, iobuf, IOBUFSIZE);
if(acs_debug) acs_log("acsint read %d bytes\n", nr);
if(nr < 0) {
setError();
return -1;
}

i = 0;
while(i <= nr-4) {
switch(iobuf[i]) {
case ACSINT_KEYSTROKE:
if(acs_debug) acs_log("key %d\n", iobuf[i+1]);
// keystroke refreshes automatically in line mode;
// we have to do it here for screen mode.
if(screenmode && !refreshed) { screenSnap(); refreshed = 1; }
// check for macro here.
if(acs_key_h != swallow_key_h && acs_key_h != swallow1_h) {
// get the modified key code.
int mkcode = acs_build_mkcode(iobuf[i+1], iobuf[i+2]);
// see if this key has a macro
char *m = acs_getmacro(mkcode);
if(!m && iobuf[i+2]&ACS_SS_ALT) {
// couldn't find it on left alt or right alt, try generic alt
mkcode = acs_build_mkcode(iobuf[i+1], ACS_SS_ALT);
m = acs_getmacro(mkcode);
}
if(m) {
if(*m == '|')
system(m+1);
else
acs_injectstring(m);
i += 4;
break;
}
}
if(acs_key_h) acs_key_h(iobuf[i+1], iobuf[i + 2], iobuf[i+3]);
i += 4;
break;

case ACSINT_FGC:
if(acs_debug) acs_log("fg %d\n", iobuf[i+1]);
acs_fgc = iobuf[i+1];
if(screenmode) {
/* I hope linux has done the console switch by this time. */
screenSnap();
refreshed = 1;
rb->cursor = rb->v_cursor;
memset(rb->marks, 0, sizeof(rb->marks));
} else {
checkAlloc();
}
if(acs_fgc_h) acs_fgc_h();
i += 4;
break;

case ACSINT_TTY_MORECHARS:
if(i > nr-8) break;
d = *(unsigned int *) (iobuf+i+4);
if(acs_debug) {
acs_log("output echo %d", iobuf[i+1]);
if(iobuf[i+1]) acs_log(" 0x%x", d);
acs_log("\n");
}
// no automatic refresh here; you have to call it if you want it
if(acs_more_h) acs_more_h(iobuf[i+1], d);
i += 8;
break;

case ACSINT_REFRESH:
if(acs_debug) acs_log("refresh\n", 0);
i += 4;
break;

case ACSINT_TTY_NEWCHARS:
/* this is the refresh data in line mode
 * minor is always the foreground console; we could probably discard it. */
minor = iobuf[i+1];
culen = iobuf[i+2] | ((unsigned short)iobuf[i+3]<<8);
if(acs_debug) acs_log("new %d\n", culen);
i += 4;
if(!culen) break;
if(nr-i < culen*4) break;

tl = tty_log[minor - 1];
if(!tl || tl == &tty_nomem) {
/* not allocated; no room for this data */
i += culen*4;
break;
}

nlen = tl->end - tl->start + culen;
diff = nlen - TTYLOGSIZE;

if(diff >= tl->end-tl->start) {
/* a complete replacement
 * should never be greater; diff = tl->end - tl->start
 * copy the new stuff */
custart = tl->start;
memcpy(custart, iobuf+i, TTYLOGSIZE*4);
tl->end = tl->start + TTYLOGSIZE;
tl->end[0] = 0;
tl->cursor = 0;
memset(tl->marks, 0, sizeof(tl->marks));
if(!screenmode) imark_start = 0;
} else {
if(diff > 0) {
// partial replacement
memmove(tl->start, tl->start+diff, (tl->end-tl->start - diff)*4);
tl->end -= diff;
if(tl->cursor) {
tl->cursor -= diff;
if(tl->cursor < tl->start) tl->cursor = 0;
}
if(imark_start && !screenmode) {
imark_start -= diff;
if(imark_start < tl->start) imark_start = 0;
}
for(j=0; j<NUMBUFMARKS; ++j)
if(tl->marks[j]) {
tl->marks[j] -= diff;
if(tl->marks[j] < tl->start) tl->marks[j] = 0;
}
}
/* copy the new stuff */
custart = tl->end;
memcpy(custart, iobuf+i, culen*4);
tl->end += culen;
tl->end[0] = 0;
}

postprocess(custart);

if(acs_debug) {
/* Log the new characters, but they're in unicode, so convert back to ascii. */
/* Not yet implemented. */
;
}

/* If you're in screen mode, I haven't moved your reading cursor,
 * or imark _start, or the pointers in marks[], appropriately.
 * See the todo file for tracking the cursor in screen mode. */

i += culen*4;
break;

default:
/* Perhaps a phase error.
 * Not sure what to do here.
 * Just give up. */
if(acs_debug) acs_log("unknown command %d\n", iobuf[i]);
i += 4;
} // switch
} // looping through events

return 0;
} // acs_events

int acs_refresh(void)
{
iobuf[0] = ACSINT_REFRESH;
if(acs_write(1)) return -1;
if(screenmode) screenSnap();
return acs_events();
} // acs_refresh


// cursor commands.
static unsigned int *tc; // temp cursor

void acs_cursorset(void)
{
tc = rb->cursor;
} // acs_cursorset

void acs_cursorsync(void)
{
rb->cursor = tc;
} // acs_cursorsync

/* This routine lowers a unicode down to an ascii symbol that is essentially equivalent.
 * If this unicode is not in our table then we return the same unicode,
 * unless the unicode is greater than 256, whence we return dot.
 * This effectively retains the iso8859-1 chars;
 * we should really have code here that converts to other pages
 * based on your locale. */
unsigned int acs_downshift(unsigned int u)
{
static const unsigned int in_c[] = {
0x95, 0x99, 0x9c, 0x9d, 0x91, 0x92, 0x93, 0x94,
0xa0, 0xad, 0x96, 0x97, 0x85,
0x2022, 0x25ba, 0x113, 0x2013, 0x2014,
0x2018, 0x2019, 0x201c, 0x201d,
0};
static char out_c[] =
"*'`'`'`' ----**`--`'";
int i;

for(i=0; in_c[i]; ++i)
if(u == in_c[i]) return out_c[i];

if(u >= 256) return '?';
return u;
}

/* Return the character pointed to by the temp cursor.
 * This is the iso version, using downshift().
 * The unicode version follows. */
int acs_getc(void)
{
if(!tc) return 0;
return acs_downshift(*tc);
} // acs_getc

unsigned int acs_getc_uc(void)
{
return (tc ? *tc : 0);
} // acs_getc_uc

int acs_forward(void)
{
if(rb->end == rb->start) return 0;
if(!tc) return 0;
if(++tc == rb->end) return 0;
return 1;
} // acs_forward

int acs_back(void)
{
if(rb->end == rb->start) return 0;
if(!tc) return 0;
if(tc-- == rb->start) return 0;
return 1;
} // acs_back

int acs_startline(void)
{
int colno = 0;
if(rb->end == rb->start) return 0;
if(!tc) return 0;
do ++colno;
while(acs_back() && acs_getc() != '\n');
acs_forward();
return colno;
} // acs_startline

int acs_endline(void)
{
if(rb->end == rb->start) return 0;
if(!tc) return 0;
while(acs_getc() != '\n') {
if(acs_forward()) continue;
acs_back();
break;
}
return 1;
} // acs_endline

// put the cursor back to a known location.
// Internal use only.
static void putback(int n)
{
	if(!n) return;
	if(n > 0) {
		while(n--) acs_forward();
	} else {
		while(n++) acs_back();
	}
} // putback

// start of word (actually token/symbol)
int acs_startword(void)
{
	int forward, backward;
	char apos, apos1;
	unsigned int c = acs_getc();
/* calling getc() means we are in the range for the ctype functions */

if(!c) return 0;

	if(!isalnum(c)) {
		if(c == '\n' || c == ' ' || c == '\7') return 1;
		// a punctuation mark, an atomic token.
		// But wait, if there are more than four in a row,
		// we have a linear token.  Pull back to the start.
		for(forward=0; acs_forward(); ++forward)
			if(c != acs_getc()) break;
		putback(-(forward+1));
		for(backward=0; acs_back(); ++backward)
			if(c != acs_getc()) break;
		acs_forward();
		if(forward+backward < 4) putback(backward);
		return 1;
} // punctuation

	apos = apos1 = 0;
	do {
		c = acs_getc();
		if(c == '\'') {
			if(apos1) break;
			apos = apos1 = 1;
			continue;
		}
		if(!isalnum(c)) break;
		apos = 0;
	} while(acs_back());
	acs_forward();
	if(apos) acs_forward();

return 1;
} // acs_startword

// end of word
int acs_endword(void)
{
	int forward, backward;
	char apos, apos1;
	unsigned int c = acs_getc();
/* calling getc() means we are in the range for the ctype functions */

if(!c) return 0;

	if(!isalnum(c)) {
		if(c == '\n' || c == ' ' || c == '\7') return 1;
		for(backward=0; acs_back(); ++backward)
			if(c != acs_getc()) break;
		putback(backward+1);
		for(forward=0; acs_forward(); ++forward)
			if(c != acs_getc()) break;
		acs_back();
		if(forward+backward < 4) putback(-forward);
		return 1;
} // punctuation

	apos = apos1 = 0;
	do {
		c = acs_getc();
		if(c == '\'') {
			if(apos1) break;
			 apos = apos1 = 1;
			continue;
		}
		if(!isalnum(c)) break;
		apos = 0;
	} while(acs_forward());
	acs_back();
	if(apos) acs_back();

return 1;
} // acs_endword

void acs_startbuf(void)
{
tc = rb->start;
} // acs_startbuf

void acs_endbuf(void)
{
tc = rb->end;
if(tc != rb->start) --tc;
} // acs_endbuf

// skip past left spaces
void acs_lspc(void)
{
if(rb->end == rb->start) return;
if(!tc) return;
	if(!acs_back()) goto done;
	while(acs_getc() == ' ')
		if(!acs_back()) break;
done:
	acs_forward();
} // acs_lspc

// skip past right spaces
void acs_rspc(void)
{
if(rb->end == rb->start) return;
if(!tc) return;
	if(!acs_forward()) goto done;
	while(acs_getc() == ' ')
		if(!acs_forward()) break;
done:
	acs_back();
} // acs_rspc

int acs_nextline(void)
{
if(!acs_endline()) return 0;
return acs_forward();
} // acs_nextline

int acs_prevline(void)
{
if(!acs_startline()) return 0;
if(!acs_back()) return 0;
return acs_startline();
} // acs_prevline

int acs_nextword(void)
{
if(!acs_endword()) return 0;
acs_rspc();
return acs_forward();
} // acs_nextword

int acs_prevword(void)
{
if(!acs_startword()) return 0;
acs_lspc();
if(!acs_back()) return 0;
return acs_startword();
} // acs_prevword

/* Case insensitive match.  Assumes the first letters already match.
 * This is an iso8859 match, not a unicode match. */
static int stringmatch(const char *s)
{
	unsigned char x, y;
	short count = 0;

	if(!(y = (unsigned char)*++s)) return 1;
y = tolower(y);

	while(++count, acs_forward()) {
		x = acs_getc();
x = tolower(x);
if(x != y) break;
		if(!(y = (unsigned char)*++s)) return 1;
y = tolower(y);
	} /* while matches */

	// put the cursor back
	putback(-count);
	return 0;
} // stringmatch

int acs_bufsearch(const char *string, int back, int newline)
{
	int ok;
	unsigned char c, first;

if(rb->end == rb->start) return 0;
if(!tc) return 0;

	if(newline) {
		if(back) acs_startline(); else acs_endline();
	}

	ok = back ? acs_back() : acs_forward();
	if(!ok) return 0;

first = (unsigned char) *string;
first = tolower(first);

	do {
		c = acs_getc();
c = tolower(c);
if(c == first && stringmatch(string)) return 1;
		ok = back ? acs_back() : acs_forward();
	} while(ok);

	return 0;
} // acs_bufsearch

// inject chars into the stream
int acs_injectstring(const char *s)
{
int len = strlen(s);
if(len > TTYLOGSIZE) {
errno = ENOMEM;
setError();
return -1;
}

iobuf[0] = ACSINT_PUSH_TTY;
iobuf[1] = len;
iobuf[2] = len>>8;
strcpy((char*)iobuf+3, s);
return acs_write(len+3);
} // acs_injectstring

int acs_getsentence(char *dest, int destlen, ofs_type *offsets, int prop)
{
const char *destend = dest + destlen - 1; /* end of destination array */
char *t = dest;
const unsigned int *s = rb->cursor;
ofs_type *o = offsets;
int j, l;
unsigned int c;
unsigned char c1; /* cut c down to 1 byte */
char spaces = 1, alnum = 0;

if(!s || !t) {
errno = EFAULT;
setError();
return -1;
}

if(destlen <= 0) {
errno = ENOMEM;
setError();
return -1;
}

if(destlen == 1) {
*dest = 0;
if(offsets) *offsets = 0;
return 0;
}

// zero offsets by default
if(o) memset(o, 0, sizeof(ofs_type)*destlen);

while((c = *s) && t < destend) {
if(c == '\n' && prop&ACS_GS_NLSPACE)
c = ' ';

c1 = acs_downshift(c);

if(c == ' ') {
alnum = 0;
if(prop&ACS_GS_ONEWORD) {
if(t == dest) *t++ = c, ++s;
break;
}
if(!spaces) *t++ = c;
spaces = 1;
++s;
continue;
}

if(c == '\n' || c == '\7') {
alnum = 0;
if(t > dest && t[-1] == ' ') --t;
if(prop&ACS_GS_ONEWORD) {
if(t == dest) *t++ = c, ++s;
break;
}
if(o) o[t-dest] = s-rb->cursor;
*t++ = c;
++s;
if(prop&ACS_GS_STOPLINE) break;
spaces = 1;
continue;
}

spaces = 0;

if(isalnum(c1)) {
if(!alnum) {
// new word
if(o) o[t-dest] = s-rb->cursor;
}
// building our word
*t++ = c1;
++s;
alnum = 1;
continue;
}

/* some unicodes like 0x92 downshift to apostrophe */
if(c1 == '\'' && alnum && isalpha(acs_downshift(s[1]))) {
const char *u;
const unsigned int *v;
unsigned int v0;
/* this is treated as a letter, as in wouldn't,
 * unless there is another apostrophe before or after,
 * or digits are involved. */
for(u=t-1; u>=dest && isalpha((unsigned char)*u); --u)  ;
if(u >= dest) {
if(*u == '\'') goto punc;
if(isdigit((unsigned char)*u)) goto punc;
}
for(v=s+1; isalpha(v0 = acs_downshift(*v)); ++v)  ;
if(v0 == '\'') goto punc;
if(isdigit(v0)) goto punc;
// keep alnum alive
*t++ = c1;
++s;
continue;
}

// punctuation
punc:
alnum = 0;
if(t > dest && prop&ACS_GS_ONEWORD) break;
if(o) o[t-dest] = s-rb->cursor;

// check for repeat
if(prop&ACS_GS_REPEAT &&
c == s[1] &&
c == s[2] &&
c == s[3] &&
c == s[4]) {
char reptoken[60];
const char *pname = acs_getpunc(c); /* punctuation name */
if(pname) {
strncpy(reptoken, pname, 30);
reptoken[30] = 0;
} else {
reptoken[0] = c1;
reptoken[1] = 0;
}
strcat(reptoken, " length ");
for(j=5; c == s[j]; ++j)  ;
sprintf(reptoken+strlen(reptoken), "%d", j);
l = strlen(reptoken);
if(t+l+2 > destend) break; // no room
if(t > dest && t[-1] != ' ')
*t++ = ' ';
strcpy(t, reptoken);
t += l;
*t++ = ' ';
spaces = 1;
s += j;
if(prop & ACS_GS_ONEWORD) break;
continue;
}

/* just a punctuation mark on its own.
/* If it's a high unicode, see if it has a downshift,
 * or if it is pronounceable. */
if(c >= 256 && c1 == '?') {
const char *u = acs_getpunc(c);
if(u) {
l = strlen(u);
if(t+l+2 > destend) break; // no room
if(t > dest && t[-1] != ' ')
*t++ = ' ';
strcpy(t, u);
t += l;
*t++ = ' ';
spaces = 1;
++s;
if(prop & ACS_GS_ONEWORD) break;
continue;
}
}

*t++ = c1;
++s;
if(prop & ACS_GS_ONEWORD) break;
} // loop over characters in the tty buffer

*t = 0;
if(o) o[t-dest] = s-rb->cursor;

return 0;
} /* acs_getsentence */

/* If you want to manage the unicode chars yourself. */
int acs_getsentence_uc(unsigned int *dest, int destlen, ofs_type *offsets, int prop)
{
const unsigned int *destend = dest + destlen - 1; /* end of destination array */
unsigned int *t = dest;
const unsigned int *s = rb->cursor;
ofs_type *o = offsets;
int j, l;
unsigned int c;
unsigned char c1; /* cut c down to 1 byte */
char spaces = 1, alnum = 0;

if(!s || !t) {
errno = EFAULT;
setError();
return -1;
}

if(destlen <= 0) {
errno = ENOMEM;
setError();
return -1;
}

if(destlen == 1) {
*dest = 0;
if(offsets) *offsets = 0;
return 0;
}

// zero offsets by default
if(o) memset(o, 0, sizeof(ofs_type)*destlen);

while((c = *s) && t < destend) {
if(c == '\n' && prop&ACS_GS_NLSPACE)
c = ' ';

c1 = acs_downshift(c);

if(c == ' ') {
alnum = 0;
if(prop&ACS_GS_ONEWORD) {
if(t == dest) *t++ = c, ++s;
break;
}
if(!spaces) *t++ = c;
spaces = 1;
++s;
continue;
}

if(c == '\n' || c == '\7') {
alnum = 0;
if(t > dest && t[-1] == ' ') --t;
if(prop&ACS_GS_ONEWORD) {
if(t == dest) *t++ = c, ++s;
break;
}
if(o) o[t-dest] = s-rb->cursor;
*t++ = c;
++s;
if(prop&ACS_GS_STOPLINE) break;
spaces = 1;
continue;
}

spaces = 0;

if(isalnum(c1)) {
if(!alnum) {
// new word
if(o) o[t-dest] = s-rb->cursor;
}
// building our word
*t++ = c1;
++s;
alnum = 1;
continue;
}

/* some unicodes like 0x92 downshift to apostrophe */
if(c1 == '\'' && alnum && isalpha(acs_downshift(s[1]))) {
const unsigned int *v;
unsigned int v0;
/* this is treated as a letter, as in wouldn't,
 * unless there is another apostrophe before or after,
 * or digits are involved. */
for(v=t-1; v>=dest && isalpha(v0 = acs_downshift(*v)); --v)  ;
if(v >= dest) {
if(v0 == '\'') goto punc;
if(isdigit(v0)) goto punc;
}
for(v=s+1; isalpha(v0 = acs_downshift(*v)); ++v)  ;
if(v0 == '\'') goto punc;
if(isdigit(v0)) goto punc;
// keep alnum alive
*t++ = c1;
++s;
continue;
}

// punctuation
punc:
alnum = 0;
if(t > dest && prop&ACS_GS_ONEWORD) break;
if(o) o[t-dest] = s-rb->cursor;

// check for repeat
if(prop&ACS_GS_REPEAT &&
c == s[1] &&
c == s[2] &&
c == s[3] &&
c == s[4]) {
char reptoken[60];
const char *pname = acs_getpunc(c); /* punctuation name */
if(pname) {
strncpy(reptoken, pname, 30);
reptoken[30] = 0;
} else {
reptoken[0] = c1;
reptoken[1] = 0;
}
strcat(reptoken, " length ");
for(j=5; c == s[j]; ++j)  ;
sprintf(reptoken+strlen(reptoken), "%d", j);
l = strlen(reptoken);
if(t+l+2 > destend) break; // no room
if(t > dest && t[-1] != ' ')
*t++ = ' ';
for(l=0; reptoken[l]; ++l)
*t++ = reptoken[l];
*t++ = ' ';
spaces = 1;
s += j;
if(prop & ACS_GS_ONEWORD) break;
continue;
}

// just a punctuation mark on its own
*t++ = c;
++s;
if(prop & ACS_GS_ONEWORD) break;
} // loop over characters in the tty buffer

*t = 0;
if(o) o[t-dest] = s-rb->cursor;

return 0;
} /* acs_getsentence_uc */

