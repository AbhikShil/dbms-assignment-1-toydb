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

    for(int i=0 ; i<schema->numColumns ; i++){
        if(i > 0){
            printf(", ");
        }

        switch(schema->columns[i]->type){

            case VARCHAR:{
                char str[MAX_PAGE_SIZE];
                
                int n = DecodeCString(cursor, str, sizeof(str));

                printf("%s", str);
                cursor += n+2;
                break;
            }

            case INT:{
                int value = DecodeInt(cursor);

                printf("%d", value);

                cursor += 4;
                break;
            }

            case LONG:{
                long long value = DecodeLong(cursor);
                printf("%lld", value);
                cursor += 8;
                break;
            }

            default:
                fprintf(2, "Unknown column type!");
                exit(EXIT_FAILURE);
        }
    }

    printf('\n');

}

#define DB_NAME "data.db"
#define INDEX_NAME "data.db.0"
	 
void
index_scan(Table *tbl, Schema *schema, int indexFD, int op, int value) {
    UNIMPLEMENTED;
    /*
    Open index ...
    while (true) {
	find next entry in index
	fetch rid from table
        printRow(...)
    }
    close index ...
    */
}

int
main(int argc, char **argv) {
    char *schemaTxt = "Country:varchar,Capital:varchar,Population:int";
    Schema *schema = parseSchema(schemaTxt);
    Table *tbl;

    UNIMPLEMENTED;
    if (argc == 2 && *(argv[1]) == 's') {
	UNIMPLEMENTED;
	// invoke Table_Scan with printRow, which will be invoked for each row in the table.
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
