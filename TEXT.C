/* Microsoft C 6.00 / OS/2 1.3: no screen or keyboard dependencies. */
#ifndef NP_TEXT_EMBEDDED
#define INCL_DOSFILEMGR
#define INCL_DOSMEMMGR
#include <os2.h>
#include <string.h>
#include "TEXT.H"
#endif
static TextBlock far *TB(SEL s) { return (TextBlock far *)MAKEP(s,0); }
static SEL NewBlock(void)
{
    SEL s=0;
    if (DosAllocSeg(sizeof(TextBlock), &s, 0)) return 0;
    TB(s)->used=0; TB(s)->next=0; return s;
}
void TextInit(Text *t) { memset(t,0,sizeof(*t)); t->lines=1; t->eol=2; }
void TextFree(Text *t)
{
    SEL s=t->head, next;
    while(s) { next=TB(s)->next; DosFreeSeg(s); s=next; }
    TextInit(t);
}
static void Seek(Text *t, ULONG at, TextIter *i)
{
    i->sel=t->head; i->offset=at; i->pos=0;
    while(i->sel && at >= TB(i->sel)->used) {
        at-=TB(i->sel)->used; i->sel=TB(i->sel)->next;
    }
    i->pos=(USHORT)at;
}
static int Next(TextIter *i)
{
    int c;
    if(!i->sel) return -1;
    c=(unsigned char)TB(i->sel)->data[i->pos++]; i->offset++;
    if(i->pos==TB(i->sel)->used) { i->sel=TB(i->sel)->next; i->pos=0; }
    return c;
}
USHORT TextRead(Text *t, ULONG at, char *buf, USHORT n)
{
    TextIter i; USHORT k=0; int c;
    Seek(t,at,&i);
    while(k<n && (c=Next(&i))!=-1) buf[k++]=(char)c;
    return k;
}
/* Insertion reserves every required segment before changing existing bytes.
   At most one existing block is split; all other blocks retain their storage. */
