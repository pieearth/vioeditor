#ifndef NP_TEXT_H
#define NP_TEXT_H
/* Include os2.h first. All positions/counts are byte-based ULONG values. */
#define TEXT_BLOCK 4096
#define TEXT_MAX 0x7fffffffUL
#define TE_OK 0
#define TE_MEMORY 1
#define TE_RANGE 2
#define TE_IO 3
typedef struct { SEL next; USHORT used; char data[TEXT_BLOCK]; } TextBlock;
typedef struct { SEL head, tail; ULONG bytes, lines; USHORT eol; } Text;
typedef struct { SEL sel; USHORT pos; ULONG offset; } TextIter;
void TextInit(Text *t);
void TextFree(Text *t);
int TextInsert(Text *t, ULONG at, const char far *s, USHORT n);
int TextDelete(Text *t, ULONG at, ULONG n);
int TextLine(Text *t, ULONG line, ULONG *start, ULONG *len, USHORT *ending);
USHORT TextRead(Text *t, ULONG at, char *buf, USHORT n);
void TextRecount(Text *t);
int TextLoad(Text *t, HFILE hf);
int TextWrite(Text *t, HFILE hf);
int TextInsertLine(Text *t, ULONG line);
int TextDeleteLine(Text *t, ULONG line);
#endif
