/* ------------------------------------------------------------------------
   phase2.c

   University of Arizona
   Computer Science 452

   ------------------------------------------------------------------------ */

#include <phase1.h>
#include <phase2.h>
#include <usloss.h>

#include "message.h"

/* ------------------------- Prototypes ----------------------------------- */
int start1 (char *);
extern int start2 (char *);
void addProcToBlockedList(mboxProcPtr toAdd, int mbox_id);
int inKernelMode(char *procName);
void disableInterrupts();
void enableInterrupts();
slotPtr getEmptySlot(int size, slotPtr boxSlotList, int mbox_id);
void addSlot(slotPtr front, slotPtr toAdd);

/* -------------------------- Globals ------------------------------------- */

int debugflag2 = 0;

// the mail boxes 
mailbox MailBoxTable[MAXMBOX];

// the process table
struct mboxProc processTable[MAXPROC];

//the mail slots
struct mailSlot MailSlotTable[MAXSLOTS];

int nextMailBoxID = 7;
int totalSlotsInUse = 0;

// also need array of function ptrs to system call 
// handlers, ...




/* -------------------------- Functions ----------------------------------- */

/* ------------------------------------------------------------------------
   Name - start1
   Purpose - Initializes mailboxes and interrupt vector.
             Start the phase2 test process.
   Parameters - one, default arg passed by fork1, not used here.
   Returns - one to indicate normal quit.
   Side Effects - lots since it initializes the phase2 data structures.
   ----------------------------------------------------------------------- */
int start1(char *arg)
{
    if (DEBUG2 && debugflag2)
        USLOSS_Console("start1(): at beginning\n");

    // check kernel mode
    if (!inKernelMode("start1"))
      USLOSS_Console("Kernel Error: Not in kernel mode, may not run\n");
    
    // Disable interrupts
    disableInterrupts();



    //initialize mailbox table
    int i;
    for (i = 0; i < MAXMBOX; i++){
      MailBoxTable[i].mboxID = EMPTY;
      MailBoxTable[i].nextBlockedProc = NULL;
      MailBoxTable[i].firstSlot = NULL;
    }

    //initialize process table
    for (i = 0; i < MAXPROC; i++){
      processTable[i].pid = EMPTY;
    }
    processTable[getpid()].pid = getpid();
    processTable[getpid()].status = READY;

    
    // initialize mail slots
    for (i = 0; i < MAXSLOTS; i++){
      MailSlotTable[i].mboxID = EMPTY; 
    }
    

    // Initialize USLOSS_IntVec and system call handlers,
    // allocate mailboxes for interrupt handlers.  Etc... 

    enableInterrupts();
    
    // Create a process for start2, then block on a join until start2 quits
    if (DEBUG2 && debugflag2)
        USLOSS_Console("start1(): fork'ing start2 process\n");
    int kid_pid = fork1("start2", start2, 0, 4 * USLOSS_MIN_STACK, 1);
    //add process to the process table
    processTable[kid_pid].pid = kid_pid;
    processTable[kid_pid].status = READY;
    int status;
    if ( join(&status) != kid_pid ) {
        USLOSS_Console("start2(): join returned something other than ");
        USLOSS_Console("start2's pid\n");
    }
    
    return 0;
} /* start1 */


/* ------------------------------------------------------------------------
   Name - MboxCreate
   Purpose - gets a free mailbox from the table of mailboxes and initializes it 
   Parameters - maximum number of slots in the mailbox and the max size of a msg
                sent to the mailbox.
   Returns - -1 to indicate that no mailbox was created, or a value >= 0 as the
             mailbox id.
   Side Effects - initializes one element of the mail box array. 
   ----------------------------------------------------------------------- */
int MboxCreate(int slots, int slot_size)
{
  disableInterrupts();
  //check to make sure slots or slot_size doesnt exceed constants
  if (slots > MAXSLOTS || slot_size > MAX_MESSAGE){
    if (DEBUG2 && debugflag2)
        USLOSS_Console("MboxCreate(): Unable to create mmailbox, parameters too large!\n");
    return -1;
  }
  //find the next avaliable mailbox
  //right now it starts at 7, but I do not know why exactly
  //so this loops around the box starting at 7. Check out test01
  //what's weird is this still works for test02. Maybe a bug on patricks end
  int i;
  int newMailBoxID = -1;
  for( i=nextMailBoxID; i < MAXMBOX; i++){
    if (MailBoxTable[i].mboxID == EMPTY){
      newMailBoxID = i;
      nextMailBoxID++;
      break;
    }
  }

  //if newMailBoxID=-1, return -1 to indicate that all mailboxes are in use
  if (newMailBoxID == -1){
    if (DEBUG2 && debugflag2)
        USLOSS_Console("MboxCreate(): All mailboxes in use!\n");
    enableInterrupts();
    return -1;
  }

  //initialize new mailbox
  MailBoxTable[newMailBoxID].mboxID = newMailBoxID;
  MailBoxTable[newMailBoxID].numSlots = slots;
  MailBoxTable[newMailBoxID].slotSize = slot_size;
  MailBoxTable[newMailBoxID].slotsInUse = 0;
  MailBoxTable[newMailBoxID].firstSlot = NULL;
  if (DEBUG2 && debugflag2)
        USLOSS_Console("MboxCreate(): initializing new mailbox\n");

  //return mailBoxID
  enableInterrupts();
  return newMailBoxID;



} /* MboxCreate */


/* ------------------------------------------------------------------------
   Name - MboxSend
   Purpose - Put a message into a slot for the indicated mailbox.
             Block the sending process if no slot available.
   Parameters - mailbox id, pointer to data of msg, # of bytes in msg.
   Returns - zero if successful, -1 if invalid args.
   Side Effects - none.
   ----------------------------------------------------------------------- */
