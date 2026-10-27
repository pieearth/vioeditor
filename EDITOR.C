#define INCL_DOSFILEMGR
#define INCL_DOSMEMMGR
#define INCL_DOSPROCESS
#define INCL_DOSERRORS
#define INCL_VIO
#define INCL_KBD
#include <os2.h>
#include <string.h>
#include <stdio.h>

#define ATTR_MENU       0x70    /* Black on Light Gray */
#define ATTR_MENU_ACC   0x74    /* Red on Light Gray */
#define ATTR_MENU_SEL   0x0F    /* White on Black */
#define ATTR_CANVAS     0x17    /* White on Blue */
#define ATTR_STATUS     0x70    /* Black on Light Gray */
#define ATTR_SCROLL_BAR 0x78    /* Dark Gray on Light Gray (track) */
#define ATTR_SCROLL_THM 0x70    /* High-contrast Black on Light Gray */
#define ATTR_DIALOG     0x70    /* Black on Light Gray */
#define ATTR_BTN_SEL    0x0F    /* White on Black */

#include "TEXT.H"
/* Embedded compilation preserves BUILD EDITOR's single-source procedure. */
#define NP_TEXT_EMBEDDED
#include "TEXT.C"
#include "HISTORY.H"
#define NP_HISTORY_EMBEDDED
#include "HISTORY.C"

typedef struct {
    USHORT rows, cols, cur_row, cur_col;
    ULONG top_line, left_col, total_lines;
    ULONG cursor_line, cursor_col;
    Text text;
    History history;
    BOOL bModified;
    char szFilename[64];
} EditorState;

/* CUA Menu Command IDs */
#define CMD_NONE        0
#define CMD_FILE_NEW    1
#define CMD_FILE_OPEN   2
#define CMD_FILE_SAVE   3
#define CMD_FILE_SAVEAS 4
#define CMD_FILE_EXIT   5
#define CMD_EDIT_CLEAR  6
#define CMD_SEARCH_FIND 7
#define CMD_EDIT_UNDO 8
#define CMD_EDIT_REDO 9

typedef struct {
    char   *pszText;
    char    chAccel;
    USHORT  usCmd;
} MenuItem;

typedef struct {
    char     *pszTitle;
    char      chAccel;
    USHORT    usCol;
    USHORT    itemCount;
    MenuItem *items;
} MenuBarItem;

static MenuItem fileItems[] = {
    {" New         ", 'N', CMD_FILE_NEW},
    {" Open...     ", 'O', CMD_FILE_OPEN},
    {" Save      F5", 'S', CMD_FILE_SAVE},
    {" Save As...  ", 'A', CMD_FILE_SAVEAS},
    {" Exit     Esc", 'x', CMD_FILE_EXIT}
};

static MenuItem editItems[] = {
    {" Undo  Ctrl+Z", 'U', CMD_EDIT_UNDO},
    {" Redo  Ctrl+Y", 'R', CMD_EDIT_REDO},
    {" Clear  Del ", 'C', CMD_EDIT_CLEAR}
};

static MenuItem searchItems[] = {
    {" Find...    ", 'F', CMD_SEARCH_FIND}
};

static MenuItem optItems[] = {
    {" Wrap: Off ", 'W', CMD_NONE}
};

static MenuBarItem g_Menus[] = {
    {" File ",    'F', 1,  5, fileItems},
    {" Edit ",    'E', 8,  3, editItems},
    {" Search ",  'S', 15, 1, searchItems},
    {" Options ", 'O', 24, 1, optItems}
};
#define NUM_MENUS 4

/* Forward declarations */
static BOOL SaveFile(EditorState *ed);
static void CleanupMemory(EditorState *ed);
static void FileError(EditorState *ed, const char *message);
static void DrawDocument(EditorState *ed);
static void DrawScrollbar(const EditorState *ed);
static void DrawStatusBar(const EditorState *ed);
/* floor(value * steps / range), without a 32-bit product overflow. */
static USHORT ScaleScroll(ULONG value, ULONG range, USHORT steps)
{
    ULONG remainder=0; USHORT result=0, i;
    for(i=0;i<steps;i++) {
        remainder+=value;
        if(remainder>=range){remainder-=range;result++;}
    }
    return result;
}

static void DrawScrollbar(const EditorState *ed)
{
    USHORT track_len = (USHORT)(ed->rows - 4);
    USHORT visible_lines = (USHORT)(ed->rows - 2);
    USHORT thumb_size;
    USHORT thumb_pos = 0;
    ULONG max_top;
    USHORT i;
    USHORT scroll_col = (USHORT)(ed->cols - 1);

    if (ed->total_lines <= visible_lines) {
        thumb_size = track_len;
    } else {
        thumb_size = (USHORT)(((ULONG)visible_lines * track_len) / ed->total_lines);
        if (thumb_size < 1) thumb_size = 1;
    }

    max_top = (ed->total_lines > visible_lines) ? (ed->total_lines - visible_lines) : 0;
    if (max_top > 0) {
        thumb_pos = ScaleScroll(ed->top_line>max_top?max_top:ed->top_line,max_top,(USHORT)(track_len-thumb_size));
    }

    VioWrtCharStrAtt((PCH)"\x1E", 1, 1, scroll_col, (PBYTE)"\x70", 0);

    for (i = 0; i < track_len; i++) {
        if (i >= thumb_pos && i < (thumb_pos + thumb_size)) {
            VioWrtCharStrAtt((PCH)"\xDB", 1, (USHORT)(2 + i), scroll_col, (PBYTE)"\x70", 0);
        } else {
            VioWrtCharStrAtt((PCH)"\xB1", 1, (USHORT)(2 + i), scroll_col, (PBYTE)"\x78", 0);
        }
    }

    VioWrtCharStrAtt((PCH)"\x1F", 1, (USHORT)(ed->rows - 2), scroll_col, (PBYTE)"\x70", 0);
}

