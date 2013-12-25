/*********************************************************************

jupiter.c: jupiter speech adapter.

Copyright (C) Karl Dahlke, 2014.
This software may be freely distributed under the GPL, general public license,
as articulated by the Free Software Foundation.
*********************************************************************/

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <locale.h>

#include <linux/vt.h>

#include "tp.h"

#define stringEqual !strcmp


/* Speech command structure, one instance for each jupiter command. */
struct cmd {
	const char *desc; // description
	const char brief[12]; // brief name for the command
	char nonempty; // buffer must be nonempty
	char endstring; // must be last in a command sequence
	char nextchar; // needs next key to complete command
	char nextline; // needs line of text to complete command
};

/* the available speech commands */
static const struct cmd speechcommands[] = {
	{0,""}, // 0 is not a function
	{"clear buffer","clbuf"},
	{"visual cursor","cursor",1},
	{"start of buffer","sbuf",1},
	{"end of buffer","ebuf",1},
	{"start of line","sline",1},
	{"end of line","eline",1},
	{"start of word","sword",1},
	{"end of word","eword",1},
	{"left spaces","lspc",1},
	{"right spaces","rspc",1},
	{"back one character","back",1},
	{"forward one character","for",1},
	{"preivious row","prow",1},
	{"next row","nrow",1},
	{"reed the current karecter as a nato word","asword",1,1},
	{"reed the current karecter","char",1,1},
	{"read capital x as cap x","capchar",1,1},
	{"current cohllumm number","colnum",1,1},
	{"reed the current word","word",1,1},
	{"start reeding","read",1,1},
	{"stop speaking","shutup"},
	{"pass next karecter through","bypass",0,1},
	{"clear bighnary mode","clmode",0,0,1},
	{"set bighnary mode","stmode",0,0,1},
	{"toggle bighnary mode","toggle",0,0,1},
	{"search up","searchu",1,1,0,1},
	{"search down","searchd",1,1,0,1},
	{"set volume","volume",0,0,1},
	{"increase volume", "incvol"},
	{"decrease volume", "decvol"},
	{"set speed","speed",0,0,1},
	{"increase speed", "incspd"},
	{"decrease speed", "decspd"},
	{"set pitch","pitch",0,0,1},
	{"increase pitch", "incpch"},
	{"decrease pitch", "decpch"},
	{"set voice", "voice", 0, 0, 1},
	{"key binding","bind",0,1,0,1},
	{"last complete line","lcline",1},
	{"mark left", "markl", 1},
	{"mark right", "markr", 1, 0, 1},
	{"set echo", "echo", 0, 0, 1},
	{"label", "label", 1, 0, 1},
	{"jump", "jump", 1, 0, 1},
	{"restart the adapter","reexec",0,1},
	{"reload the config file","reload",0,1,1},
	{"dump buffer","dump",0, 1},
	{"suspend the adapter","suspend",0,1},
	{"test step function","step",0,1},
	{0,""}
};

static short const max_cmd = sizeof(speechcommands)/sizeof(struct cmd) - 1;

/* Make sure these correspond to the commands above */
#define CMD_BYPASS 22
#define CMD_REEXEC 45
#define CMD_RELOAD 46
#define CMD_SUSPEND 48

/* messages in the languages supported. */

static const char *usageMessage[] = {
"",
// English
"usage:  jupiter [-d] [-a] [-c configfile] synthesizer port\n"
"-d is daemon mode, run in background.\n"
"-a is alphanumeric translations, such as dates,\n"
"\ttimes, fractions, urls, and so on.\n"
"Synthesizer is: dbe = doubletalk external,\n"
"dte = dectalk external, dtp = dectalk pc,\n"
"bns = braille n speak, ace = accent, esp = espeakup.\n"
"port is 0 1 2 or 3, for the serial device.\n"
"jupiter tc    to test the configuration file.\n",
// German (but still English)
"usage:  jupiter [-d] [-a] [-c configfile] synthesizer port\n"
"-d is daemon mode, run in background.\n"
"-a is alphanumeric translations, such as dates,\n"
"\ttimes, fractions, urls, and so on.\n"
"Synthesizer is: dbe = doubletalk external,\n"
"dte = dectalk external, dtp = dectalk pc,\n"
"bns = braille n speak, ace = accent, esp = espeakup.\n"
"port is 0 1 2 or 3, for the serial device.\n"
"jupiter tc    to test the configuration file.\n",
// Portuguese
"uso: jupiter [-d] [-a] [-c arq. de config.] sintetizador porta\n"
"-d é modo daemon, roda em segundo plano.\n"
"-a é tradução alfanumérica, para datas,\n"
"\thoras, frações, URLs, etc.\n"
"Sintetizador é: dbe = doubletalk externo,\n"
"dte = dectalk externo, dtp = dectalk pc,\n"
"bns = braille n speak, ace = accent, esp = espeakup.\n"
"porta é 0 1 2 ou 3, para o dispositivo serial.\n"
"jupiter tc    para testar o arquivo de configuração.\n",
// French
"",
};

static void
usage(void)
{
fprintf(stderr, "%s", usageMessage[acs_lang]);
exit(1);
}

static const char *openConfig[] = {"",
"cannot open config file %s\n",
"nicht öfnen config file %s\n",
"impossível abrir arquivo de configuração %s\n",
"impossible d'ouvrir fichier de configuration %s\n",
};

