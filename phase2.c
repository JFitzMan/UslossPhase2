/* ------------------------------------------------------------------------
   phase2.c

   University of Arizona
   Computer Science 452

   ------------------------------------------------------------------------ */

#include <phase1.h>
#include <phase2.h>
#include <usloss.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "message.h"
#include "handler.h"

/* ------------------------- Prototypes ----------------------------------- */
int start1 (char *);
extern int start2 (char *);
void addProcToBlockedList(mboxProcPtr toAdd, int mbox_id);
int inKernelMode(char *procName);
void disableInterrupts();
void enableInterrupts();
slotPtr getEmptySlot(int size, int mbox_id);
void addSlot(slotPtr *front, slotPtr toAdd);
int MboxRelease(int mailboxID);
int waitDevice(int type, int unit, int *status);


/* -------------------------- Globals ------------------------------------- */

int debugflag2 = 1;

// the mail boxes 
mailbox MailBoxTable[MAXMBOX];

// the process table
struct mboxProc processTable[MAXPROC];

//the mail slots
struct mailSlot MailSlotTable[MAXSLOTS];

int nextMailBoxID = 0;
int totalSlotsInUse = 0;

//mailboxes for device interrupt handlers
int clockMboxID;
int termMboxID[USLOSS_TERM_UNITS];
int diskMboxID[USLOSS_DISK_UNITS];
int lastStatusRead = 0;