static void DrawMenuBar(const EditorState *ed, SHORT nActiveMenu)
{
    USHORT i;
    VioWrtNCell((PBYTE)"\x20\x70", ed->cols, 0, 0, 0);

    for (i = 0; i < NUM_MENUS; i++) {
        BYTE bAttr = (nActiveMenu == (SHORT)i) ? ATTR_MENU_SEL : ATTR_MENU;
        BYTE bAcc  = (nActiveMenu == (SHORT)i) ? ATTR_MENU_SEL : ATTR_MENU_ACC;
        USHORT len = (USHORT)strlen(g_Menus[i].pszTitle);

        VioWrtCharStrAtt((PCH)g_Menus[i].pszTitle, len, 0, g_Menus[i].usCol, &bAttr, 0);
        VioWrtCharStrAtt((PCH)&g_Menus[i].chAccel, 1, 0, (USHORT)(g_Menus[i].usCol + 1), &bAcc, 0);
    }

    if (ed->cols >= 45) {
        VioWrtCharStrAtt((PCH)"F1=Help ", 8, 0, (USHORT)(ed->cols - 8), (PBYTE)"\x70", 0);
    }
}

static void DrawStatusBar(const EditorState *ed)
{
    USHORT status_row = (USHORT)(ed->rows - 1);
    char szLeft[96];
    char szRight[64];
    ULONG curDocLine = ed->cursor_line + 1;
    ULONG curDocCol = ed->cursor_col + 1;

    VioWrtNCell((PBYTE)"\x20\x70", ed->cols, status_row, 0, 0);

    sprintf(szLeft, " [%s%s]  [%s] ", ed->szFilename, ed->bModified ? "*" : "", ed->text.eol==2?"DOS/CRLF":ed->text.eol==1?"LF":"CR");
    VioWrtCharStrAtt((PCH)szLeft, (USHORT)((strlen(szLeft) < (USHORT)(ed->cols - 1)) ? strlen(szLeft) : (USHORT)(ed->cols - 1)), status_row, 1, (PBYTE)"\x70", 0);

    sprintf(szRight, "Ln %lu/%lu, Col %lu \xB3 INS", (unsigned long)curDocLine, (unsigned long)ed->total_lines, (unsigned long)curDocCol);
    if (ed->cols > (USHORT)strlen(szRight) + 2) {
        VioWrtCharStrAtt((PCH)szRight, (USHORT)strlen(szRight), status_row, 
                         (USHORT)(ed->cols - (USHORT)strlen(szRight) - 2), (PBYTE)"\x70", 0);
    }
}

/* Write complete cells once per row/chunk instead of blanking then repainting. */
static void DrawTextRow(EditorState *ed, USHORT row, ULONG index)
{
    BYTE cells[256]; char chars[128];
    USHORT left,n,j,got,e; ULONG start,len;
    BOOL valid = TextLine(&ed->text,index,&start,&len,&e)==TE_OK;
    for(left=0;left<ed->cols-1;left+=n) {
        n=(USHORT)(ed->cols-1-left);if(n>128)n=128;got=0;
        if(valid && ed->left_col+left<len){
            ULONG available=len-ed->left_col-left;
            got=TextRead(&ed->text,start+ed->left_col+left,chars,
                         available<n?(USHORT)available:n);
        }
        for(j=0;j<n;j++){cells[j*2]=j<got?(BYTE)chars[j]:' ';cells[j*2+1]=ATTR_CANVAS;}
        VioWrtCellStr((PCH)cells,(USHORT)(n*2),row,left,0);
    }
}
static void DrawCurrentLine(EditorState *ed)
{ DrawTextRow(ed,ed->cur_row,ed->cursor_line); }
static void DrawDocument(EditorState *ed)
{
    USHORT r;
    for(r=1;r<ed->rows-1;r++)DrawTextRow(ed,r,ed->top_line+r-1);
}

static void FileError(EditorState *ed, const char *message)
{
    BYTE cells[160];
    USHORT width = ed->cols > 80 ? 80 : ed->cols;
    USHORT cb = (USHORT)(width * 2);
    USHORT len = (USHORT)strlen(message);
    KBDKEYINFO key;
    if (len > width) len = width;
    VioReadCellStr((PCH)cells, &cb, (USHORT)(ed->rows - 1), 0, 0);
    VioWrtNCell((PBYTE)"\x20\x70", width, (USHORT)(ed->rows - 1), 0, 0);
    VioWrtCharStrAtt((PCH)message, len, (USHORT)(ed->rows - 1), 0, (PBYTE)"\x70", 0);
    KbdCharIn(&key, IO_WAIT, 0);
    VioWrtCellStr((PCH)cells, cb, (USHORT)(ed->rows - 1), 0, 0);
}

