/*********************************************************************
File: acstalk.c
Description: communicate with a synthesizer over a serial port, socket, or pipe.
*********************************************************************/

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/select.h>
#include <ctype.h>
#include <unistd.h>
#include <stdarg.h>
#include <signal.h>

#include "acsbridge.h"

#define stringEqual !strcmp

#define SSBUFSIZE 64 // synthesizer buffer for events
static char ss_inbuf[SSBUFSIZE]; /* input buffer for the synthesizer */

/* The file descriptors to and from the synth. */
/* These are the same for serial port or socket; different if over a pipe. */
int ss_fd0 = -1, ss_fd1 = -1;

/* parent process, if a child is forked to manage the software synth. */
static int pss_pid;
/* Set this for broken pipe - need to respawnw the child */
int pss_broken;

/* What is the style of synthesizer? */
int ss_style;

/* current synth parameters */
int ss_curvolume, ss_curpitch, ss_curspeed, ss_curvoice;

/* What are the speech parameters when the unit is first turned on? */
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
} /* ss_startvalues */

/* Handler - as index markers are returned to us */
imark_handler_t ss_imark_h;

/* send return to the synth - start speaking */
static const char kbyte = '\13';
static const char crbyte = '\r';
static void ss_cr(void)
{
if(ss_style == SS_STYLE_DECEXP || ss_style == SS_STYLE_DECPC)
write(ss_fd1, &kbyte, 1);
write(ss_fd1, &crbyte, 1);
}

/* The start of the sentence that is sent with index markers. */
unsigned int *imark_start;

/* location of each index marker relative to imark_start */
static ofs_type imark_loc[100];
static int imark_first, imark_end;
static int bnsf; // counting control f's from bns

/* move the cursor to the returned index marker. */
static void indexSet(int n)
{
if(ss_style == SS_STYLE_BNS || ss_style == SS_STYLE_ACE) {
/* don't return until you have done this bookkeeping. */
++bnsf;
n = imark_end + bnsf;
} else {
n -= imark_first;
}

if(!imark_start) return;
if(n < 0 || n >= imark_end) return;
rb->cursor = imark_start + imark_loc[n];
if(acs_debug)
acs_log("imark %d cursor now base+%d\n", n, imark_loc[n]);

/* should never be past the end of buffer, but let's check */
if(rb->cursor >= rb->end) {
rb->cursor = rb->end;
if(rb->end > rb->start) --rb->cursor;
imark_start = 0;
if(acs_debug) acs_log("cursor ran past the end of buffer\n");
}

if(n == imark_end - 1) {
/* last index marker, sentence is finished */
if(acs_debug) acs_log("sentence spoken\n");
imark_start = 0;
}

if(ss_imark_h) (*ss_imark_h)(n+1, imark_end);
} // indexSet

static struct termios tio; // tty io control

/* Set up tty with either hardware or software flow control */
static unsigned int thisbaud = B9600;
int ess_flowcontrol(int hw)
{
tio.c_iflag = IGNBRK | ISTRIP | IGNPAR;
if(!hw) tio.c_iflag |= IXON | IXOFF;
tio.c_oflag = 0;
tio.c_cflag = PARENB | HUPCL | CS8 | CREAD | thisbaud | CLOCAL;
if(hw) tio.c_cflag |= CRTSCTS;
tio.c_lflag = 0;
tio.c_cc[VSTOP] = 17;
tio.c_cc[VSTART] = 19;
    tio.c_cc[VMIN] = 1;
    tio.c_cc[VTIME] = 0;

return tcsetattr(ss_fd0, TCSANOW, &tio);
} // ess_flowcontrol

/* Get status lines from the serial device.
 * Active status lines are indicated with the following bit definitions:
 * TIOCM_RTS Request To Send (output signal)
 * TIOCM_DTR Data Terminal Ready (output signal)
 * TIOCM_CAR Data Carrier Detect (input signal)
 * TIOCM_RNG Ring Indicator (input signal)
 * TIOCM_DSR Data Set Ready (input signal)
 * TIOCM_CTS Clear To Send (input signal) */
