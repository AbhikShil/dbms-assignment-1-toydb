
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "tbl.h"
#include "codec.h"
#include "../pflayer/pf.h"

#define SLOT_COUNT_OFFSET 2
#define checkerr(err) {if (err < 0) {PF_PrintError(); exit(EXIT_FAILURE);}}

int getNumSlots(byte *pageBuf);
int  getNthSlotOffset(int slot, char* pageBuf);

int  getLen(int slot, byte *pageBuf){
    int nslots = getNumSlots(pageBuf);
    int offset = getNthSlotOffset(slot, pageBuf);
    
    int NextOffset;
    if(slot == nslots-1){
        NextOffset = DecodeShort(pageBuf);
    }else{
        NextOffset = getNthSlotOffset(slot+1, pageBuf);
    }

    return offset - NextOffset;

}

int  getNumSlots(byte *pageBuf){
    return DecodeShort(pageBuf + SLOT_COUNT_OFFSET);

}
void setNumSlots(byte *pageBuf, int nslots){
    EncodeShort((short)nslots, pageBuf+SLOT_COUNT_OFFSET);
}
int  getNthSlotOffset(int slot, char* pageBuf){
    return DecodeShort(pageBuf+4+slot*2);
}



/**
   Opens a paged file, creating one if it doesn't exist, and optionally
   overwriting it.
   Returns 0 on success and a negative error code otherwise.
   If successful, it returns an initialized Table*.
 */
int
Table_Open(char *dbname, Schema *schema, bool overwrite, Table **ptable)
{
    //UNIMPLEMENTED;
    PF_Init();

    if(overwrite){

        //destroy previous file and create a new one - Antro
        PF_DestroyFile(dbname);
        PF_CreateFile(dbname);
    }

    int fd = PF_OpenFile(dbname);
    if(fd < 0){
        return fd;
    }

    Table* tbl = (Table *)malloc(sizeof(Table));
    if(tbl == NULL){
        PF_CloseFile(fd);
        return PFE_NOMEM;
    }

    tbl->schema = schema;
    tbl->fd = fd;
    tbl->currpage = -1;

    *ptable = tbl;

    return PFE_OK;
}

void
Table_Close(Table *tbl) {
    int err = PF_CloseFile(tbl->fd);
    checkerr(err);
    free(tbl);
}


int
Table_Insert(Table *tbl, byte *record, int len, RecId *rid) {
    int err;
    char *pageBuf;

    if(tbl->currpage == -1){ //that means we do not have a page yet
        err = PF_GetNextPage(tbl->fd, &tbl->currpage, &pageBuf);
        if(err == PFE_EOF){
            // file has no pages yet
            err = PF_AllocPage(tbl->fd, &tbl->currpage, &pageBuf);

            checkerr(err);

            EncodeShort(PF_PAGE_SIZE, pageBuf);
            setNumSlots(pageBuf, 0);
        }else{
            checkerr(err);
        }
    }else{
        // Get the page we are currently inserting in
        err = PF_GetThisPage(tbl->fd, tbl->currpage, &pageBuf);

        checkerr(err);
    }
    
    int nslots = getNumSlots(pageBuf);
    int freeOffset = DecodeShort(pageBuf);

    int slotOffset = 4 + nslots*2; //first 4 bytes are for free offset pointer and num slots, then we have n slot offsets each of 2B
    int recordOffset = freeOffset - len;

    //check if current page is full, that is if record offset and slotoffset overlaps
    if(slotOffset + 2 > recordOffset){
        //current page is full
        err = PF_UnfixPage(tbl->fd, tbl->currpage, FALSE);
        checkerr(err);

        err = PF_AllocPage(tbl->fd, &tbl->currpage, &pageBuf);
        checkerr(err);

        //Initialize the new page
        EncodeShort(PF_PAGE_SIZE, pageBuf);
        setNumSlots(pageBuf, 0);
        
        nslots = 0;
        freeOffset = PF_PAGE_SIZE;
        slotOffset = 4;
        recordOffset = freeOffset - len;

        //record itself must fit
        if(slotOffset +2 > recordOffset){
            PF_UnfixPage(tbl->fd,
                         tbl->currpage,
                         FALSE);
            return PFE_NOMEM;
        }

    }

    //finally time to copy the record into page
    memcpy(pageBuf+recordOffset, record, len );

    EncodeShort((short)recordOffset, pageBuf+slotOffset);

    //update the free-space pointer
    EncodeShort((short)recordOffset, pageBuf);

    //increase slotnumber
    setNumSlots(pageBuf, nslots+1);


    *rid = (tbl->currpage << 16) | nslots;

    //mark the page dirty as modifed
    err = PF_UnfixPage(tbl->fd, tbl->currpage, true);
    
    checkerr(err);

    return PFE_OK;

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

    char *pageBuf;
    int err;

    //get the page of record
    err = PF_GetThisPage(tbl->fd, pageNum, &pageBuf);

    checkerr(err);

    //find the offset of the page
    int offset = getNthSlotOffset(slot, pageBuf);

    int len = getLen(slot, pageBuf);

    int copylen = len;
    if(copylen > maxlen){
        copylen = maxlen;
    }

    memcpy(record, pageBuf+offset, copylen);

    err = PF_UnfixPage(tbl->fd, pageNum, false);
    checkerr(err);

    return copylen;
}

void
Table_Scan(Table *tbl, void *callbackObj, ReadFunc callbackfn) {

    int err;
    int pageNum = -1;
    char *pageBuf;

    while((err = PF_GetNextPage(tbl->fd, &pageNum, &pageBuf)) == PFE_OK){
        int nslots = getNumSlots(pageBuf);

        for(int slot=0 ; slot<nslots ; slot++){
            int offset = getNthSlotOffset(slot, pageBuf); 
            int len = getLen(slot, pageBuf);

            RecId rid= (pageNum << 16) | slot;
            callbackfn(callbackObj, rid, (byte*)(pageBuf + offset), len);
        }

        err = PF_UnfixPage(tbl->fd, pageNum, false);
        checkerr(err);
    }


    if(err != PFE_EOF){
        checkerr(err);
    }
    // For each page obtained using PF_GetFirstPage and PF_GetNextPage
    //    for each record in that page,
    //          callbackfn(callbackObj, rid, record, recordLen)
}


