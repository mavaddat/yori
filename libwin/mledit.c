/**
 * @file libwin/mledit.c
 *
 * Yori window multiline edit control
 *
 * Copyright (c) 2020-2024 Malcolm J. Smith
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "yoripch.h"
#include "yorilib.h"
#include "yoriwin.h"
#include "winpriv.h"

#if DBG
/**
 When reallocating a line, add this many extra characters.  For debug, set
 this to a low value to force plenty of allocations and frees, since these
 invalidate freed memory to force access violations on memory errors.
 */
#define YORIWIN_MLEDIT_LINE_PADDING (0x4)
#else
/**
 When reallocating a line, add this many extra characters on the assumption
 that the user is actively working on the line and another modification
 that needs space is likely.  This value is arbitrary.
 */
#define YORIWIN_MLEDIT_LINE_PADDING (0x40)
#endif

/**
 Information about the selection region within a multiline edit control.
 */
typedef struct _YORIWIN_MLEDIT_SELECT {

    /**
     Indicates if a selection is currently active, and if so, what caused the
     activation.
     */
    enum {
        YoriWinMlEditSelectNotActive = 0,
        YoriWinMlEditSelKbdFromTopDown = 1,
        YoriWinMlEditSelKbdFromBottomUp = 2,
        YoriWinMlEditSelMouseFromTopDown = 3,
        YoriWinMlEditSelMouseFromBottomUp = 4,
        YoriWinMlEditSelMouseComplete = 5
    } Active;

    /**
     Specifies the line index containing the beginning of the selection.
     */
    YORI_ALLOC_SIZE_T FirstLine;

    /**
     Specifies the character offset containing the beginning of the selection.
     */
    YORI_ALLOC_SIZE_T FirstCharOffset;

    /**
     Specifies the line index containing the end of the selection.
     */
    YORI_ALLOC_SIZE_T LastLine;

    /**
     Specifies the index after the final character selected on the final line.
     This value can therefore be zero through to the length of string
     inclusive.
     */
    YORI_ALLOC_SIZE_T LastCharOffset;

} YORIWIN_MLEDIT_SELECT, FAR *PYORIWIN_MLEDIT_SELECT;

/**
 A set of modification operations that can be performed on the buffer that
 can be undone.
 */
typedef enum _YORIWIN_CTRL_MLEDIT_UNDO_OP {
    YoriWinMlEditUndoInsertText = 0,
    YoriWinMlEditUndoOverwriteText = 1,
    YoriWinMlEditUndoDeleteText = 2
} YORIWIN_CTRL_MLEDIT_UNDO_OP;

/**
 Information about a single operation to undo.
 */
typedef struct _YORIWIN_CTRL_MLEDIT_UNDO {

    /**
     The list of operations that can be undone on the multiline edit control.
     */
    YORI_LIST_ENTRY ListEntry;

    /**
     The type of this operation.
     */
    YORIWIN_CTRL_MLEDIT_UNDO_OP Op;

    /**
     If TRUE, when this record is applied, the next record should be applied
     at the same time.  This allows insert + delete type operations to be
     combined.  If FALSE, this record is the final record to apply.
     */
    BOOLEAN ChainWithNext;

    /**
     Information specific to each type of operation.
     */
    union {
        struct {

            /**
             The first line of the range that was inserted and should be
             deleted on undo.
             */
            YORI_ALLOC_SIZE_T FirstLineToDelete;

            /**
             The first offset of the range that was inserted and should be
             deleted on undo.
             */
            YORI_ALLOC_SIZE_T FirstCharOffsetToDelete;

            /**
             The last line of the range that was inserted and should be
             deleted on undo.
             */
            YORI_ALLOC_SIZE_T LastLineToDelete;

            /**
             The last offset of the range that was inserted and should be
             deleted on undo.
             */
            YORI_ALLOC_SIZE_T LastCharOffsetToDelete;
        } InsertText;

        struct {

            /**
             The first line of the range that was deleted and needs to be
             reinserted.
             */
            YORI_ALLOC_SIZE_T FirstLine;

            /**
             The first character of the range that was deleted and needs to be
             reinserted.
             */
            YORI_ALLOC_SIZE_T FirstCharOffset;

            /**
             The text to reinsert on undo.
             */
            YORI_STRING Text;

        } DeleteText;

        struct {
            /**
             The first line of the range that was overwritten and should be
             deleted on undo.
             */
            YORI_ALLOC_SIZE_T FirstLineToDelete;

            /**
             The first offset of the range that was overwritten and should be
             deleted on undo.
             */
            YORI_ALLOC_SIZE_T FirstCharOffsetToDelete;

            /**
             The last line of the range that was overwritten and should be
             deleted on undo.
             */
            YORI_ALLOC_SIZE_T LastLineToDelete;

            /**
             The last offset of the range that was overwritten and should be
             deleted on undo.
             */
            YORI_ALLOC_SIZE_T LastCharOffsetToDelete;

            /**
             The first line of the range that should be inserted to replace
             the overwritten text.
             */
            YORI_ALLOC_SIZE_T FirstLine;

            /**
             The first character of the range that should be inserted to replace
             the overwritten text.
             */
            YORI_ALLOC_SIZE_T FirstCharOffset;

            /**
             The offset of the first character that the user changed.  This
             must be on the same line as FirstLine but may be after
             FirstCharOffset because the saved range may be larger than the
             range that the user modified.  This value is used to determine
             the cursor location on undo.
             */
            YORI_ALLOC_SIZE_T FirstCharOffsetModified;

            /**
             The offset of the last character that the user changed.  This
             must be on the same line as LastLineToDelete but may be before
             LastCharOffsetToDelete because the saved range may be larger than
             the range that the user modified.  This value is used to
             determine if a later modification should be part of an earlier
             undo record.
             */
            YORI_ALLOC_SIZE_T LastCharOffsetModified;

            /**
             The text to reinsert on undo.
             */
            YORI_STRING Text;
        } OverwriteText;
    } u;
} YORIWIN_CTRL_MLEDIT_UNDO, FAR *PYORIWIN_CTRL_MLEDIT_UNDO;

/**
 A structure describing the contents of a multiline edit control.
 */
typedef struct _YORIWIN_CTRL_MLEDIT {

    /**
     A common header for all controls
     */
    YORIWIN_CTRL Ctrl;

    /**
     Pointer to the vertical scroll bar associated with the multiline edit.
     */
    PYORIWIN_CTRL VScrollCtrl;

    /**
     Optional pointer to a callback to invoke when the cursor moves.
     */
    PYORIWIN_NOTIFY_MLEDIT_CURSOR CursorMoveCallback;

    /**
     The caption to display above the edit control.
     */
    YORI_STRING Caption;

    /**
     An array of lines corresponding to lines within a file.
     */
    PYORI_STRING LineArray;

    /**
     The number of lines allocated within LineArray.
     */
    YORI_ALLOC_SIZE_T LinesAllocated;

    /**
     The number of lines populated with text within LineArray.
     */
    YORI_ALLOC_SIZE_T LinesPopulated;

    /**
     A stack of changes which can be undone.
     */
    YORI_LIST_ENTRY Undo;

    /**
     A stack of changes which can be redone.
     */
    YORI_LIST_ENTRY Redo;

    /**
     The index within LineArray that is displayed at the top of the control.
     */
    YORI_ALLOC_SIZE_T ViewportTop;

    /**
     The horizontal offset within each line to display.
     */
    YORI_ALLOC_SIZE_T ViewportLeft;

    /**
     The index within LineArray that the cursor is located at.
     */
    YORI_ALLOC_SIZE_T CursorLine;

    /**
     The horizontal offset of the cursor in terms of the offset within the
     line buffer.
     */
    YORI_ALLOC_SIZE_T CursorOffset;

    /**
     The horizontal offset of the cursor in terms of the cell where it should
     be displayed.  This is typically the same as CursorOffset but can differ
     due to things like tab expansion.
     */
    YORI_ALLOC_SIZE_T DisplayCursorOffset;

    /**
     The desired horizontal offset from the beginning of the display.  This
     can be greater than the actual DisplayCursorOffset above if the user is
     navigating up or down, and the current line is shorter than the offset
     of the cursor when the user started navigating.  A special value of -1
     is used to indicate that this value is not populated, because navigation
     is not currently occurring.
     */
    YORI_ALLOC_SIZE_T DesiredDisplayCursorOffset;

    /**
     The current number of spaces to display for each tab.
     */
    YORI_ALLOC_SIZE_T TabWidth;

    /**
     The first line, in cursor coordinates, that requires redrawing.  Lines
     between this and the last line below (inclusive) will be redrawn on
     paint.  If this value is greater than the last line, no redrawing
     occurs.  This is a fairly common scenario when the cursor is moved,
     where a repaint is needed but no data changes are occurring.
     */
    YORI_ALLOC_SIZE_T FirstDirtyLine;

    /**
     The last line, in cursor coordinates, that requires redrawing.  Lines
     between the first line above and this line (inclusive) will be redrawn
     on paint.
     */
    YORI_ALLOC_SIZE_T LastDirtyLine;

    /**
     Specifies the selection state of text within the multiline edit control.
     This is encapsulated into a structure purely for readability.
     */
    YORIWIN_MLEDIT_SELECT Selection;

    /**
     If TRUE, the previous edit has started a new line which has auto indent
     applied.  When this occurs, backspace should remove an entire indent,
     not just a character.  Any modification or cursor movement should set
     this to FALSE, with the frustrating exception of backspace itself,
     which leaves this mode in effect.
     */
    BOOLEAN AutoIndentApplied;

    /**
     When AutoIndentApplied is TRUE, specifies the number of characters to
     obtain from the previous indented line.  This can be less than the total
     number of indentation characters if the lines contain different
     white space characters (eg. if the first line contains a space and tab,
     and a later line contains two spaces, the first space is considered a
     match but the tab is not.
     */
    YORI_ALLOC_SIZE_T AutoIndentSourceLength;

    /**
     When AutoIndentApplied is TRUE, specifies the line that has an auto
     indent applied.  This is used to detect cursor movement away from the
     line and reset auto indent state.
     */
    YORI_ALLOC_SIZE_T AutoIndentAppliedLine;

    /**
     Records the last observed mouse location when a mouse selection is
     active.  This is repeatedly used via a timer when the mouse moves off
     the control area.  Once the mouse returns to the control area or the
     button is released (completing the selection) this value is undefined.
     */
    YORIWIN_BOUNDED_COORD LastMousePos;

    /**
     A timer that is used to indicate the previous mouse position should be
     repeated to facilitate scroll.  This can be NULL if auto scroll is not
     in effect.
     */
    PYORIWIN_CTRL_HANDLE Timer;

    /**
     When inputting a character by value, the current value that has been
     accumulated (since this requires multiple key events.)
     */
    DWORD NumericKeyValue;

    /**
     Indicates how to interpret the NumericKeyValue.  Ascii uses CP_OEMCP,
     Ansi uses CP_ACP, Unicode is direct.  Also note that Unicode takes
     input in hexadecimal to match the normal U+xxxx specification.
     */
    YORI_LIB_NUMERIC_KEY_TYPE NumericKeyType;

    /**
     The attributes to display text in.
     */
    WORD TextAttributes;

    /**
     The attributes to display selected text in.
     */
    WORD SelectedAttributes;

    /**
     The attributes to display the caption in.
     */
    WORD CaptionAttributes;

    /**
     0 if the cursor is currently not visible.  20 for insert mode, 50 for
     overwrite mode.  Paint calculates the desired value and based on
     comparing the new value with the current value decides on the action
     to take.
     */
    UCHAR PercentCursorVisibleLastPaint;

    /**
     If TRUE, new characters are inserted at the cursor position.  If FALSE,
     new characters overwrite existing characters.
     */
    BOOLEAN InsertMode;

    /**
     If TRUE, the edit control should not support editing.  If FALSE, it is
     a regular, editable edit control.
     */
    BOOLEAN ReadOnly;

    /**
     TRUE if the control currently has focus, FALSE if another control has
     focus.
     */
    BOOLEAN HasFocus;

    /**
     TRUE if the contents of the control have been modified by user input.
     FALSE if the contents have not changed since this value was last reset.
     */
    BOOLEAN UserModified;

    /**
     TRUE if events indicate that the left mouse button is currently held
     down.  FALSE if the mouse button is released.
     */
    BOOLEAN MouseButtonDown;

    /**
     TRUE if the multiline edit control is following traditional MS-DOS edit
     navigation rules, FALSE if following more modern multiline edit
     navigation rules.  In the traditional model, the cursor can move
     infinitely right of the text in any line, so the cursor's line does not
     change in response to left and right keys.
     */
    BOOLEAN TradEditNavigation;

    /**
     TRUE if new lines should start with leading whitespace characters from
     previous lines.  FALSE if new lines should start at offset zero.
     */
    BOOLEAN AutoIndent;

    /**
     TRUE if a tab key should be substituted with the number of spaces
     specified in TabWidth.  FALSE if it should be retained as a tab and only
     visually rendered according to TabWidth.
     */
    BOOLEAN ExpandTab;

} YORIWIN_CTRL_MLEDIT, FAR *PYORIWIN_CTRL_MLEDIT;

//
//  =========================================
//  DISPLAY FUNCTIONS
//  =========================================
//

/**
 Return TRUE if the multiline edit control supports double-wide characters.
 If FALSE, all characters are rendered as narrow.

 @param MlEdit Pointer to the multiline edit control.

 @return TRUE to indicate double wide characters are supported, FALSE if they
         are not.
 */
BOOLEAN
YoriWinMlEditIsDblWideSupp(
    __in PYORIWIN_CTRL_MLEDIT MlEdit
    )
{
    PYORIWIN_WINDOW TopLevelWindow;
    PYORIWIN_WINMGR_HANDLE WinMgr;

    TopLevelWindow = YoriWinGetTopLevelWindow(&MlEdit->Ctrl);
    WinMgr = YoriWinGetWinMgrHandle(TopLevelWindow);

    return YoriWinIsDblWideSupp(WinMgr);
}

/**
 Given a cursor offset expressed in terms of the display location of the
 cursor, find the offset within the string buffer.  These are typically the
 same but tab expansion means they are not guaranteed to be identical.

 @param MlEdit Pointer to the multiline edit control.

 @param LineIndex Specifies the line to evaluate against.

 @param DisplayChar Specifies the location in terms of the number of cells
        from the left of the line.

 @param CursorChar On completion, populated with the offset in the line buffer
        corresponding to the display offset.

 @param Remainder If specified, on completion, updated with the number of
        empty display cells before the data at CursorChar is displayed.
        This can be caused by a tab or wide char that encompasses the
        DisplayChar cell along with other cells, where no single character
        starts at the requested DisplayChar.
 */
VOID
YoriWinMlEditFindCursorCharFromDisplayChar(
    __in PYORIWIN_CTRL_MLEDIT MlEdit,
    __in YORI_ALLOC_SIZE_T LineIndex,
    __in YORI_ALLOC_SIZE_T DisplayChar,
    __out PYORI_ALLOC_SIZE_T CursorChar,
    __out_opt PYORI_ALLOC_SIZE_T Remainder
    )
{
    PYORI_STRING Line;
    PYORIWIN_WINDOW TopLevelWindow;
    PYORIWIN_WINMGR_HANDLE WinMgrHandle;

    TopLevelWindow = YoriWinGetTopLevelWindow(&MlEdit->Ctrl);
    WinMgrHandle = YoriWinGetWinMgrHandle(TopLevelWindow);

    if (LineIndex >= MlEdit->LinesPopulated) {
        *CursorChar = DisplayChar;
        if (Remainder != NULL) {
            *Remainder = 0;
        }
        return;
    }

    Line = &MlEdit->LineArray[LineIndex];

    YoriWinTextBufferOffsetFromDisp(WinMgrHandle,
                                    Line,
                                    MlEdit->TabWidth,
                                    DisplayChar,
                                    MlEdit->TradEditNavigation,
                                    CursorChar,
                                    Remainder);
}

/**
 Given a cursor offset expressed in terms of the buffer offset of the cursor,
 find the offset within the display.  These are typically the same but tab
 expansion means they are not guaranteed to be identical.

 @param MlEdit Pointer to the multiline edit control.

 @param LineIndex Specifies the line to evaluate against.

 @param CursorChar Specifies the location in terms of the line buffer offset.

 @param DisplayChar On completion, populated with the offset from the left of
        the display.
 */
VOID
YoriWinMlEditFindDisplayCharFromCursorChar(
    __in PYORIWIN_CTRL_MLEDIT MlEdit,
    __in YORI_ALLOC_SIZE_T LineIndex,
    __in YORI_ALLOC_SIZE_T CursorChar,
    __out PYORI_ALLOC_SIZE_T DisplayChar
    )
{
    PYORI_STRING Line;
    PYORIWIN_WINDOW TopLevelWindow;
    PYORIWIN_WINMGR_HANDLE WinMgrHandle;

    TopLevelWindow = YoriWinGetTopLevelWindow(&MlEdit->Ctrl);
    WinMgrHandle = YoriWinGetWinMgrHandle(TopLevelWindow);

    if (LineIndex >= MlEdit->LinesPopulated) {
        *DisplayChar = CursorChar;
        return;
    }

    Line = &MlEdit->LineArray[LineIndex];

    YoriWinTextDispOffsetFromBuffer(WinMgrHandle,
                                    Line,
                                    MlEdit->TabWidth,
                                    CursorChar,
                                    DisplayChar);
}

/**
 Translate coordinates relative to the control's client area into
 cursor coordinates, being offsets to the line and character within the
 buffers being edited.

 @param MlEdit Pointer to the multiline edit control.

 @param ViewportLeftOffset Offset from the left of the client area.

 @param ViewportTopOffset Offset from the top of the client area.

 @param LineIndex Populated with the cursor index to the line.

 @param CursorChar Populated with the offset within the line of the cursor.

 @return TRUE to indicate the region is within the current buffer.  FALSE
         to indicate it's beyond the current buffer.
 */
BOOLEAN
YoriWinMlEditTransViewCoordToCursor(
    __in PYORIWIN_CTRL_MLEDIT MlEdit,
    __in YORI_ALLOC_SIZE_T ViewportLeftOffset,
    __in YORI_ALLOC_SIZE_T ViewportTopOffset,
    __out PYORI_ALLOC_SIZE_T LineIndex,
    __out PYORI_ALLOC_SIZE_T CursorChar
    )
{
    YORI_ALLOC_SIZE_T LineOffset;
    YORI_ALLOC_SIZE_T DisplayOffset;
    BOOLEAN Result = TRUE;

    LineOffset = ViewportTopOffset + MlEdit->ViewportTop;
    if (LineOffset >= MlEdit->LinesPopulated) {
        if (MlEdit->LinesPopulated == 0) {
            LineOffset = 0;
        } else {
            LineOffset = MlEdit->LinesPopulated - 1;
        }
        Result = FALSE;
    }

    DisplayOffset = ViewportLeftOffset + MlEdit->ViewportLeft;

    //
    //  MSFIX Review callers to this function and see what they need for
    //  Remainder
    //

    YoriWinMlEditFindCursorCharFromDisplayChar(MlEdit, LineOffset, DisplayOffset, CursorChar, NULL);
    *LineIndex = LineOffset;
    return Result;
}

/**
 If one is not already defined, define the desired display offset, which is
 the display column that would ideally be returned to as the cursor moves up
 or down lines.  This may already be defined, if the user is navigating up or
 down multiple times.

 @param MlEdit Pointer to the multiline edit control.
 */
VOID
YoriWinMlEditPopulateDesiredDisplayOffset(
    __in PYORIWIN_CTRL_MLEDIT MlEdit
    )
{
    if (MlEdit->DesiredDisplayCursorOffset == (YORI_ALLOC_SIZE_T)-1) {
        MlEdit->DesiredDisplayCursorOffset = MlEdit->DisplayCursorOffset;
    }
}

/**
 Indicate that the user has performed an operation that is not navigating up
 or down, meaning that any desired offset should be cleared.

 @param MlEdit Pointer to the multiline edit control.
 */
VOID
YoriWinMlEditClearDesiredDisplayOffset(
    __in PYORIWIN_CTRL_MLEDIT MlEdit
    )
{
    MlEdit->DesiredDisplayCursorOffset = (YORI_ALLOC_SIZE_T)-1;
}

/**
 Return TRUE if a selection region is active, or FALSE if no selection is
 currently active.

 @param CtrlHandle Pointer to the multiline edit control.

 @return TRUE if a selection region is active, or FALSE if no selection is
         currently active.
 */
BOOLEAN
YoriWinMlEditSelectionActive(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle
    )
{
    PYORIWIN_CTRL_MLEDIT MlEdit;
    PYORIWIN_CTRL Ctrl;

    Ctrl = (PYORIWIN_CTRL)CtrlHandle;
    MlEdit = CONTAINING_RECORD(Ctrl, YORIWIN_CTRL_MLEDIT, Ctrl);

    if (MlEdit->Selection.Active == YoriWinMlEditSelectNotActive) {
        return FALSE;
    }
    return TRUE;
}

/**
 Draw the scroll bar with current information about the location and contents
 of the viewport.

 @param MlEdit Pointer to the multiline edit to draw.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinMlEditRepaintScrollBar(
    __in PYORIWIN_CTRL_MLEDIT MlEdit
    )
{
    if (MlEdit->VScrollCtrl) {
        DWORD MaximumTopValue;
        COORD ClientSize;

        YoriWinGetCtrlClientSize(&MlEdit->Ctrl, &ClientSize);

        if (MlEdit->LinesPopulated > (YORI_ALLOC_SIZE_T)ClientSize.Y) {
            MaximumTopValue = MlEdit->LinesPopulated - ClientSize.Y;
        } else {
            MaximumTopValue = 0;
        }

        YoriWinScrollBarSetPosition(MlEdit->VScrollCtrl, MlEdit->ViewportTop, ClientSize.Y, MaximumTopValue);
    }

    return TRUE;
}

/**
 Draw the border, caption and scroll bars on the control.

 @param MlEdit Pointer to the multiline edit to draw.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinMlEditPaintNonClient(
    __in PYORIWIN_CTRL_MLEDIT MlEdit
    )
{
    SMALL_RECT BorderPoint;
    WORD BorderFlags;
    WORD WindowAttributes;
    WORD ColumnIndex;

    BorderPoint.Left = 0;
    BorderPoint.Top = 0;
    BorderPoint.Right = (SHORT)(MlEdit->Ctrl.FullRect.Right - MlEdit->Ctrl.FullRect.Left);
    BorderPoint.Bottom = (SHORT)(MlEdit->Ctrl.FullRect.Bottom - MlEdit->Ctrl.FullRect.Top);

    BorderFlags = YORIWIN_BORDER_TYPE_SUNKEN | YORIWIN_BORDER_TYPE_SINGLE;

    WindowAttributes = MlEdit->TextAttributes;
    YoriWinDrawBorderCtrl(&MlEdit->Ctrl, &BorderPoint, WindowAttributes, BorderFlags);

    if (MlEdit->Caption.LengthInChars > 0) {
        YORI_ALLOC_SIZE_T CaptionCharsToDisplay;
        YORI_ALLOC_SIZE_T StartOffset;
        COORD ClientSize;

        YoriWinGetCtrlClientSize(&MlEdit->Ctrl, &ClientSize);

        CaptionCharsToDisplay = MlEdit->Caption.LengthInChars;
        if (CaptionCharsToDisplay > (YORI_ALLOC_SIZE_T)ClientSize.X) {
            CaptionCharsToDisplay = ClientSize.X;
        }

        StartOffset = (ClientSize.X - CaptionCharsToDisplay) / 2;
        for (ColumnIndex = 0; ColumnIndex < CaptionCharsToDisplay; ColumnIndex++) {
            YoriWinSetCtrlNonClientCell(&MlEdit->Ctrl,
                                        (WORD)(ColumnIndex + StartOffset),
                                        0,
                                        MlEdit->Caption.StartOfString[ColumnIndex],
                                        MlEdit->CaptionAttributes);
        }
    }

    //
    //  Repaint the scroll bar after the border is drawn
    //

    YoriWinMlEditRepaintScrollBar(MlEdit);
    return TRUE;
}

/**
 Calculate the line of text to display.  This is typically the exact same
 string as the line from the file's contents, but can diverge due to
 display requirements such as tab expansion or wide characters.  The
 generated line contains the text visible within the viewport (ie., is
 truncated according to the current state of ViewportLeft.)

 @param MlEdit Pointer to the multiline edit control.

 @param LineIndex Specifies the line number to obtain a display line for.

 @param ClientWidth Specifies the number of cells that will be displayed
        within the control.  There is no need to populate more data than
        this.

 @param DisplayLine On successful completion, populated with a string to
        display.  This may point back into the same data as the original
        line, or may be a fresh allocation.  The caller should free it with
        @ref YoriLibFreeStringContents .  If the result points back to the
        original string, the MemoryToFree member will be NULL to indicate
        that the caller has nothing to deallocate.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinMlEditGenerateDisplayLine(
    __in PYORIWIN_CTRL_MLEDIT MlEdit,
    __in YORI_ALLOC_SIZE_T LineIndex,
    __in YORI_ALLOC_SIZE_T ClientWidth,
    __out PYORI_STRING DisplayLine
    )
{
    PYORI_STRING SourceLine;
    YORI_STRING SourceString;
    PYORIWIN_WINDOW TopLevelWindow;
    PYORIWIN_WINMGR_HANDLE WinMgrHandle;
    YORI_ALLOC_SIZE_T BufferChar;
    YORI_ALLOC_SIZE_T Remainder;

    ASSERT(LineIndex < MlEdit->LinesPopulated);

    TopLevelWindow = YoriWinGetTopLevelWindow(&MlEdit->Ctrl);
    WinMgrHandle = YoriWinGetWinMgrHandle(TopLevelWindow);
    SourceLine = &MlEdit->LineArray[LineIndex];

    //
    //  Create a string that corresponds to the current position in the
    //  viewport.
    //

    YoriWinMlEditFindCursorCharFromDisplayChar(MlEdit,
                                               LineIndex,
                                               MlEdit->ViewportLeft,
                                               &BufferChar,
                                               &Remainder);

    //
    //  If none of the source line is visible, return nothing.
    //

    if (BufferChar >= SourceLine->LengthInChars) {
        YoriLibInitEmptyString(DisplayLine);
        return TRUE;
    }

    YoriLibInitEmptyString(DisplayLine);
    YoriLibInitEmptyString(&SourceString);
    SourceString.StartOfString = &SourceLine->StartOfString[BufferChar];
    SourceString.LengthInChars = SourceLine->LengthInChars - BufferChar;

    //
    //  Generate display cells for the text.
    //

    return YoriWinTextStringToDisplayCells(WinMgrHandle,
                                           &SourceString,
                                           Remainder,
                                           MlEdit->TabWidth,
                                           ClientWidth,
                                           DisplayLine);
}

/**
 Draw a single line of text within the client area of a multiline edit
 control.

 @param MlEdit Pointer to the multiline edit control.

 @param ClientSize Pointer to the dimensions of the client area of the
        control.

 @param LineIndex Specifies the index of the line to draw, in cursor
        coordinates.
 */
VOID
YoriWinMlEditPaintSingleLine(
    __in PYORIWIN_CTRL_MLEDIT MlEdit,
    __in PCOORD ClientSize,
    __in YORI_ALLOC_SIZE_T LineIndex
    )
{
    WORD ColumnIndex;
    WORD WindowAttributes;
    WORD TextAttributes;
    WORD RowIndex;
    BOOLEAN SelectionActive;
    YORI_STRING Line;
    TCHAR Char;
    YORI_ALLOC_SIZE_T ClientWidth;

    ColumnIndex = 0;
    RowIndex = (WORD)(LineIndex - MlEdit->ViewportTop);
    WindowAttributes = MlEdit->TextAttributes;
    SelectionActive = YoriWinMlEditSelectionActive(&MlEdit->Ctrl);
    ClientWidth = (YORI_ALLOC_SIZE_T)ClientSize->X;

    if (LineIndex < MlEdit->LinesPopulated) {
        TextAttributes = WindowAttributes;

        //
        //  If the entire line is selected, indicate that.
        //

        if (SelectionActive &&
            LineIndex > MlEdit->Selection.FirstLine &&
            LineIndex < MlEdit->Selection.LastLine) {
            TextAttributes = MlEdit->SelectedAttributes;
        }

        //
        //  Capture a display string.  This contains one char per cell,
        //  substituting spaces for tabs, and applying any NULLs needed
        //  for wide char display.
        //

        if (!YoriWinMlEditGenerateDisplayLine(MlEdit, LineIndex, ClientWidth, &Line)) {
            YoriLibInitEmptyString(&Line);
        }

        for (; ColumnIndex < (WORD)ClientSize->X && ColumnIndex < Line.LengthInChars; ColumnIndex++) {

            //
            //  If a selection is active, calculate which display cells
            //  should be selected.
            //

            if (SelectionActive) {
                YORI_ALLOC_SIZE_T DisplayFirstCharOffset;
                YORI_ALLOC_SIZE_T DisplayLastCharOffset;
                YORI_ALLOC_SIZE_T DisplayCurrentOffset;
                YoriWinMlEditFindDisplayCharFromCursorChar(MlEdit,
                                                           MlEdit->Selection.FirstLine,
                                                           MlEdit->Selection.FirstCharOffset,
                                                           &DisplayFirstCharOffset);
                YoriWinMlEditFindDisplayCharFromCursorChar(MlEdit,
                                                           MlEdit->Selection.LastLine,
                                                           MlEdit->Selection.LastCharOffset,
                                                           &DisplayLastCharOffset);


                DisplayCurrentOffset = MlEdit->ViewportLeft + ColumnIndex;
                if (LineIndex == MlEdit->Selection.FirstLine &&
                    LineIndex == MlEdit->Selection.LastLine) {
                    TextAttributes = WindowAttributes;
                    if (DisplayCurrentOffset >= DisplayFirstCharOffset &&
                        DisplayCurrentOffset < DisplayLastCharOffset) {
                        TextAttributes = MlEdit->SelectedAttributes;
                    }
                } else if (LineIndex == MlEdit->Selection.FirstLine) {
                    TextAttributes = WindowAttributes;
                    if (DisplayCurrentOffset >= DisplayFirstCharOffset) {
                        TextAttributes = MlEdit->SelectedAttributes;
                    }
                } else if (LineIndex == MlEdit->Selection.LastLine) {
                    TextAttributes = WindowAttributes;
                    if (DisplayCurrentOffset < DisplayLastCharOffset) {
                        TextAttributes = MlEdit->SelectedAttributes;
                    }
                }
            }


            Char = Line.StartOfString[ColumnIndex];

            YoriWinSetCtrlClientCell(&MlEdit->Ctrl, ColumnIndex, RowIndex, Char, TextAttributes);
        }

        //
        //  Unless a tab or wide char is present, this is a no-op
        //

        YoriLibFreeStringContents(&Line);
    }
    for (; ColumnIndex < (WORD)ClientSize->X; ColumnIndex++) {
        YoriWinSetCtrlClientCell(&MlEdit->Ctrl, ColumnIndex, RowIndex, ' ', WindowAttributes);
    }
}

