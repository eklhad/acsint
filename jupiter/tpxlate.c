/*********************************************************************

tpxlate.c: perform final translations to prepare for tts.
This expands the constructs that were encoded in tpencode.c.

Copyright (C) Karl Dahlke, 2011.
This software may be freely distributed under the GPL, general public license,
as articulated by the Free Software Foundation.

The point of these routines is to add text, or codes, to an output string,
as a result of processing an input string.
When we reach the end of the output string, we either return an error code
all the way back to the calling routine, or we realloc the string,
depending on the application.
In practice the output buffer is large enough to handle any
reasonably sized sentence.
*********************************************************************/

#include <time.h>
#include <malloc.h>

#include "tp.h"


/*********************************************************************
A few global variables.
These can be adjusted based on your synthesizer.
*********************************************************************/

char tp_alnumPrep = 0;
char tp_relativeDate = 0;
char tp_showZones = 0;
int tp_myZone = -5; /* offset from gmt */
char tp_digitWords = 0; /* read digits as words */
char tp_acronUpper = 1; /* acronym letters in upper case? */
char tp_acronDelim = ' ';
char tp_oneSymbol; /* read one symbol - not a sentence */
char tp_readLiteral = 1; // read each punctuation mark
/* a convenient place to put little phrases to speak */
char shortPhrase[NEWWORDLEN];


/*********************************************************************
Strings that are folded into the output text.
There is one structure of strings for each language.
Of course other aspects of translation must also change.
Spanish might say "the fifth of may", rather than May fifth,
but we hope most of the changes are captured in these strings.
*********************************************************************/

struct OUTWORDS {
const char *decades[8];
const char *hundredWord;
const char *thousandWord;
const char *millionWord;
const char *billionWord;
const char *trillionWord;
const char *bigNumbers[5];
const char *hundredthWord;
/*Sometimes it's more portable to use words than numbers. */
/*Especially if you're adding 's on the end, such as 5 Boing 707's. */
const char *idigits[10];
const char *ohWord; /*o instead of 0 */
const char *teens[10];
const char *ordinals[20];
const char *orddecades[8];
const char *todayWord;
const char *yesterdayWord;
const char *tomorrowWord;
const char *weekdays[7];
const char *noonWord;
const char *midnightWord;
const char *oclockWord;
const char *andWord;
const char *orWord;
const char *halfWord;
const char *toTheWord;
const char *squareWord;
const char *cubeWord;
const char *dashWord;
const char *colonWord;
const char *bangWord;
const char *minusWord;
const char *dollarWord;
const char *dollarsWord;
const char *centWord;
const char *centsWord;
const char *ieWord;
const char *egWord;
const char *theWord;
const char *lengthWord;
const char *byWord;
const char *pointWord;
const char *dotWord;
const char *atWord;
const char *toWord;
const char *throughWord;
const char *slashWord;
const char *numberWord;
const char *numbersWord;
const char *lessWord;
const char *greaterWord;
const char *equalsWord;
const char *oreqWord;
const char *months[12];
const char *zones[30];
const char *nohundred[30];
const char *articles[20];
const char *verbs[20];
const char *slashOrPhrases[8];
const char *areaWord;
const char *extWord;
const char *emotPhrase[8];
const char *states[54];
const char *bible[67];
const char *chapterWord;
const char *verseWord;
const char *versesWord;
const char *protocols[8];
const char *locUnder;
const char *fileUnder;
const char *pageUnder;
const char *flowInto[12];
const char *natoWords[26];
};

static const struct OUTWORDS outwords[3] = {

{ /* no output words for the zero language */
{0}

},{ /* English */

{"twenty", "thirdy", "fordy", "fifdy", "sixdy", "sevendy", "eighty", "ninety"},
"hundred", "thousand", "million", "billion", "trillion",
{"thousand", "million", "billion", "trillion", 0},
"hundredth",
{"0", /* I haven't found a good portable word for zero */
"one", "too", "three", "four", "five", "six", "seven", "ate", "nine"},
"o",
{"ten", "eleven", "twelve", "thirteen", "fourteen",
"fifteen", "sixteen", "seventeen", "eighteen", "nineteen"},
{"zeroath", "first", "second", "third", "forth",
"fifth", "sixth", "seventh", "eighth", "ninth",
"tenth", "eleventh", "twelvth", "thirteenth", "fourteenth",
"fifteenth", "sixteenth", "seventeenth", "eighteenth", "nineteenth"},
{"twendyith", "thirdyith", "fordyith", "fifdyith", "sixdyith", "sevendyith", "aidyith", "ninetyith"},
"today", "yesterday", "tomorrow",
{"sunday", "munday", "toosday", "wensday",
"thursday", "friday", "saturday"},
"noon", "midnight", "uhclock",
"and", "or",
"half",
"to the", "squared", "cubed",
"dash", "colen", "bang", "minus", "doller", "dollers", "cent", "cents",
"that is,", "for example,",
"the", "length", "by",
"point", "dot", "at", "to", "through",
"slash", "number", "numbers",
"less than", "greater than", "eequals", "or eequal to",
{"january", "february", "march",
"aiprle", "may", "june",
"juligh", "august", "september",
"october", "noavember", "december"},
{0,
"pacific standard time", "pacific daylight time",
"mountain standard time", "mountain daylight time",
"central standard time", "central daylight time",
"eastern standard time", "eastern daylight time",
"hawaiian standard time",
"british standard time", "british daylight time",
"grenich mean time", "universal time", "universal time",
0},
{"room", "suite", "apartment", "apt",
"dept", "department", "box", "pobox",
"part", "piece", "site", "box",
"car", "flight", "number",
0},
{"a", "an", "the", "this", "that", "my", "your", "his", "her", "our", "their", 0},
{"is", "was", "should", "could", "might", "may", "can", "has", "had",
"would", "will", "came", "arrived", "gave", "made",
0},
{"he/she", "she/he", "him/her", "her/him",
"his/her", "her/his",
0},
", area code", "extension",
{"wink wink", "that's funny", "so sad", "wow",
"ha ha", "told you so"},
{0,
"ahlebama", "alaska", "arizoana", "arkansaw",
"californya", "colurrado", "connetiket", "dellawair",
"floridda", "jorja", "hohwighey", "ighdaho",
"illinoy", "indeeana", "ighowa", "kansis",
"kintucky", "looeeseeana", "main", "maralend",
"massichusitts", "michigan", "minnassoada", "mississippy",
"missoory", "mohntana", "nubraska", "nuvvada",
"new hampsher", "new jerzy", "new mexico", "new york",
"north carolighna", "north deckoada", "oahigho", "oakla hoama",
"oragohn", "pensle vainya", "rode ighlend", "south carolighna",
"south deckoada", "tennessee", "texis", "utaw",
"vermont", "verginya", "washington", "wisconsen",
"west verginya", "wyoming", "D C", "porta reeko",
"vergin ighlends"},
{0,
"genisis", "exidis", "levitikis", "numbers", "duterronomy",
"joshua", "judges", "rooth", "first samule", "second samule",
"first kings", "second kings", "first chronicles", "second chronicles",
"ezra", "neeamigha", "esther", "jobe", "salms", "prohverbs",
"ekleaziasties", "solomon", "ighzaia", "jeramigha",
"lamentations", "izeakyal", "dannyal", "hoazaya", "jole",
"aimus", "oabedigha", "joana", "mighka", "naium", "habekuk",
"zephannigha", "haggigh", "zakirrigha", "malikigh",
"matheu", "mark", "luke", "john", "acts", "romens",
"first corinthians", "second corinthians",
"galaytiens", "epheesions", "philippians", "colossions",
"first thessalonians", "second thessalonians",
"first timithy", "second timithy", "tightis",
"phighleamen", "heebrews", "james",
"first peater", "second peater",
"first john", "second john", "third john", "jude",
"rehvalations"},
"chapter", "verse", "verses",
{0,
"web site", "telnet server", "R login server", "mail server",
"F T P site", "goapher server"},
"a location under", "a file under", "a web page under",
{"site", "at", "from", "to", "on", "visit", "is", 0},
{"alpha", "brohvo", "charlie", "delta", "echo",
"foxtrot", "gawlf", "hotell", "india", "juleyet",
"killo", "liema", "mike", "noavember", "oscar",
"popa", "kebeck", "romeo", "seeara", "tango",
"uniform", "victor", "wiskey", "x ray", "yangkey", "zoolu"},

},{ /* Spanish */

/* not yet implemented */
{0}

}};

static const struct OUTWORDS *ow;
const char * const * articles;
const char *andWord;

/* Set things up for tts preprocessing */
int
setupTTS(void)
{
const int room = 400;
tp_in->buf = malloc(room);
tp_in->offset = malloc(room * sizeof(acs_ofs_type));
tp_out->buf = malloc(room);
tp_out->offset = malloc(room * sizeof(acs_ofs_type));
if(!tp_in->buf || !tp_in->offset || !tp_out->buf || !tp_out->offset)
return -1;
tp_in->room = room;
tp_out->room = room;

	ow = outwords + ACS_LANG_EN; /* that's all we have for now */
	articles = ow->articles;
	andWord = ow->andWord;

sortReservedWords();

return 0;
} /* setupTTS */


/* speak a single character.
 * Or write, into shortPhrase, how it should be pronounced.
* sayit means speak the character now.
 * bellsound means newline and bell make sounds, rather than speaking words.
 * asword = 1, say the word cap before capital letter.
 * asword = 2, a letter is spoken using the nato phonetic alphabet,
 * thus making it clear whether it is m or n. */
void speakChar(int c, int sayit, int bellsound, int asword)
{
	short i;
	const char *t;
	static char ctrlstr[] = "controal x";

	if(c == '\7') {
		if(bellsound) { acs_bell(); return; }
		t = "bell";
		goto copy_t;
	}

	if(c == '\r') {
		t = "return";
		goto copy_t;
	}

	if(c == '\n') {
		if(bellsound) { acs_cr(); return; }
		t = "line";
		goto copy_t;
	}

	if(c == '\t') {
		t = "tab";
		goto copy_t;
	}

	if(c < 27) {
		ctrlstr[9] = c|0x40;
		t = ctrlstr;

copy_t:
		strcpy((char*)shortPhrase, t);
		if(sayit) acs_say_string(shortPhrase);
		return;
	} /* control character */

top:
t = (char*)acs_getpunc(c);
if(t) goto copy_t;

if(c >= 256) {
/* We are past getpunc(), guess we don't know how to say this unicode. */
c = '?';
goto top;
}

		if(!isalnum(c)) {
			static char nonascii[] = "bite x x";
			static const char almostHex[] = "0123456789xqcdlf";
			nonascii[5] = almostHex[c>>4];
			nonascii[7] = almostHex[c&15];
			t =  nonascii;
		goto copy_t;
	}

/* now a letter or digit */
if(isalpha(c) && asword == 2) {
c = tolower(c);
t = ow->natoWords[c-'a'];
goto copy_t;
}

	if(sayit) {
if(isupper(c) && asword == 1) {
char capbuf[8];
c = tolower(c);
sprintf(capbuf, "cap %c", c);
acs_say_string(capbuf);
} else {
acs_say_char(c);
}
}
} /* speakChar */


/*********************************************************************
Text buffer structures.
Holds input text and output text, as transformed by each pass.
*********************************************************************/

static struct textbuf tb1, tb2;

struct textbuf *tp_in = &tb1, *tp_out = &tb2;

