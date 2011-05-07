/*********************************************************************
File: acsbind.c
Description: bind speech commands and macros to keys.
Also manage punctuation pronunciations and word replacements.
*********************************************************************/

#include <string.h>
#include <malloc.h>

#include "acsbridge.h"

#define stringEqual !strcmp


/* Turn a key code and a shift state into a modified key number. */

#define MK_RANGE (ACS_NUM_KEYS * 16)

int acs_build_mkcode(int key, int ss)
{
if((unsigned)key >= ACS_NUM_KEYS) return -1;
if(ss & ~0xf) return -1;
return ss * ACS_NUM_KEYS + key;
} /* acs_build_mkcode */

/* Match two strings, n characters, case insensitive. */
/* This is pure ascii. */
static int wordmatch_ci(const char *s, const char *t, int n)
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
} /* wordmatch_ci */

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

/* We talk about keywords all the time, but these really are words for keys. */
static struct keyword {
const char *name;
int keycode;
} keywords[] = {
{"up", KEY_UP},
{"left", KEY_LEFT},
{"right", KEY_RIGHT},
{"down", KEY_DOWN},
{"home", KEY_HOME},
{"scroll", KEY_SCROLLLOCK},
{"pause", KEY_PAUSE},
{"sysrq", KEY_SYSRQ},
{0, 0}};
struct keyword *kw;

while(1) {
if(s[0] == '+') { ss |= ACS_SS_SHIFT; ++s; continue; }
if(s[0] == '^') { ss|= ACS_SS_CTRL; ++s; continue; }
if(s[0] == '@') { ss|= ACS_SS_ALT; ++s; continue; }
if((s[0] == 'l' || s[0] == 'L') && s[1] == '@') { ss|= ACS_SS_LALT; s += 2; continue; }
if((s[0] == 'r' || s[0] == 'R') && s[1] == '@') { ss|= ACS_SS_RALT; s += 2; continue; }
break;
}

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

for(kw=keywords; kw->name; ++kw) {
int l = strlen(kw->name);
if(!wordmatch_ci(s, kw->name, l)) continue;
key = kw->keycode;
s += l;
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
key1ss = ss;
return acs_build_mkcode(key, ss);

error:
return -1;
} /* acs_ascii2mkcode */

static char *macrolist[MK_RANGE];
static char *speechcommandlist[MK_RANGE];

void acs_clearmacro(int mkcode)
{
if(mkcode < 0) return;
if(macrolist[mkcode]) free(macrolist[mkcode]);
macrolist[mkcode] = 0;
} /* acs_clearmacro */

char *acs_getmacro(int mkcode)
{
if(mkcode < 0) return 0;
return macrolist[mkcode];
} /* acs_getmacro */

void acs_setmacro(int mkcode, const char *s)
{
if(mkcode < 0) return;
acs_clearmacro(mkcode);
acs_clearspeechcommand(mkcode);
if(!s) return;
macrolist[mkcode] = malloc(strlen(s) + 1);
strcpy(macrolist[mkcode], s);
} /* acs_setmacro */

void acs_clearspeechcommand(int mkcode)
{
if(mkcode < 0) return;
if(speechcommandlist[mkcode]) free(speechcommandlist[mkcode]);
speechcommandlist[mkcode] = 0;
} /* acs_clearspeechcommand */

char *acs_getspeechcommand(int mkcode)
{
if(mkcode < 0) return 0;
return speechcommandlist[mkcode];
} /* acs_getspeechcommand */

void acs_setspeechcommand(int mkcode, const char *s)
{
if(mkcode < 0) return;
acs_clearmacro(mkcode);
acs_clearspeechcommand(mkcode);
if(!s) return;
speechcommandlist[mkcode] = malloc(strlen(s) + 1);
strcpy(speechcommandlist[mkcode], s);
} /* acs_setspeechcommand */

static char *punclist[65536];

struct uc_name {
unsigned int unicode;
const char *name;
};

