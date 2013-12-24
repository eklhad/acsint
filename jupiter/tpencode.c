/*********************************************************************

tpencode.c: Encode dates, times, etc for text preprocessing.

At present this entire file isn't being used.

Copyright (C) Karl Dahlke, 2014.
This software may be freely distributed under the GPL, general public license,
as articulated by the Free Software Foundation.
*********************************************************************/

#include "tp.h"


#if 0
/*********************************************************************
See if a line is an unreadable string
of letters and/or digits, such as uuencoded data,
or a mime binary attachment that we didn't detach properly.
A line is garbage if it is sufficiently long,
and has relatively few spaces.
A small amount of text between garbage lines is also excised.
Similarly, short text before or after garbage, up to a paragraph break,
is deleted.
Each block of garbage is replaced with a paragraph break.
*********************************************************************/

/* Squish this many characters before and after garbage. */
#define PREPOSTGARBAGE 40
/* And this many betwee garbage blocks. */
#define BETWEENGARBAGE 120

void ungarbage(void)
{
		char *s, *t, c;
	char *start_line_t, *start_para_t;
		acs_ofs_type *u, *v, of;
	acs_ofs_type *start_line_o, *start_para_o;
	int alphacount, digcount, charcount, length;
	int isgarbage, wasgarbage = 0;

	if(tp_readLiteral) return;
if(tp_in->len < 100) return;

	start_para_t = s = t = tp_in->buf + 1;
	start_para_o = u = v = tp_in->offset + 1;

	while(*s) { /* another line */
		isgarbage = 0;
		start_line_t = t;
		start_line_o = v;
		alphacount = digcount = charcount = length = 0;

		while((of = *u), (c = *s)) {
			char mc = c; /* modified version of c */
			if(mc == 0x8b) mc = '<';
			if(mc == 0x91) mc = '`';
			if(mc == 0x92) mc = '\'';
			if(mc == 0x93) mc = '"';
			if(mc == 0x94) mc = '"';
			if(mc == 0x96) mc = '-';
			if(mc == 0x9b) mc = '>';
			if(mc == 0xa0) mc = ' ';
			*t++ = mc;
			*v++ = of;
			if(c == ',' && isdigit(s[1]) && isdigit(s[-1])) c = ' ';
			++s, ++u, ++length;
			if(c <= ' ') {
				if(c == '\n' || c == '\f') { --length; break; }
				continue;
			}
			++charcount;
			if(c&0x80) continue;
			/* embeded dot or slash, as in a url, looks like a space. */
			if(strchr(".-\\/", (char)c) &&
			isalnum(s[0]) && isalnum(s[-2]))
				--charcount;
			if(isalpha(c)) ++alphacount;
			if(isdigit(c)) ++digcount;
			if(c == ':' &&
			(*s == '/' || *s == '\\') &&
			s[1] == *s &&
			isalpha(s[-2]))
				length += 20;
		} /* loop counting chars in the line */

		/* assess garbageicity */
		if(length >= 20) {
			if(length == charcount) isgarbage = 1; /* no spaces */
			if(charcount*10 >= length*9) { /* few spaces */
				if(length >= 70 || alphacount*2 <= length)
					isgarbage = 1;
			} /* few spaces */
		} /* line is not too short to analyze */

		if(isgarbage) {
			/* back up over this line */
			t = start_line_t, v = start_line_o;
			/* If we're close to the start of the paragraph,
			 * back up to the paragraph boundary */
			if(t-start_para_t <= (wasgarbage ? BETWEENGARBAGE : PREPOSTGARBAGE))
				t = start_para_t, v = start_para_o;
#ifdef __KERNEL__
			/* preserve the cr, for the sounds */
			if(c) *t++ = c, *v++ = of;
#else
			if(t > tp_in->buf + 1)
				t[-1] = '\f', v[-1] = of;
#endif
			start_para_t = t, start_para_o = v;
			wasgarbage = 1;
			if(c == '\f') wasgarbage = 0;
			continue;
		} /* garbage line */

		if(c == 0 || c == '\f') {
			if(wasgarbage && t-start_para_t <= PREPOSTGARBAGE) {
				t = start_para_t, v = start_para_o;
				if(c == '\n')
					*t++ = c, *v++ = of;
			}
			start_para_t = t, start_para_o = v;
			wasgarbage = 0;
		} /* paragraph break */

	} /* loop over lines in the message */

#ifndef __KERNEL__
	/* In case the last line was garbage. */
	if(t[-1] == '\f') --t, --v;
#endif
	*t = 0;
	*v = *u;
	tp_in->len = t - tp_in->buf;
} /* ungarbage */


/*********************************************************************
Look for titles: a sequence of words that are all upper case,
surrounded by mixed case text.
Or, a sequence of capitalized words surrounded by text
that is predominantly lower case.
The state machine that calculates this is of course rather complicated,
taking newlines and periods into account.
For instance, a title must begin with a newline,
though it need not end with a newline.
When a title is identified, separate it out into its own paragraph.

It is best to run this before the encode() function.
Encoded constructs may contain upper or lower case letters,
and this can throw you off if you're not careful.
More important, the encoded construct
doesn't remember whether the original words were upper or lower case,
hence information is lost.
*********************************************************************/

#define CAP_LONG 1
#define CAP_BANNER 3
#define CAP_UPPER 4
#define CAP_MIXED 5
#define CAP_LOWER 6
#define CAP_GARBAGE 7

/* Determine the capitalization status of a line. */
static int capLevel(char **lp)
{
	char *base = *lp;
	char *s = base;
	int length = 0;
	int letters = 0;
	int lowFound = 0; /* lower letters */
	int upFound = 0;
	int lowCount = 0; /* count lower case words (besides the and an a etc) */
	int ambCount = 0; /* lower, but too short */
	int upCount = 0; /* count capital words */
	int firstLow = 0;
	char c, lastchar = 0;
	int adjacent = 0, inword = 0;
	int inAddress = 0;

	for(; (c = *s); ++s, lastchar = c) {
		if(c == '\f' || c == '\n') break;
		if(inAddress) continue;
		if(length == 120) continue;
		++length;

		if(!isalpha(c)) {
			/* Allow U.S.A. to be one word */
			if(inword && c == '.' &&
			isupper(s[-1]) && isupper(s[1]) && s[2] == '.') continue;
			if(c != '\'') inword = 0;
			if((c == '.' || c == '@') &&
			isalpha(s[1]) && isalnum(s[-1])) inAddress = 1;
			continue;
		}

		if(isupper(c)) {
			upFound = 1;
			if(!inword) {
				if(isalpha(s[1])) {
					adjacent = 1;
					if(isalpha(s[2])) ++upCount;
				}
				if(s[1] == '.' && isupper(s[2]) &&
				s[3] == '.') ++upCount;
			}
		} else {
			lowFound = 1;
			if(!firstLow) firstLow = length;
			if(!inword) {
				if(isalpha(s[1]) && isalpha(s[2]) &&
				isalpha(s[3])) ++lowCount;
				else ++ambCount;
			}
		} /* letter or not */

		inword = 1;
		++letters;
	} /* loop over chars in the sentence */

	*lp = s;
	if(inAddress) return CAP_GARBAGE;
	if(length == 120) return CAP_LONG;

	if(!lowFound && letters >= 8 && !adjacent) {
		/* this is an ASCII banner.
		 *  H A P P Y   B I R T H D A Y !!
		 * Squash the spaces out of these letters and return BANNER.
		 * This was originally a readonly routine, so now we need
		 * some convoluted code to move the offsets.  Sigh. */
		char *q, *t;
		acs_ofs_type *q1, *t1;
		q = t = base;
		q1 = t1 = tp_in->offset + (base - tp_in->buf);
		for(; q<s; ++q, ++q1) {
			c = *q;
			*t++ = c;
			*t1++ = *q1;
			if(!isalpha(c)) continue;
			c = q[1];
			if(c == ' ' || c == '-') ++q, ++q1;
			if(c == '\t') *q = ' ';
		}
		while(t < s) *t++ = '\t', *t1++ = 0;
		return CAP_BANNER;
	}

	if(letters*3 < length*2-1) return CAP_GARBAGE;

	if(!lowFound) {
		if(upCount >= 2) return CAP_UPPER;
		if(letters >= 4) return CAP_UPPER;
		return CAP_GARBAGE;
	}

	if(lowCount) {
		if(lastchar == ',') return CAP_GARBAGE;
		/* Lots of words, most lower case. */
		if(lowCount + ambCount + upCount >= 7 &&
		upCount < lowCount + ambCount)
			return CAP_LOWER;
		if(lastchar == '.') lowCount += 2;
		if(upCount*2 >= lowCount+ambCount) return CAP_GARBAGE;
		return CAP_LOWER;
	}

	if(ambCount > 3) return CAP_GARBAGE;
	if(upCount > 4) return CAP_MIXED;
	if(upCount > 2 && (lastchar == '.' || letters >= 16)) return CAP_MIXED;
	if(upCount == 2 && letters >= 20) return CAP_MIXED;
	return CAP_GARBAGE;
} /* capLevel */

/* How many of the previous lines form their own title? */
static int isTitle(int l0, int l1, int l2, int l3)
{
	int rc;
	int prev;
	if(l0 == l1) return 0;
	if(l1 == CAP_LONG || l1 >= CAP_LOWER) return 0;
	rc = 1;
	prev = l2;
	if(l2 == l1) {
		if(l3 == l2) return 0;
		rc = 2;
		prev = l3;
	}
	if(l1 <= CAP_UPPER) return rc;
	/* Mixed title, MUST be bracketed by lower text. */
	if(prev != CAP_LOWER) return 0;
	if(l0 != CAP_LOWER) return 0;
	return rc;
} /* isTitle */

/* Can we separate the title at this point? */
static int canSeparate(char *base, unsigned char type, int length)
{
	char *s, c;

	if(*base == 0 || *base == '\f') return 1;
	/* There's a valid line on either side */

	/* If it is a list item, say no. */
	if(base[1] == 0xb7) return 0;

	if(type == CAP_UPPER) {
		/* Treat a short upper case title with the same
		 * skepticism as a mixed case title. */
		if(length <= 12) type = CAP_MIXED;
	}

	if(type == CAP_MIXED) {
		/* Be careful about separating mixed case titles.
		 * The first word needs to be upper case. */
		if(!isupper(base[1])) return 0;
		/* And the previous line should complete a sentence. */
		s = base-1;
		c = *s;
		/* Skip past closing quote or right paren */
		if(c == '"' || c == ')') c = *--s;
		/* At this point, anything
		 * encoded could end a sentence.
		 * Also, the traditional punctuation marks. */
		if(c && strchr(".?!\200", (char)c)) return 1;
		return 0;
	} /* mixed case */

	return 1;
} /* canSeparate */

/* Separate out titles */
void titles(void)
{
	char *s = tp_in->buf + 1;
	unsigned char l1type = CAP_LOWER;
	unsigned char l2type = 0, l3type = 0;
	char *l1ptr = s-1, *l2ptr = 0, *l3ptr = 0;
	char *start, *end;
	unsigned char cap, rc;

	while(*s) {
		cap = capLevel(&s);
		/* Treat a long line like a line of standard text. */
		if(cap == CAP_LONG) cap = CAP_LOWER;
		rc = isTitle(cap, l1type, l2type, l3type);

		if(rc) {
			end = l1ptr;
			start = l2ptr;
			if(rc == 2) start = l3ptr;
			if(canSeparate(start, l1type, end-start) &
			canSeparate(end, CAP_GARBAGE, 0)) {
				if(*start) *start = '\f';
				if(l1type != CAP_MIXED || isupper(end[1]))
					*end = '\f';
				l1type = CAP_LOWER;
			} /* isolating a title */
		} /* title found */

		if(*s == 0 || *s == '\f') {
			rc = isTitle(CAP_GARBAGE, cap, l1type, l2type);
			if(rc) {
				end = s;
				start = l1ptr;
				if(rc == 2) start = l2ptr;
				if(canSeparate(start, cap, end-start)) {
					if(*start) *start = '\f';
					if(*end) *end = '\f';
					cap = CAP_LOWER;
				} /* isolating a title */
			} /* title found */
		} /* end paragraph */

		if(!*s) break;

		l3type = l2type, l3ptr = l2ptr;
		l2type = l1type, l2ptr = l1ptr;
		l1type = cap, l1ptr = s;
		++s;
	} /* loop through the text */
} /* titles */

/*********************************************************************
Encode list items.
If literal mode is off, adjust either period or comma
after the letter or number,
depending on the size of the list item.
*********************************************************************/

void listItem(void)
{
	const char *s;
	const char *t;
	const char *start;
	char c, d;
	int endpunct;
	char wordbuf[12];
	int last_li = 0;
	int overflowValue = 1;

	wordbuf[0] = SP_MARK;
	wordbuf[1] = SP_LI;
	s = tp_in->buf + 1;

	while((c = *s)) { /* another line */
		endpunct = 0;
		if(c == 0xb7) {
			t = s+1;
			wordbuf[2] = SP_MARK;
			wordbuf[3] = 0;
			goto encoded;
		}

		if(!isalnum(c)) goto copy;
		t = s;
		if(isalpha(c)) c = *++t;
		else while(isdigit(c)) c = *++t;
		if(t - s > 6) goto copy;
		if(c != '.' && c != ':') goto copy;
		c = *++t;
		if(c && !isspace(c)) goto copy;
		strncpy(wordbuf+2, (char*)s, t-s);
		endpunct = 1 + t-s;
		if(!tp_readLiteral) wordbuf[endpunct] = '.';
		wordbuf[endpunct+1] = SP_MARK;
		wordbuf[endpunct+2] = 0;

/* This looks good, but we might be fooled by one of two constructs.
I wrote a letter to John
C. Calhoon, and he told me the answer to life was
42.  I thought it was 54. */

		if(t[-1] == ':') goto encoded;
		d = s[-1];
		if(!d) goto encoded;
		if(d == '\f') goto encoded;
		/* at this point, d should be \r */
		if(!isalpha(s[-2])) goto encoded;
		if(last_li) goto encoded;
		endpunct = 0;
		goto copy;

encoded:
		carryOffsetForward(s);
		if(appendString(wordbuf)) goto overflow;
		appendBackup();
		s = t;

copy:
		start = s;
		while((c = *s)) {
			if(c == 0xb7) c = '.';
			carryOffsetForward(s);
			if(appendChar(c)) goto overflow;
			++s;
			if(c == '\f') break;
			if(c == '\n') break;
		} /* loop copying this line */

		/* revert to comma for a short item */
		if(endpunct && !tp_readLiteral &&
		s - start <= 20)
			tp_out->buf[tp_out->len - (s-start) - 2] = ',';

		last_li = (endpunct > 0);
		if(c == '\f') last_li = 0;
	} /* loop over lines */

	overflowValue = 0;

overflow:
	textbufClose(s, overflowValue);
} /* listItem */