static BOOL InitDocument(EditorState *ed,const char *path)
{
    if(strlen(path)>=sizeof(ed->szFilename))return FALSE;
    TextInit(&ed->text);HistoryInit(&ed->history);strcpy(ed->szFilename,path);
    ed->total_lines=1;ed->cursor_line=ed->cursor_col=0;
    ed->top_line=ed->left_col=0;ed->cur_row=1;ed->cur_col=0;ed->bModified=FALSE;
    return TRUE;
}
static BOOL LoadFile(EditorState *ed,const char *path)
{
    Text next;HFILE hf;USHORT action,rc;int result;
    if(strlen(path)>=sizeof(ed->szFilename)){FileError(ed,"Filename too long. Press a key.");return FALSE;}
    rc=DosOpen((PSZ)path,&hf,&action,0L,FILE_NORMAL,
        OPEN_ACTION_FAIL_IF_NEW|OPEN_ACTION_OPEN_IF_EXISTS,
        OPEN_ACCESS_READONLY|OPEN_SHARE_DENYWRITE,0L);
    if(rc){FileError(ed,"Cannot open file. Press a key.");return FALSE;}
    TextInit(&next);result=TextLoad(&next,hf);rc=DosClose(hf);
    if(result || rc){
        TextFree(&next);
        FileError(ed,result==TE_MEMORY?"Out of memory. Document retained. Press a key.":
                     result==TE_RANGE?"Document exceeds byte address limit. Press a key.":
                     "Read/close error. Document retained. Press a key.");
        return FALSE;
    }
    TextFree(&ed->text);HistoryFree(&ed->history);ed->text=next;strcpy(ed->szFilename,path);
    ed->total_lines=next.lines;ed->cursor_line=ed->cursor_col=0;
    ed->top_line=ed->left_col=0;ed->cur_row=1;ed->cur_col=0;ed->bModified=FALSE;
    return TRUE;
}

/* Reserve a sibling name without overwriting any pre-existing file. */
static BOOL ReserveFile(const char *path, char *name, HFILE *hf)
{
    char *base;
    const char *targetBase;
    USHORT n, action, rc;
    strcpy(name, path);
    base = name;
    for (n = 0; name[n]; n++)
        if (name[n] == '\\' || name[n] == '/' || name[n] == ':') base = name + n + 1;
    targetBase = path + (base - name);
    for (n = 0; n < 1000; n++) {
        sprintf(base, "NP%03u.TMP", n);
        if (!stricmp(base, targetBase)) continue;
        rc = DosOpen(name, hf, &action, 0L, FILE_NORMAL,
            OPEN_ACTION_CREATE_IF_NEW | OPEN_ACTION_FAIL_IF_EXISTS,
            OPEN_ACCESS_WRITEONLY | OPEN_SHARE_DENYREADWRITE, 0L);
        if (!rc) return TRUE;
        if (rc != ERROR_FILE_EXISTS && rc != ERROR_OPEN_FAILED) return FALSE;
    }
    return FALSE;
}

static BOOL SaveFile(EditorState *ed)
{
    HFILE hf, bh;
    USHORT rc;
    char temp[80], backup[80];
    BOOL ok = TRUE, hadOld = FALSE;
    HistoryBreak(&ed->history);
    if (!ReserveFile(ed->szFilename, temp, &hf)) {
        FileError(ed, "Cannot create save file. Press a key.");
        return FALSE;
    }
    ok = TextWrite(&ed->text,hf)==TE_OK;
    if (DosClose(hf)) ok = FALSE;
    if (!ok) {
        DosDelete(temp, 0L);
        FileError(ed, "Write/close failed. Original retained. Press a key.");
        return FALSE;
    }
    /* Move old target aside; failure to install new data must be recoverable. */
    if (!ReserveFile(ed->szFilename, backup, &bh)) {
        DosDelete(temp, 0L);
        FileError(ed, "Cannot reserve backup. Original retained. Press a key.");
        return FALSE;
    }
    rc = DosClose(bh);
    if (!rc) rc = DosDelete(backup, 0L);
    if (!rc) rc = DosMove(ed->szFilename, backup, 0L);
    if (!rc) hadOld = TRUE;
    else if (rc != ERROR_FILE_NOT_FOUND) {
        DosDelete(temp, 0L);
        FileError(ed, "Cannot back up target. Save failed. Press a key.");
        return FALSE;
    }
    rc = DosMove(temp, ed->szFilename, 0L);
    if (rc) {
        if (hadOld && DosMove(backup, ed->szFilename, 0L)) {
            FileError(ed, backup);
            FileError(ed, temp);
            FileError(ed, "Recovery files above retained. Save failed. Press a key.");
        } else {
            DosDelete(temp, 0L);
            FileError(ed, "Cannot install save. Original restored. Press a key.");
        }
        return FALSE;
    }
    if (hadOld && DosDelete(backup, 0L)) {
        FileError(ed, backup);
        FileError(ed, "Saved; old backup could not be removed. Press a key.");
    }
    HistorySaved(&ed->history);
    ed->bModified = FALSE;
    return TRUE;
}