static const struct uc_name english_uc[] = {
{7, "bell"},
{8, "backspace"},
{9, "tab"},
{10, "newline"},
{12, "formfeed"},
{13, "return"},
{27, "escape"},
{' ', "space"},
{'!', "bang"},
{'"', "quote"},
{'#', "pound"},
{'$', "doller"},
{'%', "percent"},
{'&', "and"},
{'\'', "apostrophe"},
{'(', "left paren"},
{')', "right paren"},
{'*', "star"},
{'+', "plus"},
{',', "comma"},
{'-', "dash"},
{'.', "period"},
{'/', "slash"},
{':', "colen"},
{';', "semmycolen"},
{'<', "less than"},
{'=', "eequals"},
{'>', "greater than"},
{'?', "question mark"},
{'@', "at sign"},
{'[', "left bracket"},
{'\\', "backslash"},
{']', "right bracket"},
{'^', "up airow"},
{'_', "underscore"},
{'`', "backquote"},
{'{', "left brace"},
{'|', "pipe"},
{'}', "right brace"},
{'~', "tilde"},
{0x7f, "delete"},
{0xa0, "break space"},
{0xa1, "bang up"},
{0xa2, "cents"},
{0xa3, "pounds"},
{0xa4, "currency"},
{0xa5, "yen"},
{0xa6, "broken bar"},
{0xa7, "section"},
{0xa8, "diaeresis"},
{0xa9, "copyright"},
{0xaa, "feminine"},
{0xab, "left airow"},
{0xac, "not"},
{0xad, "soft hyphen"},
{0xae, "registered"},
{0xaf, "macron"},
{0xb0, "degrees"},
{0xb1, "plus minus"},
{0xb2, "squared"},
{0xb3, "cubed"},
{0xb4, "acute"},
{0xb5, "micro"},
{0xb6, "pilcrow"},
{0xb7, "bullet"},
{0xb8, "cedilla"},
{0xb9, "to the first"},
{0xba, "masculine"},
{0xbb, "right airow"},
{0xbc, "one fourth"},
{0xbd, "one half"},
{0xbe, "three fourths"},
{0xbf, "question up"},
{0xd7, "times"},
{0xf7, "divided by"},
{0x113, "backquote"},
{0x391, "Alpha"},
{0x392, "Beta"},
{0x393, "Gamma"},
{0x394, "Delta"},
{0x395, "Epsilon"},
{0x396, "Zaita"},
{0x397, "Aita"},
{0x398, "Thaita"},
{0x399, "Iota"},
{0x39a, "Kappa"},
{0x39b, "Lambda"},
{0x39c, "Mew"},
{0x39d, "New"},
{0x39e, "Ksigh"},
{0x39f, "Omikrohn"},
{0x3a0, "Pie"},
{0x3a1, "Row"},
{0x3a3, "Sigma"},
{0x3a4, "Toue"},
{0x3a5, "Upsilon"},
{0x3a6, "Figh"},
{0x3a7, "Kigh"},
{0x3a8, "Psigh"},
{0x3a9, "Omega"},
{0x3b1, "alpha"},
{0x3b2, "beta"},
{0x3b3, "gamma"},
{0x3b4, "delta"},
{0x3b5, "epsilon"},
{0x3b6, "zaita"},
{0x3b7, "aita"},
{0x3b8, "thaita"},
{0x3b9, "iota"},
{0x3ba, "kappa"},
{0x3bb, "lambda"},
{0x3bc, "mew"},
{0x3bd, "new"},
{0x3be, "ksigh"},
{0x3bf, "omikron"},
{0x3c0, "pie"},
{0x3c1, "row"},
{0x3c2, "sigma f"},
{0x3c3, "sigma"},
{0x3c4, "toue"},
{0x3c5, "upsilon"},
{0x3c6, "figh"},
{0x3c7, "kigh"},
{0x3c8, "psigh"},
{0x3c9, "omega"},
{0x2013, "dash"},
{0x2014, " dash "},
{0x2018, "backquote"},
{0x2019, "apostrophe"},
{0x201c, "backquote"},
{0x201d, "apostrophe"},
{0x2022, "star"},
{0x2026, "..."},
{0x2032, "prime"},
{0x2135, "oleph"},
{0x2190, "left arrow"},
{0x2191, "up arrow"},
{0x2192, "right arrow"},
{0x2193, "down arrow"},
{0x21d4, "double arrow"},
{0x2200, "every"},
{0x2202, "d"},
{0x2203, "some"},
{0x2205, "empty set"},
{0x2207, "del"},
{0x2208, "member of"},
{0x2209, "not a member of"},
{0x220d, "such that"},
{0x2211, "sum"},
{0x221e, "infinity"},
{0x2220, "angle"},
{0x2229, "intersect"},
{0x222a, "union"},
{0x222b, "integral"},
{0x2245, "congruent to"},
{0x2260, "not equal"},
{0x2264, "less than or equal to"},
{0x2265, "grater than or equal to"},
{0x2282, "proper subset of"},
{0x2283, "proper superset of"},
{0x2284, "not a subset of"},
{0x2286, "subset of"},
{0x2287, "superset of"},
{0x25ba, "star"},
{0x266d, "flat"},
{0, 0}
};