void (*sys_vec[MAXSYSCALLS])(sysargs *args);

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
	
    
	// Create mailboxes for devices: 1 for clock, 4 for terminals, 2 for disks
	// Seven mailboxes in total
	clockMboxID = MboxCreate(0, 50); // Zero slot mailbox
	for(i =0; i<USLOSS_TERM_UNITS; i++)
		termMboxID[i] = MboxCreate(0, 50);
	diskMboxID[0] = MboxCreate(0, 50);
	diskMboxID[1] = MboxCreate(0, 50);


	// Initialize USLOSS_IntVec and system call handlers, etc...
	USLOSS_IntVec[USLOSS_CLOCK_INT] = clockHandler2;
	USLOSS_IntVec[USLOSS_TERM_INT] = termHandler;
	USLOSS_IntVec[USLOSS_DISK_INT] = diskHandler;
	USLOSS_IntVec[USLOSS_SYSCALL_INT] = syscallHandler;

	for(i =0; i<MAXSYSCALLS; i++)
		sys_vec[i] = nullsys;

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
	if (slot_size > MAX_MESSAGE || slot_size < 0 || slots < 0){
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
	for( i=0; i < MAXMBOX; i++){
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
	MailBoxTable[newMailBoxID].nextBlockedProc = NULL;

	if (DEBUG2 && debugflag2)
		USLOSS_Console("MboxCreate(): initializing new mailbox\n");

	//return mailBoxID
	enableInterrupts();
	return newMailBoxID;
} /* MboxCreate */

/*
Releases a previously created mailbox. Any process 
waiting on the mailbox should be zap’d. Note, however, 
that zap’ing does not quite work. It would work for a high 
priority process releasing low priority processes from the 
mailbox, but not the other way around. You will need to devise a 
different means of handling processes that are blocked on a 
mailbox being released. Essentially, you will need to have a 
blocked process return -3 from the send or receive that caused 
it to block. You will need to have the process that called 
MboxRelease unblock all the blocked processes. When each of these 
processes awake from the block_me call inside send or receive, they 
will need to “notice” that the mailbox has been released...
Return values:
-3: process was zap’d while releasing the mailbox. 
-1: the mailboxID is not a mailbox that is in use.
0: successful completion.
*/
int MboxRelease(int mailboxID){

	if (MailBoxTable[mailboxID].mboxID == EMPTY){
		if (DEBUG2 && debugflag2)
			USLOSS_Console("MboxRelease(): Not valid mailbox ID!\n");
		return -1;
	}

	//there are procs blocked on send, "zap" them before releasing
	if (MailBoxTable[mailboxID].nextProcBlockedOnSend != NULL){
		if (DEBUG2 && debugflag2)
			USLOSS_Console("MboxRelease(): zapping procs blocked on send\n");
		mboxProcPtr cur = MailBoxTable[mailboxID].nextProcBlockedOnSend;
		mboxProcPtr next;
		while (cur != NULL){
			cur->messageSize = -3;
			cur = cur->nextProc;
		}
		//unblock the procs that were waiting on the mailbox
		cur = MailBoxTable[mailboxID].nextProcBlockedOnSend;
		while (cur != NULL){
			next = cur->nextProc;
			unblockProc(cur->pid);
			cur = next;
		}
		MailBoxTable[mailboxID].nextProcBlockedOnSend = NULL;
	}

	//same thing as above, but for procs blocked on receive

	if (MailBoxTable[mailboxID].nextBlockedProc != NULL){
		if (DEBUG2 && debugflag2)
			USLOSS_Console("MboxRelease(): zapping procs blocked on recieve\n");
		mboxProcPtr cur = MailBoxTable[mailboxID].nextBlockedProc;
		while (cur != NULL){
			cur->messageSize = -3;
			cur = cur->nextProc;
		}
		//unblock the procs that were waiting on the mailbox
		cur = MailBoxTable[mailboxID].nextBlockedProc;
		mboxProcPtr next;
		int i = 1;
		while (cur != NULL){
			next = cur->nextProc;
			unblockProc(cur->pid);
			if (DEBUG2 && debugflag2)
				USLOSS_Console("MboxRelease(): Procs unblocked = %d\n", i);
			i++;
			cur = next;
		}
		MailBoxTable[mailboxID].nextBlockedProc = NULL;
	}

	//free the slots, if there are any
	if (MailBoxTable[mailboxID].firstSlot != NULL){
		slotPtr cur = MailBoxTable[mailboxID].firstSlot;
		slotPtr prev = NULL;
		while (cur != NULL){
			prev = cur;
			cur = cur->nextSlot;
			prev->mboxID = EMPTY;
			prev->nextSlot = NULL;
			prev->msg_size = EMPTY;
			free(prev->message);
		}
		MailBoxTable[mailboxID].firstSlot = NULL;
	}

	//clear the table out
	MailBoxTable[mailboxID].mboxID = EMPTY;
	MailBoxTable[mailboxID].numSlots = EMPTY;
	MailBoxTable[mailboxID].slotsInUse = EMPTY;
	MailBoxTable[mailboxID].slotSize = EMPTY;
	MailBoxTable[mailboxID].nextBlockedProc = NULL; 
	MailBoxTable[mailboxID].nextProcBlockedOnSend = NULL;
	MailBoxTable[mailboxID].firstSlot = NULL;

  return 0;

}//mboxRelease


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
	if (processTable[getpid()].messageSize == -3){
		//MailBoxTable[mbox_id].mboxID = EMPTY;
		return -1;
	}
	disableInterrupts();
	processTable[getpid()].pid = getpid();

	//check that arguments are valid
	if (msg_size > MailBoxTable[mbox_id].slotSize){
		if (DEBUG2 && debugflag2)
			USLOSS_Console("MboxSend(): msg is too large!\n");
		enableInterrupts();
		return -1;
	}
	else if (MailBoxTable[mbox_id].mboxID == EMPTY){
		if (DEBUG2 && debugflag2)
			USLOSS_Console("MboxSend(): mbox ID does not exist!!\n");
		enableInterrupts();
		return -1;
	}

	//check to see if there are processes blocked on receive
	if (MailBoxTable[mbox_id].nextBlockedProc != NULL){
		//place message directly in blocked procs message field 
		if (DEBUG2 && debugflag2)
			USLOSS_Console("MboxSend(): process waiting on receive! Placing message in proc table slot..\n");
		//initialize the proper fields in the process table entry
		if (MailBoxTable[mbox_id].nextBlockedProc->messageSize <= msg_size){
			MailBoxTable[mbox_id].nextBlockedProc->messageSize = -1;
			unblockProc(MailBoxTable[mbox_id].nextBlockedProc->pid);
			return -1;
		}
		MailBoxTable[mbox_id].nextBlockedProc->message = malloc(msg_size);
		MailBoxTable[mbox_id].nextBlockedProc->messageSize = msg_size;
		MailBoxTable[mbox_id].nextBlockedProc->pidOfMessageSender = getpid();
		memcpy(MailBoxTable[mbox_id].nextBlockedProc->message, msg_ptr, msg_size);
		if (DEBUG2 && debugflag2)
			USLOSS_Console("MboxSend(): unblocking process that now has the message, pid %d\n", MailBoxTable[mbox_id].nextBlockedProc->pid);

		unblockProc(MailBoxTable[mbox_id].nextBlockedProc->pid);
		//remove the process from the list of blocked processes
		if (MailBoxTable[mbox_id].nextBlockedProc->nextProc == NULL){
			MailBoxTable[mbox_id].nextBlockedProc = NULL;
		}
		else
			MailBoxTable[mbox_id].nextBlockedProc = MailBoxTable[mbox_id].nextBlockedProc->nextProc;

		if (DEBUG2 && debugflag2)
			USLOSS_Console("MboxSend(): Message sent successfully\n");
	}

	//check to make sure system has enough slots left
	else if (totalSlotsInUse >= MAXSLOTS){
		USLOSS_Console("No slots left in system! Halting...\n");
		USLOSS_Halt(1);
	}

	//No slots left in Mailbox
	else if (MailBoxTable[mbox_id].slotsInUse == MailBoxTable[mbox_id].numSlots){
		processTable[getpid()].status = SEND_BLOCKED;
		processTable[getpid()].nextProc = NULL;

		if (MailBoxTable[mbox_id].nextProcBlockedOnSend == NULL){
			MailBoxTable[mbox_id].nextProcBlockedOnSend = &processTable[getpid()];
		}
		else{
			mboxProcPtr cur = MailBoxTable[mbox_id].nextProcBlockedOnSend;
			while (cur->nextProc != NULL){
				cur = cur->nextProc;
			}
			cur->nextProc = &processTable[getpid()];
		}

		processTable[getpid()].message = malloc(msg_size);
		processTable[getpid()].messageSize = msg_size;
		processTable[getpid()].pid = getpid();
		memcpy(processTable[getpid()].message, msg_ptr, msg_size);
		if (DEBUG2 && debugflag2)
			USLOSS_Console("MboxSend(): No slots left! Blocking...\n");
		blockMe(12);//something higher than 10, patrick will post which numbers display what soon
		if (processTable[getpid()].messageSize == -3){
			if (DEBUG2 && debugflag2)
				USLOSS_Console("MboxSend(): Zapped while blocked on send!\n");
			return -3;
		}
	}

	/*free slot in mailbox:
	1)Allocate slot 2)copy message 3)profit
	*/
	else{
		//get new slot and add it to the list of mail slots
		slotPtr newSlot = getEmptySlot(msg_size, mbox_id);
		memcpy(newSlot->message, msg_ptr, msg_size);

		if (MailBoxTable[mbox_id].firstSlot == NULL){
			MailBoxTable[mbox_id].firstSlot = newSlot;
			if (DEBUG2 && debugflag2)
				USLOSS_Console("MboxSend(): first slot created!\n");
		}
		else{
			slotPtr cur = MailBoxTable[mbox_id].firstSlot;
			while (cur->nextSlot != NULL){
				cur = cur->nextSlot;
			}
			cur->nextSlot = newSlot;
			if (DEBUG2 && debugflag2)
				USLOSS_Console("MboxSend(): Added new slot to the end\n");
		}
		if (DEBUG2 && debugflag2)
			USLOSS_Console("MboxSend(): New slot allocated and message copied\n");
	}

	//TURN THOSE INTERRUPTS BACK ON BEFORE LEAVING

	enableInterrupts();
	//message sent successfully
	return 0;

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
	disableInterrupts();
	inKernelMode("MboxReceive");
	processTable[getpid()].pid = getpid();
	processTable[getpid()].nextProc = NULL;
	int toReturn = -1;

	if (processTable[getpid()].messageSize == -3){
		//MailBoxTable[mbox_id].mboxID = EMPTY;
		return -1;
	}
	//check that arguments are valid
	if (msg_size < MailBoxTable[mbox_id].slotSize && msg_ptr != NULL && MailBoxTable[mbox_id].firstSlot != NULL){
		if (DEBUG2 && debugflag2)
			USLOSS_Console("MboxReceive(): invalid message size for receive!\n");
		enableInterrupts();
		return -1;
	}
	if (MailBoxTable[mbox_id].mboxID == EMPTY){
		if (DEBUG2 && debugflag2)
			USLOSS_Console("MboxReceive(): mbox ID does not exist!!\n");
		enableInterrupts();
		return -1;
	}	

	//there are no messages! Process gets blocked until one comes in
	if (MailBoxTable[mbox_id].slotsInUse == 0 && MailBoxTable[mbox_id].nextProcBlockedOnSend == NULL){
		processTable[getpid()].status = RECEIVE_BLOCKED;
		processTable[getpid()].messageSize = msg_size;
    
		if (DEBUG2 && debugflag2)
			USLOSS_Console("MboxReceive(): No messages to receive! Blocking...\n");
		
	
		if (MailBoxTable[mbox_id].nextBlockedProc == NULL){
			MailBoxTable[mbox_id].nextBlockedProc = &processTable[getpid()];
			if (DEBUG2 && debugflag2)
				USLOSS_Console("MboxReceive(): first proc to block on revieve\n");
		}
		else{
			mboxProcPtr cur = MailBoxTable[mbox_id].nextBlockedProc;
			while (cur->nextProc != NULL){
				cur = cur->nextProc;
			}
			cur->nextProc = &processTable[getpid()];
			if (DEBUG2 && debugflag2){
				USLOSS_Console("MboxReceive(): added to end of blocked proc list\n");
				cur = MailBoxTable[mbox_id].nextBlockedProc;
				USLOSS_Console("MboxRecieve(): BLocked list:\n");
				while(cur != NULL){
					USLOSS_Console("MboxRecieve():          %d\n", cur->pid);
					cur = cur->nextProc;
				}
			}
		}
		blockMe(11);
		disableInterrupts();
	
		if (processTable[getpid()].messageSize == -3){
			//MailBoxTable[mbox_id].mboxID = EMPTY;
			return -3;
		}
		else if (processTable[getpid()].messageSize == -1){
			//MailBoxTable[mbox_id].mboxID = EMPTY;
			return -1;
		}

		if (DEBUG2 && debugflag2)
			USLOSS_Console("MboxReceive(): Unblocked, message reads: %s\n", processTable[getpid()].message);
		memcpy(msg_ptr, processTable[getpid()].message, processTable[getpid()].messageSize);
		toReturn = processTable[getpid()].messageSize;
		if (DEBUG2 && debugflag2)
			USLOSS_Console("MboxReceive(): Unblocking sender, pid %d\n", processTable[getpid()].pidOfMessageSender);
		//unblock the sending process
		unblockProc(processTable[getpid()].pidOfMessageSender);
	}
  
	else if (MailBoxTable[mbox_id].slotsInUse == 0 && MailBoxTable[mbox_id].nextProcBlockedOnSend != NULL)
    {
		if (DEBUG2 && debugflag2)
			USLOSS_Console("MboxReceive(): There is a process blocked on send, lets get that message!\n");
		memcpy(msg_ptr, MailBoxTable[mbox_id].nextProcBlockedOnSend->message, MailBoxTable[mbox_id].nextProcBlockedOnSend->messageSize);
		free(MailBoxTable[mbox_id].nextProcBlockedOnSend->message);
		int pidToUnblock = MailBoxTable[mbox_id].nextProcBlockedOnSend->pid;
		toReturn = MailBoxTable[mbox_id].nextProcBlockedOnSend->messageSize;

		if (MailBoxTable[mbox_id].nextProcBlockedOnSend->nextProc == NULL){
			if (DEBUG2 && debugflag2)
				USLOSS_Console("MboxReceive(): No more procs blocked on send\n");
			MailBoxTable[mbox_id].nextProcBlockedOnSend = NULL;
		}
		else{
			MailBoxTable[mbox_id].nextProcBlockedOnSend = MailBoxTable[mbox_id].nextProcBlockedOnSend->nextProc;
		}
		unblockProc(pidToUnblock);
	}
	//there is a message to receive waiting in the box
	else{
		//copy the message from the slot, free the slot
		memcpy(msg_ptr, MailBoxTable[mbox_id].firstSlot->message, MailBoxTable[mbox_id].firstSlot->msg_size);
		if (DEBUG2 && debugflag2)
			USLOSS_Console("MboxReceive(): Copied message to buffer\n");

		slotPtr toFree = MailBoxTable[mbox_id].firstSlot;
		if (MailBoxTable[mbox_id].firstSlot->nextSlot == NULL){
			if (DEBUG2 && debugflag2)
				USLOSS_Console("MboxReceive(): No more slots in slot list, mailbox is empty\n");
			MailBoxTable[mbox_id].firstSlot = NULL;
		}
		else{
			if (DEBUG2 && debugflag2)
				USLOSS_Console("MboxReceive(): Removing slot from slot list\n");
		MailBoxTable[mbox_id].firstSlot = MailBoxTable[mbox_id].firstSlot->nextSlot;
		}
		//decrement slotsInUse
		MailBoxTable[mbox_id].slotsInUse--;
		//free the box, grab the return value first
		toReturn = toFree->msg_size;
		toFree->mboxID = EMPTY;
		toFree->nextSlot = NULL;
		toFree->msg_size = EMPTY;
		free(toFree->message);
		if (DEBUG2 && debugflag2)
			USLOSS_Console("MboxReceive(): Freed allocated memory\n");

		//is there any procs waiting on block? If so, get their message sent and free them.
		if (MailBoxTable[mbox_id].nextProcBlockedOnSend != NULL)
		{
			if (DEBUG2 && debugflag2)
				USLOSS_Console("MboxReceive(): There is a process blocked on send, and we just freed a slot!\n");
			MboxSend(mbox_id, MailBoxTable[mbox_id].nextProcBlockedOnSend->message, MailBoxTable[mbox_id].nextProcBlockedOnSend->messageSize);
			free(MailBoxTable[mbox_id].nextProcBlockedOnSend->message);
			int pidToUnblock = MailBoxTable[mbox_id].nextProcBlockedOnSend->pid;
			if (MailBoxTable[mbox_id].nextProcBlockedOnSend->nextProc == NULL){
				if (DEBUG2 && debugflag2)
					USLOSS_Console("MboxReceive(): No more procs blocked on send\n");
				MailBoxTable[mbox_id].nextProcBlockedOnSend = NULL;
			}
			else
				MailBoxTable[mbox_id].nextProcBlockedOnSend = MailBoxTable[mbox_id].nextProcBlockedOnSend->nextProc;
			unblockProc(pidToUnblock);
		}
	}

	enableInterrupts();
	return toReturn;

} /* MboxReceive */