static const char *openDriver[] = {0,
"cannot open the device driver %s;\n%s.\n",
"nicht öfnen die device driver %s;\n%s.\n",
"impossível abrir arquivo de dráiver do dispositivo %s;\n%s.\n",
"impossible d'ouvrir device driver %s;\n%s.\n",
};

static const char *openSerial[] = {0,
"cannot open the serial port %s\n",
"nicht öfnen die serial port %s\n",
"impossível abrir arquivo de porta do serial %s\n",
"impossible d'ouvrir auto port %s\n",
};

static const char *execSoft[] = {0,
"cannot execute command %s\n",
"nicht execute command %s\n",
"impossível executar comando %s\n",
"impossible de lancer commande %s\n",
};

static const char *busyDriver[] = {0,
"Acsint can only be opened by one program at a time.\n"
"Another program is currently using the acsint device driver.\n",
"Acsint can only be opened by one program at a time.\n"
"Another program is currently using the acsint device driver.\n",
"O acsint só pode ser aberto por um programa de cada vez.\n"
"Outro programa está usando o dispositivo acsint no momento.\n",
"Acsint can only be opened by one program at a time.\n"
"Another program is currently using the acsint device driver.\n",
};

static const char *permDriver[] = {0,
"Check the permissions on /dev/vcsa and %s\n",
"Check the permissions on /dev/vcsa and %s\n",
"Verifique as permissões para /dev/vcsa e %s\n",
"Check the permissions on /dev/vcsa and %s\n",
};

static const char *makeDriver[] = {0,
"Did you make %s character special,\n"
"and did you install the acsint module?\n",
"Did you make %s character special,\n"
"and did you install the acsint module?\n",
"Já criou o dispositivo especial de caractere %s\n"
"e instalou o módulo acsint?\n",
"Did you make %s character special,\n"
"and did you install the acsint module?\n",
};

static const char *configError[][10] = {
{0, // no language
},{ // English
0, "syntax error",
"%s cannot be in the middle of a composite speech command",
"%s must be followed by a letter or digit",
"%s is not a recognized speech command",
"bypass cannot be mixed with any other commands",
"dictionary word or replacement word is too long",
"too many words in the replacement dictionary",
"cannot leave a punctuation or unicode with no pronunciation",
"cannot set the pronunciation of a letter, digit, or low or high unicode",
},{ // German
0, "syntax error",
"%s cannot be in the middle of a composite speech command",
"%s must be followed by a letter or digit",
"%s is not a recognized speech command",
"bypass cannot be mixed with any other commands",
"dictionary word or replacement word is too long",
"too many words in the replacement dictionary",
"cannot leave a punctuation or unicode with no pronunciation",
"cannot set the pronunciation of a letter, digit, or low or high unicode",
},{ // Portuguese
0, "erro de sintaxe",
"%s não pode estar no meio dum comando de fala composto",
"%s tem que ser seguido por uma letra ou dígito",
"%s não é um comando de fala conhecido",
"passar não pode ser misturado com outros comandos",
"palavra original ou palavra substituta longa demais",
"palavras demais no dicionário de substituição",
"não é possível deixar uma pontuação ou unicode sem pronúncia",
"não é possível determinar a pronúncia duma letra, algarismo, ou unicode baixo ou alto",
},{ // French
0, "erreur de syntaxe",
"%s cannot be in the middle of a composite speech command",
"%s must be followed by a letter or digit",
"%s is not a recognized speech command",
"bypass cannot be mixed with any other commands",
"dictionary word or replacement word is too long",
"too many words in the replacement dictionary",
"cannot leave a punctuation or unicode with no pronunciation",
"cannot set the pronunciation of a letter, digit, or low or high unicode",
}};

static const char *bufword[] = {0,
"buffer",
"buffer",
"buffer",
"tampon",
};

static const char *lineword[] = {0,
"line",
"linia",
"linha",
"ligne",
};

static const char *yesword[] = {0,
"yes",
"ja",
"sim",
"uix",
};

static const char *noword[] = {0,
"no",
"nein",
"não",
"no",
};

static const char *consword[] = {0,
"console",
"console",
"console",
"console",
};

static const char *readyword[] = {0,
"jupiter ready",
"jupiter fertig",
"jupiter ativado",
"jupiter prêt",
};

static const char *setvolword[] = {0,
"set volume",
"set volume",
"determinar volume",
"set volume",
};

static const char *louderword[] = {0,
"louder",
"louder",
"mais alto",
"louder",
};

static const char *softerword[] = {0,
"softer",
"ruhig",
"mais baixo",
"softer",
};

static const char *setrateword[] = {0,
"set rate",
"set rate",
"determinar velocidade",
"set rate",
};

static const char *fasterword[] = {0,
"faster",
"schnell",
"mais rápido",
"faster",
};

static const char *slowerword[] = {0,
"slower",
"langsam",
"mais lento",
"slower",
};

static const char *setpitchword[] = {0,
"set pitch",
"set pitch",
"determinar tom",
"set pitch",
};

static const char *lowerword[] = {0,
"lower",
"lower",
"mais baixo",
"lower",
};

static const char *higherword[] = {0,
"higher",
"higher",
"mais alto",
"higher",
};

static const char *helloword[] = {0,
"hello there",
"guten tag",
"olá",
"bon jour",
};

static const char *reloadword[] = {0,
"reload",
"reload",
"recarregar",
"reload",
};

