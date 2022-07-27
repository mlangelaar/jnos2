/*
 * New module for February 2, 2008 - Maiko Langelaar / VE4KLM
 * experimental idea so that shell scripts can be executed for
 * events like BBS login, logout, CONVERS connect, msg arrival,
 * and disconnect.
 */

#include "global.h"

#ifdef SYSEVENT

#include "sysevent.h"

static char *syseventcmds[] = {
	"convconn",
	"convdisc",
	"convmsg",
	"bbsconn",
	"bbsdisc"
};

void j2sysevent (J2SYSEVENT sysevent, char *callsign)
{
	char syscmd[100];

	sprintf (syscmd, "sh /jnos/sys/%s %s &\n",
		syseventcmds[sysevent], callsign);

	system (syscmd);
}

#endif