void textBufSwitch(void)
{
	struct textbuf *save;
	save = tp_in;
	tp_in = tp_out;
	tp_out = save;
	memset(tp_out->offset, 0, tp_out->room*sizeof(acs_ofs_type));
	tp_out->buf[0] = 0;
	tp_out->len = 1;
} /* textBufSwitch */

void carryOffsetForward(const char *s)
{
	acs_ofs_type offset = tp_in->offset[s - tp_in->buf];
	tp_out->offset[tp_out->len] = offset;
} /* carryOffsetForward */

/* There's always room for the last zero */
void textbufClose(const char *s, int overflow)
{
	if(overflow) {
		/* Back up to the start of this token. */
		while(!tp_out->offset[tp_out->len]) {
			appendBackup();
			if(tp_out->len == 1) break;
		}
	} else carryOffsetForward(s);
	tp_out->buf[tp_out->len] = 0;
} /* textbufClose */


/* isvowel, kinda like isalpha etc. */
int isvowel(char c)
{
c = tolower(c);
if(c == 'a' || c == 'e' || c == 'i' ||
c == 'o' || c == 'u' || c == 'y')
return 1;
return 0;
} /* isvowel */

/* case_different, one must be upper and the other must be lower */
int case_different(char x, char y)
{
if(isupper(x) && islower(y)) return 1;
if(isupper(y) && islower(x)) return 1;
return 0;
} /* case_different */

/*********************************************************************
Keep track of the current date.
This is done to simplify the reading of dates.
If we're talking about yesterday, we can just say "yesterday",
rather than month day year.
*********************************************************************/

static int nowyear; /* years past 1970 */
static long nowday; /* days past 1970 */

static void time_checkpoint(void)
{
	int days;
	unsigned long ulsec;
	time_t sec;
	time(&sec);
	ulsec = (unsigned long)sec;
	ulsec += tp_myZone*3600; /* convert from gmt */
	ulsec /= (24*3600L);
	nowday = (int)ulsec + 1;
	nowyear = (int)(ulsec / 366);
	ulsec -= 365*nowyear;
	ulsec -= (nowyear+1)/4;
	days = 365;
	if((nowyear%4) == 2) days = 366;
	if(ulsec >= days) ulsec -= days, ++nowyear;
} /* time_checkpoint */


/*********************************************************************
Check to see whether a word is contained in a list of words.
This is a case insensitive search.
If <s_len> is 0, we ask whether the candidate word
has any of the strings as a left prefix.
Returns the index of the matching string,
or -1 if there is no match.
*********************************************************************/

int isSubword(const char *s, const char *t)
{
	int n = 0;
	while(*s) {
		if(ci_cmp(*s, *t)) return -1;
		if(*s != *t && !isalpha(*s)) return -1;
		++s, ++t, ++n;
	}
	return n;
} /* isSubword */

int wordInList(const char * const *list, const char *s, int s_len)
{
	const char *x;
	int i, len;

	for(i=0; (x = *list); ++list, ++i) {
		if((len = isSubword(x, s)) < 0) continue;
		if(!s_len || s_len == len) return i;
	} /* loop over words  in list */

	return -1;
} /* wordInList */


/*********************************************************************
A dozen append() routines add strings, dates,
times, moneys, phone numbers, etc to the growing buffer.
They all return 1 if we run out of buffer.
*********************************************************************/

static int roomCheck(int n)
{
	char *buf;
	acs_ofs_type *ofs;
	int room;
	if(tp_out->len + n < tp_out->room) return 0;
	room = tp_out->room/3*4;
	buf = realloc(tp_out->buf, room);
	if(!buf) return 1;
	ofs = realloc(tp_out->offset, room*sizeof(acs_ofs_type));
	if(!ofs) return 1;
	tp_out->buf = buf;
	tp_out->offset = ofs;
tp_out->room = room;
	return 0;
} /* roomCheck */

int appendChar(char c)
{
	if(roomCheck(1)) return 1;
	tp_out->buf[tp_out->len++] = c;
	return 0;
} /* appendChar */

/* append an isolated char or digit */
static int appendIchar(char c)
{
	if(roomCheck(2)) return 1;
	tp_out->buf[tp_out->len++] = c;
	tp_out->buf[tp_out->len++] = ' ';
	return 0;
} /* appendIchar */

/* This routine assumes we are dealing with bytes, not 16-bit unicodes. */
/* It also assumes any letters in the string are lower case. */
int appendString(const char *s)
{
	int n = strlen(s);
	if(roomCheck(n+1)) return 1;
	strcpy((tp_out->buf + tp_out->len), s);
	tp_out->len += n;
	tp_out->buf[tp_out->len++] = ' ';
	return 0;
} /* appendString */

static int appendIdigit(int n)
{
	return (tp_digitWords ?
	appendString(ow->idigits[n]) :
	appendIchar('0'+n));
} /* appendIdigit */

/* Speak a string of digits.
 * Should we put spaces between the digits?  Depends on the synthesizer.
 * It would probably be better, i.e. more portable,
 * to read them as words -- so that's what we do. */
static int appendDigitString(const char *s, int n)
{
	char c;
	while(n--) {
		c = *s++;
		if(appendIdigit(c-'0')) return 1;
//		if(n && !tp_digitWords) appendBackup();
	}
	return 0;
} /* appendDigitString */

void lastUncomma(void)
{
	int len = tp_out->len;
	acs_ofs_type offset = tp_out->offset[len];
	char *s = tp_out->buf + len - 1;
	char c = *s;
	while(c == ' ') --len, c = *--s;
	if(c != ',') return;
	--len;
	tp_out->offset[len] = offset;
	tp_out->len = len;
} /* lastUncomma */

static int appendAcronString(const char *s)
{
	char c;
	int n = strlen(s);
	if(roomCheck(2*n)) return 1;
	while(n--) {
		c = *s++;
		/* we assume c is a letter */
		if(tp_acronUpper) c = toupper(c);
		else c = tolower(c);
		tp_out->buf[tp_out->len++] = c;
		c = n ? tp_acronDelim : ' ';
		tp_out->buf[tp_out->len++] = c;
	}
	return 0;
} /* appendAcronString */

/* Read a natural number, up to 3 digits. */
/* The dohundred parameter indicates 2 hundred 3 or 2 oh 3. */
/* The zero parameter indicates whether 0 will be read. */
static int append3num(int n, int dohundred, int zero)
{
	const char *q;
	int rc = 0;

	if(!n && zero) return appendIdigit(0);

	if(n >= 100) {
		rc |= appendIdigit(n/100);
		n %= 100;
		if(!n) dohundred = 1;
		if(dohundred) rc |= appendString(ow->hundredWord); 
		else if(n < 10) rc |= appendString(ow->ohWord);
	} /* hundreds */

	if(n >= 10) {
		if(n < 20) q = ow->teens[n-10];
		else q = ow->decades[n/10 - 2];
		rc |= appendString(q);
	}

	if(n%10 && (n < 10 || n > 20))
		rc |= appendIdigit(n%10);
	return rc;
} /* append3num */

/* read 09 as O 9, and 00 as o o */
static int appendOX(int n)
{
	int rc = 0;
	if(n < 10) rc |= appendString(ow->ohWord);
	if(n) rc |= append3num(n, 0, 0);
	else rc |= appendString(ow->ohWord);
	return rc;
} /* appendOX */

/* Read a 4 digit number as a year. */
/* This is optimal for other 4-digit numbers, such as house numbers etc. */
static int appendYear(int y)
{
	int rc = 0;

	if(!((y%1000) / 100)) {
		if(y >= 1000) {
			rc |= appendIdigit(y/1000);
			rc |= appendString(ow->thousandWord);
		}
		y %= 100;
		rc |= append3num(y, 0, 0);
		return rc;
	} /* in the year 2007 */

	rc |= append3num(y/100, 0, 0); /* century */
	y %= 100;
	if(!y) rc |= appendString(ow->hundredWord);
	else rc |= appendOX(y);
	return rc;
} /* appendYear */

/* Read a natural number, up to 6 digits. */
static int append6num(int n)
{
	int rc = 0;
	int top = n/1000;
	int bottom = n%1000;
	if(top < 10) return appendYear(n);
	if(top) {
		rc |= append3num(top, 1, 0);
		rc |= appendString(ow->thousandWord);
	}
	rc |= append3num(bottom, 1, (int)!top);
	return rc;
} /* append6num */

/* append 3-digit ordinal, such as first, or seventeenth */
static int appendOrdinal(int n)
{
	int rc = 0;

	if(n >= 100) {
		rc |= appendIdigit(n/100);
		n %= 100;
		if(!n) { rc |= appendString(ow->hundredthWord); return rc; }
		rc |= appendString(ow->hundredWord);
	}

	if(n < 20) {
		rc |= appendString(ow->ordinals[n]);
		return rc;
	}

	if(n%10 == 0) {
		rc |= appendString(ow->orddecades[n/10-2]);
		return rc;
	}

	/* write the decade first */
	rc |= appendString(ow->decades[n/10 - 2]);
	rc |= appendString(ow->ordinals[n%10]);
	return rc;
} /* appendOrdinal */

/* read the nxx and xxxx of a phone number */
static int appendNxx(int n)
{
	int rc = 0;
	int h = n/100; /* h is nonzero */
	n %= 100;
	rc |= appendIdigit(h);
	if(!n) rc |= appendString(ow->hundredWord);
	else {
		rc |= appendIdigit(n/10);
		rc |= appendIdigit(n%10);
	}
	return 0;
} /* appendNxx */

static int appendXxxx(int n)
{
	int rc = 0;
	int t, h; /* thousands, hundreds */

	t = n / 1000;
	n %= 1000;
	h = n / 100;
	n %= 100;

	if(h) {
		if(t && (n/10 || !n)) {
			rc |= append3num(t*10 + h, 0, 0);
		} else {
			rc |= appendIdigit(t);
			rc |= appendIdigit(h);
		}

		if(!n) {
			rc |= appendString(ow->hundredWord);
			return rc;
		}

		if(t && n/10) {
			rc |= append3num(n, 0, 0);
		} else {
			rc |= appendIdigit(n/10);
			rc |= appendIdigit(n%10);
		}

		return rc;
	} /* second digit is nonzero */

	rc |= appendIdigit(t);

	if(!n) {
		if(t) rc |= appendString(ow->thousandWord);
		else rc |= appendString("0 0 0");
		return rc;
	} /* ends in 000 */

	rc |= appendIdigit(0);
	rc |= appendIdigit(n/10);
	rc |= appendIdigit(n%10);
	return rc;
} /* appendXxxx */