// derive a command code from its brief name
static int
cmdByName(const char *name)
{
	const char *t;
	const struct cmd *cmdp = &speechcommands[1];

	while(*(t = cmdp->brief)) {
		if(stringEqual(name, t)) return cmdp - speechcommands;
		++cmdp;
}

	return 0;
} // cmdByName

// Return the composite status of an atomic command.
//  1  must end the composite.
//  2  requires follow-on char.
//  4  requires follow-on string -- stands alone.
static unsigned
compStatus(int cmd)
{
	unsigned compstat = 0;
	const struct cmd *cmdp = speechcommands + cmd;
	if(cmdp->endstring) compstat |= 1;
	if(cmdp->nextchar) compstat |= 2;
	/* Follow-on string always ends the composite. */
	if(cmdp->nextline) compstat |= 1;
if(cmd == CMD_BYPASS || cmd == CMD_REEXEC ||
cmd == CMD_RELOAD || cmd == CMD_SUSPEND)
compstat = 5;
	return compstat;
} // compStatus

static int last_cmd_index(const char *list)
{
	int i;
	char cmd;
	const struct cmd *cmdp;
	for(i=0; list[i]; ++i) {
		cmd = list[i];
		cmdp = &speechcommands[cmd];
		if(cmdp->nextchar && list[i+1]) {
			if(!list[i+2]) return i;
			++i;
		}
	}
	return i-1;
} // last_cmd_index

static char *last_atom;
static int cfg_syntax(char *s)
{
char *v = s;
char *s0 = s;
char *t;
char mustend = 0, nextchar = 0;
char c;
int cmd;
struct cmd *cmdp;
unsigned compstat;

	// look up each command designator
	while(c = *s) {
if(c == ' ' || c == '\t') { ++s; continue; }

		if(mustend) return -2;
		t = strpbrk(s, " \t");
		if(t) *t = 0;
		if(nextchar) {
			if(c < 0 || !isalnum(c) || s[1]) return -3;
*v++ = c;
			nextchar = 0;
		} else {
			cmd = cmdByName(s);
last_atom = s;
			if(!cmd) return -4;
			compstat = compStatus(cmd);
			if(compstat&4 && v > s0) return -5;
			if(compstat & 1) mustend = 1;
			if(compstat & 2) nextchar = 1;
*v++ = cmd;
		}

		if(!t) break;
		s = ++t;
	}

*v = 0;
	return 0;
} // cfg_syntax

/* configure the jupiter system. */
static char base_config[] = "/etc/jupiter/setup0.cfg";
static char *my_config = base_config;
static const char *acsdriver = "/dev/acsint";

static void
j_configure(void)
{
FILE *f;
char line[200];
char *s;
int lineno, rc;

f = fopen(my_config, "r");
if(!f) {
fprintf(stderr, openConfig[acs_lang], my_config);
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

if(rc = acs_line_configure(line, cfg_syntax)) {
fprintf(stderr, "%s %s %d: ",
my_config, lineword[acs_lang], lineno);
fprintf(stderr, configError[acs_lang][-rc], last_atom);
fprintf(stderr, "\n");
}
}
fclose(f);
} // j_configure


/*********************************************************************
Set, clear, or toggle a binary mode based on the follow-on character.
Also generate the appropriate sound.
For instance, ^G beep if the character doesn't correspond
to a boolean mode in the system.
*********************************************************************/

static char **argvector;
static char soundsOn = 1; // sounds on, over all */
static char clickTTY = 1; // clicks accompany tty output
static char autoRead = 1; // read new text automatically
static char oneLine; /* read one line at a time */
static char overrideSignals = 0; // don't rely on cts rts etc
static char keyInterrupt;
static char reading; /* continuous reading in progress */
static char goRead; /* read the next sentence */
/* for cut&paste */
#define markleft acs_rb->marks[26]
static unsigned int *markright;
static char screenMode = 0;
static char jdebug;
static char cc_buffer = 0; // control chars in the buffer
static char echoMode; // echo keys as they are typed
static char suspended;
static char suspendClicks;
static char suspendlist[MAX_NR_CONSOLES+1];
/* buffer for cut&paste */
static char cutbuf[10000];

static void binmode(int action, int c, int quiet)
{
	char *p;
static int save_postprocess;

	switch(c) {
	case 'e': p = &echoMode; break;
	case 't': p = &clickTTY; break;
	case 'n': p = &soundsOn; break;
	case 'a': p = &autoRead; break;
	case '1': p = &oneLine; break;
	case 'o': p = &overrideSignals; break;
case 's': markleft = 0; p = &screenMode; break;
	case 'c': p = &cc_buffer; break;
	case 'd': p = &jdebug; break;
	case 'l': p = &tp_readLiteral; break;
case 'i': p = &keyInterrupt; break;
	default: acs_bell(); return;
} // switch

	if(action == 0) *p = 0;
	if(action == 1) *p = 1;
	if(action == 2) *p ^= 1;
	if(!quiet) {
if(soundsOn || c == 'n')
acs_tone_onoff(*p);
else
acs_say_string(*p ? yesword[acs_lang] : noword[acs_lang]);
}

switch(c) {
case 'n':
acs_sounds(*p);
/* If turning sounds on, then the previous tone didn't take. */
if(!quiet && *p) acs_tone_onoff(1);
break;
case 't':
acs_tty_clicks(*p);
break;
case 'o': acs_serial_flow(1-*p); break;
case 's':
acs_screenmode(*p);
/* this line is really important; don't leave the temp cursor in the other world. */
acs_cursorset();
break;
case 'd':
acs_debug = jdebug;
break;
case 'c':
if(*p) save_postprocess = acs_postprocess, acs_postprocess = 0;
else acs_postprocess = save_postprocess;
break;
} // switch
} /* binmode */

