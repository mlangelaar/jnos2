/*  General Mail Utilities
 *  culled from other files to reduce unnecessary cross references.
 *  This material is believed to be in the public domain.
 */

/*
 * 18Jun2021, Maiko, Interesting, in order to resolve the
 * strcasestr() prototype, you have to do this before ANY
 * include or it doesn't work, you'll still get warnings.
 *  (need this for new ':' rewrite tag)
 */
#define _GNU_SOURCE
#include <string.h>

#ifdef MSDOS
#include <io.h>
#endif
#include <fcntl.h>
#include <ctype.h>
#include "global.h"
#include "socket.h"
#include "mailutil.h"
#include "mailbox.h"
#include "smtp.h"
#include "files.h"
#include "bm.h"
#include "index.h"
#ifdef UNIX
#include "unix.h"
#endif  
#include "commands.h"
  
/*  Jan 92  Bill Simpson
 *      The following routines were combined from smtpserv.c,
 *      pop3cli.c and nntpcli.c
 */
  
/* June 93 - Johan. K. Reinalda, WG7J
 * Mail file indexing added.
 */

#ifdef POP3CLIENT
  
#ifdef POPT4
extern int32 Popt4;
#endif

/* Receive message from socket, copying to file.
 * Returns number of lines received, -1 indicates error
 */