/* Zero parameters are missing, such as February 16, with no year. */
/* relativeDate will read yesterday as "yesterday", rather than the date. */
static int appendDate(int m, int d, int y, int z)
{
	static int const ndays[] = {0,
	31,29,31,30,31,30,31,31,30,31,30,31};
	static int const ntdays[] = {0,
	0,31,59,90,120,151,181,212,243,273,304,334};
	int rc = 0;

	/* See about reading "yesterday" */
	if(tp_relativeDate &&
	y >= 1970 && y < 2400 &&
	m > 0 && m <= 12 &&
	d > 0 && d <= ndays[m] &&
	(m != 2 || d < 29 || y%4 == 0)) {
		int diff, dayval;
		y -= 1970; /* I'll put it back later */
		dayval = y * 365;
		dayval += (y+1)/4;
		dayval += ntdays[m];
		if(y%4 == 2 && m > 2) ++dayval;
		dayval += d;
		diff = nowday - dayval;
		if(diff >= 0 && diff < 7) { /* within the last week */
			if(diff == 0) rc |= appendString(ow->todayWord);
			else if(diff == 1) rc |= appendString(ow->yesterdayWord);
			else rc |= appendString(ow->weekdays[(dayval+3)%7]);
			return rc;
		} /* within a week */

		/* If the date is close to today, don't read the year. */
		if(diff >= -90 && diff <= 90 &&
		y <= nowyear)
			y = 0;
		else
			y += 1970; /* told you I'd put it back */
	} /* valid date somewhere near the present */

	if(m > 0 && m <= 12) rc |= appendString(ow->months[m-1]);
	if(d > 0 && d < 1000) rc |= appendOrdinal(d);
	/* should we inject a comma here? */
	if(y > 0 && y <= 9999) rc |= appendYear(y);

	if(z && tp_showZones) {
		rc |= appendIchar(',');
		rc |= appendString(ow->zones[z]);
		rc |= appendIchar(',');
	}
	return rc;
} /* appendDate */

/* Negative parameters are unspecified fields. */
static int appendTime(int h, int m, int s, char ampm, int z)
{
	int rc = 0;
	char ampmString[3];

	ampm = toupper(ampm);
	if(h > 12) ampm = 'P';
	if(h == 24 || h == 0) ampm = 'A';
	if(ampm == 'M') { /* military */
		if(h == 12) ampm = 'P'; else ampm = 'A';
	}
	if(h > 12) h -= 12;
	if(h == 0) h = 12;

	/* detect noon and midnight */
	if(m == 0 && h == 12) {
		if(ampm == 'A') { rc |= appendString(ow->midnightWord); goto zoneCheck; }
		if(ampm == 'P') { rc |= appendString(ow->noonWord); goto zoneCheck; }
	} /* 12:00:00 */

	rc |= append3num(h, 0, 0);
	if(m) {
		rc |= appendOX(m);
	} else {
		if(!ampm) rc |= appendString(ow->oclockWord);
	} /* minutes or not */
	if(ampm) {
		ampmString[0] = ampm;
		ampmString[1] = 'm';
		ampmString[2] = 0;
		rc |= appendAcronString(ampmString);
	}

zoneCheck:
	if(z && tp_showZones) {
		rc |= appendString(ow->zones[z]);
		rc |= appendIchar(',');
	}
	return rc;
} /* appendTime */

static int appendFraction(int num, int den, int preand)
{
	int rc = 0;
	if(preand) rc |= appendString(ow->andWord);
	rc |= append3num(num, 1, 1);
	if(den > 1) {
		if(den == 2) rc |= appendString(ow->halfWord);
		else rc |= appendOrdinal(den);
	}
	if(num > 1) { appendBackup(); rc |= appendIchar('s'); }
	return rc;
} /* appendFraction */

int alphaLength(const char *s)
{
	int len = 0;
	while(isalpha(*s)) ++s, ++len;
	return len;
} /* alphaLength */

int atoiLength(const char *s, int len)
{
	int n = 0;
	while(len--) {
		n = 10*n + *s - '0';
		++s;
	}
	return n;
} /* atoiLength */

/* Append "dollars and xx cents" to a money expression.
 * The number of dollars has already been spoken, as a natural number,
 * unless that number is zero.
 * The hard part of this routine is the plurality dollars.
 * You owe me $5.
 * Send me a $5 rebate.
 * The $5 will surely come in handy.
 * The $5 check is in the mail.
 * The subcontractor was given $8.5 million.
 * The subcontractor was awarded an $8.5 million contract.
 * Zeroflag indicates 0 dollars.
 * Oneflag indicates 1 dollar.
 * If the number of cents is negative, cents were not specified. */
static int appendMoney(int zeroflag, int oneflag, int cents, const char *q)
{
	int rc = 0;
	int pluralflag = 1;
	int j, len;
	char c;
	const char *s = q;

	/* Let's dive into the hard part;
	 * figure out if $5 is five dollars. */
	if(*q == ' ') ++q;
	if(isalpha(*q)) {
		/* back up to $ */
		for(; *s != '$'; --s)  ;
		c = *--s;
		if(c == ' ') c = *--s;
		if(c == 0) c = ' ';
		j = -1;
		if(strchr(".!?,:;", (char)c)) j = 2;
		else if(tolower(c) == 's' && s[-1] == '\'') j = 2;
		else if(isalpha(c)) {
			/* back up and check the word */
			len = 1;
			while(isalpha(s[-1])) --s, ++len;
			j = wordInList(ow->articles, s, len);
		} /* prior word */
		if(j >= 0) {
			pluralflag = 0;
			if(j > 1) {
				if(wordInList(ow->verbs, q, alphaLength(q)) >= 0)
					pluralflag = 1;
			} /* preceding article is not "a" or "an" */
		} /* the $5 something */
	} /* word follows money */

	if(zeroflag && cents <= 0)
		rc |= appendIdigit(0);

	/* print dollar or dollars */
	if(!zeroflag || (zeroflag && cents <= 0)) {
		rc |= appendString(~oneflag & pluralflag ? ow->dollarsWord : ow->dollarWord);
		if(cents > 0) rc |= appendString(ow->andWord);
	} /* print dollars */

	if(cents > 0) {
		oneflag = (cents == 1);
		rc |= append3num(cents, 0, 0);
		rc |= appendString(~oneflag & pluralflag ? ow->centsWord : ow->centWord);
	} /* print cents */

	return rc;
} /* appendMoney */


/*********************************************************************
We need to decide whether a word is a native word
or an acronym, which should be read letter by letter.
Begin by storing the 2 and 3 letter words in tables.
These are the words that are "pronunceable", in the native language.
Needless to say, these will be modified
as we support other languages.
*********************************************************************/

static const char realWords2[] =
"adahalamanasatauawaxhalamapaedehelemenbedehemereweyeidifinisithipi\
ofohonorowoxozcodogohojolonosotouhumunupusbymy";

/* is a 2-letter word native? */
static int isWord2(const char *s)
{
	char c1 = tolower(*s);
	char c2 = tolower(s[1]);

	for(s = realWords2; *s; s+=2) {
		if(*s == c1 && s[1] == c2) return 1;
	}

	return 0;
} /* isWord2 */


/*********************************************************************
Here come the 3 letter words.
Note, some of these aren't words at all,
but they should, nonetheless, be pronounced, rather than spelled.
Ron and Tim are perfectly good names.
ROM and SIM are well-known computer acronyms.
TAM is an acronym that I never heard of, but if it comes up
in your line of work, I'm betting you will pronounce it,
rather than saying t.a.m.

The following properties lead us to spell out acronyms:
1. Negative conotations on the word, such as hor or fuk.
2. Word is difficult to pronounce, such as fbi or cia.
3. The word would be a misspelled version of a common word, such as los or lac.

The following properties lead us to pronounce the acronym:
1. The word is easy to pronounce.
2. The acronym contains awkward letters such as h and w.
3. The acronym contains many letters.

In this table, a > appended to a word means the word is spelled
only when it is in upper case.
If an ^ is appended, we also require a context
of lower case words.
For example, "The ERA almost passed in the Reagan era."
*********************************************************************/

/* Map modified letters back down to their basic lettters. */
	static char const charDowngrade[256+1] =
	"                "
	"                "
	"                "
	"                "
	" abcdefghijklmno"
	"pqrstuvwxyz     "
	" abcdefghijklmno"
	"pqrstuvwxyz     "
	"          s     "
	"          s    y"
	"                "
	"                "
	"aaaaaaa eeeeiiii"
	"dnooooo ouuuuy s"
	"aaaaaaa eeeeiiii"
	" nooooo ouuuuy s";

static const char *const realWords3[] = { "\
abe abs>ace ack^act ada>add ado^ads^adz^\
aft age ago aha>aid ail>aim air ala^ale>\
alf>all alm>alo>alp alt amo^amp amy ana>\
and ani>ann ant any ape app^apt^arc are \
arg>ark arm art ase>ash ask ass ast>ate \
aud>aux ave>awe^awk awl^axe aye aze>azy>", "\
bac>bad baf>bag bah>bak bal>bam ban bar \
bas bat bay bec>bed bee beg bek>bel>ben \
ber^bes^bet bib bic bid bif^big bik>bil>\
bin bio bis>bit bla>bly boa bob boc^boe>\
bog^bok>bom>bon^boo bop>bor^bow box boy \
bra^bry>btw^bub>bud bug bum bun bur bus \
but buy bye ", "\
cab cad caf cal cam can cap car cas cat \
caw>cel cen cob cod cog com con coo cop \
cor cot cow coy cry cub cud^cum>cup cus \
cut cuz^", "\
dab dad dam dan dat>dax^day deb dec ded>\
dee>def>del den deo>det dev dew dib did \
die dig dim din dip dis>dob>doc>doe>dog \
dom>don dos dot dry dub>dud^due dug duh>\
dum>dun duo^dye ", "\
eak>eal>ear eat ebb ebs>eck>ect>eek^eel^\
eep>eke^egg ego elf>elk>elm elp>els>elt>ema>\
emy end eon^era^erg>esh>esk>est>etc eth>\
eve evi>ewe ext>eye ", "\
fab fad fag^fan far fat fax fay feb fed \
fee fer few fez fib fie>fig fin fit fix \
fiz flo flu fly foe fog foo for fox fro \
fry fud^fug>fun fur fus fyi ", "\
gab gad gaf>gag gak>gal gam>gan>gap gar \
gas gat>gay gee>gem gen geo get gig^gin \
gip^git>gnu gob god gon>goo gor got gov \
guf>gum gun gus gut guy gym gyp>", "\
hab hac had haf>hag hak hal ham han>hap \
har>has hat haw>hay hed hee>hem hen her \
hew>hex hey hic hid him hin>hip his hit \
hob hoc hoe hog hon hoo hop hor^hot how \
hub hud hue^hug hum hun hut hyp>", "\
ian>ice ich>ick>icy ids>ied>ier>iff>ike \
ilk>ill ime>imp inc^ine>ink inn ins>int \
ion ips>ire>irk>ise>ish>isk>iso ist>ite>\
ith>its ity>ive>ivy ize>", "\
jab jac jag>jam jan jap jar jaw jay jed \
jem jen jet jew jib jif jig jim jin jip \
job joc joe jog jon jot joy jud jug jut ", "\
kam kan kap kar kat kay ked^keg ken key \
kid kim kin kip kit kob kod kon kop kub \
kut ", "\
lab lac>lad lag^lam lan>lap law lax lay \
lea led^lee leg lei>lem len>les>let lew^\
lex lib lid lie lim lin lip lis>lit liv>\
liz lob^loc log lop^los^lot lou^lox low \
lug lux>lyn ", "\
mac mad mag mak mam^man map mar^mas>mat \
max maw may med meg mel men mes>met mew>\
mex mid mig min mip mir^mis^mit mix mob \
mod mom mop mos>mow mox>muc>mud muf>mug \
mum mus>mut mux ", "\
nab nad>nag nak>nan>nap nas>nay ned nel \
neo>net new nip nit nod non nop nor nos>\
not nov>now nox nub nuc>nuk>num nun nut ", "\
oaf>oak oam>oar oat obe>obs>ock>odd ode^\
ods>off oft oge>ohm oil ola>old ole>oli^\
ome>one ong>onk>ons>oon>oop>ops opt ora \
orb ore org ork>orn>orr>ors>ort>ose>osh>\
ost>ote>oth>oul>our out ova owe owl own \
ows>", "\
pac pad pak pal pam pan par pas>pat paw \
pay pea peb ped>peg pek pel pen pep per \
pet pew phi pic pie pig pik pil pin pip>\
pit ply pod poe poo pop pot pow>pox poy \
pre pro pry pub pug pun pup pur put ", "\
que quo quy ", "\
rac rad rag rak ram ran rap rat raw ray \
raz red rem reo rep rex rev^rew rib rid \
rif>rig ril>rim rin>rip ris>rit>rob roc>\
rod rok>rom ron rop rot row roy roz rub \
rue rug rum run rut rye ", "\
sac>sad sag sal sam san>sap sar>sas>sat \
saw sax say sea sco sed see sel>sep>set \
sew sex sha she shy sic>sid>sil sim sin \
sip sir sis>sit six ski sky sly sob soc \
sod sog son sop^sot>sow sox soy spa spy \
sty sub sue sum sun sup^syn ", "\
tab tac tad^tag tak tal>tam tan tap tar \
tat>tax taz>tea ted tee tel ten tes>tex \
the thy tib>tic tie tif tik>til tim tin \
tip tis^tiz>toc tod>toe tom ton too top \
tor>tos>tot tow tox>toy try tub tug tum \
tun tut tux two ", "\
ugh^uma>una uni uno ups>urn use ush>", "\
vac vad val>van var>vat vax vet vex via^\
vic vie vik vim vix voc vol von vow vox ", "\
wac wad wag wam wap war was wax wav>way \
web wed wee^wer>wet who^why wic wig win \
wip wix woe^wok^won woo wop>wow wry ", "\
", "\
yac yak yam yan>yap yaw yee yen yes yet \
yon yot you yuk yum yup ", "\
zac zag zam zak zan zap zar zed zen zig \
zip zit>zoo "
};