/*********************************************************************
An event is interrupting speech.
Key command, echoed character, console switch.
Stop any reading in progress.
*********************************************************************/

static void interrupt(void)
{
reading = 0;
goRead = 0;
if(acs_stillTalking())
acs_shutup();
}

#define readNextMark acs_rb->marks[27]

static void
readNextPart(void)
{
int gsprop;
int i;
unsigned int *end; /* the end of the sentence */
unsigned int first; /* first character of the sentence */
static int flip = 1; /* flip between two ranges of numbers */

acs_refresh(); /* whether we need to or not */

if(readNextMark) {
/* could be past the buffer */
if(readNextMark >= acs_rb->end) {
readNextMark = 0;
reading = 0;
return;
}
acs_rb->cursor = readNextMark;
readNextMark = 0;
}

if(!acs_rb->cursor) {
/* lots of text has pushed the reading cursor off the edge. */
acs_buzz();
reading = 0;
return;
}

gsprop = ACS_GS_REPEAT;
if(oneLine | soundsOn)
gsprop |= ACS_GS_STOPLINE;
else
gsprop |= ACS_GS_NLSPACE;

top:
/* grab something to read */
acs_log("nextpart 0x%x\n", acs_rb->cursor[0]);
tp_in->buf[0] = 0;
tp_in->offset[0] = 0;
acs_getsentence(tp_in->buf+1, 120, tp_in->offset+1, gsprop);

if(!tp_in->buf[1]) {
/* Empty sentence, nothing else to read. */
acs_log("empty done\n");
reading = 0;
return;
}

first = tp_in->buf[1];
if(first == '\n' || first == '\7') {
/* starts out with newline or bell, which is usually associated with a sound */
/* This will swoop/beep with clicks on, or say the word newline or bell with clicks off */
speakChar(tp_in->buf[1], 1, soundsOn, 0);

if(oneLine && first == '\n') {
acs_log("newline done\n");
reading = 0;
return;
}

/* Find the next token/offset, which should be the next character */
for(i=2; !tp_in->offset[i]; ++i)  ;
acs_rb->cursor += tp_in->offset[i];
/* but don't leave it there if you have run off the end of the buffer */
if(acs_rb->cursor >= acs_rb->end) {
acs_rb->cursor = acs_rb->end-1;
acs_log("eof done\n");
reading = 0;
return;
}

/* The following line is bad if there are ten thousand bells, no way to interrupt */
/* I'll deal with that case later. */
goto top;
}

if(jdebug) {
char *w = acs_uni2utf8(tp_in->buf+1);
if(w) {
acs_log("insentence %s\n", w);
free(w);
#if 0
/* show offsets as returned by getsentence() */
tp_in->len = 1 + acs_unilen(tp_in->buf+1);
for(i=1; i<=tp_in->len; ++i)
if(tp_in->offset[i])
acs_log("%d=%d\n", i, tp_in->offset[i]);
#endif
}
}

tp_in->len = 1 + acs_unilen(tp_in->buf+1);
/* If the sentence runs all the way to the end of the buffer,
 * then we might be in the middle of printing a word.
 * We don't want to read half the word, then come back and refresh
 * and read the other half.  So back up to the beginning of the word.
 * Nor do we want to hear the word return, when newline is about to follow.
 * Despite this code, it is still possible to hear part of a word,
 * or the cr in crlf, if the output is delayed for some reason. */
end = tp_in->buf + tp_in->len - 1;
if(*end == '\r') {
if(tp_in->len > 2 && tp_in->offset[tp_in->len-1])
*end = 0, --tp_in->len;
} else if(acs_isalnum(*end)) {
for(--end; *end; --end)
if(!acs_isalnum(*end)) break;
if(*end++ && tp_in->offset[end-tp_in->buf]) {
*end = 0;
tp_in->len = end - tp_in->buf;
}
}

prepTTS();

/* Cut the text at a logical sentence, as indicated by newline.
 * If newline wasn't already present in the input, this has been
 * set for you by prepTTS. */
for(end=tp_out->buf+1; *end; ++end)
if(*end == '\n' || *end == '\7') break;
*end = 0;
tp_out->len = end - tp_out->buf;

/* An artificial newline, inserted by prepTTS to denote a sentence boundary,
 * won't have an offset.  In that case we need to grab the next one. */
i = tp_out->len;
while(!tp_out->offset[i]) ++i;
tp_out->offset[tp_out->len] = tp_out->offset[i];

readNextMark = acs_rb->cursor + tp_out->offset[tp_out->len];
//flip = 51 - flip;
acs_say_indexed(tp_out->buf+1, tp_out->offset+1, flip);
} /* readNextPart */

/* index mark handler, read next sentence if we finished the last one */
static void imark_h(int mark, int lastmark)
{
/* Not sure how we would get here if we weren't reading, but just in case */
if(!reading) return;

if(mark == lastmark)
readNextPart();
}


/*********************************************************************
Dump the tty buffer into a file.
But first we need to convert it from unicode to utf8.
The conversion routine is part of the acsint bridge layer.
*********************************************************************/

