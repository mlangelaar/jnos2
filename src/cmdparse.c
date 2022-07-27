/* Parse command line, set up command arguments Unix-style, and call function.
 * Note: argument is modified (delimiters are overwritten with nulls)
 *
 * Copyright 1991 Phil Karn, KA9Q
 *
 * Improved error handling by Brian Boesch of Stanford University
 * Feb '91 - Bill Simpson
 *              bit16cmd for PPP
 * Mar '91 - Glenn McGregor
 *              handle string escaped sequences
 */
#include <ctype.h>
#ifdef MSDOS
#include <conio.h>
#endif
#include "global.h"
#include "proc.h"
#include "cmdparse.h"
#include "session.h"
#include "pktdrvr.h"
#include "iface.h"
#include "socket.h"
  
struct boolcmd {
    char *str;      /* Token */
    int val;        /* Value */
};
  
static struct boolcmd Boolcmds[] = {
    { "y",            1 },      /* Synonyms for "true" */
    { "yes",          1 },
    { "true",         1 },
    { "on",           1 },
    { "1",            1 },
    { "set",          1 },
    { "enable",       1 },
  
    { "n",            0 },      /* Synonyms for "false" */
    { "no",           0 },
    { "false",        0 },
    { "off",          0 },
    { "0",            0 },
    { "clear",        0 },
    { "disable",      0 },

    { NULLCHAR,	0 }
};

static char *stringparse __ARGS((char *line));
  
static char *
stringparse(line)
char *line;
{
    register char *cp = line;
    unsigned long num;
  
    while ( *line != '\0' && *line != '\"' ) {
        if ( *line == '\\' ) {
            line++;
            switch ( *line++ ) {
                case 'n':
                    *cp++ = '\n';
                    break;
                case 't':
                    *cp++ = '\t';
                    break;
                case 'v':
                    *cp++ = '\v';
                    break;
                case 'b':
                    *cp++ = '\b';
                    break;
                case 'r':
                    *cp++ = '\r';
                    break;
                case 'f':
                    *cp++ = '\f';
                    break;
                case 'a':
                    *cp++ = '\a';
                    break;
                case '\\':
                    *cp++ = '\\';
                    break;
                case '\?':
                    *cp++ = '\?';
                    break;
                case '\'':
                    *cp++ = '\'';
                    break;
                case '\"':
                    *cp++ = '\"';
                    break;
                case 'x':
			--line;
                    num = strtoul( line, &line, 16 );
                    *cp++ = (char) num;
                    break;
                case '0':
                case '1':
                case '2':
                case '3':
                case '4':
                case '5':
                case '6':
                case '7':
			--line;
                    num = strtoul( line, &line, 8 );
                    *cp++ = (char) num;
                    break;
                case '\0':
                    return NULLCHAR;
                default:
                    *cp++ = *(line - 1);
                    break;
            };
        } else {
            *cp++ = *line++;
        }
    }
  
    if ( *line == '\"' )
        line++;         /* skip final quote */
    *cp = '\0';             /* terminate string */
    return line;
}
  
#ifdef LOCK
int Kblocked;
char *Kbpasswd;
#endif
  
