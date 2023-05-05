
/*
 * Courtesy of Brandon Mills (btmills on github), Columbus, Ohio
 *
 * This function lets me input any length string at the BBS prompt, with
 * out the need for a fixed length input buffer, it's all dynamic, exactly
 * what I need to replace (eventually) the fixed length m->line (MBXLINE),
 * right now I just want to use it to deal with unknown length CC entries.
 *
 * 14Oct2014, Maiko (VE4KLM), added this nifty function to JNOS 2.0, but
 * modified to use my JNOS input stream, not fgetc() as he uses. Also I've
 * added a check on realloc() incase it returns an error code.
 *
 */

#include <stdio.h>
#include <stdlib.h>

#define	USE4JNOS

#ifdef	USE4JNOS
extern int recvchar (int);
extern void j2alarm (int);
extern void tflush ();
extern int Mbtdiscinit;
#define log nos_log
extern void nos_log (int, char*, ...);
#endif

/*
 * Similar to fgets(), but handles automatic reallocation of the buffer.
 * Only parameter is the input stream.
 * Return value is a string. Don't forget to free it.
 */

#ifdef	USE4JNOS
char* ufgets (int stream)
#else
char* ufgets(FILE* stream)
#endif
{
    unsigned int maxlen = 128, size = 128;
    char* buffer = (char*)malloc(maxlen);

    if(buffer != NULL) /* NULL if malloc() fails */
    {
        int ch = EOF;
        int pos = 0;

        /* Read input one character at a time, resizing buffer as necessary */
#ifdef USE4JNOS
	tflush ();
	j2alarm (Mbtdiscinit*1000);   /* Start inactivity timeout - WG7J */

	while (((ch = recvchar(stream)) != EOF) && (ch != '\n'))
#else
        while((ch = fgetc(stream)) != '\n' && ch != EOF && !feof(stream))
#endif
        {
            buffer[pos++] = ch;
            if(pos == size) /* Next char to be inserted needs more memory */
            {
                size = pos + maxlen;
                buffer = (char*)realloc(buffer, size);
#ifdef USE4JNOS
				if (buffer == NULL)
				{
    				j2alarm(0);    /* disable inactivity timeout */
					log (stream, "realloc failed for unlimited input");
					return buffer;
				}
#endif
            }
        }
#ifdef USE4JNOS
		j2alarm(0);    /* disable inactivity timeout */
#endif
        buffer[pos] = '\0'; /* Null-terminate the completed string */
    }
    return buffer;
}

