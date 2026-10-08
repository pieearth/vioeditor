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

#define MAX_LINES_LIMIT 1024
#define MAX_LINE_LEN    254

typedef struct {
    SEL    sel;
    USHORT len;
} LineDesc;

typedef struct {
    USHORT   rows;
    USHORT   cols;
    USHORT   cur_row;       /* Physical screen row (1..rows-2) */
    USHORT   cur_col;       /* Physical screen col (0..cols-2) */
    USHORT   top_line;      /* Document line index at top */
    USHORT   left_col;      /* Horizontal scroll column */
    USHORT   total_lines;
    BOOL     bModified;
    char     szFilename[64];
    SEL      selTable;      /* Memory selector for line descriptors */
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
    {" Edit ",    'E', 8,  1, editItems},
    {" Search ",  'S', 15, 1, searchItems},
    {" Options ", 'O', 24, 1, optItems}
};
#define NUM_MENUS 4

/* Forward declarations */
static void SaveFile(EditorState *ed);
static void DrawDocument(EditorState *ed);
static void DrawScrollbar(const EditorState *ed);
static void DrawStatusBar(const EditorState *ed);
static void AllocLineWithText(LineDesc far *pLine, const char far *pSrc, USHORT len);
static void FreeLine(LineDesc far *pLine);

static LineDesc far *GetLine(EditorState *ed, USHORT idx)
{
    LineDesc far *pTable = (LineDesc far *)MAKEP(ed->selTable, 0);
    return &pTable[idx];
}

static char far *GetLineText(LineDesc far *pLine)
{
    if (pLine->sel == 0) return NULL;
    return (char far *)MAKEP(pLine->sel, 0);
}