static int
getModemStatus(void)
{
int sigs, rc;
rc = ioctl(ss_fd0, TIOCMGET, &sigs);
if(rc) sigs = 0;
return sigs;
/* We should never need to do this one.
sigs = TIOCM_RTS | TIOCM_DTR;
rc = ioctl(fd, TIOCMBIS, &sigs);
*/
} /* getModemStatus */

/* For debugging */
static void
printModemStatus(void)
{
int sigs = getModemStatus();
printf("serial");
if(sigs&TIOCM_DSR) printf(" dsr");
if(sigs&TIOCM_CAR) printf(" carrier");
if(sigs&TIOCM_RNG) printf(" ring");
if(sigs&TIOCM_CTS) printf(" cts");
puts("");
} /* printModemStatus */

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

ss_fd0 = open(devname, O_RDWR|O_NONBLOCK);
if(ss_fd0 < 0) return 0;
ss_fd1 = ss_fd0;

// Set up the tty characteristics.
// Especially important to have no echo and no cooked mode and clocal.
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

/* Now that clocal is set, go back to blocking mode
 * In other words, clear the nonblock bit.
 * The other bits can all be zero, they don't mean anything on a serial port. */
fcntl(ss_fd0, F_SETFL, 0);

	// Send an initial CR.
		// Some units like to see this to establish baud rate.
usleep(5000);
write(ss_fd1, &crbyte, 1);
usleep(2000);

return 0;
} // ess_open

