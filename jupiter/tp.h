/*********************************************************************

tp.h: header file for the text preprocessor.

Copyright (C) Karl Dahlke, 2011.
This software may be freely distributed under the GPL, general public license,
as articulated by the Free Software Foundation.

*********************************************************************/

#ifndef TP_H
#define TP_H 1

#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include "../bridge/acsbridge.h"

/* WORDLEN comes from acsbridge.h */
#define NEWWORDLEN 200 /* size of word or number after expansion */

/* Speech preparation mark */
/* This is generally assigned to, and compared against, a char */
#define SP_MARK ((char)0x80)

/* Coded constructs such as date, time, state, etc. */
enum sp_codes {
SP_NONE,
SP_REPEAT,
SP_DATE,
SP_TIME,
SP_PHONE, // USA phone numbers only
SP_LI, // list item
SP_EMOT,
SP_FRAC,
SP_WDAY,
SP_STATE,
SP_BIBLE,
SP_URL,
};

struct textbuf {
	char *buf;
	acs_ofs_type *offset;
	unsigned short room;
	unsigned short len;
};

extern struct textbuf *tp_in, *tp_out;

#define appendBackup() (--tp_out->len)
/* case independent character compare */
#define ci_cmp(x, y) (tolower(x) != tolower(y))

extern char tp_alnumPrep;
extern char tp_relativeDate;
extern char tp_showZones;
extern int tp_myZone; /* offset from gmt */
extern char tp_digitWords; /* read digits as words */
extern char tp_acronUpper; /* acronym letters in upper case? */
extern char tp_acronDelim;
extern char tp_oneSymbol; /* read one symbol - not a sentence */
extern char tp_readLiteral; // read each punctuation mark
/* a convenient place to put little phrases to speak */
extern char shortPhrase[NEWWORDLEN];

/* prototypes */

/* sourcefile=tpencode.c */
void ascify(void) ;
void doWhitespace(void) ;
void ungarbage(void) ;
void titles(void) ;
void sortReservedWords(void) ;
void listItem(void) ;
void doEncode(void) ;

/* sourcefile=tpxlate.c */
int setupTTS(void) ;
void speakChar(unsigned int c, int sayit, int bellsound, int asword) ;
void textBufSwitch(void) ;
void carryOffsetForward(const char *s) ;
void textbufClose(const char *s, int overflow) ;
int isvowel(char c) ;
int case_different(char x, char y) ;
int isSubword(const char *s, const char *t) ;
int wordInList(const char * const *list, const char *s, int s_len) ;
int appendChar(char c) ;
int appendString(const char *s) ;
void lastUncomma(void) ;
int alphaLength(const char *s) ;
int atoiLength(const char *s, int len) ;
void prepTTS(void) ;
char *prepTTSmsg(const char *msg) ;

#endif
