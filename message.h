
#define DEBUG2 1
#define NULL 0

typedef struct mailSlot *slotPtr;
typedef struct mailbox   mailbox;
typedef struct mboxProc *mboxProcPtr;

struct mailbox {
    int          mboxID;
    int          numSlots;
    int          slotsInUse;
    int          slotSize;
    slotPtr      firstSlot;
};

struct mailSlot {
    int       mboxID;
    int       status;
    char*     message;
    slotPtr   nextSlot;
};

struct mboxProc{
    int       pid;
    int       status;
    //pointer to next proc
    //place to store message directly by a mailbox
    //probably a few more, but less than phase 1
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

#define EMPTY -1

//probably some blocked on send/recieve ready, various other constants for proc table
