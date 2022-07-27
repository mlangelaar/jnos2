
/*
 * 29Aug2020, Maiko (VE4KLM), Need to deal with this limitation of
 * sequence number for the BID without wrecking forwarding to every
 * other system out there - some of us are thinking perhaps the use
 * of BASE36 instead of BASE10 should provide enough lee-way here.
 *
 * So here is a basic function I wrote from scratch :]
 *  (no dependency on ANY outside functions or packages)
 *
 * 04Sep2020, Maiko, Working properly now, was not shifting properly.
 *
 */

#undef TEST_PROGRAM

#include <stdio.h>
#include <string.h>

#include "j2base36.h"

extern char *j2strdup (const char *);

char *j2base36 (long sequence)
{
	char base36number[10];

	long part_b;

	int idx, cnt = 0;

	if (!sequence)
	{
		base36number[cnt] = '0';

		cnt++;
	}
	else while (sequence > 0)
	{
		/* shift previously processed numbers to the right */
		if (cnt)
		{
			for (idx = cnt; idx > 0; idx--)
			{
				base36number[idx] = base36number[idx-1];
			}
		}

		part_b = sequence % 36L;

		if (part_b > 9L)
			base36number[0] = part_b - 10L + 'A';
		else
			base36number[0] = part_b + '0';

		cnt++;

		sequence /= 36;
	}

	base36number[cnt] = '\0';

	return j2strdup (base36number);
}

#ifdef	TEST_PROGRAM
int main ()
{
	char *ptr;

	long seq;

	for (seq = 16796160; seq < 170000000; seq = seq + 1)
	{
		ptr = j2base36 (seq);
		printf ("%ld = %s\n", seq, ptr);
		free (ptr);
	}

	seq = 60466175L;

	ptr = j2base36 (seq);
	printf ("max out 5 characters with %ld = %s\n", seq, ptr);
	free (ptr);

	return 0;
}
#endif

