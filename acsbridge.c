/*********************************************************************
File: acsbridge.c
Description: Encapsulates communication between userspace and kernel via
the /dev/acsint device.  This file implements the interface described
and declared in acsbridge.h.
*********************************************************************/

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <malloc.h>
#include <termios.h>
#include <sys/select.h>
#include <ctype.h>
#include <unistd.h>
#include <linux/vt.h>

#include "acsbridge.h"

#define stringEqual !strcmp

#define MAX_ERRMSG_LEN 256 // description of error
#define MAXNOTES 10 // how many notes to play in one call
#define IOBUFSIZE (TTYLOGSIZE + 2000) /* size of input buffer */
#define ATTRIBOFFSET 20000 // about half of TTYLOGSIZE
#define SSBUFSIZE 64 // synthesizer buffer for events

int acs_fd = -1; // file descriptor for /dev/acsint
static int vcs_fd; // file descriptor for /dev/vcsa
static unsigned char vcs_header[4];
#define nrows (int)vcs_header[0]
#define ncols (int)vcs_header[1]
#define csr (int)vcs_header[3] // cursor row
#define csc (int)vcs_header[2] // cursor column
int acs_fgc = 1; // current foreground console
int acs_postprocess = 0xf; // postprocess the text from the tty
static const achar crbyte = '\r';
static const achar kbyte = '\13';

int ss_style;
int ss_curvolume, ss_curpitch, ss_curspeed, ss_curvoice;

// The start of the sentence that is sent with index markers.
static achar *imark_start;
// location of each index marker relative to imark_start
static unsigned short imark_loc[100];
static int imark_first, imark_end;
static bnsf; // counting control f's from bns

// move the cursor to the index marker
static void indexSet(int n)
{
if(!imark_start) return;

if(ss_style == SS_STYLE_BNS || ss_style == SS_STYLE_ACE) {
++bnsf;
n = imark_end + bnsf;
} else {
n -= imark_first;
}
if(n < 0 || n >= imark_end) return;
rb->cursor = imark_start + imark_loc[n];
if(n == imark_end - 1) {
/* last index marker, sentence is finished */
imark_start = 0;
imark_end = 0;
}
} // indexSet

int acs_debug = 0;
// for debugging: save a message, without sending it to tty,
// which would just generate more events.
static const char debuglog[] = "acslog";
int acs_log(const char *msg, int n)
{
FILE *f = fopen(debuglog, "a");
fprintf(f, msg, n);
fclose(f);
} // acs_log

key_handler_t acs_key_h;
more_handler_t acs_more_h;
fgc_handler_t acs_fgc_h;
ks_echo_handler_t acs_ks_echo_h;

static int cerror; /* Indicate communications error. */
static char errorDesc[MAX_ERRMSG_LEN];

static achar iobuf[IOBUFSIZE]; // input output buffer for acsint
static achar ss_inbuf[SSBUFSIZE]; // input buffer for the synthesizer

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
static struct readingBuffer tty_log[MAX_NR_CONSOLES];
static struct readingBuffer *tl; // current tty log
static struct readingBuffer screenBuf;
static int screenmode; // 1 = screen, 0 = tty log
struct readingBuffer *rb = tty_log; // current reading buffer for the application

static void screenSnap(void)
{
achar *s, *t;
achar *a;
int i, j;

lseek(vcs_fd, 0, 0);
read(vcs_fd, vcs_header, 4);
read(vcs_fd, screenBuf.area+4, 2*nrows*ncols);

screenBuf.attribs = a = screenBuf.area + ATTRIBOFFSET;
t = screenBuf.area + 1;
s = t + 3;
screenBuf.start = t;
screenBuf.area[0] = 0;
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
screenBuf.v_cursor = screenBuf.area + 1 + csr * (ncols+1) + csc;
} // screenSnap

void
acs_screenmode(int enabled)
{
// If you issued this command at the call of a keystroke,
// and that is what I am expecting / assuming,
// then yes the buffer will be caught up.
// If it is called by some other automated process, or at startup,
// then you will want to call acs_refresh to bring the buffer up to date.
if(enabled) {
screenmode = 1;
rb = &screenBuf;
} else {
screenmode = 0;
rb = tty_log + acs_fgc - 1;
}
} // acs_linear


// Open and close the device.

int
acs_open(const char *devname)
{
int j;

if(acs_fd >= 0) {
// already open
errno = EEXIST;
setError();
return -1;
}

if(acs_debug) unlink(debuglog);

tl = tty_log;
for(j=0; j<MAX_NR_CONSOLES; ++j, ++tl) {
tl->start = tl->end = tl->area + 1;
tl->area[0] = 0;
tl->end[0] = 0;
tl->attribs = 0;
tl->cursor = tl->end;
tl->v_cursor = 0;
}

vcs_fd = open("/dev/vcsa", O_RDONLY);
if(vcs_fd < 0) {
setError();
return -1;
}

acs_fd = open(devname, O_RDWR);
if(acs_fd < 0) {
close(vcs_fd);
setError();
} else {
clearError();
}

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
iobuf[0] = ACSINT_ECHO;
iobuf[1] = enabled;
return acs_write(2);
} // acs_monitor

int
acs_bypass(void)
{
iobuf[0] = ACSINT_BYPASS;
return acs_write(1);
} // acs_bypass

// Use divert to swallow a string.
static achar *swallow_string;
static int swallow_max, swallow_len;
static int swallow_prop, swallow_rc;
static key_handler_t save_key_h;
static void swallow_key_h(int key, int ss, int leds);
static void swallow1_h(int key, int ss, int leds);

int acs_keystring(achar *buf, int buflen, int properties)
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

static const achar lowercode[] =
" \0331234567890-=\b qwertyuiop[]\r asdfghjkl;'` \\zxcvbnm,./    ";
static const achar uppercode[] =
" \033!@#$%^&*()_+\b QWERTYUIOP{}\r ASDFGHJKL:\"~ |ZXCVBNM<>?    ";