static void SetPosition(EditorState *ed,ULONG line,ULONG col)
{
    ULONG start,len;USHORT ending;
    if(line>=ed->total_lines)line=ed->total_lines-1;
    TextLine(&ed->text,line,&start,&len,&ending);if(col>len)col=len;
    ed->cursor_line=line;ed->cursor_col=col;
    if(line<ed->top_line)ed->top_line=line;
    if(line>=ed->top_line+ed->rows-2)ed->top_line=line-(ed->rows-3);
    if(col<ed->left_col)ed->left_col=col;
    if(col>=ed->left_col+ed->cols-1)ed->left_col=col-(ed->cols-2);
    ed->cur_row=(USHORT)(line-ed->top_line+1);ed->cur_col=(USHORT)(col-ed->left_col);
}
static void Navigate(EditorState *ed,USHORT scan)
{
    ULONG line=ed->cursor_line,col=ed->cursor_col,start,len;USHORT e;
    HistoryBreak(&ed->history);
    TextLine(&ed->text,line,&start,&len,&e);
    switch(scan){
    case 0x47:col=0;break;
    case 0x4f:col=len;break;
    case 0x77:line=col=0;break;
    case 0x75:line=ed->total_lines-1;col=TEXT_MAX;break;
    case 0x48:if(line)line--;break;
    case 0x50:if(line+1<ed->total_lines)line++;break;
    case 0x4b:if(col)col--;else if(line){line--;col=TEXT_MAX;}break;
    case 0x4d:if(col<len)col++;else if(line+1<ed->total_lines){line++;col=0;}break;
    case 0x49:line=line>(ULONG)(ed->rows-2)?line-(ed->rows-2):0;break;
    case 0x51:line+=ed->rows-2;break;
    }
    SetPosition(ed,line,col);
}
static BOOL EditResult(EditorState *ed,int rc)
{
    if(rc){FileError(ed,rc==TE_MEMORY?"Out of memory. Operation cancelled. Press a key.":rc==HE_LIMIT?"History disabled by its limits. Edit cancelled. Press a key.":"Invalid or oversized edit. Press a key.");return FALSE;}
    TextRecount(&ed->text);ed->total_lines=ed->text.lines;ed->bModified=HistoryModified(&ed->history);return TRUE;
}
static int RecordEdit(EditorState *ed,ULONG at,const char far *data,
                      USHORT n,BOOL insert,BOOL typing,ULONG line,ULONG col)
{
    HistoryPos before,after;
    before.line=ed->cursor_line;before.col=ed->cursor_col;
    after.line=line;after.col=col;
    return HistoryEdit(&ed->history,&ed->text,at,data,n,insert,typing,before,after);
}
static BOOL UndoRedo(EditorState *ed,BOOL redo)
{
    HistoryPos pos;int rc=HistoryStep(&ed->history,&ed->text,redo,&pos);
    if(rc==HE_EMPTY)return FALSE;
    if(!EditResult(ed,rc))return FALSE;
    SetPosition(ed,pos.line,pos.col);
    return TRUE;
}
static void InsertChar(EditorState *ed,char ch)
{
    ULONG start,len;USHORT e;
    TextLine(&ed->text,ed->cursor_line,&start,&len,&e);
    if(EditResult(ed,RecordEdit(ed,start+ed->cursor_col,&ch,1,TRUE,
        (ch!=' ' && ch!='\t'),ed->cursor_line,ed->cursor_col+1)))
        SetPosition(ed,ed->cursor_line,ed->cursor_col+1);
}
static void InsertNewline(EditorState *ed)
{
    ULONG start,len;USHORT e;
    TextLine(&ed->text,ed->cursor_line,&start,&len,&e);
    if(EditResult(ed,RecordEdit(ed,start+ed->cursor_col,
        ed->text.eol==2?"\r\n":ed->text.eol==3?"\r":"\n",ed->text.eol==2?2:1,TRUE,FALSE,ed->cursor_line+1,0)))
        SetPosition(ed,ed->cursor_line+1,0);
}
static void DeleteCharBack(EditorState *ed)
{
    ULONG start,len,p,l,line=ed->cursor_line,col=ed->cursor_col;USHORT e,pe;
    TextLine(&ed->text,line,&start,&len,&e);
    if(col){if(EditResult(ed,RecordEdit(ed,start+col-1,NULL,1,FALSE,FALSE,line,col-1)))SetPosition(ed,line,col-1);}
    else if(line){
        TextLine(&ed->text,line-1,&p,&l,&pe);
        if(EditResult(ed,RecordEdit(ed,start-pe,NULL,pe,FALSE,FALSE,line-1,l)))SetPosition(ed,line-1,l);
    }
}
static void DeleteCharForward(EditorState *ed)
{
    ULONG start,len;USHORT e;
    TextLine(&ed->text,ed->cursor_line,&start,&len,&e);
    if(ed->cursor_col<len)EditResult(ed,RecordEdit(ed,start+ed->cursor_col,NULL,1,FALSE,FALSE,ed->cursor_line,ed->cursor_col));
    else if(e)EditResult(ed,RecordEdit(ed,start+len,NULL,e,FALSE,FALSE,ed->cursor_line,ed->cursor_col));
}

/* Save screen cells underneath popup dialogs/menus */
static void SaveScreenRect(USHORT top, USHORT left, USHORT rows, USHORT cols, PBYTE pBuf)
{
    USHORT r;
    USHORT cbRow = (USHORT)(cols * 2);
    for (r = 0; r < rows; r++) {
        USHORT cb = cbRow;
        VioReadCellStr((PCH)(pBuf + (r * cbRow)), &cb, (USHORT)(top + r), left, 0);
    }
}

/* Restore screen cells underneath popup dialogs/menus */
static void RestoreScreenRect(USHORT top, USHORT left, USHORT rows, USHORT cols, PBYTE pBuf)
{
    USHORT r;
    USHORT cbRow = (USHORT)(cols * 2);
    for (r = 0; r < rows; r++) {
        VioWrtCellStr((PCH)(pBuf + (r * cbRow)), cbRow, (USHORT)(top + r), left, 0);
    }
}