int TextInsert(Text *t, ULONG at, const char far *src, USHORT n)
{
    SEL s, prev=0, fresh=0; ULONG off=at; USHORT p,j,remain;
    TextBlock far *b; TextBlock far *f;
    if(at>t->bytes || n>TEXT_BLOCK || t->bytes>TEXT_MAX-n) return TE_RANGE;
    if(!n) return TE_OK;
    s=t->head;
    if(at==t->bytes && t->tail) { s=t->tail; off=TB(s)->used; }
    else while(s && off>TB(s)->used) { off-=TB(s)->used; prev=s; s=TB(s)->next; }
    if(!s) {
        fresh=NewBlock(); if(!fresh) return TE_MEMORY;
        b=TB(fresh); _fmemcpy(b->data,src,n); b->used=n;
        if(prev) TB(prev)->next=fresh; else t->head=fresh;
        t->tail=fresh;
    } else {
        b=TB(s); p=(USHORT)off;
        if(b->used+n<=TEXT_BLOCK) {
            for(j=b->used;j>p;j--) b->data[j+n-1]=b->data[j-1];
            _fmemcpy(b->data+p,src,n); b->used+=n;
        } else {
            /* Prefix and insertion may themselves exceed one block. */
            SEL extra=0;
            fresh=NewBlock(); if(!fresh) return TE_MEMORY;
            if(p+n>TEXT_BLOCK) { extra=NewBlock(); if(!extra){DosFreeSeg(fresh);return TE_MEMORY;} }
            f=TB(fresh); remain=(USHORT)(b->used-p);
            _fmemcpy(f->data,b->data+p,remain); f->used=remain; f->next=b->next;
            b->used=p;
            if(extra) {
                USHORT first=(USHORT)(TEXT_BLOCK-p);
                _fmemcpy(b->data+p,src,first); b->used=TEXT_BLOCK;
                _fmemcpy(TB(extra)->data,src+first,(USHORT)(n-first));
                TB(extra)->used=(USHORT)(n-first); TB(extra)->next=fresh; b->next=extra;
            } else { _fmemcpy(b->data+p,src,n); b->used+=n; b->next=fresh; }
            if(!f->next) t->tail=fresh;
            /* Avoid an empty suffix block at EOF. */
            if(!remain) {
                SEL last=extra?extra:s;
                TB(last)->next=f->next; if(t->tail==fresh)t->tail=last;
                DosFreeSeg(fresh);
            }
        }
    }
    t->bytes+=n; return TE_OK;
}
int TextDelete(Text *t, ULONG at, ULONG n)
{
    SEL s=t->head,prev=0,next; ULONG off=at;
    USHORT p,take,j; TextBlock far *b;
    if(at>t->bytes || n>t->bytes-at) return TE_RANGE;
    t->bytes-=n;
    while(s && off>=TB(s)->used) { off-=TB(s)->used; prev=s;s=TB(s)->next; }
    while(n && s) {
        b=TB(s);p=(USHORT)off;take=b->used-p;if((ULONG)take>n)take=(USHORT)n;
        for(j=p;j<b->used-take;j++)b->data[j]=b->data[j+take];
        b->used-=take;n-=take;next=b->next;
        if(!b->used){if(prev)TB(prev)->next=next;else t->head=next;if(t->tail==s)t->tail=prev;DosFreeSeg(s);}
        else prev=s;
        s=next;off=0;
    }
    return TE_OK;
}
/* CRLF remains one separator even across segment boundaries. */
int TextLine(Text *t, ULONG line, ULONG *start, ULONG *len, USHORT *ending)
{
    TextIter i,peek; ULONG current=0,begin=0,pos; int c,d;
    Seek(t,0,&i);
    while((pos=i.offset,(c=Next(&i)))!=-1) {
        if(c=='\r'||c=='\n') {
            USHORT e=1;
            if(c=='\r'){peek=i;d=Next(&peek);if(d=='\n'){i=peek;e=2;}}
            if(current==line){*start=begin;*len=pos-begin;*ending=e;return TE_OK;}
            current++;begin=i.offset;
        }
    }
    if(current==line){*start=begin;*len=t->bytes-begin;*ending=0;return TE_OK;}
    return TE_RANGE;
}
void TextRecount(Text *t)
{
    TextIter i,peek; int c; USHORT first=0;
    t->lines=1;Seek(t,0,&i);
    while((c=Next(&i))!=-1)if(c=='\r'||c=='\n'){
        USHORT e=(c=='\r')?3:1;
        if(c=='\r'){peek=i;if(Next(&peek)=='\n'){i=peek;e=2;}}
        if(!first)first=e;t->lines++;
    }
    if(first)t->eol=first;
}
int TextLoad(Text *t,HFILE hf)
{
    Text staged; char buf[TEXT_BLOCK]; USHORT got; int rc;
    TextInit(&staged);
    do {
        if(DosRead(hf,buf,sizeof(buf),&got)){TextFree(&staged);return TE_IO;}
        rc=TextInsert(&staged,staged.bytes,buf,got);
        if(rc){TextFree(&staged);return rc;}
    }while(got);
    TextRecount(&staged);TextFree(t);*t=staged;return TE_OK;
}
int TextWrite(Text *t,HFILE hf)
{
    SEL s;USHORT wrote;
    for(s=t->head;s;s=TB(s)->next)
        if(DosWrite(hf,TB(s)->data,TB(s)->used,&wrote)||wrote!=TB(s)->used)return TE_IO;
    return TE_OK;
}
int TextInsertLine(Text *t,ULONG line)
{
    ULONG start,len;USHORT e;int rc;
    rc=TextLine(t,line,&start,&len,&e);if(rc)return rc;
    rc=TextInsert(t,start,t->eol==2?"\r\n":t->eol==3?"\r":"\n",t->eol==2?2:1);
    if(!rc)TextRecount(t);return rc;
}
int TextDeleteLine(Text *t,ULONG line)
{
    ULONG start,len,p,l;USHORT e,pe;int rc;
    rc=TextLine(t,line,&start,&len,&e);if(rc)return rc;
    if(!e && line){TextLine(t,line-1,&p,&l,&pe);start-=pe;len+=pe;}
    rc=TextDelete(t,start,len+e);if(!rc)TextRecount(t);return rc;
}
