/*********************************************************************
File: acsbind.c
Description: bind speech commands and macros to keys.
Also manage punctuation pronunciations and word replacements.
*********************************************************************/

#include <string.h>
#include <malloc.h>

#include "acsbridge.h"

#define stringEqual !strcmp


/* Internationalization support routines */
/* Switch between unicode and utf8. */

static unsigned char *uni_p;

/* assumes there is room at uni_p and advances it accordingly */
static int uni_1(unsigned int c)
{
	    if(c <= 0x7f) {
			*uni_p++ = c;
		return 1;
	}
	    if(c <= 0x7ff) {
			*uni_p++ = 0xc0 | ((c >> 6) & 0x1f);
			*uni_p++ = 0x80 | (c & 0x3f);
		return 2;
	}
	    if(c <= 0xffff) {
			*uni_p++ = 0xe0 | ((c >> 12) & 0xf);
			*uni_p++ = 0x80 | ((c >> 6) & 0x3f);
			*uni_p++ = 0x80 | (c & 0x3f);
		return 3;
	}
	    if(c <= 0x1fffff) {
			*uni_p++ = 0xf0 | ((c >> 18) & 7);
			*uni_p++ = 0x80 | ((c >> 12) & 0x3f);
			*uni_p++ = 0x80 | ((c >> 6) & 0x3f);
			*uni_p++ = 0x80 | (c & 0x3f);
		return 4;
	}
	    if(c <= 0x3ffffff) {
			*uni_p++ = 0xf8 | ((c >> 24) & 3);
			*uni_p++ = 0x80 | ((c >> 18) & 0x3f);
			*uni_p++ = 0x80 | ((c >> 12) & 0x3f);
			*uni_p++ = 0x80 | ((c >> 6) & 0x3f);
			*uni_p++ = 0x80 | (c & 0x3f);
		return 5;
	}
	    if(c <= 0x7fffffff) {
			*uni_p++ = 0xfc | ((c >> 30) & 1);
			*uni_p++ = 0x80 | ((c >> 24) & 0x3f);
			*uni_p++ = 0x80 | ((c >> 18) & 0x3f);
			*uni_p++ = 0x80 | ((c >> 12) & 0x3f);
			*uni_p++ = 0x80 | ((c >> 6) & 0x3f);
			*uni_p++ = 0x80 | (c & 0x3f);
		return 6;
	}
	return 0;
} /* uni_1 */

static unsigned int utf8_1(void)
{
	unsigned int c;
	int j;
	unsigned char mask;
	unsigned char base = *uni_p++;
	if(base <= 0x7f) return base;
	mask = 0x80;
	j = 0;
	while(mask&base) {
		++j;
		base &= ~mask;
		mask >>= 1;
	}
	if(j == 1 || j > 6) return '?'; /* malformed */
	c = base;
	for(--j; j; --j) {
		c <<= 6;
		base = *uni_p;
		if((base & 0xc0) != 0x80) return '?';
		c |= (base&0x3f);
		++uni_p;
	}
	return (c ? c : '?');
} /* utf8_1 */

/* This function allocates; you need to free when done. */
unsigned char *acs_uni2utf8(const unsigned int *ubuf)
{
	const unsigned int *t;
	int l = 0;
	unsigned char tempbuf[8];
	char *out;
	for(t=ubuf; *t; ++t) {
		uni_p = tempbuf;
		l += uni_1(*t);
	}
	out = malloc(l+1);
	if(!out) return 0;
	uni_p = out;
	for(t=ubuf; *t; ++t)
		uni_1(*t);
	*uni_p = 0;
	return out;
} /* uni2utf8 */

/* convert to utf8 then write to a file */
void acs_write_mix(int fd, const unsigned int *s, int len)
{
static unsigned char buf[256];
uni_p = buf;
while(len--) {
uni_1(*s++);
if(uni_p - buf < 250) continue;
write(fd, buf, uni_p - buf);
uni_p = buf;
}
if(uni_p > buf)
write(fd, buf, uni_p - buf);
} /* acs_write-mix */

