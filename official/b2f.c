
#include "global.h"

#ifdef B2F

/*
 * 23Apr2008, Maiko (VE4KLM), New source file to keep the B2F related
 * functions in their own module.
 */

#define	MAXNOB2F 20

char *noB2Fcalls[MAXNOB2F] = { NULL, NULL, NULL, NULL, NULL };

/*
 * Some systems may not be able to handle the 2 portion of the SID. I have
 * found that FBB 7.00i does not like it when I have the '2' in my opening
 * prompt (SID) when FBB connects to my system. Instead of trying to use a
 * kludge in the code to try and acommodate different software out there,
 * perhaps it would be better to just have a list of callsigns for which
 * we simply do NOT indicate that we support B2F for when they connect.
 *
 * 24Apr2008, Maiko (VE4KLM), works okay now !
 */
int j2noB2F (char *callsign)
{
	char *ptr;

	int cnt;

	if (callsign == NULL)
		tprintf ("no B2F for these callsigns : ");

	for (cnt = 0; cnt < MAXNOB2F; cnt++)
	{
		ptr = noB2Fcalls[cnt];

		if (ptr == NULL)
			break;
		
		if (callsign == NULL)
			tprintf ("%s ", ptr);

		else if (!stricmp (callsign, ptr))
			return 1;
	}

	if (callsign == NULL)
		tprintf ("\n");

	return 0;
}

void j2AddnoB2F (char *callsign)
{
	int cnt;

	for (cnt = 0; cnt < MAXNOB2F; cnt++)
	{
		if (noB2Fcalls[cnt] == NULL)
		{
			noB2Fcalls[cnt] = j2strdup (callsign);
			break;
		}
	}
}

#endif