static int dumpBuffer(void)
{
int fd, l, n;
char *utf8 =  acs_uni2utf8(acs_rb->start);
if(!utf8) return -1;
sprintf(shortPhrase, "/tmp/buf%d", acs_fgc);
fd = open(shortPhrase, O_WRONLY|O_CREAT|O_TRUNC, 0666);
if(fd < 0) {
free(utf8);
return -1;
}
l = strlen(utf8);
n = write(fd, utf8, l);
close(fd);
free(utf8);
return (n < l ? -1 : 0);
} /* dumpBuffer */


/*********************************************************************
Suspend or unsuspend the adapter.
This is invoked by a speech command,
or called when you switch consoles.
*********************************************************************/

static void suspend(void)
{
static const char suspendCommand[] = { CMD_SUSPEND, 0};
acs_suspendkeys(suspendCommand);
reading = 0;
suspended = 1;
suspendClicks = soundsOn;
if(soundsOn) {
soundsOn = 0;
acs_sounds(0);
}
} /* suspend */

static void unsuspend(void)
{
acs_resumekeys();
if(suspendClicks) {
soundsOn = 1;
acs_sounds(1);
}
suspended = 0;
} /* unsuspend */


static void
testStepFunction(void)
{
FILE *f;
int f1, f2, step, duration;
f = fopen("/tmp/step", "r");
if(!f) return;
fscanf(f, "%d,%d,%d,%d",
&f1, &f2, &step, &duration);
fclose(f);
acs_scale(f1, f2, step, duration);
} /* testStepFunction */


/*********************************************************************
Execute the speech command.
The argument is the command list, null-terminated.
*********************************************************************/

