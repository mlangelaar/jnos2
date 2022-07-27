/* ed_el.zip (Unix ed clone) obtained from SimTel collection, and adapted
 * to Jnos by N5KNX, 12/95.  A few minor bugs fixed.
 * The concept is that if EDITOR is defined, we generate code to invoke
 * either ed, or ted, depending on whether ED or TED was defined.
 * TED takes up much less code space and only works from the console.
 *
 * Define STANDALONE & ED/TED for use (testing?) outside Jnos, eg,
 *  bcc -DSTANDALONE -DED/-DTED -O -G -Z -ml editor.c
 */

/*
 *  ed - standard editor
 *  ~~
 *	Authors: Brian Beattie, Kees Bot, and others
 *
 * Copyright 1987 Brian Beattie Rights Reserved.
 * Permission to copy or distribute granted under the following conditions:
 * 1). No charge may be made other than reasonable charges for reproduction.
 * 2). This notice must remain intact.
 * 3). No further restrictions may be added.
 *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
 *  TurboC mods and cleanup 8/17/88 RAMontante.
 *  Further information (posting headers, etc.) at end of file.
 * _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _
 */

#define	edversion	401	/* used only in the "set" function, for i.d. */

#ifdef STANDALONE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <io.h>
#define mallocw		malloc
#define tflush()	fflush(stdout)
#define tputs(x)	fputs(x,stdout)
#define tputc(c)	putchar(c)
#define tprintf		printf
#define pwait(ev)
#define dosigs		1
#define EDITOR		1

#else

#include "global.h"
#include "proc.h"
#include "socket.h"
#include "session.h"
#ifdef DBGLINKS
#include <dos.h>
#endif

#ifdef __TURBOC__
#include <signal.h>
#include <io.h>

#endif  /* ifdef __TURBOC__  */
#endif /* STANDALONE */



#ifdef EDITOR

#define tputs j2tputs	/* 05Dec2020, Maiko (VE4KLM */

#ifdef TED
int	ted2(char far *filepath);
extern int StatusLines;
#endif	/* TED */
#ifdef ED
/*
 *	#defines for non-printing ASCII characters
 */
#define NUL	0x00	/* ^@ */
#define EOS	0x00	/* end of string */
#ifdef	DONT_COMPILE
#define SOH	0x01	/* ^A */
#define STX	0x02	/* ^B */
#define EOT	0x04	/* ^D */
#endif
#define ETX	0x03	/* ^C */
#define ENQ	0x05	/* ^E */
#define ACK	0x06	/* ^F */
#define BEL	0x07	/* ^G */
#define BS	0x08	/* ^H */
#define HT	0x09	/* ^I */
#define LF	0x0a	/* ^J */
#define NL	'\n'
#define VT	0x0b	/* ^K */
#define FF	0x0c	/* ^L */
#define CR	0x0d	/* ^M */
#define SO	0x0e	/* ^N */
#define SI	0x0f	/* ^O */
#define DLE	0x10	/* ^P */
#define DC1	0x11	/* ^Q */
#define DC2	0x12	/* ^R */
#define DC3	0x13	/* ^S */
#define DC4	0x14	/* ^T */
#define NAK	0x15	/* ^U */
#define SYN	0x16	/* ^V */
#define ETB	0x17	/* ^W */
#define CAN	0x18	/* ^X */
#define EM	0x19	/* ^Y */
#define SUB	0x1a	/* ^Z */
#define ESC	0x1b	/* ^[ */
#define FS	0x1c	/* ^\ */
#define GS	0x1d	/* ^] */
#define RS	0x1e	/* ^^ */
#define US	0x1f	/* ^_ */
#define SP	0x20	/* space */
#define DEL	0x7f	/* DEL*/


/*	Definitions of meta-characters used in pattern matching
 *	routines.  LITCHAR & NCCL are only used as token identifiers;
 *	all the others are also both token identifier and actual symbol
 *	used in the regular expression.
 */
#define BOL	'^'
#define EOL	'$'
#define ANY	'.'
#define LITCHAR	'L'
#define	ESCPE	'\\'
#define CCL	'['	/* Character class: [...] */
#define CCLEND	']'
#define NEGATE	'^'	/* n5knx: No collision with BOL, since only flags NCCL */
#define NCCL	'!'	/* Negative character class [^...] */
#define CLOSURE	'*'
#define OR_SYM	'|'
#define DITTO	'&'
#define OPEN	'('
#define CLOSE	')'


/* Largest permitted size for an expanded character class.  (i.e. the class
 * [a-z] will expand into 26 symbols; [a-z0-9] will expand into 36.)
 */
#define CLS_SIZE	128


#define TRUE	1
#define FALSE	0
#define ERR		-2
#define FATAL		(ERR-1)
#define CHANGED		(ERR-2)
#define SET_FAIL	(ERR-3)
#define SUB_FAIL	(ERR-4)


#define	BUFFER_SIZE	2048	/* stream-buffer size:  == 1 hd cluster */

#define LINFREE	1	/* entry not in use */
#define LGLOB	2       /* line marked global */

#define MAXLINE	512	/* max number of chars per line */
#define MAXPAT	256	/* max number of chars per replacement pattern */
#define MAXFNAME 128	/* max file name size */


/**  Global variables  [made static to hide them from other modules!--n5knx] **/

/*  Tokens are used to hold pattern templates. (see makepat())  */

typedef	char	BITMAP;
typedef struct token {
	char		tok;
	char		lchar;
	BITMAP		*bitmap;
	struct token	*next;
} TOKEN;

#define TOKSIZE sizeof (TOKEN)


struct	line {
	int		l_stat;		/* empty, mark */
	struct line	*l_prev;
	struct line	*l_next;
	char		l_buff[1];
};
typedef struct line	LINE;


static int	diag = 1;		/* diagnostic-output? flag */
static char	*paropen[9];
static char	*parclose[9];
static int	between, parnum;
static int	truncflg = 1;	/* truncate long line flag (not used) */
static int	eightbit = 1;	/* save eighth bit */
static int	nonascii;	/* count of non-ascii chars read */
static int	nullchar;	/* count of null chars read */
static int	truncated;	/* count of lines truncated */
static char	fname[MAXFNAME];
static int	fchanged;	/* file-changed? flag */
static int	nofname;
static int	restricted;	/* only access command-line file */
static int	mark['z'-'a'+1];
static TOKEN	*oldpat;

static LINE	Line0;
static int	CurLn = 0;
static int	LastLn = 0;
static char	inlin[MAXLINE];
static int	Line1, Line2, nlines;
static int	nflg;		/* print line number flag */
static int	lflg;		/* print line in verbose mode */
static char	*inptr;		/* tty input buffer */

/* 05Dec2020, Maiko, Compiler warning about braces */

static struct tbl {
	char	*t_str;
	int	*t_ptr;
	int	t_val;
} *t, tbl[] = {
	{ "number",	&nflg,		TRUE },
	{ "nonumber",	&nflg,		FALSE },
	{ "list",	&lflg,		TRUE },
	{ "nolist",	&lflg,		FALSE },
	{ "eightbit",	&eightbit,	TRUE },
	{ "noeightbit",	&eightbit,	FALSE },
	{ NULLCHAR, NULL, FALSE }
};

static char NOMEM[] = "insert error (out of memory)";

extern int Numrows, NumCols, StatusLines;	/* from Jnos */

/*-------------------------------------------------------------------------*/

#if defined(ANSIPROTO) || defined(__TURBOC__)

