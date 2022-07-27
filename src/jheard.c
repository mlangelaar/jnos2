
/*
 * More webbased statistics and forms - experimental of course
 *
 * These functions build the HTML code to display the stations heard on a
 * particular interface, and gives the user a chance to select a different
 * interface (port) and see the stations heard on that one as well, and so
 * on and so on. Also included are forms to show 'mbox past' and * current
 * users connected to the NOS system. This will evolve over time. What I
 * eventually want to do is make a complete web based interface for JNOS,
 * perhaps going as far as removing the CURSES library dependencies at
 * some point in time (who knows).
 *
 * Wrote this stuff from scratch, June 2005 - Maiko Langelaar, VE4KLM
 *
 * This module takes us to JNOS 2.0c5a ...
 *
 */

#include "global.h"

#ifdef	APRSD

#include "pktdrvr.h"
#include "mailbox.h"
#include "timer.h"
#include "iface.h"
#include "ax25.h"

/*
 * Interfaces and Heard html strings
 */

static char *jh_str1 = "<center><table bgcolor=\"#00ffee\" border=\"1\" cellpadding=\"2\"><tr><td><table bgcolor=\"#aaffee\" cellpadding=\"2\"><tr><td>Stations</td><td><select>";

static char *jh_str2 = "</select><td><td>heard on port</td><td><select onchange=\"location = this.options[this.selectedIndex].value;\">";

static char *jh_str3 = "</select></td><td>";

static char *jh_str4 = "</td></tr></table></td></tr></table>";

/*
 * Users and Past Users html strings
 */

static char *usr_str1 = "<center><table bgcolor=\"#00ffee\" border=\"1\" cellpadding=\"5\"><tbody><tr><td><table bgcolor=\"#aaffee\" cellpadding=\"5\"><tbody><tr><td>Users</td><td><select onchange=\"location = this.options[this.selectedIndex].value;\">";

static char *usr_str2 = "</select></td><td>Past Users</td><td><select onchange=\"location = this.options[this.selectedIndex].value;\">";

static char *usr_str3 = "</select></td></tr></tbody></table></td></tr><tr><td align=\"center\">";

static char *usr_str4 = "</td></tr></tbody></table></center>";

/* ___ NOTE TO MYSELF - PUT THIS INTO A HEADER FILE HERE AND THERE ___ */

/*
 * Huh ? A kludge, this should be in a header file
 */

struct pu {
    struct pu *next;    /* next one in list */
    int32 time;                 /* When was the last login ? */
    int number;                 /* Number of times logged in */
    char name[MBXNAME+1];       /* user name */
};

extern struct pu *Pu;

#define NULLPU (struct pu *)NULL

/* ___ END OF NOTE TO MYSELF ___ */

static char *glob_str_none = "none";

/*
 * Function to show a user friendly date and time string
 */

static char *date_time_str (int32 timepast)
{
	long days, hours, mins, secs; 
	static char tmptime[50];
	char *fptr, *tmpptr;

	/* 14Aug2010, Maiko, Should really be using a format string here */
	sprintf (tmptime, "%s", tformat (secclock() - timepast));

	tmpptr = tmptime;

	// log (-1, "date_time_str (%d) [%s]", strlen (tmptime), tmptime);

	days = strtol (tmpptr, &tmpptr, 0);
	hours = strtol (tmpptr+1, &tmpptr, 0);
	mins = strtol (tmpptr+1, &tmpptr, 0);
	secs = strtol (tmpptr+1, &tmpptr, 0);

	fptr = tmptime;

	/* log (-1, "days %ld hours %ld mins %ld secs %ld",
		days, hours, mins, secs);
	 */

	*fptr = 0;	/* always start with a null string */

	if (days)
		fptr += sprintf (fptr, " %ld day", days);
	if (hours)
		fptr += sprintf (fptr, " %ld hour", hours);
	if (mins)
		fptr += sprintf (fptr, " %ld min", mins);
	if (secs)
		fptr += sprintf (fptr, " %ld sec", secs);

	return tmptime;
}

/*
 * Functions to build the Users and Past Users form
 */

static char *usr_current_option (char *ptr, struct mbx *mbox)
{
	char *nameptr;

	if (mbox == (struct mbx*)0)
		nameptr = glob_str_none;
	else
		nameptr = mbox->name;

	ptr += sprintf (ptr, "<option>%s</option>", nameptr);

	return ptr;
}

static char *usr_past_option (char *ptr, struct pu *pu)
{
	char *nameptr;

	if (pu == (struct pu*)0)
		nameptr = glob_str_none;
	else
		nameptr = pu->name;

	ptr += sprintf (ptr, "<option>%s</option>", nameptr);

	return ptr;
}

