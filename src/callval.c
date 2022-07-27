
/*
 * July 10, 2007 - JNOS never had any callsign validation. Now it does !
 * with kind permission from Barry (K2MF), plus a few mods for compiler.
 *
 * Oct 19, 2010 - Added tactical callsign validation by Bob (K6FSH), so
 * renamed the original callcheck to org_callcheck, then created a new
 * callcheck to include both functions (if that makes any sense).
 */

#include "global.h"
#include "proc.h"

#ifdef MBX_CALLCHECK

#include <string.h>
#include <ctype.h>

/* Universal callsign validation function.  It is assumed that a string
 * containing only alphanumeric characters is being passed to it.
 * - 08/94 K2MF */

static int org_callcheck (char *str)
{
	int i, len;
	int digits = 0, firstdigit = 0, seconddigit = 0;
	int thirddigit = 0, fourthdigit = 0, fifthdigit = 0;

	len = strlen(str);

	if(len < 3 || len > 6)
		/* Callsign less than 3 or greater than 6 characters */
		return 0;				/* invalid */

	for(i = 0; i < len; i++) {
		if(!isalnum(str[i]))
			/* Character is not alphanumeric */
			return 0;			/* invalid */

		if(isdigit(str[i])) {
			/* Got a digit */
			if(++digits > 2)
				/* More than 2 digits in callsign */
				return 0;		/* invalid */

			switch(i) {
				case 0:
					/* 1st character */
					if(len == 3)
						/* Callsign only 3
						 * characters */
						return 0;	/* invalid */

					firstdigit++;
					break;
				case 1:
					/* 2nd character */
					if(firstdigit || len == 3)
						/* 1st character also a
						 * digit or callsign only
						 * 3 characters */
						return 0;	/* invalid */

					seconddigit++;
					break;
				case 2:
					if(seconddigit && len == 3)
						/* 2nd character also a
						 * digit and callsign only
						 * 3 characters */
						return 0;	/* invalid */

					thirddigit++;
					break;
				case 3:
					/* 4th character */
					if(firstdigit || len == 4)
						/* 1st character also a
						 * digit or callsign only
						 * 4 characters */
						return 0;	/* invalid */

					fourthdigit++;
					break;
				case 4:
					/* 5th character */
					if(firstdigit || len == 5)
						/* 1st character also a
						 * digit or callsign only
						 * 5 characters */
						return 0;	/* invalid */

					fifthdigit++;
					break;
				case 5:
					/* Digit in 6th character */
					return 0;		/* invalid */
			}
		}
	}
	if(!thirddigit && (firstdigit || !seconddigit))
		/* The 3rd character is not a digit and the 1st character
		 * is a digit or the 2nd character is not a digit */
		return 0;					/* invalid */

	/* String is a valid callsign (it passed all the tests!) */
	return 1;
}

#ifdef MBX_TAC_CALLCHECK

/* Compare the call (pointed to by callP) to the list contained in
 * the file ./TacCalls.  Return 1 if a match is found, otherwise 0.
 *
 * Format of the TacCalls file is:
 * Each line can contain-
 * 1 alphanumeric string, no more than 6 characters long and
 * beginning with the 1st character on the line.
 *
 * Optional comments are allowed on lines that begin with '#' or
 * on the same line as a call separated by
 * 1 or more SPACE, TAB or '#' characters.
 * All lines should be terminated with a '\n', '\r' or '\0'
 * - K6FSH  2010-08-05
 */

#define LINELEN 256
#define TAC_CALL_SIZE 6
static int tac_callcheck (char *callP)
{
  int lenCall, lenLine;
  char line[LINELEN];
  FILE *tf;

  lenCall = strlen(callP);

    /* Check size of tactical call */
  if(lenCall > TAC_CALL_SIZE)
    return 0;		  /* invalid */


  if ( ( tf= fopen("./TacCalls", READ_TEXT) ) == NULLFILE )
    return 0;  // Open error

  /* Read lines from the TacCalls file. */
  while(fgets(line, sizeof(line), tf) != NULLCHAR)
  {
    pwait(NULL); /* be nice */

    /* skip comment lines and blank lines */
    if( (*line == '#') || (*line == '\n') ||
        (*line == '\r') || (*line == '\0') ||
        (*line == '\t') )
      continue;

  /* Truncate all after 1st SPACE, TAB, '#', LF or CR */
    lenLine= strcspn (line, " \t#\n\r");
    line[lenLine] = '\0';
    if (lenLine > TAC_CALL_SIZE)
    {
      fclose(tf);
      return 0;
    }

    if ( strncmpi(callP, line, lenLine)== 0 )   /* Compare ignoring case */
    {  /* found it! */
      fclose(tf);
      return 1;
    }
    else
      continue;   /* not a match, try another */
  }

  /* No match was found */
  fclose(tf);
  return 0;
}

#endif	/* end of MBX_TAC_CALLCHECK */

/* Oct19, 2010, Maiko, Restructured callcheck function */
int callcheck (char *str)
{
	if (!org_callcheck (str))
	{
#ifdef MBX_TAC_CALLCHECK
	/*
	 * If the callsign check above fails, allow the connection if the
	 * name is contained in the TacCalls file - K6FSH  2010-05-28
	 */
		if (!tac_callcheck (str))
#endif
		return 0;
	}

	return 1;
}

#endif	/* end of MBX_CALLCHECK */