int MboxSend(int mbox_id, void *msg_ptr, int msg_size)
{
  disableInterrupts();

  //check that arguments are valid
  if (msg_size > MAX_MESSAGE){
    if (DEBUG2 && debugflag2)
        USLOSS_Console("MboxSend(): msg is too large!\n");
    enableInterrupts();
    return -1;
  }else if (MailBoxTable[mbox_id].mboxID == EMPTY){
    if (DEBUG2 && debugflag2)
        USLOSS_Console("MboxSend(): mbox ID does not exist!!\n");
    enableInterrupts();
    return -1;
  }

  //check to make sure system has enough slots left
  if (totalSlotsInUse >= MAXSLOTS){
    USLOSS_Console("No slots left in system! Halting...\n");
    USLOSS_Halt(1);
  }

  //No slots left in Mailbox
  if (MailBoxTable[mbox_id].slotsInUse == MailBoxTable[mbox_id].numSlots){
    processTable[getpid()].status = SEND_BLOCKED;
    addProcToBlockedList(&processTable[getpid()], mbox_id);
    blockMe(12);//something higher than 10, patrick will post which numbers display what soon
  }
  /*free slot in mailbox:
  1)Allocate slot 2)copy message 3)profit
  */
  else{
    slotPtr newSlot = getEmptySlot(msg_size, MailBoxTable[mbox_id].firstSlot, mbox_id);
  }

  //TURN THOSE INTERRUPTS BACK ON BEFORE LEAVING

} /* MboxSend */


/* ------------------------------------------------------------------------
   Name - MboxReceive
   Purpose - Get a msg from a slot of the indicated mailbox.
             Block the receiving process if no msg available.
   Parameters - mailbox id, pointer to put data of msg, max # of bytes that
                can be received.
   Returns - actual size of msg if successful, -1 if invalid args.
   Side Effects - none.
   ----------------------------------------------------------------------- */
int MboxReceive(int mbox_id, void *msg_ptr, int msg_size)
{
} /* MboxReceive */


/*
 *checks the PSR for kernel mode
 *returns true in if its in kernel mode, and false if not
*/
int inKernelMode(char *procName){
    if( (USLOSS_PSR_CURRENT_MODE & USLOSS_PsrGet()) == 0 ) {
      USLOSS_Console("Kernel Error: Not in kernel mode, may not run %s()\n", procName);
      USLOSS_Halt(1);
      return 0;
    }
    else{
      return 1;
    }
}

/*
 * Disables the interrupts.
 */
void disableInterrupts()
{
    /* turn the interrupts OFF iff we are in kernel mode */
    if( (USLOSS_PSR_CURRENT_MODE & USLOSS_PsrGet()) == 0 ) {
        //not in kernel mode
        USLOSS_Console("Kernel Error: Not in kernel mode, may not ");
        USLOSS_Console("disable interrupts\n");
        USLOSS_Halt(1);
    } else
        /* We ARE in kernel mode */
        USLOSS_PsrSet( USLOSS_PsrGet() & ~USLOSS_PSR_CURRENT_INT );
} /* disableInterrupts */

/*
 * Enables the interrupts.
 */
void enableInterrupts()
{
    /* turn the interrupts ON iff we are in kernel mode */
    if( (USLOSS_PSR_CURRENT_MODE & USLOSS_PsrGet()) == 0 ) {
        //not in kernel mode
        USLOSS_Console("Kernel Error: Not in kernel mode, may not ");
        USLOSS_Console("enable interrupts\n");
        USLOSS_Halt(1);
    } else
        /* We ARE in kernel mode */
        USLOSS_PsrSet( USLOSS_PsrGet() | USLOSS_PSR_CURRENT_INT );
} /* enableInterrupts */

void addProcToBlockedList(mboxProcPtr toAdd, int mbox_id){
  //no other procs on list, easy add
  if (MailBoxTable[mbox_id].nextBlockedProc == NULL){
    MailBoxTable[mbox_id].nextBlockedProc = toAdd;
  }
  //it must be added to the end of the blocked list
  else{

    mboxProcPtr prev = NULL;
    mboxProcPtr cur;
    //get a pointer to the last proc
    for (cur = MailBoxTable[mbox_id].nextBlockedProc; 
      cur != NULL; cur = cur->nextProc){
      prev = cur;
    }

    prev->nextProc = toAdd;

  }//end else

}// addProcToBlockedList

/* ------------------------------------------------------------------------
   Name - getEmptySlot
   Purpose - returns a pointer to an empty mail slot
   Parameters - None
   Returns - Pointer to mail slot, NULL if all slots are full.
   Side Effects - increments totalSlotsInUse
                  updates the mailslot fields of the slot it's returning
   ----------------------------------------------------------------------- */
slotPtr getEmptySlot(int size, slotPtr boxSlotList, int mbox_id){
  slotPtr newSlot = NULL;
  int i;
  for (i=0; i > MAXSLOTS; i++){
    if (MailSlotTable[i].mboxID == -1){
      //initialize new slot
      newSlot = &MailSlotTable[i];
      newSlot->mboxID = mbox_id;
      newSlot->nextSlot = NULL;
      newSlot->message = malloc(size);
      //add new slot into mailbox slot list
      addSlot(boxSlotList, newSlot);
      //increment total slots in use
      totalSlotsInUse++;
      break;
    }
  }
  return newSlot;
}

/* ------------------------------------------------------------------------
   Name - addSlot
   Purpose - places new slot at the end of the specified slot list
   Parameters - front of the list, slot to add
   Returns - nothing
   Side Effects - adds the slot into the end of the slot list specified by front.
   ----------------------------------------------------------------------- */
void addSlot(slotPtr front, slotPtr toAdd){

}
