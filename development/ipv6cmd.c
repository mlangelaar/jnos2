/*
 * Adding IPV6, beginning Feb, 2023, by Maiko Langelaar, VE4KLM
 *
 * 09Feb2023, Maiko (VE4KLM), so ICMP V6 ECHO REPLY is finally working after
 * one week of starting IPV6 support. We will need to have a few basic IPV6
 * commands available to the sysop, so they can configure things properly.
 */

#include "global.h"
#include "cmdparse.h"
#include "iface.h"
#include "ipv6.h"	/* 23Feb2023, Maiko */

char *ipv6iface = (char*)0;	/* 03Mar2023, Maiko, not static anymore */

static int doipv6iface (int argc, char **argv, void *p)
{
	if (argc < 2)
	{
		if (ipv6iface)
			tprintf ("%s\n", ipv6iface);
		else
			tprintf ("not defined\n");
	}
	else
	{
		if (ipv6iface)
			free (ipv6iface);

		/*
		 * 19Apr2023, Maiko (VE4KLM), We need to enforce the use of a TAP
		 * interface, since JNOS will crash and burn if you try to send TAP
		 * specific data streams to a TUN interface ...
		 */

		if (strstr (argv[1], "tap"))
			ipv6iface = j2strdup (argv[1]);
		else
			tprintf ("warning - the ipv6 iface must contain the word 'tap'.\n (the ipv6 iface will remain undefined)\n");
	}

	return 0;
}

/*
 * 09Feb2023, Maiko (VE4KLM), moved out of ipv6iface.c, modified
 * to have no argument, now we just use the ipv6iface global.
 */
struct iface *myipv6iface (void)
{
	struct iface *ifp = NULLIF;

	if (ipv6iface)	/* only go in here if global iface is set ! */
	{
		for (ifp = Ifaces; ifp != NULLIF; ifp = ifp->next)
			if (!strcmp (ipv6iface, ifp->name))
				break;
	}

	return ifp;
}

/* 23Feb2023, Maiko, Changed from int16 to uchar */
extern unsigned char myipv6addr[16];	/* 09Feb2023, Maiko, Leave in ipv6iface.c */

/*
 * 11Feb2023, Maiko (VE4KLM), SO, Ubuntu will crash if this prototype is
 * missing, that is one thing I've learned from Ubuntu GCC et all, unlike
 * other OS, if you do NOT prototype the function, then %s below will get
 * a default integer type pointing to who knows where and boom !!! Other
 * systems like RHEL and Slackware are more forgiving, but no excuse :]
 *
 * 23Feb2023, Maiko, Now in ipv6.h where it belongs !
     extern char *human_readable_ipv6 (unsigned char *addr);
 */

static int doipv6addr (int argc, char **argv, void *p)
{
	char segment[3], *ptr;

	long segment_l;

	unsigned int segment_i, cnt;

	if (argc < 2)
		tprintf ("%s\n", human_readable_ipv6 (myipv6addr));
	else
	{
		ptr = argv[1];

		for (cnt = 0; cnt < 16; cnt++)
		{
			if (!(*ptr)) break;	/* out of input data */

			strncpy (segment, ptr, 2);

			segment_l = strtol (segment, NULL, 16);

			// tprintf ("segment [%s] %ld\n", segment, segment_l);

			segment_i = (unsigned int)segment_l;

			myipv6addr[cnt] = (unsigned char)segment_i;

			ptr += 2;	/* advance to next segment */

			if (*ptr == ':')	/* skip the delimiter */
				ptr++;
		}
		if (cnt != 16)
			tprintf ("looks like you made a mistake, try again\n");
/*
		sscanf (argv[1], "%2c%2c:%2c%2c:%2c%2c:%2c%2c:%2c%2c:%2c%2c:%2c%2c:%2c%2c",
			myipv6addr[0], myipv6addr[1], myipv6addr[2], myipv6addr[3],
			myipv6addr[4], myipv6addr[5], myipv6addr[6], myipv6addr[7],
			myipv6addr[8], myipv6addr[9], myipv6addr[10], myipv6addr[11],
			myipv6addr[12], myipv6addr[13], myipv6addr[14], myipv6addr[15]);
*/
	}

	/*
	 * 09Feb2023, Maiko, okay this is quick and dirty (above) but works for now
	 *
		jnos> ipv6 addr fd00:0004:0000:0000:0000:0000:3200:0002
		jnos> ipv6 addr 
		fd00:0004:0000:0000:0000:0000:3200:0002
	 *
	 */

	return 0;
}

#ifdef	SERVES_NO_PURPOSE_RIGHT_NOW
/*
 * 22Mar2023, Maiko, New Router Solicitation command (icmpv6.c)
 * 25Mar2025, Maiko, Don't need this, never did, part of experimenting
 */
extern int icmpv6_router_solicitation (int argc, char **argv, void*p);
#endif

static struct cmds DFAR Ipv6cmds[] = {
    { "iface",		doipv6iface,	0,	0,	NULLCHAR },
    { "addr",		doipv6addr,	0,	0,	NULLCHAR },
#ifdef	SERVES_NO_PURPOSE_RIGHT_NOW
	/* 22Mar2023, Maiko, New Router Solicitation command */
	{ "rs",			icmpv6_router_solicitation,	0,	0,	NULLCHAR },
#endif
    { NULLCHAR,		NULL,		0,	0,	NULLCHAR }
};

int doipv6 (int argc, char **argv, void *p)
{
    return subcmd (Ipv6cmds, argc, argv, p);
}