/* dest has to have enough room */
int acs_utf82uni(const unsigned char *ubuf, unsigned int *dest)
{
int l = 0;
	uni_p = (unsigned char *)ubuf;
	while(*dest++ = utf8_1()) ++l;
return l;
} /* acs_utf82uni */

int acs_isalpha(unsigned int c)
{
	if(c < 0x80 && isalpha(c)) return 1;

	switch(acs_lang) {
	case ACS_LANG_DE:
		if(c == 0xdf) return 1;
		c |= 0x20;
		if(c == 0xe4 || c == 0xfc || c == 0xf6) return 1;
		break;

	case ACS_LANG_PT_BR:
		c |= 0x20;
		if( c == 0xe0 || c == 0xe1 || c == 0xe2 || c == 0xe3 || c == 0xe7) return 1;
		if(c == 0xe9 || c == 0xea || c == 0xed) return 1;
		if(c == 0xf3 || c == 0xf4 || c == 0xf5 || c == 0xfa || c == 0xfc) return 1;
		break;

	case ACS_LANG_FR:
		c |= 0x20;
		if(c == 0xe0 || c == 0xe8 || c == 0xe9 || c == 0xea || c == 0xee) return 1;
		if(c == 0xf4 || c == 0xfb) return 1;
		break;

	}

	return 0;
} /* acs_isalpha */

int acs_isdigit(unsigned int c)
{
	return (c >= '0' && c <= '9');
} /* acs_isdigit */

int acs_isalnum(unsigned int c)
{
	return (acs_isalpha(c) || acs_isdigit(c));
} /* acs_isalnum */

int acs_isspace(unsigned int c)
{
	if(c < 0x80 && isspace(c)) return 1;
	return 0;
} /* acs_isspace */

/* this assumes you already know it's alpha */
int acs_isupper(unsigned int c)
{
if(c == 0xdf) return 0;
if(c&0x20) return 0;
return 1;
} /* acs_isupper */

/* this assumes you already know it's alpha */
int acs_islower(unsigned int c)
{
if(c == 0xdf) return 1;
if(c&0x20) return 1;
return 0;
} /* acs_isupper */

/* this assumes you already know it's alpha */
unsigned int acs_tolower(unsigned int c)
{
if(c == 0xdf) return c;
return (c | 0x20);
} /* acs_tolower */

/* this assumes you already know it's alpha */
unsigned int acs_toupper(unsigned int c)
{
// 0xdf works here
return (c & ~0x20);
} /* acs_toupper */

/* this assumes you already know it's alpha */
int acs_isvowel(unsigned int c)
{
c = acs_tolower(c);
// english
if (c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' || c == 'y')
return 1;

// latin 1
if(c >= 0xc0 && c < 0x100) {
static const unsigned char wv[] = {
1,1,1,1,1,1,0,0,1,1,1,1,1,1,1,1,
1,0,1,1,1,1,1,0,1,1,1,1,1,0,0,0,
1,1,1,1,1,1,0,0,1,1,1,1,1,1,1,1,
1,0,1,1,1,1,1,0,1,1,1,1,1,0,0,1,
};
return wv[c-0xc0];
}

// higher voweles not yet implemented
return 0;
} /* acs_isvowel */

/* Turn unicode into lower case ascii, as best we can. */
char acs_unaccent(unsigned int c)
{
	static const char down[256+1] =
	"\0......\07.\t\n.\f\r.."
	"................"
	" !\"#$%&'()*+,-./"
	"0123456789:;<=>?"
	"@abcdefghijklmno"
	"pqrstuvwxyz[\\]^_"
	"`abcdefghijklmno"
	"pqrstuvwxyz{|}~\177"
	"..........s....."
	"..........s....y"
	" ..............."
	"................"
	"aaaaaaa eeeeiiii"
	"dnooooo.ouuuuy.s"
	"aaaaaaaceeeeiiii"
	".nooooo.ouuuuy.y";
static const unsigned int in_c[] = {
0x95, 0x99, 0x9c, 0x9d, 0x91, 0x92, 0x93, 0x94,
0xa0, 0xad, 0x96, 0x97, 0x85,
0x2022, 0x25ba, 0x113, 0x2013, 0x2014,
0x2018, 0x2019, 0x201c, 0x201d, 0x200e,
0x2010, 0};
static const char out_c[] =
"*'`'`'`' ----**`--`'`' -";
int i;

if(c < 0x100) return down[c];

for(i=0; in_c[i]; ++i)
if(c == in_c[i]) return out_c[i];

return '?';
} /* acs_unaccent */

int acs_substring_mix(const char *s, const unsigned int *t)
{
int n = 0;
unsigned int c, d;
uni_p = (unsigned char *)s;
while(*uni_p) {
c = utf8_1();
d = *t++;
// if c is a letter it is lower case by assumption.
if(acs_isalpha(d)) d = acs_tolower(d);
if(c != d) return -1;
++n;
}
return n;
} /* acs_substring_mix */

int acs_unilen(const unsigned int *u)
{
	int i;
	for(i=0; *u; ++i, ++u)  ;
	return i;
} /* acs_unilen */

/* Turn a key code and a shift state into a modified key number. */

#define MK_RANGE (ACS_NUM_KEYS * 16)

int acs_build_mkcode(int key, int ss)
{
if((unsigned)key >= ACS_NUM_KEYS) return -1;
if(ss & ~0xf) return -1;
return ss * ACS_NUM_KEYS + key;
} /* acs_build_mkcode */

/* Match two strings, n characters, case insensitive.
 * This is pure ascii, because I'm looking for keywords in a config file,
 * keywords like "pause" for the pause key on your keyboard,
 * so there won't be any utf8 or anything weird.
 * This is used solely by the ascii2mkcode function below.
 * If s and t match up to length n, but s has more letters after,
 * then it is not a match. */
static int keywordmatch_ci(const char *s, const char *t, int n)
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
} /* keywordmatch_ci */