static void	prntln(char *, int, int);
static void	putcntl(char);
static int	doprnt(int, int);
static int	esc(char **s);
static LINE	*getptr(int);
static BITMAP	*makebitmap(unsigned);
static int	edsetbit(unsigned c, char *map, unsigned val);
static int	testbit	(unsigned c, char *map );
static int	omatch(char **linp, TOKEN *pat, char *boln);
static char	*match(char *lin, TOKEN *pat, char *boln);
static char	*amatch(char *lin, TOKEN *pat, char* boln);
static void	relink(LINE *, LINE *, LINE *, LINE *);
static int	del(int, int);
static int	ins(char *);
static int	egets(char *str, int size, FILE *stream);
static char	*gettxt(int);
static int	doappend(int, int);
static char	*catsub(char *, char *, char *, char *, char *);
static char	*matchs(char *, TOKEN *, int);
static int	deflt(int, int);
static TOKEN	*makepat(char *arg, int delim);
static void	unmakepat(TOKEN *head);
static char	*maksub(char *sub, int subsz);
static TOKEN	*optpat(void);
static char	*dodash(int delim, char *src, char *map);
static char	*getfn(void);
static int	doread(int lin, char *fname);
static int	dowrite(int from, int to, char *fname, int apflg);
static int	join(int first, int last);
static int	getnum(int first);
static int	getlst(void);
static int	dolst(int line1, int line2);
static int	getone(void);
static int	move(int num);
static int	set(void);
static int	getrhs(char *sub);
static int	find(TOKEN *pat, int dir);
static int	subst(TOKEN *pat, char *sub, int gflg, int pflag);
static int	transfer(int num);
static int	docmd(int glob);
static int	ckglob(void);
static int	doglob(void);
static void	set_ed_buf(void);
static void	cleanup(void);

#else	/* !__TURBOC__ */

extern	char	*strcpy();
extern	int	*malloc();
extern	LINE	*getptr();
BITMAP	*makebitmap();
extern	char	*gettxt();
extern	char	*catsub();
extern	char	*matchs();
char	*maksub();
TOKEN	*optpat();

#endif  /* __TURBOC__ */


/*________  Macros  ________________________________________________________*/

#ifndef max
#  define max(a,b)	((a) > (b) ? (a) : (b))
#endif

#ifndef min
#  define min(a,b)	((a) < (b) ? (a) : (b))
#endif

#ifndef toupper
#  define toupper(c)	((c >= 'a' && c <= 'z') ? c-32 : c )
#endif

/*  getpat -- Translate arg into a TOKEN string  */
#define	getpat(arg)	makepat((arg), '\000')

#define nextln(l)	((l)+1 > LastLn ? 0 : (l)+1)
#define prevln(l)	((l)-1 < 0 ? LastLn : (l)-1)

#define clrbuf()	del(1, LastLn)

#define	Skip_White_Space	{while (*inptr==SP || *inptr==HT) inptr++;}


#ifdef DBGLINKS
void
validatelinks(char *hdr)
{
	LINE	*nxt = &Line0;
	int	lnum=0;

	nxt = nxt->l_next;
	while(nxt != &Line0) {
		lnum++;
		if (strlen(nxt->l_buff) > MAXLINE) {
			printf("%s: anomaly @ line %d/%p. Prev,Next=%p,%p\n",
				hdr, lnum, nxt, nxt->l_prev, nxt->l_next);
			break;
		}
		nxt = nxt->l_next;
	}
}
#endif
/*________  functions  ______________________________________________________*/

/*****************************************************************************
 *	BITMAP.C -	makebitmap, edsetbit, testbit
 *			bit-map manipulation routines.
 *
 *	Copyright (c) Allen I. Holub, all rights reserved.  This program may
 *		for copied for personal, non-profit use only.
 */
static BITMAP	*makebitmap( size )
unsigned size;
{
	/*	Make a bit map with "size" bits.  The first entry in
	 *	the map is an "unsigned int" representing the maximum
	 *	bit.  The map itself is concatenated to this integer.
	 *	Return a pointer to a map on success, 0 if there's
	 *	not enough memory.
	 */

	unsigned *map, numbytes;

	numbytes = (size >> 3) + ((size & 0x07) ? 1 : 0 );

#ifdef DEBUG
	tprintf("Making a %u bit map (%u bytes required)\n", size, numbytes);
#endif

	if( (map=(unsigned *)calloc(numbytes + sizeof(unsigned),sizeof(char))) != (unsigned *)NULL)
		*map = size;

	return ((BITMAP *)map);
}

static int edsetbit( c, map, val )
unsigned	c, val;
char		*map;
{
	/*	Set bit c in the map to val.
	 *	If c > map-size, 0 is returned, else 1 is returned.
	 */

	if( c >= *(unsigned *)map )	/* if c >= map size */
		return 0;

	map += sizeof(unsigned);	/* skip past size */

	if( val )
		map[c >> 3] |= 1 << (c & 0x07);
	else
		map[c >> 3] &= ~(1 << (c & 0x07));

	return( 1 );
}

static int testbit( c, map )
unsigned	c;
char		*map;
{
	/*	Return 1 if the bit corresponding to c in map is set.
	 *	0 if it is not.
	 */

	if( c >= *(unsigned *)map )
		return 0;

	map += sizeof(unsigned);

	return(map[ c >> 3 ] & (1 << (c & 0x07)));
}
/*********  end of BITMAP.C  ************************************************/


/*	omatch.c
 *
 * Match one pattern element, pointed at by pat, with the character at
 * **linp.  Return non-zero on match.  Otherwise, return 0.  *Linp is
 * advanced to skip over the matched character; it is not advanced on
 * failure.  The amount of advance is 0 for patterns that match null
 * strings, 1 otherwise.  "boln" should point at the position that will
 * match a BOL token.
 */
static int omatch(linp, pat, boln)
char	**linp;
TOKEN	*pat;
char	*boln;
{
	register int	advance;

	advance = -1;
	if (**linp) {
		switch (pat->tok) {
		case LITCHAR:
			if (**linp == pat->lchar)
				advance = 1;
			break;

		case BOL:
			if (*linp == boln)	/* Replaced (*linp = boln) */
				advance = 0;
			break;

		case ANY:
			if (**linp != '\n')
				advance = 1;
			break;

		case EOL:
			if (**linp == '\n')
				advance = 0;
			break;

		case CCL:
			if( testbit( **linp, pat->bitmap))
				advance = 1;
			break;

		case NCCL:
			if (!testbit (**linp, pat->bitmap))
				advance = 1;
			break;
		}
	}
	if (advance >= 0)
		*linp += advance;

	return (++advance);
}


static char *match(lin, pat, boln)
char	*lin;
TOKEN	*pat;
char	*boln;
{
	register char	*bocl, *rval, *strstart;

	if(pat == 0)
		return 0;

	strstart = lin;
	while(pat) {
		if(pat->tok == CLOSURE && pat->next) {

			/* Process a closure:
			 * first skip over the closure token to the
			 * object to be repeated.  This object can be
			 * a character class.
			 */
			pat = pat->next;

			/* Now match as many occurrences of the
			 * closure pattern as possible.
			 */
			bocl = lin;
			while( *lin && omatch(&lin, pat, boln))
				;

			/* 'Lin' now points to the character that
			 * made us fail.  Now go on to process the
			 * rest of the string.  A problem here is
			 * a character following the closure which
			 * could have been in the closure.
			 * For example, in the pattern "[a-z]*t" (which
			 * matches any lower-case word ending in a t),
			 * the final 't' will be sucked up in the while
			 * loop.  So, if the match fails, we back up a
			 * notch and try to match the rest of the
			 * string again, repeating this process
			 * recursively until we get back to the
			 * beginning of the closure.  The recursion
			 * goes, at most two levels deep.
			 */
			if((pat = pat->next)!=(TOKEN *)NULL)
			{
				int savbtwn=between;
				int savprnm=parnum;

				while(bocl <= lin) {
					if ((rval = match(lin, pat, boln))!=NULL) {
						return(rval);	/* success */
					} else {
						--lin;
						between=savbtwn;
						parnum=savprnm;
					}
				}
				return (0);	/* match failed */
			}
		} else
		if (pat->tok == OPEN) {
			if (between || parnum>=9)
				return 0;
			paropen[parnum] = lin;
			between = 1;
			pat = pat->next;
		} else
		if (pat->tok == CLOSE) {
			if (!between)
				return 0;
			parclose[parnum++] = lin;
			between = 0;
			pat = pat->next;
		} else
		if (omatch(&lin, pat, boln)) {
			pat = pat->next;
		} else {
			return (0);
		}
	}
	/* Note that omatch() advances lin to point at the next
	 * character to be matched.  Consequently, when we reach
	 * the end of the template, lin will be pointing at the
	 * character following the last character matched.  The
	 * exceptions are templates containing only a BOLN or EOLN
	 * token.  In these cases omatch doesn't advance.
	 *
	 * A philosophical point should be mentioned here.  Is $
	 * a position or a character? (i.e. does $ mean the EOL
	 * character itself or does it mean the character at the end
	 * of the line.)  I decided here to make it mean the former,
	 * in order to make the behavior of match() consistent.  If
	 * you give match the pattern ^$ (match all lines consisting
	 * only of an end of line) then, since something has to be
	 * returned, a pointer to the end of line character itself is
	 * returned.
	 */

	return ((char *)max(strstart , lin));
}


