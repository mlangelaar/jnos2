
/*
 * APRS Services for JNOS 111x, TNOS 2.30 & TNOS 2.4x
 *
 * April,2001-June,2004 - Release (C-)1.16+
 *
 * Designed and written by VE4KLM, Maiko Langelaar
 *
 * For non-commercial use (Ham Radio) only !!!
 *
 * These functions are used to start and stop APRS servers from within
 * the NOS config.c module. 18May2004, Maiko, I decided to put this code
 * in it's own source file, after also doing some revamping (started back
 * in 03Apr2004).
 *
 */

#include "global.h"

#ifdef	APRSD

#include "aprs.h"

/* Function declarations of the NOSaprs servers */

#ifdef	APRS_14501
extern void serv14501 (int s, void *unused OPTIONAL, void *p OPTIONAL);
#endif
#ifdef	APRS_44825
extern void serv44825 (int s, void *unused OPTIONAL, void *p OPTIONAL);
#endif
extern void serv45845 (int s, void *unused OPTIONAL, void *p OPTIONAL);

#if !defined (APRS_14501) || !defined(APRS_44825)
static void dummy (void)
{
	tprintf ("not supported\n");
}
#endif

/* Arrays of information needed by NOS Start and Stop server functions */

static /*const*/ char *Sdescstr[3] = {
	"14501",
	"44825",
	"45845"
};

static void (*Sfnct[3])(int, void* OPTIONAL, void* OPTIONAL) = {
#ifdef	APRS_14501
	serv14501,
#else
	dummy,
#endif
#ifdef	APRS_44825
	serv44825,
#else
	dummy,
#endif
	serv45845
};

static int Ssrv[3] = {
	-1,
	-1,
	-1
};

/* Return the index into the above arrays for the particular service */

static int srv_offset (int service)
{
	int offset_i = -1;

	switch (service)
	{
#ifdef	APRS_14501
		case 14501: offset_i = 0; break;
#endif
#ifdef	APRS_44825
		case 44825: offset_i = 1; break;
#endif
		case 45845: offset_i = 2; break;
	}

	if (offset_i == -1)
		tprintf ("no such service, nothing done\n");

	tprintf ("returning offset %d\n", offset_i);

	return offset_i;
}

/* START a NOSaprs server */

int aprsstart (int argc, char *argv[], void *p)
{
	int ret_i, offset_i, service = atoi (argv[1]);

	tprintf ("starting %d (aprs) server\n", service);

	if ((offset_i = srv_offset (service)) != -1)
	{
#ifdef	JNOSAPRS
		Ssrv[offset_i] = 1;
		ret_i = start_tcp (service, Sdescstr[offset_i], Sfnct[offset_i], 512);
		tprintf ("start_tcp returned %d\n", ret_i);
		return ret_i;
#else
		argc = 1;	/* Use the passed port number 14501 below */

		return (installserver (argc, argv, &Ssrv[offset_i],
			Sdescstr[offset_i], service, INADDR_ANY, Sdescstr[offset_i],
				Sfnct[offset_i], 512, NULL));
#endif
	}

    return 0;	/* must have a return value - 10Mar2009, Maiko */
}

/* STOP a NOSaprs server */

int aprsstop (int argc, char *argv[], void *p)
{
	int offset_i, service = atoi (argv[1]);

	tprintf ("stopping %d (aprs) server\n", service);
 
	if ((offset_i = srv_offset (service)) != -1)
	{
#ifdef	JNOSAPRS
		Ssrv[offset_i] = -1;
		stop_tcp (service);
#else
		deleteserver (&Ssrv[offset_i]);
#endif
	}

	return 0;
}

#endif