/* is a 3-letter word native? */
static int isWord3(const char *w)
{
	const char *s = w;
	char c1 = tolower(s[0]);
	char c2 = tolower(s[1]);
	char c3 = tolower(s[2]);
	int lowbit = islower(s[0]) | islower(s[1]) | islower(s[2]);
	short i;

	for(s = realWords3[charDowngrade[(unsigned char)c1]-'a']; *s; s+=4) {
		if(s[1] != c2) continue;
		if(s[2] != c3) continue;

		/* we've got the right 3 letters */
		c1 = s[3];
		if(c1 == ' ') return 1;
		if(lowbit) return 1;
		if(c1 == '>') return 0;
		/* check for lower case context */
		for(i=0; i<10; ++i) {
			c1 = *--w;
			if(!c1) break;
			if(islower(c1)) return 0;
			if(!strchr(" \t.,;:-", (char)c1)) break;
		}
		return 1;
	} /* loop looking for this 3-letter word */

	return 0;
} /* isWord3 */


/*********************************************************************
This table contains the initial 3/4 letter consonent clusters,
in the native language, or in common foreign names such as Schroedinger.
*********************************************************************/

static const char * const icc3[] =  {
	"chl","chr",
	"phl","phr",
	"sch","scl","scr",
	"shr","shw","sph","spl","spr","str",
	"thr","thw",
0};

static const char * const icc4[] =  {
	"schl","schr","schw",
0};

/* Words with apostrophes in them.
 * This list doesn't include 'll words, because the time'll come when
 * you'll realize that almost any word'll take this ending.
 * The 'll ending is stripped off before we analyze the word.
 * Similarly for 's, which usually denotes possession.
 * Thus we don't have to include she's, he's, etc. */
static const char * const apos_words[] = {
	"isn't", "wasn't", "hasn't", "didn't",
	"hadn't", "i've", "you've", "we've", "they've",
	"who've", "who'd", "who're",
	"how've", "how'd", "how're",
	"doesn't", "haven't", "weren't", "mustn't",
	"shouldn't", "couldn't", "wouldn't", "don't",
	"aren't", "ain't", "oughtn't", "mightn't",
	"shan't", "i'm", "can't", "won't",
	"i'd", "you'd", "he'd", "she'd",
	"they'd", "we'd", "it'd",
	"would've", "should've", "could've", "must've",
	"you're", "we're", "they're",
0};


/*********************************************************************
This 32x32 matrix records the reasonable letter pairs in the native language.
Bit 4 means the pair can start a word.
Bit 2 means the pair can appear in the middle of a word.
Bit 1 means the pair can end a word.
For now, the transitionValue() function normalizes
characters by removing umlauts and accent marks.
In other words, a often appears between m and d iff
a umlaut often appears between m and d, as in madchen.
The umlauted letter is treated just like the letter.
This is only an approximation, and it breaks down at the end of words,
where umlauted vowels and n tilde don't appear.
But it will do for now.
This must be carefully reviewed for each language.
*********************************************************************/

static const unsigned char letterPairs[26][32] = {
	{0,7,7,7,4,7,7,0,6,0,3,7,7,7,0,7,0,7,7,7,6,6,7,7,3,6},
	{7,2,0,0,7,0,0,0,7,0,0,6,0,0,7,0,0,6,3,0,6,0,0,0,7,0},
	{7,0,2,0,7,0,0,7,6,0,3,6,0,0,7,0,2,6,3,3,6,0,0,0,7,0},
	{7,0,0,3,7,0,2,0,6,0,0,0,0,0,7,0,0,6,3,0,6,0,0,0,7,0},
	{7,7,7,7,7,7,7,0,6,0,7,7,7,7,2,7,6,7,7,7,4,6,3,7,3,0},
	{7,0,0,0,7,3,0,0,6,0,0,6,0,0,7,0,0,6,0,3,6,0,0,0,1,0},
	{7,0,0,0,7,0,2,3,7,0,0,6,0,0,7,0,0,6,3,0,6,0,0,0,7,0},
	{7,0,0,0,7,0,0,0,6,0,0,0,0,0,7,0,0,2,0,3,6,0,0,0,7,0},
	{3,3,7,7,3,7,7,0,0,0,7,7,7,7,7,7,2,7,7,7,0,6,0,3,0,3},
	{6,0,0,0,6,0,0,0,6,0,0,0,0,0,6,0,0,0,0,0,6,0,0,0,0,0},
	{7,0,0,0,7,0,0,0,7,0,0,0,0,6,7,0,0,0,7,0,6,0,0,0,1,0},
	{7,0,0,3,7,3,0,0,6,0,1,3,3,0,7,3,0,0,3,3,6,2,0,0,7,0},
	{7,3,0,0,7,0,0,0,7,0,0,0,2,0,7,3,0,0,3,0,6,0,0,0,7,0},
	{7,0,2,3,7,2,3,0,7,0,3,0,0,2,7,0,0,0,3,3,6,0,0,0,3,0},
	{6,7,7,7,3,7,7,7,7,0,7,7,7,7,6,7,0,7,7,7,6,6,7,7,3,0},
	{7,0,0,0,7,0,0,7,7,0,0,6,0,0,7,2,0,6,7,3,6,0,0,0,7,0},
	{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,6,0,0,0,0,0},
	{7,3,3,3,7,3,3,6,7,0,3,3,3,3,7,3,0,2,3,3,6,2,0,0,7,0},
	{7,0,7,0,7,0,0,7,7,0,7,6,6,6,7,7,6,0,3,7,6,0,4,0,7,0},
	{7,0,0,0,7,0,0,7,7,0,0,2,0,0,7,0,0,6,3,2,6,0,4,0,7,0},
	{2,3,3,3,3,3,3,0,2,0,0,7,7,7,0,7,0,7,7,7,0,0,0,3,0,3},
	{7,0,0,0,7,0,0,0,6,0,0,0,0,0,7,0,0,0,0,0,0,0,0,0,1,0},
	{7,0,0,3,7,0,0,6,6,0,0,3,0,3,6,0,0,4,3,2,0,0,0,0,0,0},
	{3,0,2,0,3,0,0,0,3,0,0,0,0,0,2,2,0,0,0,3,0,0,0,0,1,0},
	{7,0,0,0,7,0,2,0,2,0,0,0,3,3,7,0,0,0,3,2,0,0,0,0,0,0},
	{7,0,0,0,7,0,0,0,6,0,0,0,0,0,7,0,0,0,0,0,0,0,0,0,7,3},
};

static unsigned char transitionValue(char x, char y)
{
	x = charDowngrade[(unsigned char)x];
	y = charDowngrade[(unsigned char)y];
	/* paranoia */
	if(x == ' ' || y == ' ') return 0;
	return letterPairs[x-'a'][y-'a'];
} /* transitionValue */


/*********************************************************************
Do the first 2 or 3 letters obey the above rules?
This is used to analyze login@domain.  For instance,
pronounce mjordan@basketbal.com as m jordan, since mj doesn't start a word.
*********************************************************************/

static int leadSequence(const char *s, int n)
{
	char c1 = *s;
	char c2 = s[1];
	char c3 = s[2];

	if(!(transitionValue(c1, c2)&4)) return 0;
	if(n == 3 && !(transitionValue(c2, c3)&2)) return 0;
	return 1;
} /* leadSequence */


/*********************************************************************
Analyze a string of letters and apostrophes.
Is it a native word?
The string may have other letters following it.
This happens when we analyze runTow->ogetherWords.
We specify its length, rather than assuming it is null terminated.
We try to analyze the word no matter its length --
but the calling function will not acronize any word longer than 6 letters.
Nobody wants to hear that many letters in a row.
Garbage is garbage -- you'll get through it faster by pronouncing it.
Therefore, this routine only HAS to work for words up to 6 letters.
The 2 and 3 letter cases are table driven,
and there's some heuristics for the 4 letter words.
Longer words use the transition matrix above.
This has lots of English assumptions in it.
*********************************************************************/