int
recvmail(s,buf,len,fp,trace)
int s;      /* Socket index */
char *buf;  /* Line buffer */
unsigned len;   /* Length of buffer */
FILE *fp;   /* File to copy into */
int trace;  /* to trace or not to trace */
{
    int lines = 0;
    int continued = 0;  /* n5knx */
  
#ifdef POPT4
    while (j2alarm(Popt4*1000), recvline(s,buf,len) != -1) {
#else
    while (recvline(s,buf,len) != -1) {
#endif
        register char *p = buf;
  
#ifdef POPT4
        j2alarm(0);
#endif
        if (trace >= 4)
            log(s,"<==%s", buf);
  
        if (!continued) {
            /* check for end of message . or escaped .. */
            if (*p == '.') {
                if (*++p == '\n') {
                    if (trace >= 3)
                        log(s,"received %d lines", lines);
                    return lines;
                } else if ( *p != '.' ) {
                    p--;
                }
            } else if (strncmp(p,"From ",5) == 0) {
                /* for UNIX mail compatiblity */
                (void) putc('>',fp);
            }
            ++lines;
        }
        /* Append to data file */
        if (fputs(p,fp) == -1) {
            if ( trace >= 1 )
                log(s,"POP write error %d after %d lines", errno, lines);
            return -1;
        }
        continued = (strchr(p,'\n') == NULLCHAR);
    }
#ifdef POPT4
    j2alarm(0);
#endif
    if ( trace >= 1 )
        log(s,"POP receive error %d after %d lines", errno, lines);
    return -1;
}
#endif
  
#if (defined(POP2CLIENT) || defined(POP3CLIENT))
  
/* Copy from the work file into the mailbox.
 * -1 indicates error
 */
int
copymail(filename,buf,len,wfp,trace)
char *filename; /* Target Filename */
char *buf;  /* Line buffer */
unsigned len;   /* Length of buffer */
FILE *wfp;  /* File to copy from */
int trace;      /* to trace or not to trace */
{
    FILE *mfp = NULLFILE;
  
    while (mlock(Mailspool,filename)) {
        j2pause(10000);  /* 10 seconds */
    }
  
    sprintf(buf,"%s/%s.txt",Mailspool,filename);
    if ((mfp = fopen(buf,"a+")) == NULL) {
        if ( trace >= 1 )
            log(-1,"*** Unable to open %s!!!\n", buf);
        tprintf("\n*** Unable to open %s!!!\n", buf);
        rmlock(Mailspool,filename);
        return -1;
    }
  
    rewind(wfp);            /* start of new mail, just to be safe ! */
    fseek(mfp,0,SEEK_END);  /* End of mailbox file */
  
    /* loop for all lines in file */
    while(fgets(buf,len,wfp) != NULL) {
        pwait(NULL);    /* give other processes time in long copy */
        fputs(buf,mfp); /* Write to the mailbox */
    }
    fclose(mfp);
  
    /* Update the index */
    IndexFile(filename,0);
  
    rmlock(Mailspool,filename);
    return 0;
}
#endif
  
  
/*  Jan 92  Bill Simpson
 *      The following routines were extracted from smtpserv.c
 *      and smtpcli.c, since they are used from several places.
 */
  
static void mklockname(char *lockname,char *dir,char *id) {
  
    if (id == NULLCHAR || !*id) strcpy(lockname, dir);
    else sprintf(lockname,"%s/%s",dir,id);
    dirformat(lockname);
    strcat(lockname,".lck");
}
  
/* create mail lockfile */
int
mlock(dir,id)
char *dir,*id;
{
    int fd;
    char lockname[FILE_PATH_SIZE];
  
    /* Try to create the lock file in an atomic operation */
    mklockname(lockname,dir,id);

    /* KO4KS reports O_EXCL fails in some emulated-DOS environments, and
     * apparently it can't be trusted on an Amiga.  So, to be safe, we at
     * least can detect an existing lock file. -- N5KNX  1.10L
     */
    if(access(lockname, 0) == 0)
        return -1;

    if((fd = open(lockname, O_WRONLY|O_EXCL|O_CREAT,0600)) == -1) {
        return -1;
    }
    close(fd);
    return 0;
}
  
  
/* remove mail lockfile */
int
rmlock(dir,id)
char *dir,*id;
{
    char lockname[FILE_PATH_SIZE];
  
    mklockname(lockname,dir,id);
    return(unlink(lockname));
}
  
  
/*      Jan 92  Bill Simpson
 *      The following routines were extracted from mailbox.c,
 *      since they are used from several places.
 */
  
  
/*
 * 16Jun2021, New ':' tag added to rewrite file so we can specify that
 * rewrite rules are specific to a particular station or call (From).
 *
 * IMPORTANT : Only call this from fbbfwd.c and smtpserv.c, the other
 * regular rewrite_address() calls can stay as is, the stub is further
 * below this function.
 *
 * Read the rewrite file for lines where the first word is a regular
 * expression and the second word are rewriting rules. The special
 * character '$' followed by a digit denotes the string that matched
 * a '*' character. The '*' characters are numbered from 1 to 9.
 * Example: the line "*@*.* $2@$1.ampr.org" would rewrite the address
 * "foo@bar.xxx" to "bar@foo.ampr.org".
 * $H is replaced by our hostname, and $$ is an escaped $ character.
 * If the third word on the line has an 'r' character in it, the function
 * will recurse with the new address.
 */
char *rewrite_address_new (char *addr, unsigned int filecode, char *fromsender)
{
    char *argv[10], buf[PLINELEN], *cp, *cp2, *retstr;
    int cnt, userspecific;
    FILE *fp;

#ifdef	DEBUG
	if (fromsender != NULLCHAR)
    	log (-1, "debug - rewrite_address [%s] [%d] [%s]", addr, filecode, fromsender);
#endif

#ifdef TRANSLATEFROM
    if (filecode == REWRITE_TO) cp=Rewritefile;
    else if (filecode == REWRITE_FROM) cp=Translatefile;
#ifdef SMTP_REFILE
    else if (filecode == REWRITE_FROM_TO) cp=Refilefile;
#endif
    else return NULLCHAR;
#else
#ifdef SMTP_REFILE
    if (filecode == REWRITE_FROM_TO) cp=Refilefile;
    else
#endif
    cp=Rewritefile;
#endif

    if ((fp = fopen(cp,READ_TEXT)) == NULLFILE)
        return NULLCHAR;
  
    memset((char *)argv,0,10*sizeof(char *));
    while(fgets(buf,sizeof(buf),fp) != NULLCHAR)
    {
        if(*buf == '#')     /* skip commented lines */
            continue;
 
        if((cp = strpbrk(buf," \t:")) == NULLCHAR) /* get the first word */
            continue;

	/* 17Jun2021, Maiko (VE4KLM), added the new ':' tag */
	userspecific = 0; if (*cp == ':') userspecific = 1;

        *cp = '\0';

        if(!wildmat(addr,buf,argv))
            continue;       /* no match */
	/*
	 * 16Jun2021, Maiko (VE4KLM), added a new tag ':' to let us make the rule
	 * specific to a sending station (FROM field of a message), so we can put
	 * it into a HOLD area for example - thanks Charles (N2NOV) for the idea.
	 */
	cp++;

	/*
	 * 25Jun2021, Maiko, Oops if we get a NULL fromsender, which can happen
	 * if a JNOS bbs user tries 'sb all@ww', it will actually get rewritten
	 * to the first user specific rule it finds ! Thanks for catching this
	 * Charles (N2NOV). So if the fromsender is NULL and this is a user
	 * specific rule, then IGNORE this rule and continue to next one.
	 *
	 * The original rewrite_address() has no idea who fromsender is at
	 * the time of composition, so as a result it has no choice but to
	 * pass a NULL value for fromsender to this 'new function' here :|
	 *
	 */

	if (userspecific)
	{
		char tsender[50], *sptr = tsender;

		/* 25Jun2021, Maiko, Ignore this rewrite rule - original rewrite_address() */
		if (fromsender == NULLCHAR)
			continue;

		while (*cp && (*cp != ' '))
			*sptr++ = *cp++;

		*sptr = 0; /* terminate tsender (FROM) string */

#ifdef	DEBUG
		log (-1, "debug - From Rule [%s] Msg From [%s]", tsender, fromsender);
#endif
		if (!strcasestr (fromsender, tsender))	/* make it case compare just to be safe */
			continue;

#ifdef	DEBUG
		log (-1, "debug - rule applied");
#endif
		/* and just pass through to process the rule */
	}

        // rip(++cp);
	rip (cp);	/* small change needed for new ':' tag above ! */

        /* scan past additional whitespaces */
        while (*cp == ' ' || *cp == '\t') ++cp;
        cp2 = retstr = (char *) callocw(1,PLINELEN);
        while(*cp != '\0' && *cp != ' ' && *cp != '\t')
            if(*cp == '$') {
                if(isdigit(*(++cp)))
                    if(argv[*cp - '0'-1] != '\0')
                        strcat(cp2,argv[*cp - '0'-1]);
                if(*cp == 'h' || *cp == 'H') /* Our hostname */
                    strcat(cp2,Hostname);
                if(*cp == '$')  /* Escaped $ character */
                    strcat(cp2,"$");
                cp2 = retstr + strlen(retstr);
                cp++;
            }
            else
                *cp2++ = *cp++;
        for(cnt=0; argv[cnt] != NULLCHAR; ++cnt)
            free(argv[cnt]);
        fclose(fp);
        /* If there remains an 'r' character on the line, repeat
         * everything by recursing.
         */
        if(strpbrk(cp,"rR") != NULLCHAR) {
            if((cp2 = rewrite_address_new(retstr,filecode, fromsender)) != NULLCHAR) {
                free(retstr);
                return cp2;
            }
        }
        return retstr;
    }
    fclose(fp);
    return NULLCHAR;
}

/*
 * 16Jun2021, Maiko (VE4KLM), instead of changing the argument list on all
 * the rewrite_address() calls elsewhere in JNOS, I am going to rename the
 * rewrite_address() to rewrite_address_new() but ONLY in this source file,
 * then create a 'new' rewrite_address() ONLY in this source file, use it
 * a stub function to the new rewrite_address_new() with a new NULL arg.
 */
char *rewrite_address (char *addr, unsigned int filecode)
{
	return rewrite_address_new (addr, filecode, NULLCHAR);
}
  
int dorewrite(int argc, char *argv[], void *p) {
    char *address;

#ifdef SMTP_REFILE
    address = rewrite_address(argv[1], REWRITE_FROM_TO);
    if (address) j2tputs("Refiled ");
    else 
#endif
    address = rewrite_address(argv[1], REWRITE_TO);

    tprintf("to: %s\n", address ? address : argv[1]);
    if(address)
        free(address);
#ifdef TRANSLATEFROM
    address = rewrite_address(argv[1], REWRITE_FROM);
    if (address) {
        tprintf("from: %s\n", address);
        free(address);
    }
#endif
    return 0;
}
  
