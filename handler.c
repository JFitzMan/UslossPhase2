#include <stdio.h>
#include <phase1.h>
#include <phase2.h>
#include "message.h"

extern int debugflag2;
int timesCalled = 0;

/* an error method to handle invalid syscalls */
void nullsys(sysargs *args)
{
    USLOSS_Console("nullsys(): Invalid syscall. Halting...\n");
    USLOSS_Halt(1);
} /* nullsys */


void clockHandler2(int dev, int unit)
{
	timesCalled++;

   if (DEBUG2 && debugflag2)
      USLOSS_Console("clockHandler2(): called\n");
	
	if(timesCalled == 5){ //Supposed to only send at 100ms, or every 5 interrupts.
		MboxCondSend(0, "a", 50);
		timesCalled = 0;
	}
		  

} /* clockHandler */


void diskHandler(int dev, int unit)
{

   if (DEBUG2 && debugflag2)
      USLOSS_Console("diskHandler(): called\n");


} /* diskHandler */


void termHandler(int dev, int unit)
{

   if (DEBUG2 && debugflag2)
      USLOSS_Console("termHandler(): called\n");


} /* termHandler */


void syscallHandler(int dev, int unit)
{

   if (DEBUG2 && debugflag2)
      USLOSS_Console("syscallHandler(): called\n");


} /* syscallHandler */