/**
 Draw the edit with its current state applied.

 @param MlEdit Pointer to the multiline edit to draw.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinMlEditPaint(
    __in PYORIWIN_CTRL_MLEDIT MlEdit
    )
{
    WORD RowIndex;
    YORI_ALLOC_SIZE_T LineIndex;
    COORD ClientSize;

    YoriWinGetCtrlClientSize(&MlEdit->Ctrl, &ClientSize);

    if (MlEdit->FirstDirtyLine <= MlEdit->LastDirtyLine) {

        for (RowIndex = 0; RowIndex < (WORD)ClientSize.Y; RowIndex++) {
            LineIndex = MlEdit->ViewportTop + RowIndex;

            //
            //  If the line in the viewport actually has a line in the buffer.
            //  Lines after the end of the buffer still need to be rendered in
            //  the viewport, even if it's trivial.
            //

            if (LineIndex >= MlEdit->FirstDirtyLine &&
                LineIndex <= MlEdit->LastDirtyLine) {

                YoriWinMlEditPaintSingleLine(MlEdit, &ClientSize, LineIndex);
            }
        }

        MlEdit->FirstDirtyLine = (YORI_ALLOC_SIZE_T)-1;
        MlEdit->LastDirtyLine = 0;
    }

    YoriWinMlEditFindDisplayCharFromCursorChar(MlEdit,
                                               MlEdit->CursorLine,
                                               MlEdit->CursorOffset,
                                               &MlEdit->DisplayCursorOffset);

    {
        WORD CursorLineWithinDisplay = 0;
        WORD CursorColumnWithinDisplay = 0;
        UCHAR NewPercentCursorVisible = 0;

        //
        //  If the control has focus, check based on insert state which
        //  type of cursor to display.
        //

        if (MlEdit->HasFocus) {
            if (MlEdit->InsertMode) {
                NewPercentCursorVisible = 20;
            } else {
                NewPercentCursorVisible = 50;
            }
        }

        //
        //  If the cursor is off the display, make it invisible.  If not,
        //  find the offset relative to the display.
        //

        if (MlEdit->CursorLine < MlEdit->ViewportTop ||
            MlEdit->CursorLine >= MlEdit->ViewportTop + ClientSize.Y) {

            NewPercentCursorVisible = 0;
        } else {
            CursorLineWithinDisplay = (WORD)(MlEdit->CursorLine - MlEdit->ViewportTop);
        }

        if (MlEdit->DisplayCursorOffset < MlEdit->ViewportLeft ||
            MlEdit->DisplayCursorOffset >= MlEdit->ViewportLeft + ClientSize.X) {

            NewPercentCursorVisible = 0;
        } else {
            CursorColumnWithinDisplay = (WORD)(MlEdit->DisplayCursorOffset - MlEdit->ViewportLeft);
        }

        //
        //  If the cursor is now invisible and previously wasn't, hide the
        //  cursor.  If it should be visible and previously was some other
        //  state, make it visible in the correct percentage.  If it should
        //  be visible now, position it regardless of state.  Note that the
        //  Windows API expects a nonzero percentage even when hiding the
        //  cursor, so we give it a fairly meaningless value.
        //

        if (NewPercentCursorVisible == 0)  {
            if (MlEdit->PercentCursorVisibleLastPaint != 0) {
                YoriWinSetCtrlCursorState(&MlEdit->Ctrl, FALSE, 25);
            }
        } else {
            if (MlEdit->PercentCursorVisibleLastPaint != NewPercentCursorVisible) {
                YoriWinSetCtrlCursorState(&MlEdit->Ctrl, TRUE, NewPercentCursorVisible);
            }

            YoriWinSetCtrlClientCursorPoint(&MlEdit->Ctrl, CursorColumnWithinDisplay, CursorLineWithinDisplay);
        }

        MlEdit->PercentCursorVisibleLastPaint = NewPercentCursorVisible;
    }

    return TRUE;
}

/**
 Set the range of the multiline edit control that requires redrawing.  This
 range can only be shrunk by actual drawing, so use any new lines to extend
 but not contract the range.

 @param MlEdit Pointer to the multiline edit control.

 @param NewFirstDirtyLine Specifies the first line that needs to be redrawn.

 @param NewLastDirtyLine Specifies the last line that needs to be redrawn.
 */
VOID
YoriWinMlEditExpandDirtyRange(
    __in PYORIWIN_CTRL_MLEDIT MlEdit,
    __in YORI_ALLOC_SIZE_T NewFirstDirtyLine,
    __in YORI_ALLOC_SIZE_T NewLastDirtyLine
    )
{
    if (NewFirstDirtyLine < MlEdit->FirstDirtyLine) {
        MlEdit->FirstDirtyLine = NewFirstDirtyLine;
    }

    if (NewLastDirtyLine > MlEdit->LastDirtyLine) {
        MlEdit->LastDirtyLine = NewLastDirtyLine;
    }
}

/**
 Clear any selection if it is active and indicate that the region it covered
 needs to be redrawn.

 @param MlEdit Pointer to the multiline edit control.
 */
VOID
YoriWinMlEditClearSelection(
    __in PYORIWIN_CTRL_MLEDIT MlEdit
    )
{
    if (MlEdit->Selection.Active == YoriWinMlEditSelectNotActive) {
        return;
    }
    YoriWinMlEditExpandDirtyRange(MlEdit, MlEdit->Selection.FirstLine, MlEdit->Selection.LastLine);
    MlEdit->Selection.Active = YoriWinMlEditSelectNotActive;
}


/**
 Modify the cursor location within the multiline edit control.

 @param MlEdit Pointer to the multiline edit control.

 @param NewCursorOffset The offset of the cursor from the beginning of the
        line, in buffer coordinates.

 @param NewCursorLine The buffer line that the cursor is located on.
 */
VOID
YoriWinMlEditSetCursorPointInt(
    __in PYORIWIN_CTRL_MLEDIT MlEdit,
    __in YORI_ALLOC_SIZE_T NewCursorOffset,
    __in YORI_ALLOC_SIZE_T NewCursorLine
    )
{
    if (NewCursorOffset == MlEdit->CursorOffset &&
        NewCursorLine == MlEdit->CursorLine) {

        return;
    }

    if (MlEdit->AutoIndentApplied) {
        if (NewCursorLine != MlEdit->AutoIndentAppliedLine ||
            NewCursorOffset != MlEdit->AutoIndentSourceLength) {

            MlEdit->AutoIndentApplied = FALSE;
        }
    }

    ASSERT(NewCursorLine == 0 || NewCursorLine < MlEdit->LinesPopulated);

    if (MlEdit->CursorMoveCallback != NULL) {
        MlEdit->CursorMoveCallback(&MlEdit->Ctrl, NewCursorOffset, NewCursorLine);
    }

    MlEdit->CursorOffset = NewCursorOffset;
    MlEdit->CursorLine = NewCursorLine;
}

/**
 Adjust the first character to display in the control to ensure the current
 user cursor is visible somewhere within the control.

 @param MlEdit Pointer to the multiline edit control.
 */
VOID
YoriWinMlEditEnsureCursorShown(
    __in PYORIWIN_CTRL_MLEDIT MlEdit
    )
{
    COORD ClientSize;
    YORI_ALLOC_SIZE_T NewViewportLeft;
    YORI_ALLOC_SIZE_T NewViewportTop;

    NewViewportLeft = MlEdit->ViewportLeft;
    NewViewportTop = MlEdit->ViewportTop;

    YoriWinGetCtrlClientSize(&MlEdit->Ctrl, &ClientSize);

    //
    //  We can't guarantee that the entire selection is on the screen,
    //  but if it's a single line selection that would fit, try to
    //  take that into account.  Do this first so if the cursor would
    //  move the viewport, that takes precedence.
    //

    if (YoriWinMlEditSelectionActive(MlEdit)) {
        YORI_ALLOC_SIZE_T StartSelection;
        YORI_ALLOC_SIZE_T EndSelection;

        YoriWinMlEditFindDisplayCharFromCursorChar(MlEdit,
                                                   MlEdit->Selection.FirstLine,
                                                   MlEdit->Selection.FirstCharOffset,
                                                   &StartSelection);
        YoriWinMlEditFindDisplayCharFromCursorChar(MlEdit,
                                                   MlEdit->Selection.LastLine,
                                                   MlEdit->Selection.LastCharOffset,
                                                   &EndSelection);

        if (StartSelection < NewViewportLeft) {
            NewViewportLeft = StartSelection;
        } else if (EndSelection >= NewViewportLeft + ClientSize.X) {
            NewViewportLeft = EndSelection - ClientSize.X + 1;
        }
    }

    YoriWinMlEditFindDisplayCharFromCursorChar(MlEdit,
                                               MlEdit->CursorLine,
                                               MlEdit->CursorOffset,
                                               &MlEdit->DisplayCursorOffset);

    if (MlEdit->DisplayCursorOffset < NewViewportLeft) {
        NewViewportLeft = MlEdit->DisplayCursorOffset;
    } else if (MlEdit->DisplayCursorOffset >= NewViewportLeft + ClientSize.X) {
        NewViewportLeft = MlEdit->DisplayCursorOffset - ClientSize.X + 1;
    }

    if (MlEdit->CursorLine < NewViewportTop) {
        NewViewportTop = MlEdit->CursorLine;
    } else if (MlEdit->CursorLine >= NewViewportTop + ClientSize.Y) {
        NewViewportTop = MlEdit->CursorLine - ClientSize.Y + 1;
    }

    if (NewViewportTop != MlEdit->ViewportTop) {
        MlEdit->ViewportTop = NewViewportTop;
        YoriWinMlEditExpandDirtyRange(MlEdit, NewViewportTop, (YORI_ALLOC_SIZE_T)-1);
        YoriWinMlEditRepaintScrollBar(MlEdit);
    }

    if (NewViewportLeft != MlEdit->ViewportLeft) {
        MlEdit->ViewportLeft = NewViewportLeft;
        YoriWinMlEditExpandDirtyRange(MlEdit, NewViewportTop, (YORI_ALLOC_SIZE_T)-1);
    }
}

/**
 Toggle the insert state of the control.  If new keystrokes would previously
 insert new characters, future characters overwrite existing characters, and
 vice versa.  The cursor shape will be updated to reflect the new state.

 @param MlEdit Pointer to the multiline edit control.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinMlEditToggleInsert(
    __in PYORIWIN_CTRL_MLEDIT MlEdit
    )
{
    if (MlEdit->InsertMode) {
        MlEdit->InsertMode = FALSE;
    } else {
        MlEdit->InsertMode = TRUE;
    }
    return TRUE;
}

//
//  =========================================
//  UNDO FUNCTIONS
//  =========================================
//

/**
 Free a single undo entry.  This entry is expected to be unlinked from the
 chain.

 @param Undo Pointer to the undo entry to free.
 */
VOID
YoriWinMlEditFreeSingleUndo(
    __in PYORIWIN_CTRL_MLEDIT_UNDO Undo
    )
{
    switch(Undo->Op) {
        case YoriWinMlEditUndoOverwriteText:
            YoriLibFreeStringContents(&Undo->u.OverwriteText.Text);
            break;
        case YoriWinMlEditUndoDeleteText:
            YoriLibFreeStringContents(&Undo->u.DeleteText.Text);
            break;
    }

    YoriLibFree(Undo);
}

/**
 Free all undo entries that are linked into the multiline edit control.

 @param MlEdit Pointer to the multiline edit control.
 */
VOID
YoriWinMlEditClearUndo(
    __in PYORIWIN_CTRL_MLEDIT MlEdit
    )
{
    PYORI_LIST_ENTRY ListHead;
    PYORI_LIST_ENTRY ListEntry;
    PYORIWIN_CTRL_MLEDIT_UNDO Undo;

    ListHead = &MlEdit->Undo;
    while (ListHead != NULL) {

        ListEntry = YoriLibGetNextListEntry(ListHead, NULL);
        while (ListEntry != NULL) {
            YoriLibRemoveListItem(ListEntry);
            Undo = CONTAINING_RECORD(ListEntry, YORIWIN_CTRL_MLEDIT_UNDO, ListEntry);
            YoriWinMlEditFreeSingleUndo(Undo);
            ListEntry = YoriLibGetNextListEntry(ListHead, NULL);
        }

        if (ListHead == &MlEdit->Undo) {
            ListHead = &MlEdit->Redo;
        } else {
            ListHead = NULL;
        }
    }
}

/**
 Free all redo entries that are linked into the multiline edit control.

 @param MlEdit Pointer to the multiline edit control.
 */
VOID
YoriWinMlEditClearRedo(
    __in PYORIWIN_CTRL_MLEDIT MlEdit
    )
{
    PYORI_LIST_ENTRY ListEntry;
    PYORIWIN_CTRL_MLEDIT_UNDO Undo;

    ListEntry = YoriLibGetNextListEntry(&MlEdit->Redo, NULL);
    while (ListEntry != NULL) {
        YoriLibRemoveListItem(ListEntry);
        Undo = CONTAINING_RECORD(ListEntry, YORIWIN_CTRL_MLEDIT_UNDO, ListEntry);
        YoriWinMlEditFreeSingleUndo(Undo);
        ListEntry = YoriLibGetNextListEntry(&MlEdit->Redo, NULL);
    }
}

/**
 Check if a new modification should be included in a previous undo entry
 because the new modification is immediately before the range in the previous
 entry.

 @param MlEdit Pointer to the multiline edit control.

 @param ExistingFirstLine Specifies the beginning line of the range currently
        covered by an undo record.

 @param ExistingFirstCharOffset Specifies the beginning offset of the range
        currently covered by an undo record.

 @param ProposedLastLine Specifies the ending line of a newly modified range.

 @param ProposedLastCharOffset Specifies the ending offset of a newly modified
        range.

 @return TRUE to indicate that the new change is immediately before the
         previous undo record.  FALSE to indicate it requires a new entry.
 */
BOOLEAN
YoriWinMlEditRangeBefore(
    __in PYORIWIN_CTRL_MLEDIT MlEdit,
    __in DWORD ExistingFirstLine,
    __in DWORD ExistingFirstCharOffset,
    __in DWORD ProposedLastLine,
    __in DWORD ProposedLastCharOffset
    )
{
    UNREFERENCED_PARAMETER(MlEdit);

    if (ExistingFirstLine == ProposedLastLine &&
        ExistingFirstCharOffset == ProposedLastCharOffset) {

        return TRUE;
    }

    return FALSE;
}

/**
 Check if a new modification should be included in a previous undo entry
 because the new modification is immediately after the range in the previous
 entry.

 @param MlEdit Pointer to the multiline edit control.

 @param ExistingLastLine Specifies the ending line of the range currently
        covered by an undo record.

 @param ExistingLastCharOffset Specifies the ending offset of the range
        currently covered by an undo record.

 @param ProposedFirstLine Specifies the beginning line of a newly modified
        range.

 @param ProposedFirstCharOffset Specifies the beginning offset of a newly
        modified range.

 @return TRUE to indicate that the new change is immediately after the
         previous undo record.  FALSE to indicate it requires a new entry.
 */
BOOLEAN
YoriWinMlEditRangeNext(
    __in PYORIWIN_CTRL_MLEDIT MlEdit,
    __in DWORD ExistingLastLine,
    __in DWORD ExistingLastCharOffset,
    __in DWORD ProposedFirstLine,
    __in DWORD ProposedFirstCharOffset
    )
{
    UNREFERENCED_PARAMETER(MlEdit);

    if (ExistingLastLine == ProposedFirstLine &&
        ExistingLastCharOffset == ProposedFirstCharOffset) {

        return TRUE;
    }

    return FALSE;
}

/**
 Return an undo record for the incoming operation.  This may be a newly
 allocated undo record, or if the operation is adjacent to the previous
 operation it may return an existing record.

 @param MlEdit Pointer to the multiline edit control.

 @param Op Specifies the type of the operation.  Only the same type of
        operations can reuse previous records.

 @param FirstLine Specifies the beginning line of the range that is being
        modified.

 @param FirstCharOffset Specifies the beginning offset of the range that is
        being modified.

 @param LastLine Specifies the last line of the range that is being modified.
        This may not always be known until after the operation is performed,
        but in that case the operation cannot be prepended to a previous
        operation of the same type, so it is not required.

 @param LastCharOffset Specifies the last offset of the range that is being
        modified.  This may not always be known until after the operation is
        performed, but in that case the operation cannot be prepended to a
        previous operation of the same type, so it is not required.

 @param NewRangeBeforeExistingRange On successful completion, set to TRUE
        to indicate the new change is being applied to an existing record
        before the existing record's current range.  FALSE implies the
        change is either after the end of an existing record or is going to
        a new record.

 @return Pointer to the undo record, or NULL to indicate failure.
 */
PYORIWIN_CTRL_MLEDIT_UNDO
YoriWinMlEditGetUndoRecordForOp(
    __in PYORIWIN_CTRL_MLEDIT MlEdit,
    __in YORIWIN_CTRL_MLEDIT_UNDO_OP Op,
    __in YORI_ALLOC_SIZE_T FirstLine,
    __in YORI_ALLOC_SIZE_T FirstCharOffset,
    __in YORI_ALLOC_SIZE_T LastLine,
    __in YORI_ALLOC_SIZE_T LastCharOffset,
    __out PBOOLEAN NewRangeBeforeExistingRange
    )
{
    PYORI_LIST_ENTRY ListEntry;
    PYORIWIN_CTRL_MLEDIT_UNDO Undo = NULL;

    YoriWinMlEditClearRedo(MlEdit);

    *NewRangeBeforeExistingRange = FALSE;

    ListEntry = YoriLibGetNextListEntry(&MlEdit->Undo, NULL);
    if (ListEntry != NULL) {
        Undo = CONTAINING_RECORD(ListEntry, YORIWIN_CTRL_MLEDIT_UNDO, ListEntry);
        if (Undo->Op != Op) {
            Undo = NULL;
        } else {
            switch (Op) {
                case YoriWinMlEditUndoInsertText:
                    if (!YoriWinMlEditRangeNext(MlEdit,
                                                Undo->u.InsertText.LastLineToDelete,
                                                Undo->u.InsertText.LastCharOffsetToDelete,
                                                FirstLine,
                                                FirstCharOffset)) {
                        Undo = NULL;
                    }
                    break;
                case YoriWinMlEditUndoDeleteText:
                    if (YoriWinMlEditRangeBefore(MlEdit,
                                                 Undo->u.DeleteText.FirstLine,
                                                 Undo->u.DeleteText.FirstCharOffset,
                                                 LastLine,
                                                 LastCharOffset)) {
                        *NewRangeBeforeExistingRange = TRUE;
                    } else if (!YoriWinMlEditRangeNext(MlEdit,
                                                       Undo->u.DeleteText.FirstLine,
                                                       Undo->u.DeleteText.FirstCharOffset,
                                                       FirstLine,
                                                       FirstCharOffset)) {
                        Undo = NULL;
                    }
                    break;
                case YoriWinMlEditUndoOverwriteText:
                    if (!YoriWinMlEditRangeNext(MlEdit,
                                                Undo->u.OverwriteText.LastLineToDelete,
                                                Undo->u.OverwriteText.LastCharOffsetModified,
                                                FirstLine,
                                                FirstCharOffset)) {
                        Undo = NULL;
                    }
                    break;
                default:
                    Undo = NULL;
                    break;
            }
        }
    }

    if (Undo == NULL) {
        Undo = YoriLibMalloc(sizeof(YORIWIN_CTRL_MLEDIT_UNDO));
        if (Undo == NULL) {
            YoriWinMlEditClearUndo(MlEdit);
            return NULL;
        }

        ZeroMemory(Undo, sizeof(YORIWIN_CTRL_MLEDIT_UNDO));
        YoriLibInsertList(&MlEdit->Undo, &Undo->ListEntry);

        Undo->Op = Op;

        switch(Op) {
            case YoriWinMlEditUndoInsertText:
                Undo->u.InsertText.FirstLineToDelete = FirstLine;
                Undo->u.InsertText.FirstCharOffsetToDelete = FirstCharOffset;
                Undo->u.InsertText.LastLineToDelete = LastLine;
                Undo->u.InsertText.LastCharOffsetToDelete = LastCharOffset;
                break;
            case YoriWinMlEditUndoDeleteText:
                Undo->u.DeleteText.FirstLine = FirstLine;
                Undo->u.DeleteText.FirstCharOffset = FirstCharOffset;
                YoriLibInitEmptyString(&Undo->u.DeleteText.Text);
                break;
            case YoriWinMlEditUndoOverwriteText:
                Undo->u.OverwriteText.FirstLineToDelete = FirstLine;
                Undo->u.OverwriteText.FirstCharOffsetToDelete = FirstCharOffset;
                Undo->u.OverwriteText.LastLineToDelete = LastLine;
                Undo->u.OverwriteText.LastCharOffsetToDelete = LastCharOffset;
                Undo->u.OverwriteText.FirstLine = FirstLine;
                Undo->u.OverwriteText.FirstCharOffset = FirstCharOffset;
                Undo->u.OverwriteText.FirstCharOffsetModified = FirstCharOffset;
                Undo->u.OverwriteText.LastCharOffsetModified = LastCharOffset;
                YoriLibInitEmptyString(&Undo->u.OverwriteText.Text);
                break;
        }
    }
    return Undo;
}

/**
 If a change needs to be saved so that it can be undone, the change may be
 before or after a previous change that should be undone in the same
 operation (consider when the user hits backspace or del.)  In order to do
 this, new text may need to be saved before or after previously saved text.
 Here a string is allocated where the range used is in the middle of the
 allocation, allowing characters to be inserted before or after it by
 adjusting the start pointer and length of the string.  Clearly if it is
 continually modified, it may also need to be reallocated periodically, but
 not for each key press.

 @param CombinedString Pointer to a string which contains the current saved
        text.  The StartOfString and LengthInChars members specify the current
        saved text, and the gap before can be found from the difference
        between MemoryToFree and StartOfString, and the gap afterwards from
        LengthAllocated and LengthInChars.

 @param CharsNeeded Specifies the number of new characters that should be
        added.

 @param CharsBefore TRUE if the new characters should be added before existing
        text, FALSE if the new characters should be added after the existing
        text.

 @param Substring On successful completion, populated with a string for the
        caller to write their new changes in the correct place.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
__success(return)
BOOLEAN
YoriWinMlEditEnsureBufferAroundStr(
    __in PYORI_STRING CombinedString,
    __in YORI_ALLOC_SIZE_T CharsNeeded,
    __in BOOLEAN CharsBefore,
    __out PYORI_STRING Substring
    )
{
    YORI_ALLOC_SIZE_T CurrentCharsBefore;
    YORI_ALLOC_SIZE_T CurrentCharsAfter;
    YORI_ALLOC_SIZE_T LengthNeeded;
    YORI_STRING Temp;

    CurrentCharsBefore = (YORI_ALLOC_SIZE_T)(CombinedString->StartOfString - (LPTSTR)CombinedString->MemoryToFree);
    CurrentCharsAfter = CombinedString->LengthAllocated - CurrentCharsBefore - CombinedString->LengthInChars;

    while(TRUE) {

        if (CharsBefore) {
            if (CharsNeeded <= CurrentCharsBefore) {
                CombinedString->StartOfString = CombinedString->StartOfString - CharsNeeded;
                CombinedString->LengthInChars = CombinedString->LengthInChars + CharsNeeded;
                YoriLibInitEmptyString(Substring);
                Substring->StartOfString = CombinedString->StartOfString;
                Substring->LengthInChars = CharsNeeded;
                return TRUE;
            }
        } else {
            if (CharsNeeded <= CurrentCharsAfter) {
                YoriLibInitEmptyString(Substring);
                Substring->StartOfString = CombinedString->StartOfString + CombinedString->LengthInChars;
                Substring->LengthInChars = CharsNeeded;
                CombinedString->LengthInChars = CombinedString->LengthInChars + CharsNeeded;
                return TRUE;
            }
        }

        //
        //  Allocate an extra 1Kb before and after in the hope that repeated
        //  keystrokes won't cause new allocations and copies.
        //

        CurrentCharsBefore = CurrentCharsAfter = 0x400;
        if (CharsBefore) {
            CurrentCharsBefore = CurrentCharsBefore + CharsNeeded;
        } else {
            CurrentCharsAfter = CurrentCharsAfter + CharsNeeded;
        }

        LengthNeeded = CurrentCharsBefore + CombinedString->LengthInChars + CurrentCharsAfter;
        if (!YoriLibAllocateString(&Temp, LengthNeeded)) {
            return FALSE;
        }

        Temp.StartOfString = Temp.StartOfString + CurrentCharsBefore;

        memcpy(Temp.StartOfString,
               CombinedString->StartOfString,
               CombinedString->LengthInChars * sizeof(TCHAR));

        Temp.LengthInChars = CombinedString->LengthInChars;
        YoriLibFreeStringContents(CombinedString);
        memcpy(CombinedString, &Temp, sizeof(YORI_STRING));
    }
}

/**
 Return TRUE to indicate that there are records specifying how to undo
 previous operations.

 @param CtrlHandle Pointer to the multiline edit control.

 @return TRUE if there are operations available to undo, FALSE if there
         are not.
 */
BOOLEAN
YoriWinMlEditIsUndoAvailable(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle
    )
{
    PYORIWIN_CTRL_MLEDIT MlEdit;
    PYORIWIN_CTRL Ctrl;

    Ctrl = (PYORIWIN_CTRL)CtrlHandle;
    MlEdit = CONTAINING_RECORD(Ctrl, YORIWIN_CTRL_MLEDIT, Ctrl);

    if (!YoriLibIsListEmpty(&MlEdit->Undo)) {
        return TRUE;
    }

    return FALSE;
}

/**
 Return TRUE to indicate that there are records specifying how to redo
 previous operations.

 @param CtrlHandle Pointer to the multiline edit control.

 @return TRUE if there are operations available to redo, FALSE if there
         are not.
 */
BOOLEAN
YoriWinMlEditIsRedoAvailable(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle
    )
{
    PYORIWIN_CTRL_MLEDIT MlEdit;
    PYORIWIN_CTRL Ctrl;

    Ctrl = (PYORIWIN_CTRL)CtrlHandle;
    MlEdit = CONTAINING_RECORD(Ctrl, YORIWIN_CTRL_MLEDIT, Ctrl);

    if (!YoriLibIsListEmpty(&MlEdit->Redo)) {
        return TRUE;
    }

    return FALSE;
}

__success(return)
BOOLEAN
YoriWinMlEditGetTextRange(
    __in PYORIWIN_CTRL_MLEDIT MlEdit,
    __in YORI_ALLOC_SIZE_T FirstLine,
    __in YORI_ALLOC_SIZE_T FirstCharOffset,
    __in YORI_ALLOC_SIZE_T LastLine,
    __in YORI_ALLOC_SIZE_T LastCharOffset,
    __in PYORI_STRING NewlineString,
    __out PYORI_STRING SelectedText
    );

__success(return)
BOOLEAN
YoriWinMlEditDeleteTextRange(
    __in PYORIWIN_CTRL_MLEDIT MlEdit,
    __in BOOLEAN ProcessingBackspace,
    __in BOOLEAN ProcessingUndo,
    __in BOOLEAN ChainWithNext,
    __in YORI_ALLOC_SIZE_T FirstLine,
    __in YORI_ALLOC_SIZE_T FirstCharOffset,
    __in YORI_ALLOC_SIZE_T LastLine,
    __in YORI_ALLOC_SIZE_T LastCharOffset
    );