/*********************************************************************
The rest of the routines in this file encode various
(often multi-word) constructs such as dates and times,
so they can be readily recognized by subsequent routines,
and managed as atomic tokens.
For instance, we must often ask the contextual question,
"does a time field come next in the text stream?"
This question use to entail the same awkward code over and over again,
but not quite the same code, since each pass through the message
alters the syntax slightly.
This was terrible programming!
Now that we encode these constructs early on,
other routines can simply look ahead for SP_TIME, and act accordingly.
We can also write a simple time-rendering module to read times in English.
When we adapt this system to Spanish,
we can plug in a Spanish time-reader and go.
The system should be more language independent.

There are many small functions that support this encoding process,
such as zoneCheck, which looks ahead and encodes the time zone.
I use to pass the input and output cursors down through all these
support routines.  By reference of course, so they could
advance both cursors as they made incremental transformations
on behalf of the calling routine.
The code stank!
Now these cursors are global variables, visible to all the routines.
(See the structures tp_in and tp_out)
Some would call this bad programming too, since the support routines now have
"side-effects".  But they had side-effects anyways,
and the trickling down pointer parameters were confusing as hell.

The largest chunk of this effort is the word lookup, which I combine
into one routine.
Thus we look for Wednesday, or Wed, or September, or Sep, or Illinois,
all in one go.
This gives us the opportunity to sort the wordlist
and make the lookup efficient.

Each entry is associated with a particular language.
At sort time, most of these entries are culled,
leaving only the entries that correspond with the setting of acs_lang.
Thus you cannot switch between languages on the fly.
That's ok, you can't change the language of espeak on the fly either.

Some words are language independent, such as
roman numbers, metric units, and the month/weekday abbreviations in computer
time stamps.
These entries are marked with LANG_NONE, and retained
no matter what language is specified.
For now, states and state abbreviations, for the almighty United States,
are retained in every language.
However, we may add code making it more difficult to expand
these abbreviations in other languages.
Troy NY might become Troy New York in English,
but for the same thing to happen in Spanish, you might have to write Troy NY USA.
The international aspects of this program are still in their infancy.
We really don't know what we're doing yet.
*********************************************************************/

struct RESERVED {
	const char *word;
	unsigned char seqno;
	unsigned char lang;
	unsigned short context;
	unsigned short value;
	unsigned short abbrev;
	const char *replace;
};

/* Various contexts to check agains */
#define CX_WORD 1
#define CX_ROMAN 2
#define CX_EG 3
#define CX_WDAY 4
#define CX_MONTH 5
#define CX_NOON 6
#define CX_UNIT 7
#define CX_PREFIX 8
#define CX_VOLUME 9
#define CX_INC 10
#define CX_TM 11
#define CX_APT 12
#define CX_RE 13
#define CX_A_AN 14
#define CX_ADDRESS 15
#define CX_STATE 16
#define CX_BIBLE 17

