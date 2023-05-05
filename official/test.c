/* test XMS ... compile by bcc -ml test.c xms.obj xmsutil.obj  -- n5knx */
#include <stdio.h>
#include "xms.h"
  
void main(void) {
    struct XMS_Move X;
    long handle, foo;
    int n,i;
    unsigned char c;
  
    if(Installed_XMS()) {
        puts("XMS detected");
	printf("Query_XMS = %ld K, Total_XMS = %ld\n", Query_XMS(), Total_XMS());
        n=2;
        if(((handle = Alloc_XMS(n))&0xFF000000L) == 0L) {
            printf("Allocated %d KB using handle %ld\n", n, handle);
            foo = Query_XMS_handle((unsigned int)handle&0xFFFF);
            printf("Query_Handle: blk is %u KB, %u locks, and %u handles are free\n",
                (unsigned int)(foo>>16), (unsigned int)(foo>>8)&0xFF, (unsigned int)foo&0xFF);
	    fflush(stdout);
            /* Now transfer to XMS */
            X.Length = n * XMS_PAGE_SIZE;
            X.SourceHandle = 0; /* Indicate conventional memory */
            X.SourceOffset = 0xB8000000L;/* B800:0000, video ram */
            X.DestHandle = (int)(handle&0xFFFF);
            X.DestOffset = 0L;
            if(Move_XMS(&X))
                puts("Cannot move to XMS!");
            else {
                puts("Data moved to XMS");
		if (Free_XMS((int) handle&0xFFFF))
		    printf("Can't free XMS block\n");
            }

	    printf("largestUMB() reports %u paragraphs available\n", largestUMB());
	    n=32767;	  /* get impossibly-large # paragraphs */
            foo = Request_UMB(n);
	    if ((int)(foo>>16) == n) {  /* got as many paras as we requested? */
                printf("Request_UMB: at %4.4x, for %u paragraphs (wanted %d)\n",
 		    (int)(foo&0xFFFF), (unsigned int)(foo>>16), n);
                foo = Release_UMB((int)(foo&0xFFFF));
		if (foo) printf("release_UMB failed, code %2.2X\n", (int)(foo>>24)&0xFF);
	    }
	    else {
		c = (unsigned char)(foo>>24);
		printf ("Request_UMB for %d paras failed (code %2.2X)",	n, c);
		if (XMSErrSmUMB == c)
			printf ("; but %d paras are available\n", (int)(foo&0xFFFF));
		else printf ("\n");
	    }

	    n=10;	  /* get 10 paragraphs */
            foo = Request_UMB(n);
	    if ((int)(foo>>16) == n) {  /* got as many paras as we requested? */
                printf("Request_UMB: at %4.4x, for %u paragraphs (wanted %d)\n",
 		    (int)(foo&0xFFFF), (unsigned int)(foo>>16), n);
                foo = Release_UMB((int)(foo&0xFFFF));
		if (foo) printf("release_UMB failed, code %2.2X\n", (int)(foo>>24)&0xFF);
	    }
	    else {
		c = (unsigned char)(foo>>24);
		printf ("Request_UMB for %d paras failed (code %2.2X)",	n, c);
		if (XMSErrSmUMB == c)
			printf ("; but %d paras are available\n", (int)(foo&0xFFFF));
		else printf ("\n");
	    }

            if (Request_HMA() != 1) printf("Request_HMA unsuccessful\n");
            else if (Release_HMA() != 1) printf("Release_HMA failed\n");
        } else
            printf("Cannot get XMS, error %2.2X (int32 is %8.8lX)\n", (int)(handle>>24)&0xFF, handle);
    } else
        puts("No XMS");
}
  
