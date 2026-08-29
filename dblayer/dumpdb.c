#include <stdio.h>
#include <stdlib.h>
#include "codec.h"
#include "tbl.h"
#include "util.h"
#include "../pflayer/pf.h"
#include "../amlayer/am.h"
#define checkerr(err) {if (err < 0) {PF_PrintError(); exit(1);}}
#define MAX_PAGE_SIZE 4000

void
printRow(void *callbackObj, RecId rid, byte *row, int len) {
    Schema *schema = (Schema *) callbackObj;
    byte *cursor = row;

    for(int i=0; i<schema->numColumns; i++){
        int type=schema->columns[i]->type;
        char strBuf[256];
        switch(type){
            case VARCHAR: 
                int offset = DecodeCString(cursor, strBuf, sizeof(strBuf));
                printf("%s",strBuf);
                cursor += offset+2;
                break;
            
            case INT:
                int intVal = DecodeInt(cursor);
                printf("%d", intVal);
                cursor += 4;
                break;
            
            case LONG:
                long long longVal = DecodeLong(cursor);
                printf("%lld", longVal);
                cursor+=8;
                break;
        }
        if(i!=schema->numColumns-1)printf(",");
        else printf("\n");
    }
}

#define DB_NAME "data.db"
#define INDEX_NAME "data.db.0"
	 
void
index_scan(Table *tbl, Schema *schema, int indexFD, int op, int value) {
    //UNIMPLEMENTED;
    /*
    Open index ...
    while (true) {
	find next entry in index
	fetch rid from table
        printRow(...)
    }
    close index ...
    */
    int scan = AM_OpenIndexScan(indexFD, 'i', 4, op, (char *)&value);
    if(scan<0){
        AM_PrintError("AM_OpenIndexScan failed.");
        exit(1);
    }
    int rid;
    byte record[MAX_PAGE_SIZE];
    while(true){
        rid = AM_FindNextEntry(scan);
        if(rid<0)break;
        int len = Table_Get(tbl, rid, record, sizeof(record));
        printRow(schema, rid, record, len);
    }

    int err = AM_CloseIndexScan(scan);
    if(err<0){
        AM_PrintError("AM_CloseIndexScan failed.");
        exit(1);
    }
}

int
main(int argc, char **argv) {
    char *schemaTxt = "Country:varchar,Capital:varchar,Population:int";
    Schema *schema = parseSchema(schemaTxt);
    Table *tbl;

    //UNIMPLEMENTED;
    int err = Table_Open(DB_NAME, schema, false, &tbl);
    checkerr(err);
    if (argc == 2 && *(argv[1]) == 's') {
	//UNIMPLEMENTED;
	// invoke Table_Scan with printRow, which will be invoked for each row in the table.
        Table_Scan(tbl, schema, printRow);
    } else {
	// index scan by default
	int indexFD = PF_OpenFile(INDEX_NAME);
	checkerr(indexFD);

	// Ask for populations less than 100000, then more than 100000. Together they should
	// yield the complete database.
	index_scan(tbl, schema, indexFD, LESS_THAN_EQUAL, 100000);
	index_scan(tbl, schema, indexFD, GREATER_THAN, 100000);
    }
    Table_Close(tbl);
}