int
cmdparse(cmds,line,p)
struct cmds cmds[];
register char *line;
void *p;
{
    struct cmds *cmdp;
    char *argv[NARG],*cp;
    char **pargv;
    int argc,i;
#ifdef REDIRECT
    int prev_s = -1;
    int s, prev_flow;
#endif
  
    /* Remove cr/lf */
    rip(line);
  
    for(argc = 0;argc < NARG;argc++)
        argv[argc] = NULLCHAR;
  
    for(argc = 0;argc < NARG;){
        register int qflag = FALSE;
  
        /* Skip leading white space */
        while(*line == ' ' || *line == '\t')
            line++;
        if(*line == '\0')
            break;
        /* return if comment character first non-white */
        if ( argc == 0  &&  *line == '#' )
            return 0;
        /* Check for quoted token */
        if(*line == '"'){
            line++; /* Suppress quote */
            qflag = TRUE;
        }
        argv[argc++] = line;    /* Beginning of token */
  
        if(qflag){
            /* Find terminating delimiter */
            if((line = stringparse(line)) == NULLCHAR){
                return -1;
            }
        } else {
            /* Find space or tab. If not present,
             * then we've already found the last
             * token.
             */
#ifdef old
            if((cp = strchr(line,' ')) == NULLCHAR
            && (cp = strchr(line,'\t')) == NULLCHAR){
                break;
            }
#endif
            for(cp=line;*cp;cp++) {
                if(*cp == ' ' || *cp == '\t')
                    break;
            }
        /* if we didn't reach end of line yet, cutoff this arg here */
            if(*cp == '\0')
                break;
            *cp++ = '\0';
            line = cp;
        }
    }
    if (argc < 1) {         /* empty command line */
        argc = 1;
        argv[0] = "";
    }
  
#ifdef LOCK
    /* Check to see if this is the Command session.
     * If so, check to see if the keyboard is locked
     * Added 12/12/91 WG7J
     * Also, skips this check if it is a command from the AT queue
     * 12/16/93 KO4KS
     */
    if((Curproc->input == Command->input) &&
        strcmp ("Mbox forwarding",Curproc->name) &&
        strcmp ("AT handler", Curproc->name))
        if(Kblocked) { /*check argv[0] for password!*/
            if(strcmp(argv[0],Kbpasswd)) {
                j2tputs("\nKeyboard remains locked\n");
                return 0;
            }
            Command->ttystate.echo = 1; /* turn character echo back on ! */
            Kblocked=0;   /* correct password, so unlock */
            return 0;
        }
#endif
  
    /* Look up command in table; prefix matches are OK */
    /* Not case sensitive anymore - WG7J */
    for(cmdp = cmds;cmdp->name != NULLCHAR;cmdp++){
        if(strncmpi(argv[0],cmdp->name,strlen(argv[0])) == 0)
            break;
    }
    if(cmdp->name == NULLCHAR) {
        if(cmdp->argc_errmsg != NULLCHAR)
            tprintf("%s\n",cmdp->argc_errmsg);
        return -1;
    } else {
#ifdef REDIRECT
        /* Use the new write-only-file type of socket to let us redirect
         * stdout to a file.  Note that commands inplemented as separate procs
         * will generally switch to a different session, so that stdout is NOT
         * preserved, and no redirection takes place.  Oh well, guess that's
         * an unfortunate limitation for now...  Jnos 1.10L n5knx
        */
        if(argc > 2 &&  /* cmd [options] > outfile */
          Curproc->input == Command->input &&  /* Command session only */
          (!strcmp(argv[argc-2],">") || !strcmp(argv[argc-2],">>"))) {
            s = sockfopen(argv[argc-1], (strlen(argv[argc-2])==1)?WRITE_TEXT:APPEND_TEXT);
            if(s==-1) {
                tprintf("Error %d opening %s\n", errno, argv[argc-1]);
                return -1;
            }
            else {
                prev_s = Curproc->output;
                Curproc->output = s;
                prev_flow = Command->flowmode;
                Command->flowmode = 0;   /* no -more- prompting */
                argv[--argc]=NULLCHAR;
                argv[--argc]=NULLCHAR;
            }
        }
#endif
        if(argc < cmdp->argcmin) {
            /* Insufficient arguments */
            tprintf("Usage: %s\n",cmdp->argc_errmsg);
            i = -1;  /* return code */
        } else {
            if(cmdp->stksize == 0){
                i=(*cmdp->func)(argc,argv,p);
            } else {
                /* Make private copy of argv and args,
                 * spawn off subprocess and return.
                 */
                pargv = (char **)callocw(argc,sizeof(char *));
                for(i=0;i<argc;i++)
                    pargv[i] = j2strdup(argv[i]);

                i = (newproc(cmdp->name,cmdp->stksize,
                     (void (*)__ARGS((int,void*,void*)))cmdp->func,argc,pargv,
                     p,1) == NULLPROC);  /* 0 => OK, -1 => err */
            }
        }
    }
#ifdef REDIRECT
    if(prev_s != -1) {
        close_s(Curproc->output);
        Curproc->output = prev_s;
        Command->flowmode = prev_flow;
    }
#endif
    return i;  /* 0 => OK, else err */
}
  