static int usr_sizeof_table (void)
{
	struct mbx *mbox;

	struct pu *pu;

	/* size of each option */
	int optsize = MBXNAME + 20;

	/* base size without any information */
	int size = 301;

	/* reserve space for current users */
	for (mbox = Mbox; mbox; mbox = mbox->next)
		size += optsize;

	/* reserve space for past users */
	for (pu = Pu; pu != NULLPU; pu = pu->next)
		size += optsize;

	/* reserve space for none entries */
	size += optsize;
	size += optsize;

	/* reserve space for an iface description */
	size += 80;

	// log (-1, "usr form %d bytes", size);

	return size;	/* that should do it */
}

static int usr_build_table (char *ptr)
{
	char *orig_ptr = ptr;
	struct mbx *mbox;
	struct pu *pu;

	ptr += sprintf (ptr, "%s", usr_str1);

	if (!Mbox)
		ptr = usr_current_option (ptr, (struct mbx*)0);

	else
	{
		for (mbox = Mbox; mbox; mbox = mbox->next)
			ptr = usr_current_option (ptr, mbox);
	}

	ptr += sprintf (ptr, "%s", usr_str2);

	if (!Pu)
		ptr = usr_past_option (ptr, (struct pu*)0);

	else
	{	
		for (pu = Pu; pu != NULLPU; pu = pu->next)
			ptr = usr_past_option (ptr, pu);
	}

	ptr += sprintf (ptr, "%s", usr_str3);

	ptr += sprintf (ptr, "Uptime is %s", date_time_str (0));

	ptr += sprintf (ptr, "%s", usr_str4);

	return (ptr - orig_ptr);
}

/*
 * Functions to build the Interfaces and Heard form
 */

static char *jh_interface_option (char *ptr, char *ifacename, int selected)
{
	ptr += sprintf (ptr, "<option ");

	if (selected)
		ptr += sprintf (ptr, "selected ");

	ptr += sprintf (ptr, "value=\"ifc?%s\">%s</option>",
		ifacename, ifacename);

	return ptr;
}

static char *jh_stations_option (char *ptr, struct lq *lp)
{
	char tmp[AXBUF], *nameptr;

	if (lp == (struct lq*)0)
		nameptr = glob_str_none;
	else
		nameptr = pax25 (tmp, lp->addr);

	ptr += sprintf (ptr, "<option>%s</option>", nameptr);

	return ptr;
}

static struct iface *cur_iface = (struct iface*)0;

static int jh_sizeof_table (void)
{
	struct iface *ifp;
	char tmp[AXBUF];
	struct lq *lp;

	int size = 600;	/* by observation, initial size of blank form */

	for (ifp = Ifaces; ifp != NULLIF; ifp = ifp->next)
	{
		if(ifp->type != CL_AX25 || !(ifp->flags & LOG_AXHEARD))
			continue;

		size += 40;	/* for the HTML */

		size += (2 * strlen (ifp->name));	/* twice for iface name */
	}

	for (lp = Lq; lp != NULLLQ; lp = lp->next)
	{
		if (lp->iface != cur_iface)
			continue;

		size += 35;	/* for the HTML */

		size += strlen (pax25 (tmp, lp->addr)); /* for callsign */
	}

	// log (-1, "jh form %d bytes", size);

	return size;
}

static int jh_build_table (char *ptr, char *ifacename)
{
	char *orig_ptr = ptr;
	struct iface *ifp;
	struct lq *lp;
	int heard = 0;

	if (*ifacename)
		cur_iface = if_lookup (ifacename);

	if (cur_iface == (struct iface*)0)
		cur_iface = Ifaces;

	ptr += sprintf (ptr, "%s", jh_str1);

	for (lp = Lq; lp != NULLLQ; lp = lp->next)
	{
		if (lp->iface != cur_iface)
			continue;

		ptr = jh_stations_option (ptr, lp);

		heard = 1;
	}

	if (!heard)
		ptr = jh_stations_option (ptr, (struct lq*)0);

	ptr += sprintf (ptr, "%s", jh_str2);

	for (ifp = Ifaces; ifp != NULLIF; ifp = ifp->next)
	{
		if(ifp->type != CL_AX25 || !(ifp->flags & LOG_AXHEARD))
			continue;

		ptr = jh_interface_option (ptr, ifp->name, (ifp == cur_iface));
	}

	ptr += sprintf (ptr, "%s", jh_str3);

	ptr += sprintf (ptr, "%s", cur_iface->descr);

	ptr += sprintf (ptr, "%s", jh_str4);

	return (ptr - orig_ptr);
}

/*
 * The following 3 functions are the only ones that need to be called
 * from outside this module - ie, called from the 'aprssrv.c' module.
 */

int http_obj_sizeof_forms (void)
{
	int size = 0;

	size += jh_sizeof_table ();

	size += usr_sizeof_table ();

	return size;
}

int http_obj_jh_form (char *ptr, char *ifacename)
{
	return jh_build_table (ptr, ifacename);
}

int http_obj_usr_form (char *ptr)
{
	return usr_build_table (ptr);
}

#endif	/* end of APRSD */

