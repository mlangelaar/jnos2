/*
 * 24May2007, Maiko, Created new functions to initialize the HFDD modems
 * from configuration files. These functions are literally the same for
 * all the HF modems, so might as well have just one set of functions,
 * instead of each module having it's own (saves duplicate code, etc).
 *
 * I have tried to make this as plug and play as possible. All one has
 * to do is switch the modem OFF and ON, and the hostmode driver should
 * detect if hostmode is active or not. If the driver thinks hostmode is
 * not active, then the driver will call the hfdd_init_tnc () function.
 */

#include "global.h"
#include "asy.h"
#include "unixasy.h"
#include "hfdd.h"

#define	CFGLINELEN 80

/* 10Sep2008, Maiko, This function probably should be in ifaces.c at some point */

static struct iface *if_lookup_dev (int dev)
{
    register struct iface *ifp;
  
    for (ifp = Ifaces; ifp != NULLIF; ifp = ifp->next)
        if (ifp->dev == dev)
            break;

    return ifp;
}

/* 10Sep2008, Maiko, Not static anymore, used outside, added hfdd suffix */
void hfdd_empty_tnc (int dev)
{
	unsigned char tncdata[300], *ptr = tncdata;

	struct iface *ifp;	/* 10Sep2008, NEW */

	int c, len;

	j2alarm (1000);

	while ((c = get_asy (dev)) != -1)
		*ptr++ = c & 0xff;

	j2alarm (0);

	len = (int)(ptr - tncdata);

	if ((ifp = if_lookup_dev (dev)) == NULLIF)
		log (-1, "wierd, can't find interface");

	else if (hfdd_is_kam (ifp->name))
		kam_dump (len, tncdata);

	else if (hfdd_is_pk232 (ifp->name))
		pk232_dump (len, tncdata);
}

void hfdd_init_tnc (int dev)
{
	char buf[CFGLINELEN+2];
	char kresponse[250];
	FILE *fp;
	int len;

	log (-1, "modem does not appear to be in host mode");

	hfdd_empty_tnc (dev);
	hfdd_empty_tnc (dev);
	hfdd_empty_tnc (dev);

	sprintf (kresponse, "%s.cfg", Asy[dev].iface->name);

	log (-1, "initializing ... using file - %s", kresponse);

	if ((fp = fopen (kresponse, READ_TEXT)) == NULLFILE)
	{
		log (-1, "unable to open the file");
		return;
	}

	log (-1, "standbye ... this will take a few seconds");

	while (fgets (buf, CFGLINELEN, fp) != NULLCHAR)
	{
		if (*buf == '#' )
			continue;

		len = strlen (buf);

		buf[len-1] = '\r';	/* TNC needs this to respond */

		hfdd_send (dev, buf, len);

		hfdd_empty_tnc (dev);
	}

 	fclose(fp);

	hfdd_empty_tnc (dev);
	hfdd_empty_tnc (dev);
	hfdd_empty_tnc (dev);
	hfdd_empty_tnc (dev);

	log (-1, "modem should now be in host mode");
}