/*
 * Conditionally sends a message to a mailbox. Does not block the invoking process.
 * Returns different values depending on the outcome.
*/
int MboxCondSend(int mbox_id, void *msg_ptr, int msg_size){
	disableInterrupts();
	processTable[getpid()].pid = getpid();

	//check that arguments are valid
	if (msg_size > MAX_MESSAGE){
		if (DEBUG2 && debugflag2)
			USLOSS_Console("MboxCondSend(): msg is too large!\n");
		enableInterrupts();
		return -1;
	}else if (MailBoxTable[mbox_id].mboxID == EMPTY){
		if (DEBUG2 && debugflag2)
			USLOSS_Console("MboxCondSend(): mbox ID does not exist!!\n");
		enableInterrupts();
		return -1;
	}

	//check to see if there are processes blocked on receive
	if (MailBoxTable[mbox_id].nextBlockedProc != NULL){
		//place message directly in blocked procs message field 
		if (DEBUG2 && debugflag2)
			USLOSS_Console("MboxCondSend(): process waiting on receive! Placing message in proc table slot...\n");
		MailBoxTable[mbox_id].nextBlockedProc->message = malloc(msg_size);
		MailBoxTable[mbox_id].nextBlockedProc->messageSize = msg_size;
		MailBoxTable[mbox_id].nextBlockedProc->pidOfMessageSender = getpid();
		memcpy(MailBoxTable[mbox_id].nextBlockedProc->message, msg_ptr, msg_size);
		if (DEBUG2 && debugflag2)
			USLOSS_Console("MboxCondSend(): unblocking process that now has the message, pid %d\n", MailBoxTable[mbox_id].nextBlockedProc->pid);


		unblockProc(MailBoxTable[mbox_id].nextBlockedProc->pid);

    if (MailBoxTable[mbox_id].nextBlockedProc->nextProc == NULL ){
      MailBoxTable[mbox_id].nextBlockedProc = NULL;
    }
    else{
      MailBoxTable[mbox_id].nextBlockedProc = MailBoxTable[mbox_id].nextBlockedProc->nextProc;
    }

		if (DEBUG2 && debugflag2)
			USLOSS_Console("MboxCondSend(): Message sent successfully\n");
	}
	
	//check to make sure system has enough slots left
	else if (totalSlotsInUse >= MAXSLOTS){
    if (DEBUG2 && debugflag2)
		  USLOSS_Console("MBoxCondSend(): No slots left in system! Returning -2...\n");
		enableInterrupts();
		return -2;
	}
	//No slots left in Mailbox
	else if (MailBoxTable[mbox_id].slotsInUse == MailBoxTable[mbox_id].numSlots){
		if (DEBUG2 && debugflag2)
			USLOSS_Console("MboxCondSend(): No slots left! Returning -2...\n");
			enableInterrupts();
			return -2; 
	}
	else{
		//get new slot and add it to the list of mail slots
    if (DEBUG2 && debugflag2)
      USLOSS_Console("MboxCondSend(): Adding new slot to mailbox\n");
		slotPtr newSlot = getEmptySlot(msg_size, mbox_id);
		memcpy(newSlot->message, msg_ptr, msg_size);

		if (MailBoxTable[mbox_id].firstSlot == NULL){
			MailBoxTable[mbox_id].firstSlot = newSlot;
		}
		else{
			slotPtr cur = MailBoxTable[mbox_id].firstSlot;
			while (cur->nextSlot != NULL){
				cur = cur->nextSlot;
		}
		cur->nextSlot = newSlot;
    }

    if (DEBUG2 && debugflag2){
        USLOSS_Console("MboxCondSend(): New slot allocated and message copied\n");
      }
  }
	
	enableInterrupts();
	if (DEBUG2 && debugflag2){
        USLOSS_Console("MboxCondSend(): Message sent!\n");
      }
	return 0; //message sent successfully
} /* MboxCondSend */