__success(return)
BOOLEAN
YoriWinMlEditInsertTextRange(
    __in PYORIWIN_CTRL_MLEDIT MlEdit,
    __in BOOLEAN ProcessingUndo,
    __in YORI_ALLOC_SIZE_T FirstLine,
    __in YORI_ALLOC_SIZE_T FirstCharOffset,
    __in PYORI_STRING Text,
    __out PYORI_ALLOC_SIZE_T LastLine,
    __out PYORI_ALLOC_SIZE_T LastCharOffset
    );

VOID
YoriWinMlEditCalcEndingPointOfText(
    __in YORI_ALLOC_SIZE_T FirstLine,
    __in YORI_ALLOC_SIZE_T FirstCharOffset,
    __in PYORI_STRING Text,
    __out PYORI_ALLOC_SIZE_T LastLine,
    __out PYORI_ALLOC_SIZE_T LastCharOffset
    );

/**
 Given an undo record, generate a record that would undo the undo.

 @param MlEdit Pointer to the multiline edit control containing the
        state of the buffer before the undo record has been applied.

 @param Undo Pointer to an undo record indicating changes to perform.

 @param AddToUndoList If FALSE, the resulting undo of the undo should be added
        to the Redo list.  If TRUE, the undo here is already a redo, so the
        undo of the undo (of the undo) goes onto the undo list.

 @return Pointer to a newly allocated undo of the undo record.
 */
PYORIWIN_CTRL_MLEDIT_UNDO
YoriWinMlEditGenRedoRecordForUndo(
    __in PYORIWIN_CTRL_MLEDIT MlEdit,
    __in PYORIWIN_CTRL_MLEDIT_UNDO Undo,
    __in BOOLEAN AddToUndoList
    )
{
    PYORIWIN_CTRL_MLEDIT_UNDO Redo;
    YORI_STRING Newline;

    //
    //  Note that clearing all undo may remove the undo record that is
    //  causing this redo.  This implies that when this call fails, the
    //  caller cannot continue using the undo record.
    //

    Redo = YoriLibMalloc(sizeof(YORIWIN_CTRL_MLEDIT_UNDO));
    if (Redo == NULL) {
        YoriWinMlEditClearUndo(MlEdit);
        return NULL;
    }

    ZeroMemory(Redo, sizeof(YORIWIN_CTRL_MLEDIT_UNDO));
    if (AddToUndoList) {
        YoriLibInsertList(&MlEdit->Undo, &Redo->ListEntry);
    } else {
        YoriLibInsertList(&MlEdit->Redo, &Redo->ListEntry);
    }

    switch(Undo->Op) {
        case YoriWinMlEditUndoInsertText:
            Redo->Op = YoriWinMlEditUndoDeleteText;
            Redo->u.DeleteText.FirstLine = Undo->u.InsertText.FirstLineToDelete;
            Redo->u.DeleteText.FirstCharOffset = Undo->u.InsertText.FirstCharOffsetToDelete;
            YoriLibConstantString(&Newline, _T("\n"));
            if (!YoriWinMlEditGetTextRange(MlEdit,
                                           Undo->u.InsertText.FirstLineToDelete,
                                           Undo->u.InsertText.FirstCharOffsetToDelete,
                                           Undo->u.InsertText.LastLineToDelete,
                                           Undo->u.InsertText.LastCharOffsetToDelete,
                                           &Newline,
                                           &Redo->u.DeleteText.Text)) {
                YoriWinMlEditClearUndo(MlEdit);
                return NULL;
            }

            break;
        case YoriWinMlEditUndoDeleteText:
            Redo->Op = YoriWinMlEditUndoInsertText;
            Redo->u.InsertText.FirstLineToDelete = Undo->u.DeleteText.FirstLine;
            Redo->u.InsertText.FirstCharOffsetToDelete = Undo->u.DeleteText.FirstCharOffset;
            YoriWinMlEditCalcEndingPointOfText(Undo->u.DeleteText.FirstLine,
                                                    Undo->u.DeleteText.FirstCharOffset,
                                                    &Undo->u.DeleteText.Text,
                                                    &Redo->u.InsertText.LastLineToDelete,
                                                    &Redo->u.InsertText.LastCharOffsetToDelete);
            break;
        case YoriWinMlEditUndoOverwriteText:

            Redo->Op = Undo->Op;
            Redo->u.OverwriteText.FirstLineToDelete = Undo->u.OverwriteText.FirstLine;
            Redo->u.OverwriteText.FirstCharOffsetToDelete = Undo->u.OverwriteText.FirstCharOffset;
            YoriWinMlEditCalcEndingPointOfText(Undo->u.OverwriteText.FirstLine,
                                                    Undo->u.OverwriteText.FirstCharOffset,
                                                    &Undo->u.OverwriteText.Text,
                                                    &Redo->u.OverwriteText.LastLineToDelete,
                                                    &Redo->u.OverwriteText.LastCharOffsetToDelete);
            Redo->u.OverwriteText.FirstLine = Undo->u.OverwriteText.FirstLineToDelete;
            Redo->u.OverwriteText.FirstCharOffset = Undo->u.OverwriteText.FirstCharOffsetToDelete;

            YoriLibConstantString(&Newline, _T("\n"));
            if (!YoriWinMlEditGetTextRange(MlEdit,
                                           Undo->u.OverwriteText.FirstLineToDelete,
                                           Undo->u.OverwriteText.FirstCharOffsetToDelete,
                                           Undo->u.OverwriteText.LastLineToDelete,
                                           Undo->u.OverwriteText.LastCharOffsetToDelete,
                                           &Newline,
                                           &Redo->u.OverwriteText.Text)) {
                YoriWinMlEditClearUndo(MlEdit);
                return NULL;
            }

            Redo->u.OverwriteText.FirstCharOffsetModified = Undo->u.OverwriteText.FirstCharOffsetModified;
            Redo->u.OverwriteText.LastCharOffsetModified = Undo->u.OverwriteText.FirstCharOffsetModified;

            break;
    }

    return Redo;
}

/**
 Modify the buffer of the control per the direction of an undo record.

 @param MlEdit Pointer to the multiline edit control indicating the
        buffer and cursor position.

 @param Undo Pointer to the undo record indicating the changes to perform.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinMlEditApplyUndoRecord(
    __in PYORIWIN_CTRL_MLEDIT MlEdit,
    __in PYORIWIN_CTRL_MLEDIT_UNDO Undo
    )
{
    BOOLEAN Success;
    YORI_ALLOC_SIZE_T NewLastLine;
    YORI_ALLOC_SIZE_T NewLastCharOffset;

    Success = FALSE;
    switch(Undo->Op) {
        case YoriWinMlEditUndoInsertText:
            Success = YoriWinMlEditDeleteTextRange(MlEdit,
                                                   FALSE,
                                                   TRUE,
                                                   FALSE,
                                                   Undo->u.InsertText.FirstLineToDelete,
                                                   Undo->u.InsertText.FirstCharOffsetToDelete,
                                                   Undo->u.InsertText.LastLineToDelete,
                                                   Undo->u.InsertText.LastCharOffsetToDelete);
            if (Success) {
                YoriWinMlEditSetCursorPointInt(MlEdit,
                                               Undo->u.InsertText.FirstCharOffsetToDelete,
                                               Undo->u.InsertText.FirstLineToDelete);
            }
            break;
        case YoriWinMlEditUndoDeleteText:
            Success = YoriWinMlEditInsertTextRange(MlEdit,
                                                   TRUE,
                                                   Undo->u.DeleteText.FirstLine,
                                                   Undo->u.DeleteText.FirstCharOffset,
                                                   &Undo->u.DeleteText.Text,
                                                   &NewLastLine,
                                                   &NewLastCharOffset);
            if (Success) {
                YoriWinMlEditSetCursorPointInt(MlEdit,
                                               NewLastCharOffset,
                                               NewLastLine);
            }
            break;
        case YoriWinMlEditUndoOverwriteText:
            NewLastLine = 0;
            NewLastCharOffset = 0;
            Success = YoriWinMlEditDeleteTextRange(MlEdit,
                                                   FALSE,
                                                   TRUE,
                                                   FALSE,
                                                   Undo->u.OverwriteText.FirstLineToDelete,
                                                   Undo->u.OverwriteText.FirstCharOffsetToDelete,
                                                   Undo->u.OverwriteText.LastLineToDelete,
                                                   Undo->u.OverwriteText.LastCharOffsetToDelete);
            if (Success) {
                Success = YoriWinMlEditInsertTextRange(MlEdit,
                                                       TRUE,
                                                       Undo->u.OverwriteText.FirstLine,
                                                       Undo->u.OverwriteText.FirstCharOffset,
                                                       &Undo->u.OverwriteText.Text,
                                                       &NewLastLine,
                                                       &NewLastCharOffset);
            }
            if (Success) {
                YoriWinMlEditSetCursorPointInt(MlEdit,
                                               Undo->u.OverwriteText.FirstCharOffsetModified,
                                               Undo->u.OverwriteText.FirstLine);
            }
            break;
    }

    return Success;
}

/**
 Undo the most recent change to a multiline edit control.

 @param CtrlHandle Pointer to the multiline edit control.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinMlEditUndo(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle
    )
{
    PYORIWIN_CTRL_MLEDIT_UNDO Undo = NULL;
    PYORIWIN_CTRL_MLEDIT_UNDO Redo;
    PYORIWIN_CTRL_MLEDIT MlEdit;
    PYORIWIN_CTRL Ctrl;
    BOOLEAN Success;
    BOOLEAN ChainWithNext;
    BOOLEAN ChainWithPrevious;

    Ctrl = (PYORIWIN_CTRL)CtrlHandle;
    MlEdit = CONTAINING_RECORD(Ctrl, YORIWIN_CTRL_MLEDIT, Ctrl);

    Success = FALSE;
    ChainWithPrevious = FALSE;

    do {
        if (YoriLibIsListEmpty(&MlEdit->Undo)) {
            break;
        }

        Undo = CONTAINING_RECORD(MlEdit->Undo.Next, YORIWIN_CTRL_MLEDIT_UNDO, ListEntry);
        ChainWithNext = Undo->ChainWithNext;

        Redo = YoriWinMlEditGenRedoRecordForUndo(MlEdit, Undo, FALSE);
        if (Redo == NULL) {
            break;
        }

        if (ChainWithPrevious) {
            Redo->ChainWithNext = TRUE;
        }

        Success = YoriWinMlEditApplyUndoRecord(MlEdit, Undo);

        if (Success) {
            YoriLibRemoveListItem(&Undo->ListEntry);
            YoriWinMlEditFreeSingleUndo(Undo);
        } else {
            YoriLibRemoveListItem(&Redo->ListEntry);
            YoriWinMlEditFreeSingleUndo(Redo);
        }

        ChainWithPrevious = TRUE;

    } while (Success && ChainWithNext);

    return Success;
}

/**
 Redo the most recently undone change to a multiline edit control.

 @param CtrlHandle Pointer to the multiline edit control.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinMlEditRedo(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle
    )
{
    PYORIWIN_CTRL_MLEDIT_UNDO Undo = NULL;
    PYORIWIN_CTRL_MLEDIT_UNDO Redo;
    PYORIWIN_CTRL_MLEDIT MlEdit;
    PYORIWIN_CTRL Ctrl;
    BOOLEAN Success;
    BOOLEAN ChainWithNext;
    BOOLEAN ChainWithPrevious;

    Ctrl = (PYORIWIN_CTRL)CtrlHandle;
    MlEdit = CONTAINING_RECORD(Ctrl, YORIWIN_CTRL_MLEDIT, Ctrl);

    Success = FALSE;
    ChainWithPrevious = FALSE;

    do {

        if (YoriLibIsListEmpty(&MlEdit->Redo)) {
            break;
        }

        Undo = CONTAINING_RECORD(MlEdit->Redo.Next, YORIWIN_CTRL_MLEDIT_UNDO, ListEntry);
        ChainWithNext = Undo->ChainWithNext;

        Redo = YoriWinMlEditGenRedoRecordForUndo(MlEdit, Undo, TRUE);
        if (Redo == NULL) {
            break;
        }

        if (ChainWithPrevious) {
            Redo->ChainWithNext = TRUE;
        }

        Success = YoriWinMlEditApplyUndoRecord(MlEdit, Undo);

        if (Success) {
            YoriLibRemoveListItem(&Undo->ListEntry);
            YoriWinMlEditFreeSingleUndo(Undo);
        } else {
            YoriLibRemoveListItem(&Redo->ListEntry);
            YoriWinMlEditFreeSingleUndo(Redo);
        }

        ChainWithPrevious = TRUE;

    } while (Success && ChainWithNext);

    return Success;
}

//
//  =========================================
//  BUFFER MANIPULATION FUNCTIONS
//  =========================================
//

/**
 Find the length in characters needed to store a single continuous string
 covering the specified range in a multiline edit control.

 @param MlEdit Pointer to the multiline edit control containing the
        contents of the buffer.

 @param FirstLine Specifies the line that contains the first character to
        return.

 @param FirstCharOffset Specifies the offset within FirstLine of the first
        character to return.

 @param LastLine Specifies the line that contains the last character to
        return.

 @param LastCharOffset Specifies the offset beyond the last character to
        return.

 @param NewlineLength Specifies the number of characters in each newline.

 @return The number of characters needed to contain the requested range.
 */
DWORD
YoriWinMlEditGetTextRangeLength(
    __in PYORIWIN_CTRL_MLEDIT MlEdit,
    __in YORI_ALLOC_SIZE_T FirstLine,
    __in YORI_ALLOC_SIZE_T FirstCharOffset,
    __in YORI_ALLOC_SIZE_T LastLine,
    __in YORI_ALLOC_SIZE_T LastCharOffset,
    __in YORI_ALLOC_SIZE_T NewlineLength
    )
{
    DWORD CharsInRange;
    YORI_ALLOC_SIZE_T LinesInRange;
    YORI_ALLOC_SIZE_T LineIndex;

    if (FirstLine == LastLine) {
        ASSERT(LastCharOffset >= FirstCharOffset);
        CharsInRange = 0;
        if (FirstCharOffset >= MlEdit->LineArray[FirstLine].LengthInChars) {
            CharsInRange = 0;
        } else if (LastCharOffset >= MlEdit->LineArray[FirstLine].LengthInChars) {
            CharsInRange = MlEdit->LineArray[FirstLine].LengthInChars - FirstCharOffset;
        } else {
            CharsInRange = LastCharOffset - FirstCharOffset;
        }
    } else {
        LinesInRange = LastLine - FirstLine;
        CharsInRange = 0;
        if (FirstCharOffset < MlEdit->LineArray[FirstLine].LengthInChars) {
            CharsInRange += MlEdit->LineArray[FirstLine].LengthInChars - FirstCharOffset;
        }
        for (LineIndex = FirstLine + 1; LineIndex < LastLine; LineIndex++) {
            CharsInRange += MlEdit->LineArray[LineIndex].LengthInChars;
        }
        if (LastCharOffset < MlEdit->LineArray[LineIndex].LengthInChars) {
            CharsInRange += LastCharOffset;
        } else {
            CharsInRange += MlEdit->LineArray[LineIndex].LengthInChars;
        }
        CharsInRange += LinesInRange * NewlineLength;
    }

    return CharsInRange;
}

/**
 Count the leading whitespace characters in a line and return a substring
 that can be used to apply an indentation to a later line.

 @param Line Specifies the contents of the line that contains the indentation
        string to return.

 @param Indent On successful completion, updated to point to the substring
        within the line that consists of initial white space characters.
 */
VOID
YoriWinMlEditGetIndentOnString(
    __in PCYORI_STRING Line,
    __out PYORI_STRING Indent
    )
{
    YORI_ALLOC_SIZE_T Index;

    YoriLibInitEmptyString(Indent);
    for (Index = 0; Index < Line->LengthInChars; Index++) {
        if (Line->StartOfString[Index] != ' ' &&
            Line->StartOfString[Index] != '\t') {

            break;
        }
    }
    if (Index > 0) {
        Indent->StartOfString = Line->StartOfString;
        Indent->LengthInChars = Index;
    }
}

/**
 Count the leading whitespace characters in a line and return a substring
 that can be used to apply an indentation to a later line.

 @param MlEdit Pointer to the multiline edit control containing the
        contents of the buffer.

 @param LineIndex Specifies the line that contains the indentation string
        to return.

 @param Indent On successful completion, updated to point to the substring
        within the line that consists of initial white space characters.
 */
VOID
YoriWinMlEditGetIndentOnLine(
    __in PYORIWIN_CTRL_MLEDIT MlEdit,
    __in YORI_ALLOC_SIZE_T LineIndex,
    __out PYORI_STRING Indent
    )
{
    PCYORI_STRING Line;

    Line = &MlEdit->LineArray[LineIndex];
    YoriWinMlEditGetIndentOnString(Line, Indent);
}

/**
 When an auto indent has been applied to a line and the backspace key is
 pressed, search backwards through previous lines to find one that contains
 less indentation than the current match, and return that line index along
 with the new indentation to apply.

 @param MlEdit Pointer to the multiline edit control containing the
        contents of the buffer.

 @param NewLine On successful completion, updated to indicate the line
        index containing the new indentation.

 @param NewIndent On successful completion, updated to point to a substring
        within the NewLine buffer containing the new indentation to apply.
        This should be a subset of the current indentation, up to zero
        characters.
 */
VOID
YoriWinMlEditFindPreviousIndentLine(
    __in PYORIWIN_CTRL_MLEDIT MlEdit,
    __out PYORI_ALLOC_SIZE_T NewLine,
    __out PYORI_STRING NewIndent
    )
{
    YORI_ALLOC_SIZE_T ProbeLine;
    PCYORI_STRING ProbeLineString;
    YORI_STRING ProbeIndent;
    YORI_STRING CurrentIndent;
    YORI_ALLOC_SIZE_T MatchingLength;

    ASSERT(MlEdit->AutoIndentApplied);
    YoriWinMlEditGetIndentOnLine(MlEdit, MlEdit->AutoIndentAppliedLine, &CurrentIndent);
    ASSERT(CurrentIndent.LengthInChars >= MlEdit->AutoIndentSourceLength);
    CurrentIndent.LengthInChars = MlEdit->AutoIndentSourceLength;

    //
    //  Count backwards from one prior to the current auto indent line up
    //  to the first
    //

    for (ProbeLine = MlEdit->AutoIndentAppliedLine; ProbeLine > 0; ProbeLine--) {
        ProbeLineString = &MlEdit->LineArray[ProbeLine - 1];
        if (ProbeLineString->LengthInChars > 0) {
            YoriWinMlEditGetIndentOnString(ProbeLineString, &ProbeIndent);
            MatchingLength = YoriLibCntStringMatchChars(&CurrentIndent, &ProbeIndent);
            if (MatchingLength < CurrentIndent.LengthInChars) {
                *NewLine = ProbeLine - 1;
                ProbeIndent.LengthInChars = MatchingLength;
                memcpy(NewIndent, &ProbeIndent, sizeof(YORI_STRING));
                return;
            }
        }
    }

    *NewLine = 0;
    YoriLibInitEmptyString(NewIndent);
}

/**
 Build a single continuous string covering the specified range in a multiline
 edit control and store it in a preallocated allocation.

 @param MlEdit Pointer to the multiline edit control containing the
        contents of the buffer.

 @param FirstLine Specifies the line that contains the first character to
        return.

 @param FirstCharOffset Specifies the offset within FirstLine of the first
        character to return.

 @param LastLine Specifies the line that contains the last character to
        return.

 @param LastCharOffset Specifies the offset beyond the last character to
        return.

 @param NewlineString Specifies the string to use to delimit lines.  This
        allows this routine to return text with any arbitrary line ending.

 @param SelectedText On input, contains an allocated string that's large
        enough to contain the requested text.  On successful completion,
        populated with the selected text.  The caller is expected to free
        this buffer with @ref YoriLibFreeStringContents .
 */
VOID
YoriWinMlEditPopulateTextRange(
    __in PYORIWIN_CTRL_MLEDIT MlEdit,
    __in YORI_ALLOC_SIZE_T FirstLine,
    __in YORI_ALLOC_SIZE_T FirstCharOffset,
    __in YORI_ALLOC_SIZE_T LastLine,
    __in YORI_ALLOC_SIZE_T LastCharOffset,
    __in PYORI_STRING NewlineString,
    __inout PYORI_STRING SelectedText
    )
{
    YORI_ALLOC_SIZE_T CharsInRange;
    YORI_ALLOC_SIZE_T LineIndex;
    PYORI_STRING Line;
    LPTSTR Ptr;

    Line = &MlEdit->LineArray[FirstLine];

    if (FirstLine == LastLine) {
        if (FirstCharOffset > Line->LengthInChars) {
            CharsInRange = 0;
        } else if (LastCharOffset > Line->LengthInChars) {
            CharsInRange = Line->LengthInChars - FirstCharOffset;
        } else {
            CharsInRange = LastCharOffset - FirstCharOffset;
        }

        if (CharsInRange > 0) {
            memcpy(SelectedText->StartOfString, &Line->StartOfString[FirstCharOffset], CharsInRange * sizeof(TCHAR));
        }
        SelectedText->LengthInChars = CharsInRange;
    } else {
        Ptr = SelectedText->StartOfString;
        if (FirstCharOffset < Line->LengthInChars) {
            memcpy(Ptr, &Line->StartOfString[FirstCharOffset], (Line->LengthInChars - FirstCharOffset) * sizeof(TCHAR));
            Ptr += (Line->LengthInChars - FirstCharOffset);
        }
        for (LineIndex = FirstLine + 1; LineIndex < LastLine; LineIndex++) {
            memcpy(Ptr, NewlineString->StartOfString, NewlineString->LengthInChars * sizeof(TCHAR));
            Ptr += NewlineString->LengthInChars;
            memcpy(Ptr,
                   MlEdit->LineArray[LineIndex].StartOfString,
                   MlEdit->LineArray[LineIndex].LengthInChars * sizeof(TCHAR));
            Ptr += MlEdit->LineArray[LineIndex].LengthInChars;
        }
        memcpy(Ptr, NewlineString->StartOfString, NewlineString->LengthInChars * sizeof(TCHAR));
        Ptr += NewlineString->LengthInChars;
        if (LastCharOffset < MlEdit->LineArray[LastLine].LengthInChars) {
            CharsInRange = LastCharOffset;
        } else {
            CharsInRange = MlEdit->LineArray[LastLine].LengthInChars;
        }
        memcpy(Ptr, MlEdit->LineArray[LastLine].StartOfString, CharsInRange * sizeof(TCHAR));
        Ptr += LastCharOffset;

        SelectedText->LengthInChars = (YORI_ALLOC_SIZE_T)(Ptr - SelectedText->StartOfString);
    }
}

/**
 Build a single continuous string covering the specified range in a multiline
 edit control and return it in a new allocation.

 @param MlEdit Pointer to the multiline edit control containing the
        contents of the buffer.

 @param FirstLine Specifies the line that contains the first character to
        return.

 @param FirstCharOffset Specifies the offset within FirstLine of the first
        character to return.

 @param LastLine Specifies the line that contains the last character to
        return.

 @param LastCharOffset Specifies the offset beyond the last character to
        return.

 @param NewlineString Specifies the string to use to delimit lines.  This
        allows this routine to return text with any arbitrary line ending.

 @param SelectedText On successful completion, populated with a newly
        allocated buffer containing the selected text.  The caller is
        expected to free this buffer with @ref YoriLibFreeStringContents .

 @return TRUE to indicate success, FALSE to indicate failure.
 */
__success(return)
BOOLEAN
YoriWinMlEditGetTextRange(
    __in PYORIWIN_CTRL_MLEDIT MlEdit,
    __in YORI_ALLOC_SIZE_T FirstLine,
    __in YORI_ALLOC_SIZE_T FirstCharOffset,
    __in YORI_ALLOC_SIZE_T LastLine,
    __in YORI_ALLOC_SIZE_T LastCharOffset,
    __in PYORI_STRING NewlineString,
    __out PYORI_STRING SelectedText
    )
{
    DWORD CharsInRange;

    CharsInRange = YoriWinMlEditGetTextRangeLength(MlEdit,
                                                   FirstLine,
                                                   FirstCharOffset,
                                                   LastLine,
                                                   LastCharOffset,
                                                   NewlineString->LengthInChars);

    CharsInRange = CharsInRange + 1;
    if (!YoriLibIsSizeAllocatable(CharsInRange)) {
        return FALSE;
    }

    if (!YoriLibAllocateString(SelectedText, (YORI_ALLOC_SIZE_T)CharsInRange)) {
        return FALSE;
    }

    YoriWinMlEditPopulateTextRange(MlEdit,
                                   FirstLine,
                                   FirstCharOffset,
                                   LastLine,
                                   LastCharOffset,
                                   NewlineString,
                                   SelectedText);
    SelectedText->StartOfString[CharsInRange - 1] = '\0';

    return TRUE;
}