/*  amatch.c
 *
 *	Scans throught the pattern template looking for a match
 *	with lin.  Each element of lin is compared with the template
 *	until either a mis-match is found or the end of the template
 *	is reached.  In the former case a 0 is returned; in the latter,
 *	a pointer into lin (pointing to the character following the
 *	matched pattern) is returned.
 *
 *	"lin"	is a pointer to the line being searched.
 *	"pat"	is a pointer to a template made by makepat().
 *	"boln"	is a pointer into "lin" which points at the
 *			character at the beginning of the line.
 */
static char *amatch(lin, pat, boln)
char	*lin;
TOKEN	*pat;
char	*boln;
{
	between = parnum = 0;
	lin = match(lin, pat, boln);

	if (between)
		return 0;

	while (parnum<9) {
		paropen[parnum] = parclose[parnum] = "";
		parnum++;
	}
	return lin;
}


/*	doappend.c	*/

static int doappend(line, glob)
int	line, glob;
{
	int	stat;
	char	lin[MAXLINE];

	if(glob)
		return(ERR);
	CurLn = line;
	for (;;) {
#ifdef DBGLINKS
validatelinks("doappend");
#endif
		if(nflg)
			tprintf("%6d. ",CurLn+1);

#ifdef	STANDALONE
		if(fgets(lin, MAXLINE, stdin) == NULL)
#else
		if (recvline(Curproc->input, lin, MAXLINE) == -1)
#endif
			return( EOF );
		if(lin[0] == '.' && lin[1] == '\n')
			return(0);
		stat = ins(lin);
		if(stat < 0)
			return( ERR );

	}
}


/*	catsub.c	*/

static char *catsub(from, to, sub, new, newend)
char	*from, *to, *sub, *new, *newend;
{
	char	*cp, *cp2;

	for(cp = new; *sub != EOS && cp < newend;) {
		if(*sub == DITTO)
			for(cp2 = from; cp2 < to;) {
				*cp++ = *cp2++;
				if(cp >= newend)
					break;
			}
		else
		if (*sub == ESCPE) {
			sub++;
			if ('1' <= *sub && *sub <= '9') {
				char *parcl = parclose[*sub - '1'];

				for (cp2 = paropen[*sub - '1']; cp2 < parcl;) {
					*cp++ = *cp2++;
					if (cp >= newend)
						break;
				}
			} else
				*cp++ = *sub;
		} else
			*cp++ = *sub;

		sub++;
	}

	return(cp);
}


/*	ckglob.c	*/

static int ckglob()
{
	TOKEN	*glbpat;
	char	c, delim, *lin;
	int	num;
	LINE	*ptr;

	c = *inptr;

	if(c != 'g' && c != 'v')
		return(0);
	if (deflt(1, LastLn) < 0)
		return(ERR);

	delim = *++inptr;
	if(delim <= ' ')
		return(ERR);

	glbpat = optpat();
	if(*inptr == delim)
		inptr++;
	for (num=1; num<=LastLn; num++) {
		ptr = getptr(num);
		ptr->l_stat &= ~LGLOB;
		if (Line1 <= num && num <= Line2) {
			lin = gettxt(num);
			if(matchs(lin, glbpat, 0)) {
				if (c=='g') ptr->l_stat |= LGLOB;
			} else {
				if (c=='v') ptr->l_stat |= LGLOB;
			}
		}
	}
	return(1);
}


/*  deflt.c
 *	Set Line1 & Line2 (the command-range delimiters) if the file is
 *	empty; Test whether they have valid values.
 */

static int deflt(def1, def2)
int	def1, def2;
{
	if(nlines == 0) {
		Line1 = def1;
		Line2 = def2;
	}
	return ( (Line1>Line2 || Line1<=0) ? ERR : 0 );
}


/*	del.c	*/

/* One of the calls to this function tests its return value for an error
 * condition.  But del doesn't return any error value, and it isn't obvious
 * to me what errors might be detectable/reportable.  To silence a warning
 * message, I've added a constant return statement. -- RAM
 */

static int del(from, to)
int	from, to;
{
	LINE	*first, *last, *next, *tmp;
#ifdef DBGLINKS
	int	lnum;
#endif

	if(from < 1)
		from = 1;
	first = getptr(prevln(from));
	last = getptr(nextln(to));
	next = first->l_next;
#ifdef DBGLINKS
printf("del: first,last,next = %p,%p,%p (%d..%d) Cur=%d\n", first,last,next,from,to,CurLn);
lnum=from;
#endif
	while(next != last && next != &Line0) {
		tmp = next->l_next;
		free(next);
		next = tmp;
#ifdef DBGLINKS
printf("%d/%p  ", ++lnum,next);
#endif
	}
	relink(first, last, first, last);
	LastLn -= (to - from)+1;
	CurLn = prevln(from);
	return(0);
}


static int dolst(line1, line2)
int line1, line2;
{
	int oldlflg=lflg, p;

	lflg = 1;
	p = doprnt(line1, line2);
	lflg = oldlflg;
	return p;
}


/*	esc.c
 * Map escape sequences into their equivalent symbols.  Returns the
 * correct ASCII character.  If no escape prefix is present then s
 * is untouched and *s is returned, otherwise **s is advanced to point
 * at the escaped character and the translated character is returned.
 */
static int esc(s)
char	**s;
{
	register int	rval;

	if (**s != ESCPE) {
		rval = **s;
	} else {
		(*s)++;
		switch(toupper(**s)) {
		case '\000':
			rval = ESCPE;	break;
		case 'S':
			rval = ' ';	break;
		case 'N':
			rval = '\n';	break;
		case 'T':
			rval = '\t';	break;
		case 'B':
			rval = '\b';	break;
		case 'R':
			rval = '\r';	break;
		default:
			rval = **s;	break;
		}
	}
	return (rval);
}


/*  dodash.c
 *	Expand the set pointed to by *src into dest.
 *	Stop at delim.  Return 0 on error or size of
 *	character class on success.  Update *src to
 *	point at delim.  A set can have one element
 *	{x} or several elements ( {abcdefghijklmnopqrstuvwxyz}
 *	and {a-z} are equivalent ).  Note that the dash
 *	notation is expanded as sequential numbers.
 *	This means (since we are using the ASCII character
 *	set) that a-Z will contain the entire alphabet
 *	plus the symbols: [\]^_`.  The maximum number of
 *	characters in a character class is defined by maxccl.
 */
static char *dodash(delim, src, map)
int	delim;
char	*src, *map;
{

	register int	first,	last;
	char		*start;

	start = src;

	while( *src && *src != delim ) {
		if( *src != '-')
			edsetbit( esc( &src ), map, 1 );

		else
		if( src == start || *(src + 1) == delim )
			edsetbit( '-', map, 1 );
		else {
			src++;
			if( *src < *(src - 2)) {
				first = *src;
				last = *(src - 2);
			} else {
				first = *(src - 2);
				last = *src;
			}
			while( ++first <= last )
				edsetbit( first, map, 1);
		}
		src++;
	}
	return( src );
}


/*	doprnt.c	*/

static int doprnt(from, to)
int	from, to;
{
	from = (from < 1) ? 1 : from;
	to = (to > LastLn) ? LastLn : to;

	if(to != 0) {
		for(CurLn = from; CurLn <= to; CurLn++)
			prntln(gettxt(CurLn), lflg, (nflg ? CurLn : 0));
		CurLn = to;
	}

	return(0);
}