static void runSpeechCommand(int input, const char *cmdlist)
{
	const struct cmd *cmdp;
	char suptext[256]; /* supporting text */
static 	char lasttext[256]; /* supporting text */
	char support; /* supporting character */
	int i, n;
	int asword, quiet, rc, gsprop;
	unsigned int c;
	char cmd;
const char *t;
char *cut8;

interrupt();

acs_cursorset();

top:
	cmd = *cmdlist;
if(cmd) ++cmdlist;
	cmdp = &speechcommands[cmd];
asword = 0;

if(suspended && cmd != CMD_SUSPEND)
return;

	/* some comands are meaningless when the buffer is empty */
	if(cmdp->nonempty) {
if(!acs_rb->cursor &&
cmd != 3 && cmd != 4 && cmd != 39 && cmd != 44)
goto error_buzz;
if(acs_rb->end == acs_rb->start) goto error_bound;
}

	support = 0;
	if(cmdp->nextchar) {
		if(*cmdlist) support = *cmdlist++;
else{
acs_click();
if(acs_get1char(&support)) goto error_bell;
}
	}

suptext[0] = 0;
if(cmdp->nextline) {
acs_tone_onoff(0);
if(acs_keystring(suptext, sizeof(suptext), ACS_KS_DEFAULT)) return;
}

quiet = ((!input)|*cmdlist);

	/* perform the requested action */
	switch(cmd) {
	case 0: /* command string is finished */
		acs_cursorsync();
if(!quiet) acs_click();
		return;

	case 1:
acs_clearbuf();
markleft = 0;
if(!quiet) acs_tone_onoff(0);
break;

	case 2: /* locate visual cursor */
		if(!screenMode) goto error_bell;
acs_rb->cursor = acs_rb->v_cursor;
acs_cursorset();
		break;

	case 3: acs_startbuf(); break;

	case 4: acs_endbuf(); break;

	case 5: acs_startline(); break;

	case 6: acs_endline(); break;

	case 7: acs_startword(); break;

	case 8: acs_endword(); break;

	case 9: acs_lspc(); break;

	case 10: acs_rspc(); break;

	case 11: if(!acs_back()) goto error_bound; break;

	case 12: if(!acs_forward()) goto error_bound; break;

	case 13: /* up a row */
		n = acs_startline();
		if(!acs_back()) goto error_bound;
		acs_startline();
		for(i=1; i<n; ++i) {
			if(acs_getc() == '\n') goto error_bell;
acs_forward();
		}
		break;

	case 14: /* down a row */
		n = acs_startline();
acs_endline();
		if(!acs_forward()) goto error_bound;
		for(i=1; i<n; ++i) {
			if(acs_getc() == '\n') goto error_bell;
			if(!acs_forward()) goto error_bound;
		}
		break;

/* read character, or cap character, or word for character */
	case 15: asword = 2; goto letter;
case 17: asword = 1; /* fall through */
	case 16:
letter:
acs_cursorsync();
		speakChar(acs_getc(), 1, soundsOn, asword);
		break;

	case 18: /* read column number */
if(!quiet) acs_click();
		acs_cursorsync();
		n = acs_startline();
		sprintf(shortPhrase, "%d", n);
		acs_say_string_uc(prepTTSmsg(shortPhrase));
		return;

	case 19: /* just read one word */
acs_cursorsync();
c = acs_getc();
if(c <= ' ') goto letter;
acs_startword();
acs_cursorsync();
gsprop = ACS_GS_STOPLINE | ACS_GS_REPEAT | ACS_GS_ONEWORD;
tp_in->buf[0] = 0;
tp_in->offset[0] = 0;
acs_getsentence(tp_in->buf+1, WORDLEN, tp_in->offset+1, gsprop);
		tp_in->len = acs_unilen(tp_in->buf+1) + 1;
acs_rb->cursor += tp_in->offset[tp_in->len] - 1;
acs_cursorset();
tp_oneSymbol = 1;
prepTTS();
tp_oneSymbol = 0;
		acs_say_string_uc(tp_out->buf+1);
break;

	case 20: /* start continuous reading */
if(!quiet) acs_click();
startread:
		/* We always start reading at the beginning of a word */
acs_startword();
		acs_cursorsync();
reading = 1;
/* start at the cursor, not at some leftover nextMark */
readNextMark = 0;
readNextPart();
		return; /* has to be the end of the composite */

	case 21: acs_shutup(); break;

	case 22: acs_bypass(); break;

	/* clear, set, and toggle binary modes */
	case 23: binmode(0, support, quiet); break;
	case 24: binmode(1, support, quiet); break;
	case 25: binmode(2, support, quiet); break;

	case 26: asword = 1; /* search backwards */
	case 27: /* search forward */
		if(*suptext)
			strcpy(lasttext, suptext);
else strcpy(suptext, lasttext);
		if(!*suptext) goto error_bell;
		if(!acs_bufsearch(suptext, asword, oneLine)) goto error_bound;
		acs_cursorsync();
if(!quiet) acs_cr();
		if(!oneLine) {
			acs_say_string("o k");
acs_rb->cursor -= (strlen(suptext)-1);
			return;
		}
		/* start reading at the beginning of this line */
acs_startline();
		goto startread;

	case 28: /* volume */
		if(!isdigit(support)) goto error_bell;
rc = acs_setvolume(support-'0');
t = setvolword[acs_lang];
speechparam:
if(rc == -1) goto error_bound;
if(rc == -2) goto error_bell;
		if(quiet) break;
		acs_say_string(t);
		break;

	case 29: /* inc volume */
rc = acs_incvolume();
t = louderword[acs_lang];
goto speechparam;

	case 30: /* dec volume */
rc = acs_decvolume();
t = softerword[acs_lang];
goto speechparam;

	case 31: /* speed */
		if(!isdigit(support)) goto error_bell;
rc = acs_setspeed(support-'0');
t = setrateword[acs_lang];
goto speechparam;

	case 32: /* inc speed */
rc = acs_incspeed();
t = fasterword[acs_lang];
goto speechparam;

	case 33: /* dec speed */
rc = acs_decspeed();
t = slowerword[acs_lang];
goto speechparam;

	case 34: /* pitch */
		if(!isdigit(support)) goto error_bell;
rc = acs_setpitch(support-'0');
t = setpitchword[acs_lang];
goto speechparam;

	case 35: /* inc pitch */
rc = acs_incpitch();
t = higherword[acs_lang];
goto speechparam;

	case 36: /* dec pitch */
rc = acs_decpitch();
t = lowerword[acs_lang];
goto speechparam;

	case 37: /* set voice */
		if(support < '0' || support > '9') goto error_bell;
		rc = acs_setvoice(support-'0');
t = helloword[acs_lang];
goto speechparam;

	case 38: /* key binding */
if(acs_line_configure(suptext, cfg_syntax))
goto error_bell;
if(!quiet) acs_cr();
		return;

	case 39: /* last complete line */
acs_endbuf();
acs_cursorsync();
		if(screenMode) acs_back();
	while(1) {
		c = acs_getc();
		if(c == '\n') asword = 1;
		if(!acs_isspace(c) && asword) break;
		if(!acs_back()) goto error_bound;
	}
	break;

case 40: /* mark left */
if(!input) goto error_bell;
acs_cursorsync();
markleft = acs_rb->cursor;
if(!quiet) acs_tone_onoff(0);
break;

case 41: /* mark right */
if(!input) goto error_bell;
if(support < 'a' || support > 'z') goto error_bell;
if(!markleft) goto error_bell;
n = 0;
cutbuf[n++] = '@';
cutbuf[n++] = support;
cutbuf[n] = 0;
acs_line_configure(cutbuf, 0);
cutbuf[n++] = '<';
acs_cursorsync();
markright = acs_rb->cursor;
if(markright < markleft) goto error_bound;
++markright;
i = markright - markleft;
if(i + n >= sizeof(cutbuf)) goto error_bound;
c = *markright; // save this value
*markright = 0;
cut8 = acs_uni2utf8(markleft);
*markright = c; // put it back
if(!cut8) goto error_bell;
if(n + strlen(cut8) >= sizeof(cutbuf)) { free(cut8); goto error_bound; }
strcpy(cutbuf + n, cut8);
free(cut8);
/* Stash it in the macro */
if(acs_line_configure(cutbuf, cfg_syntax) < 0)
goto error_bell;
markleft = 0;
if(!quiet) acs_tone_onoff(0);
return;

	case 42: /* set echo */
		if(support < '0' || support > '4') goto error_bell;
echoMode = support - '0';
		if(input && !*cmdlist) {
static const char * const echoWords[] = { "off", "letters", "words", "letters pause", "words pause"};
acs_say_string(echoWords[echoMode]);
}
		break;

case 43: /* set a marker in the tty buffer */
if(support < 'a' || support > 'z') goto error_bell;
acs_cursorsync();
if(!acs_rb->cursor) goto error_bell;
acs_rb->marks[support-'a'] = acs_rb->cursor;
if(!quiet) acs_tone_onoff(0);
break;

case 44: /* jump to a preset marker */
if(support < 'a' || support > 'z') goto error_bell;
if(!acs_rb->marks[support-'a']) goto error_bell;
acs_rb->cursor = acs_rb->marks[support-'a'];
acs_cursorset();
if(!quiet) acs_tone_onoff(0);
break;

case 45: /* reexec */
acs_buzz();
acs_sy_close();
acs_close();
usleep(700000);
/* We should really capture the absolute path of the running program,
 * and feed it to execv.  Not sure how to do that,
 * so I'm just using execvp instead.
 * Hope it gloms onto the correct executable.
 * Best to create a symblink from /usr/local/bin/jupiter to a safe executable. */
execvp("jupiter", argvector);
/* should never get here */
puts("\7\7\7");
exit(1);

case 46: /* reload config file */
base_config[18] = support;
if(access(base_config, 4)) goto error_bell;
acs_cr();
acs_reset_configure();
acs_say_string(reloadword[acs_lang]);
j_configure();
return;

case 47: /* dump tty buffer to a file */
if(dumpBuffer()) goto error_bell;
acs_cr();
sprintf(shortPhrase, "%s %d",
bufword[acs_lang], acs_fgc);
		acs_say_string_uc(prepTTSmsg(shortPhrase));
return;

case 48: /* suspend */
if(suspended) {
static const short unsuspendNotes[] = {
500, 10, 530, 10, 560, 10, 0};
unsuspend();
acs_notes(unsuspendNotes);
} else {
static const short suspendNotes[] = {
560, 10, 530, 10, 500, 10, 0};
acs_notes(suspendNotes);
suspend();
}
suspendlist[acs_fgc] = suspended;
return;

case 49:
testStepFunction();
return;

	default:
	error_bell:
		acs_bell();
		return;

	error_buzz:
		acs_buzz();
		return;

	error_bound:
acs_highbeeps();
		return;
	} /* end switch on function */

	goto top; /* next command */
} /* runSpeechCommand */

