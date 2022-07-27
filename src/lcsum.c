/*
 * Word aligned linear buffer checksum routine.  Called from mbuf checksum
 * routine with simple args.  Intent is that this routine may be replaced
 * by assembly language routine for speed if so desired. (On the PC, the
 * replacement is in pcgen.asm.)
 *
 * Copyright 1991 Phil Karn, KA9Q
 */
  
#include "global.h"
#include "ip.h"

int16
lcsum(wp,len)
register int16 *wp;
register int16 len;
{
    register int32 sum = 0;
    int16 result;
  
    while(len-- != 0)
        sum += *wp++;
    result = eac(sum);
#if __BYTE_ORDER ==  __LITTLE_ENDIAN
    /* Swap the result because of the (char *) to (int *) type punning */
    result = (result << 8) | (result >> 8);
#endif
    return result;
}
  
