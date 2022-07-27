/*
 * JNOS 2.0
 *
 * $Id: ttydriv.c,v 1.2 2012/03/20 16:38:06 ve4klm Exp $
 *
 * TTY input line editing
 * Copyright 1991 Phil Karn, KA9Q
 * split screen by G. J. van der Grinten, PA0GRI
 * command recall, and status line by Johan. K. Reinalda, WG7J
 */
#ifdef __TURBOC__
#include <conio.h>
#endif
#ifdef UNIX
#include <curses.h>
#undef TRUE
#undef FALSE
#endif
#include <ctype.h>
#include "global.h"
#include "mbuf.h"
#include "session.h"
#include "tty.h"
#include "socket.h"
  
extern FILE *Rawterm;
  
#define OFF 0
#define ON  1
  
#define LINESIZE    256
  
#define CTLR    18      /* reprint current line */
#define CTLU    21      /* delete current line in total */
#define CTLW    23      /* erase last word including preceding space */
#define CTLZ    26      /* EOF char in dos */
#define CTLB    02      /* use as F3 in dos but no editing */
#define DEL 0x7f
  
extern int Numrows,Numcols;

#ifdef UNIX
/*
 * 19Jan2012, Maiko, We need to capture the ARROW keys, which in linux
 * actually comes in as a sequence of 3 bytes. Apparently for windows it
 * is 2 bytes. Capture the entire sequence and map to proper ARROW code.
 *
 * 27Jan2012, Maiko, Should clarify why this function was written. IF the
 * sysop runs JNOS in an xterm (ie, TERM=xterm) or any terminal supporting
 * the ANSI escape sequences, arrow keys show up as a byte sequences. The
 * original JNOS code had no way to deal with this, so I wrote a function
 * to do this. IF the sysop runs JNOS in a linux console (ie, TERM=linux),
 * then arrow keys map to a single byte, and this function will just let
 * the single byte pass through unchanged.
 */
static int capture_seq_keys (int key_i)
{
	if (key_i == 27)	/* first escape character */
	{
		if (kbread () == 91)	/* second escape character */
		{
			switch (kbread ())
			{
				case 65:
					key_i = UPARROW;
					break;

				case 66:
					key_i = DNARROW;
					break;
			}
		}
	}

	return key_i;
}
#endif	/* end of UNIX */

#ifdef SPLITSCREEN

extern int StatusLines;
static int  Lastsize = 1;
static char Lastline[LINESIZE+1] = "\n";
  
char MainColors = 0;    /* Gets initialized to the startup attribute */
char SplitColors = WHITE+(GREEN<<4);
  
/* Accept characters from the incoming tty buffer and process them
 * (if in cooked mode) or just pass them directly (if in raw mode).
 *
 * Echoing (if enabled) is direct to the raw terminal. This requires
 * recording (if enabled) of locally typed info to be done by the session
 * itself so that edited output instead of raw input is recorded.
 * Control-W added by g1emm again.... for word delete.
 * Control-B/Function key 3 added by g1emm for line repeat.
 */
  