static struct RESERVED reserved[] = {
/* Words that are replaced, rather than encoded.
 * These are simple substitutions.  When the value is 0,
 * the replacement strings should (ideally) be a single word --  all letters.
 * Thus the single token 42sync53 remains a single token: 42sink53 */
{"ascii", 99, ACS_LANG_EN, CX_WORD, 0, 0, "askey"},
{"mailto", 99, ACS_LANG_EN, CX_WORD, 1, 0, "mailTo"},
{"copyto", 99, ACS_LANG_EN, CX_WORD, 1, 0, "copyTo"},
{"pi", 99, ACS_LANG_EN, CX_WORD, 1, 0, "pie"},
{"pls", 99, ACS_LANG_EN, CX_WORD, 1, 0, "please"},
{"pkg", 99, ACS_LANG_EN, CX_WORD, 1, 0, "package"},
{"mtg", 99, ACS_LANG_EN, CX_WORD, 1, 0, "meeting"},
{"defn", 99, ACS_LANG_EN, CX_WORD, 1, 0, "deffinition"},
{"thru", 99, ACS_LANG_EN, CX_WORD, 0, 0, "through"},
{"attn", 99, ACS_LANG_EN, CX_WORD, 1, 0, "attention"},
{"xpress", 99, ACS_LANG_EN, CX_WORD, 0, 0, "express"},
{"nasdaq", 99, ACS_LANG_EN, CX_WORD, 0, 0, "nazdack"},
{"acme", 99, ACS_LANG_EN, CX_WORD, 1, 0, "ackmey"},
{"mapi", 99, ACS_LANG_EN, CX_WORD, 0, 0, "mappy"},
{"tapi", 99, ACS_LANG_EN, CX_WORD, 0, 0, "tappy"},
{"sapi", 99, ACS_LANG_EN, CX_WORD, 0, 0, "sappy"},
{"wav", 99, ACS_LANG_EN, CX_WORD, 0, 0, "wave"},
{"email", 99, ACS_LANG_EN, CX_WORD, 0, 0, "eemail"},
{"etc", 99, ACS_LANG_EN, CX_WORD, 0, 0, "etcetera"},
{"dept", 99, ACS_LANG_EN, CX_WORD, 1, 1, "department"},
{"ext", 99, ACS_LANG_EN, CX_WORD, 1, 1, "extension"},
{"bldg", 99, ACS_LANG_EN, CX_WORD, 1, 0, "building"},
{"blvd", 99, ACS_LANG_EN, CX_WORD, 1, 0, "bullevard"},
{"fyi", 99, ACS_LANG_EN, CX_WORD, 1, 0, "for your information"},
{"btw", 99, ACS_LANG_EN, CX_WORD, 1, 0, "by the way"},
{"ieee", 99, ACS_LANG_EN, CX_WORD, 1, 0, "i triple e"},
{"asap", 99, ACS_LANG_EN, CX_WORD, 1, 0, "a.s.a.p"},
{"tbd", 99, ACS_LANG_EN, CX_WORD, 1, 0, "to be determined"},
{"faq", 99, ACS_LANG_EN, CX_WORD, 1, 0, "frequently asked question"},
{"faqs", 99, ACS_LANG_EN, CX_WORD, 1, 0, "frequently asked questions"},
/* The following entries are here because our Englishicity rules,
 * in tc_xlate.c, do not acronize them when it should,
 *or vice versa.
 * In other words, these are the "exceptions."
 * In some instances we may simply capitalize the word, so it will be treated
 * as a name, hence left alone by the acronyzing machinery.
 * It will be put back into lower case when fed to the synthesizer. */
{"dewy", 99, ACS_LANG_EN, CX_WORD, 0, 0, "dooey"},
{"debt", 99, ACS_LANG_EN, CX_WORD, 0, 0, "dett"},
{"thai", 99, ACS_LANG_EN, CX_WORD, 0, 0, "tie"},
{"beau", 99, ACS_LANG_EN, CX_WORD, 0, 0, "boe"},
{"israel", 99, ACS_LANG_EN, CX_WORD, 0, 0, "izriall"},
{"damn", 99, ACS_LANG_EN, CX_WORD, 0, 0, "dam"},
{"sydney", 99, ACS_LANG_EN, CX_WORD, 0, 0, "sidney"},
{"nozzle", 99, ACS_LANG_EN, CX_WORD, 0, 0, "nossle"},
{"zinc", 99, ACS_LANG_EN, CX_WORD, 0, 0, "zink"},
{"widths", 99, ACS_LANG_EN, CX_WORD, 0, 0, "widthss"},
{"lieu", 99, ACS_LANG_EN, CX_WORD, 0, 0, "lew"},
{"dwarfs", 99, ACS_LANG_EN, CX_WORD, 0, 0, "dwarfss"},
{"hawk", 99, ACS_LANG_EN, CX_WORD, 0, 0, "halk"},
{"gawk", 99, ACS_LANG_EN, CX_WORD, 0, 0, "galk"},
{"kbytes", 99, ACS_LANG_EN, CX_WORD, 0, 0, "killaBites"},
{"mbytes", 99, ACS_LANG_EN, CX_WORD, 0, 0, "megaBites"},
{"kiwi", 99, ACS_LANG_EN, CX_WORD, 0, 0, "keewey"},
{"unwrap", 99, ACS_LANG_EN, CX_WORD, 0, 0, "unrap"},
{"maui", 99, ACS_LANG_EN, CX_WORD, 0, 0, "Maui"},
{"yoyo", 99, ACS_LANG_EN, CX_WORD, 0, 0, "Yoyo"},
{"sync", 99, ACS_LANG_EN, CX_WORD, 0, 0, "sink"},
{"sign", 99, ACS_LANG_EN, CX_WORD, 0, 0, "sine"},
{"john", 99, ACS_LANG_EN, CX_WORD, 0, 0, "John"},
{"ezra", 99, ACS_LANG_EN, CX_WORD, 0, 0, "Ezra"},
{"peru", 99, ACS_LANG_EN, CX_WORD, 0, 0, "perroo"},
{"menu", 99, ACS_LANG_EN, CX_WORD, 0, 0, "Menu"},
{"guru", 99, ACS_LANG_EN, CX_WORD, 0, 0, "Goo Roo"},
{"thre", 99, ACS_LANG_EN, CX_WORD, 0, 0, "three"},
{"banjo", 99, ACS_LANG_EN, CX_WORD, 0, 0, "banjoe"},
/* multi-word tokens */
{"at&t", 99, ACS_LANG_EN, CX_WORD, 1, 0, "a t and t"},
{"it&t", 99, ACS_LANG_EN, CX_WORD, 1, 0, "i t and t"},
{"po box", 99, ACS_LANG_EN, CX_WORD, 1, 0, "poBox"},
{"po-box", 99, ACS_LANG_EN, CX_WORD, 1, 0, "poBox"},
{"sci fi", 99, ACS_LANG_EN, CX_WORD, 1, 0, "scienceFiction"},
{"sci-fi", 99, ACS_LANG_EN, CX_WORD, 1, 0, "scienceFiction"},
{"se ya", 99, ACS_LANG_EN, CX_WORD, 1, 0, "seeYou"},
{"se-ya", 99, ACS_LANG_EN, CX_WORD, 1, 0, "seeYou"},
{"see ya", 99, ACS_LANG_EN, CX_WORD, 1, 0, "seeYou"},
{"see-ya", 99, ACS_LANG_EN, CX_WORD, 1, 0, "seeYou"},
/* How do we tell the TTS engine to say shh, or hmm? */
{"shh", 99, ACS_LANG_EN, CX_WORD, 1, 0, "shshsh"},
{"shhh", 99, ACS_LANG_EN, CX_WORD, 1, 0, "shshsh"},
{"hmm", 99, ACS_LANG_EN, CX_WORD, 1, 0, "hmm"},
{"hmmm", 99, ACS_LANG_EN, CX_WORD, 1, 0, "hmm"},
{"nooo", 99, ACS_LANG_EN, CX_WORD, 1, 0, "no!"},

/* Roman numbers. */
{"i", 0, ACS_LANG_NONE, CX_ROMAN, 1, 0, "1"},
{"ii", 0, ACS_LANG_NONE, CX_ROMAN, 0, 0, "2"},
{"iii", 0, ACS_LANG_NONE, CX_ROMAN, 0, 0, "3"},
{"iv", 0, ACS_LANG_NONE, CX_ROMAN, 0, 0, "4"},
{"v", 0, ACS_LANG_NONE, CX_ROMAN, 1, 0, "5"},
{"vi", 1, ACS_LANG_NONE, CX_ROMAN, 3, 0, "6"},
{"vii", 0, ACS_LANG_NONE, CX_ROMAN, 0, 0, "7"},
{"viii", 0, ACS_LANG_NONE, CX_ROMAN, 0, 0, "8"},
{"ix", 0, ACS_LANG_NONE, CX_ROMAN, 0, 0, "9"},
{"x", 0, ACS_LANG_NONE, CX_ROMAN, 1, 0, "10"},
{"xi", 0, ACS_LANG_NONE, CX_ROMAN, 0, 0, "11"},
{"xii", 0, ACS_LANG_NONE, CX_ROMAN, 0, 0, "12"},
{"xiii", 0, ACS_LANG_NONE, CX_ROMAN, 0, 0, "13"},
{"xiv", 0, ACS_LANG_NONE, CX_ROMAN, 0, 0, "14"},
{"xv", 0, ACS_LANG_NONE, CX_ROMAN, 1, 0, "15"},
{"xvi", 0, ACS_LANG_NONE, CX_ROMAN, 0, 0, "16"},
{"xvii", 0, ACS_LANG_NONE, CX_ROMAN, 0, 0, "17"},
{"xviii", 0, ACS_LANG_NONE, CX_ROMAN, 0, 0, "18"},
{"xix", 0, ACS_LANG_NONE, CX_ROMAN, 0, 0, "19"},

/* use ie or eg rather than i.e. or e.g. */
/* Should these be language independent? */
{"eg", 0, ACS_LANG_EN, CX_EG, 0, 0, "e.g"},
{"ie", 0, ACS_LANG_EN, CX_EG, 0, 0, "i.e"},

/* Days of the week */
{"sunday", 0, ACS_LANG_EN, CX_WDAY, 0, 0, 0},
{"monday", 0, ACS_LANG_EN, CX_WDAY, 1, 0, 0},
{"tuesday", 0, ACS_LANG_EN, CX_WDAY, 2, 0, 0},
{"wednesday", 0, ACS_LANG_EN, CX_WDAY, 3, 0, 0},
{"thursday", 0, ACS_LANG_EN, CX_WDAY, 4, 0, 0},
{"friday", 0, ACS_LANG_EN, CX_WDAY, 5, 0, 0},
{"saturday", 0, ACS_LANG_EN, CX_WDAY, 6, 0, 0},
{"sun", 0, ACS_LANG_NONE, CX_WDAY, 0, 1, 0},
{"mon", 0, ACS_LANG_NONE, CX_WDAY, 1, 1, 0},
{"tue", 0, ACS_LANG_NONE, CX_WDAY, 2, 1, 0},
{"wed", 0, ACS_LANG_NONE, CX_WDAY, 3, 1, 0},
{"thu", 0, ACS_LANG_NONE, CX_WDAY, 4, 1, 0},
{"fri", 0, ACS_LANG_NONE, CX_WDAY, 5, 1, 0},
{"sat", 0, ACS_LANG_NONE, CX_WDAY, 6, 1, 0},

/* Months of the year */
{"january", 0, ACS_LANG_EN, CX_MONTH, 1, 0, 0},
{"february", 0, ACS_LANG_EN, CX_MONTH, 2, 0, 0},
{"febuary", 0, ACS_LANG_EN, CX_MONTH, 2, 0, 0},
{"march", 0, ACS_LANG_EN, CX_MONTH, 3, 2, 0},
{"april", 0, ACS_LANG_EN, CX_MONTH, 4, 0, 0},
{"may", 0, ACS_LANG_EN, CX_MONTH, 5, 2, 0},
{"june", 0, ACS_LANG_EN, CX_MONTH, 6, 2, 0},
{"july", 0, ACS_LANG_EN, CX_MONTH, 7, 2, 0},
{"august", 0, ACS_LANG_EN, CX_MONTH, 8, 0, 0},
{"september", 0, ACS_LANG_EN, CX_MONTH, 9, 0, 0},
{"october", 0, ACS_LANG_EN, CX_MONTH, 10, 0, 0},
{"november", 0, ACS_LANG_EN, CX_MONTH, 11, 0, 0},
{"december", 0, ACS_LANG_EN, CX_MONTH, 12, 0, 0},
{"jan", 0, ACS_LANG_NONE, CX_MONTH, 1, 3, 0},
{"feb", 0, ACS_LANG_NONE, CX_MONTH, 2, 1, 0},
{"mar", 0, ACS_LANG_NONE, CX_MONTH, 3, 3, 0},
{"apr", 0, ACS_LANG_NONE, CX_MONTH, 4, 3, 0},
{"jun", 0, ACS_LANG_NONE, CX_MONTH, 6, 1, 0},
{"jul", 0, ACS_LANG_NONE, CX_MONTH, 7, 1, 0},
{"aug", 0, ACS_LANG_NONE, CX_MONTH, 8, 1, 0},
{"sep", 0, ACS_LANG_NONE, CX_MONTH, 9, 1, 0},
{"sept", 0, ACS_LANG_NONE, CX_MONTH, 9, 1, 0},
{"oct", 0, ACS_LANG_NONE, CX_MONTH, 10, 3, 0},
{"nov", 0, ACS_LANG_NONE, CX_MONTH, 11, 1, 0},
{"dec", 0, ACS_LANG_NONE, CX_MONTH, 12, 3, 0},

/* special time words */
{"noon", 0, ACS_LANG_EN, CX_NOON, 'P', 0, 0},
{"midnight", 0, ACS_LANG_EN, CX_NOON, 'A', 0, 0},

/* Units -- metric and English. */
{"hr", 0, ACS_LANG_EN, CX_UNIT, 1, 0, "hours"},
{"hrs", 0, ACS_LANG_EN, CX_UNIT, 1, 0, "hours"},
{"yr", 0, ACS_LANG_EN, CX_UNIT, 1, 0, "years"},
{"yrs", 0, ACS_LANG_EN, CX_UNIT, 1, 0, "years"},
{"ms", 1, ACS_LANG_EN, CX_UNIT, 1, 0, "miliseconds"},
{"lb", 0, ACS_LANG_EN, CX_UNIT, 1, 0, "pounds"},
{"lbs", 0, ACS_LANG_EN, CX_UNIT, 1, 0, "pounds"},
{"oz", 0, ACS_LANG_EN, CX_UNIT, 1, 0, "ounces"},
{"mg", 0, ACS_LANG_EN, CX_UNIT, 1, 0, "miligrams"},
{"kg", 0, ACS_LANG_EN, CX_UNIT, 1, 0, "kilograms"},
{"kt", 0, ACS_LANG_EN, CX_UNIT, 1, 0, "kilotons"},
{"in", 1, ACS_LANG_EN, CX_UNIT, 10, 0, "inches"},
{"ft", 0, ACS_LANG_EN, CX_UNIT, 3, 0, "feet"},
{"dpi", 0, ACS_LANG_EN, CX_UNIT, 0, 0, "dots per inch"},
{"cm", 0, ACS_LANG_EN, CX_UNIT, 1, 0, "centimeters"},
{"mm", 0, ACS_LANG_EN, CX_UNIT, 1, 0, "milimeters"},
{"km", 0, ACS_LANG_EN, CX_UNIT, 1, 0, "kilometers"},
{"hz", 0, ACS_LANG_EN, CX_UNIT, 0, 0, "hurtz"},
{"khz", 0, ACS_LANG_EN, CX_UNIT, 0, 0, "kilohurtz"},
{"mhz", 0, ACS_LANG_EN, CX_UNIT, 0, 0, "megahurtz"},
{"kb", 0, ACS_LANG_EN, CX_UNIT, 1, 0, "kilobites"},
{"mb", 0, ACS_LANG_EN, CX_UNIT, 1, 0, "megabites"},
{"gal", 2, ACS_LANG_EN, CX_UNIT, 1, 0, "gallons"},
{"qt", 0, ACS_LANG_EN, CX_UNIT, 1, 0, "quarts"},
{"tsp", 0, ACS_LANG_EN, CX_UNIT, 1, 0, "teaspoons"},
{"tbsp", 0, ACS_LANG_EN, CX_UNIT, 1, 0, "tablespoons"},
{"mph", 0, ACS_LANG_EN, CX_UNIT, 0, 0, "miles per hour"},
{"mpg", 0, ACS_LANG_EN, CX_UNIT, 0, 0, "miles per gallon"},

/* Standard prefixes/suffixes. */
{"mr", 0, ACS_LANG_EN, CX_PREFIX, 1, 0, "mister"},
{"mrs", 0, ACS_LANG_EN, CX_PREFIX, 1, 0, "misses"},
{"ms", 3, ACS_LANG_EN, CX_PREFIX, 1, 0, "mizz"},
{"dr", 2, ACS_LANG_EN, CX_PREFIX, 1, 0, "doctor"},
{"drs", 0, ACS_LANG_EN, CX_PREFIX, 1, 0, "doctors"},
{"sis", 0, ACS_LANG_EN, CX_PREFIX, 1, 0, "sister"},
{"rev", 2, ACS_LANG_EN, CX_PREFIX, 1, 0, "rehverend"},
{"st", 2, ACS_LANG_EN, CX_PREFIX, 1, 0, "saint"},
{"jr", 0, ACS_LANG_EN, CX_PREFIX, 0, 0, "junior"},
{"sr", 0, ACS_LANG_EN, CX_PREFIX, 0, 0, "senior"},

/* Sitations. */
{"vol", 0, ACS_LANG_EN, CX_VOLUME, 0, 0, "volume"},
{"no", 0, ACS_LANG_EN, CX_VOLUME, 0, 0, "number"},
{"pg", 0, ACS_LANG_EN, CX_VOLUME, 0, 0, "page"},
{"pgs", 0, ACS_LANG_EN, CX_VOLUME, 0, 0, "page"},
{"pp", 0, ACS_LANG_EN, CX_VOLUME, 1, 0, "page"},

/* company words */
{"co", 2, ACS_LANG_EN, CX_INC, 0, 0, "company"},
{"corp", 0, ACS_LANG_EN, CX_INC, 0, 0, "corporation"},
{"inc", 0, ACS_LANG_EN, CX_INC, 0, 0, "incorporated"},

/* parenthetical notes, such as (tm) */
{"tm", 0, ACS_LANG_EN, CX_TM, 0, 0, 0},
{"c", 0, ACS_LANG_EN, CX_TM, 0, 0, 0},
{"sp", 0, ACS_LANG_EN, CX_TM, 0, 0, 0},

/* words in email headers */
{"re", 0, ACS_LANG_EN, CX_RE, 0, 0, "reply"},
{"cc", 0, ACS_LANG_EN, CX_RE, 0, 0, "copyTo"},
{"fw", 0, ACS_LANG_EN, CX_RE, 0, 0, "forwarded"},
{"fwd", 0, ACS_LANG_EN, CX_RE, 0, 0, "forwarded"},

/* apt -> apartment */
{"apt", 0, ACS_LANG_EN, CX_APT, 0, 0, "apartment"},

/* a $11 check */
{"a", 0, ACS_LANG_EN, CX_A_AN, 0, 0, "an"},

/* Abbreviations commonly found in addresses. */
{"n", 0, ACS_LANG_EN, CX_ADDRESS, 2, 0, "north"},
{"s", 0, ACS_LANG_EN, CX_ADDRESS, 2, 0, "south"},
{"e", 0, ACS_LANG_EN, CX_ADDRESS, 2, 0, "east"},
{"w", 0, ACS_LANG_EN, CX_ADDRESS, 2, 0, "west"},
{"ne", 1, ACS_LANG_EN, CX_ADDRESS, 13, 0, "northeast"},
{"n.e", 0, ACS_LANG_EN, CX_ADDRESS, 13, 0, "northeast"},
{"nw", 0, ACS_LANG_EN, CX_ADDRESS, 13, 0, "northwest"},
{"n.w", 0, ACS_LANG_EN, CX_ADDRESS, 13, 0, "northwest"},
{"se", 0, ACS_LANG_EN, CX_ADDRESS, 13, 0, "southeast"},
{"s.e", 0, ACS_LANG_EN, CX_ADDRESS, 13, 0, "southeast"},
{"sw", 0, ACS_LANG_EN, CX_ADDRESS, 13, 0, "southwest"},
{"s.w", 0, ACS_LANG_EN, CX_ADDRESS, 13, 0, "southwest"},
{"hwy", 0, ACS_LANG_EN, CX_ADDRESS, 13, 0, "highway"},
{"rd", 0, ACS_LANG_EN, CX_ADDRESS, 13, 0, "road"},
{"st", 1, ACS_LANG_EN, CX_ADDRESS, 9, 0, "street"},
{"dr", 1, ACS_LANG_EN, CX_ADDRESS, 13, 0, "drive"},
{"ln", 0, ACS_LANG_EN, CX_ADDRESS, 13, 0, "lane"},
{"ave", 0, ACS_LANG_EN, CX_ADDRESS, 13, 0, "avenue"},
{"ct", 1, ACS_LANG_EN, CX_ADDRESS, 13, 0, "court"},
{"cty", 0, ACS_LANG_EN, CX_ADDRESS, 9, 0, "county"},

/* Words for U.S. states.  Someday we need to generalize this to */
/* Ontario Can, Queensland Au, etc. */
{"alabama", 0, ACS_LANG_NONE, CX_STATE, 1, 0, 0},
{"alaska", 0, ACS_LANG_NONE, CX_STATE, 2, 0, 0},
{"arizona", 0, ACS_LANG_NONE, CX_STATE, 3, 0, 0},
{"arkansas", 0, ACS_LANG_NONE, CX_STATE, 4, 0, 0},
{"california", 0, ACS_LANG_NONE, CX_STATE, 5, 0, 0},
{"colorado", 0, ACS_LANG_NONE, CX_STATE, 6, 0, 0},
{"conneticut", 0, ACS_LANG_NONE, CX_STATE, 7, 0, 0},
{"delaware", 0, ACS_LANG_NONE, CX_STATE, 8, 0, 0},
{"florida", 0, ACS_LANG_NONE, CX_STATE, 9, 0, 0},
{"georgia", 0, ACS_LANG_NONE, CX_STATE, 10, 0, 0},
{"hawaii", 0, ACS_LANG_NONE, CX_STATE, 11, 0, 0},
{"idaho", 0, ACS_LANG_NONE, CX_STATE, 12, 0, 0},
{"illinois", 0, ACS_LANG_NONE, CX_STATE, 13, 0, 0},
{"indiana", 0, ACS_LANG_NONE, CX_STATE, 14, 0, 0},
{"iowa", 0, ACS_LANG_NONE, CX_STATE, 15, 0, 0},
{"kansas", 0, ACS_LANG_NONE, CX_STATE, 16, 0, 0},
{"kentucky", 0, ACS_LANG_NONE, CX_STATE, 17, 0, 0},
{"louisiana", 0, ACS_LANG_NONE, CX_STATE, 18, 0, 0},
{"maine", 0, ACS_LANG_NONE, CX_STATE, 19, 0, 0},
{"maryland", 0, ACS_LANG_NONE, CX_STATE, 20, 0, 0},
{"massachusetts", 0, ACS_LANG_NONE, CX_STATE, 21, 0, 0},
{"michigan", 0, ACS_LANG_NONE, CX_STATE, 22, 0, 0},
{"minnesota", 0, ACS_LANG_NONE, CX_STATE, 23, 0, 0},
{"mississippi", 0, ACS_LANG_NONE, CX_STATE, 24, 0, 0},
{"missouri", 0, ACS_LANG_NONE, CX_STATE, 25, 0, 0},
{"montana", 0, ACS_LANG_NONE, CX_STATE, 26, 0, 0},
{"nebraska", 0, ACS_LANG_NONE, CX_STATE, 27, 0, 0},
{"nevada", 0, ACS_LANG_NONE, CX_STATE, 28, 0, 0},
{"new hampshire", 0, ACS_LANG_NONE, CX_STATE, 29, 0, 0},
{"new jersey", 0, ACS_LANG_NONE, CX_STATE, 30, 0, 0},
{"new mexico", 0, ACS_LANG_NONE, CX_STATE, 31, 0, 0},
{"new york", 0, ACS_LANG_NONE, CX_STATE, 32, 0, 0},
{"north carolina", 0, ACS_LANG_NONE, CX_STATE, 33, 0, 0},
{"north dakota", 0, ACS_LANG_NONE, CX_STATE, 34, 0, 0},
{"ohio", 0, ACS_LANG_NONE, CX_STATE, 35, 0, 0},
{"oklahoma", 0, ACS_LANG_NONE, CX_STATE, 36, 0, 0},
{"oregon", 0, ACS_LANG_NONE, CX_STATE, 37, 0, 0},
{"pennsylvania", 0, ACS_LANG_NONE, CX_STATE, 38, 0, 0},
{"rhode island", 0, ACS_LANG_NONE, CX_STATE, 39, 0, 0},
{"south carolina", 0, ACS_LANG_NONE, CX_STATE, 40, 0, 0},
{"south dakota", 0, ACS_LANG_NONE, CX_STATE, 41, 0, 0},
{"tennessee", 0, ACS_LANG_NONE, CX_STATE, 42, 0, 0},
{"texas", 0, ACS_LANG_NONE, CX_STATE, 43, 0, 0},
{"utah", 0, ACS_LANG_NONE, CX_STATE, 44, 0, 0},
{"vermont", 0, ACS_LANG_NONE, CX_STATE, 45, 0, 0},
{"virginia", 0, ACS_LANG_NONE, CX_STATE, 46, 0, 0},
{"washington", 0, ACS_LANG_NONE, CX_STATE, 47, 0, 0},
#define STATE_WASHINGTON 47
{"wisconsin", 0, ACS_LANG_NONE, CX_STATE, 48, 0, 0},
{"west virginia", 0, ACS_LANG_NONE, CX_STATE, 49, 0, 0},
{"wyoming", 0, ACS_LANG_NONE, CX_STATE, 50, 0, 0},
{"dc", 0, ACS_LANG_NONE, CX_STATE, 51, 0, 0},
{"puerto rico", 0, ACS_LANG_NONE, CX_STATE, 52, 0, 0},
{"virgin islands", 0, ACS_LANG_NONE, CX_STATE, 53, 0, 0},
/* standard and old abbreviations for the states */ 
{"al", 0, ACS_LANG_NONE, CX_STATE, 1, 3, 0},
{"ala", 0, ACS_LANG_NONE, CX_STATE, 1, 1, 0},
{"ak", 0, ACS_LANG_NONE, CX_STATE, 2, 1, 0},
{"az", 0, ACS_LANG_NONE, CX_STATE, 3, 1, 0},
{"ariz", 0, ACS_LANG_NONE, CX_STATE, 3, 1, 0},
{"ar", 0, ACS_LANG_NONE, CX_STATE, 4, 1, 0},
{"ark", 0, ACS_LANG_NONE, CX_STATE, 4, 1, 0},
{"ca", 0, ACS_LANG_NONE, CX_STATE, 5, 1, 0},
{"cal", 0, ACS_LANG_NONE, CX_STATE, 5, 5, 0},
{"calif", 0, ACS_LANG_NONE, CX_STATE, 5, 1, 0},
{"co", 1, ACS_LANG_NONE, CX_STATE, 6, 1, 0},
{"col", 2, ACS_LANG_NONE, CX_STATE, 6, 1, 0},
{"ct", 2, ACS_LANG_NONE, CX_STATE, 7, 1, 0},
{"de", 0, ACS_LANG_NONE, CX_STATE, 8, 1, 0},
{"del", 0, ACS_LANG_NONE, CX_STATE, 8, 5, 0},
{"fl", 0, ACS_LANG_NONE, CX_STATE, 9, 1, 0},
{"fla", 0, ACS_LANG_NONE, CX_STATE, 9, 1, 0},
{"ga", 0, ACS_LANG_NONE, CX_STATE, 10, 1, 0},
{"hi", 0, ACS_LANG_NONE, CX_STATE, 11, 1, 0},
{"id", 0, ACS_LANG_NONE, CX_STATE, 12, 1, 0},
{"ida", 0, ACS_LANG_NONE, CX_STATE, 12, 5, 0},
{"il", 0, ACS_LANG_NONE, CX_STATE, 13, 1, 0},
{"ill", 0, ACS_LANG_NONE, CX_STATE, 13, 1, 0},
{"in", 2, ACS_LANG_NONE, CX_STATE, 14, 1, 0},
{"ia", 0, ACS_LANG_NONE, CX_STATE, 15, 1, 0},
{"ks", 0, ACS_LANG_NONE, CX_STATE, 16, 1, 0},
{"ky", 0, ACS_LANG_NONE, CX_STATE, 17, 1, 0},
{"ken", 0, ACS_LANG_NONE, CX_STATE, 17, 5, 0},
{"la", 0, ACS_LANG_NONE, CX_STATE, 18, 5, 0},
{"me", 0, ACS_LANG_NONE, CX_STATE, 19, 5, 0},
{"md", 0, ACS_LANG_NONE, CX_STATE, 20, 1, 0},
{"ma", 0, ACS_LANG_NONE, CX_STATE, 21, 1, 0},
{"mas", 0, ACS_LANG_NONE, CX_STATE, 21, 1, 0},
{"mass", 0, ACS_LANG_NONE, CX_STATE, 21, 5, 0},
{"mi", 0, ACS_LANG_NONE, CX_STATE, 22, 1, 0},
{"mich", 0, ACS_LANG_NONE, CX_STATE, 22, 1, 0},
{"mn", 0, ACS_LANG_NONE, CX_STATE, 23, 1, 0},
{"min", 0, ACS_LANG_NONE, CX_STATE, 23, 5, 0},
{"ms", 2, ACS_LANG_NONE, CX_STATE, 24, 1, 0},
{"mo", 0, ACS_LANG_NONE, CX_STATE, 25, 1, 0},
{"mt", 2, ACS_LANG_NONE, CX_STATE, 26, 1, 0},
{"ne", 2, ACS_LANG_NONE, CX_STATE, 27, 1, 0},
{"nb", 0, ACS_LANG_NONE, CX_STATE, 27, 1, 0},
{"neb", 0, ACS_LANG_NONE, CX_STATE, 27, 1, 0},
{"nv", 0, ACS_LANG_NONE, CX_STATE, 28, 1, 0},
{"nev", 0, ACS_LANG_NONE, CX_STATE, 28, 1, 0},
{"nh", 0, ACS_LANG_NONE, CX_STATE, 29, 1, 0},
{"n.h", 0, ACS_LANG_NONE, CX_STATE, 29, 3, 0},
{"nj", 0, ACS_LANG_NONE, CX_STATE, 30, 1, 0},
{"n.j", 0, ACS_LANG_NONE, CX_STATE, 30, 3, 0},
{"nm", 0, ACS_LANG_NONE, CX_STATE, 31, 1, 0},
{"n.m", 0, ACS_LANG_NONE, CX_STATE, 31, 3, 0},
{"ny", 0, ACS_LANG_NONE, CX_STATE, 32, 1, 0},
{"n.y", 0, ACS_LANG_NONE, CX_STATE, 32, 3, 0},
{"nc", 0, ACS_LANG_NONE, CX_STATE, 33, 1, 0},
{"n.c", 0, ACS_LANG_NONE, CX_STATE, 33, 3, 0},
{"nd", 0, ACS_LANG_NONE, CX_STATE, 34, 1, 0},
{"n.d", 0, ACS_LANG_NONE, CX_STATE, 34, 3, 0},
{"oh", 0, ACS_LANG_NONE, CX_STATE, 35, 1, 0},
{"ok", 0, ACS_LANG_NONE, CX_STATE, 36, 1, 0},
{"okl", 0, ACS_LANG_NONE, CX_STATE, 36, 1, 0},
{"or", 0, ACS_LANG_NONE, CX_STATE, 37, 1, 0},
{"pa", 0, ACS_LANG_NONE, CX_STATE, 38, 1, 0},
{"pen", 0, ACS_LANG_NONE, CX_STATE, 38, 1, 0},
{"penn", 0, ACS_LANG_NONE, CX_STATE, 38, 1, 0},
{"ri", 0, ACS_LANG_NONE, CX_STATE, 39, 1, 0},
{"r.i", 0, ACS_LANG_NONE, CX_STATE, 39, 3, 0},
{"sc", 0, ACS_LANG_NONE, CX_STATE, 40, 1, 0},
{"s.c", 0, ACS_LANG_NONE, CX_STATE, 40, 3, 0},
{"sd", 0, ACS_LANG_NONE, CX_STATE, 41, 1, 0},
{"s.d", 0, ACS_LANG_NONE, CX_STATE, 41, 3, 0},
{"tn", 0, ACS_LANG_NONE, CX_STATE, 42, 1, 0},
{"ten", 0, ACS_LANG_NONE, CX_STATE, 42, 5, 0},
{"tx", 0, ACS_LANG_NONE, CX_STATE, 43, 1, 0},
{"tex", 0, ACS_LANG_NONE, CX_STATE, 43, 5, 0},
{"ut", 0, ACS_LANG_NONE, CX_STATE, 44, 1, 0},
{"vt", 0, ACS_LANG_NONE, CX_STATE, 45, 1, 0},
{"va", 0, ACS_LANG_NONE, CX_STATE, 46, 1, 0},
{"wa", 0, ACS_LANG_NONE, CX_STATE, 47, 1, 0},
{"wi", 0, ACS_LANG_NONE, CX_STATE, 48, 1, 0},
{"wv", 0, ACS_LANG_NONE, CX_STATE, 49, 1, 0},
{"w.v", 0, ACS_LANG_NONE, CX_STATE, 49, 3, 0},
{"wy", 0, ACS_LANG_NONE, CX_STATE, 50, 1, 0},
{"d.c", 0, ACS_LANG_NONE, CX_STATE, 51, 2, 0},
{"pr", 0, ACS_LANG_NONE, CX_STATE, 52, 1, 0},
{"p.r", 0, ACS_LANG_NONE, CX_STATE, 52, 3, 0},
{"vi", 2, ACS_LANG_NONE, CX_STATE, 53, 5, 0},
{"v.i", 0, ACS_LANG_NONE, CX_STATE, 53, 3, 0},

/* Books of the Bible, old testament. */
{"genesis", 0, ACS_LANG_NONE, CX_BIBLE, 1, 0, 0},
{"exodus", 0, ACS_LANG_NONE, CX_BIBLE, 2, 0, 0},
{"leviticus", 0, ACS_LANG_NONE, CX_BIBLE, 3, 0, 0},
{"numbers", 0, ACS_LANG_NONE, CX_BIBLE, 4, 0, 0},
{"deuteronomy", 0, ACS_LANG_NONE, CX_BIBLE, 5, 0, 0},
{"joshua", 0, ACS_LANG_NONE, CX_BIBLE, 6, 0, 0},
{"judges", 0, ACS_LANG_NONE, CX_BIBLE, 7, 0, 0},
{"ruth", 0, ACS_LANG_NONE, CX_BIBLE, 8, 0, 0},
{"samuel", 0, ACS_LANG_NONE, CX_BIBLE, 9, 5, 0},
{"kings", 0, ACS_LANG_NONE, CX_BIBLE, 11, 5, 0},
{"chronicles", 0, ACS_LANG_NONE, CX_BIBLE, 13, 5, 0},
{"ezra", 1, ACS_LANG_NONE, CX_BIBLE, 15, 0, 0},
{"nehemiah", 0, ACS_LANG_NONE, CX_BIBLE, 16, 0, 0},
{"esther", 0, ACS_LANG_NONE, CX_BIBLE, 17, 0, 0},
{"job", 0, ACS_LANG_NONE, CX_BIBLE, 18, 0, 0},
{"psalms", 0, ACS_LANG_NONE, CX_BIBLE, 19, 0, 0},
{"proverbs", 0, ACS_LANG_NONE, CX_BIBLE, 20, 0, 0},
{"ecclesiastes", 0, ACS_LANG_NONE, CX_BIBLE, 21, 0, 0},
{"solomon", 0, ACS_LANG_NONE, CX_BIBLE, 22, 0, 0},
{"isaiah", 0, ACS_LANG_NONE, CX_BIBLE, 23, 0, 0},
{"jeremiah", 0, ACS_LANG_NONE, CX_BIBLE, 24, 0, 0},
{"lamentations", 0, ACS_LANG_NONE, CX_BIBLE, 25, 0, 0},
{"ezekiel", 0, ACS_LANG_NONE, CX_BIBLE, 26, 0, 0},
{"daniel", 0, ACS_LANG_NONE, CX_BIBLE, 27, 0, 0},
{"hosea", 0, ACS_LANG_NONE, CX_BIBLE, 28, 0, 0},
{"joel", 0, ACS_LANG_NONE, CX_BIBLE, 29, 0, 0},
{"amos", 0, ACS_LANG_NONE, CX_BIBLE, 30, 0, 0},
{"obadiah", 0, ACS_LANG_NONE, CX_BIBLE, 31, 0, 0},
{"jonah", 0, ACS_LANG_NONE, CX_BIBLE, 32, 0, 0},
{"micah", 0, ACS_LANG_NONE, CX_BIBLE, 33, 0, 0},
{"nahum", 0, ACS_LANG_NONE, CX_BIBLE, 34, 0, 0},
{"habakkuk", 0, ACS_LANG_NONE, CX_BIBLE, 35, 0, 0},
{"zephaniah", 0, ACS_LANG_NONE, CX_BIBLE, 36, 0, 0},
{"haggai", 0, ACS_LANG_NONE, CX_BIBLE, 37, 0, 0},
{"zechariah", 0, ACS_LANG_NONE, CX_BIBLE, 38, 0, 0},
{"malachi", 0, ACS_LANG_NONE, CX_BIBLE, 39, 0, 0},
/* Books of the Bible, new testament. */
{"matthew", 0, ACS_LANG_NONE, CX_BIBLE, 40, 0, 0},
{"mark", 0, ACS_LANG_NONE, CX_BIBLE, 41, 0, 0},
{"luke", 0, ACS_LANG_NONE, CX_BIBLE, 42, 0, 0},
/* John defers to 1 John 2 John 3 John -- becomes John without the prefix */
{"acts", 0, ACS_LANG_NONE, CX_BIBLE, 44, 0, 0},
{"romans", 0, ACS_LANG_NONE, CX_BIBLE, 45, 0, 0},
{"corinthians", 0, ACS_LANG_NONE, CX_BIBLE, 46, 5, 0},
{"galatians", 0, ACS_LANG_NONE, CX_BIBLE, 48, 0, 0},
{"ephesians", 0, ACS_LANG_NONE, CX_BIBLE, 49, 0, 0},
{"philippians", 0, ACS_LANG_NONE, CX_BIBLE, 50, 0, 0},
{"colossians", 0, ACS_LANG_NONE, CX_BIBLE, 51, 0, 0},
{"thessalonians", 0, ACS_LANG_NONE, CX_BIBLE, 52, 5, 0},
{"timothy", 0, ACS_LANG_NONE, CX_BIBLE, 54, 5, 0},
{"titus", 0, ACS_LANG_NONE, CX_BIBLE, 56, 0, 0},
{"philemon", 0, ACS_LANG_NONE, CX_BIBLE, 57, 0, 0},
{"hebrews", 0, ACS_LANG_NONE, CX_BIBLE, 58, 0, 0},
{"james", 0, ACS_LANG_NONE, CX_BIBLE, 59, 0, 0},
{"peter", 0, ACS_LANG_NONE, CX_BIBLE, 60, 5, 0},
{"john", 1, ACS_LANG_NONE, CX_BIBLE, 62, 6, 0},
{"jude", 0, ACS_LANG_NONE, CX_BIBLE, 65, 0, 0},
{"revelations", 0, ACS_LANG_NONE, CX_BIBLE, 66, 0, 0},
/* Abbreviations, old testament. */
{"gen", 0, ACS_LANG_NONE, CX_BIBLE, 1, 1, 0},
{"ex", 0, ACS_LANG_NONE, CX_BIBLE, 2, 1, 0},
{"lev", 0, ACS_LANG_NONE, CX_BIBLE, 3, 1, 0},
{"num", 0, ACS_LANG_NONE, CX_BIBLE, 4, 1, 0},
{"dt", 0, ACS_LANG_NONE, CX_BIBLE, 5, 1, 0},
{"josh", 0, ACS_LANG_NONE, CX_BIBLE, 6, 1, 0},
{"jud", 0, ACS_LANG_NONE, CX_BIBLE, 7, 1, 0},
{"sam", 0, ACS_LANG_NONE, CX_BIBLE, 9, 5, 0},
{"ki", 0, ACS_LANG_NONE, CX_BIBLE, 11, 5, 0},
{"chr", 0, ACS_LANG_NONE, CX_BIBLE, 13, 5, 0},
{"neh", 0, ACS_LANG_NONE, CX_BIBLE, 16, 1, 0},
{"est", 0, ACS_LANG_NONE, CX_BIBLE, 17, 1, 0},
{"ps", 0, ACS_LANG_NONE, CX_BIBLE, 19, 1, 0},
{"prov", 0, ACS_LANG_NONE, CX_BIBLE, 20, 1, 0},
{"eccl", 0, ACS_LANG_NONE, CX_BIBLE, 21, 1, 0},
{"song", 0, ACS_LANG_NONE, CX_BIBLE, 22, 1, 0},
{"isa", 0, ACS_LANG_NONE, CX_BIBLE, 23, 1, 0},
{"jer", 0, ACS_LANG_NONE, CX_BIBLE, 24, 1, 0},
{"lam", 0, ACS_LANG_NONE, CX_BIBLE, 25, 1, 0},
{"ezek", 0, ACS_LANG_NONE, CX_BIBLE, 26, 1, 0},
{"dan", 0, ACS_LANG_NONE, CX_BIBLE, 27, 1, 0},
{"hos", 0, ACS_LANG_NONE, CX_BIBLE, 28, 1, 0},
{"obad", 0, ACS_LANG_NONE, CX_BIBLE, 31, 1, 0},
{"jon", 0, ACS_LANG_NONE, CX_BIBLE, 32, 1, 0},
{"mic", 0, ACS_LANG_NONE, CX_BIBLE, 33, 1, 0},
{"nah", 0, ACS_LANG_NONE, CX_BIBLE, 34, 1, 0},
{"hab", 0, ACS_LANG_NONE, CX_BIBLE, 35, 1, 0},
{"zeph", 0, ACS_LANG_NONE, CX_BIBLE, 36, 1, 0},
{"hag", 0, ACS_LANG_NONE, CX_BIBLE, 37, 1, 0},
{"zech", 0, ACS_LANG_NONE, CX_BIBLE, 38, 1, 0},
{"mal", 0, ACS_LANG_NONE, CX_BIBLE, 39, 1, 0},
/* Abbreviations, new testament. */
{"mt", 1, ACS_LANG_NONE, CX_BIBLE, 40, 1, 0},
{"mk", 0, ACS_LANG_NONE, CX_BIBLE, 41, 1, 0},
{"lk", 0, ACS_LANG_NONE, CX_BIBLE, 42, 1, 0},
{"jn", 0, ACS_LANG_NONE, CX_BIBLE, 43, 1, 0},
{"rom", 0, ACS_LANG_NONE, CX_BIBLE, 45, 1, 0},
{"cor", 0, ACS_LANG_NONE, CX_BIBLE, 46, 5, 0},
{"gal", 1, ACS_LANG_NONE, CX_BIBLE, 48, 1, 0},
{"eph", 0, ACS_LANG_NONE, CX_BIBLE, 49, 1, 0},
{"phil", 0, ACS_LANG_NONE, CX_BIBLE, 50, 1, 0},
{"col", 1, ACS_LANG_NONE, CX_BIBLE, 51, 1, 0},
{"th", 0, ACS_LANG_NONE, CX_BIBLE, 52, 5, 0},
{"tim", 0, ACS_LANG_NONE, CX_BIBLE, 54, 5, 0},
{"ti", 0, ACS_LANG_NONE, CX_BIBLE, 56, 1, 0},
{"phile", 0, ACS_LANG_NONE, CX_BIBLE, 57, 1, 0},
{"heb", 0, ACS_LANG_NONE, CX_BIBLE, 58, 1, 0},
{"jas", 0, ACS_LANG_NONE, CX_BIBLE, 59, 1, 0},
{"pet", 0, ACS_LANG_NONE, CX_BIBLE, 60, 5, 0},
{"rev", 1, ACS_LANG_NONE, CX_BIBLE, 66, 1, 0},
/* alternate spellings */
{"revelation", 0, ACS_LANG_NONE, CX_BIBLE, 66, 0, 0},

/* end of list */
{0, 0, 0, 0x4000, 0, 0, 0},
};