static void prntln(str, vflg, lin)
char	*str;
int	vflg, lin;
{
	if(lin)
		tprintf("%7d ",lin);
	while(*str && *str != NL) {
		if(*str < ' ' || *str >= 0x7f) {
			switch(*str) {
			case '\t':
				if(vflg)
					putcntl(*str);
				else
					tputc(*str);
				break;

			case DEL:
				tputc('^');
				tputc('?');
				break;

			default:
				putcntl(*str);
				break;
			}
		} else
			tputc(*str);
		str++;
	}
	if(vflg)
		tputc('$');
	tputc('\n');
}


static void putcntl(c)
char	c;
{
	tputc('^');
	tputc((c&31)|'@');
}


/*	egets.c	*/

static int egets(str,size,stream)
char	*str;
int	size;
FILE	*stream;
{
	int	c, count;
	char	*cp;

	for(count = 0, cp = str; size > count;) {
		c = getc(stream);
		if(c == EOF) {
			*cp++ = '\n';
			*cp = EOS;
			if(count)
				tputs("[Incomplete last line]");
			return(count);
		}
		if(c == NL) {
			*cp++ = c;
			*cp = EOS;
			return(++count);
		}
		if(c > 127) {
			if(!eightbit)		/* if not saving eighth bit */
				c = c&127;	/* strip eigth bit */
			nonascii++;		/* count it */
		}
		if(c) {
			*cp++ = c;	/* not null, keep it */
			count++;
		} else
			nullchar++;	/* count nulls */
	}
	str[count-1] = EOS;
	if(c != NL) {
		tputs("truncating line");
		truncated++;
		while((c = getc(stream)) != EOF)
			if(c == NL)
				break;
	}
	return(count);
}  /* egets */


static int doread(lin, fname)
int	lin;
char	*fname;
{
	FILE	*fp;
	int	err;
	unsigned long	bytes;
	unsigned int	lines;
	static char	str[MAXLINE];

	err = 0;
	nonascii = nullchar = truncated = 0;

	if (diag) tprintf("\"%s\" ",fname);
	if( (fp = fopen(fname, "r")) == NULL ) {
		tputs(" isn't readable.");
		return( ERR );
	}
	setvbuf(fp, NULL, _IOFBF, BUFFER_SIZE);
#if !defined(STANDALONE) && !defined(UNIX)
        fseek(fp,0L,SEEK_END);
        bytes = ftell(fp);
	fseek(fp,0L,SEEK_SET);
	if (bytes > (long)(availmem() - Memthresh)) {
		tputs(NOMEM);
                fclose(fp);
		return( ERR );
	}
#endif
	CurLn = lin;
	for(lines = 0, bytes = 0;(err = egets(str,MAXLINE,fp)) > 0;) {
		pwait(NULL);
		bytes += strlen(str);
		if( (err = ins(str)) < 0) {
			/*tputs(NOMEM);*/
			break;
		}
		lines++;
	}
	fclose(fp);
	if(err < 0)
		return(err);
	if (diag) {
		/* tprintf("%u lines %u bytes",lines,bytes); - %u for long is wrong - 05Dec2020, Maiko */
		tprintf("%u lines %lu bytes",lines,bytes);
		if(nonascii)
			tprintf(" [%d non-ascii]",nonascii);
		if(nullchar)
			tprintf(" [%d nul]",nullchar);
		if(truncated)
			tprintf(" [%d lines truncated]",truncated);
		tputc('\n');
	}
	return( err );
}  /* doread */


static int dowrite(from, to, fname, apflg)
int	from, to;
char	*fname;
int	apflg;
{
	FILE	*fp;
	int	lin, err;
	unsigned int	lines, bytes;
	char	*str;
	LINE	*lptr;

	err = 0;
	lines = bytes = 0;

	tprintf("\"%s\" ",fname);
	if((fp = fopen(fname,(apflg?"a":"w"))) == NULL) {
		tputs(" can't be opened for writing!");
		return( ERR );
	}

	setvbuf(fp, NULL, _IOFBF, BUFFER_SIZE);
	lptr = getptr(from);
	for(lin = from; lin <= to; lin++) {
		pwait(NULL);
		str = lptr->l_buff;
		lines++;
		bytes += strlen(str) + 1;	/* str + '\n' */
		if(fputs(str, fp) == EOF) {
			tputs("file write error");
			err++;
			break;
		}
		fputc('\n', fp);
		lptr = lptr->l_next;
	}
	tprintf("%u lines %u bytes\n",lines,bytes);
	fclose(fp);
	return( err );
}  /* dowrite */


/*	find.c	*/

static int find(pat, dir)
TOKEN	*pat;
int	dir;
{
	int	i, num;
	char	*lin;

	num=CurLn;
	for(i=0; i<LastLn; i++) {
		lin = gettxt(num);
		if(matchs(lin, pat, 0)) {
			return(num);
		}
		num = (dir ? nextln(num) : prevln(num));
	}
	return ( ERR );
}


/*	getfn.c	*/

static char *getfn()
{
	static char	file[128];
	char	*cp;

	if(*inptr == NL) {
		nofname=TRUE;
		strcpy(file, fname);
	} else {
		if(restricted) {
			tputs("restricted to original file");
			return NULL;
		}
		nofname=FALSE;
		Skip_White_Space;

		cp = file;
		while(*inptr && *inptr != NL && *inptr != SP && *inptr != HT)
			*cp++ = *inptr++;
		*cp = '\0';

		if(strlen(file) == 0) {
			tputs("bad file name");
			return( NULL );
		}
	}

	if(strlen(file) == 0) {
		tputs("no file name");
		return(NULL);
	}
	return( file );
}  /* getfn */


static int getnum(first)
int first;
{
	TOKEN	*srchpat;
	int	num;
	char	c;

	Skip_White_Space;

	if(*inptr >= '0' && *inptr <= '9') {	/* line number */
		for(num = 0; *inptr >= '0' && *inptr <= '9'; ++inptr) {
			num = (num * 10) + (*inptr - '0');
		}
		return num;
	}

	switch(c = *inptr) {
	case '.':
		inptr++;
		return (CurLn);

	case '$':
		inptr++;
		return (LastLn);

	case '/':
	case '?':
		srchpat = optpat();
		if(*inptr == c)
			inptr++;
		return(find(srchpat,c == '/'?1:0));

	case '-':
	case '+':
		return(first ? CurLn : 1);

	case '\'':
		inptr++;
		if (*inptr < 'a' || *inptr > 'z')
			return(EOF);
		return mark[ *inptr++ - 'a' ];

	default:
		return ( first ? EOF : 1 );	/* unknown address */
	}
}  /* getnum */


/*  getone.c
 *	Parse a number (or arithmetic expression) off the command line.
 */
#define FIRST 1
#define NOTFIRST 0

static int getone()
{
	int	c, i, num;

	if((num = getnum(FIRST)) >= 0) {
		for (;;) {
			Skip_White_Space;
			if(*inptr != '+' && *inptr != '-')
				break;	/* exit infinite loop */

                        c = *inptr++;
			if((i = getnum(NOTFIRST)) < 0)
				return ( i );
			if(c == '+')
				num += i;
			else
				num -= i;
		}
	}
	return ( num>LastLn ? ERR : num );
}  /* getone */


static int getlst()
{
	int	num;

	Line2 = 0;
	for(nlines = 0; (num = getone()) >= 0;)
	{
		Line1 = Line2;
		Line2 = num;
		nlines++;
		if(*inptr != ',' && *inptr != ';')
			break;
		if(*inptr == ';')
			CurLn = num;
		inptr++;
	}
	nlines = min(nlines, 2);
	if(nlines == 0)
		Line2 = CurLn;
	if(nlines <= 1)
		Line1 = Line2;

	return ( (num == ERR) ? num : nlines );
}  /* getlst */


/*	getptr.c	*/

static LINE *getptr(num)
int	num;
{
	LINE	*ptr;
	int	j;

	if (2*num>LastLn && num<=LastLn) {	/* high line numbers */
		ptr = Line0.l_prev;
		for (j = LastLn; j>num; j--)
			ptr = ptr->l_prev;
	} else {				/* low line numbers */
		ptr = &Line0;
		for(j = 0; j < num; j++)
			ptr = ptr->l_next;
	}
	return(ptr);
}


