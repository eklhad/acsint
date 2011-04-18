/* acsbind.c: bind speech commands and macros to keys */
/* Also manage punctuation pronunciations and word replacements. */

#include <string.h>
#include <malloc.h>

#include "acsbridge.h"

#define stringEqual !strcmp


/* Turn a key code and a shift state into a modified key number. */

#define MK_BLOCK 60
#define MK_OFFSET 55

extern const char lowercode[], uppercode[];

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
} /* acs_build_mkcode */

/* Match two strings, n characters, case insensitive. */
/* This is pure ascii. */
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
} /* lettermatch_ci */

/* Build a modified key code from an ascii string. */
/* Remember the encoded key and state; needed by line_configure below. */
static int key1key, key1ss;
int acs_ascii2mkcode(const char *s, char **endptr)
{
int ss = 0;
int key;
unsigned char c;
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

if((s[0] == 'f' || s[0] == 'F') && isdigit((unsigned char)s[1])) {
++s;
key = 0;
c = (unsigned char)s[0];
while(isdigit(c)) {
key = 10*key + c - '0';
++s;
c = (unsigned char)s[0];
}
if(key <= 0 || key > 12) goto error;
// the real keycode
if(key <= 10) key += KEY_F1-1;
else key += KEY_F11 - 11;
goto done;
}

if(s[0] == '#' && s[1] && strchr("*+.-/0123456789", s[1])) {
c = (unsigned char) *++s;
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

c = (unsigned char) s[0] | 0x20;
if(c < 'a' || c > 'z') goto error;
key = lettercode[c-'a'];
++s;
c = (unsigned char) s[0] | 0x20;
if(c >= 'a' && c <= 'z') goto error;

done:
if(endptr) *endptr = (char*)s;
// save these for line_configure
key1key = key;
if(!ss) ss = ACS_SS_PLAIN;
key1ss = ss;
return acs_build_mkcode(key, ss);

error:
return -1;
} /* acs_ascii2mkcode */

static char *macrolist[MK_BLOCK*8];
static char *speechcommandlist[MK_BLOCK*8];

void acs_clearmacro(int mkcode)
{
if(macrolist[mkcode]) free(macrolist[mkcode]);
macrolist[mkcode] = 0;
} /* acs_clearmacro */

char *acs_getmacro(int mkcode)
{
return macrolist[mkcode];
} /* acs_getmacro */

void acs_setmacro(int mkcode, const char *s)
{
acs_clearmacro(mkcode);
acs_clearspeechcommand(mkcode);
if(!s) return;
macrolist[mkcode] = malloc(strlen(s) + 1);
strcpy(macrolist[mkcode], s);
} /* acs_setmacro */

void acs_clearspeechcommand(int mkcode)
{
if(speechcommandlist[mkcode]) free(speechcommandlist[mkcode]);
speechcommandlist[mkcode] = 0;
} /* acs_clearspeechcommand */

char *acs_getspeechcommand(int mkcode)
{
return speechcommandlist[mkcode];
} /* acs_getspeechcommand */

void acs_setspeechcommand(int mkcode, const char *s)
{
acs_clearmacro(mkcode);
acs_clearspeechcommand(mkcode);
if(!s) return;
speechcommandlist[mkcode] = malloc(strlen(s) + 1);
strcpy(speechcommandlist[mkcode], s);
} /* acs_setspeechcommand */

static char *punclist[65536];

static const char *firstpunclist[256] = {
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
"degrees", "plus minus", "squared", "cubed", "acute", "micro", "pilcrow", "bullet",
"cedilla", "to the first", "masculine", "forward", "one fourth", "one half", "three fourths", "question up",
0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, "times",
0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, "divided by",
0, 0, 0, 0, 0, 0, 0, 0,
};

void acs_clearpunc(unsigned int c)
{
if(c > 0xffff) return;
if(punclist[c]) free(punclist[c]);
punclist[c] = 0;
} /* acs_clearpunc */

char *acs_getpunc(unsigned int c)
{
if(c > 0xffff) return 0;
return punclist[c];
} /* acs_getpunc */

void acs_setpunc(unsigned int c, const char *s)
{
if(c > 0xffff) return;
acs_clearpunc(c);
if(!s) return;
punclist[c] = malloc(strlen(s) + 1);
strcpy(punclist[c], s);
} /* acs_setpunc */

/* The replacement dictionary is all iso, no unicodes. */
/* You might get away with n tilde, but nothing beyond a byte. */

static char *dict1[NUMDICTWORDS];
static char *dict2[NUMDICTWORDS];
static int numdictwords;
static char lowerdict[WORDLEN+1];

static int lowerword(const char *w)
{
char c;
int i;
for(i=0; (c = *w); ++i, ++w) {
if(i == WORDLEN) return -1;
if(!isalpha((unsigned char)c)) return -1;
lowerdict[i] = tolower(c);
}
lowerdict[i] = 0;
return 0;
} /* lowerword */

static int
inDictionary(void)
{
int i;
for(i=0; i<numdictwords; ++i)
if(stringEqual(lowerdict, dict1[i])) return i;
return -1;
} /* inDictionary */

int acs_setword(const char *word1, const char *word2)
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
} /* acs_setword */