/**
 Delete a range of characters, which may span lines.  This is used when
 deleting a selection.  When deleting ranges that are not entire lines,
 this implies merging the end of one line with the beginning of another.

 @param MlEdit Pointer to the multiline edit control containing the
        contents of the buffer.

 @param ProcessingBackspace If TRUE, this delete is a response to the
        backspace key, which means any auto indent that has previously
        been applied should remain in effect.  Backspace processing should
        clear this explicitly if the indentation has been reduced to zero.
        If FALSE, this routine is considered a buffer modification that
        invalidates auto indent.

 @param ProcessingUndo If TRUE, this delete is being invoked by undo and
        should not try to create or maintain an undo entry.

 @param ChainWithNext If TRUE, when this delete is undone, the next operation
        should be undone with it.  This allows combined insert + delete
        operations to be combined for undo.  If FALSE, this delete is a normal
        standalone operation to undo.  If ProcessingUndo is TRUE, this
        parameter is meaningless.

 @param FirstLine Specifies the line that contains the first character to
        remove.

 @param FirstCharOffset Specifies the offset within FirstLine of the first
        character to remove.

 @param LastLine Specifies the line that contains the last character to
        remove.

 @param LastCharOffset Specifies the offset beyond the last character to
        remove.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
__success(return)
BOOLEAN
YoriWinMlEditDeleteTextRange(
    __in PYORIWIN_CTRL_MLEDIT MlEdit,
    __in BOOLEAN ProcessingBackspace,
    __in BOOLEAN ProcessingUndo,
    __in BOOLEAN ChainWithNext,
    __in YORI_ALLOC_SIZE_T FirstLine,
    __in YORI_ALLOC_SIZE_T FirstCharOffset,
    __in YORI_ALLOC_SIZE_T LastLine,
    __in YORI_ALLOC_SIZE_T LastCharOffset
    )
{
    PYORIWIN_CTRL_MLEDIT_UNDO Undo = NULL;
    YORI_ALLOC_SIZE_T CharsToCopy;
    YORI_ALLOC_SIZE_T CharsToDelete;
    YORI_ALLOC_SIZE_T LinesToDelete;
    YORI_ALLOC_SIZE_T LinesToCopy;
    YORI_ALLOC_SIZE_T FirstLineIndexToKeep;
    YORI_ALLOC_SIZE_T LineIndexToDelete;
    PYORI_STRING Line;
    PYORI_STRING FinalLine;

    if (!ProcessingBackspace) {
        MlEdit->AutoIndentApplied = FALSE;
    }

    //
    //  In the majority of cases, selection ought to be cleared due to the
    //  input that triggered the operation.  It's cleared here
    //  unconditionally though because maintaining the same text range as
    //  selected implies manipulating the selection range to reflect
    //  changes earlier in the buffer (eg. if lines 3-5 are selected and
    //  line 2 is deleted, selection logically moves up.)  Sometimes all
    //  text selected is being deleted, and the selection would need to
    //  be cleared anyway.
    //

    YoriWinMlEditClearSelection(MlEdit);

    if (!ProcessingUndo) {
        BOOLEAN RangeBeforeExistingRange;
        Undo = YoriWinMlEditGetUndoRecordForOp(MlEdit,
                                               YoriWinMlEditUndoDeleteText,
                                               FirstLine,
                                               FirstCharOffset,
                                               LastLine,
                                               LastCharOffset,
                                               &RangeBeforeExistingRange);
        if (Undo != NULL) {
            YORI_STRING Newline;
            YORI_STRING Text;
            DWORD CharsNeeded;
            YoriLibConstantString(&Newline, _T("\n"));

            CharsNeeded = YoriWinMlEditGetTextRangeLength(MlEdit,
                                                          FirstLine,
                                                          FirstCharOffset,
                                                          LastLine,
                                                          LastCharOffset,
                                                          Newline.LengthInChars);

            if (!YoriLibIsSizeAllocatable(CharsNeeded)) {
                return FALSE;
            }

            if (!YoriWinMlEditEnsureBufferAroundStr(&Undo->u.DeleteText.Text,
                                                    (YORI_ALLOC_SIZE_T)CharsNeeded,
                                                    RangeBeforeExistingRange,
                                                    &Text)) {
                return FALSE;
            }

            YoriWinMlEditPopulateTextRange(MlEdit,
                                           FirstLine,
                                           FirstCharOffset,
                                           LastLine,
                                           LastCharOffset,
                                           &Newline,
                                           &Text);

            if (RangeBeforeExistingRange) {
                Undo->u.DeleteText.FirstLine = FirstLine;
                Undo->u.DeleteText.FirstCharOffset = FirstCharOffset;
            }

            if (ChainWithNext) {
                Undo->ChainWithNext = TRUE;
            }
        }
    }

    Line = &MlEdit->LineArray[FirstLine];

    //
    //  If the selection is one line, this is a simple case, because no
    //  line combining is required.
    //

    if (FirstLine == LastLine) {

        if (FirstCharOffset >= LastCharOffset || FirstCharOffset > Line->LengthInChars) {
            return TRUE;
        }

        if (LastCharOffset > Line->LengthInChars) {
            CharsToDelete = Line->LengthInChars - FirstCharOffset;
            CharsToCopy = 0;
        } else {
            CharsToDelete = LastCharOffset - FirstCharOffset;
            CharsToCopy = Line->LengthInChars - LastCharOffset;
        }

        if (CharsToCopy > 0) {
            memmove(&Line->StartOfString[FirstCharOffset],
                    &Line->StartOfString[LastCharOffset],
                    CharsToCopy * sizeof(TCHAR));
        }

        Line->LengthInChars = Line->LengthInChars - CharsToDelete;
        YoriWinMlEditExpandDirtyRange(MlEdit, FirstLine, FirstLine);
        MlEdit->UserModified = TRUE;
        return TRUE;
    }

    LinesToDelete = 0;
    ASSERT(LastLine < MlEdit->LinesPopulated ||
           (LastLine == MlEdit->LinesPopulated && LastCharOffset == 0));
    if (LastLine < MlEdit->LinesPopulated) {
        FinalLine = &MlEdit->LineArray[LastLine];
    } else {
        FinalLine = NULL;
    }

    if (FinalLine != NULL && LastCharOffset < FinalLine->LengthInChars) {
        CharsToCopy = FinalLine->LengthInChars - LastCharOffset;
    } else {
        CharsToCopy = 0;
    }

    //
    //  If the first part of the first line and the last part of the last
    //  line (the unselected regions of each) don't fit in the first line's
    //  allocation, reallocate it.
    //

    if (FinalLine != NULL &&
        FirstCharOffset + CharsToCopy > Line->LengthAllocated) {

        YORI_STRING NewLine;
        YORI_ALLOC_SIZE_T CharsFromFirstLine;
        if (!YoriLibAllocateString(&NewLine, FirstCharOffset + CharsToCopy + YORIWIN_MLEDIT_LINE_PADDING)) {
            return FALSE;
        }

        if (FirstCharOffset < Line->LengthInChars) {
            CharsFromFirstLine = FirstCharOffset;
        } else {
            CharsFromFirstLine = Line->LengthInChars;
        }

        memcpy(NewLine.StartOfString, Line->StartOfString, CharsFromFirstLine * sizeof(TCHAR));
        while (CharsFromFirstLine < FirstCharOffset) {
            NewLine.StartOfString[CharsFromFirstLine] = ' ';
            CharsFromFirstLine++;
        }

        NewLine.LengthInChars = FirstCharOffset;
        YoriLibFreeStringContents(Line);
        memcpy(Line, &NewLine, sizeof(YORI_STRING));
    }

    //
    //  Move the combined regions into one line.
    //

    if (CharsToCopy > 0) {
        memcpy(&Line->StartOfString[FirstCharOffset],
               &FinalLine->StartOfString[LastCharOffset],
               CharsToCopy * sizeof(TCHAR));
    }

    //
    //  Delete any completely selected lines.
    //

    Line->LengthInChars = FirstCharOffset + CharsToCopy;
    if (LastLine < MlEdit->LinesPopulated) {
        LinesToDelete = LastLine - FirstLine;
    } else {
        if (FirstLine + 1 < MlEdit->LinesPopulated) {
            LinesToDelete = MlEdit->LinesPopulated - 1 - FirstLine;
        } else {
            LinesToDelete = 0;
        }
    }

    for (LineIndexToDelete = 0; LineIndexToDelete < LinesToDelete; LineIndexToDelete++) {
        YoriLibFreeStringContents(&MlEdit->LineArray[FirstLine + 1 + LineIndexToDelete]);
    }

    FirstLineIndexToKeep = FirstLine + 1 + LinesToDelete;
    if (FirstLineIndexToKeep < MlEdit->LinesPopulated) {
        LinesToCopy = MlEdit->LinesPopulated - FirstLineIndexToKeep;
    } else {
        LinesToCopy = 0;
    }

    if (LinesToCopy > 0) {
        memmove(&MlEdit->LineArray[FirstLine + 1],
                &MlEdit->LineArray[FirstLine + 1 + LinesToDelete],
                LinesToCopy * sizeof(YORI_STRING));
    }

    YoriWinMlEditExpandDirtyRange(MlEdit, FirstLine, MlEdit->LinesPopulated);
    MlEdit->UserModified = TRUE;

    MlEdit->LinesPopulated = MlEdit->LinesPopulated - LinesToDelete;

    return TRUE;
}

/**
 Given a starting location and a pile of text, determine the ending point of
 the pile of text.

 @param FirstLine Specifies the first line of the text.

 @param FirstCharOffset Specifies the offset within the line to start
        inserting text.

 @param Text Pointer to the text to insert.

 @param LastLine On completion, updated to indicate the final line containing
        the text.

 @param LastCharOffset On completion, updated to indicate the character
        following the final character in the text.
 */
VOID
YoriWinMlEditCalcEndingPointOfText(
    __in YORI_ALLOC_SIZE_T FirstLine,
    __in YORI_ALLOC_SIZE_T FirstCharOffset,
    __in PYORI_STRING Text,
    __out PYORI_ALLOC_SIZE_T LastLine,
    __out PYORI_ALLOC_SIZE_T LastCharOffset
    )
{
    YORI_ALLOC_SIZE_T LineCount;
    YORI_ALLOC_SIZE_T Index;
    YORI_ALLOC_SIZE_T LineCharCount;

    //
    //  Count the number of lines in the input text.  This may be zero.
    //

    LineCount = 0;
    LineCharCount = FirstCharOffset;
    for (Index = 0; Index < Text->LengthInChars; Index++) {
        if (Text->StartOfString[Index] == '\r') {
            LineCount++;
            if (Index + 1 < Text->LengthInChars &&
                Text->StartOfString[Index + 1] == '\n') {
                Index++;
            }
            LineCharCount = 0;
        } else if (Text->StartOfString[Index] == '\n') {
            LineCount++;
            LineCharCount = 0;
        } else {
            LineCharCount++;
        }
    }

    *LastLine = FirstLine + LineCount;
    *LastCharOffset = LineCharCount;
}


/**
 Allocate new lines for the line array.  This is used when the number of lines
 in the file grows.  Note the allocations for the contents in each line are
 not performed here.

 @param MlEdit Pointer to the multiline edit control.

 @param LinesRequired Specifies the new minimum number of lines that the
        control should have allocated.

 @param LinesDesired Specifies the number of lines that should be allocated if
        possible.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinMlEditReallocLineArray(
    __in PYORIWIN_CTRL_MLEDIT MlEdit,
    __in DWORD LinesRequired,
    __in DWORD LinesDesired
    )
{
    DWORD BytesRequired;
    DWORD BytesDesired;
    PYORI_STRING NewLineArray;
    YORI_ALLOC_SIZE_T BytesToAllocate;
    YORI_ALLOC_SIZE_T NewLineCount;

    ASSERT(LinesDesired >= LinesRequired);
    ASSERT(LinesRequired > MlEdit->LinesPopulated);

    BytesRequired = LinesRequired * sizeof(YORI_STRING);
    BytesDesired = LinesDesired * sizeof(YORI_STRING);

    BytesToAllocate = YoriLibMaximumAllocationInRange(BytesRequired, BytesDesired);
    if (BytesToAllocate == 0) {
        return FALSE;
    }

    NewLineCount = BytesToAllocate / sizeof(YORI_STRING);

    NewLineArray = YoriLibReferencedMalloc(NewLineCount * sizeof(YORI_STRING));
    if (NewLineArray == NULL) {
        return FALSE;
    }

    if (MlEdit->LinesPopulated > 0) {
        memcpy(NewLineArray, MlEdit->LineArray, MlEdit->LinesPopulated * sizeof(YORI_STRING));
        YoriLibDereference(MlEdit->LineArray);
    }

    MlEdit->LineArray = NewLineArray;
    MlEdit->LinesAllocated = NewLineCount;
    return TRUE;
}

/**
 Trim any autoindent back to the specified offset.

 @param MlEdit Pointer to the multiline edit control.

 @param LineIndex The line that may have an autoindent.

 @param MaxOffset The maximum amount of autoindent to retain.

 @return TRUE if autoindent was removed, FALSE if it was not.
 */
BOOLEAN
YoriWinMlEditTrimAutoIndent(
    __in PYORIWIN_CTRL_MLEDIT MlEdit,
    __in YORI_ALLOC_SIZE_T LineIndex,
    __in YORI_ALLOC_SIZE_T MaxOffset
    )
{
    PYORI_STRING Line;
    YORI_ALLOC_SIZE_T CharsToTruncate;
    BOOLEAN Result;

    if (!MlEdit->AutoIndentApplied) {
        return FALSE;
    }

    ASSERT(MlEdit->AutoIndentAppliedLine == LineIndex);
    if (MlEdit->AutoIndentAppliedLine != LineIndex) {
        return FALSE;
    }

    ASSERT(LineIndex < MlEdit->LinesPopulated);
    if (LineIndex >=MlEdit->LinesPopulated) {
        return FALSE;
    }

    Line = &MlEdit->LineArray[LineIndex];

    ASSERT(Line->LengthInChars == MlEdit->AutoIndentSourceLength);
    ASSERT(Line->LengthInChars != 0);
    if (Line->LengthInChars == 0) {
        MlEdit->AutoIndentApplied = FALSE;
        return FALSE;
    }

    ASSERT(MaxOffset < Line->LengthInChars);
    if (MaxOffset >= Line->LengthInChars) {
        return FALSE;
    }

    CharsToTruncate = Line->LengthInChars - MaxOffset;
    if (CharsToTruncate == 0) {
        return FALSE;
    }

    Result = YoriWinMlEditDeleteTextRange(MlEdit,
                                          TRUE,
                                          FALSE,
                                          FALSE,
                                          LineIndex,
                                          MaxOffset,
                                          LineIndex,
                                          MaxOffset + CharsToTruncate);
    if (Result) {
        MlEdit->AutoIndentSourceLength = MaxOffset;
        if (MaxOffset == 0) {
            MlEdit->AutoIndentApplied = FALSE;
        }
    }

    return Result;
}

/**
 Create new empty lines after an insertion point, and move all existing lines
 further down.

 @param MlEdit Pointer to the multiline edit control.

 @param FirstLine The line that insertion should happen after.

 @param LineCount The number of lines to insert.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinMlEditInsertLines(
    __in PYORIWIN_CTRL_MLEDIT MlEdit,
    __in YORI_ALLOC_SIZE_T FirstLine,
    __in YORI_ALLOC_SIZE_T LineCount
    )
{
    YORI_ALLOC_SIZE_T SourceLine;
    YORI_ALLOC_SIZE_T TargetLine;
    YORI_ALLOC_SIZE_T Index;
    DWORD LinesRequired;
    DWORD LinesDesired;

    LinesRequired = MlEdit->LinesPopulated;
    LinesRequired = LinesRequired + LineCount;
    if (MlEdit->LinesPopulated == 0) {
        LinesRequired++;
    }
    if (LinesRequired > MlEdit->LinesAllocated) {

        LinesDesired = MlEdit->LinesAllocated;
        LinesDesired = LinesDesired * 2;

        if (LinesDesired < LinesRequired) {
            LinesDesired = LinesRequired;
            LinesDesired = LinesDesired + 0x1000;
            LinesDesired = LinesDesired & ~(0xfff);
        } else if (LinesDesired < 0x1000) {
            LinesDesired = 0x1000;
        }

        if (!YoriWinMlEditReallocLineArray(MlEdit, LinesRequired, LinesDesired)) {
            return FALSE;
        }
    }

    if (MlEdit->LinesPopulated > 0) {
        SourceLine = FirstLine + 1;
        TargetLine = SourceLine + LineCount;
    } else {
        SourceLine = FirstLine;
        TargetLine = SourceLine + LineCount + 1;
    }
    if (SourceLine < MlEdit->LinesPopulated) {
        DWORD LinesToMove;
        LinesToMove = MlEdit->LinesPopulated - SourceLine;
        memmove(&MlEdit->LineArray[TargetLine],
                &MlEdit->LineArray[SourceLine],
                LinesToMove * sizeof(YORI_STRING));
    }

    for (Index = SourceLine; Index < TargetLine; Index++) {
        YoriLibInitEmptyString(&MlEdit->LineArray[Index]);
    }

    MlEdit->LinesPopulated = (YORI_ALLOC_SIZE_T)LinesRequired;
    return TRUE;
}

/**
 Insert a block of text, which may contain newlines, into the control at the
 specified position.  Currently, this happens in three scenarios: user input,
 clipboard paste, or undo.

 @param MlEdit Pointer to the multiline edit control.

 @param ProcessingUndo If TRUE, this insert is being invoked by undo and
        should not try to create or maintain an undo entry.

 @param FirstLine Specifies the line in the buffer where text should be
        inserted.

 @param FirstCharOffset Specifies the offset in the line where text should be
        inserted.

 @param Text Pointer to the text to insert.

 @param LastLine On successful completion, populated with the line containing
        the end of the newly inserted text.

 @param LastCharOffset On successful completion, populated with the offset
        beyond the newly inserted text.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
__success(return)
BOOLEAN
YoriWinMlEditInsertTextRange(
    __in PYORIWIN_CTRL_MLEDIT MlEdit,
    __in BOOLEAN ProcessingUndo,
    __in YORI_ALLOC_SIZE_T FirstLine,
    __in YORI_ALLOC_SIZE_T FirstCharOffset,
    __in PYORI_STRING Text,
    __out PYORI_ALLOC_SIZE_T LastLine,
    __out PYORI_ALLOC_SIZE_T LastCharOffset
    )
{
    PYORIWIN_CTRL_MLEDIT_UNDO Undo = NULL;
    YORI_ALLOC_SIZE_T LineCount;
    YORI_ALLOC_SIZE_T LineIndex;
    YORI_ALLOC_SIZE_T Index;
    YORI_ALLOC_SIZE_T CharsThisLine;
    YORI_ALLOC_SIZE_T CharsFirstLine;
    YORI_ALLOC_SIZE_T CharsLastLine;
    YORI_ALLOC_SIZE_T CharsNeeded;
    YORI_ALLOC_SIZE_T LocalLastLine;
    YORI_ALLOC_SIZE_T LocalLastCharOffset;
    YORI_STRING TrailingPortionOfFirstLine;
    YORI_STRING AutoIndentLeadingString;
    PYORI_STRING Line;
    BOOLEAN TerminateLine;
    BOOLEAN TruncateFirstLine;

    TruncateFirstLine = FALSE;
    YoriLibInitEmptyString(&AutoIndentLeadingString);

    //
    //  Count the number of lines in the input text.  This may be zero.
    //

    YoriWinMlEditCalcEndingPointOfText(FirstLine, FirstCharOffset, Text, &LocalLastLine, &LocalLastCharOffset);
    LineCount = LocalLastLine - FirstLine;

    //
    //  If auto indent is in effect, and the text ends on the beginning of
    //  a subsequent line, calculate any indentation prefix.
    //

    if (LineCount > 0 &&
        LocalLastCharOffset == 0 &&
        MlEdit->AutoIndent &&
        !ProcessingUndo &&
        FirstLine < MlEdit->LinesPopulated) {

        YoriWinMlEditGetIndentOnLine(MlEdit, FirstLine, &AutoIndentLeadingString);

        //
        //  Truncate if a line is being inserted within the indent itself.
        //  This is to avoid double-indenting - the new line will contain the
        //  trailing portion of the current line's indent, and auto indent
        //  should only fill in the leading portion, which may be empty.
        //

        if (FirstCharOffset < AutoIndentLeadingString.LengthInChars) {
            AutoIndentLeadingString.LengthInChars = FirstCharOffset;
        }
        LocalLastCharOffset = AutoIndentLeadingString.LengthInChars;

        //
        //  If the first line is entirely autoindent, and the first character
        //  is a newline, that indicates the autoindent should be removed.
        //  Do this last, along with other first line manipulations, which
        //  helps ensure the indent string is still available when needed.
        //

        if (AutoIndentLeadingString.LengthInChars > 0 && Text->LengthInChars > 0) {
            TCHAR FirstChar;
            Line = &MlEdit->LineArray[FirstLine];
            FirstChar = Text->StartOfString[0];
            if (AutoIndentLeadingString.LengthInChars == Line->LengthInChars &&
                (FirstChar == '\n' || FirstChar == '\r')) {

                TruncateFirstLine = TRUE;
            }
        }
    }

    //
    //  If new lines are being added, check if the line array is large
    //  enough and reallocate as needed.  Even if lines are already
    //  allocated, the current lines need to be moved downwards to make room
    //  for the lines that are about to be inserted.
    //

    if (LineCount > 0 || MlEdit->LinesPopulated == 0) {
        if (!YoriWinMlEditInsertLines(MlEdit, FirstLine, LineCount)) {
            return FALSE;
        }
    }

    //
    //  Record pointers to the string following the cursor on the current
    //  cursor line.  This text needs to be logically moved to the end of
    //  the newly inserted text, which is on a new line.  To achieve this
    //  the current text is pointed to, and the first line is processed
    //  last, after the last line has been constructed.
    //

    YoriLibInitEmptyString(&TrailingPortionOfFirstLine);
    if (FirstLine < MlEdit->LinesPopulated) {
        Line = &MlEdit->LineArray[FirstLine];
        if (FirstCharOffset < Line->LengthInChars) {
            ASSERT(Line->MemoryToFree != NULL);
            YoriLibReference(Line->MemoryToFree);
            TrailingPortionOfFirstLine.MemoryToFree = Line->MemoryToFree;
            TrailingPortionOfFirstLine.StartOfString = &Line->StartOfString[FirstCharOffset];
            TrailingPortionOfFirstLine.LengthInChars = Line->LengthInChars - FirstCharOffset;
        }
    }

    MlEdit->AutoIndentApplied = FALSE;

    //
    //  Go through each line.  For all lines except the first, construct the
    //  new line.  Note that these lines should be empty lines due to the
    //  line rearrangement above.
    //

    LineIndex = 0;
    CharsThisLine = 0;
    CharsFirstLine = 0;
    CharsLastLine = 0;
    TerminateLine = FALSE;
    for (Index = 0; Index <= Text->LengthInChars; Index++) {

        //
        //  Look for end of line, and treat end of string as end of line
        //

        if (Index == Text->LengthInChars) {
            TerminateLine = TRUE;
        } else if (Text->StartOfString[Index] == '\r' ||
                   Text->StartOfString[Index] == '\n') {

            TerminateLine = TRUE;
        }

        if (TerminateLine) {

            //
            //  On the end of the first line, make a note of where the string
            //  is.  This is done so the trailing portion of the current text
            //  in the first line can be moved to the end of the last line
            //  without needing to reallocate it
            //

            if (LineIndex == 0) {
                CharsFirstLine = CharsThisLine;
                if (LineCount == 0) {
                    CharsLastLine = CharsThisLine;
                }
            } else {
                Line = &MlEdit->LineArray[FirstLine + LineIndex];
                ASSERT(Line->LengthInChars == 0);
                CharsNeeded = CharsThisLine;
                if (LineIndex == LineCount) {
                    CharsNeeded += AutoIndentLeadingString.LengthInChars + TrailingPortionOfFirstLine.LengthInChars;
                }
                if (Line->LengthAllocated < CharsNeeded) {
                    YoriLibFreeStringContents(Line);
                    if (!YoriLibAllocateString(Line, CharsNeeded + YORIWIN_MLEDIT_LINE_PADDING)) {
                        YoriLibFreeStringContents(&TrailingPortionOfFirstLine);
                        return FALSE;
                    }
                }

                //
                //  On the final line, apply any auto indent if needed.  Auto
                //  indent wouldn't make sense if new data is arriving.
                //

                Line->LengthInChars = 0;
                if (LineIndex == LineCount) {
                    if (AutoIndentLeadingString.LengthInChars > 0) {

                        ASSERT(CharsThisLine == 0);

                        memcpy(Line->StartOfString,
                               AutoIndentLeadingString.StartOfString,
                               AutoIndentLeadingString.LengthInChars * sizeof(TCHAR));
                        Line->LengthInChars = AutoIndentLeadingString.LengthInChars;
                        MlEdit->AutoIndentApplied = TRUE;
                        MlEdit->AutoIndentSourceLength = AutoIndentLeadingString.LengthInChars;
                        MlEdit->AutoIndentAppliedLine = LocalLastLine;
                    }
                }

                //
                //  Add the new text to the beginning of the line
                //

                if (CharsThisLine > 0) {
                    memcpy(&Line->StartOfString[Line->LengthInChars],
                           &Text->StartOfString[Index - CharsThisLine],
                           CharsThisLine * sizeof(TCHAR));
                }
                Line->LengthInChars = Line->LengthInChars + CharsThisLine;

                //
                //  On the final line, copy the final portion currently in the
                //  first line after the newly inserted text.  Save away the
                //  number of characters on this line so that the cursor can
                //  be positioned at that point.
                //

                if (LineIndex == LineCount) {
                    CharsLastLine = AutoIndentLeadingString.LengthInChars + CharsThisLine;
                    if (TrailingPortionOfFirstLine.LengthInChars > 0) {
                        memcpy(&Line->StartOfString[CharsThisLine + AutoIndentLeadingString.LengthInChars],
                               TrailingPortionOfFirstLine.StartOfString,
                               TrailingPortionOfFirstLine.LengthInChars * sizeof(TCHAR));
                        Line->LengthInChars = Line->LengthInChars + TrailingPortionOfFirstLine.LengthInChars;
                    }
                }
            }
            LineIndex++;
            CharsThisLine = 0;
            TerminateLine = FALSE;

            //
            //  Skip one extra char if this is a \r\n line
            //

            if (Index + 1 < Text->LengthInChars &&
                Text->StartOfString[Index] == '\r' &&
                Text->StartOfString[Index + 1] == '\n') {

                Index++;
            }
            continue;
        }

        CharsThisLine++;
    }

    //
    //  Because the first line was left unaltered in the regular loop to
    //  enable its text to be moved to the end of the last line, fix up
    //  the first line now.  If the first line is the same as the last
    //  line (LineCount == 0), we have to move the trailing portion after
    //  the newly inserted text.  Otherwise, that text is on a different
    //  line so we can completely ignore it.
    //
    //  We don't need any auto indent string anymore, since that can't
    //  apply to the first line (and only makes sense if multiple lines
    //  were present.)
    //

    if (LineCount != 0) {
        YoriLibFreeStringContents(&TrailingPortionOfFirstLine);
        YoriLibInitEmptyString(&TrailingPortionOfFirstLine);
        YoriLibInitEmptyString(&AutoIndentLeadingString);
    } else {
        //
        //  Autoindent shouldn't exist unless there are multiple lines.
        //

        ASSERT(AutoIndentLeadingString.StartOfString == NULL);
    }

    Line = &MlEdit->LineArray[FirstLine];
    if (FirstCharOffset + CharsFirstLine + TrailingPortionOfFirstLine.LengthInChars > Line->LengthAllocated) {
        if (!YoriLibReallocString(Line, FirstCharOffset + CharsFirstLine + TrailingPortionOfFirstLine.LengthInChars + YORIWIN_MLEDIT_LINE_PADDING)) {
            YoriLibFreeStringContents(&TrailingPortionOfFirstLine);
            return FALSE;
        }
    }

    if (TrailingPortionOfFirstLine.LengthInChars > 0) {
        memmove(&Line->StartOfString[FirstCharOffset + CharsFirstLine],
                TrailingPortionOfFirstLine.StartOfString,
                TrailingPortionOfFirstLine.LengthInChars * sizeof(TCHAR));
    }

    while (FirstCharOffset > Line->LengthInChars) {
        Line->StartOfString[Line->LengthInChars++] = ' ';
    }

    if (CharsFirstLine > 0) {
        memcpy(&Line->StartOfString[FirstCharOffset], Text->StartOfString, CharsFirstLine * sizeof(TCHAR));
    }
    Line->LengthInChars = FirstCharOffset + CharsFirstLine + TrailingPortionOfFirstLine.LengthInChars;

    YoriLibFreeStringContents(&TrailingPortionOfFirstLine);

    if (LineCount > 0) {
        YoriWinMlEditExpandDirtyRange(MlEdit, FirstLine, (YORI_ALLOC_SIZE_T)-1);
        ASSERT(LocalLastCharOffset == CharsLastLine);
    } else {
        YoriWinMlEditExpandDirtyRange(MlEdit, FirstLine, FirstLine);
        ASSERT(LocalLastCharOffset == FirstCharOffset + CharsFirstLine);
    }

    if (!ProcessingUndo) {
        BOOLEAN RangeBeforeExistingRange;
        Undo = YoriWinMlEditGetUndoRecordForOp(MlEdit,
                                               YoriWinMlEditUndoInsertText,
                                               FirstLine,
                                               FirstCharOffset,
                                               LocalLastLine,
                                               LocalLastCharOffset,
                                               &RangeBeforeExistingRange);
        if (Undo != NULL) {
            Undo->u.InsertText.LastLineToDelete = LocalLastLine;
            Undo->u.InsertText.LastCharOffsetToDelete = LocalLastCharOffset;
        }

        //
        //  Truncate the first line if it was entirely autoindent.  This
        //  generates its own undo record so doesn't need to be performed
        //  implicitly during undo.
        //

        if (TruncateFirstLine) {
            Line = &MlEdit->LineArray[FirstLine];
            YoriWinMlEditDeleteTextRange(MlEdit,
                                         TRUE,
                                         FALSE,
                                         TRUE,
                                         FirstLine,
                                         0,
                                         FirstLine,
                                         Line->LengthInChars);
        }
    }

    //
    //  Set the cursor to be after the newly inserted range.
    //

    *LastLine = LocalLastLine;
    *LastCharOffset = LocalLastCharOffset;
    MlEdit->UserModified = TRUE;

    return TRUE;
}

/**
 Overwrite a block of text, which may contain newlines, into the control at the
 specified position.  Note that "overwrite" in this context refers to adding
 text with insert mode off.  This is not a true/strict overwrite, because the
 semantics of typing with insert off is that new lines are inserted, but text
 on an existing line are overwritten.

 @param MlEdit Pointer to the multiline edit control.

 @param ProcessingUndo If TRUE, this overwrite is being invoked by undo and
        should not try to create or maintain an undo entry.

 @param FirstLine Specifies the line in the buffer where text should be
        added.

 @param FirstCharOffset Specifies the offset in the line where text should be
        added.

 @param Text Pointer to the text to add.

 @param LastLine On successful completion, populated with the line containing
        the end of the newly added text.

 @param LastCharOffset On successful completion, populated with the offset
        beyond the newly added text.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
__success(return)
BOOLEAN
YoriWinMlEditOverwriteTextRange(
    __in PYORIWIN_CTRL_MLEDIT MlEdit,
    __in BOOLEAN ProcessingUndo,
    __in YORI_ALLOC_SIZE_T FirstLine,
    __in YORI_ALLOC_SIZE_T FirstCharOffset,
    __in PYORI_STRING Text,
    __out PYORI_ALLOC_SIZE_T LastLine,
    __out PYORI_ALLOC_SIZE_T LastCharOffset
    )
{
    PYORIWIN_CTRL_MLEDIT_UNDO Undo = NULL;
    YORI_ALLOC_SIZE_T LineCount;
    YORI_ALLOC_SIZE_T LineIndex;
    YORI_ALLOC_SIZE_T Index;
    YORI_ALLOC_SIZE_T CharsThisLine;
    YORI_ALLOC_SIZE_T CharsLastLine;
    YORI_ALLOC_SIZE_T CharsNeeded;
    YORI_ALLOC_SIZE_T StartOffsetThisLine;
    YORI_ALLOC_SIZE_T LocalLastLine;
    YORI_ALLOC_SIZE_T LocalLastCharOffset;
    PYORI_STRING Line;
    BOOLEAN TerminateLine;
    BOOLEAN MoveTrailingTextToNextLine;

    MlEdit->AutoIndentApplied = FALSE;

    if (!ProcessingUndo) {
        BOOLEAN RangeBeforeExistingRange;

        //
        //  At this point we don't know the ending range for this text but it
        //  doesn't matter.  An overwrite will only extend a previous one, not
        //  occur before it, so the end range specified here can be bogus.
        //

        Undo = YoriWinMlEditGetUndoRecordForOp(MlEdit,
                                               YoriWinMlEditUndoOverwriteText,
                                               FirstLine,
                                               FirstCharOffset,
                                               FirstLine,
                                               FirstCharOffset,
                                               &RangeBeforeExistingRange);
        if (Undo != NULL) {

            //
            //  If this is a new record, save off the entire line to be
            //  deleted and restored.  This is done to ensure it doesn't
            //  need to be manipulated on each keypress.  It also means if
            //  the user starts a new line, the delete range can be expanded
            //  while leaving the restore range alone.
            //

            if (Undo->u.OverwriteText.Text.StartOfString == NULL) {
                Line = &MlEdit->LineArray[FirstLine];
                if (!YoriLibCopyString(&Undo->u.OverwriteText.Text, Line)) {
                    return FALSE;
                }

                Undo->u.OverwriteText.FirstLineToDelete = FirstLine;
                Undo->u.OverwriteText.FirstCharOffsetToDelete = 0;
                Undo->u.OverwriteText.LastLineToDelete = FirstLine;
                Undo->u.OverwriteText.LastCharOffsetToDelete = Line->LengthInChars;
                Undo->u.OverwriteText.FirstLine = FirstLine;
                Undo->u.OverwriteText.FirstCharOffset = 0;
            }
        }
    }

    //
    //  Count the number of lines in the input text.  This may be zero.
    //

    YoriWinMlEditCalcEndingPointOfText(FirstLine, FirstCharOffset, Text, &LocalLastLine, &LocalLastCharOffset);
    LineCount = LocalLastLine - FirstLine;

    //
    //  If new lines are being added, check if the line array is large
    //  enough and reallocate as needed.  Even if lines are already
    //  allocated, the current lines need to be moved downwards to make room
    //  for the lines that are about to be inserted.
    //

    if (LineCount > 0 || MlEdit->LinesPopulated == 0) {
        if (!YoriWinMlEditInsertLines(MlEdit, FirstLine, LineCount)) {
            return FALSE;
        }
        if (Undo != NULL) {
            Undo->u.OverwriteText.LastLineToDelete = FirstLine + LineCount;
            Undo->u.OverwriteText.LastCharOffsetToDelete = 0;
        }
    }

    //
    //  Go through each line.  Construct the new line.  For all lines except
    //  the first, these lines should be empty lines due to the line
    //  rearrangement above.
    //

    LineIndex = 0;
    CharsThisLine = 0;
    CharsLastLine = 0;
    TerminateLine = FALSE;
    MoveTrailingTextToNextLine = FALSE;
    for (Index = 0; Index <= Text->LengthInChars; Index++) {

        //
        //  Look for end of line, and treat end of string as end of line
        //

        if (Index == Text->LengthInChars) {
            TerminateLine = TRUE;
            MoveTrailingTextToNextLine = FALSE;
        } else if (Text->StartOfString[Index] == '\r' ||
                   Text->StartOfString[Index] == '\n') {

            TerminateLine = TRUE;
            MoveTrailingTextToNextLine = TRUE;
        }

        if (TerminateLine) {

            //
            //  On the end of the first line, make a note of where the string
            //  is.
            //

            StartOffsetThisLine = 0;
            if (LineIndex == 0) {
                StartOffsetThisLine = FirstCharOffset;
            }

            Line = &MlEdit->LineArray[FirstLine + LineIndex];
            CharsNeeded = StartOffsetThisLine + CharsThisLine;
            if (Line->LengthAllocated < CharsNeeded) {
                YoriLibFreeStringContents(Line);
                if (!YoriLibAllocateString(Line, CharsNeeded + YORIWIN_MLEDIT_LINE_PADDING)) {
                    return FALSE;
                }
            }

            while (StartOffsetThisLine > Line->LengthInChars) {
                Line->StartOfString[Line->LengthInChars++] = ' ';
            }

            if (CharsThisLine > 0) {
                memcpy(&Line->StartOfString[StartOffsetThisLine],
                       &Text->StartOfString[Index - CharsThisLine],
                       CharsThisLine * sizeof(TCHAR));
            }

            //
            //  If the line is extending, extend it.  If enter was pressed,
            //  move any contents after this point to the next line.
            //

            if (StartOffsetThisLine + CharsThisLine > Line->LengthInChars) {
                Line->LengthInChars = StartOffsetThisLine + CharsThisLine;
            } else if (MoveTrailingTextToNextLine && Line->LengthInChars > StartOffsetThisLine + CharsThisLine) {
                PYORI_STRING NextLine;
                NextLine = &MlEdit->LineArray[FirstLine + LineIndex + 1];
                ASSERT(NextLine->LengthInChars == 0);
                CharsNeeded = Line->LengthInChars - (StartOffsetThisLine + CharsThisLine);
                if (NextLine->LengthAllocated < CharsNeeded) {
                    YoriLibFreeStringContents(NextLine);
                    if (!YoriLibAllocateString(NextLine, CharsNeeded + YORIWIN_MLEDIT_LINE_PADDING)) {
                        return FALSE;
                    }
                }

                memcpy(NextLine->StartOfString,
                       &Line->StartOfString[StartOffsetThisLine + CharsThisLine],
                       CharsNeeded * sizeof(TCHAR));
                NextLine->LengthInChars = CharsNeeded;
                Line->LengthInChars = StartOffsetThisLine + CharsThisLine;
            }

            //
            //  Save away the number of characters on this line so that
            //  the cursor can be positioned at that point.  Update the undo
            //  record so that any modification after this point is attributed
            //  to the same undo record, and any changes made up to this point
            //  need to be deleted.
            //

            if (LineIndex == LineCount) {
                CharsLastLine = CharsThisLine;
                if (Undo != NULL) {
                    ASSERT(Undo->u.OverwriteText.LastLineToDelete == FirstLine + LineIndex);
                    Undo->u.OverwriteText.LastCharOffsetModified = StartOffsetThisLine + CharsLastLine;

                    if (Line->LengthInChars > Undo->u.OverwriteText.LastCharOffsetToDelete) {
                        Undo->u.OverwriteText.LastCharOffsetToDelete = Line->LengthInChars;
                    }
                }
            }

            LineIndex++;
            CharsThisLine = 0;
            TerminateLine = FALSE;

            //
            //  Skip one extra char if this is a \r\n line
            //

            if (Index + 1 < Text->LengthInChars &&
                Text->StartOfString[Index] == '\r' &&
                Text->StartOfString[Index + 1] == '\n') {

                Index++;
            }
            continue;
        }

        CharsThisLine++;
    }


    //
    //  Set the cursor to be after the newly inserted range.
    //

    if (LineCount > 0) {
        YoriWinMlEditExpandDirtyRange(MlEdit, FirstLine, (YORI_ALLOC_SIZE_T)-1);
        *LastLine = FirstLine + LineCount;
        *LastCharOffset = CharsLastLine;
        ASSERT(LocalLastLine == FirstLine + LineCount);
        ASSERT(LocalLastCharOffset == CharsLastLine);
    } else {
        YoriWinMlEditExpandDirtyRange(MlEdit, FirstLine, FirstLine);
        *LastLine = FirstLine;
        *LastCharOffset = FirstCharOffset + CharsLastLine;
        ASSERT(LocalLastLine == FirstLine + LineCount);
        ASSERT(LocalLastCharOffset == FirstCharOffset + CharsLastLine);
    }
    MlEdit->UserModified = TRUE;

    return TRUE;
}

/**
 Add an array of lines to the end of a multiline edit control.

 @param CtrlHandle Pointer to the multiline edit control.

 @param NewLines Pointer to an array of lines.

 @param NewLineCount Specifies the number of lines in the array.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinMlEditAddLinesNoDataCopy(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in PYORI_STRING NewLines,
    __in YORI_ALLOC_SIZE_T NewLineCount
    )
{
    PYORIWIN_CTRL_MLEDIT MlEdit;
    PYORIWIN_CTRL Ctrl;
    DWORD LinesRequired;
    DWORD LinesDesired;

    Ctrl = (PYORIWIN_CTRL)CtrlHandle;
    MlEdit = CONTAINING_RECORD(Ctrl, YORIWIN_CTRL_MLEDIT, Ctrl);

    YoriWinMlEditClearUndo(MlEdit);

    LinesRequired = MlEdit->LinesPopulated;
    LinesRequired = LinesRequired + NewLineCount;
    if (MlEdit->LinesPopulated == 0) {
        LinesRequired++;
    }

    if (LinesRequired > MlEdit->LinesAllocated) {

        LinesDesired = MlEdit->LinesAllocated;
        LinesDesired = LinesDesired * 2;

        if (LinesDesired < LinesRequired) {
            LinesDesired = LinesRequired;
            LinesDesired = LinesDesired + 0x1000;
            LinesDesired = LinesDesired & ~(0xfff);
        } else if (LinesDesired < 0x1000) {
            LinesDesired = 0x1000;
        }

        if (!YoriWinMlEditReallocLineArray(MlEdit, LinesRequired, LinesDesired)) {
            return FALSE;
        }
    }

    memcpy(&MlEdit->LineArray[MlEdit->LinesPopulated], NewLines, NewLineCount * sizeof(YORI_STRING));
    YoriWinMlEditExpandDirtyRange(MlEdit, MlEdit->LinesPopulated, MlEdit->LinesPopulated + NewLineCount);
    MlEdit->LinesPopulated = MlEdit->LinesPopulated + NewLineCount;

    YoriWinMlEditPaint(MlEdit);
    return TRUE;
}

/**
 If a selection is currently active, delete all text in the selection.
 This implies deleting multiple lines, and/or merging the end of one line
 with the beginning of another.

 @param CtrlHandle Pointer to the multiline edit control containing the
        selection and contents of the buffer.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinMlEditDeleteSelection(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle
    )
{
    PYORIWIN_CTRL_MLEDIT MlEdit;
    PYORIWIN_CTRL Ctrl;
    PYORIWIN_MLEDIT_SELECT Selection;
    YORI_ALLOC_SIZE_T FirstLine;
    YORI_ALLOC_SIZE_T FirstCharOffset;
    YORI_ALLOC_SIZE_T LastLine;
    YORI_ALLOC_SIZE_T LastCharOffset;

    Ctrl = (PYORIWIN_CTRL)CtrlHandle;
    MlEdit = CONTAINING_RECORD(Ctrl, YORIWIN_CTRL_MLEDIT, Ctrl);

    if (!YoriWinMlEditSelectionActive(&MlEdit->Ctrl)) {
        return TRUE;
    }

    Selection = &MlEdit->Selection;

    FirstLine = Selection->FirstLine;
    FirstCharOffset = Selection->FirstCharOffset;
    LastLine = Selection->LastLine;
    LastCharOffset = Selection->LastCharOffset;

    if (!YoriWinMlEditDeleteTextRange(MlEdit,
                                      FALSE,
                                      FALSE,
                                      FALSE,
                                      FirstLine,
                                      FirstCharOffset,
                                      LastLine,
                                      LastCharOffset)) {
        return FALSE;
    }

    YoriWinMlEditClearSelection(MlEdit);
    YoriWinMlEditSetCursorPointInt(MlEdit, FirstCharOffset, FirstLine);

    return TRUE;
}

/**
 Build a single continuous string covering the selected range in a multiline
 edit control.

 @param CtrlHandle Pointer to the multiline edit control containing the
        selection and contents of the buffer.

 @param NewlineString Specifies the string to use to delimit lines.  This
        allows this routine to return text with any arbitrary line ending.

 @param SelectedText On successful completion, populated with a newly
        allocated buffer containing the selected text.  The caller is
        expected to free this buffer with @ref YoriLibFreeStringContents .

 @return TRUE to indicate success, FALSE to indicate failure.
 */