static unsigned int nentries;

/* Function used by qsort() to sort entries. */
static int reserved_cmp(const struct RESERVED *s, const struct RESERVED *t)
{
	int rc = (int)s->word[0] - (int)t->word[0];
	if(!rc) {
		rc = strcmp((char*)s->word, (char*)t->word);
		if(!rc) {
			rc = (int)s->seqno - (int)t->seqno;
		}
	}
	return rc;
} /* reserved_cmp */

/* The above is a nice thought, but qsort() isn't in the Linux kernel.
 * Here's a simple bubble sort.
 * I don't think there are so many entries that this is prohibitive. */
void sortReservedWords(void)
{
	struct RESERVED *s, swap;
	int changed = 1;
	int rc;
	int i, j;
	extern void langOutWords(void);

	/* Remove entries that are not part of this language. */
	for(i=j=0; reserved[i].word; ++i) {
		unsigned char lang = reserved[i].lang;
		if(lang && lang != acs_lang) continue;
		reserved[j++] = reserved[i]; /* structure copy */
	}
	reserved[j] = reserved[i];
	nentries = j;

	while(changed) {
		changed = 0;
		for(s=reserved; s[1].word; ++s) {
			rc = reserved_cmp(s, s+1);
			if(rc <= 0) continue;
			changed = 1;
			swap = s[0];
			s[0] = s[1];
			s[1] = swap;
		}
	}

	/* Mark the first instance of each word */
	for(s=reserved; s->word; ++s) {
		if(s == reserved || strcmp((char*)s->word, (char*)s[-1].word))
			s->context |= 0x4000;
	}
} /* sortReservedWords */