void ss_close(void)
{
if(ss_fd0 < 0) return; // already closed
close(ss_fd0);
if(ss_fd1 != ss_fd0)
close(ss_fd1);
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
char c = ss_inbuf[i];

switch(ss_style) {
case SS_STYLE_DOUBLE:
if(c >= 1 && c <= 99) {
if(acs_debug) acs_log("index %d\n", c);
indexSet(c);
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
i += 8;
continue;
}
if(nr-i < 9) break;
if(ss_inbuf[i+8] == 'z' && isdigit((unsigned char)ss_inbuf[i+7])) {
c = ss_inbuf[i+7] - '0';
if(acs_debug) acs_log("index %d\n", c);
indexSet(c);
i += 9;
continue;
}
if(nr-i < 10) break;
if(ss_inbuf[i+9] == 'z' && isdigit((unsigned char)ss_inbuf[i+7]) && isdigit((unsigned char)ss_inbuf[i+8])) {
c = ss_inbuf[i+7] - '0';
c = 10*c + ss_inbuf[i+8] - '0';
if(acs_debug) acs_log("index %d\n", c);
indexSet(c);
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

int ss_say_string(const char *s)
{
write(ss_fd1, s, strlen(s));
ss_cr();
} // ss_say_string

int ss_say_char(char c)
{
char c2[2];
char *s = acs_getpunc(c);
if(!s) {
c2[0] = c;
c2[1] = 0;
s = c2;
}
ss_say_string(s);
} // ss_say_char

int ss_say_string_imarks(const char *s, const ofs_type *o, int mark)
{
const char *t;
char ibuf[12]; // index mark buffer
const ofs_type *o0 = o;

imark_start = rb->cursor;
imark_end = 0;
if(ss_style == SS_STYLE_BNS || ss_style == SS_STYLE_ACE) mark = 0;
imark_first = mark;

t = s;
while(1) {
if(*o && mark >= 0 && mark <= 100) { // mark here
// have to send the prior word
if(s > t)
write(ss_fd1, t, s-t);
t = s;
// set the index marker
imark_loc[imark_end++] = *o;
// send the index marker
ibuf[0] = 0;
switch(ss_style) {
case SS_STYLE_DOUBLE:
/* The following if statement addresses a bug that is, as far as I know,
 * specific to doubletalk.
 * We can't send a single letter, and then an index marker
 * on the next word.  It screws everything up!
 * But we have to send it if that is the end of the sentence. */
if(o-o0 > 2 || !*s)
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
if(ibuf[0])
write(ss_fd1, ibuf, strlen(ibuf));
++mark;
}
if(!*s) break;
++s, ++o;
}

/* End of string should always be an index marker,
 * so there should be nothing else to send.
 * But just in case ... */
if(s > t)
write(ss_fd1, t, s-t);

ss_cr();
if(acs_debug)
acs_log("sent %d markers, last offset %d\n", imark_end, imark_loc[imark_end-1]);
} // ss_say_string_imarks

void ss_shutup(void)
{
char ibyte; // interrupt byte

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
if(acs_debug) acs_log("shutup\n");
} // ss_shutup

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
int n0 = n;

if(n < 0 || n > 9) return -1;

switch(ss_style) {
case SS_STYLE_DOUBLE:
doublestring[1] = '0' + n;
ss_writeString(doublestring);
break;

case SS_STYLE_DECPC:
n = 10 + 8*n;
dtpcstring[9] = '0' + n/10;
dtpcstring[10] = '0' + n%10;
ss_writeString(dtpcstring);
break;

case SS_STYLE_DECEXP:
/* The Dec Express takes volume levels from 60 to 86. */
/* This code gives a range from 60 to 85. */
n = 60 + n*72/25;
extstring[8] = '0' + n / 10;
extstring[9] = '0' + n % 10;
ss_writeString(extstring);
break;

case SS_STYLE_BNS:
sprintf(bnsstring, "\x05%02dV", (n+1) * 16 / 10);
ss_writeString(bnsstring);
break;

case SS_STYLE_ACE:
acestring[2] = '0' + n;
ss_writeString(acestring);
break;

default:
return -2;
} // switch

ss_curvolume = n0;
return 0;
} /* ss_setvolume */

int ss_incvolume(void)
{
if(ss_curvolume == 9) return -1;
if(ss_setvolume(ss_curvolume+1))
return -2;
return 0;
}

int ss_decvolume(void)
{
if(ss_curvolume == 0) return -1;
if(ss_setvolume(ss_curvolume-1))
return -2;
return 0;
}

int ss_setspeed(int n)
{
static char doublestring[] = "\1xs\1xa";
static char decstring[] = "[:ra ddd]";
static char bnsstring[10];
static char acestring[] = "\33R5";
static const char acerate[] ="02468ACEGH";
int n0 = n;

if(n < 0 || n > 9) return -1;

switch(ss_style) {
case SS_STYLE_DOUBLE:
doublestring[1] = doublestring[4] = '0' + n;
ss_writeString(doublestring);
break;

case SS_STYLE_DECEXP: case SS_STYLE_DECPC:
n = 50*n + 120;
sprintf(decstring+5, "%03d]", n);
ss_writeString(decstring);
break;

case SS_STYLE_BNS:
sprintf(bnsstring, "\x05%02dE", (n+1) * 14 / 10);
ss_writeString(bnsstring);
break;

case SS_STYLE_ACE:
acestring[2] = acerate[n];
ss_writeString(acestring);
break;

default:
return -2;
} // switch

ss_curspeed = n0;
return 0;
} /* ss_setspeed */

int ss_incspeed(void)
{
if(ss_curspeed == 9) return -1;
if(ss_setspeed(ss_curspeed+1))
return -2;
return 0;
}

int ss_decspeed(void)
{
if(ss_curspeed == 0) return -1;
if(ss_setspeed(ss_curspeed-1))
return -2;
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
int n0 = n;

if(n < 0 || n > 9) return -1;

switch(ss_style) {
case SS_STYLE_DOUBLE:
n = 9*n + 10;
doublestring[1] = '0' + n/10;
ss_writeString(doublestring);
break;

case SS_STYLE_DECEXP: case SS_STYLE_DECPC:
n = tohurtz[n];
sprintf(decstring+8, "%d]", n);
ss_writeString(decstring);
break;

case SS_STYLE_BNS:
/* BNS pitch is 01 through 63.  An increment of 6, giving levels from 6 .. 60
should work well. */
sprintf(bnsstring, "\x05%02dP", (n+1) * 6);
ss_writeString(bnsstring);
break;

case SS_STYLE_ACE:
acestring[2] = '0' + n;
ss_writeString(acestring);
break;

default:
return -2;
} // switch

ss_curpitch = n0;
return 0;
} /* ss_setpitch */

int ss_incpitch(void)
{
if(ss_curpitch == 9) return -1;
if(ss_setpitch(ss_curpitch+1))
return -2;
return 0;
}

int ss_decpitch(void)
{
if(ss_curpitch == 0) return -1;
if(ss_setpitch(ss_curpitch-1))
return -2;
return 0;
}

/* Changing voice could reset the pitch */
// Return -1 if the synthesizer cannot support that voice.
int ss_setvoice(int v)
{
	char buf[8];
static const short doublepitch[] = {
2,4,2,4,6,4,5,1,8,2};
static const char decChars[] = "xphfdburwk";
static const short decpitch[] = {
-1,3,1,4,3,6,7,6,2,8};
static char acestring[] = "\33V5";

switch(ss_style) {
case SS_STYLE_DOUBLE:
if(v < 1 || v > 8) return -1;
		sprintf(buf, "\1%do", v-1);
		ss_writeString(buf);
ss_cr();
ss_curpitch = doublepitch[v];
break;

case SS_STYLE_DECEXP: case SS_STYLE_DECPC:
if(v < 1 || v > 8) return -1;
		sprintf(buf, "[:n%c]", decChars[v]);
		ss_writeString(buf);
ss_cr();
ss_curpitch = decpitch[v];
break;

case SS_STYLE_ACE:
acestring[2] = '0' + v;
ss_writeString(acestring);
break;

default:
return -2; /* no voice function for this synth */
} // switch

return 0;
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
 * Or nearly done - still speaking the last word.
 * In that case imark_start should be 0, and we shouldn't be here.
 * But we are here, so return 1. */
return 1;
} /* ss_stillTalking */

/* Signal handler, to watch for broken pipe, or death of child,
 * which ever is more convenient. */

static void sig_h (int n)
{
pss_broken = 1;
/* reset the signal handler */
signal(n, sig_h);
}

/* Spin off the child process with a vector of args */
/* I wanted to use const char * const, but that's now how execvp works. */
int pss_openv(const char *progname,  char * const  alist[])
{
int p0[2]; /* pipe reading ss_fd0 */
int p1[2]; /* pipe writing ss_fd1 */

if (pipe(p0) == -1 || pipe(p1) == -1)
return -1;

pss_pid = fork();
switch(pss_pid) {
case -1:
perror("fork");
return -1;

case 0: /* child */
if (dup2(p0[1], STDOUT_FILENO) == -1 ||
dup2(p1[0], STDIN_FILENO) == -1) {
perror("dup2");
exit(1);
}
/* close the unneeded fds */
close(p0[0]);
close(p1[1]);
if(p0[1] != STDOUT_FILENO) close(p0[1]);
if(p1[0] != STDIN_FILENO) close(p1[0]);
/* run the program */
execvp(progname, alist);
perror("execv");
exit(1);

default: /* parent */
ss_fd0 = p0[0];
ss_fd1 = p1[1];
close(p0[1]);
close(p1[0]);
} /* switch */

/* watch for broken pipe, indicating no child process */
signal(SIGPIPE, sig_h);
pss_broken = 0;

return 0;
} /* pss_openv */

/* This isn't like printfv; I don't have a string with percent directives
 * to tell me how many args you are passing, or the type of each arg.
 * So each arg must be a string, and you must end the list with NULL.
 * Unfortunately I have to repack everything, so arg0 is the program name. */
#define MAX_ARGS 16
int pss_open(const char *progname, ...)
{
char * alist[MAX_ARGS+2];
int count = 0;
va_list ap;
va_start(ap, progname);
alist[count++] = (char*)progname;
while(count <= MAX_ARGS+1) {
alist[count] = va_arg(ap, char*);
if(!alist[count]) break;
++count;
}
alist[count] = 0;
va_end(ap);
return pss_openv(progname, alist);
} /* pss_open */