/* Render drop-down menu panel */
static void DrawDropdownMenu(USHORT top, USHORT left, MenuBarItem *pMenu, SHORT nSel)
{
    USHORT w = 15;
    USHORT h = (USHORT)(pMenu->itemCount + 2);
    USHORT i;
    char szBorder[18];

    /* Top border: ┌─────────────┐ */
    szBorder[0] = '\xDA';
    memset(szBorder + 1, '\xC4', w - 2);
    szBorder[w - 1] = '\xBF';
    VioWrtCharStrAtt((PCH)szBorder, w, top, left, (PBYTE)"\x70", 0);

    /* Menu options */
    for (i = 0; i < pMenu->itemCount; i++) {
        BYTE bAttr = (nSel == (SHORT)i) ? ATTR_MENU_SEL : ATTR_MENU;
        VioWrtCharStrAtt((PCH)"\xB3", 1, (USHORT)(top + 1 + i), left, (PBYTE)"\x70", 0);
        VioWrtNCell((PBYTE)"\x20\x70", (USHORT)(w - 2), (USHORT)(top + 1 + i), (USHORT)(left + 1), 0);
        VioWrtCharStrAtt((PCH)pMenu->items[i].pszText, (USHORT)strlen(pMenu->items[i].pszText), (USHORT)(top + 1 + i), (USHORT)(left + 1), &bAttr, 0);
        VioWrtCharStrAtt((PCH)"\xB3", 1, (USHORT)(top + 1 + i), (USHORT)(left + w - 1), (PBYTE)"\x70", 0);
    }

    /* Bottom border: └─────────────┘ */
    szBorder[0] = '\xC0';
    memset(szBorder + 1, '\xC4', w - 2);
    szBorder[w - 1] = '\xD9';
    VioWrtCharStrAtt((PCH)szBorder, w, (USHORT)(top + h - 1), left, (PBYTE)"\x70", 0);
}

/* Interactive CUA Menu Event Loop */
static USHORT RunMenu(EditorState *ed, SHORT nMenuIdx)
{
    BYTE cellBuf[20 * 20 * 2];
    SHORT nCurMenu = nMenuIdx;
    SHORT nCurItem = 0;
    KBDKEYINFO kki;
    USHORT usW = 15;
    USHORT usH;
    USHORT usTop = 1;
    USHORT usLeft;

    HistoryBreak(&ed->history);
    while (1) {
        usH = (USHORT)(g_Menus[nCurMenu].itemCount + 2);
        usLeft = g_Menus[nCurMenu].usCol;

        DrawMenuBar(ed, nCurMenu);
        SaveScreenRect(usTop, usLeft, usH, usW, cellBuf);
        DrawDropdownMenu(usTop, usLeft, &g_Menus[nCurMenu], nCurItem);

        KbdCharIn(&kki, IO_WAIT, 0);

        RestoreScreenRect(usTop, usLeft, usH, usW, cellBuf);

        if (kki.chChar == 0 || (UCHAR)kki.chChar == 0xE0) {
            switch (kki.chScan) {
                case 0x44: /* F10: dismisses menu cleanly */
                    DrawMenuBar(ed, -1);
                    return CMD_NONE;

                case 0x4B: /* Left Arrow */
                    nCurMenu = (nCurMenu > 0) ? (nCurMenu - 1) : (SHORT)(NUM_MENUS - 1);
                    nCurItem = 0;
                    break;

                case 0x4D: /* Right Arrow */
                    nCurMenu = (SHORT)(((USHORT)(nCurMenu + 1) < NUM_MENUS) ? (nCurMenu + 1) : 0);
                    nCurItem = 0;
                    break;

                case 0x48: /* Up Arrow */
                    nCurItem = (nCurItem > 0) ? (nCurItem - 1) : (SHORT)(g_Menus[nCurMenu].itemCount - 1);
                    break;

                case 0x50: /* Down Arrow */
                    nCurItem = ((USHORT)(nCurItem + 1) < g_Menus[nCurMenu].itemCount) ? (nCurItem + 1) : 0;
                    break;
            }
        } else if (kki.chChar == '\r') {
            DrawMenuBar(ed, -1);
            return g_Menus[nCurMenu].items[nCurItem].usCmd;
        } else if (kki.chChar == 0x1B) {
            break;
        }
    }

    DrawMenuBar(ed, -1);
    return CMD_NONE;
}