static const struct uc_name *uc_names[] = {
0,
english_uc,
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
			if(t[i] != ' ' && !isalpha((unsigned char)t[i])) return 0;
strcpy(rootword, t);
		reconst(root);
return rootword;
} /* acs_smartreplace */

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
int code_l, code_r; /* left and right alt */
code_l = code_r = 0;
if((key1ss&ACS_SS_ALT) == ACS_SS_ALT) {
code_l = acs_build_mkcode(key1key, (key1ss & ~ACS_SS_RALT));
code_r = acs_build_mkcode(key1key, (key1ss & ~ACS_SS_LALT));
}

skipWhite(&s);
c = *s;
	if(c == '<' || c == '|') {
if(!s[1]) goto clear;
if(c == '<')
++s;
if(code_l) {
acs_setmacro(code_l, s);
acs_setmacro(code_r, s);
acs_setkey(key1key, (key1ss & ~ACS_SS_RALT));
acs_setkey(key1key, (key1ss & ~ACS_SS_LALT));
} else {
acs_setmacro(mkcode, s);
acs_setkey(key1key, key1ss);
}
return 0;
}

	if(!c) {
clear:
if(code_l) {
acs_clearmacro(code_l);
acs_clearmacro(code_r);
acs_clearspeechcommand(code_l);
acs_clearspeechcommand(code_r);
acs_unsetkey(key1key, (key1ss & ~ACS_SS_RALT));
acs_unsetkey(key1key, (key1ss & ~ACS_SS_LALT));
} else {
acs_clearmacro(mkcode);
acs_clearspeechcommand(mkcode);
acs_unsetkey(key1key, key1ss);
}
return 0;
}

// keystroke command
// call the syntax checker / converter if provided
if(syn_h && (rc = (*syn_h)(s))) return rc;
if(code_l) {
acs_setspeechcommand(code_l, s);
acs_setspeechcommand(code_r, s);
acs_setkey(key1key, (key1ss & ~ACS_SS_RALT));
acs_setkey(key1key, (key1ss & ~ACS_SS_LALT));
} else {
acs_setspeechcommand(mkcode, s);
acs_setkey(key1key, key1ss);
}
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
u = t + 1;
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
const struct uc_name *u;

acs_clearkeys();

for(i=0; i<numdictwords; ++i) {
free(dict1[i]);
dict1[i] = 0;
free(dict2[i]);
dict2[i] = 0;
}
numdictwords = 0;

for(i=0; i<65536; ++i)
acs_clearpunc(i);

u = uc_names[LANG_ENGLISH]; /* that's all we have right now */
while(u->unicode) {
acs_setpunc(u->unicode, u->name);
++u;
}

for(i=0; i<MK_RANGE; ++i) {
acs_clearmacro(i);
acs_clearspeechcommand(i);
}
} /* acs_reset_configure */

void acs_suspendkeys(const char *except)
{
int key, ss, mkcode;
const char *t;

acs_log("suspend keys\n");
acs_clearkeys();
if(!except) return;

for(ss=0; ss<=15; ++ss) {
for(key=0; key<ACS_NUM_KEYS; ++key) {
mkcode = acs_build_mkcode(key, ss);
t = acs_getspeechcommand(mkcode);
if(!t) continue;
if(!stringEqual(t, except)) continue;
/* this is the one we have to keep, to break out of suspend mode */
acs_log("exception %d, %d\n", key, ss);
acs_setkey(key, ss);
}
}
} /* acs_suspendkeys */

void acs_resumekeys(void)
{
int key, ss, mkcode;

acs_log("resume keys\n");

/* in case the binding of suspend has changed */
acs_clearkeys();

for(ss=0; ss<=15; ++ss) {
for(key=0; key<ACS_NUM_KEYS; ++key) {
mkcode = acs_build_mkcode(key, ss);
if(acs_getspeechcommand(mkcode) || acs_getmacro(mkcode))
acs_setkey(key, ss);
}
}
} /* acs_resumekeys */