/* Build a modified key code from an ascii string. */
/* Remember the encoded key and state; needed by line_configure below. */
static int key1key, key1ss;
#define ACS_SS_EALT 0x10 /* either alt key on its own */
int acs_ascii2mkcode(const char *s, char **endptr)
{
int ss = 0;
int key;
unsigned char c;
const char *u;

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

static const unsigned char digitcode[] = {
KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9};

static const char otherchars[] = "`-=[];',./";
static const unsigned char othercode[] = {
KEY_GRAVE, KEY_MINUS, KEY_EQUAL, KEY_LEFTBRACE, KEY_RIGHTBRACE,
KEY_APOSTROPHE, KEY_SEMICOLON, KEY_COMMA, KEY_DOT, KEY_SLASH};

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
{"end", KEY_END},
{"pageup", KEY_PAGEUP},
{"pagedown", KEY_PAGEDOWN},
{"insert", KEY_INSERT},
{"delete", KEY_DELETE},
{"leftmeta", KEY_LEFTMETA},
{"rightmeta", KEY_RIGHTMETA},
{"scroll", KEY_SCROLLLOCK},
{"pause", KEY_PAUSE},
{"sysrq", KEY_SYSRQ},
{0, 0}};
struct keyword *kw;

while(1) {
if(s[0] == '+') { ss |= ACS_SS_SHIFT; ++s; continue; }
if(s[0] == '^') { ss|= ACS_SS_CTRL; ++s; continue; }
if(s[0] == '@') { ss|= (ACS_SS_ALT | ACS_SS_EALT); ++s; continue; }
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
if(!keywordmatch_ci(s, kw->name, l)) continue;
key = kw->keycode;
s += l;
goto done;
}

/* This is a key on the lower 48.  We shouldn't be capturing it
 * unless it is adjusted by control or alt. */
if(!(ss & (ACS_SS_ALT|ACS_SS_CTRL)))
goto error;

/* next character should be empty or space or < or | */
if(s[1] && !strchr(" \t<|", s[1])) goto error;
c = (unsigned char) s[0];
++s;

if(c < 0x80 && isalpha(c)) {
c = tolower(c);
key = lettercode[c-'a'];
goto done;
}

if(isdigit(c)) {
key = digitcode[c-'0'];
goto done;
}

u = strchr(otherchars, c);
if(!u) goto error;
key = othercode[u - otherchars];