static void DrawScrollbar(const EditorState *ed)
{
    USHORT track_len = (USHORT)(ed->rows - 3);
    USHORT visible_lines = (USHORT)(ed->rows - 2);
    USHORT thumb_size;
    USHORT thumb_pos = 0;
    USHORT max_top;
    USHORT i;
    USHORT scroll_col = (USHORT)(ed->cols - 1);

    if (ed->total_lines <= visible_lines) {
        thumb_size = track_len;
    } else {
        thumb_size = (USHORT)(((ULONG)visible_lines * track_len) / ed->total_lines);
        if (thumb_size < 1) thumb_size = 1;
    }

    max_top = (ed->total_lines > visible_lines) ? (USHORT)(ed->total_lines - visible_lines) : 0;
    if (max_top > 0) {
        thumb_pos = (USHORT)(((ULONG)ed->top_line * (track_len - thumb_size)) / max_top);
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
    char szLeft[80];
    char szRight[64];
    USHORT curDocLine = (USHORT)(ed->top_line + (ed->cur_row - 1) + 1);
    USHORT curDocCol  = (USHORT)(ed->left_col + ed->cur_col + 1);

    VioWrtNCell((PBYTE)"\x20\x70", ed->cols, status_row, 0, 0);

    sprintf(szLeft, " [%s%s]  [DOS/CRLF] ", ed->szFilename, ed->bModified ? "*" : "");
    VioWrtCharStrAtt((PCH)szLeft, (USHORT)strlen(szLeft), status_row, 1, (PBYTE)"\x70", 0);

    sprintf(szRight, "Ln %u/%u, Col %u \xB3 INS", curDocLine, ed->total_lines, curDocCol);
    if (ed->cols > (USHORT)strlen(szRight) + 2) {
        VioWrtCharStrAtt((PCH)szRight, (USHORT)strlen(szRight), status_row, 
                         (USHORT)(ed->cols - (USHORT)strlen(szRight) - 2), (PBYTE)"\x70", 0);
    }
}

static void DrawCurrentLine(EditorState *ed)
{
    USHORT canvas_width = (USHORT)(ed->cols - 1);
    USHORT line_idx = (USHORT)(ed->top_line + (ed->cur_row - 1));
    char szLineBuf[256];

    VioWrtNCell((PBYTE)"\x20\x17", canvas_width, ed->cur_row, 0, 0);

    if (line_idx < ed->total_lines) {
        LineDesc far *pLine = GetLine(ed, line_idx);
        if (pLine->len > ed->left_col) {
            USHORT avail = (USHORT)(pLine->len - ed->left_col);
            USHORT count = (avail > canvas_width) ? canvas_width : avail;
            char far *pText = GetLineText(pLine);

            if (pText != NULL) {
                _fmemcpy(szLineBuf, pText + ed->left_col, count);
                VioWrtCharStrAtt((PCH)szLineBuf, count, ed->cur_row, 0, (PBYTE)"\x17", 0);
            }
        }
    }
}

static void DrawDocument(EditorState *ed)
{
    USHORT visible_rows = (USHORT)(ed->rows - 2);
    USHORT canvas_width = (USHORT)(ed->cols - 1);
    USHORT r;
    char szLineBuf[256];

    for (r = 0; r < visible_rows; r++) {
        USHORT line_idx = (USHORT)(ed->top_line + r);
        USHORT screen_row = (USHORT)(1 + r);

        VioWrtNCell((PBYTE)"\x20\x17", canvas_width, screen_row, 0, 0);

        if (line_idx < ed->total_lines) {
            LineDesc far *pLine = GetLine(ed, line_idx);
            if (pLine->len > ed->left_col) {
                USHORT avail = (USHORT)(pLine->len - ed->left_col);
                USHORT count = (avail > canvas_width) ? canvas_width : avail;
                char far *pText = GetLineText(pLine);

                if (pText != NULL) {
                    _fmemcpy(szLineBuf, pText + ed->left_col, count);
                    VioWrtCharStrAtt((PCH)szLineBuf, count, screen_row, 0, (PBYTE)"\x17", 0);
                }
            }
        }
    }
}

static void AllocLineWithText(LineDesc far *pLine, const char far *pSrc, USHORT len)
{
    char far *pDest;
    pLine->len = len;
    DosAllocSeg(MAX_LINE_LEN + 1, &pLine->sel, 0);
    pDest = (char far *)MAKEP(pLine->sel, 0);
    if (len > 0 && pSrc != NULL) {
        _fmemcpy(pDest, pSrc, len);
    }
    pDest[len] = '\0';
}

static void FreeLine(LineDesc far *pLine)
{
    if (pLine->sel != 0) {
        DosFreeSeg(pLine->sel);
        pLine->sel = 0;
        pLine->len = 0;
    }
}

static void LoadFile(EditorState *ed, const char *pszPath)
{
    HFILE hf;
    USHORT usAction;
    USHORT usRc;
    ULONG  cbFile;
    USHORT cbRead;
    SEL    selRaw;
    char far *pRawText;
    ULONG i;
    ULONG lineStart = 0;

    strcpy(ed->szFilename, pszPath);
    DosAllocSeg((USHORT)(sizeof(LineDesc) * MAX_LINES_LIMIT), &ed->selTable, 0);

    usRc = DosOpen((PSZ)pszPath, &hf, &usAction, 0L, FILE_NORMAL,
                   OPEN_ACTION_FAIL_IF_NEW | OPEN_ACTION_OPEN_IF_EXISTS,
                   OPEN_ACCESS_READONLY | OPEN_SHARE_DENYNONE, 0L);

    if (usRc != 0) {
        LineDesc far *pLine = GetLine(ed, 0);
        AllocLineWithText(pLine, NULL, 0);
        ed->total_lines = 1;
        return;
    }

    DosChgFilePtr(hf, 0L, FILE_END, &cbFile);
    DosChgFilePtr(hf, 0L, FILE_BEGIN, &lineStart);

    DosAllocSeg((USHORT)cbFile + 1, &selRaw, 0);
    pRawText = (char far *)MAKEP(selRaw, 0);

    DosRead(hf, pRawText, (USHORT)cbFile, &cbRead);
    DosClose(hf);
    pRawText[cbRead] = '\0';

    ed->total_lines = 0;
    lineStart = 0;

    for (i = 0; i <= cbRead && ed->total_lines < MAX_LINES_LIMIT; i++) {
        if (i == cbRead || pRawText[i] == '\n') {
            ULONG len = i - lineStart;
            LineDesc far *pLine = GetLine(ed, ed->total_lines);

            if (len > 0 && pRawText[lineStart + len - 1] == '\r') {
                len--;
            }
            if (len > MAX_LINE_LEN) {
                len = MAX_LINE_LEN;
            }

            AllocLineWithText(pLine, pRawText + lineStart, (USHORT)len);
            ed->total_lines++;
            lineStart = i + 1;
        }
    }

    DosFreeSeg(selRaw);

    if (ed->total_lines == 0) {
        LineDesc far *pLine = GetLine(ed, 0);
        AllocLineWithText(pLine, NULL, 0);
        ed->total_lines = 1;
    }
}

static void SaveFile(EditorState *ed)
{
    HFILE hf;
    USHORT usAction;
    USHORT usRc;
    USHORT i;
    USHORT cbWritten;

    usRc = DosOpen((PSZ)ed->szFilename, &hf, &usAction, 0L, FILE_NORMAL,
                   OPEN_ACTION_CREATE_IF_NEW | OPEN_ACTION_REPLACE_IF_EXISTS,
                   OPEN_ACCESS_WRITEONLY | OPEN_SHARE_DENYREADWRITE, 0L);

    if (usRc != 0) return;

    for (i = 0; i < ed->total_lines; i++) {
        LineDesc far *pLine = GetLine(ed, i);
        char far *pText = GetLineText(pLine);

        if (pLine->len > 0 && pText != NULL) {
            DosWrite(hf, pText, pLine->len, &cbWritten);
        }
        DosWrite(hf, "\r\n", 2, &cbWritten);
    }

    DosClose(hf);
    ed->bModified = FALSE;
}

static void InsertChar(EditorState *ed, char ch)
{
    USHORT line_idx = (USHORT)(ed->top_line + (ed->cur_row - 1));
    USHORT col_idx = (USHORT)(ed->left_col + ed->cur_col);
    LineDesc far *pLine = GetLine(ed, line_idx);
    char far *pText = GetLineText(pLine);
    USHORT i;

    if (pLine->len >= MAX_LINE_LEN) return;

    for (i = pLine->len; i > col_idx; i--) {
        pText[i] = pText[i - 1];
    }
    pText[col_idx] = ch;
    pLine->len++;
    pText[pLine->len] = '\0';

    ed->bModified = TRUE;

    if (ed->cur_col < ed->cols - 2) {
        ed->cur_col++;
    } else {
        ed->left_col++;
    }
}

static void InsertNewline(EditorState *ed)
{
    USHORT line_idx = (USHORT)(ed->top_line + (ed->cur_row - 1));
    USHORT col_idx = (USHORT)(ed->left_col + ed->cur_col);
    USHORT i;
    LineDesc far *pCur;
    LineDesc far *pNext;
    char far *pCurText;

    if (ed->total_lines >= MAX_LINES_LIMIT) return;

    for (i = ed->total_lines; i > line_idx + 1; i--) {
        LineDesc far *pSrc = GetLine(ed, (USHORT)(i - 1));
        LineDesc far *pDst = GetLine(ed, i);
        *pDst = *pSrc;
    }

    pCur = GetLine(ed, line_idx);
    pNext = GetLine(ed, (USHORT)(line_idx + 1));
    pCurText = GetLineText(pCur);

    AllocLineWithText(pNext, pCurText + col_idx, (USHORT)(pCur->len - col_idx));

    pCur->len = col_idx;
    pCurText[col_idx] = '\0';

    ed->total_lines++;
    ed->bModified = TRUE;

    ed->cur_col = 0;
    ed->left_col = 0;
    if (ed->cur_row < ed->rows - 2) {
        ed->cur_row++;
    } else {
        ed->top_line++;
    }
}

static void DeleteCharBack(EditorState *ed)
{
    USHORT line_idx = (USHORT)(ed->top_line + (ed->cur_row - 1));
    USHORT col_idx = (USHORT)(ed->left_col + ed->cur_col);
    LineDesc far *pLine;
    char far *pText;
    USHORT i;

    if (col_idx > 0) {
        pLine = GetLine(ed, line_idx);
        pText = GetLineText(pLine);

        for (i = (USHORT)(col_idx - 1); i < pLine->len; i++) {
            pText[i] = pText[i + 1];
        }
        pLine->len--;
        ed->bModified = TRUE;

        if (ed->cur_col > 0) {
            ed->cur_col--;
        } else if (ed->left_col > 0) {
            ed->left_col--;
        }
    } else if (line_idx > 0) {
        LineDesc far *pPrev = GetLine(ed, (USHORT)(line_idx - 1));
        char far *pPrevText = GetLineText(pPrev);
        pLine = GetLine(ed, line_idx);
        pText = GetLineText(pLine);

        if ((USHORT)(pPrev->len + pLine->len) <= MAX_LINE_LEN) {
            USHORT old_prev_len = pPrev->len;
            _fmemcpy(pPrevText + old_prev_len, pText, pLine->len);
            pPrev->len += pLine->len;
            pPrevText[pPrev->len] = '\0';

            FreeLine(pLine);

            for (i = line_idx; i < ed->total_lines - 1; i++) {
                LineDesc far *pSrc = GetLine(ed, (USHORT)(i + 1));
                LineDesc far *pDst = GetLine(ed, i);
                *pDst = *pSrc;
            }

            ed->total_lines--;
            ed->bModified = TRUE;

            if (ed->cur_row > 1) {
                ed->cur_row--;
            } else if (ed->top_line > 0) {
                ed->top_line--;
            }
            ed->cur_col = (old_prev_len < ed->cols - 2) ? old_prev_len : (USHORT)(ed->cols - 2);
            ed->left_col = (old_prev_len >= ed->cols - 2) ? (USHORT)(old_prev_len - (ed->cols - 2)) : 0;
        }
    }
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
        VioWrtCharStrAtt((PCH)pMenu->items[i].pszText, (USHORT)(w - 2), (USHORT)(top + 1 + i), (USHORT)(left + 1), &bAttr, 0);
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
        memset(szField, ' ', fieldLen);
        if (curLen > 0) {
            memcpy(szField, pszBuffer, (curLen > fieldLen) ? fieldLen : curLen);
        }
        VioWrtCharStrAtt((PCH)"[", 1, (USHORT)(dTop + 3), (USHORT)(dLeft + 3), (PBYTE)"\x70", 0);
        VioWrtCharStrAtt((PCH)szField, fieldLen, (USHORT)(dTop + 3), (USHORT)(dLeft + 4), (PBYTE)"\x0F", 0);
        VioWrtCharStrAtt((PCH)"]", 1, (USHORT)(dTop + 3), (USHORT)(dLeft + 4 + fieldLen), (PBYTE)"\x70", 0);

        /* Pushbuttons */
        VioWrtCharStrAtt((PCH)"< OK >", 6, (USHORT)(dTop + 5), (USHORT)(dLeft + 12), (PBYTE)"\x0F", 0);
        VioWrtCharStrAtt((PCH)"< Cancel >", 10, (USHORT)(dTop + 5), (USHORT)(dLeft + 24), (PBYTE)"\x70", 0);

        VioSetCurPos((USHORT)(dTop + 3), (USHORT)(dLeft + 4 + curPos), 0);

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
                    if (curPos < curLen && curPos < fieldLen) curPos++;
                    break;
            }
        } else if ((UCHAR)kki.chChar >= 32 && (UCHAR)kki.chChar != 127) {
            if (curLen < maxLen - 1 && curLen < fieldLen) {
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
    strcpy(szNewPath, ed->szFilename);

    if (PromptFilename(ed, "Save As", szNewPath, sizeof(szNewPath))) {
        if (strlen(szNewPath) > 0) {
            strcpy(ed->szFilename, szNewPath);
            SaveFile(ed);
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
            SaveFile(ed);
        } else if (nRes == -1) {
            return;
        }
    }

    if (PromptFilename(ed, "Open File", szOpenPath, sizeof(szOpenPath))) {
        if (strlen(szOpenPath) > 0) {
            USHORT i;
            for (i = 0; i < ed->total_lines; i++) {
                LineDesc far *pLine = GetLine(ed, i);
                FreeLine(pLine);
            }
            if (ed->selTable) {
                DosFreeSeg(ed->selTable);
                ed->selTable = 0;
            }

            LoadFile(ed, szOpenPath);
            ed->top_line = 0;
            ed->left_col = 0;
            ed->cur_row = 1;
            ed->cur_col = 0;
            ed->bModified = FALSE;

            DrawDocument(ed);
            DrawScrollbar(ed);
            DrawStatusBar(ed);
        }
    }
}

static void DoFileNew(EditorState *ed)
{
    USHORT i;
    LineDesc far *pLine;

    if (ed->bModified) {
        SHORT nRes = ShowSavePrompt(ed);
        if (nRes == 1) {
            SaveFile(ed);
        } else if (nRes == -1) {
            return;
        }
    }

    for (i = 0; i < ed->total_lines; i++) {
        pLine = GetLine(ed, i);
        FreeLine(pLine);
    }

    strcpy(ed->szFilename, "UNTITLED.TXT");
    pLine = GetLine(ed, 0);
    AllocLineWithText(pLine, NULL, 0);

    ed->total_lines = 1;
    ed->top_line = 0;
    ed->left_col = 0;
    ed->cur_row = 1;
    ed->cur_col = 0;
    ed->bModified = FALSE;

    DrawDocument(ed);
    DrawScrollbar(ed);
    DrawStatusBar(ed);
    VioSetCurPos(ed->cur_row, ed->cur_col, 0);
}

static void CleanupMemory(EditorState *ed)
{
    USHORT i;
    for (i = 0; i < ed->total_lines; i++) {
        LineDesc far *pLine = GetLine(ed, i);
        FreeLine(pLine);
    }
    if (ed->selTable) DosFreeSeg(ed->selTable);
}

int main(int argc, char *argv[])
{
    VIOMODEINFO vmi;
    KBDKEYINFO kki;
    EditorState ed;
    BOOL bRunning = TRUE;
    USHORT old_line_count;
    USHORT usCmd;
    SHORT nRes;
    const char *pszTarget = (argc > 1) ? argv[1] : "NEWFILE.TXT";

    vmi.cb = sizeof(vmi);
    VioGetMode(&vmi, 0);

    memset(&ed, 0, sizeof(ed));
    ed.rows = vmi.row;
    ed.cols = vmi.col;
    ed.cur_row = 1;
    ed.cur_col = 0;
    ed.top_line = 0;
    ed.left_col = 0;
    ed.bModified = FALSE;

    LoadFile(&ed, pszTarget);

    DrawMenuBar(&ed, -1);
    DrawDocument(&ed);
    DrawScrollbar(&ed);
    DrawStatusBar(&ed);
    VioSetCurPos(ed.cur_row, ed.cur_col, 0);

    while (bRunning) {
        KbdCharIn(&kki, IO_WAIT, 0);

        if (kki.chChar == 0 || (UCHAR)kki.chChar == 0xE0) {
            switch (kki.chScan) {
                case 0x48: /* Up */
                    if (ed.cur_row > 1) {
                        ed.cur_row--;
                    } else if (ed.top_line > 0) {
                        ed.top_line--;
                        DrawDocument(&ed);
                        DrawScrollbar(&ed);
                    }
                    break;
                case 0x50: /* Down */
                    if (ed.cur_row < ed.rows - 2 && (ed.top_line + ed.cur_row) < ed.total_lines) {
                        ed.cur_row++;
                    } else if ((ed.top_line + (ed.rows - 2)) < ed.total_lines) {
                        ed.top_line++;
                        DrawDocument(&ed);
                        DrawScrollbar(&ed);
                    }
                    break;
                case 0x4B: /* Left */
                    if (ed.cur_col > 0) {
                        ed.cur_col--;
                    } else if (ed.left_col > 0) {
                        ed.left_col--;
                        DrawDocument(&ed);
                    }
                    break;
                case 0x4D: /* Right */
                    if (ed.cur_col < ed.cols - 2) {
                        ed.cur_col++;
                    } else {
                        ed.left_col++;
                        DrawDocument(&ed);
                    }
                    break;
                case 0x49: /* Page Up */
                    if (ed.top_line >= (ed.rows - 2)) {
                        ed.top_line -= (ed.rows - 2);
                    } else {
                        ed.top_line = 0;
                    }
                    DrawDocument(&ed);
                    DrawScrollbar(&ed);
                    break;
                case 0x51: /* Page Down */
                    if (ed.top_line + (ed.rows - 2) < ed.total_lines) {
                        ed.top_line += (ed.rows - 2);
                        DrawDocument(&ed);
                        DrawScrollbar(&ed);
                    }
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
                                SaveFile(&ed);
                                bRunning = FALSE;
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
                    break;
                case 0x1F: /* Alt+S: Search Menu */
                    usCmd = RunMenu(&ed, 2);
                    break;
                case 0x18: /* Alt+O: Options Menu */
                    usCmd = RunMenu(&ed, 3);
                    break;
            }
        } else if (kki.chChar == 0x1B) {
            if (ed.bModified) {
                nRes = ShowSavePrompt(&ed);
                if (nRes == 1) {
                    SaveFile(&ed);
                    bRunning = FALSE;
                } else if (nRes == 0) {
                    bRunning = FALSE;
                }
            } else {
                bRunning = FALSE;
            }
        } else if (kki.chChar == '\r') {
            InsertNewline(&ed);
            DrawDocument(&ed);
            DrawScrollbar(&ed);
        } else if (kki.chChar == '\b') {
            old_line_count = ed.total_lines;
            DeleteCharBack(&ed);
            if (ed.total_lines != old_line_count) {
                DrawDocument(&ed);
                DrawScrollbar(&ed);
            } else {
                DrawCurrentLine(&ed);
            }
        } else if ((UCHAR)kki.chChar >= 32 && (UCHAR)kki.chChar != 127) {
            InsertChar(&ed, kki.chChar);
            DrawCurrentLine(&ed);
        }

        DrawStatusBar(&ed);
        VioSetCurPos(ed.cur_row, ed.cur_col, 0);
    }

    CleanupMemory(&ed);
    VioScrollUp(0, 0, (USHORT)(ed.rows - 1), (USHORT)(ed.cols - 1), 0xFFFF, (PBYTE)"\x20\x07", 0);
    VioSetCurPos(0, 0, 0);

    return 0;
}