// special handler for keystring()
static void swallow_key_h(int key, int ss, int leds)
{
achar keychar;

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
if(leds & ACS_LEDS_CAPSLOCK && isalpha(keychar))
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

int acs_get1char(achar *p)
{
int key, state;
achar keychar;
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
postprocess(achar *s)
{
achar *t;

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
if(isalpha(s[j])) break;
if(j < 20 && s[j]) {
			// a letter indicates end of escape sequence.
			// If the letter is H, we are repositioning the cursor.
			// Most of the time we are starting a new line.
			// If not, we are at least starting a new word or phrase.
			// In either case I find it helpful to introduce a newline.
			// That deliniates a new block of text,
			// and most of the time said text is indeed on a new line.
			if(*s == 'H') *t++ = '\n';
s += j+1;
continue;
		}
}

// control chars
if(*s < ' ' && *s != '\7' && *s != '\r' && *s != '\n' &&
acs_postprocess&ACS_PP_STRIP_CTRL) {
++s;
continue;
}

*t++ = *s++;
}

tl->end = t;
*t = 0;

if(tl->cursor >= t)
tl->cursor = (t > tl->start ? t-1 : t);
} // postprocess

void acs_clearbuf(void)
{
if(!screenmode)
rb->end = rb->cursor = rb->start;
imark_start = 0;
} // acs_clearbuf

// events coming back

int acs_events(void)
{
int nr; // number of bytes read
int i;
int culen; /* catch up length */
int culen1; /* round up to 4 byte boundary */
achar *custart; // where does catch up start
int nlen; // length of new area
int diff;
int minor;
char refreshed = 0;

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
imark_start = 0;
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
if(m) { acs_injectstring(m); i += 4; break; }
}
if(acs_key_h) acs_key_h(iobuf[i+1], iobuf[i + 2], iobuf[i+3]);
i += 4;
break;

case ACSINT_FGC:
imark_start = 0;
if(acs_debug) acs_log("fg %d\n", iobuf[i+1]);
acs_fgc = iobuf[i+1];
if(screenmode) {
// I sure hope linux has actually done the console switch by this time.
if(!refreshed) { screenSnap(); refreshed = 1; }
} else {
rb = tty_log + acs_fgc - 1;
}
if(acs_fgc_h) acs_fgc_h();
i += 4;
break;

case ACSINT_TTY_MORECHARS:
if(acs_debug) acs_log("more stuff\n", 0);
// no automatic refresh here; you have to call it if you want it
if(acs_more_h) acs_more_h();
i += 4;
break;

case ACSINT_REFRESH:
if(acs_debug) acs_log("refresh\n", 0);
i += 4;
break;

case ACSINT_TTY_NEWCHARS:
// this is the refresh data in line mode
// minor is always the foreground console; we could probably discard it
minor = iobuf[i+1];
culen = iobuf[i+2] | ((unsigned short)iobuf[i+3]<<8);
culen1 = (culen+3) & ~3;
if(acs_debug) acs_log("new %d\n", culen);
i += 4;
if(!culen) break;
if(nr-i < culen1) break;
tl = tty_log + minor - 1;
nlen = tl->end - tl->start + culen;
diff = nlen - TTYLOGSIZE;
if(diff >= tl->end-tl->start) {
// complete replacement
// should never be greater; diff = tl->end - tl->start
custart = tl->start;
memcpy(custart, iobuf+i, TTYLOGSIZE);
tl->end = tl->start + TTYLOGSIZE;
tl->end[0] = 0;
tl->cursor = 0;
if(!screenmode) imark_start = 0;
} else {
if(diff > 0) {
// partial replacement
memmove(tl->start, tl->start+diff, tl->end-tl->start - diff);
tl->end -= diff;
tl->cursor -= diff;
if(tl->cursor < tl->start) tl->cursor = 0;
if(imark_start && !screenmode) {
imark_start -= diff;
if(imark_start < tl->start) imark_start = 0;
}
}
custart = tl->end;
memcpy(custart, iobuf+i, culen);
tl->end += culen;
tl->end[0] = 0;
}
if(tl->cursor == 0) tl->cursor = tl->end - 1;
i += culen1;
postprocess(custart);
if(acs_debug) acs_log("<<\n%s>>\n", (int)custart);
// But if you're in screen mode, I haven't moved your reading cursor,
// or imark _start, appropriately.
// Don't know what to do about that.
break;

default:
// Perhaps a phase error.
// Not sure what to do here.
// Just give up.
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
screenSnap();
return acs_events();
} // acs_refresh


// cursor commands.
static achar *tc; // temp cursor

void acs_cursorset(void)
{
tc = rb->cursor;
} // acs_cursorset

void acs_cursorsync(void)
{
rb->cursor = tc;
} // acs_cursorsync

achar acs_getc(void)
{
return *tc;
} // acs_getc

int acs_forward(void)
{
if(rb->end == rb->start) return 0;
if(++tc == rb->end) return 0;
return 1;
} // acs_forward

int acs_back(void)
{
if(rb->end == rb->start) return 0;
if(tc-- == rb->start) return 0;
return 1;
} // acs_back

int acs_startline(void)
{
int colno = 0;
if(rb->end == rb->start) return 0;
do ++colno;
while(acs_back() && acs_getc() != '\n');
acs_forward();
return colno;
} // acs_startline