static int isPronounceable(const char *s, int len)
{
	int i, cnt;
	char c1, c2, c3;
	const char *s0;

	/* check for words with apostrophes in them */
	c1 = c2 = 0;
	for(i=1; i<len; ++i)
		if(s[i] == '\'') { ++c1, c2 = i; }
	if(!c1) goto no_apos;
	if(c1 > 1) return 0;
	i = c2;
	if(i == len-2 &&
	tolower(s[len-1]) == 's') {
		/* analyze the word without its trailing 's */
		len -= 2;
		goto no_apos;
	}
	if(i == len-3 &&
	tolower(s[len-1]) == 'l' &&
	tolower(s[len-2]) == 'l') {
		/* analyze the word without its trailing 'll */
		len -= 3;
		goto no_apos;
	}
	if(i == 1 && tolower(s[0]) == 'o') {
		/* analyze the word without its leading o' */
		len -= 2;
		s += 2;
		goto no_apos;
	}

	/* word must be in the list to be pronunceable */
return wordInList(apos_words, s, len) >= 0;
	no_apos:

	s0 = s; /* save */
	if(len == 1) return 1;

	if(len == 2) return isWord2(s);

	/* Simple vowel check */
	c1 = 0;
	for(i=cnt=0; i<len; ++i) {
		if(!isvowel(s[i])) continue;
		if(!cnt) c1 = i;
		++cnt;
	}
	if(!cnt) return 0; /* no vowels */

	/* If it's a name, try to say it anyways */
	if(isupper(s[0]) && islower(s[1])) {
		if((len == 3 && islower(s[2])) ||
		(len > 3 && islower(s[3]))) return 1; /* name */
	} /* upper lower */

	if(len == 3) return isWord3(s);

	/* Some special checks on 4 letter words */
	/* This is very English. */
	if(len == 4) {
		if(c1 == 3) return 0; /* ends in vowel */

		c2 = tolower(s[2]);
		c3 = tolower(s[0]);

		if(!isvowel(c2)) {
			if(tolower(s[3]) == 's') {
				if(strchr("hjvxz", (char)c2)) return 0;
				if(c2 == 's') {
					if((c3 == 'y' || c3 == 'w') &&
					tolower(s[1]) == 'e')
						return 1;
					if(isvowel(c3)) return 0;
					if(c3 >= 'v') return 0;
					goto analyze;
				} /* ??ss */
				if(!isWord3(s)) return 0;
				goto analyze;
			} /* ???s */

			if(c2 == tolower(s[3]) && 
			strchr("cflmnprt", (char)c2) &&
			isvowel(s[1])) {
				if(c3 == c2) return 1;
				if(tolower(s[1]) == 'y') {
					if(c3 == 'y') return 0;
					if(strchr("lmnr", (char)c2)) return 1;
					goto analyze;
				} /* ?ynn */
				if(strchr("bcdfghjklmnprstvwy", (char)c3)) return 1;
				goto analyze;
			} /* ends in vowle double (glide or unvoiced stop) */
		} /* third letter is consonent */
	} /* word has length 4 */

	/* are there too many leading consonents? */
	/* We'll cut you some slack on McGruff. */
	if(tolower(*s) == 'm' && tolower(s[1]) == 'c')
		c1 -= 2, s += 2;
	if(c1 > 4) return 0;
	if(c1 > 2) {
		if(wordInList(c1 == 3 ? icc3 : icc4, s, 0) < 0) return 0;

		/* special case, lots of consonents and only one vowel. */
		if(isSubword((char*)"strength", s) > 0) return 1;
	}

	if(cnt*3 + 5 < len) return 0; /* not enough vowels */
	if(cnt == len) return 0; /* all vowels */

	analyze:
	/* count the invalid transitions */
	s = s0; /* restore from McGruff */
	cnt = 0;
	c1 = tolower(*s);
	++s;
	c2 = tolower(*s);
	i = 3;
	/* Allow Mc at the start of a longer word. */
	if(c1 == 'm' && c2 == 'c' && len >= 5) {
	c1 = *++s;
	c2 = *++s;
		i += 2;
	}
	if(!(transitionValue(c1, c2)&4)) ++cnt;
	for(++s; i<len; ++i, ++s) {
		c1 = c2;
		c2 = *s;
		if(!(transitionValue(c1, c2)&2)) ++cnt;
	}
	c1 = c2;
	c2 = *s;
	if(!(transitionValue(c1, c2)&1)) {
		++cnt;
		if(len == 4) return 0;
	}

	if ((len+2)/3 <= cnt) return 0;
	return 1;
} /* isPronounceable */


/*********************************************************************
Expand a coded construct.
Return 1 on overflow.
Pass back the updated pointer, just after the second code delimiter.
*********************************************************************/

static int expandCode(const char **sp)
{
	const char *start = *sp;
	const char *end;
	const char *t;
	char code = *++start;
	char badcode[12];
	char c;
	int m, d, y; /* for a date */
	int i;
	int zone; /* time zone encoded */

	if(code) ++start;
	for(end = start; *end; ++end)
		if(*end == SP_MARK) break;

	switch(code) {
	case SP_REPEAT:
		/* We wouldn't be here unless tp_readLiteral were 1 */
		c = *start++;
		if(c == ' ') c = 0;
		if(c == '\n') c = SP_MARK;
		speakChar((unsigned char)c, 0, 0, 0);
		if(appendString(shortPhrase)) goto overflow;
		if(appendString(ow->lengthWord)) goto overflow;
		if(append6num(strtol(start, 0, 10))) goto overflow;
		break;

	case SP_LI: /* list item */
		if(start == end) break; /* bullet list, don't say anything */
		c = *start;
		if(isalpha(c)) {
			if(appendIchar(c)) goto overflow;
			t = start + 1;
		} else {
			m = strtol((char*)start, (char**)&t, 10);
			d = t - start;
			if(d > 4 || c == '0') {
				if(appendDigitString(start, d)) goto overflow;
			} else {
				if(appendYear(m)) goto overflow;
			}
		} /* letter or number */
		/* next character is comma or period, */
		/* for quick list items or listed paragraphs. */
		c = *t;
		if(!tp_readLiteral) {
			if(appendIchar(*t)) goto overflow;
		} else {
			speakChar((unsigned char)c, 0, 0, 0);
				if(appendString(shortPhrase)) goto overflow;
		}
		break;

	case SP_DATE:
		m = *start++ - 'A';
		d = *start++ - 'A';
		y = 0;
		if(isdigit(*start)) {
			y = atoiLength(start, 4);
			start += 4;
		}
		zone = 0;
		if(start < end) zone = *start - 'A';
		if(appendDate(m, d, y, zone)) goto overflow;
		t = end+1;
		if(isspace(*t)) ++t;
		if(*t == SP_MARK &&
		t[1] == SP_TIME &&
		appendString(ow->atWord)) goto overflow;
		break;

	case SP_TIME:
		y = *start++ - 'A';
		m = *start++ - 'A';
		/* seconds are neither encoded nore read */
		/* let d hold the am pm indicator */
		d = *start++;
		if(d == '?') d = 0;
		zone = 0;
		if(start < end) zone = *start - 'A';
		if(appendTime(y, m, -1, d, zone)) goto overflow;
	break;

	case SP_PHONE:
		y = atoiLength(start, 3);
		start += 3;
		if(y) {
			if(appendString(ow->areaWord) ||
			appendNxx(y) ||
			appendIchar(',')) goto overflow;
		} /* area code given */
		m = atoiLength(start, 3);
		start += 3;
		d = atoiLength(start, 4);
		start += 4;
		if(appendNxx(m) ||
		appendIchar(',') ||
		appendXxxx(d)) goto overflow;
		d = end - start;
		if(d) { /* extension */
			/* extensions are no more than 6 digits */
			m = atoiLength(start, d);
			if(appendIchar(',') || appendString(ow->extWord)) goto overflow;
			if(d > 4 || *start == '0') {
				if(appendDigitString(start, d)) goto overflow;
			} else {
				if(appendYear(m)) goto overflow;
			}
		} /* extension */
		break;

	case SP_EMOT: /* emoticon */
		if(appendString(ow->emotPhrase[*start - 'A']))
			goto overflow;
			if(!tp_readLiteral && *start == 'D' &&
			appendIchar('!')) goto overflow;
		break;

	case SP_FRAC:
		/* numerator and denominator */
		m = *start++ - 'A';
		d = *start++ - 'A';
		if(code == SP_FRAC) {
			if(appendFraction(m, d, (*start == '&'))) goto overflow;
		} else {
	if(append3num(m, 0, 0) ||
			appendString(ow->slashWord) ||
			append3num(d, 0, 0)) goto overflow;
		}
		break;

	case SP_WDAY:
		d = *start;
		if(islower(d)) { /* abbreviation */
			/* suppress, if followed by date */
			t = end+1;
			if(*t == ' ') ++t;
			if(*t == SP_MARK && t[1] == SP_DATE) break;
		}
		d = toupper(d);
		d -= 'A';
		if(appendString(ow->weekdays[d])) goto overflow;
		t = end+1;
		if(isspace(*t)) ++t;
		if(*t == SP_MARK &&
		t[1] == SP_DATE)
			tp_out->buf[tp_out->len-1] = ',';
		break;

	case SP_STATE:
		if(appendString(ow->states[*start - 'A'])) goto overflow;
		break;

	case SP_BIBLE:
		if(appendString(ow->bible[*start-'0'])) goto overflow;
		++start;
		d = end - start; /* should be a multiple of 3 */
		if(!d) { appendBackup(); break; }
		if(appendString(ow->chapterWord)) goto overflow;
		m = atoiLength(start, 3);
		if(append3num(m, 0, 0)) goto overflow;
		if(d == 3) break;
		start += 3;
		if((d == 6 || d == 12) && appendString(ow->verseWord)) goto overflow;
		if(d == 9 && appendString(ow->versesWord)) goto overflow;
		m = atoiLength(start, 3);
		if(append3num(m, 0, 0)) goto overflow;
		if(d == 6) break;
		start += 3;
		if(appendString(ow->throughWord)) goto overflow;
		if(d == 12 && appendString(ow->chapterWord)) goto overflow;
		m = atoiLength(start, 3);
		if(append3num(m, 0, 0)) goto overflow;
		if(d == 9) break;
		start += 3;
		if(appendString(ow->verseWord)) goto overflow;
		m = atoiLength(start, 3);
		if(append3num(m, 0, 0)) goto overflow;
		break;

	case SP_URL:
		if(!tp_readLiteral) {
			/* prepend a comma, unless there is a flowing keyword */
			zone = 0;
			t = start - 3;
			c = *t;
			if(isspace(c)) c = *--t;
			i = 0;
			while(isalpha(c)) c = *--t, ++i;
			if(i && wordInList(ow->flowInto, t+1, i) >= 0) zone = 1;
			if(!zone && appendIchar(',')) goto overflow;
		}
			c = tolower(*start);
			if(c >= 'm') {
				const char *msg = ow->locUnder;
				c -= 12;
				if(c == 'b') msg = ow->pageUnder;
				if(c == 'h') msg = ow->fileUnder;
				if(appendString(msg)) goto overflow;
			}
			c = *start;
			if(tolower(c) >= 'm') c -= 12;
			/* no need to say web site if we start with www */
			if(c == 'b') break;
			c = tolower(c);
			c -= 'a';
			if(!ow->protocols[c]) break;
			if(appendString(ow->protocols[c])) goto overflow;
		break;

	default:
		if(code) sprintf(badcode, "code %02x", code);
		else strcpy(badcode, "code");
		if(appendString((char*)badcode)) goto overflow;
	} /* switch on code */

	if(*end) ++end;
	*sp = end;
	return 0;

overflow:
	return 1;
} /* expandCode */


/*********************************************************************
Expand a word.  More accurately, a token that starts with a letter.
If the lead character is a digit, expand the number.
Return 1 on overflow.
Pass back the updated pointer, just after the input token.
*********************************************************************/

