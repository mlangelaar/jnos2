#ifndef	_BBS10000_H
#define _BBS10000_H

/*
 * 05Nov2020, Maiko, Convert from GET to POST when submitting forms, and
 * other refinements, added 'abort_char' to added HTTPVNCSESS structure.
 *
 * Web Based BBS Access for JNOS 2.0g and beyond ...
 *
 *   June 8, 2009 - started working in a play area on a work machine.
 *   July 3, 2009 - put into my official development source (prototype).
 *  July 21, 2009 - prototype pretty much ready for single web client.
 * February, 2010 - refining session control for multiple web clients at once.
 * April 24, 2010 - seems to be working nicely over last while - done.
 *
 * Designed and written by VE4KLM, Maiko Langelaar
 *
 * For non-commercial use (Ham Radio) only !!!
 *
 */

typedef struct {

    char *name;
    char *pass;

} PASSINFO;

/*
 * 08July2009, Maiko (VE4KLM), Need session tracking so that we can have
 * multiple HTTP VNC connections going at the same time, for different calls
 * and from different clients, etc.
 */

typedef struct {

	int32 ipaddr;
	char *currcall;
	int mbxs[2];
	int escape_char;
	int clear_char;
	int mbxl;
	int mbxr;

	PASSINFO passinfo;

	char tfn[20];	/* give each CALL their own session web file */

	FILE *fpsf;	/* Session File write file descriptor */

	int valid;	/* 23Feb2010, session validation and port attacks */

	int reused;	/* 06Mar2010, track number of times slot is reclaimed */

	int abort_char;	/* 05Nov2020, Maiko, new to abort sending message */

} HTTPVNCSESS;

#endif

