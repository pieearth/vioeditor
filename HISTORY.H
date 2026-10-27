#ifndef NP_HISTORY_H
#define NP_HISTORY_H
/* Conservative defaults; payloads and records each fit in one segment. */
#define HISTORY_RECORDS 128
#define HISTORY_BYTES 65536UL
#define HISTORY_PAYLOAD 256
#define HISTORY_GROUP 64
#define HE_EMPTY 4
#define HE_LIMIT 5
typedef struct { ULONG line, col; } HistoryPos;
typedef struct {
    SEL first,last,current;
    USHORT count,limit;
    ULONG bytes,byteLimit,revision,nextRevision,savedRevision;
    BOOL savedValid,group;
} History;
void HistoryInit(History *h);
void HistoryFree(History *h);
void HistoryBreak(History *h);
void HistorySaved(History *h);
BOOL HistoryModified(History *h);
int HistoryEdit(History *h,Text *t,ULONG at,const char far *data,
                USHORT n,BOOL insert,BOOL typing,HistoryPos before,HistoryPos after);
int HistoryStep(History *h,Text *t,BOOL redo,HistoryPos *position);
#endif