/* Call a subcommand based on the first token in an already-parsed line */
int
subcmd(tab,argc,argv,p)
struct cmds tab[];
int argc;
char *argv[];
void *p;
{
    register struct cmds *cmdp;
    char **pargv;
    int found = 0;
    int i;
 
    // tprintf ("argc %d\n", argc);
 
    /* Strip off first token and pass rest of line to subcommand */
    if (argc < 2)
	{
#ifndef	SUBCMD_WASTEFULL_PROMPTING
		/* 24Feb2015, Maiko, Is this SUBCMD option even possible ??? */
        if (argc < 1)
            j2tputs("SUBCMD - Don't know what to do?\n");
        else
            tprintf("\"%s\" - takes at least one argument\n",argv[0]);
        return -1;
#endif
    }
	else	/*
			 * 24Feb2015, Added this else, I think it's stupid to tell the
			 * user or sysop that you need at least one more argument. Why
			 * not just dump them the usage right away (seriously). Easily
			 * done by (keeping) setting found = 0 !
			 */
    {
		argc--;
		argv++;

		for(cmdp = tab;cmdp->name != NULLCHAR;cmdp++)
		{
			/* Not case sensitive anymore - WG7J */
			if(strncmpi(argv[0],cmdp->name,strlen(argv[0])) == 0)
			{
				found = 1;
				break;
			}
	    }
	}

    if(!found){
        char buf[77];
        memset(buf,' ',sizeof(buf));
        buf[75] = '\n';
        buf[76] = '\0';
#ifndef	SUBCMD_WASTEFULL_PROMPTING
        j2tputs("valid subcommands:\n");
#endif
        for(i = 0, cmdp = tab;cmdp->name != NULLCHAR; cmdp++, i = (i+1)%5) {
            strncpy(&buf[i*15],cmdp->name,strlen(cmdp->name));
            if(i == 4) {
                j2tputs(buf);
                memset(buf,' ',sizeof(buf));
                buf[75] = '\n';
                buf[76] = '\0';
            }
        }
        if(i != 0)
            j2tputs(buf);
        return -1;
    }
    if(argc < cmdp->argcmin){
        if(cmdp->argc_errmsg != NULLCHAR)
            tprintf("Usage: %s\n",cmdp->argc_errmsg);
        return -1;
    }
    if(cmdp->stksize == 0){
        return (*cmdp->func)(argc,argv,p);
    } else {
        /* Make private copy of argv and args */
        pargv = (char **)callocw(argc,sizeof(char *));
        for(i=0;i<argc;i++)
            pargv[i] = j2strdup(argv[i]);
        newproc(cmdp->name,cmdp->stksize,
        (void (*)__ARGS((int,void*,void*)))cmdp->func,argc,pargv,p,1);
        return(0);
    }
}

/* 23Oct2014, Maiko (VE4KLM), new function to just check for boolcmd, so
 * I can maintain backwards compatibility with older autoexec.nos files. I
 * need this for a more user friendly usage function that I've written for
 * the revamped dolog() command in main.c - also this returns 1 (not a 0)
 * on a successful command match.
 */

int isboolcmd (int *var, char *cmdstr)
{ 
	struct boolcmd *bc;

	for (bc = Boolcmds; bc->str != NULLCHAR; bc++)
	{
		if (!strcmpi (cmdstr, bc->str))
		{
            	*var = bc->val;
            	return 1;
       	}
   	}

	return 0;
}

/* Subroutine for setting and displaying boolean flags */
int
setbool(var,label,argc,argv)
int *var;
char *label;
int argc;
char *argv[];
{
    struct boolcmd *bc;
  
    if(argc < 2){
        tprintf("%s: %s\n",label,*var ? "on":"off");
        return 1;
    }

	if (isboolcmd (var, argv[1]))
		return 0;
/*
 * 23Oct2014, Maiko, replaced with above isboolcmd() - cuts duplicate code
 *
    for(bc = Boolcmds;bc->str != NULLCHAR;bc++){
        if(strcmpi(argv[1],bc->str) == 0){
            *var = bc->val;
            return 0;
        }
    }
 *
 */
    j2tputs("Valid options:");
    for(bc = Boolcmds;bc->str != NULLCHAR;bc++)
        if(tprintf(" %s",bc->str) == EOF)
            return 1;
    tputc('\n');
    return 1;
}
  
  
#ifdef PPP
/* Subroutine for setting and displaying bit values */
int
bit16cmd(bits,mask,label,argc,argv)
int16 *bits;
int16 mask;
char *label;
int argc;
char *argv[];
{
    int doing;  /* setbool will set it to 0 or 1. [was = (*bits & mask);] */
    int result = setbool( &doing, label, argc, argv );
  
    if ( !result ) {
        if ( doing )
            *bits |= mask;
        else
            *bits &= ~mask;
    }
    return result;
}
#endif
  