__success(return)
BOOLEAN
YoriWinMlEditGetSelectedText(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in PYORI_STRING NewlineString,
    __out PYORI_STRING SelectedText
    )
{
    PYORIWIN_CTRL_MLEDIT MlEdit;
    PYORIWIN_CTRL Ctrl;
    PYORIWIN_MLEDIT_SELECT Selection;

    Ctrl = (PYORIWIN_CTRL)CtrlHandle;
    MlEdit = CONTAINING_RECORD(Ctrl, YORIWIN_CTRL_MLEDIT, Ctrl);

    if (!YoriWinMlEditSelectionActive(CtrlHandle)) {
        YoriLibInitEmptyString(SelectedText);
        return TRUE;
    }

    Selection = &MlEdit->Selection;

    return YoriWinMlEditGetTextRange(MlEdit,
                                     Selection->FirstLine,
                                     Selection->FirstCharOffset,
                                     Selection->LastLine,
                                     Selection->LastCharOffset,
                                     NewlineString,
                                     SelectedText);
}

/**
 Perform debug only checks to see that the selection state follows whatever
 rules are currently defined for it.

 @param MlEdit Pointer to the multiline edit control specifying the
        selection state.
 */
VOID
YoriWinMlEditCheckSelState(
    __in PYORIWIN_CTRL_MLEDIT MlEdit
    )
{
    PYORIWIN_MLEDIT_SELECT Selection;
    Selection = &MlEdit->Selection;

    if (Selection->Active  == YoriWinMlEditSelectNotActive) {
        return;
    }
    ASSERT(Selection->LastLine < MlEdit->LinesPopulated);
    ASSERT(Selection->FirstLine <= Selection->LastLine);
    if (Selection->Active == YoriWinMlEditSelMouseFromTopDown ||
        Selection->Active == YoriWinMlEditSelMouseFromBottomUp) {
        ASSERT(Selection->LastLine != Selection->FirstLine || Selection->FirstCharOffset <= Selection->LastCharOffset);
    } else {
        ASSERT(Selection->LastLine != Selection->FirstLine || Selection->FirstCharOffset < Selection->LastCharOffset);
    }
    ASSERT(Selection->FirstCharOffset <= MlEdit->LineArray[Selection->FirstLine].LengthInChars);
    ASSERT(Selection->LastCharOffset <= MlEdit->LineArray[Selection->LastLine].LengthInChars);
}

/**
 Start a new selection from the current cursor location if no selection is
 currently active.  If one is active, this call is ignored.

 @param MlEdit Pointer to the multiline edit control that describes
        the selection and cursor location.

 @param Mouse If TRUE, the selection is being initiated by mouse operations.
        If FALSE, the selection is being initiated by keyboard operations.
 */
VOID
YoriWinMlEditStartSelAtCursor(
    __in PYORIWIN_CTRL_MLEDIT MlEdit,
    __in BOOLEAN Mouse
    )
{
    PYORIWIN_MLEDIT_SELECT Selection;

    Selection = &MlEdit->Selection;

    //
    //  If a mouse selection is active and keyboard selection is requested
    //  or vice versa, clear the existing selection.
    //

    if (Mouse) {
        if (Selection->Active == YoriWinMlEditSelKbdFromTopDown ||
            Selection->Active == YoriWinMlEditSelKbdFromBottomUp ||
            Selection->Active == YoriWinMlEditSelMouseComplete) {

            YoriWinMlEditClearSelection(MlEdit);
        }
    } else {
        if (Selection->Active == YoriWinMlEditSelMouseFromTopDown ||
            Selection->Active == YoriWinMlEditSelMouseFromBottomUp ||
            Selection->Active == YoriWinMlEditSelMouseComplete) {

            YoriWinMlEditClearSelection(MlEdit);
        }
    }

    //
    //  If no selection is active, activate it.
    //

    if (Selection->Active == YoriWinMlEditSelectNotActive) {
        YORI_ALLOC_SIZE_T EffectiveCursorLine;
        YORI_ALLOC_SIZE_T EffectiveCursorOffset;
        EffectiveCursorLine = MlEdit->CursorLine;
        EffectiveCursorOffset = MlEdit->CursorOffset;
        if (MlEdit->LinesPopulated == 0) {
            EffectiveCursorLine = 0;
            EffectiveCursorOffset = 0;
        } else if (EffectiveCursorLine >= MlEdit->LinesPopulated) {

            EffectiveCursorLine = MlEdit->LinesPopulated - 1;
            EffectiveCursorOffset = MlEdit->LineArray[EffectiveCursorLine].LengthInChars;

        }

        if (EffectiveCursorLine < MlEdit->LinesPopulated) {
            if (EffectiveCursorOffset > MlEdit->LineArray[EffectiveCursorLine].LengthInChars) {
                EffectiveCursorOffset = MlEdit->LineArray[EffectiveCursorLine].LengthInChars;
            }
        }

        if (Mouse) {
            Selection->Active = YoriWinMlEditSelMouseFromTopDown;
        } else {
            Selection->Active = YoriWinMlEditSelKbdFromTopDown;
        }

        Selection->FirstLine = EffectiveCursorLine;
        Selection->FirstCharOffset = EffectiveCursorOffset;
        Selection->LastLine = EffectiveCursorLine;
        Selection->LastCharOffset = EffectiveCursorOffset;

        YoriWinMlEditExpandDirtyRange(MlEdit, Selection->FirstLine, Selection->LastLine);
    }
}

/**
 Modify a selection line.  The selection line could move forward or backward,
 and any gap needs to be redrawn.

 @param MlEdit Pointer to the multiline edit control.

 @param SelectionLine Pointer to a selection line value to update.

 @param NewValue Specifies the new value of the selection value.
 */
VOID
YoriWinMlEditSetSelectionLine(
    __in PYORIWIN_CTRL_MLEDIT MlEdit,
    __in PYORI_ALLOC_SIZE_T SelectionLine,
    __in YORI_ALLOC_SIZE_T NewValue
    )
{
    if (NewValue < *SelectionLine) {
        YoriWinMlEditExpandDirtyRange(MlEdit, NewValue, *SelectionLine);
    } else if (NewValue > *SelectionLine) {
        YoriWinMlEditExpandDirtyRange(MlEdit, *SelectionLine, NewValue);
    }

    *SelectionLine = NewValue;
}

/**
 Extend the current selection to the location of the cursor.

 @param MlEdit Pointer to the multiline edit control that describes
        the current selection and cursor location.
 */
VOID
YoriWinMlEditExtendSelectionToCursor(
    __in PYORIWIN_CTRL_MLEDIT MlEdit
    )
{
    YORI_ALLOC_SIZE_T AnchorLine;
    YORI_ALLOC_SIZE_T AnchorOffset;
    YORI_ALLOC_SIZE_T EffectiveCursorLine;
    YORI_ALLOC_SIZE_T EffectiveCursorOffset;
    BOOLEAN MouseSelection = FALSE;

    PYORIWIN_MLEDIT_SELECT Selection;

    AnchorLine = 0;
    AnchorOffset = 0;

    Selection = &MlEdit->Selection;

    //
    //  Find the place where the selection started from the user's point of
    //  view.  This might be the beginning or end of the selection in terms
    //  of its location in the buffer.
    //

    ASSERT(YoriWinMlEditSelectionActive(&MlEdit->Ctrl));
    if (Selection->Active == YoriWinMlEditSelKbdFromTopDown ||
        Selection->Active == YoriWinMlEditSelMouseFromTopDown) {

        AnchorLine = Selection->FirstLine;
        AnchorOffset = Selection->FirstCharOffset;

    } else if (Selection->Active == YoriWinMlEditSelKbdFromBottomUp ||
               Selection->Active == YoriWinMlEditSelMouseFromBottomUp) {

        AnchorLine = Selection->LastLine;
        AnchorOffset = Selection->LastCharOffset;

    } else {
        return;
    }

    if (Selection->Active == YoriWinMlEditSelMouseFromTopDown ||
        Selection->Active == YoriWinMlEditSelMouseFromBottomUp) {

        MouseSelection = TRUE;
    }

    //
    //  If there's no data, there's nothing to select
    //

    if (MlEdit->LinesPopulated == 0) {
        YoriWinMlEditClearSelection(MlEdit);
        return;
    }

    EffectiveCursorLine = MlEdit->CursorLine;
    EffectiveCursorOffset = MlEdit->CursorOffset;
    if (EffectiveCursorLine >= MlEdit->LinesPopulated) {
        EffectiveCursorLine = MlEdit->LinesPopulated - 1;
        EffectiveCursorOffset = MlEdit->LineArray[EffectiveCursorLine].LengthInChars;
    }

    if (EffectiveCursorOffset > MlEdit->LineArray[EffectiveCursorLine].LengthInChars) {
        EffectiveCursorOffset = MlEdit->LineArray[EffectiveCursorLine].LengthInChars;
    }

    if (EffectiveCursorLine < AnchorLine) {

        if (MouseSelection) {
            Selection->Active = YoriWinMlEditSelMouseFromBottomUp;
        } else {
            Selection->Active = YoriWinMlEditSelKbdFromBottomUp;
        }

        YoriWinMlEditSetSelectionLine(MlEdit, &Selection->LastLine, AnchorLine);
        Selection->LastCharOffset = AnchorOffset;
        YoriWinMlEditSetSelectionLine(MlEdit, &Selection->FirstLine, EffectiveCursorLine);
        Selection->FirstCharOffset = EffectiveCursorOffset;
        YoriWinMlEditExpandDirtyRange(MlEdit, EffectiveCursorLine, EffectiveCursorLine);

    } else if (EffectiveCursorLine > AnchorLine) {

        if (MouseSelection) {
            Selection->Active = YoriWinMlEditSelMouseFromTopDown;
        } else {
            Selection->Active = YoriWinMlEditSelKbdFromTopDown;
        }

        YoriWinMlEditSetSelectionLine(MlEdit, &Selection->FirstLine, AnchorLine);
        Selection->FirstCharOffset = AnchorOffset;
        YoriWinMlEditSetSelectionLine(MlEdit, &Selection->LastLine, EffectiveCursorLine);
        Selection->LastCharOffset = EffectiveCursorOffset;
        YoriWinMlEditExpandDirtyRange(MlEdit, EffectiveCursorLine, EffectiveCursorLine);

    } else {
        YoriWinMlEditSetSelectionLine(MlEdit, &Selection->FirstLine, AnchorLine);
        YoriWinMlEditSetSelectionLine(MlEdit, &Selection->LastLine, AnchorLine);
        YoriWinMlEditExpandDirtyRange(MlEdit, AnchorLine, AnchorLine);
        if (EffectiveCursorOffset < AnchorOffset) {

            if (MouseSelection) {
                Selection->Active = YoriWinMlEditSelMouseFromBottomUp;
            } else {
                Selection->Active = YoriWinMlEditSelKbdFromBottomUp;
            }

            Selection->LastCharOffset = AnchorOffset;
            Selection->FirstCharOffset = EffectiveCursorOffset;

        } else if (EffectiveCursorOffset > AnchorOffset) {

            if (MouseSelection) {
                Selection->Active = YoriWinMlEditSelMouseFromTopDown;
            } else {
                Selection->Active = YoriWinMlEditSelKbdFromTopDown;
            }

            Selection->FirstCharOffset = AnchorOffset;
            Selection->LastCharOffset = EffectiveCursorOffset;
        } else if (!MouseSelection) {
            YoriWinMlEditClearSelection(MlEdit);
        } else {
            Selection->LastCharOffset = AnchorOffset;
            Selection->FirstCharOffset = AnchorOffset;
        }
    }

    YoriWinMlEditCheckSelState(MlEdit);
}

/**
 End selection extension.  This is invoked when the mouse button is released.
 At this point, the user may have selected text (click, hold, drag) or have
 just moved the cursor (click and release.)  We don't know which case happened
 until the mouse button is released (ie., now.)

 @param MlEdit Pointer to the multiline edit control.
 */
VOID
YoriWinMlEditFinishMouseSel(
    __in PYORIWIN_CTRL_MLEDIT MlEdit
    )
{
    PYORIWIN_MLEDIT_SELECT Selection;

    MlEdit->MouseButtonDown = FALSE;

    Selection = &MlEdit->Selection;
    Selection->Active = YoriWinMlEditSelMouseComplete;

    //
    //  If no characters were selected, disable the selection
    //

    if (Selection->FirstLine == Selection->LastLine) {
        if (Selection->FirstCharOffset >= Selection->LastCharOffset) {
            Selection->Active = YoriWinMlEditSelectNotActive;
        }
    }

    if (MlEdit->Timer != NULL) {
        YoriWinMgrFreeTimer(MlEdit->Timer);
        MlEdit->Timer = NULL;
    }
}


/**
 Get the selection range within a multiline edit control.

 @param CtrlHandle Pointer to the multiline edit control.

 @param StartLine Specifies the line index of the beginning of the selection.

 @param StartOffset Specifies the character offset within the line to the
        beginning of the selection.

 @param EndLine Specifies the line index of the end of the selection.

 @param EndOffset Specifies the character offset within the line to the
        end of the selection.

 @return TRUE to indicate that the selection is active and a range has been
         returned.  FALSE to indicate no selection is active.
 */
__success(return)
BOOLEAN
YoriWinMlEditGetSelectionRange(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __out PYORI_ALLOC_SIZE_T StartLine,
    __out PYORI_ALLOC_SIZE_T StartOffset,
    __out PYORI_ALLOC_SIZE_T EndLine,
    __out PYORI_ALLOC_SIZE_T EndOffset
    )
{
    PYORIWIN_CTRL_MLEDIT MlEdit;

    if (!YoriWinMlEditSelectionActive(CtrlHandle)) {
        return FALSE;
    }

    MlEdit = (PYORIWIN_CTRL_MLEDIT)CtrlHandle;

    *StartLine = MlEdit->Selection.FirstLine;
    *StartOffset = MlEdit->Selection.FirstCharOffset;
    *EndLine = MlEdit->Selection.LastLine;
    *EndOffset = MlEdit->Selection.LastCharOffset;

    return TRUE;
}

/**
 Set the selection range within a multiline edit control to an explicitly
 provided range.

 @param CtrlHandle Pointer to the multiline edit control.

 @param StartLine Specifies the line index of the beginning of the selection.

 @param StartOffset Specifies the character offset within the line to the
        beginning of the selection.

 @param EndLine Specifies the line index of the end of the selection.

 @param EndOffset Specifies the character offset within the line to the
        end of the selection.
 */
VOID
YoriWinMlEditSetSelectionRange(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in YORI_ALLOC_SIZE_T StartLine,
    __in YORI_ALLOC_SIZE_T StartOffset,
    __in YORI_ALLOC_SIZE_T EndLine,
    __in YORI_ALLOC_SIZE_T EndOffset
    )
{
    PYORIWIN_CTRL_MLEDIT MlEdit;

    MlEdit = (PYORIWIN_CTRL_MLEDIT)CtrlHandle;

    YoriWinMlEditClearSelection(MlEdit);
    MlEdit->CursorLine = StartLine;
    MlEdit->CursorOffset = StartOffset;
    YoriWinMlEditStartSelAtCursor(MlEdit, FALSE);
    YoriWinMlEditSetCursorPointInt(MlEdit, EndOffset, EndLine);
    YoriWinMlEditExtendSelectionToCursor(MlEdit);
    YoriWinMlEditEnsureCursorShown(MlEdit);
    YoriWinMlEditPaint(MlEdit);
}

//
//  =========================================
//  CLIPBOARD FUNCTIONS
//  =========================================
//

/**
 Add the currently selected text to the clipboard and delete it from the
 buffer.

 @param CtrlHandle Pointer to the multiline edit control.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinMlEditCutSelectedText(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle
    )
{
    PYORIWIN_CTRL_MLEDIT MlEdit;
    PYORIWIN_CTRL Ctrl;
    YORI_STRING Newline;
    YORI_STRING Text;

    Ctrl = (PYORIWIN_CTRL)CtrlHandle;
    MlEdit = CONTAINING_RECORD(Ctrl, YORIWIN_CTRL_MLEDIT, Ctrl);
    YoriLibConstantString(&Newline, _T("\r\n"));
    YoriLibInitEmptyString(&Text);

    if (!YoriWinMlEditGetSelectedText(CtrlHandle, &Newline, &Text)) {
        return FALSE;
    }

    if (!YoriLibCopyTextProcFallback(&Text)) {
        YoriLibFreeStringContents(&Text);
        return FALSE;
    }

    YoriLibFreeStringContents(&Text);
    YoriWinMlEditDeleteSelection(CtrlHandle);
    return TRUE;
}

/**
 Add the currently selected text to the clipboard and clear the selection.

 @param CtrlHandle Pointer to the multiline edit control.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinMlEditCopySelectedText(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle
    )
{
    PYORIWIN_CTRL_MLEDIT MlEdit;
    PYORIWIN_CTRL Ctrl;
    YORI_STRING Newline;
    YORI_STRING Text;

    Ctrl = (PYORIWIN_CTRL)CtrlHandle;
    MlEdit = CONTAINING_RECORD(Ctrl, YORIWIN_CTRL_MLEDIT, Ctrl);
    YoriLibConstantString(&Newline, _T("\r\n"));
    YoriLibInitEmptyString(&Text);

    if (!YoriWinMlEditGetSelectedText(CtrlHandle, &Newline, &Text)) {
        return FALSE;
    }

    if (!YoriLibCopyTextProcFallback(&Text)) {
        YoriLibFreeStringContents(&Text);
        return FALSE;
    }

    YoriLibFreeStringContents(&Text);
    return TRUE;
}

/**
 Paste the text that is currently in the clipboard at the current cursor
 location.  Note this can update the cursor location.

 @param CtrlHandle Pointer to the multiline edit control.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinMlEditPasteText(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle
    )
{
    YORI_STRING Text;
    PYORI_STRING Line;
    PYORIWIN_CTRL_MLEDIT MlEdit;
    PYORIWIN_CTRL Ctrl;
    YORI_ALLOC_SIZE_T Index;

    Ctrl = (PYORIWIN_CTRL)CtrlHandle;
    MlEdit = CONTAINING_RECORD(Ctrl, YORIWIN_CTRL_MLEDIT, Ctrl);
    YoriLibInitEmptyString(&Text);

    if (YoriWinMlEditSelectionActive(CtrlHandle)) {
        YoriWinMlEditDeleteSelection(CtrlHandle);
    }

    if (!YoriLibPasteTextProcFallback(&Text)) {
        return FALSE;
    }
    if (MlEdit->AutoIndentApplied &&
        MlEdit->CursorLine == MlEdit->AutoIndentAppliedLine) {

        Line = &MlEdit->LineArray[MlEdit->CursorLine];

        for (Index = 0;
             Index < Line->LengthInChars &&
             Index < Text.LengthInChars &&
             Index < MlEdit->AutoIndentSourceLength &&
             Line->StartOfString[Index] == Text.StartOfString[Index];
             Index++);

        Text.StartOfString = Text.StartOfString + Index;
        Text.LengthInChars = Text.LengthInChars - Index;
    }

    if (!YoriWinMlEditInsertTextAtCursor(CtrlHandle, &Text)) {
        YoriLibFreeStringContents(&Text);
        return FALSE;
    }

    YoriWinMlEditEnsureCursorShown(MlEdit);
    YoriWinMlEditPaint(MlEdit);

    YoriLibFreeStringContents(&Text);
    return TRUE;
}

//
//  =========================================
//  GENERAL EXPORTED API FUNCTIONS
//  =========================================
//

/**
 Insert a block of text, which may contain newlines, into the control at the
 current cursor position.

 @param CtrlHandle Pointer to the multiline edit control.

 @param Text Pointer to the text to insert.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinMlEditInsertTextAtCursor(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in PYORI_STRING Text
    )
{
    PYORIWIN_CTRL_MLEDIT MlEdit;
    PYORIWIN_CTRL Ctrl;
    YORI_ALLOC_SIZE_T LastLine;
    YORI_ALLOC_SIZE_T LastCharOffset;

    Ctrl = (PYORIWIN_CTRL)CtrlHandle;
    MlEdit = CONTAINING_RECORD(Ctrl, YORIWIN_CTRL_MLEDIT, Ctrl);

    if (!YoriWinMlEditInsertTextRange(MlEdit,
                                      FALSE,
                                      MlEdit->CursorLine,
                                      MlEdit->CursorOffset,
                                      Text,
                                      &LastLine,
                                      &LastCharOffset)) {
        return FALSE;
    }

    YoriWinMlEditSetCursorPointInt(MlEdit, LastCharOffset, LastLine);
    return TRUE;
}

/**
 Set the color attributes of the multiline edit control.

 @param CtrlHandle Pointer to the multiline edit control.

 @param Attributes Specifies the foreground and background color for the
        multiline edit control to use.

 @param SelectedAttributes Specifies the foreground and background color
        to use for selected text within the multiline edit control.
 */
