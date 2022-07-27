
/*
 * 19Jun2020, Maiko (VE4KLM), finally decided to move my callsigns lists
 * functions into their own source file. These functions were exclusively
 * written for just the APRS code roughly 18 years ago ? but I have found
 * them to be very useful in other areas of the JNOS package.
 *
 * 21Jun2020, Maiko, the docallslist() function was only called through
 * the 'aprs calls ...' console command up until recently, but I want to
 * be able to use the same function directly for other console commands,
 * such as 'mbox winlinkcalls'. Unfortunately this requires manipulation
 * of argc and the argv index (offset) to make it work, which adds a bit
 * to the complexity of the function - a small price to pay (I figure).
 */

#include "global.h"

extern int getusage (char*, char*);	/* 28May2020, Maiko (VE4KLM), new usage function.
					 * 25Jun2020, Maiko, Added prefix argument.
					 */
/*
 * 28Jun2020, Maiko, No more using 'strings on the fly', save memory, avoid
 * mistakes, be consistent, and define them once in one place, new module.
 */
#include "j2strings.h"

/*
 * Pointer (to pointer) where we store a callsign list. This pointer
 * is initialized and the callsign list is loaded by the dofwdtorf ()
 * function at the end of this module.
 */
static char **fwdtorf = (char**)0;

/* 22Sep2002, Maiko, New 'postorf' list of callsigns (gating posit info) */
static char **postorf = (char**)0;

/* 22Sep2002, Maiko, New 'stattorf' list of callsigns (gating stat info) */
static char **stattorf = (char**)0;

/* 06May2002, Maiko, New 'bantorf' list of callsigns */
static char **bantorf = (char**)0;

/* 08May2002, Maiko, New 'ignorerf' list of callsigns */
static char **ignorerf = (char**)0;

/* 14Jun2004, Maiko, New 'ip45845' list for ip access to 45845 service */
static char **ip45845 = (char**)0;

#ifdef	J2MFA
/* 21Nov2020, Maiko, New 'MFAexclude' list for subnets NOT to use MFA on */
static char **MFAexclude = (char**)0;
#endif

/* 12Sep2004, Maiko, New 'wxtorf' for SP1LOP (Janusz) */
static char **wxtorf = (char**)0;

/* 17Sep2005, Maiko, New 'micetorf' for SP1LOP (Janusz) */
static char **micetorf = (char**)0;

/* 21Dec2008, Maiko, New 'obj2rf' for AB9FT (Josh) */
static char **obj2rf = (char**)0;

#ifdef	WINLINK_SECURE_LOGIN
/* 03Dec2020, Maiko, MULTIPLE_WL2K_CALLS removed from makefile, changed to WINLINK_SECURE_LOGIN */

/* 04Jun2020, Maiko (VE4KLM), using this 18 years later for my list of winlink calls */

static char **winlinkcalls = (char**)0;

/*
 * 04Jun2020, Maiko (VE4KLM), traverse list of calls, and invoke the passed function
 * on each of those calls, used for generating the hashes in the initial ;FW sent to
 * a Winlink CMS server, to allow us to download mail for multiple winlink calls.
 *
 * The passed function will take a callsign string and return a HASH string ...
 *
 */

int wl2kmulticalls (char* (*mcfunc) (int, char*, char*), int user, char *PQchallenge)
{
	char **tptr = winlinkcalls;

	char *lptr;

	/*
	 * 20Jun2020, Maiko, now that the primary call is defined here,
	 * there now needs to be a list, even just for the one entry !
	 */
	if (tptr == (char**)0)
	{
		log (user, "you forgot to define your primary winlink call");
		return 0;
	}

	/*
	 * 20Jun2020, Maiko (VE4KLM), oops, need to skip the first entry since
 	 * it's now the primary winlink call, replacing the use of Wl2kcall[]
	 * and dombwl2kcall(), both in mboxcmd.c, makes more sense to have all
	 * the winlink callsigns in one list now.
	 */
	tptr++;

	if (tptr == (char**)0)	/* perfectly fine to not have a list of additional calls */
		return 1;
	
	while (*tptr)
	{
		// log (user, "callsign [%s]", *tptr);

		/* '*tptr' points to a string with callsign */
		lptr = (*mcfunc) (user, *tptr, PQchallenge);

		if (!lptr)		/* serious error, tell forward.c to abort */
			return 0;

		else
			tprintf (" %s|%s", *tptr, lptr);	/* add call and passcode */

		log (user, "additional [ %s|%s]", *tptr, lptr); 

		tptr++;		/* next one in the list */
	}

	return 1;
}