/* Subroutine for setting and displaying long variables (formerly setlong) */
/* 29Sep2009, Maiko, renamed to setint32, specific to 32 bit vars now ! */

int setint32 (int32 *var, char *label, int argc, char **argv)
{
	if (argc < 2)
	{
		/*
		 * I can get away with using "%d" in the format string, since
		 * the 'int32' is an integer on both 32 bit and 64 bit systems.
		 */
		tprintf ("%s: %d\n",label, *var);
		return 1;
	}
	else
	{
		*var = atoi (argv[1]);
		return 0;
	}
}

/* Subroutine for setting and displaying short variables */
int
setshort(var,label,argc,argv)
unsigned short *var;
char *label;
int argc;
char *argv[];
{
    if(argc < 2){
        tprintf("%s: %u\n",label,*var);
        return 1;
    } else {
        *var = atoi(argv[1]);
        return 0;
    }
}
/* Subroutine for setting and displaying integer variables */
int
setint(var,label,argc,argv)
int *var;
char *label;
int argc;
char *argv[];
{
    if(argc < 2){
        tprintf("%s: %i\n",label,*var);
        return 1;
    } else {
        *var = atoi(argv[1]);
        return 0;
    }
  
}
  
/* Subroutine for setting and displaying unsigned integer variables */
int
setuns(var,label,argc,argv)
unsigned *var;
char *label;
int argc;
char *argv[];
{
    if(argc < 2){
        tprintf("%s: %u\n",label,*var);
        return 1;
    } else {
        *var = atoi(argv[1]);
        return 0;
    }
}
  
/* Subroutine for setting and displaying int variables (with range check) */
int
setintrc(var, label, argc, argv, minval, maxval)
int16 *var;
char *label;
int argc;
char *argv[];
int minval;
int16 maxval;
{
    int tmp;
  
    if (argc < 2)
        tprintf("%s: %u\n", label, *var);
    else {
        tmp = atoi(argv[1]);
        if (isalpha(*argv[1]) || tmp < minval || tmp > maxval) {
            tprintf("%s must be %i..%i\n", label, minval, maxval);
            return 1;
        }
        *var = (int16)tmp;
    }
    return 0;
}
  
/* Set flags on ax.25 interfaces - WG7J */
/* 13Aug2010, Maiko (VE4KLM), flag must be int32 as in iface struct */
  
int setflag (int argc, char *ifname, int32 flag, char *cmd)
{
    struct iface *ifp;
    struct boolcmd *bc;
  
    if(argc == 1) {
        for(ifp = Ifaces;ifp != NULLIF;ifp = ifp->next)
            if(ifp->flags & flag)
                tprintf("%s\n",ifp->name);
    } else {
        if((ifp = if_lookup(ifname)) == NULLIF){
            tprintf(Badinterface,ifname);
            return 1;
        }
        /*
        if(ifp->type != CL_AX25) {
            j2tputs("not an AX.25 interface\n");
            return 1;
        }
        */
        if(argc == 2) {
            /* Show the value of the flag */
            if(ifp->flags & flag)
                j2tputs("On\n");
            else
                j2tputs("Off\n");
        } else {
            for(bc = Boolcmds;bc->str != NULLCHAR;bc++)
                if(strcmpi(cmd,bc->str) == 0){
                    if(bc->val)
                        ifp->flags |= flag;
                    else
                        ifp->flags &= ~flag;
                    return 0;
                }
            /* Invalid option ! */
            j2tputs("Valid options:");
            for(bc = Boolcmds;bc->str != NULLCHAR;bc++)
                if(tprintf(" %s",bc->str) == EOF)
                    return 1;
            tputc('\n');
        }
    }
    return 0;
}
  
  