VOID
YoriWinMlEditSetColor(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in WORD Attributes,
    __in WORD SelectedAttributes
    )
{
    PYORIWIN_CTRL_MLEDIT MlEdit;
    PYORIWIN_CTRL Ctrl;

    Ctrl = (PYORIWIN_CTRL)CtrlHandle;
    MlEdit = CONTAINING_RECORD(Ctrl, YORIWIN_CTRL_MLEDIT, Ctrl);

    MlEdit->TextAttributes = Attributes;
    MlEdit->SelectedAttributes = SelectedAttributes;
    YoriWinMlEditExpandDirtyRange(MlEdit, 0, (YORI_ALLOC_SIZE_T)-1);
    YoriWinMlEditPaintNonClient(MlEdit);
    YoriWinMlEditPaint(MlEdit);
}

/**
 Return the current cursor location within a multiline edit control.

 @param CtrlHandle Pointer to the multiline edit control.

 @param CursorOffset On successful completion, populated with the character
        offset within the line that the cursor is currently on.

 @param CursorLine On successful completion, populated with the line that the
        cursor is currently on.
 */
VOID
YoriWinMlEditGetCursorPoint(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __out PYORI_ALLOC_SIZE_T CursorOffset,
    __out PYORI_ALLOC_SIZE_T CursorLine
    )
{
    PYORIWIN_CTRL_MLEDIT MlEdit;

    MlEdit = (PYORIWIN_CTRL_MLEDIT)CtrlHandle;

    *CursorOffset = MlEdit->CursorOffset;
    *CursorLine = MlEdit->CursorLine;
}

/**
 Modify the cursor location within the multiline edit control.

 @param CtrlHandle Pointer to the multiline edit control.

 @param NewCursorOffset The offset of the cursor from the beginning of the
        line, in buffer coordinates.

 @param NewCursorLine The buffer line that the cursor is located on.
 */
VOID
YoriWinMlEditSetCursorPoint(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in YORI_ALLOC_SIZE_T NewCursorOffset,
    __in YORI_ALLOC_SIZE_T NewCursorLine
    )
{
    PYORIWIN_CTRL Ctrl;
    PYORIWIN_CTRL_MLEDIT MlEdit;
    YORI_ALLOC_SIZE_T EffectiveNewCursorLine;

    Ctrl = (PYORIWIN_CTRL)CtrlHandle;
    MlEdit = CONTAINING_RECORD(Ctrl, YORIWIN_CTRL_MLEDIT, Ctrl);

    EffectiveNewCursorLine = NewCursorLine;

    if (EffectiveNewCursorLine > MlEdit->LinesPopulated) {
        if (MlEdit->LinesPopulated > 0) {
            EffectiveNewCursorLine = MlEdit->LinesPopulated - 1;
        } else {
            EffectiveNewCursorLine = 0;
        }
    }
    YoriWinMlEditSetCursorPointInt(MlEdit, NewCursorOffset, EffectiveNewCursorLine);
    YoriWinMlEditEnsureCursorShown(MlEdit);
    YoriWinMlEditPaint(MlEdit);
}

/**
 Return the current viewport location within a multiline edit control.

 @param CtrlHandle Pointer to the multiline edit control.

 @param ViewportLeft On successful completion, populated with the first
        character displayed in the control.

 @param ViewportTop On successful completion, populated with the first line
        displayed in the control.
 */
VOID
YoriWinMlEditGetViewportPoint(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __out PYORI_ALLOC_SIZE_T ViewportLeft,
    __out PYORI_ALLOC_SIZE_T ViewportTop
    )
{
    PYORIWIN_CTRL_MLEDIT MlEdit;

    MlEdit = (PYORIWIN_CTRL_MLEDIT)CtrlHandle;

    *ViewportLeft = MlEdit->ViewportLeft;
    *ViewportTop = MlEdit->ViewportTop;
}

/**
 Modify the viewport location within the multiline edit control.

 @param CtrlHandle Pointer to the multiline edit control.

 @param NewViewportLeft The display offset of the first character to display
        on the left of the control.

 @param NewViewportTop The display offset of the first character to display
        on the top of the control.
 */
VOID
YoriWinMlEditSetViewportPoint(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in YORI_ALLOC_SIZE_T NewViewportLeft,
    __in YORI_ALLOC_SIZE_T NewViewportTop
    )
{
    COORD ClientSize;
    PYORIWIN_CTRL Ctrl;
    PYORIWIN_CTRL_MLEDIT MlEdit;
    YORI_ALLOC_SIZE_T EffectiveNewViewportTop;

    Ctrl = (PYORIWIN_CTRL)CtrlHandle;
    MlEdit = CONTAINING_RECORD(Ctrl, YORIWIN_CTRL_MLEDIT, Ctrl);

    YoriWinGetCtrlClientSize(&MlEdit->Ctrl, &ClientSize);

    EffectiveNewViewportTop = NewViewportTop;

    if (EffectiveNewViewportTop > MlEdit->LinesPopulated) {
        if (MlEdit->LinesPopulated > 0) {
            EffectiveNewViewportTop = MlEdit->LinesPopulated - 1;
        } else {
            EffectiveNewViewportTop = 0;
        }
    }

    //
    //  Normally we'd call YoriWinMlEditEnsureCursorShown,
    //  but this series of routines allow the viewport to move where the
    //  cursor isn't.
    //

    if (EffectiveNewViewportTop != MlEdit->ViewportTop) {
        YoriWinMlEditExpandDirtyRange(MlEdit, EffectiveNewViewportTop, (YORI_ALLOC_SIZE_T)-1);
        MlEdit->ViewportTop = EffectiveNewViewportTop;
        YoriWinMlEditRepaintScrollBar(MlEdit);
    }

    if (NewViewportLeft != MlEdit->ViewportLeft) {
        YoriWinMlEditExpandDirtyRange(MlEdit, EffectiveNewViewportTop, (YORI_ALLOC_SIZE_T)-1);
        MlEdit->ViewportLeft = NewViewportLeft;
    }
    YoriWinMlEditPaint(MlEdit);
}

/**
 Clear all of the contents of a multiline edit control.

 @param CtrlHandle Pointer to the multiline edit control.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinMlEditClear(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle
    )
{
    PYORIWIN_CTRL Ctrl;
    PYORIWIN_CTRL_MLEDIT MlEdit;
    YORI_ALLOC_SIZE_T Index;

    Ctrl = (PYORIWIN_CTRL)CtrlHandle;
    MlEdit = CONTAINING_RECORD(Ctrl, YORIWIN_CTRL_MLEDIT, Ctrl);

    YoriWinMlEditClearSelection(MlEdit);

    for (Index = 0; Index < MlEdit->LinesPopulated; Index++) {
        YoriLibFreeStringContents(&MlEdit->LineArray[Index]);
    }
    YoriWinMlEditClearUndo(MlEdit);

    MlEdit->LinesPopulated = 0;
    MlEdit->ViewportTop = 0;
    MlEdit->ViewportLeft = 0;

    YoriWinMlEditExpandDirtyRange(MlEdit, MlEdit->ViewportTop, (YORI_ALLOC_SIZE_T)-1);
    YoriWinMlEditSetCursorPointInt(MlEdit, 0, 0);

    YoriWinMlEditPaint(MlEdit);
    return TRUE;
}

/**
 Indicate if auto indent is enabled, if an auto indent line is currently
 active, which line it is, and the extent of the indent.

 @param CtrlHandle Pointer to a multiline edit control.

 @param AutoIndentEnabled Optionally points to a boolean that will be written
        within this function to indicate whether auto indent can be applied or
        not.

 @param AutoIndentActive Optionally points to a boolean that will be written
        within this function to indicate whether auto indent is currently
        active on any line.

 @param AutoIndentActiveLine Optionally points to an integer that will be
        written within this function to indicate the line index that has
        auto indent applied.  This is only meaningful if AutoIndentActive is
        TRUE.

 @param AutoIndentActiveLength Optionally points to an integer that will be
        written within this function to indicate the length of the auto
        indent.  This is only meaningful if AutoIndentActive is TRUE.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinMlEditGetAutoIndent(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __out_opt PBOOLEAN AutoIndentEnabled,
    __out_opt PBOOLEAN AutoIndentActive,
    __out_opt PYORI_ALLOC_SIZE_T AutoIndentActiveLine,
    __out_opt PYORI_ALLOC_SIZE_T AutoIndentActiveLength
    )
{
    PYORIWIN_CTRL Ctrl;
    PYORIWIN_CTRL_MLEDIT MlEdit;

    Ctrl = (PYORIWIN_CTRL)CtrlHandle;
    MlEdit = CONTAINING_RECORD(Ctrl, YORIWIN_CTRL_MLEDIT, Ctrl);

    if (AutoIndentEnabled != NULL) {
        if (MlEdit->AutoIndent) {
            *AutoIndentEnabled = TRUE;
        } else {
            *AutoIndentEnabled = FALSE;
        }
    }

    if (AutoIndentActive != NULL) {
        if (MlEdit->AutoIndentApplied) {
            *AutoIndentActive = TRUE;
        } else {
            *AutoIndentActive = FALSE;
        }
    }

    if (AutoIndentActiveLine != NULL) {
        if (MlEdit->AutoIndentApplied) {
            *AutoIndentActiveLine = MlEdit->AutoIndentAppliedLine;
        } else {
            *AutoIndentActiveLine = 0;
        }
    }

    if (AutoIndentActiveLength != NULL) {
        if (MlEdit->AutoIndentApplied) {
            *AutoIndentActiveLength = MlEdit->AutoIndentSourceLength;
        } else {
            *AutoIndentActiveLength = 0;
        }
    }

    return TRUE;
}


/**
 Return the number of lines with data in a multiline edit control.

 @param CtrlHandle Pointer to the multiline edit control.

 @return The number of lines with data.
 */
YORI_ALLOC_SIZE_T
YoriWinMlEditGetLineCount(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle
    )
{
    PYORIWIN_CTRL Ctrl;
    PYORIWIN_CTRL_MLEDIT MlEdit;

    Ctrl = (PYORIWIN_CTRL)CtrlHandle;
    MlEdit = CONTAINING_RECORD(Ctrl, YORIWIN_CTRL_MLEDIT, Ctrl);

    return MlEdit->LinesPopulated;
}

/**
 Return the string that describes a single line within a multiline edit
 control.  As of this writing, this is a pointer to the string used by the
 control itself, and as such is only meaningful if the text cannot be
 altered by any mechanism.

 @param CtrlHandle Pointer to the multiline edit control.

 @param Index The line number to return.

 @return Pointer to the line string, or NULL if the line index is out of
         bounds.
 */
PYORI_STRING
YoriWinMlEditGetLineByIndex(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in YORI_ALLOC_SIZE_T Index
    )
{
    PYORIWIN_CTRL Ctrl;
    PYORIWIN_CTRL_MLEDIT MlEdit;

    Ctrl = (PYORIWIN_CTRL)CtrlHandle;
    MlEdit = CONTAINING_RECORD(Ctrl, YORIWIN_CTRL_MLEDIT, Ctrl);

    if (Index >= MlEdit->LinesPopulated) {
        return NULL;
    }

    return &MlEdit->LineArray[Index];
}

/**
 Set the title to display on the top of a multiline edit control.

 @param CtrlHandle Pointer to the multiline edit control.

 @param Caption Pointer to the caption to display on the top of the multiline
        edit control.  This can point to an empty string to indicate no
        caption should be displayed.

 @return TRUE to indicate the caption was successfully updated, or FALSE on
         failure.
 */
BOOLEAN
YoriWinMlEditSetCaption(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in PYORI_STRING Caption
    )
{
    PYORIWIN_CTRL Ctrl;
    PYORIWIN_CTRL_MLEDIT MlEdit;
    YORI_STRING NewCaption;

    Ctrl = (PYORIWIN_CTRL)CtrlHandle;
    MlEdit = CONTAINING_RECORD(Ctrl, YORIWIN_CTRL_MLEDIT, Ctrl);

    if (MlEdit->Caption.LengthAllocated < Caption->LengthInChars) {
        if (!YoriLibAllocateString(&NewCaption, Caption->LengthInChars)) {
            return FALSE;
        }

        YoriLibFreeStringContents(&MlEdit->Caption);
        memcpy(&MlEdit->Caption, &NewCaption, sizeof(YORI_STRING));
    }

    if (Caption->LengthInChars > 0) {
        memcpy(MlEdit->Caption.StartOfString, Caption->StartOfString, Caption->LengthInChars * sizeof(TCHAR));
    }
    MlEdit->Caption.LengthInChars = Caption->LengthInChars;
    YoriWinMlEditPaintNonClient(MlEdit);
    return TRUE;
}

/**
 Indicates whether the multiline edit control has been modified by the user.
 This is typically used after some external event indicates that the buffer
 should be considered unchanged, eg., a file is successfully saved.

 @param CtrlHandle Pointer to the multiline edit contorl.

 @param ModifyState TRUE if the control should consider itself modified by
        the user, FALSE if it should not.

 @return TRUE if the control was previously modified by the user, FALSE if it
         was not.
 */
BOOLEAN
YoriWinMlEditSetModifyState(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in BOOLEAN ModifyState
    )
{
    PYORIWIN_CTRL Ctrl;
    PYORIWIN_CTRL_MLEDIT MlEdit;
    BOOLEAN PreviousValue;

    Ctrl = (PYORIWIN_CTRL)CtrlHandle;
    MlEdit = CONTAINING_RECORD(Ctrl, YORIWIN_CTRL_MLEDIT, Ctrl);

    PreviousValue = MlEdit->UserModified;
    MlEdit->UserModified = ModifyState;
    return PreviousValue;
}

/**
 Query the number of spaces to display for each tab character in the buffer.

 @param CtrlHandle Pointer to the multiline edit control.

 @param TabWidth On successful completion, set to the current tab width.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
__success(return)
BOOLEAN
YoriWinMlEditGetTabWidth(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __out PYORI_ALLOC_SIZE_T TabWidth
    )
{
    PYORIWIN_CTRL Ctrl;
    PYORIWIN_CTRL_MLEDIT MlEdit;

    Ctrl = (PYORIWIN_CTRL)CtrlHandle;
    MlEdit = CONTAINING_RECORD(Ctrl, YORIWIN_CTRL_MLEDIT, Ctrl);

    *TabWidth = MlEdit->TabWidth;
    return TRUE;
}

/**
 Set the number of spaces to display for each tab character in the buffer.

 @param CtrlHandle Pointer to the multiline edit control.

 @param TabWidth Specifies the new tab width.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
__success(return)
BOOLEAN
YoriWinMlEditSetTabWidth(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in YORI_ALLOC_SIZE_T TabWidth
    )
{
    PYORIWIN_CTRL Ctrl;
    PYORIWIN_CTRL_MLEDIT MlEdit;

    Ctrl = (PYORIWIN_CTRL)CtrlHandle;
    MlEdit = CONTAINING_RECORD(Ctrl, YORIWIN_CTRL_MLEDIT, Ctrl);

    MlEdit->TabWidth = TabWidth;
    YoriWinMlEditExpandDirtyRange(MlEdit, 0, MlEdit->LinesPopulated);
    return TRUE;
}

/**
 Enable or disable traditional MS-DOS edit navigation rules.  In the
 traditional model, the cursor can move infinitely right of the text in any
 line, so the cursor's line does not change in response to left and right keys.
 In the more modern model, navigating left beyond the beginning of the line
 moves to the previous line, and navigating right beyond the end of the line
 moves to the next line.

 @param CtrlHandle Pointer to the multiline edit control.

 @param TradNavigationEnabled TRUE to use traditional MS-DOS edit
        navigation, FALSE to use Windows style multiline edit navigation.
 */
VOID
YoriWinMlEditSetTradNavigation(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in BOOLEAN TradNavigationEnabled
    )
{
    PYORIWIN_CTRL Ctrl;
    PYORIWIN_CTRL_MLEDIT MlEdit;

    Ctrl = (PYORIWIN_CTRL)CtrlHandle;
    MlEdit = CONTAINING_RECORD(Ctrl, YORIWIN_CTRL_MLEDIT, Ctrl);
    MlEdit->TradEditNavigation = TradNavigationEnabled;
    YoriWinMlEditClearDesiredDisplayOffset(MlEdit);
    if (!MlEdit->TradEditNavigation) {
        if (MlEdit->CursorLine < MlEdit->LinesPopulated) {
            if (MlEdit->CursorOffset > MlEdit->LineArray[MlEdit->CursorLine].LengthInChars) {
                YoriWinMlEditSetCursorPointInt(MlEdit,
                                               MlEdit->LineArray[MlEdit->CursorLine].LengthInChars,
                                               MlEdit->CursorLine);
            }
        }
    }
}

/**
 Enable or disable auto indent.  If a new line is created when auto indent
 is enabled, the line is initialized with the leading white space from the
 previous line.  If auto indent is disabled, a new line is initialized with
 no leading white space.

 @param CtrlHandle Pointer to the multiline edit control.

 @param AutoIndentEnabled TRUE to enable auto indent behavior; FALSE to
        disable auto indent behavior.
 */
VOID
YoriWinMlEditSetAutoIndent(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in BOOLEAN AutoIndentEnabled
    )
{
    PYORIWIN_CTRL Ctrl;
    PYORIWIN_CTRL_MLEDIT MlEdit;
    YORI_ALLOC_SIZE_T LineIndex;

    Ctrl = (PYORIWIN_CTRL)CtrlHandle;
    MlEdit = CONTAINING_RECORD(Ctrl, YORIWIN_CTRL_MLEDIT, Ctrl);

    if (!AutoIndentEnabled && MlEdit->AutoIndentApplied) {
        LineIndex = MlEdit->AutoIndentAppliedLine;
        if (YoriWinMlEditTrimAutoIndent(MlEdit, LineIndex, 0)) {
            YoriWinMlEditSetCursorPointInt(MlEdit, 0, LineIndex);
            YoriWinMlEditEnsureCursorShown(MlEdit);
            YoriWinMlEditPaint(MlEdit);
        }
    }

    MlEdit->AutoIndent = AutoIndentEnabled;
}

/**
 Enable or disable tab expansion.  If a tab key is pressed when tab expansion
 is enabled, it is substituted in text with the number of spaces from
 TabWidth.  If tab expansion is disabled, the key inserts a tab character into
 text.

 @param CtrlHandle Pointer to the multiline edit control.

 @param ExpandTabEnabled TRUE to enable expand tab behavior; FALSE to disable
        expand tab behavior.
 */
VOID
YoriWinMlEditSetExpandTab(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in BOOLEAN ExpandTabEnabled
    )
{
    PYORIWIN_CTRL Ctrl;
    PYORIWIN_CTRL_MLEDIT MlEdit;

    Ctrl = (PYORIWIN_CTRL)CtrlHandle;
    MlEdit = CONTAINING_RECORD(Ctrl, YORIWIN_CTRL_MLEDIT, Ctrl);
    MlEdit->ExpandTab = ExpandTabEnabled;
}

/**
 Returns TRUE if the multiline edit control has been modified by the user
 since the last time @ref YoriWinMlEditSetModifyState indicated that
 no user modification has occurred.

 @param CtrlHandle Pointer to the multiline edit contorl.

 @return TRUE if the control has been modified by the user, FALSE if it has
         not.
 */
BOOLEAN
YoriWinMlEditGetModifyState(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle
    )
{
    PYORIWIN_CTRL Ctrl;
    PYORIWIN_CTRL_MLEDIT MlEdit;

    Ctrl = (PYORIWIN_CTRL)CtrlHandle;
    MlEdit = CONTAINING_RECORD(Ctrl, YORIWIN_CTRL_MLEDIT, Ctrl);

    return MlEdit->UserModified;
}

/**
 Set a function to call when the cursor location changes.

 @param CtrlHandle Pointer to the multiline edit control.

 @param NotifyCallback Pointer to a function to invoke when the cursor
        moves.

 @return TRUE to indicate the callback function was successfully updated,
         FALSE to indicate another callback function was already present.
 */
BOOLEAN
YoriWinMlEditSetCursorNotifyCbk(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in PYORIWIN_NOTIFY_MLEDIT_CURSOR NotifyCallback
    )
{
    PYORIWIN_CTRL Ctrl;
    PYORIWIN_CTRL_MLEDIT MlEdit;

    Ctrl = (PYORIWIN_CTRL)CtrlHandle;
    MlEdit = CONTAINING_RECORD(Ctrl, YORIWIN_CTRL_MLEDIT, Ctrl);

    if (MlEdit->CursorMoveCallback != NULL) {
        return FALSE;
    }

    MlEdit->CursorMoveCallback = NotifyCallback;

    return TRUE;
}

//
//  =========================================
//  INPUT HANDLING FUNCTIONS
//  =========================================
//

/**
 Delete the character before the cursor and move later characters into
 position.

 @param MlEdit Pointer to the multiline edit control, indicating the
        current cursor location.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinMlEditBackspace(
    __in PYORIWIN_CTRL_MLEDIT MlEdit
    )
{
    PYORI_STRING Line;
    YORI_ALLOC_SIZE_T FirstLine;
    YORI_ALLOC_SIZE_T FirstCharOffset;
    YORI_ALLOC_SIZE_T LastLine;
    YORI_ALLOC_SIZE_T LastCharOffset;

    if (MlEdit->CursorLine >= MlEdit->LinesPopulated) {
        return FALSE;
    }

    YoriWinMlEditClearDesiredDisplayOffset(MlEdit);

    if (YoriWinMlEditSelectionActive(&MlEdit->Ctrl)) {
        return YoriWinMlEditDeleteSelection(&MlEdit->Ctrl);
    }

    Line = &MlEdit->LineArray[MlEdit->CursorLine];

    LastLine = MlEdit->CursorLine;
    LastCharOffset = MlEdit->CursorOffset;

    if (MlEdit->AutoIndentApplied) {
        YORI_STRING NewIndent;
        YORI_ALLOC_SIZE_T NewIndentSourceLine;

        ASSERT(LastCharOffset > 0);
        YoriWinMlEditFindPreviousIndentLine(MlEdit, &NewIndentSourceLine, &NewIndent);

        FirstLine = LastLine;
        FirstCharOffset = NewIndent.LengthInChars;

        if (NewIndent.LengthInChars == 0) {
            MlEdit->AutoIndentApplied = FALSE;
        } else {
            MlEdit->AutoIndentSourceLength = NewIndent.LengthInChars;
        }

    } else if (LastCharOffset == 0) {

        //
        //  If we're at the beginning of the line, we may need to merge lines.
        //  If it's the first line, we're finished.
        //

        if (MlEdit->CursorLine == 0) {
            return FALSE;
        }

        FirstLine = MlEdit->CursorLine - 1;
        FirstCharOffset = MlEdit->LineArray[FirstLine].LengthInChars;
    } else {
        FirstLine = LastLine;
        FirstCharOffset = LastCharOffset - 1;
    }

    if (!YoriWinMlEditDeleteTextRange(MlEdit,
                                      TRUE,
                                      FALSE,
                                      FALSE,
                                      FirstLine,
                                      FirstCharOffset,
                                      LastLine,
                                      LastCharOffset)) {
        return FALSE;
    }

    YoriWinMlEditSetCursorPointInt(MlEdit, FirstCharOffset, FirstLine);
    return TRUE;
}

/**
 Delete the character at the cursor and move later characters into position.

 @param MlEdit Pointer to the multiline edit control, indicating the
        current cursor location.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinMlEditDelete(
    __in PYORIWIN_CTRL_MLEDIT MlEdit
    )
{
    PYORI_STRING Line;
    YORI_ALLOC_SIZE_T FirstLine;
    YORI_ALLOC_SIZE_T FirstCharOffset;
    YORI_ALLOC_SIZE_T LastLine;
    YORI_ALLOC_SIZE_T LastCharOffset;

    if (MlEdit->CursorLine >= MlEdit->LinesPopulated) {
        return FALSE;
    }

    if (YoriWinMlEditSelectionActive(&MlEdit->Ctrl)) {
        return YoriWinMlEditDeleteSelection(&MlEdit->Ctrl);
    }

    Line = &MlEdit->LineArray[MlEdit->CursorLine];

    FirstLine = MlEdit->CursorLine;
    FirstCharOffset = MlEdit->CursorOffset;

    if (FirstCharOffset >= Line->LengthInChars) {
        LastLine = FirstLine + 1;
        LastCharOffset = 0;
    } else {
        LastLine = FirstLine;
        LastCharOffset = FirstCharOffset + 1;
    }

    if (!YoriWinMlEditDeleteTextRange(MlEdit,
                                      FALSE,
                                      FALSE,
                                      FALSE,
                                      FirstLine,
                                      FirstCharOffset,
                                      LastLine,
                                      LastCharOffset)) {
        return FALSE;
    }

    YoriWinMlEditSetCursorPointInt(MlEdit, FirstCharOffset, FirstLine);

    return TRUE;
}

/**
 Delete the line at the cursor and move later lines into position.

 @param MlEdit Pointer to the multiline edit control, indicating the
        current cursor location.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinMlEditDeleteLine(
    __in PYORIWIN_CTRL_MLEDIT MlEdit
    )
{
    if (MlEdit->LinesPopulated == 0) {
        return FALSE;
    }

    if (!YoriWinMlEditDeleteTextRange(MlEdit,
                                      FALSE,
                                      FALSE,
                                      FALSE,
                                      MlEdit->CursorLine,
                                      0,
                                      MlEdit->CursorLine + 1,
                                      0)) {
        return FALSE;
    }

    return TRUE;
}

/**
 Move the viewport up by one screenful and move the cursor to match.
 If we're at the top of the range, do nothing.  The somewhat strange
 logic here is patterned after the original edit.

 @param MlEdit Pointer to the multiline edit control specifying the
        viewport location and cursor location.  On completion these may be
        adjusted.

 @return TRUE to indicate the display was moved, FALSE if it was not.
 */
BOOLEAN
YoriWinMlEditPageUp(
    __in PYORIWIN_CTRL_MLEDIT MlEdit
    )
{
    COORD ClientSize;
    YORI_ALLOC_SIZE_T ViewportHeight;
    YORI_ALLOC_SIZE_T NewCursorLine;
    YORI_ALLOC_SIZE_T NewCursorOffset;

    YoriWinGetCtrlClientSize(&MlEdit->Ctrl, &ClientSize);
    ViewportHeight = ClientSize.Y;

    if (MlEdit->CursorLine > 0) {
        if (MlEdit->CursorLine >= ViewportHeight) {
            NewCursorLine = MlEdit->CursorLine - ViewportHeight;
        } else {
            NewCursorLine = 0;
        }

        if (MlEdit->ViewportTop >= ViewportHeight) {
            MlEdit->ViewportTop = MlEdit->ViewportTop - ViewportHeight;
        } else {
            MlEdit->ViewportTop = 0;
        }

        if (NewCursorLine != MlEdit->CursorLine) {
            YoriWinMlEditTrimAutoIndent(MlEdit, MlEdit->CursorLine, 0);
        }

        YoriWinMlEditExpandDirtyRange(MlEdit, MlEdit->ViewportTop, (YORI_ALLOC_SIZE_T)-1);

        YoriWinMlEditPopulateDesiredDisplayOffset(MlEdit);
        YoriWinMlEditFindCursorCharFromDisplayChar(MlEdit,
                                                   NewCursorLine,
                                                   MlEdit->DesiredDisplayCursorOffset,
                                                   &NewCursorOffset,
                                                   NULL);
        YoriWinMlEditSetCursorPointInt(MlEdit, NewCursorOffset, NewCursorLine);
        YoriWinMlEditRepaintScrollBar(MlEdit);
        return TRUE;
    }

    return FALSE;
}