/*
 * 20Jun2020, Maiko (VE4KLM), now get the primary winlink callsign from the
 * new '**winlinkcalls' list. There is no more Wl2kcall[] and dombwl2kcall()
 * in mboxcmd.c source file, both have been removed. Just make sure you put
 * your primary winlink account callsign as the first one in the list !
 */

char *wl2kmycall (void)
{
	char **tptr, ***topptr = &winlinkcalls;

	if (*topptr)
	{
		tptr = *topptr;

		return (*tptr);
	}

	return ((char*)0);
}

#endif

/*
 * Function that compares the passed callsign with the entries in
 * our callsign list. If there is a match, then return 1. If there
 * is no match, then return 0.
 *
 * 01Jun2001, VE4KLM, Redesign this so that WILDCARDS can now be
 * used to match callsign patterns. Now sysops can use entries like
 * the following :
 *
 * aprs fwdtorf ve4 k*6 **4 va4
 *
 * 06May2002, Maiko, Restructured functions so that I can reuse the same
 * code when I start to maintain multiple callsign lists down the road. For
 * example, I may want to 'ban' some msg recipients while allowing general
 * callsign patterns to be gated to RF. I may want to 'ban' msgs orginating
 * from specific source callsigns (or callsign patterns).
 */

/*
 * 06May2002, Maiko, The following function used to be just the plain
 * old original 'callsign_allowed'. I've restructured it so that I can
 * pass the list type, allowing me to reuse the code for multiple list
 * functions, as explained above.
 */
int callsign_in_list (char **tptr, char *callsign)
{
	char *lptr, *cptr;
	int matched = 0;

	/* 02Jun2002, Maiko, Oops !!! NOS will crash, if list not defined ! */
	if (tptr == (char**)0)
			return 0;

	while (*tptr)
	{
		cptr = callsign;
		lptr = *tptr;
		matched = 1;

		while (*lptr)
		{
			if (*lptr != '*' && *lptr != *cptr)
			{
 				matched = 0;
				break;
			}

			cptr++;
			lptr++;
		}

		if (matched)	/* matched !!! return immediately */
			return 1;

		tptr++;		/* next pattern in our list */
	}

	return 0;	/* callsign is not in the list - no match was found */
}

/* 14Jun2004, Maiko, Nice - Use this list for IP access for 45845 srv */
int ip_allow_45845 (char *ipaddr)
{
	return callsign_in_list (ip45845, ipaddr);
}

#ifdef	J2MFA
/* 21Nov2020, Maiko, New 'MFAexclude' list for subnets NOT to use MFA on */
int ip_exclude_MFA (char *ipaddr)
{
	return callsign_in_list (MFAexclude, ipaddr);
}
#endif

/* Orig func (now a stub), callsigns (recipients) permitted to go to RF */
int callsign_allowed (char *callsign)
{
	return callsign_in_list (fwdtorf, callsign);
}

/* 21Dec2008, Maiko, callsigns allowed to gate Objects to RF */
int callsign_can_obj (char *callsign)
{
	return callsign_in_list (obj2rf, callsign);
}

/* 12Sep2004, Maiko, callsigns allowed to gate WX to RF */
int callsign_can_wx (char *callsign)
{
	return callsign_in_list (wxtorf, callsign);
}

/* 17Sep2005, Maiko, callsigns allowed to gate MICE to RF */
int callsign_can_mice (char *callsign)
{
	return callsign_in_list (micetorf, callsign);
}

/* 22Sep2002, Maiko, callsigns (source) allowed to gate posits to RF */
int callsign_can_pos (char *callsign)
{
	return callsign_in_list (postorf, callsign);
}