static int expandAlphaNumeric(char **sp)
{
	char c, d, e, f;
	char *start = *sp, *end; /* bracket the token */
	char *q;
	const char *ur; /* user replacement */
	char *casecut = 0, *comma = 0, *apos = 0;
	int i, j, value;
	int zeroflag, oneflag, hundredflag;
	int rc;

	e = 0; /* quiet gcc */

	c = tolower(*start);
	if(isalpha(c)) goto alphaToken;

	/* Check for 1st 2nd etc. */
	d = start[-1];
	e = start[1];
	end = start + 1;
	i = c - '0';
	if(isdigit(e)) {
		i = 10*i + e-'0';
		e = *++end;
	}
	if(isdigit(e)) {
		i = 10*i + e-'0';
		e = *++end;
	}
	if(isalpha(e) && isalpha(end[1]) &&
	!isalnum(end[2])) {
		e = tolower(e);
		f = tolower(end[1]);
		if((e == 's' && f == 't') ||
		(e == 'n' && f == 'd') ||
		(e == 'r' && f == 'd') ||
		(e == 't' && f == 'h')) {
char g = end[-1];
if(			(g == '0' && e == 't') ||
			(g == '1' && (e == 's' || (end > start+1 && e == 't'))) ||
			(g == '2' && (e == 'n' || (end > start+1 && e == 't'))) ||
			(g == '3' && (e == 'r' || (end > start+1 && e == 't'))) ||
			(g > '3' && e == 't')) {
				end += 2;
				if(appendOrdinal(i)) goto overflow;
				goto success;
			} /* correct ending for a one-digit number */
		} /* ends in st or nd or rd or th */
	} /* 1 or 2 or 3 digits followed by 2 letters */

	/* find the start and end of this number */
	if(d == ',' || d == '.' || isalpha(d) ||
	tp_oneSymbol) comma = start;
	if(d == '-' && isalnum(start[-2])) comma = start;
	if(c == '0') comma = start;
	for(end=start+1; (e = *end); ++end) {
		if(isdigit(e)) continue;
		if(e != ',') break;
		if(!isdigit(end[1])) break;
		if(comma == start) continue;
		if(!comma) { /* first comma */
			comma = end;
			if(comma - start > 3) comma = start;
			continue;
		}
		/* subsequent comma */
		if(end - comma != 4) comma = start;
		else comma = end;
	} /* loop gathering digits and commas */
	if(comma && end - comma != 4) comma = start;

	if(comma && comma > start) {
		if(isalpha(e)) comma = start;
		if(e == '-' && isalnum(end[1])) comma = start;
		if(end - start > 19) comma = start; /* I don't do trillions */
		/* int foo[] = {237,485,193,221}; */
		if(tp_readLiteral && end - start > 7) comma = start;
	}

	/* Bad comma arrangement?  Read each component. */
	if(comma == start) {
		for(q=start; q<end; ++q)
			if(*q == ',') { end = q; break; }
		comma = 0;
	}

	if(end - start > WORDLEN) end = start + WORDLEN;
	e = *end;

	if(comma) {
		zeroflag = oneflag = 1;
		while(start < end) { /* procede by groups of 3 */
			i = 0;
			do { /* gather the next group of 3 */
				i = 10*i + *start - '0';
				++start;
			} while ((end-start) % 4);
			if(i) zeroflag = 0;
			if(start == end && i != 1) oneflag = 0;
			if(start < end && i) oneflag = 0;
			if(append3num(i, 1, 0)) goto overflow;
			if(i && start < end) {
				i = (end-start) / 4 - 1;
				if(appendString(ow->bigNumbers[i])) goto overflow;
			}
			++start;
		} /* loop over groups of 3 */
		if(d == '$') goto money;
		if(zeroflag && appendIdigit(0)) goto overflow;
		goto success;
	} /* comma formatted number */

	/* read 19980502, when part of a filename. */
	if(end-start == 8 && start[6] <= '3' && start[4] <= '1' &&
	((start[0] == '1' && start[1] == '9') || (start[0] == '2' && start[1] == '0'))) {
		if(isalpha(d) || isalpha(e) ||
		(d == '.' && isalnum(start[-2])) ||
		(e == '.' && isalnum(end[1]))) {
			i = atoiLength(start, 4);
			if(appendYear(i)) goto overflow;
			i = atoiLength(start+4, 2);
			if(appendOX(i)) goto overflow;
			i = atoiLength(start+6, 2);
			if(appendOX(i)) goto overflow;
			goto success;
		} /* number part of a larger token */
	} /* 19yymmdd */

	/* If you wanted me to read thousands and millions,
	 * you should have used commas. */
	if(end - start > 4) goto copydigits;

	/* starts with 0, read all the digits */
	if(c == '0' && d != '$') goto copydigits;

	/* read digits after the decimal point */
	if(d == '.') {
		/* Unless we are in the mids of 192.168.9.3 */
		if(e == '.' && isdigit(end[1])) goto copynumber;
		q = start-2;
		if(!isdigit(*q)) goto copydigits;
		do { --q; } while(isdigit(*q));
		if(*q != '.') goto copydigits;
copynumber:
		value = atoiLength(start, end-start);
		if(appendYear(value)) goto overflow;
		if(!value && appendIdigit(0)) goto overflow;
		goto success;
	} /* leading decimal point */

	/* read digits before the decimal point */
	if(e == '.' && d != '$') {
		if(isdigit(end[1])) goto copynumber;
	}

	/* speak digits in coded numbers, such as social security 374-81-6339.
	 * We assume English text, rather than a mathematical formula
	 * such as 374-82-7487 = -7195. */
	if(d == '-' && e == '-') goto copydigits;
	if(d == '-' && isdigit(start[-2])) {
		q = start-3;
		while(isalnum(*q)) {
			if(isalpha(*q)) goto copydigits;
			--q;
		}
		if(*q == '-') goto copydigits;
	}
	if(e == '-' && isdigit(end[1])) {
		q = end+2;
		while(isalnum(*q)) {
			if(isalpha(*q)) goto copydigits;
			++q;
		}
		if(*q == '-') goto copydigits;
	}

	/* speak the number naturally */
	i = end - start;
	value = atoiLength(start, i);
	zeroflag = !value;
	oneflag = (value == 1);
	if(i == 4) {
		if(appendYear(value)) goto overflow;
		if(d == '$') goto money;
		appendBackup();
		goto possessive; /* the 1970's */
	} /* 4 digits */

	/* Do we read 302 as 3 o 2 or 3 hundred 2? */
	/* Apply the latter if we have a word either side: I ate 302 cookies */
	hundredflag = 0;
	if(i == 3 && c != '0') {
		if(d == '$') { hundredflag = 1; goto past3; }
		if(d == '-' || d == '#') goto past3;
		if(e == '-' || e == '\'') goto past3;
		if(isalpha(d) || isalpha(e)) goto past3;
		if(isspace(d) && isalpha(start[-2])) {
			hundredflag = 1;
			/* Unless we find a keyword like room 302 */
			q = start-2;
			i = 0;
			do --q, ++i; while(isalpha(*q));
			if(wordInList(ow->nohundred, ++q, i) >= 0)
				hundredflag = 0;
			goto past3;
		}
		if(isspace(e) && isalpha(end[1])) {
			hundredflag = 1;
		}
	} /* three digits */
past3:

	if(append3num(value, hundredflag, 0)) goto overflow;
	if(d == '$' && !tp_oneSymbol) {
		if(!tp_readLiteral) goto money;
		if(end-start == 3) goto money;
		if(e == '.' && isdigit(end[1])) goto money;
		/* read $3 as dollar three, a positional parameter */
	}
	if(zeroflag && appendIdigit(0)) goto overflow;

	/* check for 4x4 -> four by four */
	if(tolower(e) == 'x' && c != '0') {
		if(isdigit(end[1]) && end[1] != '0') {
			if(appendString(ow->byWord)) goto overflow;
			++end;
			goto success;
		} /* number x number */
	} /* nonzero number, followed by x */

	appendBackup();
	goto possessive;

copydigits:
	if(d == '$' && !tp_readLiteral &&
	appendString(ow->dollarWord)) goto overflow;
	if(appendDigitString(start, end-start)) goto overflow;
	appendBackup();
	goto possessive;

money:
	/* Note that 0, as in $0, has not yet been spoken. */
	/* This allows us to read $0.39 as 39 cents. */
	i = -1;
	/* Check for $4K or $8.5M. */
	if(!comma) {
		int moneySuffix = -1;
		q = end;
		if(*q == '.') {
			++q;
			if(isdigit(*q)) i = *q++ - '0';
			if(isdigit(*q)) i = 10*i + *q++ - '0';
		} /* .xx after the number */
		e = *q;
		if(e == '\n' || e == ' ' || e == '-')
			e = *++q;
		if(isalpha(e)) {
			/* look for the word million */
			j = alphaLength(q);
			/* This is rather unusual; we use bigNumber[]
			 * for both input and output. */
			if(j > 1) {
				moneySuffix = wordInList( ow->bigNumbers, q, j);
			} else if(!isalnum(q[1])) {
				e = toupper(e);
				if(e == 'K') moneySuffix = 0;
				if(e == 'M') moneySuffix = 1;
				if(e == 'B') moneySuffix = 2;
			}
			if(moneySuffix >= 0) {
				q += j;
				if(i > 0) {
					if(appendString(ow->pointWord)) goto overflow;
					if(i >= 10 && appendIdigit(i/10)) goto overflow;
					if(i < 10 && end[1] == '0'&&
					appendString(ow->ohWord)) goto overflow;
					i %= 10;
					if(appendIdigit(i)) goto overflow;
				} /* point xx */
				if(appendString(ow->bigNumbers[moneySuffix])) goto overflow;
				if(appendMoney(0, 0, -1, q)) goto overflow;
				end = q;
				goto success;
			} /* valid money suffix */
		} /* letter follows money */
	} /* no commas in the money number */

	/* determine how many cents are present */
	i = -1;
	if(*end == '.' &&
	isdigit(end[1]) && isdigit(end[2]) &&
	!isdigit(end[3])) {
		i = atoiLength(end+1, 2);
	end += 3;
	} /* .xx follows */
	if(appendMoney(zeroflag, oneflag, i, end)) goto overflow;
	goto success;

alphaToken:
	/* Special case, PH.D.
	 * In this and the next section, we assume a prior phase
	 * has stripped off the last period, unless this word really
	 * marks the end of the sentence.
	 * Thus we compare with "Ph.d", without the last period. */
	if(c == 'p' &&
	isSubword((char*)"ph.d", start) > 0 &&
	!isalnum(start[4])) {
		if(appendAcronString((char*)"phd")) goto overflow;
		end = start + 4;
		goto success;
	}

	/* check for U.S.A etc */
	end = start;
	while(end[1] == '.' && isalpha(end[2]))
		end += 2;
	if(end - start >= 2 && !isalnum(end[1])) {
		if(!tp_readLiteral || end - start > 2 || isupper(*start)) {
			++end;
			/* check for e.g. and i.e. */
			d = tolower(start[2]);
			if(c == 'e' && d == 'g') {
				if(appendString(ow->egWord)) goto overflow;
				goto success;
			}
			if(c == 'i' && d == 'e') {
				if(appendString(ow->ieWord)) goto overflow;
				goto success;
			}
			/* speak each letter */
			for(; start < end; start+=2) {
				c = *start;
				if(tp_acronUpper) c = toupper(c);
				else c = tolower(c);
				if(appendIchar(c)) goto overflow;
				if(start < end-1) tp_out->buf[tp_out->len-1] = tp_acronDelim;
			}
			appendBackup();
			goto possessive;
		} /* reading the word without the dots */
	} /* processing an A.B.C style word */

	/* find the end of the word */
	f = c = *start;
	d = start[-1];
	for(end=start+1; (e = *end); ++end, f=e) {
		if(isalpha(e)) {
			if(case_different(e, f) && !casecut && isalpha(f)) {
				if(isupper(e)) {
					casecut = end;
					/* Don't use casecut for McDonalds. */
					if(end == start+2 &&
					tolower(start[0]) == 'm' && start[1] == 'c')
						casecut = 0;
				} else if(end-start >= 2) {
					casecut = end-1;
					/* Again, some McDonalds code */
					if(end-start == 3 &&
					tolower(start[0]) == 'm' &&
					start[1] == 'c')
						casecut = 0;
				}
			} /* letters have diferent case */
			continue;
		} /* another letter */
		if(e == '\'') {
			if(!apos) apos = end;
			continue;
		}
		break;
	} /* loop finding the end of the word */

	/* strip out trailing apostrophes */
	while(end[-1] == '\'') {
		if(end-start >= 5 && !tp_readLiteral &&
		tolower(end[-2]) == 'n' &&
		tolower(end[-3]) == 'i') {
			f = 'G';
			if(islower(end[-2])) f = 'g';
			end[-1] = f;
			break;
		} else {
			--end;
			if(end == apos) apos = 0;
		}
	}

	/* Strip off 's */
	/* The possessive code will put it back on after translation. */
	if(end-start > 2 && tolower(end[-1]) == 's' && end[-2] == '\'') {
		end -= 2, e = '\'';
		if(apos == end) apos = 0;
	}
	if(end-start > 3 && end[-3] == '\'' &&
	tolower(end[-1]) == 'l' && end[-1] == end[-2]) {
		end -= 3, e = '\'';
		if(apos == end) apos = 0;
	}

	/* check for plural acronym, as in PCs, and back over the final s */
	if(casecut == end-2 && end[-1] == 's' && isupper(end[-3]))
	--end, casecut = 0;

	/* special code for 401Ks */
	if(isdigit(d) && end-start == 2 &&
	isupper(c) && start[1] == 's')
		--end;

	/* Check the entire word, and then the first piece of the
	 * runTow->ogetherWord.  Thus readingRainbow will be
	 * transmuted into reeding rainbow, via read -> reed. */
	while(1) {
		ur = acs_replace_iso(start, end-start);
		if(ur) {
			if(appendString(ur)) goto overflow;
			appendBackup();
			goto possessive;
		} /* user replaced the entire word */
		if(!casecut) break;
		end = casecut;
		casecut = 0;
		if(apos && apos >= end) apos = 0;
	}

	e = *end;
	if(apos) { /* interior apostrophe */
		/* If it's not a native word, or it has yet more apostrophes
		 * around it, read each component. */
		if(d == '\'' || e == '\'' ||
		!isPronounceable(start, end-start))
			end = apos, apos = 0, e = '\'';
	} /* interior apostrophe */

	/* str76at  s t r 7 6 a t */
	if(isdigit(d) && end-start < 3) goto acronym;

	/* A word cannot be too long */
	if(end - start > WORDLEN)
		end = start + WORDLEN, e = *end;

	/* Check for mjordan@domain.com. */
	if(e == '@' && end - start >= 4 && isalpha(end[1])) {
		char leadLetters[3];
		leadLetters[0] = 0;
		if(!leadSequence(start, end-start)) {
			leadLetters[0] = c;
			leadLetters[1] = 0;
			if(!leadSequence(start+1, end-start-1)) {
				leadLetters[1] = start[1];
				leadLetters[2] = 0;
			}
		}
		i = strlen(leadLetters);
		if(i) {
			if(appendAcronString(leadLetters)) goto overflow;
			start += i;
		}
	} /* followed by @ */

	if(isPronounceable(start, end-start)) goto copyword;

	/* Don't bother spelling out long words;
	 * the user hasn't got the time or the patience. */
	i = end - start;
	if(i > 6) goto copyword;

	/* in a hyphenated word such as dis-obedient, the dis doesn't
	 * look like a valid English word, so it gets acronized.
	 * Check for this here, and jump to copyword. */
	if(e == '-' && i <= 3 && i > 1 && isalpha(end[1])) {
		if(leadSequence(start, i))
			goto copyword;
	}

acronym:
	*end = 0;
	rc = appendAcronString(start);
	*end = e;
	if(rc) goto overflow;
	appendBackup();
	goto possessive;

copyword:
	/* copy a word into the buffer, shiftint to lower case */
	for(; start<end; ++start) {
		c = tolower(*start);
		if(appendChar(c)) goto overflow;
	}

possessive:
	/* check for lower s after an acronym,
	 * as in plural PCs. */
	if(e == 's' && !isalnum(end[1]) &&
	isalpha(end[-1])) {
		++end;
		if(appendString("'s")) goto overflow;
		goto success;
	}

	/* check for 's or 'll */
	if(e == '\'' && tolower(end[1]) == 's' && !isalnum(end[2])) {
		end += 2;
		if(appendString("'s")) goto overflow;
		goto success;
	}
	if(e == '\'' && tolower(end[1]) == 'l' && end[1] == end[2] && !isalnum(end[3])) {
		end += 3;
		if(appendString("'ll")) goto overflow;
		goto success;
	}

	appendChar(' ');

success:
	*sp = end;
	return 0;

overflow:
	return 1;
} /* expandAlphaNumeric */