/*
 * Conditionally receives a message to a mailbox. Does not block the invoking process.
 * Returns different values depending on the outcome.
*/
int MboxCondReceive(int mbox_id, void *msg_ptr, int msg_size){

	disableInterrupts();
	inKernelMode("MboxReceive");
	processTable[getpid()].pid = getpid();
	int toReturn = -1;
	//check that arguments are valid
	if (MailBoxTable[mbox_id].firstSlot != NULL && msg_size < MailBoxTable[mbox_id].firstSlot->msg_size && msg_ptr != NULL){
		if (DEBUG2 && debugflag2)
			USLOSS_Console("MboxCondReceive(): invalid message size for receive!\n");
		enableInterrupts();
		return -1;
	}else if (MailBoxTable[mbox_id].mboxID == EMPTY){
		if (DEBUG2 && debugflag2)
			USLOSS_Console("MboxCondReceive(): mbox ID does not exist!!\n");
		enableInterrupts();
		return -1;
	}
	
	//there are no messages! Process gets blocked until one comes in
	if (MailBoxTable[mbox_id].slotsInUse == 0){
		if (DEBUG2 && debugflag2)
			USLOSS_Console("MboxCondReceive(): No messages to receive! Returning -2...\n");
		enableInterrupts();
		return -2; //Returns value for no messages, per spec
	}

	//there is a message to receive waiting in the box
	else{
		//copy the message from the slot, free the slot
		memcpy(msg_ptr, MailBoxTable[mbox_id].firstSlot->message, MailBoxTable[mbox_id].firstSlot->msg_size);
		if (DEBUG2 && debugflag2)
			USLOSS_Console("MboxCondReceive(): Copied message to buffer\n");

		slotPtr toFree = MailBoxTable[mbox_id].firstSlot;
		if (MailBoxTable[mbox_id].firstSlot->nextSlot == NULL){
			MailBoxTable[mbox_id].firstSlot = NULL;
		}
		else{
			MailBoxTable[mbox_id].firstSlot = MailBoxTable[mbox_id].firstSlot->nextSlot;
		}
		//decrement slotsInUse
		MailBoxTable[mbox_id].slotsInUse--;
		totalSlotsInUse --;
		//free the box, grab the return value first
		toReturn = toFree->msg_size;
		toFree->mboxID = EMPTY;
		toFree->nextSlot = NULL;
		free(toFree->message);
		if (DEBUG2 && debugflag2)
			USLOSS_Console("MboxCondReceive(): Freed allocated memory\n");
	}
	//is there any procs waiting on block? If so, get their message sent and free them.
    if (MailBoxTable[mbox_id].nextProcBlockedOnSend != NULL)
    {
		if (DEBUG2 && debugflag2)
			USLOSS_Console("MboxCondReceive(): There is a process blocked on send, and we just freed a slot!\n");
		MboxSend(mbox_id, MailBoxTable[mbox_id].nextProcBlockedOnSend->message, MailBoxTable[mbox_id].nextProcBlockedOnSend->messageSize);
		int pidToUnblock = MailBoxTable[mbox_id].nextProcBlockedOnSend->pid;
		toReturn = MailBoxTable[mbox_id].nextProcBlockedOnSend->messageSize;
		if (MailBoxTable[mbox_id].nextProcBlockedOnSend->nextProc == NULL){
			if (DEBUG2 && debugflag2)
				USLOSS_Console("MboxCondReceive(): No more procs blocked on send\n");
			MailBoxTable[mbox_id].nextProcBlockedOnSend = NULL;
		}
		else
			MailBoxTable[mbox_id].nextProcBlockedOnSend = MailBoxTable[mbox_id].nextProcBlockedOnSend->nextProc;
		unblockProc(pidToUnblock);
	}

	enableInterrupts();
	return toReturn; 

} /* MboxCondReceive */