/* 22Sep2002, Maiko, callsigns (source) allowed to gate status to RF */
int callsign_can_stat (char *callsign)
{
	return callsign_in_list (stattorf, callsign);
}

/* 06May2002, Maiko, callsigns (recipients) banned from going to RF */
int callsign_banned (char *callsign)
{
	return callsign_in_list (bantorf, callsign);
}

/* 08May2002, Maiko, callsigns (source) ignored from the RF side */
int callsign_ignored (char *callsign)
{
	return callsign_in_list (ignorerf, callsign);
}

/*
 * Load into memory a list of callsigns and/or callsign patterns that we
 * will allow to be forwarded from the internet to the local RF port.
 *
 * This function uses a somewhat elegant method to create a DYNAMICALLY
 * allocated list of strings in memory. Nothing is presumed, memory is
 * allocated as needed to store a simple list of strings. Using pointers
 * like this is a good way to conserve memory.
 *
 * May 2, 2001, VE4KLM, Pointers to pointers are the way to go :-)
 *
 * Use the function, callsign_allowed (char*) to verify whether or not
 * a particular callsign is in the callsign list.
 *
 * 07May2002, Maiko, Changed this function to 'docallslist', now it
 * is used to build all callsign lists, not just the original fwdtorf
 * one anymore (when the function was called 'dofwdtorf'. One new sub
 * command added internal to the function to pick which list to init,
 * we are now going into 'pointers to a pointer to a pointer' :-)
 *
 * 12Sep2004, Maiko, Added 'wxtorf' for Janusz (SP1LOP)
 * 17Sep2005, Maiko, Added 'micetorf' for Janusz (SP1LOP)
 * 21Dec2008, Maiko, Added 'obj2rf' for Josh (AB9FT)
 *
 * 21Jun2020, Maiko, manipulate argc and argv index (offset) to allow me
 * to directly call this function from other cmdparse command lists, for
 * instance, 'mbox winlinkcalls'. Also replace usagecallslist() with the
 * new getusage () function - have to change it anyways, since this code
 * could now be possibly called for a while bunch of different stuff.
 */