/* Modal CUA "Save changes?" prompt (Returns: 1 = Save, 0 = Discard/Proceed, -1 = Cancel) */
static SHORT ShowSavePrompt(EditorState *ed)
{
    BYTE dlgBuf[7 * 42 * 2];
    USHORT dW = 40;
    USHORT dH = 7;
    USHORT dTop = (USHORT)(((SHORT)ed->rows - (SHORT)dH) / 2);
    USHORT dLeft = (USHORT)(((SHORT)ed->cols - (SHORT)dW) / 2);
    SHORT nFocusBtn = 0; /* 0 = Yes, 1 = No */
    KBDKEYINFO kki;
    char szBorder[44];
    USHORT r;

    SaveScreenRect(dTop, dLeft, dH, dW, dlgBuf);

    while (1) {
        szBorder[0] = '\xC9'; /* ╔ */
        memset(szBorder + 1, '\xCD', dW - 2);
        szBorder[dW - 1] = '\xBB'; /* ╗ */
        VioWrtCharStrAtt((PCH)szBorder, dW, dTop, dLeft, (PBYTE)"\x70", 0);
        VioWrtCharStrAtt((PCH)" Save Changes? ", 15, dTop, (USHORT)(dLeft + 2), (PBYTE)"\x70", 0);

        for (r = 1; r < dH - 1; r++) {
            VioWrtCharStrAtt((PCH)"\xBA", 1, (USHORT)(dTop + r), dLeft, (PBYTE)"\x70", 0);
            VioWrtNCell((PBYTE)"\x20\x70", (USHORT)(dW - 2), (USHORT)(dTop + r), (USHORT)(dLeft + 1), 0);
            VioWrtCharStrAtt((PCH)"\xBA", 1, (USHORT)(dTop + r), (USHORT)(dLeft + dW - 1), (PBYTE)"\x70", 0);
        }

        szBorder[0] = '\xC8'; /* ╚ */
        memset(szBorder + 1, '\xCD', dW - 2);
        szBorder[dW - 1] = '\xBC'; /* ╝ */
        VioWrtCharStrAtt((PCH)szBorder, dW, (USHORT)(dTop + dH - 1), dLeft, (PBYTE)"\x70", 0);

        VioWrtCharStrAtt((PCH)"File has been modified.", 23, (USHORT)(dTop + 2), (USHORT)(dLeft + 3), (PBYTE)"\x70", 0);
        VioWrtCharStrAtt((PCH)"Save changes before proceeding?", 31, (USHORT)(dTop + 3), (USHORT)(dLeft + 3), (PBYTE)"\x70", 0);

        {
            BYTE bYesAttr = (nFocusBtn == 0) ? ATTR_BTN_SEL : ATTR_DIALOG;
            BYTE bNoAttr  = (nFocusBtn == 1) ? ATTR_BTN_SEL : ATTR_DIALOG;

            VioWrtCharStrAtt((PCH)"< Yes >", 7, (USHORT)(dTop + 5), (USHORT)(dLeft + 10), &bYesAttr, 0);
            VioWrtCharStrAtt((PCH)"< No >",  6, (USHORT)(dTop + 5), (USHORT)(dLeft + 22), &bNoAttr,  0);
        }

        KbdCharIn(&kki, IO_WAIT, 0);

        if (kki.chChar == 0 || (UCHAR)kki.chChar == 0xE0) {
            switch (kki.chScan) {
                case 0x4B: /* Left */
                case 0x4D: /* Right */
                    nFocusBtn = !nFocusBtn;
                    break;
            }
        } else if (kki.chChar == '\t') {
            nFocusBtn = !nFocusBtn;
        } else if (kki.chChar == 'y' || kki.chChar == 'Y') {
            RestoreScreenRect(dTop, dLeft, dH, dW, dlgBuf);
            return 1;
        } else if (kki.chChar == 'n' || kki.chChar == 'N') {
            RestoreScreenRect(dTop, dLeft, dH, dW, dlgBuf);
            return 0;
        } else if (kki.chChar == '\r') {
            RestoreScreenRect(dTop, dLeft, dH, dW, dlgBuf);
            return (nFocusBtn == 0) ? 1 : 0;
        } else if (kki.chChar == 0x1B) {
            RestoreScreenRect(dTop, dLeft, dH, dW, dlgBuf);
            return -1;
        }
    }
}