struct mbuf *
ttydriv(sp,c)
struct session *sp;
int c;
{
    struct mbuf *bp;
    char *cp,*rp;
    int cnt;
  
    switch(sp->ttystate.edit){
        case OFF:
            bp = ambufw(1);
            *bp->data = c;
            bp->cnt = 1;
            if(sp->ttystate.echo){
                if(sp->split){
                /* Save cursor in top screen */
                    sp->tsavex = wherex();
                    sp->tsavey = wherey();
  
                /* Access bottom screen */
                    window(1,Numrows-1,Numcols,Numrows);
                    textattr(SplitColors);
                    gotoxy(sp->bsavex,sp->bsavey);
                    highvideo();
                    putch(c);
                    lowvideo();
                    cputs("_\b");
                    sp->bsavex = wherex();
                    sp->bsavey = wherey();
  
                /* Back to top screen */
                    window(1,1+StatusLines,Numcols,Numrows-2);
                    textattr(MainColors);
                    gotoxy(sp->tsavex,sp->tsavey);
                } else {
#ifdef UNIX
                    putch(c);
#else
                    if(StatusLines)
                        putch(c);
                    else
                        putc(c,Rawterm);
#endif
                }
            }
            return bp;
        case ON:
            if(sp->ttystate.line == NULLBUF)
                sp->ttystate.line = ambufw(LINESIZE);
  
            bp = sp->ttystate.line;
            cp = bp->data + bp->cnt;
        /* Perform cooked-mode line editing */
#ifdef  UNIX
			/* 21Jan2012, Maiko, Moved code into a separate function */
			c = capture_seq_keys (c);
#endif
        /* Allow for international character sets - WG7J */
                switch(c){
                    case '\r':  /* CR and LF both terminate the line */
                    case '\n':
                        if(sp->ttystate.crnl)
                            *cp = '\n';
                        else
                            *cp = c;
                        if(sp->ttystate.echo){
                            if(sp->split){
                                highvideo();
                                rp = bp->data;
                                while(rp < cp) {
                                    putch(*rp++);
                                }
                                lowvideo();
                                clreol();
                                cputs(Eol);
                                clreol();
                                sp->tsavex = wherex();
                                sp->tsavey = wherey();
  
                    /* Access bottom screen */
                                window(1,Numrows-1,Numcols,Numrows);
                                textattr(SplitColors);
                                clrscr();
                                cputs("_\b");
                                sp->bsavex = wherex();
                                sp->bsavey = wherey();
  
                    /* Back to top screen */
                                window(1,1+StatusLines,Numcols,Numrows-2);
                                textattr(MainColors);
                                gotoxy(sp->tsavex,sp->tsavey);
                            } else {
#ifdef UNIX
                                cputs(Eol);
#else
                                if(StatusLines)
                                    cputs(Eol);
                                else
                                    fputs(Eol,Rawterm);
#endif
                            }
                        }
                        bp->cnt += 1;
                        sp->ttystate.line = NULLBUF;
                        Lastsize = bp->cnt;
                        memcpy(Lastline, bp->data, (size_t)Lastsize);
                        return bp;
                    case DEL:
                    case '\b':  /* Character delete */
                        if(bp->cnt != 0){
                            bp->cnt--;
                            if(sp->ttystate.echo){
                                if(sp->split){
                        /* Save cursor in top screen */
                                    sp->tsavex = wherex();
                                    sp->tsavey = wherey();
  
                        /* Access bottom screen */
                                    window(1,Numrows-1,Numcols,Numrows);
                                    textattr(SplitColors);
                                    gotoxy(sp->bsavex,sp->bsavey);
                                    cputs(" \b\b_\b");
                                    sp->bsavex = wherex();
                                    sp->bsavey = wherey();
  
                        /* Back to top screen */
                                    window(1,1+StatusLines,Numcols,Numrows-2);
                                    textattr(MainColors);
                                    gotoxy(sp->tsavex,sp->tsavey);
                                } else {
#ifdef UNIX
                                    cputs("\b \b");
#else
                                    if(StatusLines)
                                        cputs("\b \b");
                                    else
                                        fputs("\b \b",Rawterm);
#endif
                                }
                            }
                        }
                        break;
                    case CTLR:  /* print line buffer */
                        if(sp->ttystate.echo){
                            if(sp->split) {
                    /* Save cursor in top screen */
                                sp->tsavex = wherex();
                                sp->tsavey = wherey();
  
                    /* Access bottom screen */
                                window(1,Numrows-1,Numcols,Numrows);
                                textattr(SplitColors);
                                gotoxy(sp->bsavex,sp->bsavey);
                                clrscr();
                                rp = bp->data;
                                while (rp < cp)
                                    putch(*rp++) ;
                                cputs("_\b");
                                sp->bsavex = wherex();
                                sp->bsavey = wherey();
  
                    /* Back to top screen */
                                window(1,1+StatusLines,Numcols,Numrows-2);
                                textattr(MainColors);
                                gotoxy(sp->tsavex,sp->tsavey);
                            } else {
#ifdef UNIX
                                cputs("^R");
                                cputs(Eol);
                                rp = bp->data;
                                while (rp < cp)
                                    putch(*rp++);
#else
                                if(StatusLines)
                                    cprintf("^R%s",Eol);
                                else
                                    fprintf(Rawterm,"^R%s",Eol) ;
                                rp = bp->data;
                                while (rp < cp)
                                    if(StatusLines)
                                        putch(*rp++);
                                    else
                                        putc(*rp++,Rawterm) ;
#endif
                            }
                        }
                        break ;
                    case CTLU:  /* Line kill */
                        if(sp->split) {
                /* Save cursor in top screen */
                            sp->tsavex = wherex();
                            sp->tsavey = wherey();
  
                /* Access bottom screen */
                            window(1,Numrows-1,Numcols,Numrows);
                            textattr(SplitColors);
                            gotoxy(sp->bsavex,sp->bsavey);
                            cputs(" \b");
                            while(bp->cnt != 0){
                                cputs("\b \b");
                                bp->cnt--;
                            }
                            cputs("_\b");
                            sp->bsavex = wherex();
                            sp->bsavey = wherey();
  
                /* Back to top screen */
                            window(1,1+StatusLines,Numcols,Numrows-2);
                            textattr(MainColors);
                            gotoxy(sp->tsavex,sp->tsavey);
                        } else {
                            while(bp->cnt != 0){
                                bp->cnt--;
                                if(sp->ttystate.echo)
#ifdef UNIX
                                    cputs("\b \b");
#else
                                if(StatusLines)
                                    cputs("\b \b");
                                else
                                    fputs("\b \b",Rawterm);
#endif
                            }
                        }
                        break;
                    case CTLB:  /* Use last line to finish current */
                        cnt = bp->cnt;      /* save count so far */
  
                        while(bp->cnt != 0){
                            bp->cnt--;
                            if(sp->ttystate.echo)
#ifdef UNIX
                                cputs("\b \b");
#else
                            if(StatusLines)
                                cputs("\b \b");
                            else
                                fputs("\b \b", Rawterm);
#endif
                        }
                        bp->cnt = cnt;
                        if(bp->cnt < (Lastsize-1)){
                            memcpy(bp->data+bp->cnt, &Lastline[bp->cnt], (size_t)(Lastsize-1) - bp->cnt);
                            bp->cnt = Lastsize-1;
                        }
                        *(bp->data + bp->cnt) = '\0';   /* make it a string */
                        if(sp->ttystate.echo)
#ifdef UNIX
                            cputs(bp->data);
#else
                        if(StatusLines)
                            cputs(bp->data);
                        else
                            fputs(bp->data, Rawterm);   /* repaint line */
#endif
                        break ;
                    case CTLW:  /* erase word */
                        cnt = 0 ;   /* we haven't seen a printable char yet */
                        while(bp->cnt != 0){
                            *(bp->data + bp->cnt--) = '\n';
                            if(sp->ttystate.echo)
#ifdef UNIX
                                cputs("\b \b");
#else
                            if(StatusLines)
                                cputs("\b \b");
                            else
                                fputs("\b \b", Rawterm);
#endif
                            if (isspace((int)*(bp->data + bp->cnt))) {
                                if (cnt)
                                    break ;
                            } else {
                                cnt = 1 ;
                            }
                        }
                        break ;
                    case UPARROW:  /* Recall previous command - WG7J */
                        if(Histry) {
                /* Blank out what's already there */
                            while(bp->cnt != 0 && sp->ttystate.echo){
                                bp->cnt--;
                                cputs("\b \b");
                            }
                /* Recall last command */
                            strcpy(bp->data,Histry->cmd);
                            bp->cnt = strlen(Histry->cmd);
                /* Adjust history */
                            Histry = Histry->prev;
                /* repaint line */
                            if(sp->ttystate.echo)
                                cputs(bp->data);
                        }
                        break ;
                    case DNARROW:  /* Recall next command - WG7J */
                        if(Histry) {
                /* Blank out what's already there */
                            while(bp->cnt != 0 && sp->ttystate.echo){
                                bp->cnt--;
                                cputs("\b \b");
                            }
                /* Adjust history */
                            Histry = Histry->next;
                /* Recall last command */
                            strcpy(bp->data,Histry->cmd);
                            bp->cnt = strlen(Histry->cmd);
                /* repaint line */
                            if(sp->ttystate.echo)
                                cputs(bp->data);
                        }
                        break ;
                    default:    /* Ordinary character */
                        *cp = c;
                        bp->cnt++;
  
            /* ^Z apparently hangs the terminal emulators under
             * DoubleDos and Desqview. I REALLY HATE having to patch
             * around other people's bugs like this!!!
             */
                        if(sp->ttystate.echo &&
#ifndef AMIGA
                            c != CTLZ &&
#endif
                        bp->cnt < LINESIZE-1){
                            if(sp->split) {
                    /* Save cursor in top screen */
                                sp->tsavex = wherex();
                                sp->tsavey = wherey();
  
                    /* Access bottom screen */
                                window(1,Numrows-1,Numcols,Numrows);
                                textattr(SplitColors);
                                gotoxy(sp->bsavex,sp->bsavey);
                                putch(c);
                                cputs("_\b");
                                sp->bsavex = wherex();
                                sp->bsavey = wherey();
  
                    /* Back to top screen */
                                window(1,1+StatusLines,Numcols,Numrows-2);
                                textattr(MainColors);
                                gotoxy(sp->tsavex,sp->tsavey);
                            } else {
#ifdef UNIX
                                putch(c);
#else
                                if(StatusLines)
                                    putch(c);
                                else
                                    putc(c,Rawterm);
#endif
                            }
  
                        } else if(bp->cnt >= LINESIZE-1){
#ifdef UNIX
                            write(1, "\007", 1);
#else
                            putc('\007',Rawterm);   /* Beep */
#endif
                            bp->cnt--;
                        }
                        break;
                }
                break;
            }
            return NULLBUF;
    }
  