/*	getrhs.c	*/

static int getrhs(sub)
char	*sub;
{
	if(inptr[0] == NL || inptr[1] == NL)	/* check for eol */
		return( ERR );
	if(maksub(sub, MAXPAT) == NULL)
		return( ERR );

	inptr++;		/* skip over delimter */
	Skip_White_Space;
	if(*inptr == 'g') {
		inptr++;
		return( 1 );
	}
	return( 0 );
}


/*	gettxt.c	*/
static char *gettxt(num)
int	num;
{
	LINE	*lin;
	static char	txtbuf[MAXLINE];

	lin = getptr(num);
	strcpy(txtbuf,lin->l_buff);
	strcat(txtbuf,"\n");
	return(txtbuf);
}


/*	ins.c	*/

static int ins(str)
char	*str;
{
	char	buf[MAXLINE], *cp;
	LINE	*new, *cur, *nxt;
	int	sz;

	cp = buf;
	for (;;) {
		if((*cp = *str++) == NL)
			*cp = EOS;
		if(*cp) {
			cp++;
			continue;
		}
		sz = sizeof(LINE)+strlen(buf);
		if(
#if !defined(STANDALONE) && !defined(UNIX)
		   (sz > (long)(availmem() - Memthresh)) ||
#endif
		   (new = (LINE *)malloc(sz)) == NULL ){
			tputs(NOMEM);	/* always complain */
			return( ERR ); 	/* no memory */
		}

		new->l_stat=0;
		strcpy(new->l_buff,buf);	/* build new line */
		cur = getptr(CurLn);		/* get current line */
		nxt = getptr(nextln(CurLn));	/* get next line */
#ifdef DBGLINKS
		if(FP_OFF(new) != 8 || FP_OFF(cur) != 8 || FP_OFF(nxt) != 8) {
			if (cur != &Line0 && nxt != &Line0)
			  tprintf("ins: ptr anomaly @%d/%p nxt=%p new=%p[%s]\n",
				CurLn, cur, nxt, new, buf);
		}
#endif
		relink(cur, new, new, nxt);	/* add to linked list */
		relink(new, nxt, cur, new);
		LastLn++;
		CurLn++;

		if(*str == EOS)		/* end of line ? */
			return( 1 );

		cp = buf;
	}
}


/*	join.c	*/

static int join(first, last)
int first, last;
{
	char buf[MAXLINE];
	char *cp=buf, *str;
	int num;

	if (first<=0 || first>last || last>LastLn)
		return(ERR);
	if (first==last) {
		CurLn=first;
		return 0;
	}
	for (num=first; num<=last; num++) {
		str=gettxt(num);
		while (*str!=NL && cp<buf+MAXLINE-1)
			*cp++ = *str++;
		if (cp==buf+MAXLINE-1) {
			tputs("line too long");
			return(ERR);
		}
	}
	*cp++ = NL;
	*cp = EOS;
	del(first, last);
	CurLn=first-1;
	ins(buf);
	fchanged = TRUE;
	return 0;
}


/*	unmakepat.c
 * Free up the memory used for token string
 */
static void unmakepat(head)
TOKEN	*head;
{
	register TOKEN	*old_head;

	while (head) {
		switch (head->tok) {
		case CCL:
		case NCCL:
			free(head->bitmap);
				/* fall through to default */
		default:
			old_head = head;
			head = head->next;
			free (old_head);
			break;
		}
	}
}  /* unmakepat */


/*	makepat.c
 *
 * Make a pattern template from the strinng pointed to by arg.  Stop
 * when delim or '\000' or '\n' is found in arg.  Return a pointer to
 * the pattern template.
 *
 * The pattern template used here are somewhat different than those
 * used in the "Software Tools" book; each token is a structure of
 * the form TOKEN (see tools.h).  A token consists of an identifier,
 * a pointer to a string, a literal character and a pointer to another
 * token.  This last is 0 if there is no subsequent token.
 *
 * The one strangeness here is caused (again) by CLOSURE which has
 * to be put in front of the previous token.  To make this insertion a
 * little easier, the 'next' field of the last to point at the chain
 * (the one pointed to by 'tail) is made to point at the previous node.
 * When we are finished, tail->next is set to 0.
 */
static TOKEN *makepat(arg, delim)
char	*arg;
int	delim;
{
	 TOKEN	*head, *tail, *ntok;
	 int	error;

	/*
	 * Check for characters that aren't legal at the beginning of
	 * a template.
	 */

	if (*arg=='\0' || *arg==delim || *arg=='\n' || *arg==CLOSURE)
		return(0);

	error = 0;
	tail = head = NULL;

	while (*arg && *arg != delim && *arg != '\n' && !error) {
		ntok = (TOKEN *)mallocw(TOKSIZE);
		ntok->lchar = '\000';
		ntok->next = 0;

		switch(*arg) {
		case ANY:
			ntok->tok = ANY;
			break;

		case BOL:
			if (head == 0)	/* then this is the first symbol */
				ntok->tok = BOL;
			else {
				ntok->tok = LITCHAR;
				ntok->lchar = BOL;
			}
			break;

		case EOL:
			if (*(arg+1) == delim || *(arg+1) == '\000'
			    || *(arg+1) == '\n')
			{
				ntok->tok = EOL;
			} else {
				ntok->tok = LITCHAR;
				ntok->lchar = EOL;
			}
			break;

		case CLOSURE:
			if (head != 0) {
				switch (tail->tok) {
				case BOL:
				case EOL:
				case CLOSURE:
					return (0);
				default:
					ntok->tok = CLOSURE;
				}
			}
			break;

		case CCL:
			if(*(arg + 1) == NEGATE) {
				ntok->tok = NCCL;
				arg += 2;
			} else {
				ntok->tok = CCL;
				arg++;
			}

			if( (ntok->bitmap = makebitmap(CLS_SIZE)) != (BITMAP *)NULL)
				arg = dodash(CCLEND, arg, ntok->bitmap );
			else {
				tputs("Not enough memory for pat\n");
				error = 1;
			}
			break;

		default:
			if (*arg == ESCPE && *(arg+1) == OPEN) {
				ntok->tok = OPEN;
				arg++;
			} else
			if (*arg == ESCPE && *(arg+1) == CLOSE) {
				ntok->tok = CLOSE;
				arg++;
			} else {
				ntok->tok = LITCHAR;
				ntok->lchar = esc(&arg);
			}
		}

		if (error || ntok == 0) {
			unmakepat(head);
			return (0);
		} else
		if (head == 0) {	/* This is 1st node in the chain. */
			ntok->next = 0;
			head = tail = ntok;
		} else
		if (ntok->tok != CLOSURE) {
			/* Insert at end of list (after tail) */
			tail->next = ntok;
			ntok->next = tail;
			tail = ntok;
		} else
		if (head != tail) {
			/*
			 * More than one node in the chain.  Insert the
			 * CLOSURE node immediately in front of tail.
			 */
			(tail->next)->next = ntok;
			ntok->next = tail;
		} else {
			/*
			 * Only one node in the chain,  Insert the CLOSURE
			 * node at the head of the linked list.
			 */
			ntok->next = head;
			tail->next = ntok;
			head = ntok;
		}
		arg++;
	}
	tail->next = 0;
#ifdef DPAT
	tprintf("makepat: Head,Tail,Ntok = %p, %p, %p\n", head, tail, ntok);
	for (ntok = head; ntok; ntok=ntok->next) {
		char cc[30], x;
		
		cc[0]=0;
		switch(ntok->tok) {
		case LITCHAR:
			sprintf(cc,"Lit %c(%x)",ntok->lchar,ntok->lchar);
			break;

		case BOL:
			strcpy(cc,"BOL");
			break;

		case ANY:
			strcpy(cc,"ANY");
			break;

		case EOL:
			strcpy(cc,"EOL");
			break;

		case CLOSURE:
			strcpy(cc,"CLOSURE");
			break;

		case NCCL:
			strcpy(cc,"^");	/* fall thru */
		case CCL:
			strcat(cc,"CharClass");
			break;

		case OPEN:
			strcpy(cc,"Open");
			break;

		case CLOSE:
			strcpy(cc,"Close");
			break;
		}
		tprintf("TOKEN @ %p: %s\n", ntok, cc);
		if (ntok->tok == CCL || ntok->tok == NCCL) {
			tprintf("\tmap (%u bits): ", *(unsigned *)(ntok->bitmap));
			for (error=0; error<CLS_SIZE; error++)
				if (testbit((char)error, ntok->bitmap)) tputc((char)error);
			tputc('\n');
/*			for (error=0; error<(CLS_SIZE/8); error++)
 *				tprintf("%d) %2x\n", error, *(ntok->bitmap + sizeof(unsigned) + error));
 */		}
        }	
#endif
	return (head);
}