/*********************************************************************
Before we start encoding numbers, words, and phrases,
here are the strings that are used for context.
These will have to be changed with each language.
For now, we have only the English versions.
*********************************************************************/

/* Words that precede roman numbers */
static const char *const preRomanWords[] = {
"phase", "vol", "volume", "section",
"scene", "act", "movement",
"chapter", "episode", "page", "step",
0};
/* Words that often precede fractions */
static const char *const preFractionWords[] = {
"and", "up", "down", "than", "with", 0};
/* Words that often follow fractions */
static const char *const postFractionWords[] = {
"a", "an", "of", "the",
"your", "my", "our", "his", "her", "its", "their",
"more", "time", "power", "strength",
"foot", "feet", "ft",
"cup", "teaspoon", "tsp", "tablespoon", "tbsp", "gallon", "gal",
"quart", "qt", "pint",
"pound", "lb", "ounce", "oz", "ton",
"inch", "in", "meter",
0};
/* Words that indicate an extention to a phone number. */
static const char *const extWords[] = {
"x", "ext", "extension", 0};
/* Time zone abbreviations. */
static const char *const zoneWords[] = {
"pst","pdt","mst","mdt","cst","cdt","est","edt",
"hst", "bst", "bdt","gmt","utc", "ut", 0};
/* Record the suffixes that turn numbers into ordinals. */
static const char *const ordSuffixes[] = {
"th", "st", "nd", "rd", 0};
/* month abbreviations */
static const char * const monthAbbrev[] = {
"jan", "feb", "mar", "apr", "may", "jun",
"jul", "aug", "sep", "oct", "nov", "dec", 0};
/* Words that precede a state or city. */
static const char *const prepositions[] = {
"in", "inside", "to", "from", "through", "around", "over", "at", "near", 0};
/* Standard URL protocols. */
static const char *const urlWords[] = { 
"https",
"http", "telnet", "rlogin", "smtp",
"ftp", "gopher", "file", 0};


/*********************************************************************
Support routines for encodeWord() and encodeNumber().
*********************************************************************/

/* Determine the length of the current line */
static int lineLength(const char *s, const char **begin_p, const char **end_p)
{
	const char *t;
	const char *begin;
	const char *end;
	char c;
	acs_ofs_type begin_o, end_o;
	int diff;

	for(t=s; (c = *t); ++t) {
		if(c == '\n') break;
		if(c == '\f') break;
	}
	end = t;
	if(end_p) *end_p = end;

	for(t=s-1; (c = *t); --t) {
		if(c == '\n') break;
		if(c == '\f') break;
	}
	begin = t;
	if(begin_p) *begin_p = begin;

	end_o = tp_in->offset[end - tp_in->buf];
	if(!end_o) goto estimate;
	begin_o = 0;
	if(!*begin) {
		begin_o = tp_in->offset[1];
		if(!begin_o) goto estimate;
	} else {
		begin_o = tp_in->offset[begin - tp_in->buf];
		if(!begin_o) goto estimate;
		++begin_o;
	}
	diff = (int)end_o - (int)begin_o;
	return diff;

estimate:
	return end - begin;
} /* lineLength */

/* Skip past the dot, unless it actually ends the sentence. */
static int skipDot(const char *s)
{
	char c = *s;
	char ws;
	int len1, len2;

	if(c != '.') return 0;
	c = *++s;
	if(c == ')' || c == '"') c = *++s;
	if(!isspace(c)) goto skip; /* no whitespace */
	ws = c;
	if(ws == '\t') goto skip; /* multiple spaces */
	if(ws == '\f') return 0; /* end of paragraph */
	c = *++s; /* look ahead */
	if(isdigit(c)) {
		if(ws == '\n')return 0;
		goto skip;
	}
	/* list item logic will determine period or not */
	if(c == 0xb7) goto skip;
	if(c == SP_MARK) return 0; /* something encoded */
	if(strchr("\"([{", (char)c)) c = *++s;
	if(!isalpha(c)) goto skip;
	if(islower(c)) goto skip;
	/* Next word is upper case, decide based on lines */
	if(ws != '\n') goto skip;
	len1 = lineLength(s-1, 0, 0);
	if(len1 >= 48) return 0;
	len2 = lineLength(s, 0, 0);
	if(len2 >= 48) return 0;
skip:
	return 1;
} /* skipDot */

/* Check for time zone indicators after a date or time field.
 * These include +0600 -0500 cdt edt PST GMT etc.
 * If one is found, advance the input cursor past it,
 * and return a number that encodes the timezone. */
