#ifndef NP_HISTORY_EMBEDDED
#define INCL_DOSMEMMGR
#define INCL_DOSFILEMGR
#include <os2.h>
#include <string.h>
#include "TEXT.H"
#include "HISTORY.H"
#endif
/* Records own copied bytes, never document pointers. current is the last
   applied record; its next record (or first) is the next redo operation. */
typedef struct {
    SEL prev,next;
    ULONG at,beforeRevision,afterRevision;
    HistoryPos before,after;
    USHORT n,beforeEol,afterEol;
    BOOL insert;
    char data[HISTORY_PAYLOAD];
} HistoryRecord;
static HistoryRecord far *HR(SEL s)
{ return (HistoryRecord far *)MAKEP(s,0); }
void HistoryInit(History *h)
{
    memset(h,0,sizeof(*h));h->limit=HISTORY_RECORDS;
    h->byteLimit=HISTORY_BYTES;h->savedValid=TRUE;h->nextRevision=1;
}
void HistoryFree(History *h)
{
    SEL s=h->first,next;
    while(s){next=HR(s)->next;DosFreeSeg(s);s=next;}
    HistoryInit(h);
}
void HistoryBreak(History *h) { h->group=FALSE; }
void HistorySaved(History *h)
{ h->savedRevision=h->revision;h->savedValid=TRUE;HistoryBreak(h); }
BOOL HistoryModified(History *h)
{ return !h->savedValid || h->revision!=h->savedRevision; }
static void DropRedo(History *h)
{
    SEL s=h->current?HR(h->current)->next:h->first,next;
    while(s){next=HR(s)->next;DosFreeSeg(s);h->count--;h->bytes-=sizeof(HistoryRecord);s=next;}
    if(h->current)HR(h->current)->next=0;else h->first=0;
    h->last=h->current;
}
int HistoryEdit(History *h,Text *t,ULONG at,const char far *data,
                USHORT n,BOOL insert,BOOL typing,HistoryPos before,HistoryPos after)
{
    SEL sel=0;HistoryRecord far *r=NULL;BOOL merge=FALSE;int rc;
    USHORT oldEol=t->eol;
    if(!n)return TE_OK;
    if(!h->nextRevision)return TE_RANGE;
    if(n>HISTORY_PAYLOAD || at>t->bytes || (!insert && n>t->bytes-at))return TE_RANGE;
    if(!h->limit || h->byteLimit<sizeof(HistoryRecord))return HE_LIMIT;
    if(typing && insert && n==1 && h->group && h->current && h->current==h->last){
        r=HR(h->current);
        merge=r->insert && r->n<HISTORY_GROUP && at==r->at+r->n &&
              before.line==r->after.line && before.col==r->after.col;
    }
    if(!merge){
        if(DosAllocSeg(sizeof(HistoryRecord),&sel,0))return TE_MEMORY;
        r=HR(sel);_fmemset(r,0,sizeof(*r));
        r->at=at;r->n=n;r->insert=insert;r->before=before;r->after=after;
        r->beforeRevision=h->revision;r->beforeEol=oldEol;
        if(insert)_fmemcpy(r->data,data,n);
        else {
            char bytes[HISTORY_PAYLOAD];
            if(TextRead(t,at,bytes,n)!=n){DosFreeSeg(sel);return TE_RANGE;}
            _fmemcpy(r->data,bytes,n);
        }
    }
    rc=insert?TextInsert(t,at,data,n):TextDelete(t,at,n);
    if(rc){if(sel)DosFreeSeg(sel);HistoryBreak(h);return rc;}
    /* Nothing below can fail. Redo is discarded only after the edit commits. */
    DropRedo(h);
    h->revision=h->nextRevision++;
    if(merge){_fmemcpy(r->data+r->n,data,n);r->n+=n;r->after=after;}
    else {
        r->prev=h->last;
        if(h->last)HR(h->last)->next=sel;else h->first=sel;
        h->last=h->current=sel;h->count++;h->bytes+=sizeof(*r);
    }
    TextRecount(t);r->afterEol=t->eol;r->afterRevision=h->revision;
    h->group=typing && insert && n==1;
    while(h->count>h->limit || h->bytes>h->byteLimit){
        SEL oldest=h->first;
        h->first=HR(oldest)->next;
        if(h->first)HR(h->first)->prev=0;
        DosFreeSeg(oldest);h->count--;h->bytes-=sizeof(*r);
    }
    return TE_OK;
}
int HistoryStep(History *h,Text *t,BOOL redo,HistoryPos *position)
{
    SEL sel;HistoryRecord far *r;BOOL insert;int rc;
    HistoryBreak(h);
    sel=redo?(h->current?HR(h->current)->next:h->first):h->current;
    if(!sel)return HE_EMPTY;
    r=HR(sel);insert=redo?r->insert:!r->insert;
    rc=insert?TextInsert(t,r->at,r->data,r->n):TextDelete(t,r->at,r->n);
    if(rc)return rc;
    TextRecount(t);t->eol=redo?r->afterEol:r->beforeEol;
    *position=redo?r->after:r->before;
    h->revision=redo?r->afterRevision:r->beforeRevision;
    h->current=redo?sel:r->prev;
    return TE_OK;
}