/* Generic CUA Text Entry Dialog: returns TRUE if user hit Enter, FALSE on Esc */
static BOOL PromptFilename(EditorState *ed, const char *pszTitle, char *pszBuffer, USHORT maxLen)
{
    BYTE dlgBuf[7 * 50 * 2];
    USHORT dW = 46;
    USHORT dH = 7;
    USHORT dTop = (USHORT)(((SHORT)ed->rows - (SHORT)dH) / 2);
    USHORT dLeft = (USHORT)(((SHORT)ed->cols - (SHORT)dW) / 2);
    USHORT fieldLen = 38;
    USHORT curLen = (USHORT)strlen(pszBuffer);
    USHORT curPos = curLen;
    USHORT fieldLeft = 0;
    KBDKEYINFO kki;
    char szBorder[50];
    char szField[40];
    USHORT r;
    BOOL bResult = FALSE;

    SaveScreenRect(dTop, dLeft, dH, dW, dlgBuf);

    while (1) {
        /* Draw double-line border */
        szBorder[0] = '\xC9'; /* ╔ */
        memset(szBorder + 1, '\xCD', dW - 2);
        szBorder[dW - 1] = '\xBB'; /* ╗ */
        VioWrtCharStrAtt((PCH)szBorder, dW, dTop, dLeft, (PBYTE)"\x70", 0);

        /* Title */
        sprintf(szBorder, " %s ", pszTitle);
        VioWrtCharStrAtt((PCH)szBorder, (USHORT)strlen(szBorder), dTop, (USHORT)(dLeft + 2), (PBYTE)"\x70", 0);

        for (r = 1; r < dH - 1; r++) {
            VioWrtCharStrAtt((PCH)"\xBA", 1, (USHORT)(dTop + r), dLeft, (PBYTE)"\x70", 0);
            VioWrtNCell((PBYTE)"\x20\x70", (USHORT)(dW - 2), (USHORT)(dTop + r), (USHORT)(dLeft + 1), 0);
            VioWrtCharStrAtt((PCH)"\xBA", 1, (USHORT)(dTop + r), (USHORT)(dLeft + dW - 1), (PBYTE)"\x70", 0);
        }

        szBorder[0] = '\xC8'; /* ╚ */
        memset(szBorder + 1, '\xCD', dW - 2);
        szBorder[dW - 1] = '\xBC'; /* ╝ */
        VioWrtCharStrAtt((PCH)szBorder, dW, (USHORT)(dTop + dH - 1), dLeft, (PBYTE)"\x70", 0);

        /* Label */
        VioWrtCharStrAtt((PCH)"Filename:", 9, (USHORT)(dTop + 2), (USHORT)(dLeft + 4), (PBYTE)"\x70", 0);

        /* Input field background */
        if (curPos < fieldLeft) fieldLeft = curPos;
        if (curPos >= fieldLeft + fieldLen) fieldLeft = (USHORT)(curPos - fieldLen + 1);
        memset(szField, ' ', fieldLen);
        if (curLen > 0) {
            memcpy(szField, pszBuffer + fieldLeft,
                   (curLen - fieldLeft > fieldLen) ? fieldLen : curLen - fieldLeft);
        }
        VioWrtCharStrAtt((PCH)"[", 1, (USHORT)(dTop + 3), (USHORT)(dLeft + 3), (PBYTE)"\x70", 0);
        VioWrtCharStrAtt((PCH)szField, fieldLen, (USHORT)(dTop + 3), (USHORT)(dLeft + 4), (PBYTE)"\x0F", 0);
        VioWrtCharStrAtt((PCH)"]", 1, (USHORT)(dTop + 3), (USHORT)(dLeft + 4 + fieldLen), (PBYTE)"\x70", 0);

        /* Pushbuttons */
        VioWrtCharStrAtt((PCH)"< OK >", 6, (USHORT)(dTop + 5), (USHORT)(dLeft + 12), (PBYTE)"\x0F", 0);
        VioWrtCharStrAtt((PCH)"< Cancel >", 10, (USHORT)(dTop + 5), (USHORT)(dLeft + 24), (PBYTE)"\x70", 0);

        VioSetCurPos((USHORT)(dTop + 3), (USHORT)(dLeft + 4 + curPos - fieldLeft), 0);

        KbdCharIn(&kki, IO_WAIT, 0);

        if (kki.chChar == '\r') {
            bResult = TRUE;
            break;
        } else if (kki.chChar == 0x1B) {
            bResult = FALSE;
            break;
        } else if (kki.chChar == '\b') {
            if (curPos > 0) {
                USHORT i;
                for (i = (USHORT)(curPos - 1); i < curLen; i++) {
                    pszBuffer[i] = pszBuffer[i + 1];
                }
                curLen--;
                curPos--;
                pszBuffer[curLen] = '\0';
            }
        } else if (kki.chChar == 0 || (UCHAR)kki.chChar == 0xE0) {
            switch (kki.chScan) {
                case 0x4B: /* Left */
                    if (curPos > 0) curPos--;
                    break;
                case 0x4D: /* Right */
                    if (curPos < curLen) curPos++;
                    break;
            }
        } else if ((UCHAR)kki.chChar >= 32 && (UCHAR)kki.chChar != 127) {
            if (curLen < maxLen - 1) {
                USHORT i;
                for (i = curLen; i > curPos; i--) {
                    pszBuffer[i] = pszBuffer[i - 1];
                }
                pszBuffer[curPos] = kki.chChar;
                curLen++;
                curPos++;
                pszBuffer[curLen] = '\0';
            }
        }
    }

    RestoreScreenRect(dTop, dLeft, dH, dW, dlgBuf);
    return bResult;
}

static void DoFileSaveAs(EditorState *ed)
{
    char szNewPath[64];
    char oldPath[64];
    strcpy(szNewPath, ed->szFilename);

    if (PromptFilename(ed, "Save As", szNewPath, sizeof(szNewPath))) {
        if (strlen(szNewPath) > 0) {
            strcpy(oldPath, ed->szFilename);
            strcpy(ed->szFilename, szNewPath);
            if (!SaveFile(ed)) strcpy(ed->szFilename, oldPath);
            DrawStatusBar(ed);
        }
    }
}

static void DoFileOpen(EditorState *ed)
{
    char szOpenPath[64];
    szOpenPath[0] = '\0';

    if (ed->bModified) {
        SHORT nRes = ShowSavePrompt(ed);
        if (nRes == 1) {
            if (!SaveFile(ed)) return;
        } else if (nRes == -1) {
            return;
        }
    }

    if (PromptFilename(ed, "Open File", szOpenPath, sizeof(szOpenPath))) {
        if (strlen(szOpenPath) > 0) {
            if (!LoadFile(ed, szOpenPath)) return;

            DrawDocument(ed);
            DrawScrollbar(ed);
            DrawStatusBar(ed);
        }
    }
}

static void DoFileNew(EditorState *ed)
{
    if (ed->bModified) {
        SHORT nRes = ShowSavePrompt(ed);
        if (nRes == 1) {
            if (!SaveFile(ed)) return;
        } else if (nRes == -1) {
            return;
        }
    }

    TextFree(&ed->text);
    HistoryFree(&ed->history);
    InitDocument(ed,"UNTITLED.TXT");

    DrawDocument(ed);
    DrawScrollbar(ed);
    DrawStatusBar(ed);
    VioSetCurPos(ed->cur_row, ed->cur_col, 0);
}

static void CleanupMemory(EditorState *ed)
{ HistoryFree(&ed->history);TextFree(&ed->text); }