static int zoneCheck(const char **sp)
{
	const char *s = *sp;
	char c = *s;
	int z = 0;
	int paren = 0;
	int gmt = 0;
int l, z1;

	if(isspace(c) && c != '\f') c = *++s;
	if(c == '(') { c = *++s; paren = 1; }
	if(acs_substring_mix((char*)"gmt", s) > 0 &&
	(s[3] == '+' || s[3] == '-')) {
		gmt = 1;
		s += 3;
		c = *s;
	}
	if((c == '+' || c == '-') &&
	isdigit(s[1]) && isdigit(s[2]) &&
	isdigit(s[3+gmt]) && isdigit(s[4+gmt]) &&
	!isdigit(s[5+gmt])) {
		/* +0600, timezone is given numerically. */
		/* It's not worth reading. */
		s += 5+gmt;
		*sp = s;
	} else paren = 0;

	s = *sp;
	c = *s;
	if(paren && c == ')') c = *++s, paren = 0;
	if(isspace(c) && c != '\f') c = *++s;
	if(c == ',') c = *++s;
	if(isspace(c) && c != '\f') c = *++s;
	if(!paren && c == '(') { c = *++s; paren = 1; }

	if(isalpha(c) &&
	(z1 = (wordInList(zoneWords, s, 0) + 1)) &&
	!isalnum(s[l = strlen(zoneWords[z1-1])])) {
		z = z1;
		s += l;
		if(paren && *s == ')') ++s;
		*sp = s;
	}

	return z;
} /* zoneCheck */


/*********************************************************************
Look up the word in the list of reserved words,
using binary search.
Then back up to the first instance of that word.
Some appear many times.
MS might be a disease, a prefix to a name,
a state, or a unit of time.
Each instance should have a different context flag.
Use this context indicator to see if the word should be
expanded in that way.
Return the length of the word or phrase that was matched,
or 0 if no match was found.
Return -1 if the output buffer overflowed.
*********************************************************************/

static int encodeWord(const char *s)
{
	struct RESERVED *r = 0;
	int month, day, year;
	int yd; /* digits in year field */
	int z; /* time zone */
	int length = 0, v = 0;
	int i, j, left, right; /* binary search */
	char c, d, e;
	const char *q, *w;
	char *t;
	char wordbuf[32];
	int checkDot, iscap;
	int precity, precomma, postzip;

	/* Binary search. */
	left = -1, right = nentries;
	while(right - left > 1) {
		i = (left+right+1) / 2;
		r = reserved + i;
		v = (int)tolower(*s) - (int)r->word[0];
		if(v < 0) goto less;
		if(v > 0) goto greater;
		for(j=1; r->word[j]; ++j) {
			c = tolower(s[j] );
			v = (int)c - (int)r->word[j];
			if(v < 0) goto less;
			if(v > 0) goto greater;
		}
		v = 1;
		c = s[j];
		if(isalpha(c)) goto greater;
		/* special code to key on letter dot letter */
		if(j == 1 && c == '.' && isalpha(s[j+1]) &&
		!isalpha(s[j+2])) goto greater;
		v = 0;
		length = j;
		break;
		less: right = i; continue;
		greater: left = i;
	}
	if(v) return 0;

	while(!(r->context&0x4000)) --r;

	c = 0; /* quiet gcc warning */
	d = s[-1];
	if(!d) d = '\f';
	e = s[length];
	if(!e) e = '\f';

	/* url checks are done somewhere else */
	if(e == ':' && s[length+1] == '/' && s[length+2] == '/' &&
	isalnum(s[length+3]))
		return 0;

	do {
		unsigned short cx = r->context&0x3fff;
		checkDot = 0;
		v = r->value;

		if(cx != CX_WORD || v == 1) {
			if(tp_oneSymbol) continue;
			/* should not be part of a url or filename etc */
			if(isdigit(e)) continue;
			if(isdigit(d) && cx != CX_UNIT) continue;
			if(cx != CX_NOON) {
				if(strchr("/\\.@-_;:", (char)e) &&
				isalnum(s[length+1])) continue;
				if(strchr("/\\.@-_;:", (char)d) &&
				isalnum(s[-2])) continue;
			}
		} /* something other than a simple word substitution */

		switch(cx) {
			extern const char *const *articles;

		case CX_WORD:
			checkDot = r->abbrev;
			break;

		case CX_ROMAN:
			checkDot = 1;
			/* v&2 -- upper case following lower case */
			if(v&2 && isupper(c) &&
			(d == ' ' || d == '\t') &&
			islower(s[-2])) break;
			if(!v) break;
			/* Now we look for page I or section V or steps I through IV. */
			if(d != ' ') continue;
			q = s - 2;
			if(*q == 's') --q;
			i = 0;
			while(isalpha(*q)) --q, ++i;
			if(i < 3) continue;
			++q;
			if(wordInList(preRomanWords, q, i) >= 0) break;
			continue;

		case CX_EG:
			checkDot = 1;
			/* People now use IE for Internet Explorer.
			 * Make sure we're lower case. */
			if(isupper(s[0])) continue;
			if(isupper(s[1])) continue;
			q = s;
			c = *--q;
			if(c == ' ') c = *--q;
			if(c == ',' || c == '(') break;
			if(e == ',' && (!c || isspace(c))) break;
			continue;

		case CX_WDAY:
			checkDot = r->abbrev;
			if(!r->abbrev) goto doWday;
			iscap = 0;
			if(isupper(s[0]) && islower(s[1])) iscap = 1;
			/* Capital abbreviation is almost good enough, except
			 * for that damn company Sun Microsystems. */
			if(iscap && v) goto doWday;
			if(v && v != 3 && v < 6) goto doWday;
			/* look at the next token */
			q = s + length;
			while((c = *q) && strchr(",- \t", (char)c)) ++q;
			if(isdigit(c)) goto doWday;
			if(!isalpha(c)) continue;
			if(wordInList(monthAbbrev, q, 0) >= 0) goto doWday;
			continue;
doWday:
			sprintf(wordbuf, "%c%c%c%c",
			SP_MARK, SP_WDAY,
			v + (r->abbrev ? 'a' : 'A'), SP_MARK);
			goto encoded;

		case CX_MONTH:
			day = year = z = 0;
			checkDot = r->abbrev&1;
			month = v;
			iscap = 0;
			if(isupper(s[0]) && islower(s[1])) iscap = 1;
c = r->abbrev;
if((v == 3 || v == 4) && iscap) c &= ~2;
if(!(c&2)) goto monthYes;
/* need more proof - a number or hyphen before or after */
			q = s - 1;
			c = *q;
			if(c == ' ') c = *--q;
if(isdigit(c)) goto monthYes;
if(c == '-') goto monthYes;
			q = s + length;
			c = *q;
			if(c == '.') c = *++q;
			if(c == ' ') c = *++q;
			if(c == ',') c = *++q;
			if(c == ' ') c = *++q;
if(isdigit(c)) goto monthYes;
if(c == '-') goto monthYes;
continue; /* no additional proof */
monthYes:
q = s + length;
c = *q;
while(c && strchr(",. ", c)) c = *++q;
			yd = 0;
			while(isdigit(c)) {
				year = 10*year + c - '0';
				++yd;
				c = *++q;
			}
			if(!year) goto doDate;
			if(yd > 4 || yd == 3 ||
			(year > 31 && yd == 2)) {
				year = 0;
				goto doDate;
			}
			/* Definitely doing month + day/year */
			length = q - s;
			checkDot = 0;
			if(yd == 4) { /* it's a 4-digit year. */
				/* back up and look for day */
				t = tp_out->buf + tp_out->len - 1;
				if(*t == ' ' && isdigit(t[-1])) {
					c = *--t;
					day = c - '0';
					c = *--t;
					if(isdigit(c)) {
						day += 10*(c-'0');
						c = *--t;
					}
					if(!isalnum(c) && day && day <= 31)
						tp_out->len = t+1 - tp_out->buf;
					else day = 0;
				} /* prior number */
				goto zoneDate;
			} /* 4 digit year */
			/* day follows month, look ahead for year */
			day = year;
			year = 0;
			/* Some people write June 8th, rather than June 8. */
			if(isalpha(c) &&
			wordInList(ordSuffixes, q, 2) >= 0) {
				q += 2;
				length += 2;
				c = *q;
			}
			if(c == ',') c = *++q;
			if(c == '\f') goto doDate;
			if(isspace(c)) c = *++q;
			yd = 0;
			while(isdigit(c)) {
				year = 10*year + c - '0';
				++yd;
				c = *++q;
			}
zoneDate:
			if(year && yd == 4) {
				z = zoneCheck(&q);
				length = q - s;
			} else year = 0;
doDate:
			wordbuf[0] = SP_MARK;
			wordbuf[1] = SP_DATE;
			wordbuf[2] = month + 'A';
			wordbuf[3] = day + 'A';
			i = 4;
			if(year) {
				sprintf(wordbuf+4, "%04d", year);
				i += 4;
			}
			if(z) wordbuf[i++] = z + 'A';
			wordbuf[i++] = SP_MARK;
			wordbuf[i] = 0;
			goto encoded;

		case CX_NOON:
			q = s + length;
			z = zoneCheck(&q);
			length = q - s;
			sprintf(wordbuf, "%c%cMA%c%c%c", SP_MARK, SP_TIME,
			v, z + 'A', SP_MARK);
			goto encoded;

		case CX_UNIT:
			if(v&8) { /* requires followup period or comma */
				if(e != '.' && e != ',') continue;
			}
			v &= 7;
			iscap = 0; /* iscap will indicate 1 unit */
			q = s-1;
			c = *q;
			if(c == ' ') c = *--q;
			if(!isdigit(c)) continue;
			if(!v) break; /* not worried about plurals here */
			if(c == '1' && !isdigit(q[-1])) iscap = 1;
			do c = *--q; while(isdigit(c));
			/* but it could be a fraction */
			if(c == '/' && isdigit(q[-1])) {
				--q;
			do c = *--q; while(isdigit(c));
				/*is it 1 half inch or 1 half inches??? */
				iscap = 1; /* try 1 half inch */
				/* but it would read 3 and 1 half inches */
				if(c == ' ' || c == '\n') c = *--q;
				if(c == '-') c = *--q;
				if(c == ' ' || c == '\n') c = *--q;
				if(isdigit(c)) iscap = 0; /* back to plural */
			} /* fraction */
			if(!iscap) {
				if(c == ' ' || c == '\n') c = *--q;
				if(isalpha(c)) {
					i = 1;
					while(isalpha(q[-1])) --q, ++i;
					if(i && wordInList(articles, q, i) >= 0)
						iscap = 1;
				} /* letter */
			}
			if(!iscap) break;
			/* turn pounds back into pound */
			if(v == 3) {
				strcpy(wordbuf, "foot");
				goto encoded;
		}
			strcpy(wordbuf, (char*)r->replace);
			i = strlen(wordbuf);
			if(wordbuf[i-1] == 's') {
				--i;
				if(v == 2 && wordbuf[i-1] == 'e') --i;
			}
			wordbuf[i] = 0;
			goto encoded;

		case CX_PREFIX:
			if(!v) { /* actually it's a suffix */
				/* Need a prior name */
				q = s-1;
				c = *q;
				if(c != ' ')continue;
				c = *--q;
				if(!islower(c)) continue;
				do c = *--q; while(isalpha(c));
				if(!isupper(q[1])) continue;
				checkDot = 1;
				break;
			} /* suffix */
			/* ok, its a prefix */
			/* we need a period and letter, or a name */
			q = s + length;
			c = q[1];
			if(e == '.' && isspace(c) && isalpha(q[2])) {
				++length;
				break;
			} /* period after suffix */
			/* Better look like a name */
			if(e == ' ' && isupper(c)) {
				c = q[2];
				if(c == '.' || islower(c)) break;
			}
			continue;

		case CX_VOLUME:
			q = s + length;
			c = *q;
			if(c == '.') ++length;
			if(c == '.' || c == ',') c = *++q;
			if(c != ' ') continue;
			c = *++q;
			if(!isdigit(c)) continue;
			if(!v) break;
			/* check for pages 15-19 */
			while(isdigit(c)) c = *++q;
			if(c != '-') break;
			c = *++q;
			if(!isdigit(c)) break;
			strcpy(wordbuf, (char*)r->replace);
			strcat(wordbuf, "s");
			goto encoded;

		case CX_INC:
			checkDot = 1;
			if(d != ' ') continue;
q = s-2;
			c = *q;
			if(!isalpha(c)) continue;
			do c = *--q; while(isalpha(c));
			++q;
			if(q == s-2) continue; /* one letter word */
			if(case_different(q[0], s[0])) continue;
			if(case_different(q[1], s[1])) continue;
			if(isupper(s[0])) break;
			if(s[length] == '.') break;
			continue;

		case CX_TM:
			if(!strchr("([{<", (char)d)) continue;
			if(!strchr(")]}>", (char)e)) continue;
			if(!isspace(s[-2])) continue;
			c = s[length+1];
			if(c > ' ' && !strchr(",.?!", (char)c)) continue;
			/* It's a waste of time to read these things */
			appendBackup();
			return length+1;

		case CX_APT:
			q = s + length;
			c = *q;
			if(c == ' ') c = *++q;
			if(isdigit(c)) break;
			if(c != '#') continue;
			c = *++q;
			length = q - s;
			if(isdigit(c)) break;
			continue;

		case CX_RE:
			if(!strchr(",:;", (char)e)) continue;
			q = s + length + 1;
			c = *q;
		if(c != ' ') continue;
			c = *++q;
			if(isalnum(c)) break;
			continue;

		case CX_A_AN:
			q = s + length;
			c = *q;
			if(c != ' ') continue;
			c = *++q;
			if(c == '$') c = *++q;
			if(c == '8') break;
			if(c == '1' && q[1] == '1' && !isdigit(q[2])) break;
			continue;

		case CX_ADDRESS:
			if(v&1) { /* prior word */
				if(d != ' ') continue;
				if(!isalpha(s[-2])) continue;
			}
			if(v&2) { /* prior number */
				if(d != ' ') continue;
				if(!isdigit(s[-2])) continue;
				q = s + length;
				if(e == '.') ++q;
				c = *q;
				if(c != ',' && c != ';') {
					if(c != ' ' || !isalpha(q[1])) continue;
				}
			}
			if(v&4) { /* number in the recent past */
				q = s, i = 0;
				while(i < 24) {
					c = *--q;
					++i;
					if(!c) break;
					if(isalpha(c)) continue;
					if(isdigit(c)) break;
					if(!strchr(" -.", (char)c)) break;
				}
				if(!isdigit(c)) continue;
				if(!isdigit(q[-1])) continue;
			}
			if(v&8) { /* at the end of phrase or line */
				q = s + length;
				if(e == '.') ++q;
				c = *q;
				if(c == ' ') c = *++q;
				/* allow for followup ne or sw */
				c = tolower(c);
				if(c == 'n' || c == 's') {
					c = *++q;
					if(c == '.') c = *++q;
					c = tolower(c);
					if(c != 'e' && c != 'w') continue;
					c = *++q;
					if(c == '.') c = *++q;
					if(c == ' ') c = *++q;
				} /* n or s */
				c = *q;
				if(c && !strchr("\f\r,;:)", (char)c)) continue;
			}
			/* We always eat the period */
			if(s[length] == '.') ++length;
			break; /* all tests pass */

		case CX_STATE:
			if(r->abbrev&1) {
				if(islower(s[0])) continue;
				if(islower(s[1]) && length == 2) continue;
			}
			precity = precomma = postzip = 0;
			/* look back for a previous city. */
			q = s-1;
			c = *q;
			if(c == ' ') c = *--q;
			if(c == ',') precomma = 1, c = *--q;
			if(isalpha(c)) {
				i = j = 0;
				do {
					if(isupper(c)) ++j;
					c = *--q, ++i;
				} while(isalpha(c));
				++q;
				if(!precomma && islower(*q) &&
				!(r->abbrev&4) &&
				wordInList(prepositions, q, i) >= 0)
					precity = precomma = 1;
				if(!precity && isupper(*q) &&
				(j == i || j <= 2)) {
					if(precomma) {
						if(i == 3 && j == 1) precity = 1;
						if(i >= 4) precity = 1;
					}
					if(i >= 7 && islower(q[1]))
						precity = 1;
				} /* prev word is capitalized */
			} /* previous word */
			/* look ahead for zip code */
			q = s + length;
			c = *q;
			if(c == '.') c = *++q;
			if(c == ' ') c = *++q;
			if(c == ',') c = *++q;
			if(c == ' ' || c == '\n') c = *++q;
			if(isalnum(c)) {
				i = j = 0;
				do {
					if(isdigit(c)) ++j;
					c = *++q, ++i;
				} while(isalnum(c));
				if(i >= 5 && i <= 6 && j >= 3) postzip = 1;
			} /* subsequent token */

if(r->abbrev&4 && !(postzip|precomma)) precity = 0;
			if(!(postzip|precity) && r->abbrev) continue;
			if(precity&precomma) {
				/* watch out for city, washington, dc */
				/* we still need the comma before washington */
				if(v == STATE_WASHINGTON) {
					q = s + length;
					c = *q;
					if(c == ' ') c = *++q;
					if(c == ',') c = *++q;
					if(c == ' ') c = *++q;
					if(tolower(c) == 'd') {
						c = *++q;
						if(c == '.') c = *++q;
						if(tolower(c) == 'c' &&
						!isalnum(q[1]))
							precomma = 0;
					}
				}
			}
			if(precity&precomma)
				lastUncomma();
			checkDot = (r->abbrev >> 1) & 1;
			sprintf(wordbuf, "%c%c%c%c", SP_MARK, SP_STATE,
			'A' + v, SP_MARK);
			if(appendString((char*)wordbuf)) goto overflow;
			if(postzip) {
				if(s[length] == '.') ++length;
				checkDot = 0;
				appendBackup();
				appendChar(',');
				if(appendChar(' ')) goto overflow;
			}
			break;

		case CX_BIBLE:
			/* We're going to borrow some variables from date */
			year = month = day = yd = 0;
			/* look ahead for the chapter/verse */
			q = s + length;
			c = *q;
			if(c == ' ' || c == '\n') c = *++q;
			if(!isdigit(c)) continue;
			i = 0;
			while(isdigit(c)) { year = 10*year + c - '0'; c = *++q; ++i; }
			if(i > 3 || !year || year > 150 || isalpha(c)) continue;
			if(c != ':') continue;
			c = *++q;
			i = 0;
			while(isdigit(c)) { month = 10*month + c - '0'; c = *++q; ++i; }
			if(i > 3 || !month || month > 150) continue;
			if(c == ' ') c = *++q;
			if(c != '-') goto doBible;
			w = q;
			c = *++w;
			if(c == ' ') c = *++w;
			if(!isdigit(c)) goto doBible;
			i = 0;
			while(isdigit(c)) { day = 10*day + c - '0'; c = *++w; ++i; }
			if(i > 3 || !day || day > 150) { day = 0; goto doBible; }
			if(c != ':') {
				if(!isalpha(c)) q = w; else day = 0;
				goto doBible;
			}
			c = *++w;
			if(!isdigit(c)) { day = 0; goto doBible; }
			i = 0;
			while(isdigit(c)) { yd = 10*yd + c - '0'; c = *++w; ++i; }
			if(i > 3 || !yd || yd > 150) { day = yd = 0; goto doBible; }
			q = w;
doBible:
			if(r->abbrev&1 && !year) continue;
			length = q - s;
			/* check for number prefix */
			j = r->abbrev >> 1;
			if(j) {
				t = tp_out->buf + tp_out->len - 1;
				if(*t == ' ' &&
				t[-1] >= '1' && t[-1] <= '0'+j &&
				!isalnum(t[-2])) {
					tp_out->len -= 2;
					v += t[-1] - '1';
				} else {
					/* special code for john */
					if(v == 62) v = 43;
				}
			}
			sprintf(wordbuf, "%c%c%c",
			SP_MARK, SP_BIBLE, '0'+v);
			if(year) sprintf(wordbuf+3, "%03d", year);
			if(month) sprintf(wordbuf+6, "%03d", month);
			if(day) sprintf(wordbuf+9, "%03d", day);
			if(yd) sprintf(wordbuf+12, "%03d", yd);
			j = strlen(wordbuf);
			wordbuf[j] = SP_MARK;
			wordbuf[j+1] = 0;
			goto encoded;

		default: /* should never happen */
			sprintf(wordbuf, "context %d", cx);
encoded:
			if(appendString((char*)wordbuf)) goto overflow;
			appendBackup();
			if(checkDot) length += skipDot(s+length);
			return length;
		} /* switch */

		if(r->replace) {
			if(appendString(r->replace)) goto overflow;
			appendBackup();
		}
		if(checkDot) length += skipDot(s+length);
		return length;
	} while(!((++r)->context&0x4000));

	return 0;

overflow:
	return -1;
} /* encodeWord */


