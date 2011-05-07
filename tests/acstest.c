/* acstest.c: test the acsint system.
 * This is not a comprehensive test; just trying out a few features.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include "acsbridge.h"

#define stringEqual !strcmp

/*********************************************************************
configure this little test adapter.
Read acstest.cfg and pass each line to acs_line_configure().
A little syntax checker verifies the first letter of each speech command.
*********************************************************************/

static int syntaxcheck(char *s)
{
if(strchr("bcdghklmrswz", *s)) return 0; // ok
return -1; // don't recognize that one
} // syntaxcheck

static void
load_configure(const char *filename)
{
FILE *f;
char line[200];
char *s;
int lineno;

f = fopen(filename, "r");
if(!f) {
fprintf(stderr, "cannot open config file %s\n", filename);
return;
}

lineno = 0;
while(fgets(line, sizeof(line), f)) {
++lineno;

// strip off crlf
s = line + strlen(line);
if(s > line && s[-1] == '\n') --s;
if(s > line && s[-1] == '\r') --s;
*s = 0;

if(acs_line_configure(line, syntaxcheck))
fprintf(stderr, "syntax error in line %d\n", lineno);
}
fclose(f);
} // configure

static char moreStuff = 0;
static char doneRead = 0;

static void setMoreStuff(int echo, unsigned int c)
{
/* if not an echo character, then the computer is generating more output to read */
if(!echo) moreStuff = 1;
} // setMoreStuff

/*********************************************************************
My fake out synthesizer simply throws an alarm in 3 seconds,
as though it took 3 seconds to read the sentence that you just sent.
When the alarm goes off, we will see if there is anything else to read.
Perhpas more stuff has been printed during that time.
This is how my "read to the end of buffer" command works.
It's not quite the same as automatic reading, and I'll get to that later.

Realize that a true synthesizer will not interrupt this process when it is
ready for more.
It will send a message back over a pipe or socket or serial port.
We'll use the select(2) call to monitor both devices simultaneously.
But this is a quick and dirty test.
*********************************************************************/

static void
catchAlarm(int n)
{
doneRead = 1;
} // catchAlarm

static void
fakeSynthesizer(void)
{
signal(SIGALRM, catchAlarm);
alarm(3);
} // fakeSynthesizer

// states
static char screenmode = 0;

static const short scale[] = {
400, 20, 447, 20, 500, 20,  533, 20,
600, 20, 670, 20, 750, 20, -1, 20, 800, 20, 0};
static const short doorbell[] = {
1200, 25, -1, 5, 1000, 25, 0};

static void
keystroke(int key, int ss, int leds)
{
char dumpfile[12];
int dump_fd;
char grab[20];
int mkcode; // modified key code
char *cmd; // the speech command
char c;
char sentence[400];
int gsprop = ACS_GS_STOPLINE | ACS_GS_REPEAT;

/* Find the command that is bound to this key and state */
mkcode = acs_build_mkcode(key, ss);
cmd = acs_getspeechcommand(mkcode);

//There ought to be a speech command, else why were we called?
// Oh well, let's make sure.
if(!cmd) return;

// Don't switch on key or state.
// We're done with that.
// Switch on the command, and take action accordingly.
// Thus it is independent of the key bindings.
// Notice we don't have to worry about macros here.
// They are handled by acs_events().

switch(cmd[0]) {
case 'z':
acs_buzz();
break;

case 'h':
acs_highbeeps();
break;

case 'm':
acs_notes(scale);
break;

case 'b':
acs_bypass();
break;

case 'c':
if(screenmode) {
acs_bell();
} else {
acs_clearbuf();
acs_tone_onoff(0);
}
break;

case 's':
screenmode ^= 1;
acs_screenmode(screenmode);
acs_tone_onoff(screenmode);
break;

case 'w': gsprop |= ACS_GS_ONEWORD;
case 'g':
acs_startbuf();
if(acs_getsentence(sentence, sizeof(sentence), 0, gsprop) < 0)
puts("error");
else if(!sentence[0])
puts("nothing");
else {
printf("%s", sentence);
if(cmd[0] == 'w') puts("");
}
break;

case 'd':
sprintf(dumpfile, "dump%d", acs_fgc);
dump_fd = open(dumpfile, O_WRONLY|O_TRUNC|O_CREAT, 0666);
if(dump_fd >= 0) {
acs_startbuf();
/* nobody said this was efficient */
while((c = acs_getc())) {
write(dump_fd, &c, 1);
acs_forward();
}
close(dump_fd);
puts(dumpfile);
}
break;

case 'k':
acs_tone_onoff(0);
if(!acs_keystring(grab, sizeof(grab), ACS_KS_DEFAULT))
printf("<%s>\n", grab);
break;

case 'l':
acs_tone_onoff(0);
if(acs_get1char(&c) < 0)
acs_bell();
else printf("%c\n", c);
break;

case 'r':
acs_tone_onoff(0);
moreStuff = 0;
fakeSynthesizer();
break;

} // switch
} // keystroke

int
main(int argc, char **argv)
{
++argv, --argc;

if(argc > 0 && stringEqual(argv[0], "-d")) {
acs_debug = 1;
++argv, --argc;
}

if(acs_open("/dev/acsint") < 0) {
fprintf(stderr, "cannot open the driver /dev/acsint;\n\
did you make this character special major 11,\n\
and do you have permission to read and write it,\n\
and did you install the acsint module,\n\
and do you also have permission to read /dev/vcsa?\n");
exit(1);
}

acs_key_h = keystroke;
acs_more_h = setMoreStuff;

// this has to run after the device is open,
// because it sends "key capture" commands to the acsint driver
acs_reset_configure();
load_configure("acstest.cfg");

// This runs forever, you have to hit interrupt to kill it,
// or kill it from another tty.
while(1) {
acs_events();
if(doneRead) {
doneRead = 0;
if(moreStuff) {
acs_refresh();
moreStuff = 0;
// I'm testing this feature; I need to know I just did a refresh.
// I don't want to print anything out, that messes up the test.
// And it's awkward to send anything to my speech synthesizer;
// I'm already using that synthesizer as my (debugging) adapter.
// So I'll just ring the doorbell, like I'm asking for more.
acs_notes(doorbell);
// And speak the new stuff we just got.
fakeSynthesizer();
}
}
}

acs_close();
} // main