done:
if(endptr) *endptr = (char*)s;
// save these for line_configure
key1key = key;
key1ss = ss;
return acs_build_mkcode(key, (ss & 0xf));

error:
return -1;
} /* acs_ascii2mkcode */

/* Anything you might type, or capture through cut&paste, therefore utf8 */
static char *macrolist[MK_RANGE];

/* Speech commands should really be ascii, but that is
 * adapter specific, so I'm not sure. */
static char *speechcommandlist[MK_RANGE];

/* This mirrors ismeta in the device driver, but we don't need
 * the kernel meta keys, just the user specified meta keys. */
static unsigned char ismetalist[ACS_NUM_KEYS];

/* a mirror of passt in the device driver */
static unsigned short passt[ACS_NUM_KEYS];

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

/* Preset words for punctuation and other unicodes.
 * These can be changed in your config file. */
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
{0, 0}
};

static const struct uc_name german_uc[] = {
{7, "glocke"},
{8, "hinter"},
{9, "tab"},
{10, "neues"},
{12, "formfeed"},
{13, "return"},
{27, "aus"},
{' ', "space"},
{'!', "bang"},
{'"', "quote"},
{'#', "numer"},
{'$', "toller"},
{'%', "percent"},
{'&', "und"},
{'\'', "apostrophe"},
{'(', "links paren"},
{')', "recht paren"},
{'*', "star"},
{'+', "plus"},
{',', "comma"},
{'-', "dash"},
{'.', "punkt"},
{'/', "slash"},
{':', "colen"},
{';', "semmycolen"},
{'<', "less than"},
{'=', "eequals"},
{'>', "greater than"},
{'?', "frage"},
{'@', "at sign"},
{'[', "links bracket"},
{'\\', "backslash"},
{']', "recht bracket"},
{'^', "up airow"},
{'_', "unter"},
{'`', "backquote"},
{'{', "links brace"},
{'|', "pipe"},
{'}', "recht brace"},
{'~', "tilde"},
{0x7f, "delete"},
{0, 0}
};

static const struct uc_name portuguese_uc[] = {
{7, "bipe"},
{8, "beque speice"},
{9, "tab"},
{10, "linha nova"},
{12, "retorno"},
{13, "enter"},
{27, "esc"},
{' ', "espaço"},
{'!', "exclamação"},
{'"', "aspas"},
{'#', "cardinal"},
{'$', "cifrão"},
{'%', "por cento"},
{'&', "e"},
{'\'', "apóstrofo"},
{'(', "abre parênteses"},
{')', "fecha parênteses"},
{'*', "asterisco"},
{'+', "mais"},
{',', "vírgula"},
{'-', "hífen"},
{'.', "ponto"},
{'/', "barra"},
{':', "dois pontos"},
{';', "ponto e vírgula"},
{'<', "menor"},
{'=', "igual"},
{'>', "maior"},
{'?', "interrogação"},
{'@', "arroba"},
{'[', "abre colchetes"},
{'\\', "barra invertida"},
{']', "fecha colchetes"},
{'^', "circunflexo"},
{'_', "sublinhado"},
{'`', "grave"},
{'{', "abre chaves"},
{'|', "barra vertical"},
{'}', "fecha chaves"},
{'~', "til"},
{0x7f, "delete"},
{0xa0, "quebra"},
{0xa2, "centavos"},
{0xa3, "libra"},
{0xa4, "moeda"},
{0xa5, "iene"},
{0xa6, "barra cortada"},
{0xa7, "seção"},
{0xa8, "trema"},
{0xa9, "copyright"},
{0xaa, "feminino"},
{0xab, "seta esquerda"},
{0xad, "hífen suave"},
{0xae, "registrado"},
{0xaf, "macro"},
{0xb0, "graus"},
{0xb1, "mais ou menos"},
{0xb2, "quadrado"},
{0xb3, "cubo"},
{0xb4, "agudo"},
{0xb5, "micro"},
{0xb7, "bolinha"},
{0xb8, "cedilha"},
{0xba, "masculino"},
{0xbb, "seta direita"},
{0xbc, "um quarto"},
{0xbd, "meio"},
{0xbe, "três quartos"},
{0xd7, "vezes"},
{0xf7, "dividido por"},
{0x113, "grave"},
{0x2013, "hífen"},
{0x2014, "traveção"},
{0x2018, "grave"},
{0x2019, "apóstrofo"},
{0x201c, "grave"},
{0x201d, "apóstrofo"},
{0x2022, "estrela"},
{0x2026, "reticências"},
{0x2190, "seta esquerda"},
{0x2191, "seta cima"},
{0x2192, "seta direita"},
{0x2193, "seta baixo"},
{0x21d4, "seta dupla"},
{0, 0}
};