/*	maksub.c	*/

static char *maksub(char *sub, int subsz)
{
	int	size;
	char	delim, *cp;

	size = 0;
	cp = sub;

	delim = *inptr++;
	for(size = 0;
	    *inptr != delim && *inptr != NL && size < subsz;
	    size++)
	{
		if(*inptr == '&') {
			*cp++ = DITTO;
			inptr++;
		} else
		if((*cp++ = *inptr++) == ESCPE) {
			if (size>=subsz)
				return(NULL);

			switch(toupper(*inptr)) {
			case NL:
				*cp++ = ESCPE;  /* was ==, surely an error! */
				break;
			case 'S':
				*cp++ = SP;
				inptr++;
				break;
			case 'N':
				*cp++ = NL;
				inptr++;
				break;
			case 'T':
				*cp++ = HT;
				inptr++;
				break;
			case 'B':
				*cp++ = BS;
				inptr++;
				break;
			case 'R':
				*cp++ = CR;
				inptr++;
				break;
			case '0': {
				int i=3;
				*cp = 0;
				do {
					if (*++inptr<'0' || *inptr >'7')
						break;
					*cp = (*cp<<3) | (*inptr-'0');
				} while (--i!=0);
				cp++;
				} break;
			default:
				*cp++ = *inptr++;
				break;
			}
		}
	}
	if(size >= subsz)
		return( NULL );

	*cp = EOS;
	return( sub );
}


/*	matchs.c
 *
 * Compares line and pattern.  Line is a character string while pat
 * is a pattern template made by getpat().
 * Returns:
 *	1. A zero if no match was found.
 *
 *	2. A pointer to the last character satisfing the match
 *	   if ret_endp is non-zero.
 *
 *	3. A pointer to the beginning of the matched string if
 *	   ret_endp is zero.
 *
 * e.g.:
 *
 *	matchs ("1234567890", getpat("4[0-9]*7), 0);
 * will return a pointer to the '4', while:
 *
 *	matchs ("1234567890", getpat("4[0-9]*7), 1);
 * will return a pointer to the '7'.
 */
static char *matchs(line, pat, ret_endp)
char	*line;
TOKEN	*pat;
int	ret_endp;
{
	char	*rval, *bptr;

	bptr = line;

	while(*line) {
		if ((rval = amatch(line, pat, bptr)) == 0)
			line++;
		else {
			if(rval > bptr && rval > line)
				rval--;	    /* point to last char matched */
			rval = ret_endp ? rval : line;
			break;
		}
	}
	return (rval);
}


/*  move.c
 *	Unlink the block of lines from Line1 to Line2, and relink them
 *	after line "num".
 */

static int move(num)
int	num;
{
	int	range;
	LINE	*before, *first, *last, *after;

	if( Line1 <= num && num <= Line2 )
		return( ERR );
	range = Line2 - Line1 + 1;
	before = getptr(prevln(Line1));
	first = getptr(Line1);
	last = getptr(Line2);
	after = getptr(nextln(Line2));

	relink(before, after, before, after);
	LastLn -= range;	/* per AST's posted patch 2/2/88 */
	if (num > Line1)
		num -= range;

	before = getptr(num);
	after = getptr(nextln(num));
	relink(before, first, last, after);
	relink(last, after, before, first);
	LastLn += range;	/* per AST's posted patch 2/2/88 */
	CurLn = num + range;
	return( 1 );
}


static int transfer(num)
int num;
{
	int mid, lin, ntrans;

	if (Line1<=0 || Line1>Line2)
		return(ERR);

	mid= num<Line2 ? num : Line2;

	CurLn=num;
	ntrans=0;

	for (lin=Line1; lin<=mid; lin++) {
		if (ins(gettxt(lin)) < 0)
			return(ERR);
		ntrans++;
	}
	lin+=ntrans;
	Line2+=ntrans;

	for ( ; lin <= Line2; lin += 2 ) {
		if (ins(gettxt(lin)) < 0)
			return(ERR);
		Line2++;
	}
	return(1);
}

/*	optpat.c	*/

static TOKEN *optpat(void)
{
	char	delim, str[MAXPAT], *cp;

	delim = *inptr++;
	cp = str;
	while(*inptr != delim && *inptr != NL) {
		if(*inptr == ESCPE && inptr[1] != NL)
			*cp++ = *inptr++;
		*cp++ = *inptr++;
	}

	*cp = EOS;
	if(*str == EOS)
		return(oldpat);
	if(oldpat)
		unmakepat(oldpat);
	oldpat = getpat(str);
	return(oldpat);
}


static int set()
{
	char	word[16];
	int	i;

	if(*(++inptr) != 't') {
		if(*inptr != SP && *inptr != HT && *inptr != NL)
			return(ERR);
	} else
		inptr++;

	if ( (*inptr == NL)
#ifdef __TURBOC__
	    || (*inptr == CR)
#endif
	   )
	{
		tprintf("ed version %d.%d\n", edversion/100, edversion%100);
		tprintf("number %s, list %s\n",
			nflg?"ON":"OFF",
			lflg?"ON":"OFF");
		return(0);
	}

	Skip_White_Space;
	for(i = 0; *inptr != SP && *inptr != HT && *inptr != NL;)
		word[i++] = *inptr++;
	word[i] = EOS;
	for(t = tbl; t->t_str; t++) {
		if(strcmp(word,t->t_str) == 0) {
			*t->t_ptr = t->t_val;
			return(0);
		}
	}
	return SET_FAIL;
}


static void relink(a, x, y, b)
LINE	*a, *x, *y, *b;
{
	x->l_prev = a;
	y->l_next = b;
}


static void set_ed_buf(void)
{
	relink(&Line0, &Line0, &Line0, &Line0);
	CurLn = LastLn = 0;
/* Since our global variables are static, and we can call editor() more than
 * once per Jnos invocation, we must be sure to initialize key variables!
 */
	diag = truncflg = eightbit = 1;
	restricted = fchanged = fname[0] = 0;
	oldpat = (TOKEN *)NULL;
	memset(mark,0,sizeof(mark));
}


/*	subst.c	*/

static int subst(pat, sub, gflg, pflag)
TOKEN	*pat;
char	*sub;
int	gflg, pflag;
{
	int	lin, chngd, nchngd;
	char	*txtptr, *txt;
	char	*lastm, *m, *new, buf[MAXLINE];

	if(Line1 <= 0)
		return( SUB_FAIL );
	nchngd = 0;		/* reset count of lines changed */
	for(lin = Line1; lin <= Line2; lin++) {
		txt = txtptr = gettxt(lin);
		new = buf;
		chngd = 0;
		lastm = NULL;
		while(*txtptr) {
			if(gflg || !chngd)
				m = amatch(txtptr, pat, txt);
			else
				m = NULL;
			if(m != NULL && lastm != m) {
				chngd++;
				new = catsub(txtptr, m, sub, new, buf+MAXLINE);
				lastm = m;
			}
			if(m == NULL || m == txtptr) {
				*new++ = *txtptr++;
			} else {
				txtptr = m;
			}
		}
		if(chngd) {
			if(new >= buf+MAXLINE)
				return( SUB_FAIL );
			*new++ = EOS;
			del(lin,lin);
			ins(buf);
			nchngd++;
			if(pflag)
				doprnt(CurLn, CurLn);
		}
	}
	return (( nchngd == 0 && !gflg ) ? SUB_FAIL : nchngd);
}