/*********************************************************************
Expand a punctuation mark.
If in literal mode, we simply read it.
Otherwise we may look around for context.
For instance, hyphen may be read as nothing, pause, minus, or dash.
This routine assumes several phases have run before.
Sequences of a repeating punctuation mark should be reduced to one,
or at most two.
The "range" phase should have replaced some of the hyphens with range codes,
as in Nov 21-25.
Return 1 on overflow.
Pass back the updated pointer, just after the punctuation mark,
or passed any additional characters that are swallowed.
*********************************************************************/

static int expandPunct(const char **sp)
{
	char c, d, e;
	const char *s = *sp;
	const char *end = s+1;
	const char *t;
	char spaceAround;
	int len, leftnum, rightnum, leftcode, rightcode, leftlen, rightlen;

	c = s[0];
	d = s[-1];
	e = s[1];

	if(tp_readLiteral) {
		/* Here are the exceptions */
		if(tp_oneSymbol || !strchr(".^$", (char)c)) {
do_punct:
			speakChar((unsigned char)c, 0, 0, 0);
				if(appendString(shortPhrase)) goto overflow;
			goto success;
		}
	}

	switch(c) {
	case '(':
		if(tolower(e) == 's' && s[2] == ')' && isalpha(d)) {
			/* fill out the form(s) */
			appendBackup();
			if(appendIchar('s')) goto overflow;
			++end;
			break;
		}

	case '[': case '{':
		if(isspace(d)) {
			do_comma: if(appendIchar(',')) goto overflow;
		}
		break;

	case ')': case ']': case '}':
		if(isalnum(d) &&
		s[-2] && strchr("([{", s[-2]))
			break; /* the other side of (s) */
		if(d != ' ') goto do_comma;
		break;

	case '"':
		if(isspace(d) && isalnum(e)) {
			/* Which do we hit first, space or quote? */
			for(t=end; (c = *t); ++t) {
				if(c == '"') break;
				if(isspace(c)) break;
			}
		if(isspace(c)) goto do_comma;
		}
		if(isalnum(d) && isspace(e)) {
			for(t=s-1; (c = *t); --t) {
				if(c == '"') break;
				if(isspace(c)) break;
			}
			if(isspace(c)) goto do_comma;
		}
		break;

	case '-':
		/* Turn re-position into re position.
		 * Strange as it may seem, some people use a single -
		 * for a compressed period in a run-on sentence.
		 * Turn word-I into word.I */
		if(isalpha(d) && isalnum(e)) {
			if(e == 'I' && isalpha(s[-2]) && isspace(s[2])) {
				if(appendIchar('.')) goto overflow;
			}
			break;
		}

		/* Turn 10-year-old into 10 year old. */
		if(isdigit(d) && isalpha(e)) break;

		/* -37 becomes minus 37 */
		/* Same for -$37 and -x, but not -word. */
		if(isspace(d) &&
		(isalnum(e) || e == '$')) {
			if(isalpha(e) && isalpha(end[1])) break;
			if(appendString(ow->minusWord)) goto overflow;
			break;
		}

		/* Check for space on either side */
		t = s-1;
		++s;
		spaceAround = 0;
		if(d == ' ') { spaceAround = 2; d = *--t; }
		if(e == ' ') { spaceAround |= 1; e = *++s; }
		leftcode = rightcode = 0;
		if(e == SP_MARK) rightcode = s[1];
		if(d == SP_MARK) {
			while((d = *--t)) if(d == SP_MARK) break;
			leftcode = t[1];
		}
		leftnum = rightnum = -1;
		leftlen = rightlen = 0; /* quiet gcc warning */
		if(isdigit(e) && e != '0') {
			for(len=1; len<6; ++len)
				if(!isdigit(s[len])) break;
			rightlen = len;
			if(len < 6) rightnum = atoiLength(s, len);
			c = s[len];
			if(isalpha(c) || c == '-') rightnum = -1;
		}
		if(isdigit(d)) {
			for(len=1; len<6; ++len)
				if(!isdigit(t[-len])) break;
			leftlen = len;
			if(len < 6) leftnum = atoiLength(t-len+1, len);
			c = t[-len];
			if(isalpha(c) || c == '-') rightnum = -1;
		}
		if(leftcode == rightcode) {
			if(leftcode == SP_WDAY) {
				if(appendString(ow->throughWord)) goto overflow;
				break;
			}
			if(leftcode == SP_DATE || leftcode == SP_TIME) {
do_to_word:
				lastUncomma();
				if(appendString(ow->toWord)) goto overflow;
				break;
			}
		}
		if(leftcode == SP_DATE && rightnum > 0) {
			if(rightnum <= 31) {
				if(appendString(ow->toTheWord)) goto overflow;
				if(appendOrdinal(rightnum)) goto overflow;
				end = s + rightlen;
				break;
			}
			if(rightnum >= 1900 && rightnum < 2100) goto do_to_word;
		}
		if(leftcode == SP_TIME && rightnum > 0 && rightnum <= 12) goto do_to_word;
		if(rightcode == SP_TIME && leftnum > 0 && leftnum <= 12) goto do_to_word;
		if((leftcode == SP_DATE || leftcode == SP_FRAC) &&
		(rightcode == SP_DATE || rightcode == SP_FRAC))
			goto do_to_word;
		if(leftcode|rightcode) goto do_comma;
		if(spaceAround == 2) break;
		if(spaceAround == 1) goto do_comma;
		if(leftnum > 0 && rightnum > leftnum &&
		(rightlen == leftlen ||
		(spaceAround && rightlen == leftlen+1))) goto do_to_word;
		if(spaceAround) goto do_comma;
		if(!isdigit(e)) goto do_comma;
		if(appendString(ow->dashWord)) goto overflow;
		break;

	case '$':
		/* This logic decides whether to read the word dollar.
		 * It is applicable only when tp_readLiteral is 1.
		 * Supress the word "dollar" if we're starting a money amount.
		 * Unfortunately this logic mirrors the logic in
		 * expandAlphaNumeric, which also decides whether a number
		 * is money or not.
		 * This duplicate logic is a bad design,
		 * but right now I can't think of a better one.
		 * First check for $.39 = 39 cents */
		if(e == '.' && isdigit(end[1]) && isdigit(end[2]) &&
		!isalnum(end[3])) {
			if(appendMoney(1, 0,
			atoiLength(end+1, 2), end+3)) goto overflow;
			end += 3;
			break;
		}
		if(!isdigit(e)) goto nomoney;
		for(t=end+1; isdigit(*t); ++t)  ;
		len = t - end;
		if(len > 4) goto nomoney;
		if(len >= 3) break;
		if(!tp_readLiteral) break;
		if(*t == '.' && isdigit(t[1])) break;
		/* Check for comma formatting. */
		if(*t != ',') goto nomoney;
		for((s = ++t); isdigit(*t); ++t)  ;
		if(t-s != 3) goto nomoney;
		if(*t != ',') break;
		if(!isdigit(t[1])) break;
nomoney:
		if(tp_readLiteral) goto do_punct;
		break;

	case '.':
		/* turn . into dot or point */
		if(isdigit(d) && isdigit(e)) {
			/* Usually said as point. */
			/* But not in web addresses like 192.168.10.3 */
			for(t=end+1; isdigit(*t); ++t) ;
			if(*t == '.' && isdigit(t[1])) goto do_dot;
			for(t=s-2; isdigit(*t); --t) ;
			if(*t == '.' && isdigit(t[-1])) goto do_dot;
do_point:
			if(appendString(ow->pointWord)) goto overflow;
			break;
		}
		if(isalnum(d) && isalnum(e)) {
do_dot:
			if(appendString(ow->dotWord)) goto overflow;
			break;
		}
		if(isdigit(e)) goto do_point;
		if(tp_readLiteral) goto do_punct;
		if(!e || isspace(e)) goto copychar;
		break;

	case ';':
		if(isspace(e)) goto do_comma;
		break;

	case '<': case '>': 	case '=':
		t = s-1;
		if(*t == ' ') --t;
		++s;
		if(*s == '=') ++s;
		if(*s == ' ') ++s;
		if(isdigit(*t) || isdigit(*s) ||
		(t[1] == ' ' && s[-1] == ' ')) {
			const char *w = ow->equalsWord;
			if(c == '<') w = ow->lessWord;
			if(c == '>') w = ow->greaterWord;
			if(appendString(w)) goto overflow;
			if(e == '=' && c != '=') {
				if(appendString(ow->oreqWord)) goto overflow;
			}
			end = s;
		}
		break;

	case '%':
		if(isdigit(d) && !isalnum(e)) {
			appendBackup();
			goto copychar;
		}
		break;

		case '@':
		if(isalnum(d) && isalnum(e))
			if(appendString(ow->atWord)) goto overflow;
		break;

		case '#':
		if(isalnum(d)) break;
		if(isdigit(e)) {
			if(appendString(ow->numberWord)) goto overflow;
			break;
		}
		if((e == 's' && !isalnum(s[2])) ||
		(e == '\'' && s[2] == 's' && !isalnum(s[3]))) {
			if(appendString(ow->numbersWord)) goto overflow;
			++end;
			if(e == '\'') ++end;
		}
		break;

	case '/':
		/* this/that becomes this or that */
		if(!isalpha(d)) goto do_slash;
		if(!isalpha(e)) goto do_slash;
		t = s-2;
		s += 2;
		leftnum = rightnum = 1;
		while(isalpha(*t)) --t, ++leftnum;
		while(isalpha(*s)) ++s, ++rightnum;
		if(leftnum == 1 && rightnum == 1) break; /* an A/B switch */
		if(leftnum == 1 || rightnum == 1) goto do_slash;
		d = *t, e = *s;
		if(d && !strchr("\"( \t\n", d)) goto do_slash;
		if(e && !strchr("\") \t\n.?!,;:", e)) goto do_slash;
		if(e == '.' && s[1] && !isspace(s[1])) goto do_slash;
		/* move pointers to the start of the two words */
		s -= rightnum;
		++t;
		/* and/or is already set */
		if(rightnum == 2 && isSubword("or", s) == 2) break;
		if(wordInList(ow->slashOrPhrases, t, leftnum+rightnum+1) >= 0) {
			if(appendString(ow->orWord)) goto overflow;
			break;
		} /* predefined or phrase */
		if(case_different(*s, *t) ||
		case_different(s[rightnum-1], t[leftnum-1]))
			goto do_slash;
		if(leftnum < 4 || rightnum < 4) goto do_slash;
		if(!isPronounceable(t, leftnum)) goto do_slash;
		if(!isPronounceable(s, rightnum)) goto do_slash;
		if(appendString(ow->andWord)) goto overflow;
		break;
do_slash:
		if(appendString(ow->slashWord)) goto overflow;
		break;

	case '^': /* squared cubed etc */
		if(!isalnum(d)) goto noexp;
		if(!isdigit(e)) goto noexp;
		if(e == '0') goto noexp;
		rightnum = e - '0';
		t = end+1;
		e = *t;
		if(isdigit(e)) rightnum = 10*rightnum + e - '0', e = *++t;
		if(isdigit(e)) goto noexp;
		end = t;
		if(rightnum > 3 || rightnum < 2) {
			if(appendString(ow->toTheWord)) goto overflow;
			if(appendOrdinal(rightnum)) goto overflow;
		}
		if(rightnum == 2) {
			if(appendString(ow->squareWord)) goto overflow;
		}
		if(rightnum == 3) {
			if(appendString(ow->cubeWord)) goto overflow;
		}
			break;
noexp:
		if(tp_readLiteral) goto do_punct;
		break;

case ':':
		if(e == '/' || (isalpha(d) && !isalnum(s[-2]))) {
			if(appendString(ow->colonWord)) goto overflow;
			break;
		}
		if(isspace(e)) goto do_comma;
		break;

	case '&':
		/* I hope TTS knows what to do with AT&T etc */
		if(!isalnum(d)) break;
		if(!isalnum(e)) break;
		appendBackup();
		goto copychar;

	case '!':
		if(d == ' ') break;
		if(isalnum(d) && isalnum(e)) {
			if(appendString(ow->bangWord)) goto overflow;
		}
		/* fall through */

	case ',': case '?':
	case '+':
copychar:
		if(appendIchar(c)) goto overflow;
		if(c == '&' && isalnum(e)) appendBackup();
	} /* switch */

success:
	*sp = end;
	return 0;

overflow:
	return 1;
} /* expandPunct */


