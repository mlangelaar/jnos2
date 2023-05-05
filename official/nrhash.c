
#include "ax25.h"
#include "netrom.h"


int
setcall(out,call)
char *out;
char *call;
{
    int csize;
    unsigned ssid;
    register int i;
    register char *dp;
    char c;
  
    if(out == NULLCHAR || call == NULLCHAR || *call == '\0')
        return -1;
  
    /* Find dash, if any, separating callsign from ssid
     * Then compute length of callsign field and make sure
     * it isn't excessive
     */
    dp = strchr(call,'-');
    if(dp == NULLCHAR)
        csize = strlen(call);
    else
        csize = (int)(dp - call);
    if(csize > ALEN)
        return -1;
    /* Now find and convert ssid, if any */
    if(dp != NULLCHAR){
        dp++;   /* skip dash */
        ssid = atoi(dp);
        if(ssid > 15)
            return -1;
    } else
        ssid = 0;
    /* Copy upper-case callsign, left shifted one bit */
    for(i=0;i<csize;i++){
        c = *call++;
        if(islower(c))
            c = toupper(c);
        *out++ = c << 1;
    }
    /* Pad with shifted spaces if necessary */
    for(;i<ALEN;i++)
        *out++ = ' ' << 1;
  
    /* Insert substation ID field and set reserved bits */
    *out = 0x60 | (ssid << 1);
    return 0;
}

/* hash function for callsigns.  Look familiar? */
int16
nrhash(s)
char *s;
{
    register char x;
    register int i;
  
    x = 0;
    for(i = ALEN; i !=0; i--)
        x ^= *s++ & 0xfe;
    x ^= *s & SSID;
    return (int16)(uchar(x) % NRNUMCHAINS);
}

static char data_pulled[AXALEN] = { 0x9e, 0x9c, 0x60, 0x82, 0x9c, 0xa4, 0x60 };

main (int argc, char **argv)
{
	char thecall[AXALEN];

	int cnt;

	setcall (thecall, "ON0ANR");

	printf ("ours  theirs\n");

	for (cnt = 0; cnt < AXALEN; cnt++)
	{
		printf ("[%02x] [%02x]\n",
			(int)thecall[cnt], (int)data_pulled[cnt]);
	}

	// printf ("nrhash returned %d\n", (int)nrhash (thecall));
}