#else /* SPLITSCREEN */
  
  
/* Accept characters from the incoming tty buffer and process them
 * (if in cooked mode) or just pass them directly (if in raw mode).
 *
 * Echoing (if enabled) is direct to the raw terminal. This requires
 * recording (if enabled) of locally typed info to be done by the session
 * itself so that edited output instead of raw input is recorded.
 */
    struct mbuf *
    ttydriv(sp,c)
    struct session *sp;
    int c;
    {
        struct mbuf *bp;
        char *cp,*rp;
  
        switch(sp->ttystate.edit){
            case OFF:
                bp = ambufw(1);
                *bp->data = c;
                bp->cnt = 1;
                if(sp->ttystate.echo)
#ifdef UNIX
                    putch(c);
#else
                putc(c,Rawterm);
#endif
  
                return bp;
            case ON:
                if(sp->ttystate.line == NULLBUF)
                    sp->ttystate.line = ambufw(LINESIZE);
  
                bp = sp->ttystate.line;
                cp = bp->data + bp->cnt;
        /* Allow for international character sets - WG7J */
        /* Perform cooked-mode line editing */
#ifdef  UNIX
			/* 21Jan2012, Maiko, Moved code into a separate function */
			c = capture_seq_keys (c);
#endif
                switch(c) {
                    case '\r':  /* CR and LF both terminate the line */
                    case '\n':
                        if(sp->ttystate.crnl)
                            *cp = '\n';
                        else
                            *cp = c;
                        if(sp->ttystate.echo)
#ifdef UNIX
                            cputs(Eol);
#else
                        fputs(Eol,Rawterm);
#endif
  
                        bp->cnt += 1;
                        sp->ttystate.line = NULLBUF;
                        return bp;
                    case DEL:
                    case '\b':  /* Character delete */
                        if(bp->cnt != 0){
                            bp->cnt--;
                            if(sp->ttystate.echo)
#ifdef UNIX
                                cputs("\b \b");
#else
                            fputs("\b \b",Rawterm);
#endif
                        }
                        break;
                    case CTLR:  /* print line buffer */
                        if(sp->ttystate.echo){
#ifdef UNIX
                            cputs("^R");
                            cputs(Eol);
                            rp = bp->data;
                            while (rp < cp)
                                putch(*rp++);
#else
                            fprintf(Rawterm,"^R%s",Eol) ;
                            rp = bp->data;
                            while (rp < cp)
                                putc(*rp++,Rawterm) ;
#endif
                        }
                        break ;
                    case CTLU:  /* Line kill */
                        while(bp->cnt != 0){
                            bp->cnt--;
                            if(sp->ttystate.echo){
#ifdef UNIX
                                cputs("\b \b");
#else
                                fputs("\b \b",Rawterm);
#endif
                            }
                        }
                        break;
                    case UPARROW:  /* Recall previous command - WG7J */
                        if(Histry) {
                /* Blank out what's already there */
                            while(bp->cnt != 0 && sp->ttystate.echo){
                                bp->cnt--;
                                cputs("\b \b");
                            }
                /* Recall last command */
                            strcpy(bp->data,Histry->cmd);
                            bp->cnt = strlen(Histry->cmd);
                /* Adjust history */
                            Histry = Histry->prev;
                /* repaint line */
                            if(sp->ttystate.echo)
                                cputs(bp->data);
                        }
                        break ;
                    case DNARROW:  /* Recall next command - WG7J */
                        if(Histry) {
                /* Blank out what's already there */
                            while(bp->cnt != 0 && sp->ttystate.echo){
                                bp->cnt--;
                                cputs("\b \b");
                            }
                /* Adjust history */
                            Histry = Histry->next;
                /* Recall last command */
                            strcpy(bp->data,Histry->cmd);
                            bp->cnt = strlen(Histry->cmd);
                /* repaint line */
                            if(sp->ttystate.echo)
                                cputs(bp->data);
                        }
                        break ;
                    default:    /* Ordinary character */
                        *cp = c;
                        bp->cnt++;
  
            /* ^Z apparently hangs the terminal emulators under
             * DoubleDos and Desqview. I REALLY HATE having to patch
             * around other people's bugs like this!!!
             */
                        if(sp->ttystate.echo &&
#ifndef AMIGA
                            c != CTLZ &&
#endif
                        bp->cnt < LINESIZE-1){
#ifdef UNIX
                            putch(c);
#else
                            putc(c,Rawterm);
#endif
                        } else if(bp->cnt >= LINESIZE-1){
#ifdef UNIX
                            write(1, "\007", 1);
#else
                            putc('\007',Rawterm);   /* Beep */
#endif
                            bp->cnt--;
                        }
                        break;
                }
                break;
        }
        return NULLBUF;
    }
  
#endif /* SPLITSCREEN */