static int encodeNumber(const char *s)
{
	const char *s0 = s;
	const char *q;
	char *t;
	char c = *s;
	char ampm;
	int month, day, year;
	int hour, minute, second;
	int z = 0; /* timezone */
	const char *p1; /* area code */
	const char *p2; /* office code */
	const char *p3; /* xxxx */
	const char *p4; /* extension */
	int leadOne;
	short numext; /* number of digits in the extension */
	int i, n, length;
	char numbuf[32];

/* prevent the encoding of 2/3 in x^2/3 */
if(s[-1] == '^') goto done;

	n = 0;
	while(isdigit(c)) n = 10*n + c - '0', c = *++s;
	length = s - s0;
	if(length >= 3) goto phoneCheck;

	if(n > 24) goto done;

	if(c == '/' && n && n <= 12) {
		static int rareDenominator[] = {
		0,1,0,0,0,0,0,0,0,1,0,1,0,1,1,1,0,1,1,1,0,1,1,1,1,0,1,1,1,1,0,1};
		int yd; /* digits in year */
		int day2 = 0;
		int isFraction = 0;

		if(s0[-1] == '/') goto done;
		c = *++s;
		if(!isdigit(c)) goto done;
		month = n;
		day = c-'0';
		c = *++s;
		if(isdigit(c)) {
			day = 10*day + c - '0';
			day2 = 1;
			c = *++s;
		} /* mm/dd */
		year = yd = 0;
		if(c == '/') {
			for(i=1; i<=5; ++i)
				if(!isdigit(s[i])) break;
			yd = --i;
			/* We require digits after a second slash */
			if(!yd) goto done;
			/* Cannot be an odd number of digits */
			if(yd&1) goto done;
			year = atoiLength(s+1, yd);
			if(yd == 4 && !year) goto done;
			/* Here comes the millennium bug */
			if(yd == 2) year += 2000;
			s += yd+1;
			c = *s;
		} /* mm/dd/yyyy */
		if(c == '/') goto done;
		if(day == 0 || day > 31) goto done;
		/* cannot have letters on both sides */
		if(isalpha(c) && isalpha(s[-1])) goto done;
		if(year > 0) goto doDate;
		/* no year, I'm not going to try to call it a date. */
		/* Only thing left is maybe a fraction */
		/* No leading 0, and no improper fraction. */
		if(*s0 == '0' || day2 && day < 10) goto done;
		if(month >= day) goto done;
		/* A good indication of fraction is a prior
		 * number, such as 3 1/2 feet tall. */
		t = tp_out->buf + tp_out->len - 1;
		c = *t;
		if(c == ' ' || c == '\n') c = *--t;
		if(c == '-') c = *--t;
		if(c == ' ') c = *--t;
		if(isdigit(c)) {
			/* has to be a modest stand-alone number */
			q = t - 1;
			if(isdigit(*q)) --q;
			if(isdigit(*q)) --q;
			if(!isalnum(*q)) {
				extern const char *andWord;
				/* get rid of the minus, and any spaces. */
				++t;
				*t++ = ' ';
					tp_out->len = t - tp_out->buf;
				/* three and one half */
				if(appendString(andWord)) goto overflow;
				isFraction = 1;
				goto doFrac;
			}
		} /* prior number */
if(day2) {
		if(rareDenominator[day]) goto done;
		if(day >= 20 && month != 1) goto done;
		/* numerator and denominator are coprime,
		 * except perhaps x/10. */
		if(day != 10) {
			if(!((month|day)&1)) goto done;
			if(month%3 == 0 && day%3 == 0) goto done;
		}
		}

		/* finally check for fraction words and make
		 * your final determination. */
		q = s;
		c = *q;
		if(isspace(c)) c = *++q;
		if(isalpha(c)) {
			i = alphaLength(q);
			if(i > 2) i = 0;
			if(wordInList(postFractionWords, q, i) >= 0) {
				isFraction = 1;
				goto doFrac;
			}
		}

		q = s0-1;
		c = *q;
		if(isspace(c)) c = *--q;
		if(isalpha(c)) {
			i = 0;
			while(isalpha(c)) c = *--q, ++i;
			++q;
			if(wordInList(preFractionWords, q, i) >= 0) {
				isFraction = 1;
				goto doFrac;
			}
		}

		goto done;

doDate:
		z = zoneCheck(&s);
		if(z) isFraction = 0;
doFrac:
		numbuf[0] = SP_MARK;
		numbuf[1] = SP_DATE;
		if(isFraction) numbuf[1] = SP_FRAC;
		numbuf[2] = month + 'A';
		numbuf[3] = day + 'A';
		i = 4;
		if(!isFraction && year) {
			sprintf(numbuf+4, "%04d", year);
			i += 4;
		}
		if(z) numbuf[i++] = z + 'A';
		numbuf[i++] = SP_MARK;
		numbuf[i] = 0;
		goto encoded;
	} /* check for mm/dd/yyyy */

	/* hh:mm:ss */
	hour = second = -1;
	minute = 0; /* quiet gcc warning */
	if(c == ':') {
		if(s0[-1] == ':') goto done;
		c = *++s;
		if(!isdigit(c)) goto done;
		if(c >= '6') goto done;
		minute = c - '0';
		if(minute && n == 24) goto done;
		c = *++s;
		if(!isdigit(c)) goto done;
		minute = 10*minute + c - '0';
		c = *++s;
		if(c == ':') {
			c = *++s;
			if(!isdigit(c)) goto done;
			if(c >= '6') goto done;
			second = c - '0';
			c = *++s;
			if(!isdigit(c)) goto done;
			second = 10*second + c - '0';
			c = *++s;
		} /* third field */
		if(c == ':') goto done;
if(isalnum(c) &&
/* but we'll allow am pm */
!strchr("aApP", c)) goto done;
		/* ok, it's a time */
		hour = n;
	} /* check for hh:mm:ss */
	if(hour < 0 && (!n || n > 12)) goto done;

	/* look for AM or A.M. */
	q = s;
	ampm = '?';
	if(isspace(c)) c = *++q;
	c = toupper(c);
	if(c == 'A' || c == 'P') {
		i = c;
		c = *++q;
		if(c == '.') c = *++q;
		c = toupper(c);
		if(c == 'M' && !isalnum(q[1])) {
			c = *++q;
			if(c == '.' && q[-2] == '.')
				q += skipDot(q);
			ampm = i;
			s = q;
		}
	}
	if(hour < 0 && ampm == '?') goto done;

	z = zoneCheck(&s);
	c = *s;

	/* Special code for the output of Linux `date` */
	q = s;
	if(c == ' ') c = *++q;
	if(isdigit(c) && isdigit(q[1]) &&
	isdigit(q[2]) && isdigit(q[3]) &&
	!isdigit(q[4])) {
		/* If a date precedes, modify it */
		t = tp_out->buf + tp_out->len - 1;
		c = *t;
		if(c == ' ') c = *--t;
		if(c == SP_MARK &&
		t[-1] >= 'A' && t[-2] >= 'A' &&
		t[-3] == SP_DATE && t[-4] == SP_MARK) {
			year = atoiLength(q, 4);
			s = q+4;
			if(year) {
				tp_out->len = t - tp_out->buf;
				sprintf(numbuf, "%04d%c", year, SP_MARK);
				if(appendString((char*)numbuf)) goto overflow;
			} /* nonzero year */
		} /* prior date */
	} /* 4 digit year follows */

	/* build the time code */
	if(hour < 0) hour = n, minute = 0;
	numbuf[0] = SP_MARK;
	numbuf[1] = SP_TIME;
	numbuf[2] = 'A' + hour;
	numbuf[3] = 'A' + minute;
	numbuf[4] = ampm;
	numbuf[5] = 'A' + z;
	numbuf[6] = SP_MARK;
	numbuf[7] = 0;
	goto encoded;

phoneCheck:
	if(tp_readLiteral) goto done;
	/* Check for north American phone numbers. */
	/* Must be written with the first block of 3 separated */
	/* from the other 4 or 7. */
	if(length > 4) goto done;
	c = *s0;
	if(length == 4 && c != '1') goto done;
	if(length == 3 && c <= '1') goto done;
	leadOne = 0;
	p4 = 0, numext = 0;
	t = tp_out->buf + tp_out->len;
	p1 = s0;
	if(length == 4) ++p1, leadOne = 1;
	if(!leadOne &&
	(c = t[-1]) && strchr("- .", (char)c) &&
	t[-2] == '1') {
		leadOne = 1;
		t -= 2;
	}
	c = t[-1];
	if(isalnum(c)) goto done;
	if(c == '-' || c == '.') goto done;

	/* Skip past punctuation between first and second block */
	c = *s;
	if(!c) goto done;
	if(!strchr("- \r.)]}>/", (char)c)) goto done;
	c = *++s;
	if((c == ' ' || c == '\n') && !strchr("./", (char)s[-1]))
		c = *++s;

	/* determine size of second block */
	p2 = s;
	for(n=0; isdigit(c); c = *++s, ++n)  ;
	if(n == 7) {
		p3 = p2 + 3;
		goto number_ok;
	}
	if(n == 4) {
		if(leadOne) goto done;
		p3 = p2;
		p2 = p1;
		if(p3 != p2+4 || p2[3] != '-') goto done;
		goto number_ok;
	}

	/* arrangement must be nxx-nxx-xxxx */
	if(n != 3) goto done;
	if(c != '-' && (c != '.' || p1[3] != '.')) goto done;
	p3 = ++s;
	c = *s;
	for(n=0; isdigit(c); c = *++s, ++n)  ;
	if(n != 4) goto done;

number_ok:
	if(*p2 < '2') goto done;
	if(c == '-') goto done;
	if(c == '.' && isdigit(s[1])) goto done;
	if(isalpha(c)) goto done;

	/* We're definitely encoding a number. */
	if(strchr(")}]>", (char)p1[3]) &&
	t[-1] &&
	strchr("([{<", (char)t[-1]))
		--t;
	tp_out->len = t - tp_out->buf;

	/* look for an extention */
	if(c == ',') c = *++s;
	if(isspace(c)) c = *++s;
	if(c == '(') c = *++s;
	for(n=0; isalpha(c); c = *++s, ++n)  ;
	s -= n;
	if(n && wordInList(extWords, s, n) >= 0) {
		int lparen = (s[-1] == '(');
		s += n;
		c = *s;
		if(c == '.') c = *++s;
		if(c == '-') c = *++s;
		if(isspace(c)) c = *++s;
		if(c == '(') c = *++s, lparen = 1;
		/* how many digits in the extension? */
		for(n=0; isdigit(c); c = *++s, ++n)  ;
		if(n && n <= 6) {
			if(!isalpha(c) && c != '-') {
				p4 = s - n;
				numext = n;
				if(lparen && c == ')') ++s;
			} /* nothing weird after the extension */
		} /* between 1 and 6 digits in the extension */
	} /* found an extension keyword */
	if(!p4) s = p3+4;

	/* encode the phone number */
	sprintf(numbuf,"%c%c%03d%03d%04d", SP_MARK, SP_PHONE,
	(p1 == p2 ? 0 : atoiLength(p1, 3)),
	atoiLength(p2, 3), atoiLength(p3, 4));
	if(p4) {
		strncpy(numbuf+12, (char*)p4, numext);
	}
	numbuf[12+numext] = SP_MARK;
	numbuf[13+numext] = 0;
encoded:
	if(appendString((char*)numbuf)) goto overflow;
	appendBackup();
	return s - s0;

done:
	return 0;

overflow:
	return -1;
} /* encodeNumber */



