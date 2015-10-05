
#define DEBUG2 0

typedef struct mailSlot *slotPtr;
typedef struct mailbox   mailbox;
typedef struct mboxProc *mboxProcPtr;


struct mailbox {
    int          mboxID;
    int          numSlots;
    int          slotsInUse;
    int          slotSize;
    slotPtr      firstSlot;
    mboxProcPtr  nextBlockedProc;
    mboxProcPtr  nextProcBlockedOnSend;

};

struct mailSlot {
    int       mboxID;
    char*     message;
    int       msg_size;
    slotPtr   nextSlot;
};

struct mboxProc{
    int         pid;
    int         status;
    mboxProcPtr nextProc;
    char*       message;
    int         messageSize;
    int         pidOfMessageSender;
};

struct psrBits {
    unsigned int curMode:1;
    unsigned int curIntEnable:1;
    unsigned int prevMode:1;
    unsigned int prevIntEnable:1;
    unsigned int unused:28;
};

union psrValues {
    struct psrBits bits;
    unsigned int integerPart;
};

extern mailbox* getMboxTable();
extern int termMboxID[USLOSS_TERM_UNITS];
extern int clockMboxID;
extern int diskMboxID[USLOSS_DISK_UNITS];
extern int lastStatusRead;

#define EMPTY -1
#define READY 1
#define SEND_BLOCKED 2
#define RECEIVE_BLOCKED 3


