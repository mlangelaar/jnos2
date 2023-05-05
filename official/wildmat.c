/*
 * @(#)wildmat.c 1.3 87/11/06   Public Domain.
 *
From: rs@mirror.TMC.COM (Rich Salz)
Newsgroups: net.sources
Subject: Small shell-style pattern matcher
Message-ID: <596@mirror.TMC.COM>
Date: 27 Nov 86 00:06:40 GMT
  
There have been several regular-expression subroutines and one or two
filename-globbing routines in mod.sources.  They handle lots of
complicated patterns.  This small piece of code handles the *?[]\
wildcard characters the way the standard Unix(tm) shells do, with the
addition that "[^.....]" is an inverse character class -- it matches
any character not in the range ".....".  Read the comments for more
info.
  
For my application, I had first ripped off a copy of the "glob" routine
from within the find(1) source, but that code is bad news:  it recurses
on every character in the pattern.  I'm putting this replacement in the
public domain.  It's small, tight, and iterative.  Compile with -DTEST
to get a test driver.  After you're convinced it works, install in
whatever way is appropriate for you.
  
I would like to hear of bugs, but am not interested in additions; if I
were, I'd use the code I mentioned above.
*/
/*
**  Do shell-style pattern matching for ?, \, [], and * characters.
**  Might not be robust in face of malformed patterns; e.g., "foo[a-"
**  could cause a segmentation violation.
**
**  Written by Rich $alz, mirror!rs, Wed Nov 26 19:03:17 EST 1986.
*/
  
/*
 * Modified 6Nov87 by John Gilmore (hoptoad!gnu) to return a "match"
 * if the pattern is immediately followed by a "/", as well as \0.
 * This matches what "tar" does for matching whole subdirectories.
 *
 * The "*" code could be sped up by only recursing one level instead
 * of two for each trial pattern, perhaps, and not recursing at all
 * if a literal match of the next 2 chars would fail.
 */
  
/* Modified by Anders Klemets to take an array of pointers as an optional
   argument. Each part of the string that matches '*' is returned as a
   null-terminated, malloced string in this array.
 */

#include <ctype.h>
#include "global.h"

/* 25Oct2014, Maiko (VE4KLM), Also put '?' values into $1, $2, and so on */
#define CAPTURE_QUESTION_MARK_VALUES

/* 02Dec2014, Maiko (VE4KLM), Make the '+' value function 'properly' :) */
#define PROPER_PLUS_SIGN_FUNCTIONALITY

static int Star __ARGS((char *s,char *p,char **argv,int single));
// static int haveDot __ARGS((register char *c,register int len));
  
#ifndef PROPER_PLUS_SIGN_FUNCTIONALITY
static int
haveDot(s,len)
register char *s;
register int len;
{
    while (len-- > 0)
        if (*s++ == '.')
            return TRUE;
    return FALSE;
}
#endif

static int
Star(s,p,argv,single)
register char *s;
register char *p;
register char **argv;
int single;
{
    char *cp = s;
    while (wildmat(cp, p, argv) == FALSE)
        if(*++cp == '\0')
            return -1;
#ifndef PROPER_PLUS_SIGN_FUNCTIONALITY
    if ((single == TRUE) && haveDot(s, (int)(cp - s)))
        return -1;
#endif
    return (int)(cp - s);
}
  
int
wildmat(s,p,argv)
register char *s;
register char *p;
register char **argv;
{
    register int last;
    register int matched;
    register int reverse;
    register int cnt;
    int single;

    for(; *p; s++,p++){
        switch(*p){
            case '\\':
            /* Literal match with following character; fall through. */
                p++;
            default:
             /*   if(*s != *p)   */
                if (tolower(*s) != tolower(*p))
                    return FALSE;
                continue;
            case '?':
            /* Match anything. */
                if(*s == '\0')
                    return FALSE;
/*
 * 25Oct2014, Maiko (VE4KLM), Also put '?' values into $1, $2, and so on
 *
 * 22Oct2019, Maiko, I had (and still do not) NO clue how this wildcard crap
 * really works, my brain isn't wired that way, some folks exhibit brilliance
 * with this type of stuff, I'm not one of them ! Need to make sure 'argv' is
 * actually a valid pointer (not NULL) before writing to it or else BOOM !
 *  (also cleaned it up, no need for strncpy to do this, like really)
 *
 * Thanks to Ron (VE3CGR) for catching this quite some time ago actually :|
 *
 * Why is this CAPTURE even here ? It was 'requested' by N6MEF, he has not
 * complained about it, so I have to assume it's working, or perhaps he is
 * not using it anymore, so just leave it alone otherwise, scary stuff.
 */
#ifdef CAPTURE_QUESTION_MARK_VALUES
				if (argv)
				{
                	*argv = malloc(2);
                	**argv++ = *s;
					**argv = '\0';
				}
#endif
                continue;
            case '*':
            case '+':
                single = (*p == '+');
  
            /* Trailing star matches everything. */
                if(argv == NULLCHARP)
                    return *++p ? 1 + Star(s, p, NULLCHARP, single) : TRUE;
                if(*++p == '\0'){
                    cnt = strlen(s);
#ifndef PROPER_PLUS_SIGN_FUNCTIONALITY
                    if ((single == TRUE) && haveDot(s, cnt))
                        return FALSE;
#endif
                } else {
                    if((cnt = Star(s, p, argv+1, single)) == -1)
                        return FALSE;
                }
                *argv = mallocw((unsigned)cnt+1);
                strncpy(*argv,s,(size_t)cnt);
                *(*argv + cnt) = '\0';
/* 02Dec2014, Maiko (VE4KLM), Make the '+' value function 'properly' :) */
#ifdef PROPER_PLUS_SIGN_FUNCTIONALITY
				if (single && !cnt)
					return FALSE;
#endif
                return TRUE;
            case '[':
            /* [^....] means inverse character class. */
                reverse = (p[1] == '^' || p[1] == '!') ? TRUE : FALSE;
                if(reverse)
                    p++;
                for(last = 0400, matched = FALSE; *++p && *p != ']'; last = *p){
                /* This next line requires a good C compiler. */
                    if(*p == '-' ? *s <= *++p && *s >= last : *s == *p)
                        matched = TRUE;
                }
                if(matched == reverse)
                    return FALSE;
                continue;
        }
    }
    /* For "tar" use, matches that end at a slash also work. --hoptoad!gnu */
    return *s == '\0' || *s == '/';
}
  
  
#ifdef  TEST
#include <stdio.h>
  
extern char *gets();
  
main()
{
    char pattern[80];
    char text[80];
    char *argv[80], *cp;
    int cnt;
  
    while (TRUE){
        printf("Enter pattern:  ");
        if(gets(pattern) == NULL)
            break;
        while (TRUE){
#ifdef UNIX
            memset(argv, 0, 80 * sizeof (char *));
#else
            bzero(argv,80*sizeof(char *));
#endif
            printf("Enter text:  ");
            if(gets(text) == NULL)
                exit(0);
            if(text[0] == '\0')
                /* Blank line; go back and get a new pattern. */
                break;
            printf("      %d\n", wildmat(text, pattern, argv));
            for(cnt = 0; argv[cnt] != NULLCHAR; ++cnt){
                printf("String %d is: '%s'\n",cnt,argv[cnt]);
                free(argv[cnt]);
            }
        }
    }
    exit(0);
}
#endif  /* TEST */