static const struct uc_name *uc_names[] = {
0,
english_uc,
german_uc,
portuguese_uc,
};

static char *punclist[65536];

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

/* The replacement dictionary, in utf8 */

static char *dict1[NUMDICTWORDS];
static char *dict2[NUMDICTWORDS];
static int numdictwords;
// Build the lower case word, in utf8 or in unicode.
static char lw_utf8[WORDLEN+8];

static int lowerword(const char *w)
{
	char *lp = lw_utf8; // lower case word pointer
	unsigned int uc; // unicode of each letter

	while(*w) {
		uni_p = (unsigned char *)w;
		uc = utf8_1(); // convert utf8 to unicode
		if(!acs_isalpha(uc)) return -1; // not a letter in your language
		w = (char *)uni_p; // prior call pushes uni_p along
		
uc = acs_tolower(uc);
// back to utf8
		uni_p = (unsigned char *)lp;
		uni_1(uc);
		lp = (char *)uni_p;
		if(lp > lw_utf8 + WORDLEN) return -6; // too long
	}

	*lp = 0;
	return 0;
} /* lowerword */

static int
inDictionary(const char *s)
{
	int i;
	for(i=0; i<numdictwords; ++i)
		if(stringEqual(s, dict1[i])) return i;
	return -1;
} /* inDictionary */

static char *
fromDictionary(const char *s)
{
int j = inDictionary(s);
return (j >= 0 ? dict2[j] : 0);
} /* fromDictionary */

