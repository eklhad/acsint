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
#define SP_MARK (0x8000)

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
	unsigned int *buf;
	acs_ofs_type *offset;
	unsigned short room;
	unsigned short len;
};

extern struct textbuf *tp_in, *tp_out;

#define appendBackup() (--tp_out->len)
#define case_different(x, y) (acs_isupper(x) ^ acs_isupper(y))

extern char tp_alnumPrep;
extern char tp_relativeDate;
extern char tp_showZones;
extern int tp_myZone; /* offset from gmt */
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
void carryOffsetForward(const unsigned int *s) ;
void textbufClose(const unsigned int *s, int overflow) ;
int wordInList(const char * const *list, const unsigned int *s, int s_len) ;
int appendChar(unsigned int c) ;
int appendString(const char *s) ;
void lastUncomma(void) ;
int alphaLength(const unsigned int *s) ;
int atoiLength(const unsigned int *s, int len) ;
void prepTTS(void) ;
unsigned int *prepTTSmsg(const char *msg) ;

#endif