char *acs_replace(const char *word1)
{
int j;
if(lowerword(word1)) return 0;
j = inDictionary();
if(j < 0) return 0;
return dict2[j];
} /* acs_replace */

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
} /* isvowel */

static char rootword[WORDLEN+8];

/* Twelve regular English suffixes. */
static const char suftab[] = "s   es  ies ing ing ing d   ed  ed  ied 's  'll ";

/* Which suffixes drop e or y when appended? */
static const char sufdrop[] = "  y  e   y  ";

/* Which suffixes double the final consonent, as in dropped. */
static const char sufdouble[] = {
	0,0,0,0,1,0,0,1,0,0,0,0};

/* extract the root word */
static int mkroot(void)
{
	char l0, l1, l2, l3, l4; /* trailing letters */
	short wdlen, l;

strcpy(rootword, lowerdict);
	wdlen = strlen(rootword);
	l = wdlen - 5;
	if(l < 0) return 0; // word too short to safely rootinize
	l4 = rootword[l+4];
	l3 = rootword[l+3];
	l2 = rootword[l+2];
	l1 = rootword[l+1];
	l0 = rootword[l+0];

	if(l4 == 's') { // possible plural
		if(strchr("siau", l3)) return 0;
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
			if(strchr("shz", l2)) {
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
} /* mkroot */

/* reconstruct the word based upon its root and the removed suffix */
static void reconst(int root)
{
	char *t;
	short i, wdlen;
	char c;

	if(!root) return; /* nothing to do */

	--root;
	wdlen = strlen(rootword);
	t = rootword + wdlen-1;
	if(sufdouble[root]) c = *t, *++t = c;
	if(sufdrop[root] == *t) --t;
	for(i=4*root; i<4*root+4; ++i) {
		c = suftab[i];
		if(c == ' ') break;
		*++t = c;
	}
	*++t = 0;
} /* reconst */

char *acs_smartreplace(const char *s)
{
int i, root;
char *t;
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
			if(!isalpha((unsigned char)t[i])) return 0;
strcpy(rootword, t);
		reconst(root);
return rootword;
} /* acs_smartreplace */

/* Remember which keys are being intercepted by the driver. */
static int keycapture[ACS_NUM_KEYS];

static void skipWhite(char **t)
{
char *s = *t;
while(*s == ' ' || *s == '\t') ++s;
*t = s;
} /* skipWhite */

/* Process a line from a config file, performs any of the above functions */
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
if(isdigit((unsigned char)c)) return -1;

t = strpbrk(s, " \t");
if(t) { save = *t; *t = 0; }

if(isalpha((unsigned char)c)) {
if(!t) return acs_setword(s, 0);
u = t;
skipWhite(&u);
rc = acs_setword(s, u);
*t = save;
return rc;
}

// punctuation
// cannot leave it with no pronunciation
if(!t) return -1;
*t = save;
skipWhite(&t);
if(!*t) return -1;
acs_setpunc(c, t);
return 0;
} /* acs_line_configure */

/* Go back to the default configuration. */
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

for(; i<65536; ++i)
acs_clearpunc(i);

for(i=0; i<MK_BLOCK*8; ++i) {
acs_clearmacro(i);
acs_clearspeechcommand(i);
}
} /* acs_reset_configure */