int acs_setword(const char *word1, const char *word2)
{
int j, rc;
if(rc = lowerword(word1)) return rc;
if(word2 && strlen(word2) > WORDLEN) return -6;
j = inDictionary(lw_utf8);
if(j < 0) {
if(!word2) return 0;
// new entry
j = numdictwords;
if(j == NUMDICTWORDS) return -7; // no room
++numdictwords;
dict1[j] = malloc(strlen(lw_utf8) + 1);
strcpy(dict1[j], lw_utf8);
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
To be international, this is all done in unicode.
*********************************************************************/

static unsigned int rootword[WORDLEN+16];

static unsigned int *inline_uni(char *t)
{
int i = 0;
uni_p = (unsigned char *)t;
while(rootword[i] = utf8_1())
++i;
return rootword;
} /* inline_uni */

static int mkroot_english(int wdlen)
{
	char l0, l1, l2, l3, l4; /* trailing letters */
	short l;

	l = wdlen - 5;
	if(l < 0) return 0; // word too short to safely rootinize
// This is english specific, and all english letters are ascii,
// so no problem putting them into chars and using routines like strchr.
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
		if(!acs_isvowel(l1)) {
			if(l1 == l0) { rootword[l+1] = 0; return 5; }
			if(acs_isvowel(l0) &&  l0 < 'w' && !acs_isvowel(rootword[l-1])) {
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
		if(!acs_isvowel(l2)) {
			if(l2 == l1) { rootword[l+2] = 0; return 8; }
			if(acs_isvowel(l1) && l1 < 'w' && !acs_isvowel(l0)) {
				rootword[l+4] = 0;
				return 7;
			}
		}
		rootword[l+3] = 0;
		return 9;
	} // end final ed

	return 0;
} /* mkroot_english */

/* reconstruct the word based upon its root and the removed suffix */
static void reconst_english(int root)
{
static const char suftab[] = "s   es  ies ing ing ing d   ed  ed  ied 's  'll ";
/* I believe the last two 's and 'll are vestigial and not used.
 * Anything 's or 'll is handled in tpxlate.c
 * Which suffixes drop e or y when appended? */
static const char sufdrop[] = "  y  e   y  ";
/* Which suffixes double the final consonent, as in dropped. */
static const char sufdouble[] = {
	0,0,0,0,1,0,0,1,0,0,0,0};
	unsigned int *t;
	short i, wdlen;
	unsigned int c;

	--root;
	wdlen = acs_unilen(rootword);
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

typedef int (*root_fn)(int);

static const root_fn mkroot_fns[] = {
0,
mkroot_english,
0,
0};

typedef void (*reconst_fn)(int);

static const reconst_fn reconst_fns[] = {
0,
reconst_english,
0,
0};

unsigned int *acs_replace(const unsigned int *s, int len)
{
int i, root;
char *t;
unsigned int c;
root_fn f;

uni_p = (unsigned char *)lw_utf8;
for(i=0; i<len; ++i, ++s) {
if((char *)uni_p - lw_utf8 > WORDLEN) return 0;
c = *s;
// This should already be a letter, but let's recheck.
if(!acs_isalpha(c)) return 0;
c = acs_tolower(c);
rootword[i] = c;
uni_1(c);
}
rootword[i] = 0;
*uni_p = 0;

t = fromDictionary(lw_utf8);
if(t) return inline_uni(t);

// not there; extrac the root word
if(!(f = mkroot_fns[acs_lang])) return 0;

root = (*f)(i);
if(!root) return 0;

/* have to go back to utf8 to do the lookup */
uni_p = (unsigned char *)lw_utf8;
for(i=0; c = rootword[i]; ++i)
uni_1(c);
*uni_p = 0;
t = fromDictionary(lw_utf8);
if(!t) return 0;
// and back to unicode
inline_uni(t);
// can't add the suffix if there are punctuations and things
for(i=0; c = rootword[i]; ++i)
if(c != ' ' && !acs_isalpha(c)) return 0;
		(*reconst_fns[acs_lang])(root);
return rootword;
} /* acs_replace */

/* This works in ascii or utf8 */
static void skipWhite(char **t)
{
char *s = *t;
while(*s == ' ' || *s == '\t') ++s;
*t = s;
} /* skipWhite */

/* Process a line from a config file, performs any of the above functions */
int acs_line_configure(char *s, acs_syntax_handler_t syn_h)
{
	int mkcode, rc;
char *t, *u;
char save, c;
char teebit = 0;
unsigned int p_uc; // punctuation unicode

// leading whitespace doesn't matter
skipWhite(&s);

// comment line starts with #, except for ## space or# digit
if(s[0] == '#') {
if(!s[1]) return 0;
if(!strchr("1234567890.+-*/", s[1])) return 0;
}

mkcode = acs_ascii2mkcode(s, &s);
if(mkcode >= 0) { // key assignment
int code_l, code_r; /* left and right alt */
code_l = code_r = 0;

skipWhite(&s);

/* handle the meta settings first */
if(!key1ss && strpbrk(s, "+^@")) {
int simss = 0; // the simulated shift state
t = s;
while(c = *t) {
++t;
if(c == '+') { simss |= ACS_SS_SHIFT; continue; }
if(c == '^') { simss |= ACS_SS_CTRL; continue; }
if(*t++ != '@') goto nometa;
if(c == 'r' || c == 'R') { simss |= ACS_SS_RALT; continue; }
if(c == 'l' || c == 'L') { simss |= ACS_SS_LALT; continue; }
goto nometa;
}
// We got something.
ismetalist[key1key] = simss;
acs_ismeta(key1key, simss);
return 0;
}

nometa:
c = *s;
if(key1ss&ACS_SS_EALT) {
key1ss &= 0xf;
code_l = acs_build_mkcode(key1key, (key1ss & ~ACS_SS_RALT));
code_r = acs_build_mkcode(key1key, (key1ss & ~ACS_SS_LALT));
}

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

	if(c == 'T' && (s[1] == ' ' || s[1] == '\t')) {
		teebit = ACS_KEY_T;
++s;
		skipWhite(&s);
		c = *s;
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
passt[key1key] &= ~(1 << (key1ss & ~ACS_SS_RALT));
passt[key1key] &= ~(1 << (key1ss & ~ACS_SS_LALT));
} else {
acs_clearmacro(mkcode);
acs_clearspeechcommand(mkcode);
acs_unsetkey(key1key, key1ss);
passt[key1key] &= ~(1 << key1ss);
}
if(!key1ss) { // plain state, no meta
ismetalist[key1key] = 0;
acs_ismeta(key1key, 0);
}
return 0;
}

// keystroke command
// call the syntax checker / converter if provided
if(syn_h && (rc = (*syn_h)(s))) return rc;
if(code_l) {
acs_setspeechcommand(code_l, s);
acs_setspeechcommand(code_r, s);
acs_setkey(key1key, ((key1ss & ~ACS_SS_RALT) | teebit));
acs_setkey(key1key, ((key1ss & ~ACS_SS_LALT) | teebit));
if(teebit) {
passt[key1key] |= (1 << (key1ss & ~ACS_SS_RALT));
passt[key1key] |= (1 << (key1ss & ~ACS_SS_LALT));
}
} else {
acs_setspeechcommand(mkcode, s);
acs_setkey(key1key, (key1ss | teebit));
if(teebit)
passt[key1key] |= (1 << key1ss);
}
return 0;
}

// at this point we are setting the pronunciation of a punctuation or a word
c = *s;
if(!c) return 0; // empty line

t = strpbrk(s, " \t");
if(t) { save = *t; *t = 0; }

uni_p = (unsigned char *)s;
p_uc = utf8_1();
if(*uni_p == 0 || uni_p == (unsigned char *)t) {
punc:
// cannot leave it with no pronunciation
if(!t) return -8;
*t = save;
skipWhite(&t);
if(!*t) return -8;
if(p_uc <= ' ') return -9;
if(p_uc >= 0x10000) return -9;
if(acs_isalnum(p_uc)) return -9;
acs_setpunc(p_uc, t);
return 0;
}

if(c == 'u' && isdigit((unsigned char)s[1])) {
p_uc = strtol(s+1, &s, 10);
if(*s == 0 || s == t) goto punc;
if(t) *t = save;
return -1;
}

if(c == 'x' && isxdigit((unsigned char)s[1])) {
p_uc = strtol(s+1, &s, 16);
if(*s == 0 || s == t) goto punc;
if(t) *t = save;
return -1;
}

// The only thing left is word replacement.
if(!t) return acs_setword(s, 0);

u = t + 1;
skipWhite(&u);
rc = acs_setword(s, u);
*t = save;
return rc;
} /* acs_line_configure */

/* Go back to the default configuration. */
void acs_reset_configure(void)
{
int i;
const struct uc_name *u;

acs_clearkeys();
memset(ismetalist, 0, sizeof(ismetalist));
memset(passt, 0, sizeof(passt));

for(i=0; i<numdictwords; ++i) {
free(dict1[i]);
dict1[i] = 0;
free(dict2[i]);
dict2[i] = 0;
}
numdictwords = 0;

for(i=0; i<65536; ++i)
acs_clearpunc(i);

u = uc_names[acs_lang]; /* that's all we have right now */
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
if(t && stringEqual(t, except)) {
/* this is the one we have to keep, to break out of suspend mode */
acs_log("exception %d, %d\n", key, ss);
acs_setkey(key, ss);
}
t = acs_getmacro(mkcode);
if(t && t[0] == '|') {
/* retain keys that run system commands */
acs_log("system %d, %d\n", key, ss);
acs_setkey(key, ss);
}
}
}
} /* acs_suspendkeys */

void acs_resumekeys(void)
{
int key, ss, teebit, mkcode;

acs_log("resume keys\n");

acs_clearkeys();

for(key=0; key<ACS_NUM_KEYS; ++key) {
if(ismetalist[key]) acs_ismeta(key, ismetalist[key]);

for(ss=0; ss<=15; ++ss) {
mkcode = acs_build_mkcode(key, ss);
if(acs_getspeechcommand(mkcode) || acs_getmacro(mkcode)) {
teebit = 0;
if(passt[key] & (1<<ss))
teebit = ACS_KEY_T;
acs_setkey(key, (ss | teebit));
}
}
}
} /* acs_resumekeys */