/**
 Move the viewport down by one screenful and move the cursor to match.
 If we're at the bottom of the range, do nothing.  The somewhat strange
 logic here is patterned after the original edit.

 @param MlEdit Pointer to the multiline edit control specifying the
        viewport location and cursor location.  On completion these may be
        adjusted.

 @return TRUE to indicate the display was moved, FALSE if it was not.
 */
BOOLEAN
YoriWinMlEditPageDown(
    __in PYORIWIN_CTRL_MLEDIT MlEdit
    )
{
    COORD ClientSize;
    YORI_ALLOC_SIZE_T ViewportHeight;
    YORI_ALLOC_SIZE_T NewCursorLine;
    YORI_ALLOC_SIZE_T NewCursorOffset;

    YoriWinGetCtrlClientSize(&MlEdit->Ctrl, &ClientSize);
    ViewportHeight = ClientSize.Y;

    if (MlEdit->ViewportTop + ViewportHeight < MlEdit->LinesPopulated) {
        MlEdit->ViewportTop = MlEdit->ViewportTop + ViewportHeight;
        YoriWinMlEditExpandDirtyRange(MlEdit, MlEdit->ViewportTop, (YORI_ALLOC_SIZE_T)-1);
        NewCursorLine = MlEdit->CursorLine;
        if (MlEdit->CursorLine + ViewportHeight < MlEdit->LinesPopulated) {
            NewCursorLine = MlEdit->CursorLine + ViewportHeight;
        } else if (MlEdit->CursorLine + 1 < MlEdit->LinesPopulated) {
            NewCursorLine = MlEdit->LinesPopulated - 1;
        }

        if (NewCursorLine != MlEdit->CursorLine) {
            YoriWinMlEditTrimAutoIndent(MlEdit, MlEdit->CursorLine, 0);
        }

        YoriWinMlEditPopulateDesiredDisplayOffset(MlEdit);
        YoriWinMlEditFindCursorCharFromDisplayChar(MlEdit,
                                                   NewCursorLine,
                                                   MlEdit->DesiredDisplayCursorOffset,
                                                   &NewCursorOffset,
                                                   NULL);
        YoriWinMlEditSetCursorPointInt(MlEdit, NewCursorOffset, NewCursorLine);
        YoriWinMlEditRepaintScrollBar(MlEdit);
        return TRUE;
    }

    return FALSE;
}

/**
 Scroll the multiline edit based on a mouse wheel notification.

 @param MlEdit Pointer to the multiline edit to scroll.

 @param LinesToMove The number of lines to scroll.

 @param MoveUp If TRUE, scroll backwards through the text.  If FALSE,
        scroll forwards through the text.
 */
VOID
YoriWinMlEditNotifyMouseWheel(
    __in PYORIWIN_CTRL_MLEDIT MlEdit,
    __in YORI_ALLOC_SIZE_T LinesToMove,
    __in BOOLEAN MoveUp
    )
{
    COORD ClientSize;
    YORI_ALLOC_SIZE_T LineCountToDisplay;
    YORI_ALLOC_SIZE_T NewViewportTop;

    YoriWinGetCtrlClientSize(&MlEdit->Ctrl, &ClientSize);
    LineCountToDisplay = ClientSize.Y;

    if (MoveUp) {
        if (MlEdit->ViewportTop < LinesToMove) {
            NewViewportTop = 0;
        } else {
            NewViewportTop = MlEdit->ViewportTop - LinesToMove;
        }
    } else {
        if (MlEdit->ViewportTop + LinesToMove + LineCountToDisplay > MlEdit->LinesPopulated) {
            if (MlEdit->LinesPopulated >= LineCountToDisplay) {
                NewViewportTop = MlEdit->LinesPopulated - LineCountToDisplay;
            } else {
                NewViewportTop = 0;
            }
        } else {
            NewViewportTop = MlEdit->ViewportTop + LinesToMove;
        }
    }

    YoriWinMlEditSetViewportPoint(&MlEdit->Ctrl, MlEdit->ViewportLeft, NewViewportTop);
}

/**
 Handle a double-click within a multi line edit control.  This is supposed to
 select a "word" which is delimited by a user controllable set of characters.

 @param MlEdit Pointer to the multiline edit control.

 @param ViewportX The horizontal position in the control relative to its
        client area.

 @param ViewportY The vertial position in the control relative to its
        client area.
 */
VOID
YoriWinMlEditNotifyDoubleClick(
    __in PYORIWIN_CTRL_MLEDIT MlEdit,
    __in YORI_ALLOC_SIZE_T ViewportX,
    __in YORI_ALLOC_SIZE_T ViewportY
    )
{
    YORI_ALLOC_SIZE_T NewCursorLine;
    YORI_ALLOC_SIZE_T NewCursorChar;
    PYORI_STRING Line;
    YORI_STRING BreakChars;

    //
    //  Translate the viewport location into a buffer location.
    //

    if (YoriWinMlEditTransViewCoordToCursor(MlEdit, ViewportX, ViewportY, &NewCursorLine, &NewCursorChar)) {
        YORI_ALLOC_SIZE_T BeginRangeOffset;
        YORI_ALLOC_SIZE_T EndRangeOffset;

        //
        //  If it's beyond the number of lines populated, there's nothing to
        //  select.
        //

        if (NewCursorLine >= MlEdit->LinesPopulated) {
            return;
        }

        if (NewCursorLine != MlEdit->CursorLine) {
            YoriWinMlEditTrimAutoIndent(MlEdit, MlEdit->CursorLine, 0);
        }

        //
        //  If it's beyond the end of the line, there's nothing to select.
        //

        Line = &MlEdit->LineArray[NewCursorLine];
        if (NewCursorChar >= Line->LengthInChars) {
            return;
        }

        //
        //  Determine which characters delimit words.
        //

        if (!YoriLibGetSelDblClkBreakChars(&BreakChars)) {
            return;
        }

        //
        //  Search left looking for a delimiter or the start of the string.
        //

        BeginRangeOffset = NewCursorChar;
        if (YoriLibFindLeftMostCharacter(&BreakChars, Line->StartOfString[BeginRangeOffset]) == NULL) {
            while (BeginRangeOffset > 0 &&
                   YoriLibFindLeftMostCharacter(&BreakChars, Line->StartOfString[BeginRangeOffset - 1]) == NULL) {
                BeginRangeOffset--;
            }
        }

        //
        //  Search right looking for a delimiter or the end of the string.
        //

        EndRangeOffset = NewCursorChar;
        while (EndRangeOffset < Line->LengthInChars &&
               YoriLibFindLeftMostCharacter(&BreakChars, Line->StartOfString[EndRangeOffset]) == NULL) {
            EndRangeOffset++;
        }

        YoriLibFreeStringContents(&BreakChars);

        //
        //  If any range was found (ie., the user didn't click on a word
        //  delimiter) select the range.
        //

        if (EndRangeOffset > BeginRangeOffset) {
            YoriWinMlEditSetSelectionRange(&MlEdit->Ctrl,
                                           NewCursorLine,
                                           BeginRangeOffset,
                                           NewCursorLine,
                                           EndRangeOffset);
        }
    }
}

/**
 Adjust the viewport and selection to reflect the mouse being dragged,
 potentially outside the control's client area while the button is held down,
 thereby extending the selection.

 @param MlEdit Pointer to the multiline edit control.

 @param MousePos Specifies the mouse position.
 */
VOID
YoriWinMlEditScrollForMouseSel(
    __in PYORIWIN_CTRL_MLEDIT MlEdit,
    __in PYORIWIN_BOUNDED_COORD MousePos
    )
{
    COORD ClientSize;
    YORI_ALLOC_SIZE_T LineCountToDisplay;
    YORI_ALLOC_SIZE_T NewCursorLine;
    YORI_ALLOC_SIZE_T NewCursorOffset;
    YORI_ALLOC_SIZE_T NewViewportTop;
    YORI_ALLOC_SIZE_T NewViewportLeft;
    YORI_ALLOC_SIZE_T DisplayOffset;
    BOOLEAN SetTimer;

    SetTimer = FALSE;
    if (MousePos != &MlEdit->LastMousePos) {
        MlEdit->LastMousePos.Pos.X = MousePos->Pos.X;
        MlEdit->LastMousePos.Pos.Y = MousePos->Pos.Y;
        MlEdit->LastMousePos.Above = MousePos->Above;
        MlEdit->LastMousePos.Below = MousePos->Below;
        MlEdit->LastMousePos.Left = MousePos->Left;
        MlEdit->LastMousePos.Right = MousePos->Right;
    }

    YoriWinGetCtrlClientSize(&MlEdit->Ctrl, &ClientSize);
    LineCountToDisplay = ClientSize.Y;

    NewViewportTop = MlEdit->ViewportTop;
    NewViewportLeft = MlEdit->ViewportLeft;
    NewCursorLine = MlEdit->CursorLine;

    //
    //  First find the cursor line.  This can be above the viewport, below
    //  the viewport, or any line within the viewport.
    //

    if (MousePos->Above) {
        if (MlEdit->ViewportTop < 1) {
            NewCursorLine = 0;
        } else {
            NewCursorLine = NewViewportTop - 1;
        }
        SetTimer = TRUE;
    } else if (MousePos->Below) {
        if (NewViewportTop + 1 + LineCountToDisplay > MlEdit->LinesPopulated) {
            if (MlEdit->LinesPopulated > 0) {
                NewCursorLine = MlEdit->LinesPopulated - 1;
            } else {
                NewCursorLine = 0;
            }
        } else {
            NewCursorLine = NewViewportTop + LineCountToDisplay + 1;
        }
        SetTimer = TRUE;
    } else {
        if (NewViewportTop + MousePos->Pos.Y < MlEdit->LinesPopulated) {
            NewCursorLine = NewViewportTop + MousePos->Pos.Y;
        } else if (MlEdit->LinesPopulated > 0) {
            NewCursorLine = MlEdit->LinesPopulated - 1;
        } else {
            NewCursorLine = 0;
        }
    }

    //
    //  Now find the cursor column.  This can be left of the viewport, right
    //  of the viewport, or any column within the viewport.  When in the
    //  viewport, this needs to be translated from a display location to
    //  a buffer location.
    //

    if (MousePos->Left) {
        if (NewViewportLeft > 0) {
            DisplayOffset = NewViewportLeft - 1;
        } else {
            DisplayOffset = 0;
        }
        SetTimer = TRUE;
    } else if (MousePos->Right) {
        DisplayOffset = NewViewportLeft + ClientSize.X + 1;
        SetTimer = TRUE;
    } else {
        DisplayOffset = NewViewportLeft + MousePos->Pos.X;
    }

    if (SetTimer) {
        if (MlEdit->Timer == NULL) {
            PYORIWIN_WINDOW TopLevelWindow;
            TopLevelWindow = YoriWinGetTopLevelWindow(&MlEdit->Ctrl);
            MlEdit->Timer = YoriWinMgrAllocRecurringTimer(YoriWinGetWinMgrHandle(TopLevelWindow),
                                                          &MlEdit->Ctrl,
                                                          100);
        }
    } else {
        if (MlEdit->Timer != NULL) {
            YoriWinMgrFreeTimer(MlEdit->Timer);
            MlEdit->Timer = NULL;
        }
    }

    YoriWinMlEditFindCursorCharFromDisplayChar(MlEdit,
                                               NewCursorLine,
                                               DisplayOffset,
                                               &NewCursorOffset,
                                               NULL);

    //
    //  When using modern navigation, the cursor can't move to the right of
    //  the text in the line.  With traditional MS-DOS navigation, it can.
    //

    if (!MlEdit->TradEditNavigation) {
        if (MlEdit->LinesPopulated > 0) {
            ASSERT(NewCursorLine < MlEdit->LinesPopulated);
            if (NewCursorOffset > MlEdit->LineArray[NewCursorLine].LengthInChars) {
                NewCursorOffset = MlEdit->LineArray[NewCursorLine].LengthInChars;
            }
        }
    }

    if (NewCursorLine != MlEdit->CursorLine) {
        YoriWinMlEditTrimAutoIndent(MlEdit, MlEdit->CursorLine, 0);
    }

    YoriWinMlEditClearDesiredDisplayOffset(MlEdit);
    YoriWinMlEditSetCursorPointInt(MlEdit, NewCursorOffset, NewCursorLine);
    if (MlEdit->Selection.Active == YoriWinMlEditSelMouseFromTopDown ||
        MlEdit->Selection.Active == YoriWinMlEditSelMouseFromBottomUp) {
        YoriWinMlEditExtendSelectionToCursor(MlEdit);
    } else {
        YoriWinMlEditStartSelAtCursor(MlEdit, TRUE);
    }
    YoriWinMlEditEnsureCursorShown(MlEdit);
    YoriWinMlEditPaint(MlEdit);
}

/**
 When the user presses a regular key, insert that key into the control.

 @param MlEdit Pointer to the multiline edit control, specifying the
        location of the cursor and contents of the control.

 @param Char The character to insert.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinMlEditAddChar(
    __in PYORIWIN_CTRL_MLEDIT MlEdit,
    __in TCHAR Char
    )
{
    YORI_ALLOC_SIZE_T NewCursorLine;
    YORI_ALLOC_SIZE_T NewCursorOffset;
    YORI_STRING String;

    if (YoriWinMlEditSelectionActive(MlEdit)) {
        YoriWinMlEditDeleteSelection(MlEdit);
    }

    YoriWinMlEditClearDesiredDisplayOffset(MlEdit);

    YoriLibInitEmptyString(&String);

    if (Char == '\t' && MlEdit->ExpandTab) {
        YORI_ALLOC_SIZE_T CharIndex;

        if (MlEdit->TabWidth == 0) {
            return TRUE;
        }

        if (!YoriLibAllocateString(&String, MlEdit->TabWidth)) {
            return FALSE;
        }

        for (CharIndex = 0; CharIndex < MlEdit->TabWidth; CharIndex++) {
            String.StartOfString[CharIndex] = ' ';
        }
        String.LengthInChars = MlEdit->TabWidth;
    } else {
        String.StartOfString = &Char;
        String.LengthInChars = 1;
    }

    if (!MlEdit->InsertMode) {
        if (!YoriWinMlEditOverwriteTextRange(MlEdit,
                                             FALSE,
                                             MlEdit->CursorLine,
                                             MlEdit->CursorOffset,
                                             &String,
                                             &NewCursorLine,
                                             &NewCursorOffset)) {
            YoriLibFreeStringContents(&String);
            return FALSE;
        }
    } else {
        if (!YoriWinMlEditInsertTextRange(MlEdit,
                                          FALSE,
                                          MlEdit->CursorLine,
                                          MlEdit->CursorOffset,
                                          &String,
                                          &NewCursorLine,
                                          &NewCursorOffset)) {
            YoriLibFreeStringContents(&String);
            return FALSE;
        }
    }

    YoriLibFreeStringContents(&String);
    YoriWinMlEditSetCursorPointInt(MlEdit, NewCursorOffset, NewCursorLine);

    return TRUE;

}


/**
 Process a key that may be an enhanced key.  Some of these keys can be either
 enhanced or non-enhanced.

 @param MlEdit Pointer to the multiline edit control, indicating the
        current cursor location.

 @param Event Pointer to the event describing the state of the key being
        pressed.

 @return TRUE to indicate the key has been processed, FALSE if it is an
         unknown key.
 */
BOOLEAN
YoriWinMlEditProcEnhKey(
    __in PYORIWIN_CTRL_MLEDIT MlEdit,
    __in PYORIWIN_EVENT Event
    )
{
    BOOLEAN Recognized;
    YORI_ALLOC_SIZE_T NewCursorLine;
    YORI_ALLOC_SIZE_T NewCursorOffset;
    Recognized = FALSE;

    if (Event->u.KeyDown.VirtualKeyCode == VK_LEFT) {
        if (MlEdit->CursorOffset > 0 ||
            (!MlEdit->TradEditNavigation && MlEdit->CursorLine > 0)) {
            if (Event->u.KeyDown.CtrlMask & SHIFT_PRESSED) {
                YoriWinMlEditStartSelAtCursor(MlEdit, FALSE);
            } else if (YoriWinMlEditSelectionActive(&MlEdit->Ctrl)) {
                YoriWinMlEditClearSelection(MlEdit);
            }
            NewCursorLine = MlEdit->CursorLine;
            if (MlEdit->CursorOffset == 0) {
                ASSERT(!MlEdit->TradEditNavigation);
                NewCursorLine = NewCursorLine - 1;
                NewCursorOffset = MlEdit->LineArray[NewCursorLine].LengthInChars;
                YoriWinMlEditTrimAutoIndent(MlEdit, MlEdit->CursorLine, 0);
            } else {
                NewCursorOffset = MlEdit->CursorOffset - 1;
                YoriWinMlEditTrimAutoIndent(MlEdit, MlEdit->CursorLine, NewCursorOffset);
            }
            YoriWinMlEditSetCursorPointInt(MlEdit, NewCursorOffset, NewCursorLine);
            if (Event->u.KeyDown.CtrlMask & SHIFT_PRESSED) {
                YoriWinMlEditExtendSelectionToCursor(MlEdit);
            }
            YoriWinMlEditClearDesiredDisplayOffset(MlEdit);
            YoriWinMlEditEnsureCursorShown(MlEdit);
            YoriWinMlEditPaint(MlEdit);
        }
        Recognized = TRUE;
    } else if (Event->u.KeyDown.VirtualKeyCode == VK_RIGHT) {
        if (MlEdit->TradEditNavigation ||
            (MlEdit->CursorLine < MlEdit->LinesPopulated &&
             MlEdit->CursorOffset < MlEdit->LineArray[MlEdit->CursorLine].LengthInChars) ||
            MlEdit->CursorLine + 1 < MlEdit->LinesPopulated) {

            if (Event->u.KeyDown.CtrlMask & SHIFT_PRESSED) {
                YoriWinMlEditStartSelAtCursor(MlEdit, FALSE);
            } else if (YoriWinMlEditSelectionActive(&MlEdit->Ctrl)) {
                YoriWinMlEditClearSelection(MlEdit);
            }
            NewCursorLine = MlEdit->CursorLine;
            NewCursorOffset = MlEdit->CursorOffset + 1;
            if (!MlEdit->TradEditNavigation) {
                if ((NewCursorLine < MlEdit->LinesPopulated &&
                     NewCursorOffset > MlEdit->LineArray[NewCursorLine].LengthInChars)) {

                    NewCursorLine = NewCursorLine + 1;
                    NewCursorOffset = 0;
                    YoriWinMlEditTrimAutoIndent(MlEdit, MlEdit->CursorLine, 0);
                }
            }
            YoriWinMlEditSetCursorPointInt(MlEdit, NewCursorOffset, NewCursorLine);
            if (Event->u.KeyDown.CtrlMask & SHIFT_PRESSED) {
                YoriWinMlEditExtendSelectionToCursor(MlEdit);
            }
            YoriWinMlEditClearDesiredDisplayOffset(MlEdit);
            YoriWinMlEditEnsureCursorShown(MlEdit);
            YoriWinMlEditPaint(MlEdit);
        }
        Recognized = TRUE;
    } else if (Event->u.KeyDown.VirtualKeyCode == VK_HOME) {
        if (Event->u.KeyDown.CtrlMask & SHIFT_PRESSED) {
            YoriWinMlEditStartSelAtCursor(MlEdit, FALSE);
        } else if (YoriWinMlEditSelectionActive(&MlEdit->Ctrl)) {
            YoriWinMlEditClearSelection(MlEdit);
        }
        if (MlEdit->CursorOffset != 0) {
            NewCursorOffset = 0;
            YoriWinMlEditTrimAutoIndent(MlEdit, MlEdit->CursorLine, 0);
            YoriWinMlEditSetCursorPointInt(MlEdit, NewCursorOffset, MlEdit->CursorLine);
            if (Event->u.KeyDown.CtrlMask & SHIFT_PRESSED) {
                YoriWinMlEditExtendSelectionToCursor(MlEdit);
            }
            YoriWinMlEditClearDesiredDisplayOffset(MlEdit);
            YoriWinMlEditEnsureCursorShown(MlEdit);
            YoriWinMlEditPaint(MlEdit);
        }
        Recognized = TRUE;
    } else if (Event->u.KeyDown.VirtualKeyCode == VK_END) {
        YORI_ALLOC_SIZE_T FinalChar = 0;
        if (Event->u.KeyDown.CtrlMask & SHIFT_PRESSED) {
            YoriWinMlEditStartSelAtCursor(MlEdit, FALSE);
        } else if (YoriWinMlEditSelectionActive(&MlEdit->Ctrl)) {
            YoriWinMlEditClearSelection(MlEdit);
        }
        if (MlEdit->CursorLine < MlEdit->LinesPopulated) {
            FinalChar = MlEdit->LineArray[MlEdit->CursorLine].LengthInChars;
        }
        if (MlEdit->CursorOffset != FinalChar) {
            YoriWinMlEditSetCursorPointInt(MlEdit, FinalChar, MlEdit->CursorLine);
            if (Event->u.KeyDown.CtrlMask & SHIFT_PRESSED) {
                YoriWinMlEditExtendSelectionToCursor(MlEdit);
            }
            YoriWinMlEditClearDesiredDisplayOffset(MlEdit);
            YoriWinMlEditEnsureCursorShown(MlEdit);
            YoriWinMlEditPaint(MlEdit);
        }
        Recognized = TRUE;
    } else if (Event->u.KeyDown.VirtualKeyCode == VK_INSERT) {
        if (!MlEdit->ReadOnly) {
            YoriWinMlEditToggleInsert(MlEdit);
            YoriWinMlEditPaint(MlEdit);
        }
        Recognized = TRUE;
    } else if (Event->u.KeyDown.VirtualKeyCode == VK_UP) {
        if (MlEdit->CursorLine != 0) {
            if (Event->u.KeyDown.CtrlMask & SHIFT_PRESSED) {
                YoriWinMlEditStartSelAtCursor(MlEdit, FALSE);
            } else if (YoriWinMlEditSelectionActive(&MlEdit->Ctrl)) {
                YoriWinMlEditClearSelection(MlEdit);
            }
            YoriWinMlEditTrimAutoIndent(MlEdit, MlEdit->CursorLine, 0);
            NewCursorLine = MlEdit->CursorLine - 1;
            YoriWinMlEditPopulateDesiredDisplayOffset(MlEdit);
            YoriWinMlEditFindCursorCharFromDisplayChar(MlEdit,
                                                       NewCursorLine,
                                                       MlEdit->DesiredDisplayCursorOffset,
                                                       &NewCursorOffset,
                                                       NULL);
            YoriWinMlEditSetCursorPointInt(MlEdit, NewCursorOffset, NewCursorLine);
            if (Event->u.KeyDown.CtrlMask & SHIFT_PRESSED) {
                YoriWinMlEditExtendSelectionToCursor(MlEdit);
            }
            YoriWinMlEditEnsureCursorShown(MlEdit);
            YoriWinMlEditPaint(MlEdit);
        }
        Recognized = TRUE;
    } else if (Event->u.KeyDown.VirtualKeyCode == VK_DOWN) {
        if (MlEdit->CursorLine + 1 < MlEdit->LinesPopulated) {
            if (Event->u.KeyDown.CtrlMask & SHIFT_PRESSED) {
                YoriWinMlEditStartSelAtCursor(MlEdit, FALSE);
            } else if (YoriWinMlEditSelectionActive(&MlEdit->Ctrl)) {
                YoriWinMlEditClearSelection(MlEdit);
            }
            YoriWinMlEditTrimAutoIndent(MlEdit, MlEdit->CursorLine, 0);
            NewCursorLine = MlEdit->CursorLine + 1;
            YoriWinMlEditPopulateDesiredDisplayOffset(MlEdit);
            YoriWinMlEditFindCursorCharFromDisplayChar(MlEdit,
                                                       NewCursorLine,
                                                       MlEdit->DesiredDisplayCursorOffset,
                                                       &NewCursorOffset,
                                                       NULL);
            YoriWinMlEditSetCursorPointInt(MlEdit, NewCursorOffset, NewCursorLine);
            if (Event->u.KeyDown.CtrlMask & SHIFT_PRESSED) {
                YoriWinMlEditExtendSelectionToCursor(MlEdit);
            }
            YoriWinMlEditEnsureCursorShown(MlEdit);
            YoriWinMlEditPaint(MlEdit);
        }
        Recognized = TRUE;
    } else if (Event->u.KeyDown.VirtualKeyCode == VK_PRIOR) {
        if (Event->u.KeyDown.CtrlMask & SHIFT_PRESSED) {
            YoriWinMlEditStartSelAtCursor(MlEdit, FALSE);
        } else if (YoriWinMlEditSelectionActive(&MlEdit->Ctrl)) {
            YoriWinMlEditClearSelection(MlEdit);
        }

        if (YoriWinMlEditPageUp(MlEdit)) {
            if (Event->u.KeyDown.CtrlMask & SHIFT_PRESSED) {
                YoriWinMlEditExtendSelectionToCursor(MlEdit);
            }
            YoriWinMlEditPaint(MlEdit);
        }
        Recognized = TRUE;
    } else if (Event->u.KeyDown.VirtualKeyCode == VK_NEXT) {
        if (Event->u.KeyDown.CtrlMask & SHIFT_PRESSED) {
            YoriWinMlEditStartSelAtCursor(MlEdit, FALSE);
        } else if (YoriWinMlEditSelectionActive(&MlEdit->Ctrl)) {
            YoriWinMlEditClearSelection(MlEdit);
        }
        if (YoriWinMlEditPageDown(MlEdit)) {
            if (Event->u.KeyDown.CtrlMask & SHIFT_PRESSED) {
                YoriWinMlEditExtendSelectionToCursor(MlEdit);
            }
            YoriWinMlEditPaint(MlEdit);
        }
        Recognized = TRUE;
    } else if (Event->u.KeyDown.VirtualKeyCode == VK_BACK) {

        if (!MlEdit->ReadOnly && YoriWinMlEditBackspace(MlEdit)) {
            YoriWinMlEditEnsureCursorShown(MlEdit);
            YoriWinMlEditPaint(MlEdit);
        }
        Recognized = TRUE;

    } else if (Event->u.KeyDown.VirtualKeyCode == VK_DELETE) {

        if (!MlEdit->ReadOnly && YoriWinMlEditDelete(MlEdit)) {
            YoriWinMlEditEnsureCursorShown(MlEdit);
            YoriWinMlEditPaint(MlEdit);
        }
        Recognized = TRUE;
    } else if (Event->u.KeyDown.VirtualKeyCode == VK_ESCAPE) {
        if (YoriWinMlEditSelectionActive(&MlEdit->Ctrl)) {
            YoriWinMlEditClearSelection(MlEdit);
            YoriWinMlEditPaint(MlEdit);
        }
        Recognized = TRUE;
    } else if (Event->u.KeyDown.VirtualKeyCode == VK_RETURN) {
        if (!MlEdit->ReadOnly && YoriWinMlEditAddChar(MlEdit, '\r')) {
            YoriWinMlEditEnsureCursorShown(MlEdit);
            YoriWinMlEditPaint(MlEdit);
        }
        Recognized = TRUE;
    }

    return Recognized;
}

/**
 Process a key that may be an enhanced key with ctrl held.  Some of these
 keys can be either enhanced or non-enhanced.

 @param MlEdit Pointer to the multiline edit control, indicating the
        current cursor location.

 @param Event Pointer to the event describing the state of the key being
        pressed.

 @return TRUE to indicate the key has been processed, FALSE if it is an
         unknown key.
 */