/* keystroke handler */
static int last_key, last_ss;
static void key_h(int key, int ss, int leds)
{
last_key = key;
last_ss = ss;
} /* key_h */

/* Remember the last console, and speak a message if it changes */
static int last_fgc;
static void fgc_h(void)
{
if(!last_fgc) { /* first time */
last_fgc = acs_fgc;
return;
}

if(last_fgc == acs_fgc)
return; /* should never happen */

last_fgc = acs_fgc;
/* kill any pending keystroke command; we just switched consoles */
last_key = last_ss = 0;

/* stop reading, and speak the new console */
interrupt();
sprintf(shortPhrase, "%s %d", consword[acs_lang], acs_fgc);
		acs_say_string_uc(prepTTSmsg(shortPhrase));

if(suspended && !suspendlist[acs_fgc])
unsuspend();
if(!suspended && suspendlist[acs_fgc])
suspend();
} /* fgc_h */

/* fifo input still works, even if suspended */
static void fifo_h(char *msg)
{
/* stop reading, and speak the message */
interrupt();
acs_say_string(msg);
} /* fifo_h */

static void more_h(int echo, unsigned int c)
{
if(suspended) return;

if (keyInterrupt && echo == 1) {
/* In this case we want to shutup, whether the unit
 * is in reading/indexed mode or not. */
acs_shutup();
reading = 0;
goRead = 0;
}
if(echoMode && echo == 1 && c < 256 && isprint(c)) {
interrupt();
speakChar(c, 1, soundsOn, 0);
}

if(reading) return;
if(!autoRead) return;
/* autoRead doesn't work in screen mode anyways */
if(screenMode) return;
if(echo) return;

goRead = 1;
} /* more_h */

static void
openSound(void)
{
static const short startNotes[] = {
		476,5,
530,5,
596,5,
662,5,
762,5,
858,5,
942,5,
0,0};
acs_notes(startNotes);
} /* openSound */

static void
testTTS(void)
{
char line[400];
char *out;

/* This doesn't go through the normal acs_open() process */
acs_reset_configure();
/* key bindings don't matter here, but let's load our pronunciations */
j_configure();

while(fgets(line, sizeof(line), stdin)) {
out = acs_uni2utf8(prepTTSmsg(line));
printf("%s", out);
free(out);
}

exit(0);
} /* testTTS */

static void
selectLanguage(void)
{
    char buf[8];
    char *s = getenv("LANG");

acs_lang = ACS_LANG_EN; // default

    if(!s)
	return;
    if(!*s)
	return;

    strncpy(buf, s, 7);
    buf[7] = 0;
for(s=buf; *s; ++s) {
if(*s >= 'A' && *s <= 'Z')
*s |= 0x20;
}

    if(!strncmp(buf, "en", 2))
	return;			/* english is default */

    if(!strncmp(buf, "de", 2)) {
acs_lang = ACS_LANG_DE;
	return;
    }

    if(!strncmp(buf, "pt", 2)) {
acs_lang = ACS_LANG_PT;
	return;
    }

    fprintf(stderr, "Sorry, language %s is not implemented\n", buf);
}				/* selectLanguage */
/* Supported synthesizers */
struct synth {
const char *name;
int style;
const char *initstring;
} synths[] = {
{"dbe", ACS_SY_STYLE_DOUBLE,
"\1@ \0012b \00126g \0012o \00194i "},
{"dte", ACS_SY_STYLE_DECEXP},
{"dtp", ACS_SY_STYLE_DECPC},
{"bns", ACS_SY_STYLE_BNS},
{"ace", ACS_SY_STYLE_ACE},
{"esp", ACS_SY_STYLE_ESPEAKUP},
{0, 0}};