int main(int argc, char *argv[])
{
    VIOMODEINFO vmi;
    KBDKEYINFO kki;
    EditorState ed;
    BOOL bRunning = TRUE;
    ULONG old_line_count, old_top, old_left;
    USHORT old_row, old_col;
    BOOL old_modified, changedLine;
    USHORT usCmd;
    SHORT nRes;
    const char *pszTarget = (argc > 1) ? argv[1] : "NEWFILE.TXT";

    vmi.cb = sizeof(vmi);
    if (VioGetMode(&vmi, 0) || vmi.row < 10 || vmi.col < 46) {
        printf("NP16 requires at least 46 columns and 10 rows.\n");
        return 1;
    }

    memset(&ed, 0, sizeof(ed));
    ed.rows = vmi.row;
    ed.cols = vmi.col;
    ed.cur_row = 1;
    ed.cur_col = 0;
    ed.top_line = 0;
    ed.left_col = 0;
    ed.bModified = FALSE;

    if (!InitDocument(&ed, "NEWFILE.TXT")) {
        CleanupMemory(&ed);
        printf("Cannot allocate initial document.\n");
        return 1;
    }
    ed.total_lines = 1;
    if (argc > 1 && !LoadFile(&ed, pszTarget)) {
        CleanupMemory(&ed);
        return 1;
    }

    DrawMenuBar(&ed, -1);
    DrawDocument(&ed);
    DrawScrollbar(&ed);
    DrawStatusBar(&ed);
    VioSetCurPos(ed.cur_row, ed.cur_col, 0);

    while (bRunning) {
        if (KbdCharIn(&kki, IO_WAIT, 0)) break;
        old_line_count = ed.total_lines;
        old_top = ed.top_line; old_left = ed.left_col;
        old_row = ed.cur_row; old_col = ed.cur_col;
        old_modified = ed.bModified;
        changedLine = FALSE;
        usCmd=CMD_NONE;
        if(kki.chChar<32 || kki.chChar==0xE0)HistoryBreak(&ed.history);

        if (kki.chChar == 0 || (UCHAR)kki.chChar == 0xE0) {
            switch (kki.chScan) {
                case 0x77: case 0x75:
                case 0x47: case 0x4F: case 0x48: case 0x50:
                case 0x4B: case 0x4D: case 0x49: case 0x51:
                    Navigate(&ed, kki.chScan);
                    break;
                case 0x53:
                    DeleteCharForward(&ed);
                    changedLine = TRUE;
                    break;
                case 0x3F: /* F5: Save File */
                    SaveFile(&ed);
                    break;
                case 0x44: /* F10: Action Bar */
                case 0x21: /* Alt+F: File Menu */
                    usCmd = RunMenu(&ed, 0);
                    if (usCmd == CMD_FILE_NEW) {
                        DoFileNew(&ed);
                    } else if (usCmd == CMD_FILE_OPEN) {
                        DoFileOpen(&ed);
                    } else if (usCmd == CMD_FILE_SAVE) {
                        SaveFile(&ed);
                    } else if (usCmd == CMD_FILE_SAVEAS) {
                        DoFileSaveAs(&ed);
                    } else if (usCmd == CMD_FILE_EXIT) {
                        if (ed.bModified) {
                            nRes = ShowSavePrompt(&ed);
                            if (nRes == 1) {
                                if (SaveFile(&ed)) bRunning = FALSE;
                            } else if (nRes == 0) {
                                bRunning = FALSE;
                            }
                        } else {
                            bRunning = FALSE;
                        }
                    }
                    break;
                case 0x12: /* Alt+E: Edit Menu */
                    usCmd = RunMenu(&ed, 1);
                    if (usCmd == CMD_EDIT_CLEAR) {
                        DeleteCharForward(&ed);
                        changedLine = TRUE;
                    }
                    break;
                case 0x1F: /* Alt+S: Search Menu */
                    usCmd = RunMenu(&ed, 2);
                    break;
                case 0x18: /* Alt+O: Options Menu */
                    usCmd = RunMenu(&ed, 3);
                    break;
            }
        } else if (kki.chChar == 26 || kki.chChar == 25) {
            changedLine=UndoRedo(&ed,kki.chChar==25);
        } else if (kki.chChar == 0x1B) {
            if (ed.bModified) {
                nRes = ShowSavePrompt(&ed);
                if (nRes == 1) {
                    if (SaveFile(&ed)) bRunning = FALSE;
                } else if (nRes == 0) {
                    bRunning = FALSE;
                }
            } else {
                bRunning = FALSE;
            }
        } else if (kki.chChar == '\r') {
            InsertNewline(&ed);
            changedLine = TRUE;
        } else if (kki.chChar == '\b') {
            DeleteCharBack(&ed);
            changedLine = TRUE;
        } else if ((UCHAR)kki.chChar >= 32 && (UCHAR)kki.chChar != 127) {
            InsertChar(&ed, kki.chChar);
            changedLine = TRUE;
        }

        if(usCmd==CMD_EDIT_UNDO || usCmd==CMD_EDIT_REDO)
            changedLine=UndoRedo(&ed,usCmd==CMD_EDIT_REDO);

        if (old_top != ed.top_line || old_left != ed.left_col ||
            old_line_count != ed.total_lines) DrawDocument(&ed);
        else if (changedLine) {
            if(old_row!=ed.cur_row)DrawTextRow(&ed,old_row,ed.top_line+old_row-1);
            DrawCurrentLine(&ed);
        }
        if (old_top != ed.top_line || old_line_count != ed.total_lines)
            DrawScrollbar(&ed);
        if (changedLine || old_modified != ed.bModified || old_top != ed.top_line ||
            old_left != ed.left_col || old_row != ed.cur_row || old_col != ed.cur_col)
            DrawStatusBar(&ed);
        VioSetCurPos(ed.cur_row, ed.cur_col, 0);
    }

    CleanupMemory(&ed);
    VioScrollUp(0, 0, (USHORT)(ed.rows - 1), (USHORT)(ed.cols - 1), 0xFFFF, (PBYTE)"\x20\x07", 0);
    VioSetCurPos(0, 0, 0);

    return 0;
}