int waitDevice(int type, int unit, int *status){
	int result;
	char buffer[50];
	
	//Not sure if switch is entirely necessary, but it works
	switch(type){
		case USLOSS_CLOCK_DEV:
			result = MboxReceive(clockMboxID, buffer, 50); //Starts a receive on the clock's mailbox
			if (DEBUG2 && debugflag2)
				USLOSS_Console("waitDevice(): Recieve successful!\n");
			USLOSS_DeviceInput(type, unit, status); //Gets the device's status (passes by reference)
			if (result == -3) return -1; //If process gets zapped
			else return 0;
		case USLOSS_TERM_DEV:
			result = MboxReceive(termMboxID[unit], buffer, 50); 
			if (DEBUG2 && debugflag2)
				USLOSS_Console("waitDevice(): Recieve successful!\n");
			*status = lastStatusRead; //Terminal gets status slightly differently
			if (result == -3) return -1;
			else return 0;
		case USLOSS_DISK_DEV: 
			result = MboxReceive(diskMboxID[unit], buffer, 50);
			if (DEBUG2 && debugflag2)
				USLOSS_Console("waitDevice(): Recieve successful!\n");
			USLOSS_DeviceInput(type, unit, status);        
			if (result == -3) return -1;
			else return 0;
		default:
			break;			
	}
	return -2; //Just something so we know it made it all the way through. Should not get here.
}

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
		if (DEBUG2 && debugflag2)
			USLOSS_Console("addProcToBlockedList(): first proc to block on this mailbox\n");
		MailBoxTable[mbox_id].nextBlockedProc = toAdd;
	}
	//it must be added to the end of the blocked list
	else{
		mboxProcPtr prev = NULL;
		mboxProcPtr cur;
		//get a pointer to the last proc
		for (cur = MailBoxTable[mbox_id].nextBlockedProc;cur != NULL; cur = cur->nextProc){
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
slotPtr getEmptySlot(int size, int mbox_id){
	slotPtr newSlot = NULL;
	int i;
	for (i=0; i < MAXSLOTS; i++){
		if (MailSlotTable[i].mboxID == EMPTY){
			if (DEBUG2 && debugflag2)
				USLOSS_Console("getEmptySlot(): About to create a new slot\n");
			//initialize new slot
			newSlot = &MailSlotTable[i];
			newSlot->mboxID = mbox_id;
			newSlot->nextSlot = NULL;
			newSlot->msg_size = size;
			newSlot->message = malloc(size);
			//increment total slots in use
			MailBoxTable[mbox_id].slotsInUse++;
			totalSlotsInUse++;
			break;
		}
	}
	if (DEBUG2 && debugflag2)
		USLOSS_Console("getEmptySlot(): new slot created, total slots: %d\n", totalSlotsInUse);
	return newSlot;
}

/* ------------------------------------------------------------------------
   Name - addSlot
   Purpose - places new slot at the end of the specified slot list
   Parameters - front of the list, slot to add
   Returns - nothing
   Side Effects - adds the slot into the end of the slot list specified by front.
   ----------------------------------------------------------------------- */
void addSlot(slotPtr *front, slotPtr toAdd){
	//no other procs on list, easy add
	if (front == NULL){
		front = &toAdd;
	}
	//it must be added to the end of the blocked list
	else{
		slotPtr prev = NULL;
		slotPtr cur;
		//get a pointer to the last proc
		for (cur = *front; cur != NULL; cur = cur->nextSlot){
			prev = cur;
		}
		prev->nextSlot = toAdd;
	}//end else
	
}//addSlot

mailbox* getMboxTable(){
	//Sends the mailbox table through
	return MailBoxTable;
}