int acs_endline(void)
{
if(rb->end == rb->start) return 0;
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
	achar c = acs_getc();

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
	achar c = acs_getc();

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

// Case insensitive match.  Assumes the first letters already match.
static int stringmatch(const achar *s)
{
	achar x, y;
	short count = 0;

	if(!(y = *++s)) return 1;
y = tolower(y);

	while(++count, acs_forward()) {
		x = acs_getc();
x = tolower(x);
if(x != y) break;
		if(!(y = *++s)) return 1;
y = tolower(y);
	} /* while matches */

	// put the cursor back
	putback(-count);
	return 0;
} // stringmatch

int acs_bufsearch(const achar *string, int back, int newline)
{
	int ok;
	achar c, first;

if(rb->end == rb->start) return 0;

	if(newline) {
		if(back) acs_startline(); else acs_endline();
	}

	ok = back ? acs_back() : acs_forward();
	if(!ok) return 0;

first = *string;
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

#define MK_BLOCK 60
#define MK_OFFSET 55

int acs_build_mkcode(int key, int ss)
{
if(key >= ACS_NUM_KEYS) return -1;
ss &= 0xf;
if(key >= MK_OFFSET) {
// function or numpad or arrows etc
key -= MK_OFFSET;
key += 2*MK_BLOCK;
if(ss == ACS_SS_SHIFT) key += MK_BLOCK;
if(ss == ACS_SS_CTRL) key += MK_BLOCK*2;
if(ss == ACS_SS_ALT) key += MK_BLOCK*3;
if(ss == ACS_SS_LALT) key += MK_BLOCK*4;
if(ss == ACS_SS_RALT) key += MK_BLOCK*5;
return key;
}
if(key > KEY_M) return -1;
key = lowercode[key];
if(!isalpha(key)) return -1;
key -= 'a';
if(ss & ACS_SS_SHIFT) return -1;
if(!ss) return -1;
if(ss == ACS_SS_CTRL) return key;
if(ss & ACS_SS_CTRL) return -1;
if(ss == ACS_SS_ALT) return key + MK_BLOCK/2;
if(ss == ACS_SS_LALT) return key + MK_BLOCK;
if(ss == ACS_SS_RALT) return key + MK_BLOCK + MK_BLOCK/2;
return -1; // should never get here
} // acs_build_mkcode

// match on letter case insensitive
static int lettermatch_ci(const char *s, const char *t, int n)
{
char c;
while(n) {
c = s[0] ^ t[0];
c &= 0xdf;
if(c) return 0;
--n, ++s, ++t;
}
c = s[0] | 0x20;
if(c >= 'a' && c <= 'z') return 0;
return 1;
} // lettermatch_ci

int acs_ascii2mkcode(const char *s, char **endptr)
{
int ss = 0;
int key;
achar c;
	static const unsigned char numpad[] = {
KEY_KPASTERISK, KEY_KPPLUS, 0, KEY_KPMINUS, KEY_KPDOT, KEY_KPSLASH,
KEY_KP0, KEY_KP1, KEY_KP2, KEY_KP3, KEY_KP4,
KEY_KP5, KEY_KP6, KEY_KP7, KEY_KP8, KEY_KP9};
static const unsigned char lettercode[] = {
KEY_A, KEY_B, KEY_C, KEY_D, KEY_E,
KEY_F, KEY_G, KEY_H, KEY_I, KEY_J,
KEY_K, KEY_L, KEY_M, KEY_N, KEY_O,
KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T,
KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y,
KEY_Z};

if(s[0] == '+') ss = ACS_SS_SHIFT, ++s;
else if(s[0] == '^') ss = ACS_SS_CTRL, ++s;
else if(s[0] == '@') ss = ACS_SS_ALT, ++s;
else if((s[0] == 'l' || s[0] == 'L') && s[1] == '@') ss = ACS_SS_LALT, s += 2;
else if((s[0] == 'r' || s[0] == 'R') && s[1] == '@') ss = ACS_SS_RALT, s += 2;

if((s[0] == 'f' || s[0] == 'F') && isdigit((achar)s[1])) {
++s;
key = 0;
c = (achar)s[0];
while(isdigit(c)) {
key = 10*key + c - '0';
++s;
c = (achar)s[0];
}
if(key <= 0 || key > 12) goto error;
// the real keycode
if(key <= 10) key += KEY_F1-1;
else key += KEY_F11 - 11;
goto done;
}

if(s[0] == '#' && s[1] && strchr("*+.-/0123456789", s[1])) {
c = *++s;
++s;
		key = numpad[c-'*'];
goto done;
}

if(lettermatch_ci(s, "up", 2)) {
key = KEY_UP;
s += 2;
goto done;
}

if(lettermatch_ci(s, "down", 4)) {
key = KEY_DOWN;
s += 4;
goto done;
}

if(lettermatch_ci(s, "left", 4)) {
key = KEY_LEFT;
s += 4;
goto done;
}

if(lettermatch_ci(s, "right", 5)) {
key = KEY_RIGHT;
s += 5;
goto done;
}

if(lettermatch_ci(s, "home", 4)) {
key = KEY_HOME;
s += 4;
goto done;
}

c = s[0] | 0x20;
if(c < 'a' || c > 'z') goto error;
key = lettercode[c-'a'];
++s;
c = s[0] | 0x20;
if(c >= 'a' && c <= 'z') goto error;

done:
if(endptr) *endptr = (char *)s;
// save these for line_configure
key1key = key;
if(!ss) ss = ACS_SS_PLAIN;
key1ss = ss;
return acs_build_mkcode(key, ss);

error:
return -1;
} // acs_ascii2mkcode

static char *macrolist[MK_BLOCK*8];

void acs_clearmacro(int mkcode)
{
if(macrolist[mkcode]) free(macrolist[mkcode]);
macrolist[mkcode] = 0;
} // acs_clearmacro

char *acs_getmacro(int mkcode)
{
return macrolist[mkcode];
} // acs_getmacro

void acs_setmacro(int mkcode, const char *s)
{
acs_clearmacro(mkcode);
acs_clearspeechcommand(mkcode);
if(!s) return;
macrolist[mkcode] = malloc(strlen(s) + 1);
strcpy(macrolist[mkcode], s);
} // acs_setmacro

static char *speechcommandlist[MK_BLOCK*8];

void acs_clearspeechcommand(int mkcode)
{
if(speechcommandlist[mkcode]) free(speechcommandlist[mkcode]);
speechcommandlist[mkcode] = 0;
} // acs_clearspeechcommand

char *acs_getspeechcommand(int mkcode)
{
return speechcommandlist[mkcode];
} // acs_getspeechcommand

void acs_setspeechcommand(int mkcode, const char *s)
{
acs_clearmacro(mkcode);
acs_clearspeechcommand(mkcode);
if(!s) return;
speechcommandlist[mkcode] = malloc(strlen(s) + 1);
strcpy(speechcommandlist[mkcode], s);
} // acs_setspeechcommand

static achar *punclist[256];
static const achar *firstpunclist[256] = {
"null", 0, 0, 0, 0, 0, 0, "bell",
"backspace", "tab", "newline", 0, "formfeed", "return", 0, 0,
0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, "escape", 0, 0, 0, 0,
"space", "bang", "quote", "pound", "doller", "percent", "and", "apostrophe",
"left paren", "right paren", "star", "plus", "comma", "dash", "period", "slash",
0, 0, 0, 0, 0, 0, 0, 0,
0, 0, "colen", "semmycolen", "less than", "eequals", "greater than", "question mark",
"at sign", 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, "left bracket", "backslash", "right bracket", "up airow", "underscore",
"backquote", 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0,
"left brace", "pipe", "right brace", "tilde", "delete",
0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0,
"break space", "bang up", "cents", "pounds", "currency", "yen", "broken bar", "section",
"diaeresis", "copyright", "feminine", "backward", "not", "soft hyphen", "registered", "macron",
"degrees", "plus minus", "super two", "super three", "acute", "micro", "pilcrow", "bullet",
"cedilla", "super one", "masculine", "forward", "one fourth", "one half", "three fourths", "question up",
"cap a grave", "cap a acute", "cap a circumflex", "cap a tilde", "cap a diaeresis", "cap a ring above", "cap ligature a e", "cap c cedilla",
"cap e grave", "cap e acute", "cap e circumflex", "cap e diaeresis", "cap i grave", "cap i acute", "cap i circumflex", "cap i diaeresis",
"cap e t h", "cap n tilde", "cap o grave", "cap o acute", "cap o circumflex", "cap o tilde", "cap o diaeresis", "times",
"cap o stroke", "cap u grave", "cap u acute", "cap u circumflex", "cap u diaeresis", "cap y acute", "cap thorn", "sharp s",
"a grave", "a acute", "a circumflex", "a tilde", "a diaeresis", "a ring above", "ligature a e", "c cedilla",
"e grave", "e acute", "e circumflex", "e diaeresis", "i grave", "i acute", "i circumflex", "i diaeresis",
"e t h", "n tilde", "o grave", "o acute", "o circumflex", "o tilde", "o diaeresis", "divided by",
"o stroke", "u grave", "u acute", "u circumflex", "u diaeresis", "y acute", "thorn", "y diaeresis"
};

void acs_clearpunc(achar c)
{
if(punclist[c]) free(punclist[c]);
punclist[c] = 0;
} // acs_clearpunc

achar *acs_getpunc(achar c)
{
return punclist[c];
} // acs_getpunc

void acs_setpunc(achar c, const achar *s)
{
acs_clearpunc(c);
if(!s) return;
punclist[c] = malloc(strlen(s) + 1);
strcpy(punclist[c], s);
} // acs_setpunc

int acs_getsentence(achar *dest, int destlen, unsigned short *offsets, int prop)
{
const achar *destend = dest + destlen - 1; // end of destination array
const achar *s = rb->cursor;
achar *t = dest;
unsigned short *o = offsets;
int j, l;
achar c;
char spaces = 1, alnum = 0;

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
if(o) memset(o, 0, sizeof(unsigned short)*destlen);

while((c = *s) && t < destend) {
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
*t++ = c;
++s;
if(prop&ACS_GS_ONEWORD) break;
if(prop&ACS_GS_STOPLINE) break;
spaces = 1;
continue;
}

spaces = 0;

if(isalnum(c)) {
if(!alnum) {
// new word
if(o) o[t-dest] = s-rb->cursor;
}
// building our word
*t++ = c;
++s;
alnum = 1;
continue;
}

if(c == '\'' && alnum && isalpha(s[1])) {
const achar *u;
// this is treated as a letter, as in wouldn't,
// unless there is another apostrophe before or after, or digits are involved.
for(u=t-1; u>=dest && isalpha(*u); --u)  ;
if(u >= dest) {
if(*u == '\'') goto punc;
if(isdigit(*u)) goto punc;
}
for(u=s+1; isalpha(*u); ++u)  ;
if(*u == '\'') goto punc;
if(isdigit(*u)) goto punc;
// keep alnum alive
*t++ = c;
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
c == s[1] && c == s[2] && c == s[3] && c == s[4]) {
char reptoken[12]; // repeat token
reptoken[0] = SP_MARK;
reptoken[1] = SP_REPEAT;
for(j=5; c == s[j]; ++j)  ;
sprintf(reptoken+2, "%d", j);
l = strlen(reptoken);
reptoken[l++] = c;
reptoken[l] = 0;
if(t+l >= destend) break; // no room
strcpy(t, reptoken);
t += l;
s += j;
continue;
}

// just a punctuation mark on its own
if(c == SP_MARK && prop&ACS_GS_REPEAT) {
if(t+1 == destend) break;
// do it twice
*t++ = c;
}
*t++ = c;
++s;
} // loop over characters in the tty buffer

*t = 0;
if(o) o[t-dest] = s-rb->cursor;

return 0;
} // acs_getsentence

void acs_endsentence(achar *dest)
{
} // acs_endsentence

// Synthesizer descriptor and some routines for a serial connection.

int ss_fd0, ss_fd1;

talking_handler_t talking_h;
imark_handler_t imark_h;

static struct termios tio; // tty io control

// Set up tty with either hardware or software flow control
static unsigned int thisbaud = B9600;
int ess_flowcontrol(int hw)
{
tio.c_iflag = IGNBRK | INPCK | ISTRIP;
if(!hw) tio.c_iflag |= IXON | IXOFF;
tio.c_oflag = 0;
tio.c_cflag = PARENB | HUPCL | CS8 | CREAD | thisbaud;
if(hw) tio.c_cflag |= CRTSCTS;
tio.c_lflag = 0;
tio.c_cc[VSTOP] = 17;
tio.c_cc[VSTART] = 19;
    tio.c_cc[VMIN] = 1;
    tio.c_cc[VTIME] = 0;

return tcsetattr(ss_fd0, TCSANOW, &tio);
} // ess_flowcontrol

int ess_open(const char *devname, int baud)
{
static const int baudvalues[] = {
1200,
2400,
4800,
9600,
19200,
38400,
115200,
0};
static unsigned int baudbits[] = {
B1200,
B2400,
B4800,
B9600,
B19200,
B38400,
B115200,
0};
int j;

if(ss_fd0 >= 0) {
// already open
errno = EEXIST;
return -1;
}

ss_fd0 = open(devname, O_RDWR);
if(ss_fd0 < 0) return 0;
ss_fd1 = ss_fd0;

// Set up the tty characteristics.
// Especially important to have no echo and no cooked mode.
for(j=0; baudvalues[j]; ++j)
if(baud == baudvalues[j]) thisbaud = baudbits[j];
// Hardware flow by default, but you can change that.
if(ess_flowcontrol(1)) {
// ioctl failure ; don't understand.
// Hope errno helps.
close(ss_fd0);
ss_fd0 = ss_fd1 = -1;
return -1;
}

	// Send an initial CR.
		// Some units like to see this to establish baud rate.
usleep(5000);
write(ss_fd1, &crbyte, 1);

return 0;
} // ess_open

void ss_close(void)
{
if(ss_fd0 < 0) return; // already closed
close(ss_fd0);
ss_fd0 = ss_fd1 = -1;
} // ss_close

static fd_set channels;

int acs_ss_wait(void)
{
int rc;
int nfds;

memset(&channels, 0, sizeof(channels));
FD_SET(acs_fd, &channels);
FD_SET(ss_fd0, &channels);

nfds = ss_fd0 > acs_fd ? ss_fd0 : acs_fd;
++nfds;
rc = select(nfds, &channels, 0, 0, 0);
if(rc < 0) return; // should never happen

rc = 0;
if(FD_ISSET(acs_fd, &channels)) rc |= 1;
if(FD_ISSET(ss_fd0, &channels)) rc |= 2;
return rc;
} // acs_ss_wait

int ss_events(void)
{
int nr; // number of bytes read
int i;
static int leftover = 0;

if(ss_fd0 < 0) {
errno = ENXIO;
return -1;
}

nr = read(ss_fd0, ss_inbuf+leftover, SSBUFSIZE-leftover);
if(acs_debug) acs_log("synth read %d bytes\n", nr);
if(nr < 0) return -1;

i = 0;
nr += leftover;
while(i < nr) {
achar c = ss_inbuf[i];

switch(ss_style) {
case SS_STYLE_DOUBLE:
if(c >= 1 && c <= 99) {
if(acs_debug) acs_log("index %d\n", c);
indexSet(c);
if(imark_h) (*imark_h)(c);
++i;
continue;
}
if(acs_debug) acs_log("unknown byte %d\n", c);
++i;
break;

case SS_STYLE_DECPC: case SS_STYLE_DECEXP:
// This is butt ugly compared to the Doubletalk!
if(c == '\33') {
if(nr-i < 8) break;
if(!strncmp(ss_inbuf+i, "\33P0;32;", 7)) {
if(ss_inbuf[i+7] == 'z') {
if(acs_debug) acs_log("index %d\n", 0);
indexSet(0);
if(imark_h) (*imark_h)(0);
i += 8;
continue;
}
if(nr-i < 9) break;
if(ss_inbuf[i+8] == 'z' && isdigit(ss_inbuf[i+7])) {
c = ss_inbuf[i+7] - '0';
if(acs_debug) acs_log("index %d\n", c);
indexSet(c);
if(imark_h) (*imark_h)(c);
i += 9;
continue;
}
if(nr-i < 10) break;
if(ss_inbuf[i+9] == 'z' && isdigit(ss_inbuf[i+7]) && isdigit(ss_inbuf[i+8])) {
c = ss_inbuf[i+7] - '0';
c = 10*c + ss_inbuf[i+8] - '0';
if(acs_debug) acs_log("index %d\n", c);
indexSet(c);
if(imark_h) (*imark_h)(c);
i += 10;
continue;
}
}
}
if(acs_debug) acs_log("unknown byte %d\n", c);
++i;
break;

case SS_STYLE_BNS:
case SS_STYLE_ACE:
if(c == 6) {
if(acs_debug) acs_log("index f\n", 0);
indexSet(0);
if(imark_h) (*imark_h)(0);
++i;
continue;
}
if(acs_debug) acs_log("unknown byte %d\n", c);
++i;
break;

default:
if(acs_debug) acs_log("no style, synth data discarded\n", 0);
leftover = 0;
return -1;
} // switch

} // looping through input characters

done:
leftover = nr - i;
if(leftover) memmove(ss_inbuf, ss_inbuf+i, leftover);

return 0;
} // ss_events

int acs_ss_events(void)
{
int source = acs_ss_wait();
if(source&2) ss_events();
if(source&1) acs_events();
} // acs_ss_events

static char *dict1[NUMDICTWORDS];
static char *dict2[NUMDICTWORDS];
static int numdictwords;
static achar lowerdict[WORDLEN+1];

static int lowerword(const achar *w)
{
achar c;
int i;
for(i=0; c = *w; ++i, ++w) {
if(i == WORDLEN) return -1;
if(!isalpha(c)) return -1;
lowerdict[i] = tolower(c);
}
lowerdict[i] = 0;
return 0;
} // lowerword

static int
inDictionary(void)
{
int i;
for(i=0; i<numdictwords; ++i)
if(stringEqual(lowerdict, dict1[i])) return i;
return -1;
} // inDictionary

int acs_setword(const achar *word1, const achar *word2)
{
int j;
if(lowerword(word1)) return -1;
if(word2 && strlen(word2) > WORDLEN) return -1;
j = inDictionary();
if(j < 0) {
if(!word2) return 0;
// new entry
j = numdictwords;
if(j == NUMDICTWORDS) return -1; // no room
++numdictwords;
dict1[j] = malloc(strlen(lowerdict) + 1);
strcpy(dict1[j], lowerdict);
}
if(dict2[j]) free(dict2[j]);
dict2[j] = 0;
if(word2) {
dict2[j] = malloc(strlen(word2) + 1);
strcpy(dict2[j], word2);
return 0;
}
// deleting an entry
free(dict1[j]);
dict1[j] = 0;
--numdictwords;
dict1[j] = dict1[numdictwords];
dict2[j] = dict2[numdictwords];
dict1[numdictwords] = dict2[numdictwords] = 0;
return 0;
} // acs_setword

achar *acs_replace(const achar *word1)
{
int j;
if(lowerword(word1)) return 0;
j = inDictionary();
if(j < 0) return 0;
return dict2[j];
} // acs_replace

/*********************************************************************
A word is passed to us for possible replacement.
Our first task is to look it up in the replacement dictionary.
Has the user specified an alternate spelling?
Failing this, back up to the root word and see if that has been replaced.
Thus the user can specify   computer -> compeuter,
and the plural computers will be pronounced correctly.
This root-form replacement is suppressed if the initial word has
fewer than 5 letters (too short), or if
the rhs contains non-letters (don't know how to put the suffix back on).
The routine mkroot() leaves the root word in rootword[],
and returns a numeric code for the suffix that was removed.
The reconst() routine reconstitutes the word in the same array,
by appending the designated suffix; the suffix that was removed by mkroot().
*********************************************************************/

static int isvowel(int c)
{
return (c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' || c == 'y');
} // isvowel

static achar rootword[WORDLEN+8];

// Twelve regular English suffixes.
static const achar suftab[] = "s   es  ies ing ing ing d   ed  ed  ied 's  'll ";

// Which suffixes drop e or y when appended?
static const achar sufdrop[] = "  y  e   y  ";

// Which suffixes double the final consonent, as in dropped.
static const char sufdouble[] = {
	0,0,0,0,1,0,0,1,0,0,0,0};

// extract the root word
static int mkroot(void)
{
	achar l0, l1, l2, l3, l4; /* trailing letters */
	short wdlen, l;

strcpy(rootword, lowerdict);
	wdlen = strlen((char*)rootword);
	l = wdlen - 5;
	if(l < 0) return 0; // word too short to safely rootinize
	l4 = rootword[l+4];
	l3 = rootword[l+3];
	l2 = rootword[l+2];
	l1 = rootword[l+1];
	l0 = rootword[l+0];

	if(l4 == 's') { // possible plural
		if(strchr("siau", (char)l3)) return 0;
		if(l3 == '\'') {
			rootword[l+3] = 0;
			return 11;
		}
		if(l3 == 'e') {
			if(l2 == 'i') {
				rootword[l+2] = 'y';
				rootword[l+3] = 0;
				return 3;
			}
			if(strchr("shz", (char)l2)) {
				rootword[l+3] = 0;
				return 2;
			}
		}

		rootword[l+4] = 0;
		return 1;
	} // end final s

	if(l == 0) return 0; /* too short */

	if(l4 == 'l' && l3 == 'l' && l2 == '\'') {
		rootword[l+2] = 0;
		return 12;
	}

	if(l4 == 'g') { // possible present progressive
		if(l3 != 'n' || l2 != 'i') return 0;
		if(!isvowel(l1)) {
			if(l1 == l0) { rootword[l+1] = 0; return 5; }
			if(isvowel(l0) &&  l0 < 'w' && !isvowel(rootword[l-1])) {
				rootword[l+2] = 'e';
				rootword[l+3] = 0;
				return 6;
			}
		}
		rootword[l+2] = 0;
		return 4;
	} // end ing

	if(l4 == 'd') { // possible past tense
		if(l3 != 'e') return 0;
		if(l2 == 'i') {
			rootword[l+2] = 'y';
			rootword[l+3] = 0;
			return 10;
		}
		if(!isvowel(l2)) {
			if(l2 == l1) { rootword[l+2] = 0; return 8; }
			if(isvowel(l1) && l1 < 'w' && !isvowel(l0)) {
				rootword[l+4] = 0;
				return 7;
			}
		}
		rootword[l+3] = 0;
		return 9;
	} // end final ed

	return 0;
} // mkroot

// reconstruct the word based upon its root and the removed suffix
static void reconst(int root)
{
	achar *t;
	short i, wdlen;
	achar c;

	if(!root) return; /* nothing to do */

	--root;
	wdlen = strlen((char*)rootword);
	t = rootword + wdlen-1;
	if(sufdouble[root]) c = *t, *++t = c;
	if(sufdrop[root] == *t) --t;
	for(i=4*root; i<4*root+4; ++i) {
		c = suftab[i];
		if(c == ' ') break;
		*++t = c;
	}
	*++t = 0;
} // reconst

achar *acs_smartreplace(const achar *s)
{
int i, root;
achar *t;
int len = strlen(s);
	if(len > WORDLEN) return 0; // too long
t = acs_replace(s);
if(t) return t;
// not there; look for the root
root = mkroot();
if(!root) return 0;
t = acs_replace(rootword);
if(!t) return 0;
		for(i=0; t[i]; ++i)
			if(!isalpha(t[i])) return 0;
strcpy(rootword, t);
		reconst(root);
return rootword;
} // acs_smartreplace

static int keycapture[ACS_NUM_KEYS];

static void skipWhite(char **t)
{
char *s = *t;
while(*s == ' ' || *s == '\t') ++s;
*t = s;
} // skipWhite

int acs_line_configure(char *s, syntax_handler_t syn_h)
{
	int mkcode, rc;
char *t, *u;
char save, c;

// leading whitespace doesn't matter
skipWhite(&s);

// comment line starts with #, except for ## space or# digit
if(s[0] == '#') {
if(!s[1]) return 0;
if(!strchr("1234567890.+-*/", s[1])) {
++s;
if(s[0] != '#') return 0;
if(s[1] != ' ' && s[1] != '\t') return 0;
}
}

mkcode = acs_ascii2mkcode(s, &s);
if(mkcode >= 0) { // key assignment

skipWhite(&s);
c = *s;
	if(c == '<') {
++s;
if(!*s) goto clear;
acs_setmacro(mkcode, s);
keycapture[key1key] |= key1ss;
acs_setkey(key1key, keycapture[key1key]);
return 0;
}

	if(!c) {
clear:
acs_clearmacro(mkcode);
acs_clearspeechcommand(mkcode);
keycapture[key1key] &= ~key1ss;
acs_setkey(key1key, keycapture[key1key]);
return 0;
}

// keystroke command
// call the syntax checker / converter if provided
if(syn_h && (rc = (*syn_h)(s))) return rc;
acs_setspeechcommand(mkcode, s);
keycapture[key1key] |= key1ss;
acs_setkey(key1key, keycapture[key1key]);
return 0;
}

// at this point we are setting the pronunciation of a punctuation or a word
c = *s;
if(!c) return 0; // empty line
if(isdigit((achar)c)) return -1;

t = strpbrk(s, " \t");
if(t) { save = *t; *t = 0; }

if(isalpha((achar)c)) {
if(!t) return acs_setword((achar*)s, 0);
u = t;
skipWhite(&u);
rc = acs_setword((achar*)s, (achar*)u);
*t = save;
return rc;
}

// punctuation
// cannot leave it with no pronunciation
if(!t) return -1;
*t = save;
skipWhite(&t);
if(!*t) return -1;
acs_setpunc((achar)c, (achar*)t);
return 0;
} // acs_line_configure

void acs_reset_configure(void)
{
int i;

acs_clearkeys();

for(i=0; i<numdictwords; ++i) {
free(dict1[i]);
free(dict2[i]);
}
numdictwords = 0;

for(i=0; i<256; ++i)
acs_setpunc(i, firstpunclist[i]);

for(i=0; i<MK_BLOCK*8; ++i) {
acs_clearmacro(i);
acs_clearspeechcommand(i);
}
} // acs_reset_configure

static void ss_cr(void)
{
if(ss_style == SS_STYLE_DECEXP || ss_style == SS_STYLE_DECPC)
write(ss_fd1, &kbyte, 1);
write(ss_fd1, &crbyte, 1);
}

int ss_say_string(const achar *s)
{
write(ss_fd1, s, strlen(s));
ss_cr();
} // ss_say_string

int ss_say_char(achar c)
{
char c2[2];
achar *s = acs_getpunc(c);
if(!s) {
c2[0] = c;
c2[1] = 0;
s = c2;
}
ss_say_string(s);
} // ss_say_char

int ss_say_string_imarks(const achar *s, const unsigned short *o, int mark)
{
const achar *t;
char ibuf[12]; // index mark buffer

imark_start = rb->cursor;
imark_end = 0;
if(ss_style == SS_STYLE_BNS || ss_style == SS_STYLE_ACE) mark = 0;
imark_first = mark;

t = s;
while(*s) {
if(*o && mark >= 0 && mark <= 100) { // mark here
// have to send the prior word
write(ss_fd1, t, s-t);
t = s;
// set the index marker
imark_loc[imark_end++] = *o;
// send the index marker
ibuf[0] = 0;
switch(ss_style) {
case SS_STYLE_DOUBLE:
sprintf(ibuf, "\1%di", mark);
break;
case SS_STYLE_BNS:
case SS_STYLE_ACE:
strcpy(ibuf, "\06");
-- bnsf; // you owe me another control f
break;
case SS_STYLE_DECPC: case SS_STYLE_DECEXP:
/* Send this the most compact way we can - 9600 baud can be kinda slow. */
sprintf(ibuf, "[:i r %d]", mark);
break;
} // switch
if(ibuf[0]) {
write(ss_fd1, ibuf, strlen(ibuf));
++mark;
}
}
++s, ++o;
}

if(s > t)
write(ss_fd1, t, s-t);
ss_cr();
} // ss_say_string_imarks

void ss_shutup(void)
{
achar ibyte; // interrupt byte

switch(ss_style) {
case SS_STYLE_DOUBLE:
case SS_STYLE_BNS:
case SS_STYLE_ACE:
ibyte = 24;
break;
default:
ibyte = 3;
break;
} // switch

write(ss_fd1, &ibyte, 1);

imark_start = 0;
bnsf = 0;
} // ss_shutup

void
ss_startvalues(void)
{
ss_curvoice = 0;
ss_curvolume = ss_curspeed = 5;
ss_curpitch = 3;

switch(ss_style) {
case SS_STYLE_DOUBLE:
ss_curpitch = 4;
break;

case SS_STYLE_BNS:
case SS_STYLE_ACE:
ss_curvolume = 7;
break;

} // switch
} // ss_startvalues

static void
ss_writeString(const char *s)
{
write(ss_fd1, s, strlen(s));
} /* ss_writeString */

int ss_setvolume(int n)
{
static char doublestring[] = "\01xv";
static char dtpcstring[] = "[:vo set dd]";
static char extstring[] = "[:dv g5 dd]";
static char bnsstring[10];
static char acestring[] = "\33A5";

if(n < 0 || n > 9) return -1;

switch(ss_style) {
case SS_STYLE_DOUBLE:
doublestring[1] = '0' + n;
ss_writeString(doublestring);
return 0;

case SS_STYLE_DECPC:
n = 10 + 8*n;
dtpcstring[9] = '0' + n/10;
dtpcstring[10] = '0' + n%10;
ss_writeString(dtpcstring);
return 0;

case SS_STYLE_DECEXP:
/* The Dec Express takes volume levels from 60 to 86. */
/* This code gives a range from 60 to 85. */
n = 60 + n*72/25;
extstring[8] = '0' + n / 10;
extstring[9] = '0' + n % 10;
ss_writeString(extstring);
return 0;

case SS_STYLE_BNS:
sprintf(bnsstring, "\x05%02dV", (n+1) * 16 / 10);
ss_writeString(bnsstring);
return 0;

case SS_STYLE_ACE:
acestring[2] = '0' + n;
ss_writeString(acestring);
return 0;

} // switch

return -2;
} /* ss_setvolume */

int ss_incvolume(void)
{
if(ss_curvolume == 9) return -1;
if(ss_setvolume(ss_curvolume+1))
return -2;
++ss_curvolume;
return 0;
}

int ss_decvolume(void)
{
if(ss_curvolume == 0) return -1;
if(ss_setvolume(ss_curvolume-1))
return -2;
--ss_curvolume;
return 0;
}

int ss_setspeed(int n)
{
static char doublestring[] = "\1xs\1xa";
static char decstring[] = "[:ra ddd]";
static char bnsstring[10];
static char acestring[] = "\33R5";
static const char acerate[] ="02468ACEGH";

if(n < 0 || n > 9) return -1;

switch(ss_style) {
case SS_STYLE_DOUBLE:
doublestring[1] = doublestring[4] = '0' + n;
ss_writeString(doublestring);
return 0;

case SS_STYLE_DECEXP: case SS_STYLE_DECPC:
n = 50*n + 120;
sprintf(decstring+5, "%03d]", n);
ss_writeString(decstring);
return 0;

case SS_STYLE_BNS:
sprintf(bnsstring, "\x05%02dE", (n+1) * 14 / 10);
ss_writeString(bnsstring);
return 0;

case SS_STYLE_ACE:
acestring[2] = acerate[n];
ss_writeString(acestring);
return 0;

} // switch

return -2;
} /* ss_setspeed */

int ss_incspeed(void)
{
if(ss_curspeed == 9) return -1;
if(ss_setspeed(ss_curspeed+1))
return -2;
++ss_curspeed;
return 0;
}

int ss_decspeed(void)
{
if(ss_curspeed == 0) return -1;
if(ss_setspeed(ss_curspeed-1))
return -2;
--ss_curspeed;
return 0;
}

int ss_setpitch(int n)
{
static char doublestring[] = "\01xxp";
static const short tohurtz[] = {
66, 80, 98, 120, 144, 170, 200, 240, 290, 340};
static char decstring[] = "[:dv ap xxx]";
static char bnsstring[10];
static char acestring[] = "\33P5";

if(n < 0 || n > 9) return -1;

switch(ss_style) {
case SS_STYLE_DOUBLE:
n = 9*n + 10;
doublestring[1] = '0' + n/10;
ss_writeString(doublestring);
return 0;

case SS_STYLE_DECEXP: case SS_STYLE_DECPC:
n = tohurtz[n];
sprintf(decstring+8, "%d]", n);
ss_writeString(decstring);
return 0;

case SS_STYLE_BNS:
/* BNS pitch is 01 through 63.  An increment of 6, giving levels from 6 .. 60
should work well. */
sprintf(bnsstring, "\x05%02dP", (n+1) * 6);
ss_writeString(bnsstring);
return 0;

case SS_STYLE_ACE:
acestring[2] = '0' + n;
ss_writeString(acestring);
return 0;

} // switch

return -2;
} /* ss_setpitch */

int ss_incpitch(void)
{
if(ss_curpitch == 9) return -1;
if(ss_setpitch(ss_curpitch+1))
return -2;
++ss_curpitch;
return 0;
}

int ss_decpitch(void)
{
if(ss_curpitch == 0) return -1;
if(ss_setpitch(ss_curpitch-1))
return -2;
--ss_curpitch;
return 0;
}

/* Changing voice could reset the pitch */
// Return -1 if the synthesizer cannot support that voice.
int ss_setvoice(int v)
{
int rc = -1;
	char buf[8];
static const short doublepitch[] = {
2,4,2,4,6,4,5,1,8,2};
static const char decChars[] = "xphfdburwk";
static const short decpitch[] = {
-1,3,1,4,3,6,7,6,2,8};
static char acestring[] = "\33V5";

switch(ss_style) {
case SS_STYLE_DOUBLE:
if(v < 1 || v > 8) break;
		sprintf(buf, "\1%do", v-1);
		ss_writeString(buf);
ss_cr();
ss_curpitch = doublepitch[v];
rc = 0;
break;

case SS_STYLE_DECEXP: case SS_STYLE_DECPC:
if(v < 1 || v > 8) break;
		sprintf(buf, "[:n%c]", decChars[v]);
		ss_writeString(buf);
ss_cr();
ss_curpitch = decpitch[v];
rc = 0;
break;

case SS_STYLE_ACE:
acestring[2] = '0' + v;
ss_writeString(acestring);
rc = 0;
break;

} // switch

return rc;
} /* ss_setvoice */

/* Would the synth block if we sent it more text? */
int ss_blocking(void)
{
int rc;
int nfds;
struct timeval now;

memset(&channels, 0, sizeof(channels));
FD_SET(ss_fd1, &channels);
now.tv_sec = 0;
now.tv_usec = 0;
nfds = ss_fd1 + 1;
rc = select(nfds, 0, &channels, 0, &now);
if(rc < 0) return 0; // should never happen
rc = 0;
if(FD_ISSET(ss_fd1, &channels)) rc = 1;
return rc ^ 1;
} /* ss_blocking */

/* Is the synth still talking? */
int ss_stillTalking(void)
{
/* If we're blocked then we're definitely still talking. */
if(ss_blocking()) return 1;

/* Might put in some special code for doubletalk,
 * as they use ring indicator to indicate speech in progress.
 * Maybe later. */

/* If there is no index marker, then we're not speaking a sentence.
 * Just a word or letter or command confirmation phrase.
 * We don't need to interrupt that, and it's ok to send the next thing. */
if(!imark_start) return 0;

/* If we have reached the last index marker, the sentence is done.
 * In that case imark_start should be 0, and we shouldn't be here.
 * But if we are on the penultimate index marker,
 * the sentence is nearly done, and that's close enough.
 * Start sending the next sentence, and things won't be so choppy. */
if(imark_end < 2) return 0;
if(!rb->cursor) return 0; /* should never happen */
if(rb->cursor - imark_start == imark_loc[imark_end-2])
return 0;

/* Still waiting for index markers. */
return 1;
} /* ss_stillTalking */