#ifdef STANDALONE
/*	system.c	*/
#ifndef __TURBOC__

#define SHELL	"/bin/sh"

system(c)
char *c; {
	int pid, status;

	switch (pid = fork()) {
	case -1:
		return -1;
	case 0:
		execl(SHELL, "sh", "-c", c, (char *) 0);
		exit(-1);
	default:
		while (wait(&status) != pid)
			;
	}
	return status;
}
#endif  /* ifndef __TURBOC__ */
#endif /* STANDALONE */

/*  docmd.c
 *	Perform the command specified in the input buffer, as pointed to
 *	by inptr.  Actually, this finds the command letter first.
 */

static int docmd(glob)
int	glob;
{
	static char	rhs[MAXPAT];
	TOKEN	*subpat;
	int	c, err, line3;
	int	apflg, pflag, gflag;
	int	nchng;
	char	*fptr;

	pflag = FALSE;
	Skip_White_Space;

	c = *inptr++;
	switch(c) {
	case NL:
		if( nlines == 0 && (Line2 = nextln(CurLn)) == 0 )
			return(ERR);
		CurLn = Line2;
		return (1);

	case '=':
		tprintf("%d\n",Line2);
		break;

	case 'a':
		if(*inptr != NL || nlines > 1)
			return(ERR);

		if(doappend(Line1, glob) < 0)
			return(ERR);
		fchanged = TRUE;
		break;

	case 'c':
		if(*inptr != NL)
			return(ERR);

		if(deflt(CurLn, CurLn) < 0)
			return(ERR);

		if(del(Line1, Line2) < 0)
			return(ERR);
		if(doappend(CurLn, glob) < 0)
			return(ERR);
		fchanged = TRUE;
		break;

	case 'd':
		if(*inptr != NL)
			return(ERR);

		if(deflt(CurLn, CurLn) < 0)
			return(ERR);

		if(del(Line1, Line2) < 0)
			return(ERR);
		if(nextln(CurLn) != 0)
			CurLn = nextln(CurLn);
		fchanged = TRUE;
		break;

	case 'e':
		if(nlines > 0)
			return(ERR);
		if(fchanged)
			return CHANGED;
		/*FALL THROUGH*/
	case 'E':
		if(nlines > 0)
			return(ERR);

		if(*inptr != ' ' && *inptr != HT && *inptr != NL)
			return(ERR);

		if((fptr = getfn()) == NULL)
			return(ERR);

		clrbuf();
		if((err = doread(0, fptr)) < 0)
			return(err);

		strcpy(fname, fptr);
		fchanged = FALSE;
		break;

	case 'f':
		if(nlines > 0)
			return(ERR);

		if(*inptr != ' ' && *inptr != HT && *inptr != NL)
			return(ERR);

		if((fptr = getfn()) == NULL)
			return(ERR);

		if (nofname)
			tprintf("%s\n", fname);
		else
			strcpy(fname, fptr);
		break;

	case 'i':
		if(*inptr != NL || nlines > 1)
			return(ERR);

		if(doappend(prevln(Line1), glob) < 0)
			return(ERR);
		fchanged = TRUE;
		break;

	case 'j':
		if (*inptr != NL || deflt(CurLn, CurLn+1)<0)
			return(ERR);

		if (join(Line1, Line2) < 0)
			return(ERR);
		break;

	case 'k':
		Skip_White_Space;

		if (*inptr < 'a' || *inptr > 'z')
			return ERR;
		c= *inptr++;

		if(*inptr != ' ' && *inptr != HT && *inptr != NL)
			return(ERR);

		mark[c-'a'] = Line1;
		break;

	case 'l':
		if(*inptr != NL)
			return(ERR);
		if(deflt(CurLn,CurLn) < 0)
			return(ERR);
		if (dolst(Line1,Line2) < 0)
			return(ERR);
		break;

	case 'm':
		if((line3 = getone()) < 0)
			return(ERR);
		if(deflt(CurLn,CurLn) < 0)
			return(ERR);
		if(move(line3) < 0)
			return(ERR);
		fchanged = TRUE;
		break;

	case 'P':
	case 'p':
		if(*inptr != NL)
			return(ERR);
		if(deflt(CurLn,CurLn) < 0)
			return(ERR);
		if(doprnt(Line1,Line2) < 0)
			return(ERR);
		break;

	case 'q':
		if(fchanged)
			return CHANGED;
		/*FALL THROUGH*/
	case 'Q':
		if(*inptr == NL && nlines == 0 && !glob)
			return(EOF);
		else
			return(ERR);

	case 'r':
		if(nlines > 1)
			return(ERR);

		if(nlines == 0)			/* The original code tested */
			Line2 = LastLn;		/*	if(nlines = 0)	    */
						/* which looks wrong.  RAM  */

		if(*inptr != ' ' && *inptr != HT && *inptr != NL)
			return(ERR);

		if((fptr = getfn()) == NULL)
			return(ERR);

		if((err = doread(Line2, fptr)) < 0)
			return(err);
		fchanged = TRUE;
		break;

	case 's':
		if(*inptr == 'e')
			return(set());
		Skip_White_Space;
		if((subpat = optpat()) == NULL)
			return(ERR);
		if((gflag = getrhs(rhs)) < 0)
			return(ERR);
		if(*inptr == 'p')
			pflag++;
		if(deflt(CurLn, CurLn) < 0)
			return(ERR);
		if((nchng = subst(subpat, rhs, gflag, pflag)) < 0)
			return(ERR);
		if(nchng)
			fchanged = TRUE;
		break;

	case 't':
		if((line3 = getone()) < 0)
			return(ERR);
		if(deflt(CurLn,CurLn) < 0)
			return(ERR);
		if(transfer(line3) < 0)
			return(ERR);
		fchanged = TRUE;
		break;

	case 'W':
	case 'w':
		apflg = (c=='W');

		if(*inptr != ' ' && *inptr != HT && *inptr != NL)
			return(ERR);

		if((fptr = getfn()) == NULL)
			return(ERR);

		if(deflt(1, LastLn) < 0)
			return(ERR);
		if(dowrite(Line1, Line2, fptr, apflg) < 0)
			return(ERR);
		fchanged = FALSE;
		break;

	case 'x':
		if(*inptr == NL && nlines == 0 && !glob) {
			if((fptr = getfn()) == NULL)
				return(ERR);
			if(dowrite(1, LastLn, fptr, 0) >= 0)
				return(EOF);
		}
		return(ERR);

	case 'z':
		if(deflt(CurLn,CurLn) < 0)
			return(ERR);

#ifdef STANDALONE
		line3 = 21;
#else
		line3 = Numrows - StatusLines - 2;  /* Jnos screen height */
#endif
		switch(*inptr) {
		case '-':
			if(doprnt(Line1-line3,Line1) < 0)
				return(ERR);
			break;

		case '.':
			if(doprnt(Line1-(line3/2),Line1+(line3/2)) < 0)
				return(ERR);
			break;

		case '+':
		case '\n':
			if(doprnt(Line1,Line1+line3) < 0)
				return(ERR);
			break;
		}
		break;

	default:
		return(ERR);
	}
	return (0);
}  /* docmd */


/*	doglob.c	*/
static int doglob()
{
	int	lin, stat;
	char	*cmd;
	LINE	*ptr;

	cmd = inptr;

	for (;;) {
		for (lin=1; lin<=LastLn; lin++) {
			ptr = getptr(lin);
			if (ptr->l_stat & LGLOB)
				break;
		}
		if (lin > LastLn)
			break;

		ptr->l_stat &= ~LGLOB;
		CurLn = lin;
		inptr = cmd;
		if((stat = getlst()) < 0)
			return(stat);
		if((stat = docmd(1)) < 0)
			return(stat);
	}
	return(CurLn);
}  /* doglob */


void cleanup(void)
{
	clrbuf();	/* this releases mem allocated for lines */
	if(oldpat)	/* any saved search pattern? */
		unmakepat(oldpat);
/*	oldpat = (TOKEN *)NULL;*/
}