int
main(int argc, char **argv)
{
int i, port;
char serialdev[20];
char *cmd = NULL;

/* remember the arg vector, before we start marching along. */
argvector = argv;
++argv, --argc;

selectLanguage();

if(setupTTS()) {
fprintf(stderr, "Could not malloc space for text preprocessing buffers.\n");
exit(1);
}

while(argc) {
if(argc && stringEqual(argv[0], "-d")) {
/* it should be safe to chdir, but not to close std{in,out,err}. Reload prints stuff there. */
daemon(0, 1);
/* make this the leader of its process group,
 * so the child process, a software synth, dies when it does. */
setsid();
++argv, --argc;
continue;
}

if(argc && stringEqual(argv[0], "-a")) {
tp_alnumPrep = 1;
++argv, --argc;
continue;
}

if(argc && stringEqual(argv[0], "-c")) {
++argv, --argc;
if(argc) {
my_config = argv[0];
++argv, --argc;
}
continue;
}

break;
}

if(argc && stringEqual(argv[0], "tts")) {
tp_readLiteral = 0;
testTTS();
return 0;
}

if(argc && stringEqual(argv[0], "ltts")) {
tp_readLiteral = 1;
testTTS();
return 0;
}

if(argc && stringEqual(argv[0], "tc")) {
j_configure();
return 0;
}

if(argc != 2) usage();
for(i=0; synths[i].name; ++i)
if(stringEqual(synths[i].name, argv[0])) break;
if(!synths[i].name) usage();
acs_style = synths[i].style;
acs_style_defaults();
++argv, --argc;

if (*argv[0] == '|') {
cmd = argv[0]+1;
} else {
port = atoi(argv[0]);
if(port < 0 || port > 3) usage();
sprintf(serialdev, "/dev/ttyS%d", port);
}

/* Compare major minor numbers on acsdriver with what we see
 * in /sys.  If it's wrong, and we are root, fix it up.
 * This is linux only. */
acs_nodecheck(acsdriver);

if(acs_open(acsdriver) < 0) {
fprintf(stderr, openDriver[acs_lang],
acsdriver, strerror(errno));
if(errno == EBUSY) {
fprintf(stderr, busyDriver[acs_lang]);
exit(1);
}
if(errno == EACCES) {
fprintf(stderr, permDriver[acs_lang], acsdriver);
exit(1);
}
fprintf(stderr, makeDriver[acs_lang], acsdriver);
exit(1);
}

acs_key_h = key_h;
acs_fgc_h = fgc_h;
acs_more_h = more_h;
acs_fifo_h = fifo_h;
acs_imark_h = imark_h;

// this has to run after the device is open,
// because it sends "key capture" commands to the acsint driver
j_configure();

if (cmd && acs_pipe_system(cmd) == -1) {
fprintf(stderr, execSoft[acs_lang], cmd);
exit(1);
}

if(!cmd && acs_serial_open(serialdev, 9600)) {
fprintf(stderr, openSerial[acs_lang], serialdev);
exit(1);
}

openSound();

/* Initialize the synthesizer. */
if(synths[i].initstring)
acs_say_string(synths[i].initstring);

/* Adjust rate and voice, then greet. */
acs_setvoice(4);
acs_setspeed(9);
acs_say_string(readyword[acs_lang]);

acs_startfifo("/etc/jupiter.fifo");

/* I have a low usage machine, so a small gap in output
 * usually means something new to read.  Set it at 0.4 seconds. */
acs_obreak(4);

/* This is the same as the default, but I set it here for clarity. */
acs_postprocess = ACS_PP_CTRL_H | ACS_PP_CRLF |
ACS_PP_CTRL_OTHER | ACS_PP_ESCB;

/* This runs forever, you have to hit interrupt to kill it,
 * or kill it from another console. */
while(1) {
acs_all_events();

key_command:
if(last_key) {
char *cmdlist; // the speech command
int mkcode = acs_build_mkcode(last_key, last_ss);
last_key = last_ss = 0;
cmdlist = acs_getspeechcommand(mkcode);
//There ought to be a speech command, else why were we called?
if(cmdlist) runSpeechCommand(1, cmdlist);
}

if(goRead) {
unsigned int c;
goRead = 0;
/* fetch the new stuff and start reading */
/* Pause, to allow for some characters to print, especially if clicks are on. */
usleep((soundsOn && clickTTY) ? 250000 : 25000);
readNextMark = acs_rb->end;
/* The refresh is really a call to events() in disguise.
 * So any of those handlers could be called.
 * Turn reading on, so the more_h handler doesn't cause any trouble. */
reading = 1;
acs_refresh();
/* did a keycommand sneak in? */
if(last_key) goto key_command;
/* did reading get killed for any other reason, e.g. console switch? */
if(!reading) continue;
/* now turn it off; we'll turn it on later if it's a go */
reading = 0;
if(!readNextMark) continue;
while(c = *readNextMark) {
if(c != ' ' && c != '\n' &&
c != '\r' && c != '\7')
break;
++readNextMark;
}
if(!c) continue;
reading = 1;
readNextPart();
}

}

acs_close();
} // main

