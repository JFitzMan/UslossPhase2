#include <stdio.h>
#include <phase1.h>
#include <phase2.h>
#include "message.h"
#include <usloss.h>

extern int debugflag2;
int timesCalled = 0;

/* an error method to handle invalid syscalls */
void nullsys(sysargs *args)
{
    USLOSS_Console("nullsys(): Invalid syscall %d. Halting...\n", args->number);
    USLOSS_Halt(1);
} /* nullsys */


void clockHandler2(int dev, int unit)
{
	timesCalled++;

   if (DEBUG2 && debugflag2)
      USLOSS_Console("clockHandler2(): called\n");
	
	if(timesCalled == 5){ //Supposed to only send at 100ms, or every 5 interrupts.
		MboxCondSend(clockMboxID, "a", 50);
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
  //int status;
  USLOSS_DeviceInput(USLOSS_TERM_DEV, unit, &lastStatusRead);
  char buffer[1];
  buffer[0] = (char)USLOSS_TERM_STAT_CHAR(lastStatusRead);
  //USLOSS_DeviceInput(USLOSS_TERM_DEV, unit, &status);
  //printf("%c, %d, %d\n", (char)USLOSS_TERM_STAT_CHAR(lastStatusRead), USLOSS_TERM_STAT_XMIT(status),USLOSS_TERM_STAT_RECV(status));
  MboxCondSend(termMboxID[unit], buffer, 1);  


} /* termHandler */


void syscallHandler(int dev, struct sysargs *arg)
{
  if (DEBUG2 && debugflag2)
    USLOSS_Console("syscallHandler(): called\n");
  if (arg->number >= MAXSYSCALLS || arg->number < 0){
      USLOSS_Console("syscallHandler(): sys number %d is wrong.  Halting...\n", arg->number);
      USLOSS_Halt(1);
  }
  else{
    sys_vec[arg->number](arg);
  }
  


} /* syscallHandler */
