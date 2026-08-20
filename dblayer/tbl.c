
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "tbl.h"
#include "codec.h"
#include "../pflayer/pf.h"

#define SLOT_COUNT_OFFSET 2
#define checkerr(err) {if (err < 0) {PF_PrintError(); exit(EXIT_FAILURE);}}

int  getLen(int slot, byte *pageBuf);
int  getNumSlots(byte *pageBuf);
void setNumSlots(byte *pageBuf, int nslots);
int  getNthSlotOffset(int slot, char* pageBuf);
int  getFreeSpaceEnd(byte *pageBuf);

void setFreeSpaceEnd(byte *pageBuf, int freeSpace);
void setNthSlot(int slot, char* pageBuf, int offset, int length);
int  getRid(int pageNum, int slot);

/**
   Opens a paged file, creating one if it doesn't exist, and optionally
   overwriting it.
   Returns 0 on success and a negative error code otherwise.
   If successful, it returns an initialized Table*.
 */
int
Table_Open(char *dbname, Schema *schema, bool overwrite, Table **ptable)
{
    // Initialize PF, create PF file,
    // allocate Table structure  and initialize and return via ptable
    // The Table structure only stores the schema. The current functionality
    // does not really need the schema, because we are only concentrating
    // on record storage. 
    PF_Init();//What if more than one file open will it close all of them??
    if(overwrite){
        PF_DestroyFile(dbname);
        checkerr(PF_CreateFile(dbname));
    }else{
        FILE *fp = fopen(dbname, "r");
        if(fp==NULL){
            checkerr(PF_CreateFile(dbname));
        }else{
            fclose(fp);
        }
    }
    int fd = PF_OpenFile(dbname);
    checkerr(fd);
    *ptable = malloc(sizeof(Table));
    (*ptable)->schema = schema;
    (*ptable)->fd=fd;
    (*ptable)->currentPage=-1;

    return 0;
}

void
Table_Close(Table *tbl) {
    // Unfix any dirty pages, close file.
    if(tbl->currentPage>-1 ){
        PF_UnfixPage(tbl->fd, tbl->currentPage, FALSE);
    }
    PF_CloseFile(tbl->fd);
    free(tbl);
}



int
Table_Insert(Table *tbl, byte *record, int len, RecId *rid) {
    // Allocate a fresh page if len is not enough for remaining space
    // Get the next free slot on page, and copy record in the free
    // space
    // Update slot and free space index information on top of page.
    int pageNum=tbl->currentPage;
    char *pageBuf;
    int numSlots;
    int freeSpaceEnd;
    bool newPageNeeded = false;
    if(pageNum>-1){
        PF_GetThisPage(tbl->fd, pageNum, &pageBuf);
        numSlots=getNumSlots(pageBuf);
        freeSpaceEnd=getFreeSpaceEnd(pageBuf);
        if((freeSpaceEnd-(8+numSlots*8))<len+8){
            newPageNeeded=true;
            if(PF_UnfixPage(tbl->fd, pageNum, FALSE) != PFE_OK){
                return -1;// Ask sir whether to return -1 or checkerror()
            }
        }
    }
    if(pageNum==-1 || newPageNeeded){
        int err = PF_AllocPage(tbl->fd, &pageNum, &pageBuf);
        if(err < 0){
            return -1;
        }
        numSlots=0;
        freeSpaceEnd=PF_PAGE_SIZE;
        setNumSlots(pageBuf, 0);
        setFreeSpaceEnd(pageBuf, PF_PAGE_SIZE);
        tbl->currentPage = pageNum;
    }

    int offset=freeSpaceEnd-len;
    memcpy(pageBuf+offset, record, len);
    setNthSlot(numSlots, pageBuf, offset, len);
    setNumSlots(pageBuf, numSlots+1);
    setFreeSpaceEnd(pageBuf, offset);
    *rid= getRid(pageNum, numSlots);
    if(PF_UnfixPage(tbl->fd, pageNum, TRUE) != PFE_OK){
        return -1;
    }
    return 0;
}

#define checkerr(err) {if (err < 0) {PF_PrintError(); exit(EXIT_FAILURE);}}

/*
  Given an rid, fill in the record (but at most maxlen bytes).
  Returns the number of bytes copied.
 */
int
Table_Get(Table *tbl, RecId rid, byte *record, int maxlen) {
    int slot = rid & 0xFFFF;
    int pageNum = rid >> 16;

    // PF_GetThisPage(pageNum)
    // In the page get the slot offset of the record, and
    // memcpy bytes into the record supplied.
    // Unfix the page
    char *pageBuf;
    PF_GetThisPage(tbl->fd, pageNum, &pageBuf);
    int slot_offset = getNthSlotOffset(slot, pageBuf);
    int rec_len = getLen(slot, pageBuf);
    int len = rec_len <= maxlen ? rec_len : maxlen;
    memcpy(record, pageBuf + slot_offset, len);
    if(PF_UnfixPage(tbl->fd, pageNum, FALSE) != PFE_OK){
        return -1;
    }
    return len; // return size of record
}

void
Table_Scan(Table *tbl, void *callbackObj, ReadFunc callbackfn) {

    UNIMPLEMENTED;

    // For each page obtained using PF_GetFirstPage and PF_GetNextPage
    //    for each record in that page,
    //          callbackfn(callbackObj, rid, record, recordLen)
    int pageNum;
    char *pageBuf;
    int page_status= PF_GetFirstPage(tbl->fd, &pageNum, &pageBuf);
    if(page_status==PFE_EOF){
        return;
    }
    checkerr(page_status);
    int numSlots;
    while(1){
        numSlots=getNumSlots(pageBuf);
        int offset,length,rid;
        for(int i=0;i<numSlots;i++){
            offset=getNthSlotOffset(i, pageBuf);
            length=getLen(i, pageBuf);
            rid=getRid(pageNum, i);
            callbackfn(callbackObj, rid, pageBuf+offset, length);
        }
        if(PF_UnfixPage(tbl->fd, pageNum, FALSE) != PFE_OK){
            return;
        }
        page_status = PF_GetNextPage(tbl->fd, &pageNum, &pageBuf);
        if(page_status==PFE_EOF){
            return;
        }
        checkerr(page_status);
    }



}

int
getNumSlots(byte *pageBuf){
    return *(int *)(pageBuf);
}

void 
setNumSlots(byte *pageBuf, int nslots){
    *(int *)(pageBuf) = nslots;
}

int
getFreeSpaceEnd(byte *pageBuf){
    return *(int *)(pageBuf + 4);
}

void 
setFreeSpaceEnd(byte *pageBuf, int freeSpace){
    *(int *)(pageBuf + 4) = freeSpace;
}

int
getNthSlotOffset(int slot, char* pageBuf){
    int slot_index = 8 + (slot*8);
    return *(int *)(pageBuf + slot_index);
}

int
getLen(int slot, byte *pageBuf){
    int slot_index = 8 + (slot*8);
    return *(int *)(pageBuf + slot_index + 4);
}

void 
setNthSlot(int slot, char* pageBuf, int offset, int length){
    int slot_index = 8 + (slot*8);
    *(int *)(pageBuf + slot_index) = offset;
    *(int *)(pageBuf + slot_index + 4) = length;
}

int 
getRid(int pageNum, int slot){
    return (pageNum << 16) | slot;
}