#ifdef dosigs
/*
 *  Software signal 2 or SIGINT is caught and the result is to resume
 *  the main loop at a command prompt.
 */
#include <setjmp.h>
jmp_buf	env;

#ifndef __TURBOC__
intr()
{
	tputs("intr()?");
	longjmp(env, 1);
}

#else

static void Catcher(void)
{
	longjmp(env, 1);
}
#endif	/* !__TURBOC__ */
#endif	/* dosigs */
#endif	/* ED */

#if defined(TED) && !defined(STANDALONE)
int
tgetchar()
{
	return (rrecvchar(Curproc->input));
}
#endif	/* TED && !STANDALONE */

/*--------  main  ---------------------------------------------------------*/

#ifdef STANDALONE
int main(argc,argv,p)
#else
int editor(argc,argv,p)
#endif
int	argc;
char	**argv;
char	*p;
{
	int	stat, i, doflush, usesession=0;
#ifndef STANDALONE
	struct session *sp;

	/* Use a session if this comes from console - N5KNX*/
	if(Curproc->input == Command->input) {
		usesession = 1;
		if((sp = newsession(argv[1],EDIT,0)) == NULLSESSION){
			return 1;
		}
#ifdef TED
		/* Put tty into raw mode so single-char responses will work */
		sp->ttystate.echo = sp->ttystate.edit = 0;
#endif
	}
#ifdef TED
	else {
		return 0;	/* ignore unless from console */
	}
#endif	/* TED */
#endif /* !STANDALONE */

#ifdef TED
#ifndef STANDALONE
	if ((i = StatusLines) != 0)
		StatusLines = 0;	/* Ted requires the full screen */
#endif	/* !STANDALONE */
	stat = ted2(argv[1]);
#ifndef STANDALONE
/*	if(usesession) {*/
		keywait(NULLCHAR,1);
		StatusLines = i;	/* reset before freesession() else cls is buggy */
		freesession(sp);
/*	}*/
#endif	/* !STANDALONE */
	return(stat);
}
#endif	/* TED */
#ifdef ED
	set_ed_buf();
#ifdef STANDALONE
	doflush=isatty(1);
#else
	doflush=TRUE;
#endif	/* STANDALONE */

	while (argc>1 && *argv[1]=='-') {
		char c;
		while((c=*++argv[1])!=0) {
			if(c=='d')
				diag = 0;
			else if (c=='r')
				restricted = 1;
			else if (c=='-')
				break;
		}
		argc--;
		argv++;
		if (c=='-') break;  /* -- => end of options */
	}
	for(i = 1; i < argc; i++) {
		strcpy(fname, argv[i]);
		if(doread(0,argv[i])==0) {
			CurLn = 1;
			break;
		}
	}

	for (;;) {

		pwait(NULL);
		
#ifdef DBGLINKS
validatelinks("main");
#endif
#ifdef dosigs
		setjmp(env);
#endif
		tputc(':');		/*  The command-line prompt  */

#ifdef dosigs
#ifndef __TURBOC__
		signal(2, intr);
#else	/* __TURBOC__ */
		signal(SIGINT, Catcher);
#endif	/* !__TURBOC__ */
#endif
		if (doflush)
			tflush();/*fflush(stdout);*/
#ifdef	STANDALONE
		if(fgets(inlin, sizeof(inlin), stdin) == NULL) {
#else
		if (recvline(Curproc->input, inlin, sizeof(inlin)) == -1) {
#endif
			strcpy(inlin,"Q\n");
		}
#ifdef STANDALONE
		/* shell escape not allowed in Jnos version! */
		if(*inlin == '!') {
			for(inptr = inlin; *inptr != NL; inptr++)
				;
			*inptr = EOS;
			system(inlin+1);
			continue;
		}
#endif /* STANDALONE */
		inptr = inlin;
		if((stat=getlst()) >= 0)
		{
			if((stat = ckglob()) != 0) {
				if(stat >= 0 && (stat = doglob()) >= 0) {
					CurLn = stat;
					continue;
				}
			} else {
				if((stat = docmd(0)) >= 0) {
					if(stat == 1)
						doprnt(CurLn, CurLn);
					continue;
				}
			}
		}		/* 05Dec2020, Maiko, Compiler brace warning */

		switch (stat)
		{
		case EOF:
			stat = 0;
			/* note : flow thru */
		case FATAL:
			if (stat == FATAL)
			{
				tputs("FATAL ERROR\n");
				stat = 1;
			}
			cleanup();
#ifndef STANDALONE
			if(usesession) {
				keywait(NULLCHAR,1);
				freesession(sp);
			}
#endif	/* !STANDALONE */
   			return(stat);
		case CHANGED:
			tputs("File has been changed.\n");
			break;
		case SET_FAIL:
			tputs("`set' command failed.\n");
			break;
		case SUB_FAIL:
			tputs("string substitution failed.\n");
			break;
		default:
			tputs("  Oops?\n");
			/*  Unrecognized or failed command (this  */
			/*  is SOOOO much better than "?" :-)	  */
		}
	}
}  /* main */
/*________  end of source code  ____________________________________________*/
#ifdef notdef
/*
 * Article 1981 of comp.os.minix:
 * From: ast@cs.vu.nl (Andy Tanenbaum)
 * Subject: Debugged version of V7 ed (replaces old one)
 * Message-ID: <1840@botter.cs.vu.nl>
 * Date: 31 Jan 88 16:40:06 GMT
 * Organization: VU Informatica, Amsterdam
 * Lines: 2365
 *
 * * * * * * * * * * * *
 *
 * Article 2015 of comp.os.minix:
 * From: ast@cs.vu.nl (Andy Tanenbaum)
 * Subject: Fix to ed.c I posted recently
 * Message-ID: <1841@botter.cs.vu.nl>
 * Date: 2 Feb 88 20:51:36 GMT
 * Organization: VU Informatica, Amsterdam
 * Lines: 4
 *
 * 2035a2036
 * > 	LastLn -= Line2-Line1+1;
 * 2046a2048
 * > 	LastLn += Line2-Line1+1;
 *
 * * * * * * * * * * * *
 *
 * Article 2053 of comp.os.minix:
 * From: dean@ndmath.UUCP (Dean Alvis)
 * Subject: ed.c tuneup
 * Keywords: minor adjustments
 * Message-ID: <930@ndmath.UUCP>
 * Date: 17 Feb 88 17:22:54 GMT
 * Organization: Math. Dept., Univ. of Notre Dame
 *
 * 	I've enjoyed using the recently posted "ed", both on a UNIX
 * system and at home under CP/M. One minor annoyance is the slowness
 * of the write command for large (>>1000 lines) files. One solution
 * is to replace the calls to gettxt() (which in turn call getptr())
 * in dowrite(). I've lost the original, so can't provide diffs (sorry),
 * but the idea is to just use a LINE * in the loop in dowrite(), as
 * follows:
 *
 * 	LINE *lptr;
 * 	...
 * 	lptr = getptr(from);
 * 	for(lin=from;lin<=to;lin++) {
 * 		str = lptr->l_buff;
 * 		++lines;
 * 		bytes += strlen(str)+1;	 COMMENT (str + '\n')
 * 		if(fputs(str,fp) == EOF) {
 * 			...
 * 		}
 * 		fputc('\n',fp);
 * 		lptr = lptr->l_next;
 * 	}
 *
 * I've tested this version, and haven't run into trouble yet.
 * Also, this method avoids the strcpy and strcat to the static buffer
 * in gettxt(), which seem unnecessary (someone correct me if i'm
 * missing something here).
 *
 * 	Another minor annoyance is the state of the current line
 * number after global commands - my preference is for the last modified
 * line to be the new current line. Replacing the return(0) by
 * return(CurLn) at the end of doglob() will produce this behavior.
 *
 * 	dean
 * --
 * dean@ndmath.UUCP
 * Dean Alvis, Math. Dept., Univ. of Notre Dame, Notre Dame IN, 46556
 *
 *
 */
#endif /* notdef */
#endif /* ED */
#endif /* EDITOR */