/*********************************************************************
Mark the end of a sentence, using nl (ASCII 10).
If literal mode is off, and there isn't
a preexisting punctuation mark, add in a period.
*********************************************************************/

static int markSentence(void)
{
	char *t = tp_out->buf + tp_out->len - 1;
	char c = *t;
if(tp_readLiteral) return 0;
	while(c == ' ' || c == '\t') c = *--t;
	if(!c) return 0;
	if(isspace(c)) return 0;
	if(!strchr(".?!", c) &&
	appendChar('.')) return 1;
	return appendChar('\n');
} /* markSentence */

static int markPhrase(void)
{
	char *t = tp_out->buf + tp_out->len - 1;
	char c = *t;
	while(c == ' ' || c == '\t') c = *--t;
	if(!c) return 0;
	if(isspace(c)) return 0;
	if(strchr(".,!?;:", (char)c)) return 0;
	return appendChar(',');
} /* markPhrase */


/*********************************************************************
Encode the text, using the above routines
to encode word/number based constructs.
*********************************************************************/

void doEncode(void)
{
	char *s = tp_in->buf + 1;
	char c, d, e;
	char *t;
		const char *sp; /* start of previous line */
		const char *sn; /* end of next line */
		int lp, ln; /* lengths of prev and next lines */
	int rc;
	int j, spaces;
	int overflowValue = 1;
	int urlSet = 0, urlComma = 0;
	static const char urlTerminate[] = ",.?!:;>";
	int isquote;

	while((c = *s)) {

		if(tp_oneSymbol) goto start_c;

		if(urlSet) { /* waiting for the slash */
			if(!isalnum(c) && !strchr("/-._,", (char)c))
				urlSet = 0; /* slash never came */
		}
		if(urlComma && (c <= ' ' ||
		(s[1] <= ' ' && strchr(urlTerminate, (char)c)))) {
			if(!tp_readLiteral && appendChar(',')) goto overflow;
			urlComma = 0;
		}

		/* Sentence boundaries not based on .:!? */
		/* Of course formfeed always ends the sentence. */
		if(c == '\f' && markSentence()) goto overflow;
		if(c != '\n') goto start_c;
		/* Since this is cr, not ff, there should be
		 * text on either side, specifically non-whitespace
		 * text.  Let's make sure though. */
		d = s[-1];
		if(!d) goto start_c;
		if(isspace(d)) goto start_c;
		d = s[1];
		if(!d) goto start_c;
		if(isspace(d)) goto start_c;
		/* P.S. always begins a sentence */
		if(acs_substring_mix((char*)"p.s", s+1) > 0) {
			d = s[4];
			if(d == '.') d = s[5];
			if(!d || isspace(d)) {
				d = s[4];
				if(d && strchr(". \r", (char)d)) s[4] = ',';
				goto crSentence;
			}
		} /* P.S */
		/* get stats on previous and next line */
		lp = lineLength(s, &sp, 0);
		ln = lineLength(s+1, 0, &sn);
		/* A really long line is usually a paragraph in
		 * itself.  That's how some word processors
		 * dump to ASCII. */
		if(lp > 200 || ln > 200) { c = '\f'; goto crSentence; }
		/* short followed by long line */
		if(ln >= lp + 20 && ln >= 40 && lp <= 50 &&
		isupper(s[1]) && s[-1] != ',')
			goto crSentence;
		/* a line wholy in parentheses */
		if(ln >= 24 &&
		strchr("([{", (char)s[1]) &&
		strchr(")]}", (char)sn[-1]))
			goto crSentence;
		if(lp >= 24 &&
		strchr("([{", (char)sp[1]) &&
		strchr(")]}", (char)s[-1]))
			goto crSentence;
		/* Check for list item. */
		if(s[1] == SP_MARK && s[2] == SP_LI)
			goto crSentence;
		/* append a comma to each short line */
		if(!tp_readLiteral && lp <= 42 &&
		markPhrase()) goto overflow;
		goto start_c;
crSentence:
		if(markSentence()) goto overflow;

start_c:
		carryOffsetForward(s);
		if(c <= ' ') goto add_c;

		if(c == SP_MARK) {
			d = s[1];
			if(d == SP_EMOT && markSentence()) goto overflow;
			if(appendChar(c)) goto overflow;
			++s;
			while((c = *s)) {
				if(appendChar(c)) goto overflow;
				++s;
				if(c == SP_MARK) break;
			}
			if(d == SP_EMOT && markSentence()) goto overflow;
			continue;
	} /* passing an encoded construct through */

		d = s[-1];
		if(!d) d = '\f';

		if(isalpha(c) && !isalpha(d)) {
			rc = encodeWord(s);
			if(rc < 0) goto overflow;
			if(rc) { s += rc; continue; }
			/* look for www without prior protocol */
			if(tolower(c)== 'w' &&
			tolower(s[1]) == 'w' &&
			tolower(s[2]) == 'w' &&
			s[3] == '.' && isalnum(s[4]) &&
			!isalnum(d) && d != '/' &&
			(d != '.' || !isalnum(s[-2]))) {
				char buf[8];
				buf[0] = buf[3] = SP_MARK;
				buf[1] = SP_URL;
				buf[2] = 'b';
				buf[4] = 0;
				if(appendString(buf)) goto overflow;
	appendBackup();
				urlSet = 1;
			} /* www */
			goto add_c;
		} /* leading letter of a word */

		if(tp_oneSymbol) goto add_c;

		if(isdigit(c) && !isalnum(d)) {
			rc = encodeNumber(s);
			if(rc < 0) goto overflow;
			if(!rc) goto add_c;
			s += rc;
			continue;
		} /* leading digit of a number */

		/* check for protocol://domain */
		if(c == ':' && s[1] == '/' && s[2] == '/' &&
		isalnum(s[3]) && isalnum(d)) {
			t = tp_out->buf + tp_out->len - 1;
			j = 0;
			while(isalnum(*t)) ++j, --t;
			++t;
			rc = wordInList(urlWords, t, j);
			if(rc > 0) --rc;
if(rc >= 0) {
			rc += 'A' + 1;
			if(tolower(s[3]) == 'w' &&
			tolower(s[4]) == 'w' &&
			tolower(s[5]) == 'w' &&
			!isalnum(s[6]))
				rc = tolower(rc);
			*t++ = SP_MARK;
			*t++ = SP_URL;
			*t++ = rc;
			*t++ = SP_MARK;
			tp_out->len = t - tp_out->buf;
			urlSet = 1;
			s += 3;
			continue;
} /* recognized protocol */
		} /* url encoded */

		if(c == '/' && urlSet) {
			int n = 0, nd = 0;
			urlSet = 0;
			t = s+1;
			c = *t;
			d = '/';
			while(c > ' ') {
				++n;
				if(isdigit(c)) ++nd;
				d = c;
				c = *++t;
			}
			/* At some point we should decide whether the pathname
			 * under the web site is "readable".
			 * For now we just check its length. */
			if(tp_readLiteral || (n < 24 && nd <= 6)) {
				urlComma = 1;
				c = '/';
				goto add_c;
			}
			/* skip pass the pathname */
			s = --t;
			if(!strchr(urlTerminate, (char)d)) {
				if(!tp_readLiteral) *s = ',';
				else ++s;
			}
			t = tp_out->buf + tp_out->len;
			for(--t; *t; --t)
				if(*t == SP_MARK) break;
			t[-1] += 12;
			continue;
		}

		/* Punctuation marks that split sentences */
		if(!strchr(".?:!", (char)c)) goto add_c;
		if(isspace(d)) goto add_c;
		t = s + 1;
		e = *t;
		if(!e) goto add_c;
		/* sentence can end in quote or right paren */
		isquote = 0;
		if(strchr("\")]}", (char)e)) isquote = 1, e = *++t;
		/* look for whitespace after the period */
		if(!isspace(e)) goto add_c;
		/* Look for letter period */
		if(isspace(s[-2]) && !isdigit(d)) {
			/* Even in literal mode, we don't need the period
			 * if we're dealing with an initial.
			 * But we should retain : ? or !. */
			if(!isalpha(d)) goto add_c;
			if(c != '.') goto add_c;
			if(isquote) { ++s; continue; }
			if(e == '\f')goto add_c;
			++s;
			continue;
		} /* Harry S. Truman */
		if(e == '\f') goto add_c;
		spaces = 0; /* quiet gcc warning */
		if(e == ' ') spaces = 1;
		if(e == '\n') spaces = 2;
		if(e == '\t') spaces = 3;
		e = *++t;
		if(c == ':') {
			if(spaces != 2) goto add_c;
puncSentence:
			if(appendChar(c)) goto overflow;
			++s;
			if(isquote) {
				if(tp_readLiteral) {
					carryOffsetForward(s);
					if(appendChar(*s)) goto overflow;
				}
				++s;
			}
			if(markSentence()) goto overflow;
			continue;
		} /* colon */
		/* multiple spaces after period. */
		if(spaces == 3) goto puncSentence;
		if(c != '.') { /* ? and ! carry a lot of weight */
			if(isupper(e)) goto puncSentence;
			if(isquote) {
				if(tp_readLiteral) goto add_c;
				if(c == '?') c= ',';
				++s;
				goto add_c;
			} /* in quotes */
			if(spaces == 2) goto puncSentence;
			if(!tp_readLiteral) c= ',';
			goto add_c;
		} /* ? or ! */

		/* Skip the last . in U.S.A., unless
		 * it really ends a sentence. */
		if(isalpha(d) &&
		s[-2] == c &&
		skipDot(s)) {
			++s;
			continue;
		}

		/* Period, not an initial, and not
		 * at the end of U.S.A. */
		if(!islower(e)) goto puncSentence;
		if(spaces == 2) goto puncSentence;
		if(!tp_readLiteral) c = ',';
		/* fall through */

add_c:
		if(appendChar(c)) goto overflow;
		++s;
	} /* loop scanning input characters */

	overflowValue = 0;

overflow:
	textbufClose(s, overflowValue);
} /* doEncode */


#endif