int docallslist (int argc, char *argv[], void *p)
{
	char **tptr, ***topptr, *curlist;	/* 22Jun2020, Maiko, Added curlist */

	int cnt, arg_offset = 2;

#ifdef	DONT_COMPILE
	tprintf ("argc %d -", argc);
	for (cnt = 0; cnt < argc; cnt++)
		tprintf  (" [%s]", argv[cnt]);
	tprintf ("\n");
#endif

	/* if (!stricmp (argv[0], "calls")) */

	if (!strnicmp (argv[0], j2str_calls, strlen (argv[0])))	/* be more forgiving */
	{
		if (argc < 2)
		{
			/* 21Jun2020, Maiko, no more hardcoded version */
			getusage ("aprs", j2str_calls);

			return 0;
		}

		if (!stricmp (argv[1], "fwdtorf"))
			topptr = &fwdtorf;
		else if (!stricmp (argv[1], "bantorf"))
			topptr = &bantorf;
		else if (!stricmp (argv[1], "ignorerf"))
			topptr = &ignorerf;
		/* 22Sep2002, Maiko, New one for gating POS info to RF */
		else if (!stricmp (argv[1], "postorf"))
			topptr = &postorf;
		/* 22Sep2002, Maiko, New one for gating STAT info to RF */
		else if (!stricmp (argv[1], "stattorf"))
			topptr = &stattorf;
		/* 14Jun2004, Maiko, New one for ip access to 45845 service */
		else if (!stricmp (argv[1], "ip45845"))
			topptr = &ip45845;
		/* 12Sep2004, Maiko, Added gating of WX info to RF */
		else if (!stricmp (argv[1], "wxtorf"))
			topptr = &wxtorf;
		/* 17Sep2005, Maiko, Added gating of MIC-E frames to RF */
		else if (!stricmp (argv[1], "micetorf"))
			topptr = &micetorf;
		/* 21Dec2008, Maiko, Added gating of Objects to RF */
		else if (!stricmp (argv[1], "obj2rf"))
			topptr = &obj2rf;
		else
		{
			/* 21Jun2020, Maiko, no more hardcoded version */
			getusage ("aprs", j2str_calls);

			return 0;
		}

		curlist = argv[1];	/* 20Jun2020, Maiko, Try to make more user friendly */

	}
#ifdef	WINLINK_SECURE_LOGIN
/* 03Dec2020, Maiko, MULTIPLE_WL2K_CALLS removed from makefile, changed to WINLINK_SECURE_LOGIN */

	/*
	 * 04Jun2020, Maiko (VE4KLM), this list functioning I wrote years ago
	 * is actually quite handy for other aspects of JNOS as well, need to
	 * get this fancy pointer code moved to it's own source file. I want
	 * to add support to download mail for multiple winlink callsigns.
	 *
	else if (!stricmp (argv[0], "winlinkcalls"))
	 */
	else if (!strnicmp (argv[0], j2str_winlinkcalls, strlen (argv[0])))		/* be more forgiving */
	{
		topptr = &winlinkcalls;

		arg_offset = 1;

		curlist = j2str_winlinkcalls;	/* 20Jun2020, Maiko, Try to make more user friendly */
	}
#endif
#ifdef	J2MFA
	/*
	 * 21Nov2020, Maiko, New 'MFAexclude' list for subnets NOT to use MFA on
	 */
	else if (!stricmp (argv[0], "MFAexclude"))
	{
		topptr = &MFAexclude;

		arg_offset = 1;

		curlist = j2str_MFAexclude;	/* 20Jun2020, Maiko, Try to make more user friendly */
	}
#endif
	/*
	 * this is important, or else JNOS will crash ! This can happen
	 * if the user enters 'mbox winlinkcall' without the 's' on the
	 * end, if we don't catch that stuff, kaboom ! I suppose we can
	 * make the string comparison above more user friendly (done),
	 * since cmdparse allows for partial commands, so technically
	 * this return should never happen now (but leave it in) ...
	 */
	else return 1;

	if (argc < arg_offset + 1)
	{
		/* show it to us if already loaded into memory */
		if (*topptr)
		{
			tptr = *topptr;

			tprintf ("\n%s defined :", curlist);

			while (*tptr)
			{
				tprintf (" %s", *tptr);

				tptr++;
			}
			tprintf ("\n");
		}
		else tprintf ("\n%s not defined\n", curlist);

		if (arg_offset == 1)
		{
			/* 21Jun2020, Maiko, no more hardcoded usage */
			getusage ("", curlist);	/* 21Nov2020, Maiko, j2str_winlinkcalls); */
		}
		else
		{
			/* 21Jun2020, Maiko, no more hardcoded version */
			getusage ("aprs", curlist);
		}

		return 0;
	}

	/*
	 * If the callsign list is already allocated, then tell the user
	 * about it. I should really change this so that the user can delete
	 * the list, then recreate it again. This will do for now. It's only
	 * a prototype at this stage, still have some ideas floating around.
	 *
	 * 01Jun2001, VE4KLM, Can now reconfigure this on the fly !
	 */
	if (*topptr)
	{
		/* If callsign list is already present, delete it first */
		tptr = *topptr;

		/* first free all the j2strdups originally allocated */
		while (*tptr)
		{
			free (*tptr);
			tptr++;
		}

		/* now free the list of pointers to the j2strdups */
		free (*topptr);

		*topptr = (char**)0;
	}

	/* Build Callsign List */

	if ((*topptr = (char**)malloc (sizeof(char*) * (argc+arg_offset+1))) == (char**)0)
	{
		tprintf ("not enough memory for callsign list\n");
		return -1;
	}

	/* now load the arguments into the memory list */
	for (tptr = *topptr, cnt = 0; cnt < (argc - arg_offset); cnt++)
	{
		*tptr = j2strdup (argv[cnt+arg_offset]);

		strupr (*tptr); /* 06Jun2001, VE4KLM, force upper case */

		tptr++;
	}

	*tptr = (char*)0;	/* terminate the list with NULL */

	return 0;
}