/*********************************************************************
Expand encoded constructs and render words and numbers.
This is generally the last phase of translation.
Prior phases may remove mail headers or
strip HTML tags; we don't worry about that here.
Another earlier phase (optional) encodes certain structures,
such as dates and times.
If we encounter one of these, we'll render it in standard speech
via expandCode().
If we don't find any encoded constructs, that's ok too.
Other pre-phases are not optional.
We assume there are no control characters,
except those that denote flavors of white space,
as described in another module.
And there should be no binary data.
We assume the calling routine has placed or moved the input text to tp_in,
whence we can write to tp_out.
See tc_textBufSwitch() near the top of this file.
*********************************************************************/

static void expandSentence(void)
{
	char *s;
	char c;
	int overflowValue = 1;

	s = tp_in->buf + 1;

	while((c = *s)) {
		if(c == '\t' && !tp_oneSymbol) c = ' ';
		if(c == ' ') goto nextchar;
		carryOffsetForward(s);
		if(c == '\n' || c == '\7') {
passThrough:
			if(appendChar(c)) goto overflow;
			goto nextchar;
		} /* physical newline or bell */

		if(c == '\f' && !tp_oneSymbol) {
			goto passThrough;
		} /* formfeed */

		if(c == '\n' && !tp_oneSymbol)goto passThrough;

		if(c == SP_MARK) {
			if(expandCode((const char **)&s)) goto overflow;
			continue;
		} /* coded construct */

		if(isalnum(c)) {
			if(expandAlphaNumeric(&s)) goto overflow;
			continue;
		} /* word or number */

		if(expandPunct((const char **)&s)) goto overflow;
		continue;

nextchar:
		++s;
	} /* loop scanning input characters */

	overflowValue = 0;

overflow:
	textbufClose(s, overflowValue);
} /* expandSentence */


/*********************************************************************
Postprocess the text, to get rid of extra blanks
and multiple consecutive commas.
*********************************************************************/

static void postCleanup(void)
{
	char *s, *t;
	acs_ofs_type *u, *v;
	char c, d, e;
	char *w;
	static const char squishable[] = ",;:.?!";
	char presquish = 0, postsquish, insquish;

	s = t = tp_out->buf + 1;
	u = v = tp_out->offset + 1;
	d = '\f';

	for(; (c = *s); ++s, ++u) {
		insquish = postsquish = 0;

		if(ispunct(c)) {
			if(d == ' ') d = *--t, --v;
			if(isspace(d)) continue;
			w = strchr(squishable, (char)c);
			if(w) insquish = w - squishable + 1;
		} /* punctuation */

		e = s[1];
		if(e) {
			w = strchr(squishable, (char)e);
			if(w) postsquish = w - squishable + 1;
		}

		if(c == ' ') {
			if(isspace(d)) continue;
			if(isspace(e)) continue;
			if(postsquish) continue;
			goto add_c;
		} /* space */

		if(isspace(c)) presquish = 0;

		if(presquish && insquish) {
			if(insquish > presquish) {
				t[-1] = d = c;
				v[-1] = *u;
				presquish = insquish;
			}
			continue;
		}

		/* copy the character and the offset */
add_c:
		*t++ = d = c;
		*v++ = *u;
		presquish = insquish;
	} /* loop over chars in tp_out */

	if(d == ' ') --t, --v;
	*t = 0;
	*v = *u;
	tp_out->len = t - tp_out->buf;
} /* postCleanup */


/*********************************************************************
Prepare text for tts.
Run all the phases of translation.
*********************************************************************/

char debugPoint;
static acs_ofs_type end_ofs;
#define debugCheck(c, which) \
if(debugPoint == c) { \
int kk; \
printf("%s", which->buf+1); \
for(kk=1; kk<=which->len; ++kk) if(which->offset[kk]) printf("%d=%d\n", kk, which->offset[kk]); } \
if(end_ofs != which->offset[which->len]) { \
fprintf(stderr, "end offset inconsistency %c.%d.%d length %d\n", \
c, end_ofs, which->offset[which->len], which->len); \
exit(1); \
} \
if(debugPoint == c) exit(0)

void prepTTS(void)
{
	end_ofs = tp_in->offset[tp_in->len];

	debugCheck('a', tp_in);

	/* get ready for the first in->out transformation */
	memset(tp_out->offset, 0, tp_out->room*sizeof(acs_ofs_type));
	tp_out->buf[0] = 0;
	tp_out->len = 1;

	if(!tp_oneSymbol) {
		time_checkpoint();

		/* Get rid of binary data */
		ascify();
		debugCheck('b', tp_in);

#if 0
		/* relinearize .signature block */
/* This code was lost years ago. */
		relinearize();
		debugCheck('c', tp_in);
#endif

#if 0
		/* compress whitespace */
/* don't think we need this any more */
		doWhitespace();
		debugCheck('d', tp_in);
#endif

		/* remove garbage lines */
		ungarbage();
		debugCheck('e', tp_in);

	/* Look for titles */
		titles();
		debugCheck('f', tp_in);

		/* encode list items */
		listItem();
		debugCheck('g', tp_out);
		textBufSwitch();
	} /* tp_oneSymbol */

	/* Encode constructs such as date and time.
	 * There are some word replacements that can take place even when
	 * reading symbol by symbol. */
	if(tp_alnumPrep &&
		(!tp_oneSymbol || isalpha(tp_in->buf[1]))) {
		doEncode();
		debugCheck('h', tp_out);
		textBufSwitch();
	}

	/* translate everything to alphanum text */
	expandSentence();
	debugCheck('y', tp_out);

	/* compress whitespace and sequences of commas and periods */
	postCleanup();
	debugCheck('z', tp_out);
} /* prepTTS */


/* this is for debugging */
/* does not deal with offsets */
char *prepTTSmsg(const char *msg)
{
int len = strlen(msg);
int i;

/* I assume there is room for the message */
tp_in->buf[0] = 0;
strcpy(tp_in->buf+1, msg);
tp_in->len = len+1;

	for(i=1; i<=len+1; ++i)
		tp_in->offset[i] = i;

	prepTTS();

	return tp_out->buf + 1;
} /* prepTTSmsg */