BOOLEAN
YoriWinMlEditProcEnhCtrlKey(
    __in PYORIWIN_CTRL_MLEDIT MlEdit,
    __in PYORIWIN_EVENT Event
    )
{
    BOOLEAN Recognized;
    YORI_ALLOC_SIZE_T ProbeLine;
    YORI_ALLOC_SIZE_T ProbeOffset;
    Recognized = FALSE;

    if (Event->u.KeyDown.VirtualKeyCode == VK_HOME) {
        if (Event->u.KeyDown.CtrlMask & SHIFT_PRESSED) {
            YoriWinMlEditStartSelAtCursor(MlEdit, FALSE);
        } else if (YoriWinMlEditSelectionActive(&MlEdit->Ctrl)) {
            YoriWinMlEditClearSelection(MlEdit);
        }
        if (MlEdit->CursorOffset != 0 || MlEdit->CursorLine != 0) {
            YoriWinMlEditSetCursorPointInt(MlEdit, 0, 0);
            if (Event->u.KeyDown.CtrlMask & SHIFT_PRESSED) {
                YoriWinMlEditExtendSelectionToCursor(MlEdit);
            }
            YoriWinMlEditEnsureCursorShown(MlEdit);
            YoriWinMlEditPaint(MlEdit);
        }
        Recognized = TRUE;
    } else if (Event->u.KeyDown.VirtualKeyCode == VK_END) {
        YORI_ALLOC_SIZE_T FinalChar = 0;
        if (Event->u.KeyDown.CtrlMask & SHIFT_PRESSED) {
            YoriWinMlEditStartSelAtCursor(MlEdit, FALSE);
        } else if (YoriWinMlEditSelectionActive(&MlEdit->Ctrl)) {
            YoriWinMlEditClearSelection(MlEdit);
        }
        if (MlEdit->LinesPopulated > 0) {
            FinalChar = MlEdit->LineArray[MlEdit->LinesPopulated - 1].LengthInChars;
            if (MlEdit->CursorLine != MlEdit->LinesPopulated - 1 ||
                MlEdit->CursorOffset != FinalChar) {

                YoriWinMlEditSetCursorPointInt(MlEdit,
                                               FinalChar,
                                               MlEdit->LinesPopulated - 1);
                if (Event->u.KeyDown.CtrlMask & SHIFT_PRESSED) {
                    YoriWinMlEditExtendSelectionToCursor(MlEdit);
                }
                YoriWinMlEditEnsureCursorShown(MlEdit);
                YoriWinMlEditPaint(MlEdit);
            }
        }
        Recognized = TRUE;
    } else if (Event->u.KeyDown.VirtualKeyCode == VK_LEFT) {
        if (Event->u.KeyDown.CtrlMask & SHIFT_PRESSED) {
            YoriWinMlEditStartSelAtCursor(MlEdit, FALSE);
        } else if (YoriWinMlEditSelectionActive(&MlEdit->Ctrl)) {
            YoriWinMlEditClearSelection(MlEdit);
        }
        ProbeLine = MlEdit->CursorLine;
        ProbeOffset = MlEdit->CursorOffset;
        if (ProbeLine < MlEdit->LinesPopulated) {
            while (TRUE) {
                YORI_ALLOC_SIZE_T Index;
                YORI_STRING WhitespaceChars;
                PYORI_STRING Line;

                YoriLibConstantString(&WhitespaceChars, _T(" -\t"));

                Line = &MlEdit->LineArray[ProbeLine];
                Index = ProbeOffset;
                if (Index > Line->LengthInChars) {
                    Index = Line->LengthInChars;
                }
                while(Index > 0 &&
                      YoriLibFindLeftMostCharacter(&WhitespaceChars, Line->StartOfString[Index - 1])) {

                    Index--;
                }
                if (Index == 0 && ProbeLine > 0) {
                    ProbeLine--;
                    ProbeOffset = MlEdit->LineArray[ProbeLine].LengthInChars;
                    continue;
                }
                while(Index > 0 &&
                      (YoriLibFindLeftMostCharacter(&WhitespaceChars, Line->StartOfString[Index - 1]) == NULL)) {
                    Index--;
                }
                MlEdit->CursorLine = ProbeLine;
                MlEdit->CursorOffset = Index;
                break;
            }
        } else {
            MlEdit->CursorOffset = 0;
        }
        if (Event->u.KeyDown.CtrlMask & SHIFT_PRESSED) {
            YoriWinMlEditExtendSelectionToCursor(MlEdit);
        }
        YoriWinMlEditEnsureCursorShown(MlEdit);
        YoriWinMlEditPaint(MlEdit);
    } else if (Event->u.KeyDown.VirtualKeyCode == VK_RIGHT) {
        BOOLEAN SkipCurrentWord;
        if (Event->u.KeyDown.CtrlMask & SHIFT_PRESSED) {
            YoriWinMlEditStartSelAtCursor(MlEdit, FALSE);
        } else if (YoriWinMlEditSelectionActive(&MlEdit->Ctrl)) {
            YoriWinMlEditClearSelection(MlEdit);
        }
        ProbeLine = MlEdit->CursorLine;
        ProbeOffset = MlEdit->CursorOffset;
        SkipCurrentWord = TRUE;
        if (ProbeLine >= MlEdit->LinesPopulated) {
            MlEdit->CursorOffset = 0;
        } else {
            while (ProbeLine < MlEdit->LinesPopulated) {
                YORI_ALLOC_SIZE_T Index;
                YORI_STRING WhitespaceChars;
                PYORI_STRING Line;

                YoriLibConstantString(&WhitespaceChars, _T(" -\t"));

                Line = &MlEdit->LineArray[ProbeLine];
                Index = ProbeOffset;
                if (Index > Line->LengthInChars) {
                    Index = Line->LengthInChars;
                }
                if (SkipCurrentWord) {
                    while(Index < Line->LengthInChars &&
                          YoriLibFindLeftMostCharacter(&WhitespaceChars, Line->StartOfString[Index]) == NULL) {

                        Index++;
                    }
                }
                while(Index < Line->LengthInChars &&
                      YoriLibFindLeftMostCharacter(&WhitespaceChars, Line->StartOfString[Index])) {

                    Index++;
                }
                if (Index == Line->LengthInChars && ProbeLine + 1 < MlEdit->LinesPopulated) {
                    ProbeLine++;
                    ProbeOffset = 0;
                    SkipCurrentWord = FALSE;
                    continue;
                }
                MlEdit->CursorLine = ProbeLine;
                MlEdit->CursorOffset = Index;
                break;
            }
        }
        if (Event->u.KeyDown.CtrlMask & SHIFT_PRESSED) {
            YoriWinMlEditExtendSelectionToCursor(MlEdit);
        }
        YoriWinMlEditEnsureCursorShown(MlEdit);
        YoriWinMlEditPaint(MlEdit);
    }

    return Recognized;
}


/**
 Process input events for a multiline edit control.

 @param Ctrl Pointer to the multiline edit control.

 @param Event Pointer to the input event.

 @return TRUE to indicate that the event was processed and no further
         processing should occur.  FALSE to indicate that regular processing
         should continue (although this does not imply that no processing
         has already occurred.)
 */
BOOLEAN
YoriWinMlEditEventHandler(
    __in PYORIWIN_CTRL Ctrl,
    __in PYORIWIN_EVENT Event
    )
{
    PYORIWIN_CTRL_MLEDIT MlEdit;
    DWORD Index;
    MlEdit = CONTAINING_RECORD(Ctrl, YORIWIN_CTRL_MLEDIT, Ctrl);
    switch(Event->EventType) {
        case YoriWinEventParentDestroyed:
            YoriWinMlEditClearUndo(MlEdit);
            for (Index = 0; Index < MlEdit->LinesPopulated; Index++) {
                YoriLibFreeStringContents(&MlEdit->LineArray[Index]);
            }
            if (MlEdit->LineArray != NULL) {
                YoriLibDereference(MlEdit->LineArray);
                MlEdit->LineArray = NULL;
            }
            YoriLibFreeStringContents(&MlEdit->Caption);
            YoriWinDestroyControl(Ctrl);
            YoriLibDereference(MlEdit);
            break;
        case YoriWinEventLoseFocus:
            ASSERT(MlEdit->HasFocus);
            MlEdit->HasFocus = FALSE;
            YoriWinMlEditPaint(MlEdit);
            break;
        case YoriWinEventGetFocus:
            ASSERT(!MlEdit->HasFocus);
            MlEdit->HasFocus = TRUE;
            YoriWinMlEditPaint(MlEdit);
            break;
        case YoriWinEventKeyDown:

            //
            // This code is trying to handle the AltGr cases while not
            // handling pure right Alt which would normally be an accelerator.
            //

            if (Event->u.KeyDown.CtrlMask == 0 ||
                Event->u.KeyDown.CtrlMask == SHIFT_PRESSED ||
                Event->u.KeyDown.CtrlMask == (LEFT_CTRL_PRESSED | LEFT_ALT_PRESSED) ||
                Event->u.KeyDown.CtrlMask == (LEFT_CTRL_PRESSED | LEFT_ALT_PRESSED | SHIFT_PRESSED) ||
                Event->u.KeyDown.CtrlMask == (LEFT_CTRL_PRESSED | RIGHT_ALT_PRESSED) ||
                Event->u.KeyDown.CtrlMask == (LEFT_CTRL_PRESSED | RIGHT_ALT_PRESSED | SHIFT_PRESSED)) {


                if (!YoriWinMlEditProcEnhKey(MlEdit, Event)) {
                    if (Event->u.KeyDown.Char != '\0' &&
                        Event->u.KeyDown.Char != '\x1b' &&
                        Event->u.KeyDown.Char != '\n') {

                        if (!MlEdit->ReadOnly) {
                            YoriWinMlEditAddChar(MlEdit, Event->u.KeyDown.Char);
                            YoriWinMlEditEnsureCursorShown(MlEdit);
                            YoriWinMlEditPaint(MlEdit);
                            return TRUE;
                        }
                    }
                }
            } else if (Event->u.KeyDown.CtrlMask == LEFT_CTRL_PRESSED ||
                       Event->u.KeyDown.CtrlMask == RIGHT_CTRL_PRESSED) {

                if (!YoriWinMlEditProcEnhCtrlKey(MlEdit, Event)) {
                    if (Event->u.KeyDown.VirtualKeyCode == 'A') {
                        if (MlEdit->LinesPopulated > 0) {
                            YoriWinMlEditSetSelectionRange(Ctrl,
                                                           0,
                                                           0,
                                                           MlEdit->LinesPopulated - 1,
                                                           MlEdit->LineArray[MlEdit->LinesPopulated - 1].LengthInChars);
                        }
                        return TRUE;
                    } else if (Event->u.KeyDown.VirtualKeyCode == 'C') {
                        if (YoriWinMlEditCopySelectedText(Ctrl)) {
                            YoriWinMlEditClearSelection(MlEdit);
                            YoriWinMlEditEnsureCursorShown(MlEdit);
                            YoriWinMlEditPaint(MlEdit);
                        }
                        return TRUE;
                    } else if (Event->u.KeyDown.VirtualKeyCode == 'R') {
                        if (!MlEdit->ReadOnly && YoriWinMlEditRedo(MlEdit)) {
                            YoriWinMlEditEnsureCursorShown(MlEdit);
                            YoriWinMlEditPaint(MlEdit);
                        }
                        return TRUE;
                    } else if (Event->u.KeyDown.VirtualKeyCode == 'V') {
                        if (!MlEdit->ReadOnly && YoriWinMlEditPasteText(Ctrl)) {
                            YoriWinMlEditEnsureCursorShown(MlEdit);
                            YoriWinMlEditPaint(MlEdit);
                        }
                        return TRUE;
                    } else if (Event->u.KeyDown.VirtualKeyCode == 'X') {
                        if (!MlEdit->ReadOnly && YoriWinMlEditCutSelectedText(Ctrl)) {
                            YoriWinMlEditEnsureCursorShown(MlEdit);
                            YoriWinMlEditPaint(MlEdit);
                        }
                        return TRUE;
                    } else if (Event->u.KeyDown.VirtualKeyCode == 'Y') {
                        if (!MlEdit->ReadOnly && YoriWinMlEditDeleteLine(MlEdit)) {
                            YoriWinMlEditEnsureCursorShown(MlEdit);
                            YoriWinMlEditPaint(MlEdit);
                        }
                        return TRUE;
                    } else if (Event->u.KeyDown.VirtualKeyCode == 'Z') {
                        if (!MlEdit->ReadOnly && YoriWinMlEditUndo(MlEdit)) {
                            YoriWinMlEditEnsureCursorShown(MlEdit);
                            YoriWinMlEditPaint(MlEdit);
                        }
                        return TRUE;
                    }
                }
            } else if (Event->u.KeyDown.CtrlMask == LEFT_ALT_PRESSED ||
                       Event->u.KeyDown.CtrlMask == (LEFT_ALT_PRESSED | ENHANCED_KEY)) {
                YoriLibBuildNumericKey(&MlEdit->NumericKeyValue,
                                       &MlEdit->NumericKeyType,
                                       Event->u.KeyDown.VirtualKeyCode,
                                       Event->u.KeyDown.VirtualScanCode);

            } else if (Event->u.KeyDown.CtrlMask == ENHANCED_KEY ||
                       Event->u.KeyDown.CtrlMask == (ENHANCED_KEY | SHIFT_PRESSED)) {
                YoriWinMlEditProcEnhKey(MlEdit, Event);
            } else if (Event->u.KeyDown.CtrlMask == (ENHANCED_KEY | LEFT_CTRL_PRESSED) ||
                       Event->u.KeyDown.CtrlMask == (ENHANCED_KEY | RIGHT_CTRL_PRESSED) ||
                       Event->u.KeyDown.CtrlMask == (SHIFT_PRESSED | LEFT_CTRL_PRESSED) ||
                       Event->u.KeyDown.CtrlMask == (SHIFT_PRESSED | RIGHT_CTRL_PRESSED) ||
                       Event->u.KeyDown.CtrlMask == (ENHANCED_KEY | SHIFT_PRESSED | LEFT_CTRL_PRESSED) ||
                       Event->u.KeyDown.CtrlMask == (ENHANCED_KEY | SHIFT_PRESSED | RIGHT_CTRL_PRESSED)
                       ) {
                YoriWinMlEditProcEnhCtrlKey(MlEdit, Event);
            }
            break;

        case YoriWinEventKeyUp:
            if ((Event->u.KeyUp.CtrlMask & (RIGHT_ALT_PRESSED | LEFT_ALT_PRESSED)) == 0 &&
                !MlEdit->ReadOnly &&
                (MlEdit->NumericKeyValue != 0 ||
                 (Event->u.KeyUp.VirtualKeyCode == VK_MENU && Event->u.KeyUp.Char != 0))) {

                DWORD NumericKeyValue;
                TCHAR Char;

                NumericKeyValue = MlEdit->NumericKeyValue;
                if (NumericKeyValue == 0) {
                    MlEdit->NumericKeyType = YoriLibNumericKeyUnicode;
                    NumericKeyValue = Event->u.KeyUp.Char;
                }

                YoriLibTransNumKeyToChar(NumericKeyValue, MlEdit->NumericKeyType, &Char);
                MlEdit->NumericKeyValue = 0;
                MlEdit->NumericKeyType = YoriLibNumericKeyAscii;

                YoriWinMlEditAddChar(MlEdit, Char);
                YoriWinMlEditEnsureCursorShown(MlEdit);
                YoriWinMlEditPaint(MlEdit);
            }

            break;

        case YoriWinEventMouseWhlDownClient:
        case YoriWinEventMouseWhlDownNonCli:
            YoriWinMlEditNotifyMouseWheel(MlEdit, (YORI_ALLOC_SIZE_T)Event->u.MouseWheel.LinesToMove, FALSE);
            break;

        case YoriWinEventMouseWhlUpClient:
        case YoriWinEventMouseWhlUpNonCli:
            YoriWinMlEditNotifyMouseWheel(MlEdit, (YORI_ALLOC_SIZE_T)Event->u.MouseWheel.LinesToMove, TRUE);
            break;

        case YoriWinEventMouseUpNonCli:
            if (MlEdit->Selection.Active == YoriWinMlEditSelMouseFromTopDown ||
                MlEdit->Selection.Active == YoriWinMlEditSelMouseFromBottomUp) {

                YoriWinMlEditFinishMouseSel(MlEdit);
            }
            // Intentional fallthrough
        case YoriWinEventMouseDownNonCli:
        case YoriWinEventMouseDblClickNonCli:
            {
                PYORIWIN_CTRL Child;
                COORD ChildPoint;
                BOOLEAN InChildClientArea;
                Child = YoriWinFindControlAtCoordinates(Ctrl,
                                                        Event->u.MouseDown.Location,
                                                        FALSE,
                                                        &ChildPoint,
                                                        &InChildClientArea);

                if (Child != NULL) {
                    if (YoriWinTransMouseEventForChild(Event, Child, ChildPoint, InChildClientArea)) {
                        return TRUE;
                    }
                    return FALSE;
                }
            }
            break;
        case YoriWinEventMouseDblClickClient:
            YoriWinMlEditNotifyDoubleClick(MlEdit,
                                           (YORI_ALLOC_SIZE_T)Event->u.MouseDown.Location.X,
                                           (YORI_ALLOC_SIZE_T)Event->u.MouseDown.Location.Y);
            break;
        case YoriWinEventMouseDownClient:
            {
                YORI_ALLOC_SIZE_T NewCursorLine;
                YORI_ALLOC_SIZE_T NewCursorChar;

                if (YoriWinMlEditTransViewCoordToCursor(MlEdit,
                                                        Event->u.MouseDown.Location.X,
                                                        Event->u.MouseDown.Location.Y,
                                                        &NewCursorLine,
                                                        &NewCursorChar)) {
                    if (MlEdit->CursorLine != NewCursorLine) {
                        YoriWinMlEditTrimAutoIndent(MlEdit, MlEdit->CursorLine, 0);
                    } else if (NewCursorChar < MlEdit->CursorOffset) {
                        YoriWinMlEditTrimAutoIndent(MlEdit, MlEdit->CursorLine, NewCursorChar);
                    }

                    YoriWinMlEditSetCursorPointInt(MlEdit, NewCursorChar, NewCursorLine);
                    YoriWinMlEditClearSelection(MlEdit);
                    YoriWinMlEditStartSelAtCursor(MlEdit, TRUE);
                    MlEdit->MouseButtonDown = TRUE;

                    YoriWinMlEditEnsureCursorShown(MlEdit);
                    YoriWinMlEditPaint(MlEdit);
                }
            }
            break;
        case YoriWinEventMouseMoveClient:
            if (MlEdit->MouseButtonDown) {
                YORIWIN_BOUNDED_COORD ClientPos;
                ClientPos.Left = FALSE;
                ClientPos.Right = FALSE;
                ClientPos.Above = FALSE;
                ClientPos.Below = FALSE;
                ClientPos.Pos.X = Event->u.MouseMove.Location.X;
                ClientPos.Pos.Y = Event->u.MouseMove.Location.Y;

                YoriWinMlEditScrollForMouseSel(MlEdit, &ClientPos);
            }
            break;
        case YoriWinEventMouseMoveNonCli:
            if (MlEdit->MouseButtonDown) {
                YORIWIN_BOUNDED_COORD Pos;
                YORIWIN_BOUNDED_COORD ClientPos;
                Pos.Left = FALSE;
                Pos.Right = FALSE;
                Pos.Above = FALSE;
                Pos.Below = FALSE;
                Pos.Pos.X = Event->u.MouseMove.Location.X;
                Pos.Pos.Y = Event->u.MouseMove.Location.Y;

                YoriWinBoundCoordInSubRegion(&Pos, &Ctrl->ClientRect, &ClientPos);

                YoriWinMlEditScrollForMouseSel(MlEdit, &ClientPos);
            }
            break;
        case YoriWinEventMouseMoveOutsideWin:
            if (MlEdit->MouseButtonDown) {

                //
                //  Translate any coordinates that are present into client
                //  relative form.  Anything that's out of bounds will stay
                //  that way.
                //

                YORIWIN_BOUNDED_COORD ClientPos;
                YoriWinBoundCoordInSubRegion(&Event->u.MouseMoveOutsideWindow.Location, &Ctrl->ClientRect, &ClientPos);
                YoriWinMlEditScrollForMouseSel(MlEdit, &ClientPos);
            }
            break;
        case YoriWinEventTimer:
            ASSERT(MlEdit->MouseButtonDown);
            ASSERT(MlEdit->Selection.Active == YoriWinMlEditSelMouseFromTopDown ||
                   MlEdit->Selection.Active == YoriWinMlEditSelMouseFromBottomUp);
            ASSERT(Event->u.Timer.Timer == MlEdit->Timer);
            YoriWinMlEditScrollForMouseSel(MlEdit, &MlEdit->LastMousePos);
            break;
        case YoriWinEventMouseUpClient:
        case YoriWinEventMouseUpOutsideWin:
            if (MlEdit->Selection.Active == YoriWinMlEditSelMouseFromTopDown ||
                MlEdit->Selection.Active == YoriWinMlEditSelMouseFromBottomUp) {

                YoriWinMlEditFinishMouseSel(MlEdit);
            }
            break;
    }

    return FALSE;
}

/**
 Invoked when the user manipulates the scroll bar to indicate that the
 position within the multiline edit should be updated.

 @param ScrollCtrlHandle Pointer to the scroll bar control.
 */
VOID
YoriWinMlEditNotifyScrollChange(
    __in PYORIWIN_CTRL_HANDLE ScrollCtrlHandle
    )
{
    PYORIWIN_CTRL_MLEDIT MlEdit;
    YORI_MAX_UNSIGNED_T ScrollValue;
    COORD ClientSize;
    WORD ElementCountToDisplay;
    PYORIWIN_CTRL ScrollCtrl;
    YORI_ALLOC_SIZE_T NewViewportTop;

    ScrollCtrl = (PYORIWIN_CTRL)ScrollCtrlHandle;
    MlEdit = CONTAINING_RECORD(ScrollCtrl->Parent, YORIWIN_CTRL_MLEDIT, Ctrl);
    ASSERT(MlEdit->VScrollCtrl == ScrollCtrl);

    YoriWinGetCtrlClientSize(&MlEdit->Ctrl, &ClientSize);
    ElementCountToDisplay = ClientSize.Y;
    NewViewportTop = MlEdit->ViewportTop;

    ScrollValue = YoriWinScrollBarGetPosition(ScrollCtrl);
    ASSERT(ScrollValue <= MlEdit->LinesPopulated);
    if (ScrollValue + ElementCountToDisplay > MlEdit->LinesPopulated) {
        if (MlEdit->LinesPopulated >= ElementCountToDisplay) {
            NewViewportTop = MlEdit->LinesPopulated - ElementCountToDisplay;
        } else {
            NewViewportTop = 0;
        }
    } else {
        if (ScrollValue < MlEdit->LinesPopulated) {
            NewViewportTop = (YORI_ALLOC_SIZE_T)ScrollValue;
        }
    }

    if (NewViewportTop != MlEdit->ViewportTop) {
        MlEdit->ViewportTop = NewViewportTop;
        YoriWinMlEditExpandDirtyRange(MlEdit, NewViewportTop, (YORI_ALLOC_SIZE_T)-1);
    } else {
        return;
    }

    if (MlEdit->CursorLine < MlEdit->ViewportTop) {
        YoriWinMlEditSetCursorPointInt(MlEdit,
                                       MlEdit->CursorOffset,
                                       MlEdit->ViewportTop);
    } else if (MlEdit->CursorLine >= MlEdit->ViewportTop + ClientSize.Y) {
        YoriWinMlEditSetCursorPointInt(MlEdit,
                                       MlEdit->CursorOffset,
                                       MlEdit->ViewportTop + ClientSize.Y - 1);
    }

    YoriWinMlEditPaint(MlEdit);
}

/**
 Set the size and location of a multiline edit control, and redraw the
 contents.

 @param CtrlHandle Pointer to the multiline edit to resize or reposition.

 @param CtrlRect Specifies the new size and position of the multiline edit.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinMlEditReposition(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in PSMALL_RECT CtrlRect
    )
{
    PYORIWIN_CTRL Ctrl = (PYORIWIN_CTRL)CtrlHandle;
    PYORIWIN_CTRL_MLEDIT MlEdit;

    Ctrl = (PYORIWIN_CTRL)CtrlHandle;
    MlEdit = CONTAINING_RECORD(Ctrl, YORIWIN_CTRL_MLEDIT, Ctrl);

    if (!YoriWinControlReposition(Ctrl, CtrlRect)) {
        return FALSE;
    }

    if (MlEdit->VScrollCtrl != NULL) {
        SMALL_RECT ScrollBarRect;

        ScrollBarRect.Left = (SHORT)(MlEdit->Ctrl.FullRect.Right - MlEdit->Ctrl.FullRect.Left);
        ScrollBarRect.Right = ScrollBarRect.Left;
        ScrollBarRect.Top = 1;
        ScrollBarRect.Bottom = (SHORT)(MlEdit->Ctrl.FullRect.Bottom - MlEdit->Ctrl.FullRect.Top - 1);

        YoriWinScrollBarReposition(MlEdit->VScrollCtrl, &ScrollBarRect);
    }

    YoriWinMlEditExpandDirtyRange(MlEdit, 0, (YORI_ALLOC_SIZE_T)-1);
    YoriWinMlEditPaintNonClient(MlEdit);
    YoriWinMlEditPaint(MlEdit);

    return TRUE;
}

/**
 Change the read only state of an existing multiline edit control.

 @param CtrlHandle Pointer to the multiline edit control.

 @param NewReadOnlyState TRUE to indicate the multiline edit control should be
        read only, FALSE if it should be writable.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinMlEditSetReadOnly(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in BOOLEAN NewReadOnlyState
    )
{
    PYORIWIN_CTRL Ctrl = (PYORIWIN_CTRL)CtrlHandle;
    PYORIWIN_CTRL_MLEDIT MlEdit;

    Ctrl = (PYORIWIN_CTRL)CtrlHandle;
    MlEdit = CONTAINING_RECORD(Ctrl, YORIWIN_CTRL_MLEDIT, Ctrl);
    MlEdit->ReadOnly = NewReadOnlyState;

    return TRUE;
}


/**
 Create a multiline edit control and add it to a window.  This is destroyed
 when the window is destroyed.

 @param ParentHandle Pointer to the parent window.

 @param Caption Optionally points to the initial caption to display on the top
        of the multiline edit control.  If not supplied, no caption is
        displayed.

 @param Size Specifies the location and size of the multiline edit.

 @param Style Specifies style flags for the multiline edit.

 @return Pointer to the newly created control or NULL on failure.
 */
PYORIWIN_CTRL_HANDLE
YoriWinMlEditCreate(
    __in PYORIWIN_WINDOW_HANDLE ParentHandle,
    __in_opt PYORI_STRING Caption,
    __in PSMALL_RECT Size,
    __in WORD Style
    )
{
    PYORIWIN_CTRL_MLEDIT MlEdit;
    PYORIWIN_WINDOW Parent;
    SMALL_RECT ScrollBarRect;
    PYORIWIN_WINDOW_HANDLE TopLevelWindow;
    PYORIWIN_WINMGR_HANDLE WinMgrHandle;

    Parent = (PYORIWIN_WINDOW)ParentHandle;

    MlEdit = YoriLibReferencedMalloc(sizeof(YORIWIN_CTRL_MLEDIT));
    if (MlEdit == NULL) {
        return NULL;
    }

    ZeroMemory(MlEdit, (DWORD)sizeof(YORIWIN_CTRL_MLEDIT));

    YoriLibInitializeListHead(&MlEdit->Undo);
    YoriLibInitializeListHead(&MlEdit->Redo);

    MlEdit->Ctrl.NotifyEventFn = YoriWinMlEditEventHandler;
    if (!YoriWinCreateControl(Parent, Size, TRUE, TRUE, &MlEdit->Ctrl)) {
        YoriLibDereference(MlEdit);
        return NULL;
    }

    if (Caption != NULL && Caption->LengthInChars > 0) {
        if (!YoriLibCopyString(&MlEdit->Caption, Caption)) {
            YoriWinDestroyControl(&MlEdit->Ctrl);
            YoriLibDereference(MlEdit);
            return NULL;
        }
    }

    if (Style & YORIWIN_MLEDIT_STY_VSCROLLBAR) {

        ScrollBarRect.Left = (SHORT)(MlEdit->Ctrl.FullRect.Right - MlEdit->Ctrl.FullRect.Left);
        ScrollBarRect.Right = ScrollBarRect.Left;
        ScrollBarRect.Top = 1;
        ScrollBarRect.Bottom = (SHORT)(MlEdit->Ctrl.FullRect.Bottom - MlEdit->Ctrl.FullRect.Top - 1);
        MlEdit->VScrollCtrl = YoriWinScrollBarCreate(&MlEdit->Ctrl,
                                                     &ScrollBarRect,
                                                     0,
                                                     YoriWinMlEditNotifyScrollChange);
    }

    if (Style & YORIWIN_MLEDIT_STY_READ_ONLY) {
        MlEdit->ReadOnly = TRUE;
    }

    MlEdit->Ctrl.ClientRect.Top++;
    MlEdit->Ctrl.ClientRect.Left++;
    MlEdit->Ctrl.ClientRect.Bottom--;
    MlEdit->Ctrl.ClientRect.Right--;

    MlEdit->InsertMode = TRUE;
    MlEdit->TextAttributes = MlEdit->Ctrl.DefaultAttributes;
    TopLevelWindow = YoriWinGetTopLevelWindow(Parent);
    WinMgrHandle = YoriWinGetWinMgrHandle(TopLevelWindow);
    MlEdit->SelectedAttributes = YoriWinMgrDefaultColorLookup(WinMgrHandle, YoriWinColorEditSelectedText);
    MlEdit->CaptionAttributes = YoriWinMgrDefaultColorLookup(WinMgrHandle, YoriWinColorMultilineCaption);

    MlEdit->TabWidth = 4;
    YoriWinMlEditClearDesiredDisplayOffset(MlEdit);

    YoriWinMlEditExpandDirtyRange(MlEdit, 0, (YORI_ALLOC_SIZE_T)-1);
    YoriWinMlEditPaintNonClient(MlEdit);
    YoriWinMlEditPaint(MlEdit);

    return &MlEdit->Ctrl;
}


// vim:sw=4:ts=4:et:
