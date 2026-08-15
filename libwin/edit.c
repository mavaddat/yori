/**
 * @file libwin/edit.c
 *
 * Yori window edit control
 *
 * Copyright (c) 2019-2020 Malcolm J. Smith
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


/**
 Specifies legitimate values for horizontal text alignment within the
 edit.
 */
typedef enum _YORIWIN_TEXT_ALIGNMENT {
    YoriWinTextAlignLeft = 0,
    YoriWinTextAlignCenter = 1,
    YoriWinTextAlignRight = 2
} YORIWIN_TEXT_ALIGNMENT;

/**
 Information about the selection region within a edit control.
 */
typedef struct _YORIWIN_EDIT_SELECT {

    /**
     Indicates if a selection is currently active, and if so, what caused the
     activation.
     */
    enum {
        YoriWinEditSelNotActive = 0,
        YoriWinEditSelKbdTopDown = 1,
        YoriWinEditSelKbdBottomUp = 2,
        YoriWinEditSelMouseTopDown = 3,
        YoriWinEditSelMouseBottomUp = 4,
        YoriWinEditSelMouseComplete = 5
    } Active;

    /**
     Specifies the character offset containing the beginning of the selection.
     */
    YORI_ALLOC_SIZE_T FirstCharOffset;

    /**
     Specifies the index after the final character selected on the final line.
     This value can therefore be zero through to the length of string
     inclusive.
     */
    YORI_ALLOC_SIZE_T LastCharOffset;

} YORIWIN_EDIT_SELECT, FAR *PYORIWIN_EDIT_SELECT;

/**
 A set of modification operations that can be performed on the buffer that
 can be undone.
 */
typedef enum _YORIWIN_CTRL_EDIT_UNDO_OP {
    YoriWinEditUndoInsertText = 0,
    YoriWinEditUndoOverwriteText = 1,
    YoriWinEditUndoDeleteText = 2
} YORIWIN_CTRL_EDIT_UNDO_OP;

/**
 Information about a single operation to undo.
 */
typedef struct _YORIWIN_CTRL_EDIT_UNDO {

    /**
     The list of operations that can be undone on the edit control.
     */
    YORI_LIST_ENTRY ListEntry;

    /**
     The type of this operation.
     */
    YORIWIN_CTRL_EDIT_UNDO_OP Op;

    /**
     Information specific to each type of operation.
     */
    union {
        struct {

            /**
             The first offset of the range that was inserted and should be
             deleted on undo.
             */
            YORI_ALLOC_SIZE_T FirstCharOffsetToDelete;

            /**
             The last offset of the range that was inserted and should be
             deleted on undo.
             */
            YORI_ALLOC_SIZE_T LastCharOffsetToDelete;
        } InsertText;

        struct {

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
             The first offset of the range that was overwritten and should be
             deleted on undo.
             */
            YORI_ALLOC_SIZE_T FirstCharOffsetToDelete;

            /**
             The last offset of the range that was overwritten and should be
             deleted on undo.
             */
            YORI_ALLOC_SIZE_T LastCharOffsetToDelete;

            /**
             The first character of the range that should be inserted to replace
             the overwritten text.
             */
            YORI_ALLOC_SIZE_T FirstCharOffset;

            /**
             The offset of the first character that the user changed.  This
             may be after FirstCharOffset because the saved range may be
             larger than the range that the user modified.  This value is
             used to determine the cursor location on undo.
             */
            YORI_ALLOC_SIZE_T FirstCharOffsetModified;

            /**
             The offset of the last character that the user changed.  This
             may be before LastCharOffsetToDelete because the saved range may
             be larger than the range that the user modified.  This value is
             used to determine if a later modification should be part of an
             earlier undo record.
             */
            YORI_ALLOC_SIZE_T LastCharOffsetModified;

            /**
             The text to reinsert on undo.
             */
            YORI_STRING Text;
        } OverwriteText;
    } u;
} YORIWIN_CTRL_EDIT_UNDO, FAR *PYORIWIN_CTRL_EDIT_UNDO;


/**
 A structure describing the contents of a edit control.
 */
typedef struct _YORIWIN_CTRL_EDIT {

    /**
     A common header for all controls
     */
    YORIWIN_CTRL Ctrl;

    /**
     The text contents of the edit control
     */
    YORI_STRING Text;

    /**
     Specifies the selection state of text within the edit control.
     This is encapsulated into a structure purely for readability.
     */
    YORIWIN_EDIT_SELECT Selection;

    /**
     Specifies if the text should be rendered to the left, center, or right of
     each line horizontally.
     */
    YORIWIN_TEXT_ALIGNMENT TextAlign;

    /**
     Specifies the offset within the text string to display.
     */
    YORI_ALLOC_SIZE_T DisplayOffset;

    /**
     Specifies the offset within the text string to insert text.
     */
    YORI_ALLOC_SIZE_T CursorOffset;

    /**
     A stack of changes which can be undone.
     */
    YORI_LIST_ENTRY Undo;

    /**
     A stack of changes which can be redone.
     */
    YORI_LIST_ENTRY Redo;

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
    WORD SelectedTextAttributes;

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
     If TRUE, the control has focus, indicating the cursor should be
     displayed.
     */
    BOOLEAN HasFocus;

    /**
     TRUE if events indicate that the left mouse button is currently held
     down.  FALSE if the mouse button is released.
     */
    BOOLEAN MouseButtonDown;

    /**
     TRUE if only numeric input should be allowed.
     */
    BOOLEAN NumericOnly;

} YORIWIN_CTRL_EDIT, FAR *PYORIWIN_CTRL_EDIT;

//
//  =========================================
//  DISPLAY FUNCTIONS
//  =========================================
//

/**
 Return TRUE if a selection region is active, or FALSE if no selection is
 currently active.

 @param CtrlHandle Pointer to the edit control.

 @return TRUE if a selection region is active, or FALSE if no selection is
         currently active.
 */
BOOLEAN
YoriWinEditSelActive(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle
    )
{
    PYORIWIN_CTRL_EDIT Edit;
    PYORIWIN_CTRL Ctrl;

    Ctrl = (PYORIWIN_CTRL)CtrlHandle;
    Edit = CONTAINING_RECORD(Ctrl, YORIWIN_CTRL_EDIT, Ctrl);

    if (Edit->Selection.Active == YoriWinEditSelNotActive) {
        return FALSE;
    }
    return TRUE;
}

/**
 Adjust the first character to display in the control to ensure the current
 user cursor is visible somewhere within the control.

 @param Edit Pointer to the edit control.
 */
VOID
YoriWinEditEnsureCursorVisible(
    __in PYORIWIN_CTRL_EDIT Edit
    )
{
    COORD ClientSize;
    YORI_ALLOC_SIZE_T CursorDisplayCell;
    PYORIWIN_WINDOW TopLevelWindow;
    PYORIWIN_WINMGR_HANDLE WinMgrHandle;

    TopLevelWindow = YoriWinGetTopLevelWindow(Edit->Ctrl.Parent);
    WinMgrHandle = YoriWinGetWinMgrHandle(TopLevelWindow);

    YoriWinGetCtrlClientSize(&Edit->Ctrl, &ClientSize);

    //
    //  We can't guarantee that the entire selection is on the screen,
    //  but if it's a single line selection that would fit, try to
    //  take that into account.  Do this first so if the cursor would
    //  move the viewport, that takes precedence.
    //

    if (YoriWinEditSelActive(Edit)) {
        YORI_ALLOC_SIZE_T StartSelection;
        YORI_ALLOC_SIZE_T EndSelection;

        YoriWinTextDispOffsetFromBuffer(WinMgrHandle,
                                        &Edit->Text,
                                        1,
                                        Edit->Selection.FirstCharOffset,
                                        &StartSelection);

        YoriWinTextDispOffsetFromBuffer(WinMgrHandle,
                                        &Edit->Text,
                                        1,
                                        Edit->Selection.LastCharOffset,
                                        &EndSelection);

        if (StartSelection < Edit->DisplayOffset) {
            Edit->DisplayOffset = StartSelection;
        } else if (EndSelection >= Edit->DisplayOffset + ClientSize.X) {
            Edit->DisplayOffset = EndSelection - ClientSize.X + 1;
        }
    }

    YoriWinTextDispOffsetFromBuffer(WinMgrHandle,
                                    &Edit->Text,
                                    1,
                                    Edit->CursorOffset,
                                    &CursorDisplayCell);

    if (CursorDisplayCell < Edit->DisplayOffset) {
        Edit->DisplayOffset = CursorDisplayCell;
    } else if (CursorDisplayCell >= Edit->DisplayOffset + ClientSize.X) {
        Edit->DisplayOffset = CursorDisplayCell - ClientSize.X + 1;
    }
}

/**
 Draw the border around the edit control.

 @param Edit Pointer to the edit to draw.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinEditPaintNonClient(
    __in PYORIWIN_CTRL_EDIT Edit
    )
{
    SMALL_RECT NonClientRect;
    WORD Height;

    NonClientRect.Left = 0;
    NonClientRect.Top = 0;
    NonClientRect.Right = (SHORT)(Edit->Ctrl.FullRect.Right - Edit->Ctrl.FullRect.Left);
    NonClientRect.Bottom = (SHORT)(Edit->Ctrl.FullRect.Bottom - Edit->Ctrl.FullRect.Top);

    Height = (WORD)(NonClientRect.Bottom + 1);

    if (Height == 1) {
        YoriWinSetCtrlNonClientCell(&Edit->Ctrl, 0, 0, '[', Edit->TextAttributes);
        YoriWinSetCtrlNonClientCell(&Edit->Ctrl, NonClientRect.Right, 0, ']', Edit->TextAttributes);
    } else {
        YoriWinDrawBorderCtrl(&Edit->Ctrl, &NonClientRect, Edit->TextAttributes, YORIWIN_BORDER_TYPE_SUNKEN);
    }

    return TRUE;
}


/**
 Draw the edit with its current state applied.

 @param Edit Pointer to the edit to draw.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinEditPaint(
    __in PYORIWIN_CTRL_EDIT Edit
    )
{
    WORD WinAttributes;
    WORD TextAttributes;
    WORD StartColumn;
    WORD CellIndex;
    WORD CharIndex;
    YORI_ALLOC_SIZE_T DisplayCursorOffset;
    COORD ClientSize;
    BOOLEAN SelectionActive;
    TCHAR Char;
    YORI_ALLOC_SIZE_T ViewportBufferOffset;
    YORI_ALLOC_SIZE_T Remainder;
    PYORIWIN_WINDOW TopLevelWindow;
    PYORIWIN_WINMGR_HANDLE WinMgrHandle;
    YORI_STRING VisibleString;
    YORI_STRING DisplayCells;

    TopLevelWindow = YoriWinGetTopLevelWindow(Edit->Ctrl.Parent);
    WinMgrHandle = YoriWinGetWinMgrHandle(TopLevelWindow);

    YoriWinGetCtrlClientSize(&Edit->Ctrl, &ClientSize);
    WinAttributes = Edit->Ctrl.DefaultAttributes;
    TextAttributes = Edit->TextAttributes;

    YoriWinTextBufferOffsetFromDisp(WinMgrHandle,
                                    &Edit->Text,
                                    1,
                                    Edit->DisplayOffset,
                                    FALSE,
                                    &ViewportBufferOffset,
                                    &Remainder);

    YoriLibInitEmptyString(&VisibleString);
    VisibleString.StartOfString = &Edit->Text.StartOfString[ViewportBufferOffset];
    VisibleString.LengthInChars = Edit->Text.LengthInChars - ViewportBufferOffset;

    YoriLibInitEmptyString(&DisplayCells);

    if (!YoriWinTextStringToDisplayCells(WinMgrHandle,
                                         &VisibleString,
                                         Remainder,
                                         1,
                                         ClientSize.X,
                                         &DisplayCells)) {
        DisplayCells.StartOfString = VisibleString.StartOfString;
        DisplayCells.LengthInChars = VisibleString.LengthInChars;
        if (DisplayCells.LengthInChars > (YORI_ALLOC_SIZE_T)ClientSize.X) {
            DisplayCells.LengthInChars = ClientSize.X;
        }
    }

    ASSERT(DisplayCells.LengthInChars <= (YORI_ALLOC_SIZE_T)ClientSize.X);
    ASSERT(Edit->CursorOffset >= ViewportBufferOffset);

    YoriWinTextDispOffsetFromBuffer(WinMgrHandle,
                                    &VisibleString,
                                    1,
                                    Edit->CursorOffset - ViewportBufferOffset,
                                    &DisplayCursorOffset);
    DisplayCursorOffset = DisplayCursorOffset + Remainder;
    ASSERT(DisplayCursorOffset <= (YORI_ALLOC_SIZE_T)ClientSize.X);

    //
    //  Calculate the starting cell for the text from the left based
    //  on alignment specification
    //

    StartColumn = 0;
    if (Edit->TextAlign == YoriWinTextAlignRight) {
        StartColumn = (WORD)(ClientSize.X - DisplayCells.LengthInChars);
    } else if (Edit->TextAlign == YoriWinTextAlignCenter) {
        StartColumn = (WORD)((ClientSize.X - DisplayCells.LengthInChars) / 2);
    }

    //
    //  Pad area before the text
    //

    for (CellIndex = 0; CellIndex < StartColumn; CellIndex++) {
        YoriWinSetCtrlClientCell(&Edit->Ctrl, CellIndex, 0, ' ', WinAttributes);
    }

    //
    //  Render the text
    //

    SelectionActive = YoriWinEditSelActive(&Edit->Ctrl);
    for (CharIndex = 0; CharIndex < DisplayCells.LengthInChars; CharIndex++) {
        if (SelectionActive) {
            YORI_ALLOC_SIZE_T DisplayFirstCharOffset;
            YORI_ALLOC_SIZE_T DisplayLastCharOffset;

            TextAttributes = Edit->TextAttributes;

            YoriWinTextDispOffsetFromBuffer(WinMgrHandle,
                                            &Edit->Text,
                                            1,
                                            Edit->Selection.FirstCharOffset,
                                            &DisplayFirstCharOffset);

            YoriWinTextDispOffsetFromBuffer(WinMgrHandle,
                                            &Edit->Text,
                                            1,
                                            Edit->Selection.LastCharOffset,
                                            &DisplayLastCharOffset);

            if (CharIndex + Edit->DisplayOffset >= DisplayFirstCharOffset &&
                CharIndex + Edit->DisplayOffset < DisplayLastCharOffset) {

                TextAttributes = Edit->SelectedTextAttributes;
            }
        }
        Char = DisplayCells.StartOfString[CharIndex];

        //
        //  Nano server interprets NULL as "leave previous contents alone"
        //  which is hazardous for an editor.
        //

        if (Char == 0 && YoriLibIsNanoServer()) {
            Char = ' ';
        }

        YoriWinSetCtrlClientCell(&Edit->Ctrl, (WORD)(StartColumn + CharIndex), 0, Char, TextAttributes);
    }

    //
    //  Pad the area after the text
    //

    for (CellIndex = (WORD)(StartColumn + DisplayCells.LengthInChars); CellIndex < (WORD)(ClientSize.X); CellIndex++) {
        YoriWinSetCtrlClientCell(&Edit->Ctrl, CellIndex, 0, ' ', WinAttributes);
    }

    if (Edit->HasFocus) {
        YoriWinSetCtrlClientCursorPoint(&Edit->Ctrl, (WORD)(StartColumn + DisplayCursorOffset), 0);
    }

    YoriLibFreeStringContents(&DisplayCells);

    return TRUE;
}

/**
 Perform debug only checks to see that the selection state follows whatever
 rules are currently defined for it.

 @param Edit Pointer to the edit control specifying the selection state.
 */
VOID
YoriWinEditCheckSelectionState(
    __in PYORIWIN_CTRL_EDIT Edit
    )
{
    PYORIWIN_EDIT_SELECT Selection;
    Selection = &Edit->Selection;

    if (Selection->Active  == YoriWinEditSelNotActive) {
        return;
    }
    if (Selection->Active == YoriWinEditSelMouseTopDown ||
        Selection->Active == YoriWinEditSelMouseBottomUp) {
        ASSERT(Selection->FirstCharOffset <= Selection->LastCharOffset);
    } else {
        ASSERT(Selection->FirstCharOffset < Selection->LastCharOffset);
    }
    ASSERT(Selection->FirstCharOffset <= Edit->Text.LengthInChars);
    ASSERT(Selection->LastCharOffset <= Edit->Text.LengthInChars);
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
YoriWinEditFreeSingleUndo(
    __in PYORIWIN_CTRL_EDIT_UNDO Undo
    )
{
    switch(Undo->Op) {
        case YoriWinEditUndoOverwriteText:
            YoriLibFreeStringContents(&Undo->u.OverwriteText.Text);
            break;
        case YoriWinEditUndoDeleteText:
            YoriLibFreeStringContents(&Undo->u.DeleteText.Text);
            break;
    }

    YoriLibFree(Undo);
}

/**
 Free all undo entries that are linked into the edit control.

 @param Edit Pointer to the edit control.
 */
VOID
YoriWinEditClearUndo(
    __in PYORIWIN_CTRL_EDIT Edit
    )
{
    PYORI_LIST_ENTRY ListHead;
    PYORI_LIST_ENTRY ListEntry;
    PYORIWIN_CTRL_EDIT_UNDO Undo;

    ListHead = &Edit->Undo;
    while (ListHead != NULL) {

        ListEntry = YoriLibGetNextListEntry(ListHead, NULL);
        while (ListEntry != NULL) {
            YoriLibRemoveListItem(ListEntry);
            Undo = CONTAINING_RECORD(ListEntry, YORIWIN_CTRL_EDIT_UNDO, ListEntry);
            YoriWinEditFreeSingleUndo(Undo);
            ListEntry = YoriLibGetNextListEntry(ListHead, NULL);
        }

        if (ListHead == &Edit->Undo) {
            ListHead = &Edit->Redo;
        } else {
            ListHead = NULL;
        }
    }
}

/**
 Free all redo entries that are linked into the edit control.

 @param Edit Pointer to the edit control.
 */
VOID
YoriWinEditClearRedo(
    __in PYORIWIN_CTRL_EDIT Edit
    )
{
    PYORI_LIST_ENTRY ListEntry;
    PYORIWIN_CTRL_EDIT_UNDO Undo;

    ListEntry = YoriLibGetNextListEntry(&Edit->Redo, NULL);
    while (ListEntry != NULL) {
        YoriLibRemoveListItem(ListEntry);
        Undo = CONTAINING_RECORD(ListEntry, YORIWIN_CTRL_EDIT_UNDO, ListEntry);
        YoriWinEditFreeSingleUndo(Undo);
        ListEntry = YoriLibGetNextListEntry(&Edit->Redo, NULL);
    }
}

/**
 Check if a new modification should be included in a previous undo entry
 because the new modification is immediately before the range in the previous
 entry.

 @param Edit Pointer to the edit control.

 @param ExistingFirstCharOffset Specifies the beginning offset of the range
        currently covered by an undo record.

 @param ProposedLastCharOffset Specifies the ending offset of a newly modified
        range.

 @return TRUE to indicate that the new change is immediately before the
         previous undo record.  FALSE to indicate it requires a new entry.
 */
BOOLEAN
YoriWinEditRangePrevious(
    __in PYORIWIN_CTRL_EDIT Edit,
    __in YORI_ALLOC_SIZE_T ExistingFirstCharOffset,
    __in YORI_ALLOC_SIZE_T ProposedLastCharOffset
    )
{
    UNREFERENCED_PARAMETER(Edit);

    if (ExistingFirstCharOffset == ProposedLastCharOffset) {

        return TRUE;
    }

    return FALSE;
}

/**
 Check if a new modification should be included in a previous undo entry
 because the new modification is immediately after the range in the previous
 entry.

 @param Edit Pointer to the edit control.

 @param ExistingLastCharOffset Specifies the ending offset of the range
        currently covered by an undo record.

 @param ProposedFirstCharOffset Specifies the beginning offset of a newly
        modified range.

 @return TRUE to indicate that the new change is immediately after the
         previous undo record.  FALSE to indicate it requires a new entry.
 */
BOOLEAN
YoriWinEditRangeNext(
    __in PYORIWIN_CTRL_EDIT Edit,
    __in YORI_ALLOC_SIZE_T ExistingLastCharOffset,
    __in YORI_ALLOC_SIZE_T ProposedFirstCharOffset
    )
{
    UNREFERENCED_PARAMETER(Edit);

    if (ExistingLastCharOffset == ProposedFirstCharOffset) {

        return TRUE;
    }

    return FALSE;
}

/**
 Return an undo record for the incoming operation.  This may be a newly
 allocated undo record, or if the operation is adjacent to the previous
 operation it may return an existing record.

 @param Edit Pointer to the edit control.

 @param Op Specifies the type of the operation.  Only the same type of
        operations can reuse previous records.

 @param FirstCharOffset Specifies the beginning offset of the range that is
        being modified.

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
PYORIWIN_CTRL_EDIT_UNDO
YoriWinEditGetUndoRecordForOp(
    __in PYORIWIN_CTRL_EDIT Edit,
    __in YORIWIN_CTRL_EDIT_UNDO_OP Op,
    __in YORI_ALLOC_SIZE_T FirstCharOffset,
    __in YORI_ALLOC_SIZE_T LastCharOffset,
    __out PBOOLEAN NewRangeBeforeExistingRange
    )
{
    PYORI_LIST_ENTRY ListEntry;
    PYORIWIN_CTRL_EDIT_UNDO Undo = NULL;

    YoriWinEditClearRedo(Edit);

    *NewRangeBeforeExistingRange = FALSE;

    ListEntry = YoriLibGetNextListEntry(&Edit->Undo, NULL);
    if (ListEntry != NULL) {
        Undo = CONTAINING_RECORD(ListEntry, YORIWIN_CTRL_EDIT_UNDO, ListEntry);
        if (Undo->Op != Op) {
            Undo = NULL;
        } else {
            switch (Op) {
                case YoriWinEditUndoInsertText:
                    if (!YoriWinEditRangeNext(Edit, Undo->u.InsertText.LastCharOffsetToDelete, FirstCharOffset)) {
                        Undo = NULL;
                    }
                    break;
                case YoriWinEditUndoDeleteText:
                    if (YoriWinEditRangePrevious(Edit, Undo->u.DeleteText.FirstCharOffset, LastCharOffset)) {
                        *NewRangeBeforeExistingRange = TRUE;
                    } else if (!YoriWinEditRangeNext(Edit, Undo->u.DeleteText.FirstCharOffset, FirstCharOffset)) {
                        Undo = NULL;
                    }
                    break;
                case YoriWinEditUndoOverwriteText:
                    if (!YoriWinEditRangeNext(Edit, Undo->u.OverwriteText.LastCharOffsetModified, FirstCharOffset)) {
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
        Undo = YoriLibMalloc(sizeof(YORIWIN_CTRL_EDIT_UNDO));
        if (Undo == NULL) {
            YoriWinEditClearUndo(Edit);
            return NULL;
        }

        ZeroMemory(Undo, sizeof(YORIWIN_CTRL_EDIT_UNDO));
        YoriLibInsertList(&Edit->Undo, &Undo->ListEntry);

        Undo->Op = Op;

        switch(Op) {
            case YoriWinEditUndoInsertText:
                Undo->u.InsertText.FirstCharOffsetToDelete = FirstCharOffset;
                Undo->u.InsertText.LastCharOffsetToDelete = LastCharOffset;
                break;
            case YoriWinEditUndoDeleteText:
                Undo->u.DeleteText.FirstCharOffset = FirstCharOffset;
                YoriLibInitEmptyString(&Undo->u.DeleteText.Text);
                break;
            case YoriWinEditUndoOverwriteText:
                Undo->u.OverwriteText.FirstCharOffsetToDelete = FirstCharOffset;
                Undo->u.OverwriteText.LastCharOffsetToDelete = LastCharOffset;
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
YoriWinEditEnsureBuffAroundStr(
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

 @param CtrlHandle Pointer to the edit control.

 @return TRUE if there are operations available to undo, FALSE if there
         are not.
 */
BOOLEAN
YoriWinEditIsUndoAvailable(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle
    )
{
    PYORIWIN_CTRL_EDIT Edit;
    PYORIWIN_CTRL Ctrl;

    Ctrl = (PYORIWIN_CTRL)CtrlHandle;
    Edit = CONTAINING_RECORD(Ctrl, YORIWIN_CTRL_EDIT, Ctrl);

    if (!YoriLibIsListEmpty(&Edit->Undo)) {
        return TRUE;
    }

    return FALSE;
}

/**
 Return TRUE to indicate that there are records specifying how to redo
 previous operations.

 @param CtrlHandle Pointer to the edit control.

 @return TRUE if there are operations available to redo, FALSE if there
         are not.
 */
BOOLEAN
YoriWinEditIsRedoAvailable(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle
    )
{
    PYORIWIN_CTRL_EDIT Edit;
    PYORIWIN_CTRL Ctrl;

    Ctrl = (PYORIWIN_CTRL)CtrlHandle;
    Edit = CONTAINING_RECORD(Ctrl, YORIWIN_CTRL_EDIT, Ctrl);

    if (!YoriLibIsListEmpty(&Edit->Redo)) {
        return TRUE;
    }

    return FALSE;
}

BOOLEAN
YoriWinEditDeleteTextRange(
    __in PYORIWIN_CTRL_EDIT Edit,
    __in BOOLEAN ProcessingUndo,
    __in YORI_ALLOC_SIZE_T FirstCharOffset,
    __in YORI_ALLOC_SIZE_T LastCharOffset
    );

BOOLEAN
YoriWinEditInsertTextRange(
    __in PYORIWIN_CTRL_EDIT Edit,
    __in BOOLEAN ProcessingUndo,
    __in YORI_ALLOC_SIZE_T FirstCharOffset,
    __in PYORI_STRING Text
    );

BOOLEAN
YoriWinEditGetTextRange(
    __in PYORIWIN_CTRL_EDIT Edit,
    __in YORI_ALLOC_SIZE_T FirstCharOffset,
    __in YORI_ALLOC_SIZE_T LastCharOffset,
    __out PYORI_STRING SelectedText
    );

/**
 Given an undo record, generate a record that would undo the undo.

 @param Edit Pointer to the edit control containing the
        state of the buffer before the undo record has been applied.

 @param Undo Pointer to an undo record indicating changes to perform.

 @param AddToUndoList If FALSE, the resulting undo of the undo should be added
        to the Redo list.  If TRUE, the undo here is already a redo, so the
        undo of the undo (of the undo) goes onto the undo list.

 @return Pointer to a newly allocated undo of the undo record.
 */
PYORIWIN_CTRL_EDIT_UNDO
YoriWinEditGenRedoRecordForUndo(
    __in PYORIWIN_CTRL_EDIT Edit,
    __in PYORIWIN_CTRL_EDIT_UNDO Undo,
    __in BOOLEAN AddToUndoList
    )
{
    PYORIWIN_CTRL_EDIT_UNDO Redo;

    //
    //  Note that clearing all undo may remove the undo record that is
    //  causing this redo.  This implies that when this call fails, the
    //  caller cannot continue using the undo record.
    //

    Redo = YoriLibMalloc(sizeof(YORIWIN_CTRL_EDIT_UNDO));
    if (Redo == NULL) {
        YoriWinEditClearUndo(Edit);
        return NULL;
    }

    ZeroMemory(Redo, (DWORD)sizeof(YORIWIN_CTRL_EDIT_UNDO));
    if (AddToUndoList) {
        YoriLibInsertList(&Edit->Undo, &Redo->ListEntry);
    } else {
        YoriLibInsertList(&Edit->Redo, &Redo->ListEntry);
    }

    switch(Undo->Op) {
        case YoriWinEditUndoInsertText:
            Redo->Op = YoriWinEditUndoDeleteText;
            Redo->u.DeleteText.FirstCharOffset = Undo->u.InsertText.FirstCharOffsetToDelete;
            if (!YoriWinEditGetTextRange(Edit,
                                         Undo->u.InsertText.FirstCharOffsetToDelete,
                                         Undo->u.InsertText.LastCharOffsetToDelete,
                                         &Redo->u.DeleteText.Text)) {
                YoriWinEditClearUndo(Edit);
                return NULL;
            }

            break;
        case YoriWinEditUndoDeleteText:
            Redo->Op = YoriWinEditUndoInsertText;
            Redo->u.InsertText.FirstCharOffsetToDelete = Undo->u.DeleteText.FirstCharOffset;
            Redo->u.InsertText.LastCharOffsetToDelete = Undo->u.DeleteText.FirstCharOffset + Undo->u.DeleteText.Text.LengthInChars;
            break;
        case YoriWinEditUndoOverwriteText:

            Redo->Op = Undo->Op;
            Redo->u.OverwriteText.FirstCharOffsetToDelete = Undo->u.OverwriteText.FirstCharOffset;
            Redo->u.OverwriteText.LastCharOffsetToDelete = Undo->u.OverwriteText.FirstCharOffset + Undo->u.OverwriteText.Text.LengthInChars;
            Redo->u.OverwriteText.FirstCharOffset = Undo->u.OverwriteText.FirstCharOffsetToDelete;

            if (!YoriWinEditGetTextRange(Edit,
                                         Undo->u.OverwriteText.FirstCharOffsetToDelete,
                                         Undo->u.OverwriteText.LastCharOffsetToDelete,
                                         &Redo->u.OverwriteText.Text)) {
                YoriWinEditClearUndo(Edit);
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

 @param Edit Pointer to the edit control indicating the buffer and cursor
        position.

 @param Undo Pointer to the undo record indicating the changes to perform.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinEditApplyUndoRecord(
    __in PYORIWIN_CTRL_EDIT Edit,
    __in PYORIWIN_CTRL_EDIT_UNDO Undo
    )
{
    BOOLEAN Success;
    YORI_ALLOC_SIZE_T NewLastLine;
    YORI_ALLOC_SIZE_T NewLastCharOffset;

    Success = FALSE;
    switch(Undo->Op) {
        case YoriWinEditUndoInsertText:
            Success = YoriWinEditDeleteTextRange(Edit, TRUE, Undo->u.InsertText.FirstCharOffsetToDelete, Undo->u.InsertText.LastCharOffsetToDelete);
            if (Success) {
                Edit->CursorOffset = Undo->u.InsertText.FirstCharOffsetToDelete;
            }
            break;
        case YoriWinEditUndoDeleteText:
            Success = YoriWinEditInsertTextRange(Edit, TRUE, Undo->u.DeleteText.FirstCharOffset, &Undo->u.DeleteText.Text);
            if (Success) {
                Edit->CursorOffset = Undo->u.DeleteText.FirstCharOffset + Undo->u.DeleteText.Text.LengthInChars;
            }
            break;
        case YoriWinEditUndoOverwriteText:
            NewLastLine = 0;
            NewLastCharOffset = 0;
            Success = YoriWinEditDeleteTextRange(Edit, TRUE, Undo->u.OverwriteText.FirstCharOffsetToDelete, Undo->u.OverwriteText.LastCharOffsetToDelete);
            if (Success) {
                Success = YoriWinEditInsertTextRange(Edit, TRUE, Undo->u.OverwriteText.FirstCharOffset, &Undo->u.OverwriteText.Text);
                if (Success) {
                    Edit->CursorOffset = Undo->u.OverwriteText.FirstCharOffsetModified;
                }
            }
            break;
    }

    return Success;
}

/**
 Undo the most recent change to a edit control.

 @param CtrlHandle Pointer to the edit control.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinEditUndo(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle
    )
{
    PYORIWIN_CTRL_EDIT_UNDO Undo = NULL;
    PYORIWIN_CTRL_EDIT_UNDO Redo;
    PYORIWIN_CTRL_EDIT Edit;
    PYORIWIN_CTRL Ctrl;
    BOOLEAN Success;

    Ctrl = (PYORIWIN_CTRL)CtrlHandle;
    Edit = CONTAINING_RECORD(Ctrl, YORIWIN_CTRL_EDIT, Ctrl);

    if (YoriLibIsListEmpty(&Edit->Undo)) {
        return FALSE;
    }

    Undo = CONTAINING_RECORD(Edit->Undo.Next, YORIWIN_CTRL_EDIT_UNDO, ListEntry);

    Redo = YoriWinEditGenRedoRecordForUndo(Edit, Undo, FALSE);
    if (Redo == NULL) {
        return FALSE;
    }

    Success = YoriWinEditApplyUndoRecord(Edit, Undo);

    if (Success) {
        YoriLibRemoveListItem(&Undo->ListEntry);
        YoriWinEditFreeSingleUndo(Undo);
    } else {
        YoriLibRemoveListItem(&Redo->ListEntry);
        YoriWinEditFreeSingleUndo(Redo);
    }

    return Success;
}

/**
 Redo the most recently undone change to a edit control.

 @param CtrlHandle Pointer to the edit control.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinEditRedo(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle
    )
{
    PYORIWIN_CTRL_EDIT_UNDO Undo = NULL;
    PYORIWIN_CTRL_EDIT_UNDO Redo;
    PYORIWIN_CTRL_EDIT Edit;
    PYORIWIN_CTRL Ctrl;

    Ctrl = (PYORIWIN_CTRL)CtrlHandle;
    Edit = CONTAINING_RECORD(Ctrl, YORIWIN_CTRL_EDIT, Ctrl);

    if (YoriLibIsListEmpty(&Edit->Redo)) {
        return FALSE;
    }

    Undo = CONTAINING_RECORD(Edit->Redo.Next, YORIWIN_CTRL_EDIT_UNDO, ListEntry);

    Redo = YoriWinEditGenRedoRecordForUndo(Edit, Undo, TRUE);
    if (Redo == NULL) {
        return FALSE;
    }

    if (!YoriWinEditApplyUndoRecord(Edit, Undo)) {
        YoriLibRemoveListItem(&Redo->ListEntry);
        YoriWinEditFreeSingleUndo(Redo);
        return FALSE;
    }

    YoriLibRemoveListItem(&Undo->ListEntry);
    YoriWinEditFreeSingleUndo(Undo);

    return TRUE;
}

//
//  =========================================
//  BUFFER MANIPULATION FUNCTIONS
//  =========================================
//

/**
 Populate a caller allocated string with a range of text from the edit control.
 This routine performs no length checking.

 @param Edit Pointer to the edit control.

 @param FirstCharOffset Indicates the beginning offset of the text to
        obtain.

 @param LastCharOffset Indicates the ending offset of the text to
        obtain.

 @param SelectedText Pointer to a string to populate with text.
 */
VOID
YoriWinEditPopulateTextRange(
    __in PYORIWIN_CTRL_EDIT Edit,
    __in YORI_ALLOC_SIZE_T FirstCharOffset,
    __in YORI_ALLOC_SIZE_T LastCharOffset,
    __inout PYORI_STRING SelectedText
    )
{
    YORI_ALLOC_SIZE_T CharsInRange;
    PYORI_STRING Line;

    Line = &Edit->Text;

    ASSERT(FirstCharOffset < LastCharOffset);
    if (FirstCharOffset >= LastCharOffset) {
        return;
    }

    CharsInRange = LastCharOffset - FirstCharOffset;


    memcpy(SelectedText->StartOfString, &Line->StartOfString[FirstCharOffset], CharsInRange * sizeof(TCHAR));
    SelectedText->LengthInChars = CharsInRange;
}

/**
 Allocate a string and return a range of text from the edit control.

 @param Edit Pointer to the edit control.

 @param FirstCharOffset Indicates the beginning offset of the text to
        obtain.

 @param LastCharOffset Indicates the ending offset of the text to
        obtain.

 @param SelectedText Pointer to a string to populate with text.  This string
        will be allocated in this routine.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinEditGetTextRange(
    __in PYORIWIN_CTRL_EDIT Edit,
    __in YORI_ALLOC_SIZE_T FirstCharOffset,
    __in YORI_ALLOC_SIZE_T LastCharOffset,
    __out PYORI_STRING SelectedText
    )
{
    YORI_ALLOC_SIZE_T CharsInRange;
    PYORI_STRING Line;

    Line = &Edit->Text;

    if (FirstCharOffset >= LastCharOffset) {
        YoriLibInitEmptyString(SelectedText);
        return TRUE;
    }

    CharsInRange = LastCharOffset - FirstCharOffset;

    if (!YoriLibAllocateString(SelectedText, CharsInRange + 1)) {
        return FALSE;
    }

    YoriWinEditPopulateTextRange(Edit, FirstCharOffset, LastCharOffset, SelectedText);
    SelectedText->StartOfString[CharsInRange] = '\0';
    return TRUE;
}

/**
 Check if a numeric edit would still be numeric after a specified
 modification.

 @param Edit Pointer to the edit control.

 @param FirstCharOffset Specifies the offset where text will be modified.

 @param LastCharToDelete If nonzero, specifies a range that should be deleted.
        FirstCharOffset is the first char to remove, LastCharToDelete is one
        passed the last character to remove.

 @param Text Pointer to text to overwrite or insert.  Only meaningful if
        LastCharToDelete is zero, and required to be present when
        LastCharToDelete is zero.

 @param InsertMode If TRUE, text should be inserted; if FALSE, text should be
        overwritten.  Ignored if LastCharToDelete is nonzero.

 @return TRUE if the modification leaves the edit as a numeric value, FALSE
         if it does not.
 */
BOOLEAN
YoriWinEditIsValidNumericInput(
    __in PYORIWIN_CTRL_EDIT Edit,
    __in YORI_ALLOC_SIZE_T FirstCharOffset,
    __in YORI_ALLOC_SIZE_T LastCharToDelete,
    __in_opt _When_(LastCharToDelete == 0, __in) PYORI_STRING Text,
    __in BOOLEAN InsertMode
    )
{
    YORI_STRING TempString;
    TCHAR StringBuffer[32];
    YORI_MAX_SIGNED_T llTemp;
    YORI_ALLOC_SIZE_T CharsConsumed;
    YORI_ALLOC_SIZE_T LengthNeeded;

    //
    //  Either we're removing a range of text (LastCharToDelete says which)
    //  or we're adding text (Text says which.)
    //

    ASSERT(LastCharToDelete != 0 || Text != NULL);

    //
    //  To validate the 0x/0n type prefixes, just pass the string to the
    //  number converter and see if it works.  This is feasible-ish because
    //  numbers have to be small.  Here we use a stack buffer and summarily
    //  reject anything that wouldn't fit as non-numeric (since we can't
    //  convert it to an integer anyway.)
    //
    //  First, calculate and validate the buffer length.
    //

    if (LastCharToDelete != 0) {
        LengthNeeded = Edit->Text.LengthInChars - (LastCharToDelete - FirstCharOffset);
    } else if (InsertMode) {
        LengthNeeded = Edit->Text.LengthInChars + Text->LengthInChars;
    } else {
        LengthNeeded = Edit->Text.LengthInChars;
        if (FirstCharOffset + Text->LengthInChars > LengthNeeded) {
            LengthNeeded = FirstCharOffset + Text->LengthInChars;
        }
    }

    if (LengthNeeded > sizeof(StringBuffer)/sizeof(StringBuffer[0])) {
        return FALSE;
    }

    ASSERT(FirstCharOffset <= Edit->Text.LengthInChars);

    //
    //  Construct a stack buffer containing the merged text.
    //

    if (LastCharToDelete != 0) {
        ASSERT(LastCharToDelete <= Edit->Text.LengthInChars);
        if (FirstCharOffset > 0 &&
            Edit->Text.LengthInChars > 0) {

            memcpy(StringBuffer, Edit->Text.StartOfString, FirstCharOffset * sizeof(TCHAR));
        }

        if (Edit->Text.LengthInChars > LastCharToDelete) {
            memcpy(&StringBuffer[FirstCharOffset], &Edit->Text.StartOfString[LastCharToDelete], (Edit->Text.LengthInChars - LastCharToDelete) * sizeof(TCHAR));
        }
    } else if (InsertMode) {
        ASSERT(Text->LengthInChars > 0);

        if (FirstCharOffset > 0 &&
            Edit->Text.LengthInChars > 0) {

            memcpy(StringBuffer, Edit->Text.StartOfString, FirstCharOffset * sizeof(TCHAR));
        }

        memcpy(&StringBuffer[FirstCharOffset], Text->StartOfString, Text->LengthInChars * sizeof(TCHAR));

        if (FirstCharOffset < Edit->Text.LengthInChars) {
            memcpy(&StringBuffer[FirstCharOffset + Text->LengthInChars], &Edit->Text.StartOfString[FirstCharOffset], (Edit->Text.LengthInChars - FirstCharOffset) * sizeof(TCHAR));
        }
    } else {
        ASSERT(Text->LengthInChars > 0);
        memcpy(StringBuffer, Edit->Text.StartOfString, Edit->Text.LengthInChars * sizeof(TCHAR));
        memcpy(&StringBuffer[FirstCharOffset], Text->StartOfString, Text->LengthInChars * sizeof(TCHAR));
    }

    YoriLibInitEmptyString(&TempString);
    TempString.StartOfString = StringBuffer;
    TempString.LengthInChars = LengthNeeded;

    //
    //  See if the string can be perfectly converted to a number with no
    //  leftover chars.
    //

    if (!YoriLibStringToNumber(&TempString, FALSE, &llTemp, &CharsConsumed) ||
        CharsConsumed < LengthNeeded) {

        return FALSE;
    }

    return TRUE;
}


/**
 Delete a range of text from an edit control.

 @param Edit Pointer to the edit control.

 @param ProcessingUndo TRUE if this delete is caused by processing an undo,
        indicating that this routine should not generate an undo record.
        FALSE if this routine should generate a delete undo record.

 @param FirstCharOffset Indicates the beginning offset of the text to
        delete.

 @param LastCharOffset Indicates the end offset of the text to delete.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinEditDeleteTextRange(
    __in PYORIWIN_CTRL_EDIT Edit,
    __in BOOLEAN ProcessingUndo,
    __in YORI_ALLOC_SIZE_T FirstCharOffset,
    __in YORI_ALLOC_SIZE_T LastCharOffset
    )
{
    PYORIWIN_CTRL_EDIT_UNDO Undo = NULL;
    YORI_ALLOC_SIZE_T CharsToCopy;
    YORI_ALLOC_SIZE_T CharsToDelete;
    PYORI_STRING Line;

    //
    //  The second condition can't happen given the first, but it's made
    //  explicit to help the analyzer realize why we don't need a text buffer
    //  to delete characters below.
    //

    if (FirstCharOffset >= LastCharOffset || LastCharOffset == 0) {
        return TRUE;
    }

    __analysis_assume(LastCharOffset > 0);

    if (Edit->NumericOnly &&
        !YoriWinEditIsValidNumericInput(Edit, FirstCharOffset, LastCharOffset, NULL, FALSE)) {

        ASSERT(!ProcessingUndo);
        return FALSE;
    }

    if (!ProcessingUndo) {
        BOOLEAN RangeBeforeExistingRange;
        Undo = YoriWinEditGetUndoRecordForOp(Edit, YoriWinEditUndoDeleteText, FirstCharOffset, LastCharOffset, &RangeBeforeExistingRange);
        if (Undo != NULL) {
            YORI_STRING Text;
            YORI_ALLOC_SIZE_T CharsNeeded;

            CharsNeeded = LastCharOffset - FirstCharOffset;

            if (!YoriWinEditEnsureBuffAroundStr(&Undo->u.DeleteText.Text,
                                                CharsNeeded,
                                                RangeBeforeExistingRange,
                                                &Text)) {
                return FALSE;
            }

            YoriWinEditPopulateTextRange(Edit, FirstCharOffset,  LastCharOffset, &Text);
            if (RangeBeforeExistingRange) {
                Undo->u.DeleteText.FirstCharOffset = FirstCharOffset;
            }
        }
    }

    Line = &Edit->Text;

    CharsToDelete = LastCharOffset - FirstCharOffset;
    CharsToCopy = Line->LengthInChars - LastCharOffset;

    if (CharsToCopy > 0) {
        memmove(&Line->StartOfString[FirstCharOffset],
                &Line->StartOfString[LastCharOffset],
                CharsToCopy * sizeof(TCHAR));
    }

    Line->LengthInChars = Line->LengthInChars - CharsToDelete;
    return TRUE;
}

/**
 Insert new text into an edit control.

 @param Edit Pointer to the edit control.

 @param ProcessingUndo TRUE if this insert is caused by processing an undo,
        indicating that this routine should not generate an undo record.
        FALSE if this routine should generate an insert undo record.

 @param FirstCharOffset Indicates the beginning offset of the text to
        insert.

 @param Text Pointer to the new text to store in the control.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinEditInsertTextRange(
    __in PYORIWIN_CTRL_EDIT Edit,
    __in BOOLEAN ProcessingUndo,
    __in YORI_ALLOC_SIZE_T FirstCharOffset,
    __in PYORI_STRING Text
    )
{
    PYORIWIN_CTRL_EDIT_UNDO Undo;
    DWORD LengthNeeded;

    if (Text->LengthInChars == 0) {
        return TRUE;
    }

    if (Edit->NumericOnly &&
        !YoriWinEditIsValidNumericInput(Edit, FirstCharOffset, 0, Text, TRUE)) {

        ASSERT(!ProcessingUndo);

        return FALSE;
    }

    ASSERT(FirstCharOffset <= Edit->Text.LengthInChars);

    LengthNeeded = Edit->Text.LengthInChars;
    LengthNeeded = LengthNeeded + Text->LengthInChars + 1;

    if (LengthNeeded >= Edit->Text.LengthAllocated) {
        DWORD LengthDesired;
        YORI_ALLOC_SIZE_T LengthToAllocate;
        LengthDesired = Edit->Text.LengthAllocated;
        LengthDesired = LengthDesired * 2 + 80;
        if (LengthNeeded > LengthDesired) {
            LengthDesired = LengthNeeded;
        }
        LengthToAllocate = YoriLibMaximumAllocationInRange(LengthNeeded, LengthDesired);
        if (LengthToAllocate == 0) {
            return 0;
        }
        if (!YoriLibReallocString(&Edit->Text, LengthToAllocate)) {
            return FALSE;
        }
    }

    if (FirstCharOffset < Edit->Text.LengthInChars) {

        YORI_ALLOC_SIZE_T CharsToCopy;
        CharsToCopy = Edit->Text.LengthInChars - FirstCharOffset;
        memmove(&Edit->Text.StartOfString[FirstCharOffset + Text->LengthInChars],
                &Edit->Text.StartOfString[FirstCharOffset],
                CharsToCopy * sizeof(TCHAR));
    }

    memcpy(&Edit->Text.StartOfString[FirstCharOffset], Text->StartOfString, Text->LengthInChars * sizeof(TCHAR));
    Edit->Text.LengthInChars = Edit->Text.LengthInChars + Text->LengthInChars;

    if (!ProcessingUndo) {
        BOOLEAN RangeBeforeExistingRange;
        Undo = YoriWinEditGetUndoRecordForOp(Edit, YoriWinEditUndoInsertText, FirstCharOffset, FirstCharOffset + Text->LengthInChars, &RangeBeforeExistingRange);
        if (Undo != NULL) {
            Undo->u.InsertText.LastCharOffsetToDelete = FirstCharOffset + Text->LengthInChars;
            ASSERT(Undo->u.InsertText.LastCharOffsetToDelete <= Edit->Text.LengthInChars);
        }
    }

    return TRUE;
}

/**
 Overwrite a range of text in an edit control.

 @param Edit Pointer to the edit control.

 @param ProcessingUndo TRUE if this overwrite is caused by processing an undo,
        indicating that this routine should not generate an undo record.
        FALSE if this routine should generate an overwrite undo record.

 @param FirstCharOffset Indicates the beginning offset of the text to
        overwrite.

 @param Text Pointer to the new text to store in the control.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinEditOverwriteTextRange(
    __in PYORIWIN_CTRL_EDIT Edit,
    __in BOOLEAN ProcessingUndo,
    __in YORI_ALLOC_SIZE_T FirstCharOffset,
    __in PYORI_STRING Text
    )
{
    DWORD LengthNeeded;
    PYORIWIN_CTRL_EDIT_UNDO Undo = NULL;

    if (Text->LengthInChars == 0) {
        return TRUE;
    }

    if (Edit->NumericOnly &&
        !YoriWinEditIsValidNumericInput(Edit, FirstCharOffset, 0, Text, FALSE)) {

        ASSERT(!ProcessingUndo);

        return FALSE;
    }

    if (!ProcessingUndo) {
        BOOLEAN RangeBeforeExistingRange;

        //
        //  At this point we don't know the ending range for this text but it
        //  doesn't matter.  An overwrite will only extend a previous one, not
        //  occur before it, so the end range specified here can be bogus.
        //

        Undo = YoriWinEditGetUndoRecordForOp(Edit, YoriWinEditUndoOverwriteText, FirstCharOffset, FirstCharOffset, &RangeBeforeExistingRange);
        if (Undo != NULL) {

            //
            //  If this is a new record, save off the entire line to be
            //  deleted and restored.  This is done to ensure it doesn't
            //  need to be manipulated on each keypress.
            //

            if (Undo->u.OverwriteText.Text.StartOfString == NULL) {
                PYORI_STRING Line;
                Line = &Edit->Text;
                if (!YoriLibCopyString(&Undo->u.OverwriteText.Text, Line)) {
                    return FALSE;
                }

                Undo->u.OverwriteText.FirstCharOffsetToDelete = 0;
                Undo->u.OverwriteText.LastCharOffsetToDelete = Line->LengthInChars;
                Undo->u.OverwriteText.FirstCharOffset = 0;
            }
        }
    }

    LengthNeeded = FirstCharOffset;
    LengthNeeded = LengthNeeded + Text->LengthInChars;

    if (LengthNeeded >= Edit->Text.LengthAllocated) {
        DWORD LengthDesired;
        YORI_ALLOC_SIZE_T LengthToAllocate;
        LengthDesired = Edit->Text.LengthAllocated;
        LengthDesired = LengthDesired * 2 + 80;
        if (LengthNeeded >= LengthDesired) {
            LengthDesired = LengthNeeded;
        }

        LengthToAllocate = YoriLibMaximumAllocationInRange(LengthNeeded, LengthDesired);
        if (LengthToAllocate == 0) {
            return FALSE;
        }
        if (!YoriLibReallocString(&Edit->Text, LengthToAllocate)) {
            return FALSE;
        }
    }

    memcpy(&Edit->Text.StartOfString[FirstCharOffset], Text->StartOfString, Text->LengthInChars * sizeof(TCHAR));
    if (FirstCharOffset + Text->LengthInChars > Edit->Text.LengthInChars) {
        Edit->Text.LengthInChars = FirstCharOffset + Text->LengthInChars;
    }

    if (Undo != NULL) {
        Undo->u.OverwriteText.LastCharOffsetModified = FirstCharOffset + Text->LengthInChars;
        if (Edit->Text.LengthInChars > Undo->u.OverwriteText.LastCharOffsetToDelete) {
            Undo->u.OverwriteText.LastCharOffsetToDelete = Edit->Text.LengthInChars;
        }
    }

    return TRUE;
}

//
//  =========================================
//  SELECTION FUNCTIONS
//  =========================================
//

/**
 If a selection is currently active, delete all text in the selection.

 @param CtrlHandle Pointer to the edit control containing the selection and
        contents of the buffer.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinEditDeleteSelection(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle
    )
{
    PYORIWIN_CTRL_EDIT Edit;
    PYORIWIN_CTRL Ctrl;
    PYORIWIN_EDIT_SELECT Selection;

    Ctrl = (PYORIWIN_CTRL)CtrlHandle;
    Edit = CONTAINING_RECORD(Ctrl, YORIWIN_CTRL_EDIT, Ctrl);

    if (!YoriWinEditSelActive(&Edit->Ctrl)) {
        return TRUE;
    }

    Selection = &Edit->Selection;
    if (YoriWinEditDeleteTextRange(Edit, FALSE, Selection->FirstCharOffset, Selection->LastCharOffset)) {
        Edit->CursorOffset = Selection->FirstCharOffset;
        Selection->Active = YoriWinEditSelNotActive;
        return TRUE;
    }

    return FALSE;
}


/**
 Build a single continuous string covering the selected range in an edit
 control.

 @param CtrlHandle Pointer to the edit control containing the selection and
        contents of the buffer.

 @param SelectedText On successful completion, populated with a newly
        allocated buffer containing the selected text.  The caller is
        expected to free this buffer with @ref YoriLibFreeStringContents .

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinEditGetSelectedText(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __out PYORI_STRING SelectedText
    )
{
    PYORIWIN_CTRL_EDIT Edit;
    PYORIWIN_CTRL Ctrl;
    PYORIWIN_EDIT_SELECT Selection;

    Ctrl = (PYORIWIN_CTRL)CtrlHandle;
    Edit = CONTAINING_RECORD(Ctrl, YORIWIN_CTRL_EDIT, Ctrl);

    if (!YoriWinEditSelActive(CtrlHandle)) {
        YoriLibInitEmptyString(SelectedText);
        return TRUE;
    }

    Selection = &Edit->Selection;

    return YoriWinEditGetTextRange(Edit, Selection->FirstCharOffset, Selection->LastCharOffset, SelectedText);
}

/**
 Start a new selection from the current cursor location if no selection is
 currently active.  If one is active, this call is ignored.

 @param Edit Pointer to the edit control that describes the selection and
        cursor location.

 @param Mouse If TRUE, the selection is being initiated by mouse operations.
        If FALSE, the selection is being initiated by keyboard operations.
 */
VOID
YoriWinEditStartSelAtCursor(
    __in PYORIWIN_CTRL_EDIT Edit,
    __in BOOLEAN Mouse
    )
{
    PYORIWIN_EDIT_SELECT Selection;

    Selection = &Edit->Selection;

    //
    //  If a mouse selection is active and keyboard selection is requested
    //  or vice versa, clear the existing selection.
    //

    if (Mouse) {
        if (Selection->Active == YoriWinEditSelKbdTopDown ||
            Selection->Active == YoriWinEditSelKbdBottomUp ||
            Selection->Active == YoriWinEditSelMouseComplete) {

            Selection->Active = YoriWinEditSelNotActive;
        }
    } else {
        if (Selection->Active == YoriWinEditSelMouseTopDown ||
            Selection->Active == YoriWinEditSelMouseBottomUp ||
            Selection->Active == YoriWinEditSelMouseComplete) {

            Selection->Active = YoriWinEditSelNotActive;
        }
    }

    //
    //  If no selection is active, activate it.
    //

    if (Selection->Active == YoriWinEditSelNotActive) {
        YORI_ALLOC_SIZE_T EffectiveCursorOffset;
        EffectiveCursorOffset = Edit->CursorOffset;
        if (EffectiveCursorOffset > Edit->Text.LengthInChars) {
            EffectiveCursorOffset = Edit->Text.LengthInChars;
        }

        if (Mouse) {
            Selection->Active = YoriWinEditSelMouseTopDown;
        } else {
            Selection->Active = YoriWinEditSelKbdTopDown;
        }

        Selection->FirstCharOffset = EffectiveCursorOffset;
        Selection->LastCharOffset = EffectiveCursorOffset;
    }
}

/**
 Extend the current selection to the location of the cursor.

 @param Edit Pointer to the edit control that describes the current selection
        and cursor location.
 */
VOID
YoriWinEditExtendSelToCursor(
    __in PYORIWIN_CTRL_EDIT Edit
    )
{
    YORI_ALLOC_SIZE_T AnchorOffset;
    YORI_ALLOC_SIZE_T EffectiveCursorOffset;
    BOOLEAN MouseSelection = FALSE;

    PYORIWIN_EDIT_SELECT Selection;

    AnchorOffset = 0;

    Selection = &Edit->Selection;

    //
    //  Find the place where the selection started from the user's point of
    //  view.  This might be the beginning or end of the selection in terms
    //  of its location in the buffer.
    //

    ASSERT(YoriWinEditSelActive(&Edit->Ctrl));
    if (Selection->Active == YoriWinEditSelKbdTopDown ||
        Selection->Active == YoriWinEditSelMouseTopDown) {

        AnchorOffset = Selection->FirstCharOffset;

    } else if (Selection->Active == YoriWinEditSelKbdBottomUp ||
               Selection->Active == YoriWinEditSelMouseBottomUp) {

        AnchorOffset = Selection->LastCharOffset;

    } else {
        return;
    }

    if (Selection->Active == YoriWinEditSelMouseTopDown ||
        Selection->Active == YoriWinEditSelMouseBottomUp) {

        MouseSelection = TRUE;
    }

    EffectiveCursorOffset = Edit->CursorOffset;

    if (EffectiveCursorOffset > Edit->Text.LengthInChars) {
        EffectiveCursorOffset = Edit->Text.LengthInChars;
    }

    if (EffectiveCursorOffset < AnchorOffset) {
        if (MouseSelection) {
            Selection->Active = YoriWinEditSelMouseBottomUp;
        } else {
            Selection->Active = YoriWinEditSelKbdBottomUp;
        }
        Selection->LastCharOffset = AnchorOffset;
        Selection->FirstCharOffset = EffectiveCursorOffset;
    } else if (EffectiveCursorOffset > AnchorOffset) {
        if (MouseSelection) {
            Selection->Active = YoriWinEditSelMouseTopDown;
        } else {
            Selection->Active = YoriWinEditSelKbdTopDown;
        }
        Selection->FirstCharOffset = AnchorOffset;
        Selection->LastCharOffset = EffectiveCursorOffset;
    } else if (!MouseSelection) {
        Selection->Active = YoriWinEditSelNotActive;
    } else {
        Selection->LastCharOffset = AnchorOffset;
        Selection->FirstCharOffset = AnchorOffset;
    }

    YoriWinEditCheckSelectionState(Edit);
}

/**
 Set the selection range within an edit control to an explicitly provided
 range.

 @param CtrlHandle Pointer to the edit control.

 @param StartOffset Specifies the character offset within the line to the
        beginning of the selection.

 @param EndOffset Specifies the character offset within the line to the
        end of the selection.
 */
VOID
YoriWinEditSetSelectionRange(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in YORI_ALLOC_SIZE_T StartOffset,
    __in YORI_ALLOC_SIZE_T EndOffset
    )
{
    PYORIWIN_CTRL_EDIT Edit;

    Edit = (PYORIWIN_CTRL_EDIT)CtrlHandle;

    Edit->Selection.Active = YoriWinEditSelNotActive;
    Edit->CursorOffset = StartOffset;
    YoriWinEditStartSelAtCursor(Edit, FALSE);
    Edit->CursorOffset = EndOffset;
    YoriWinEditExtendSelToCursor(Edit);
    YoriWinEditEnsureCursorVisible(Edit);
    YoriWinEditPaint(Edit);
}

/**
 Add the currently selected text to the clipboard and delete it from the
 buffer.

 @param CtrlHandle Pointer to the edit control.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinEditCutSelectedText(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle
    )
{
    PYORIWIN_CTRL_EDIT Edit;
    PYORIWIN_CTRL Ctrl;
    YORI_STRING Text;

    Ctrl = (PYORIWIN_CTRL)CtrlHandle;
    Edit = CONTAINING_RECORD(Ctrl, YORIWIN_CTRL_EDIT, Ctrl);
    YoriLibInitEmptyString(&Text);

    if (!YoriWinEditGetSelectedText(CtrlHandle, &Text)) {
        return FALSE;
    }

    if (!YoriLibCopyTextProcFallback(&Text)) {
        YoriLibFreeStringContents(&Text);
        return FALSE;
    }

    YoriLibFreeStringContents(&Text);
    YoriWinEditDeleteSelection(CtrlHandle);
    return TRUE;
}

/**
 Add the currently selected text to the clipboard and clear the selection.

 @param CtrlHandle Pointer to the edit control.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinEditCopySelectedText(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle
    )
{
    PYORIWIN_CTRL_EDIT Edit;
    PYORIWIN_CTRL Ctrl;
    YORI_STRING Text;

    Ctrl = (PYORIWIN_CTRL)CtrlHandle;
    Edit = CONTAINING_RECORD(Ctrl, YORIWIN_CTRL_EDIT, Ctrl);
    YoriLibInitEmptyString(&Text);

    if (!YoriWinEditGetSelectedText(CtrlHandle, &Text)) {
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

 @param CtrlHandle Pointer to the edit control.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinEditPasteText(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle
    )
{
    YORI_STRING Text;
    PYORIWIN_CTRL_EDIT Edit;
    PYORIWIN_CTRL Ctrl;

    Ctrl = (PYORIWIN_CTRL)CtrlHandle;
    Edit = CONTAINING_RECORD(Ctrl, YORIWIN_CTRL_EDIT, Ctrl);
    YoriLibInitEmptyString(&Text);

    if (YoriWinEditSelActive(CtrlHandle)) {
        YoriWinEditDeleteSelection(CtrlHandle);
    }

    if (!YoriLibPasteTextProcFallback(&Text)) {
        return FALSE;
    }

    if (!YoriWinEditInsertTextRange(Edit, FALSE, Edit->CursorOffset, &Text)) {
        YoriLibFreeStringContents(&Text);
        return FALSE;
    }

    Edit->CursorOffset = Edit->CursorOffset + Text.LengthInChars;
    ASSERT(Edit->CursorOffset <= Edit->Text.LengthInChars);

    YoriLibFreeStringContents(&Text);
    return TRUE;
}

//
//  =========================================
//  API FUNCTIONS
//  =========================================
//

/**
 Set the color attributes within the edit control to a value and repaint the
 control.  Note this refers to the attributes of the text within the edit, not
 the entire edit area.

 @param CtrlHandle Pointer to a edit control.

 @param TextAttributes The new attributes to use.

 @param SelectedTextAttributes Specifies the foreground and background color
        to use for selected text within the edit control.
 */
VOID
YoriWinEditSetColor(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in WORD TextAttributes,
    __in WORD SelectedTextAttributes
    )
{
    PYORIWIN_CTRL_EDIT Edit;
    PYORIWIN_CTRL Ctrl;
    Ctrl = (PYORIWIN_CTRL)CtrlHandle;
    Edit = CONTAINING_RECORD(Ctrl, YORIWIN_CTRL_EDIT, Ctrl);
    Edit->TextAttributes = TextAttributes;
    Edit->SelectedTextAttributes = SelectedTextAttributes;
    YoriWinEditPaint(Edit);
}

/**
 Make the cursor visible or hidden as necessary and display it with the
 size implied by the current insert mode.

 @param Edit Pointer to the edit control.

 @param Visible TRUE to display the cursor, FALSE to hide it.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinEditSetCursorVisible(
    __in PYORIWIN_CTRL_EDIT Edit,
    __in BOOLEAN Visible
    )
{
    UCHAR Percent;
    if (Edit->InsertMode) {
        Percent = 20;
    } else {
        Percent = 50;
    }

    Edit->HasFocus = Visible;
    YoriWinSetCtrlCursorState(&Edit->Ctrl, Visible, Percent);
    return TRUE;
}

/**
 Toggle the insert state of the control.  If new keystrokes would previously
 insert new characters, future characters overwrite existing characters, and
 vice versa.  The cursor shape will be updated to reflect the new state.

 @param Edit Pointer to the edit control.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinEditToggleInsert(
    __in PYORIWIN_CTRL_EDIT Edit
    )
{
    if (Edit->InsertMode) {
        Edit->InsertMode = FALSE;
    } else {
        Edit->InsertMode = TRUE;
    }
    YoriWinEditSetCursorVisible(Edit, TRUE);

    return TRUE;
}

/**
 Return the text that has been entered into an edit control.

 @param CtrlHandle Pointer to the edit control.

 @param Text On input, an initialized Yori string.  On output, populated with
        the string contents of the edit control.  Note this string can be
        reallocated within this routine.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinEditGetText(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __inout PYORI_STRING Text
    )
{
    PYORIWIN_CTRL_EDIT Edit;
    PYORIWIN_CTRL Ctrl;

    Ctrl = (PYORIWIN_CTRL)CtrlHandle;

    Edit = CONTAINING_RECORD(Ctrl, YORIWIN_CTRL_EDIT, Ctrl);
    if (Text->LengthAllocated < Edit->Text.LengthInChars + 1) {
        YORI_STRING NewString;
        if (!YoriLibAllocateString(&NewString, Edit->Text.LengthInChars + 1)) {
            return FALSE;
        }

        YoriLibFreeStringContents(Text);
        memcpy(Text, &NewString, sizeof(YORI_STRING));
    }

    if (Edit->Text.LengthInChars > 0) {
        YoriWinEditPopulateTextRange(Edit, 0, Edit->Text.LengthInChars, Text);
    }
    Text->StartOfString[Text->LengthInChars] = '\0';
    return TRUE;
}

/**
 Set the text that should be entered within an edit control.

 @param CtrlHandle Pointer to the edit control.

 @param Text Points to the text to populate the edit control with.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinEditSetText(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in PYORI_STRING Text
    )
{
    PYORIWIN_CTRL_EDIT Edit;
    PYORIWIN_CTRL Ctrl;

    Ctrl = (PYORIWIN_CTRL)CtrlHandle;

    Edit = CONTAINING_RECORD(Ctrl, YORIWIN_CTRL_EDIT, Ctrl);

    YoriWinEditClearUndo(Edit);

    if (Text->LengthInChars + 1 > Edit->Text.LengthAllocated) {
        YORI_STRING NewString;
        if (!YoriLibAllocateString(&NewString, Text->LengthInChars + 1)) {
            return FALSE;
        }

        YoriLibFreeStringContents(&Edit->Text);
        memcpy(&Edit->Text, &NewString, sizeof(YORI_STRING));
    }

    memcpy(Edit->Text.StartOfString, Text->StartOfString, Text->LengthInChars * sizeof(TCHAR));
    Edit->Text.LengthInChars = Text->LengthInChars;
    Edit->Text.StartOfString[Edit->Text.LengthInChars] = '\0';
    Edit->CursorOffset = Edit->Text.LengthInChars;
    YoriWinEditEnsureCursorVisible(Edit);
    YoriWinEditPaint(Edit);
    return TRUE;
}

//
//  =========================================
//  INPUT FUNCTIONS
//  =========================================
//


/**
 Add new text and honor the current insert or overwrite state.

 @param Edit Pointer to the edit control, indicating the current cursor
        location and insert state.

 @param Text The string to include.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinEditInsertTextAtCursor(
    __in PYORIWIN_CTRL_EDIT Edit,
    __in PYORI_STRING Text
    )
{
    BOOLEAN Success;

    if (YoriWinEditSelActive(Edit)) {
        YoriWinEditDeleteSelection(Edit);
    }

    if (Text->LengthInChars == 0) {
        return TRUE;
    }

    if (Edit->InsertMode) {
        Success = YoriWinEditInsertTextRange(Edit, FALSE, Edit->CursorOffset, Text);
    } else {
        Success = YoriWinEditOverwriteTextRange(Edit, FALSE, Edit->CursorOffset, Text);
    }

    if (!Success) {
        return FALSE;
    }

    Edit->CursorOffset = Edit->CursorOffset + Text->LengthInChars;
    ASSERT(Edit->CursorOffset <= Edit->Text.LengthInChars);

    return TRUE;
}

/**
 Add a single character and honor the current insert or overwrite state.

 @param Edit Pointer to the edit control, indicating the current cursor
        location and insert state.

 @param Char The character to include.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinEditAddChar(
    __in PYORIWIN_CTRL_EDIT Edit,
    __in TCHAR Char
    )
{
    YORI_STRING String;

    YoriLibInitEmptyString(&String);
    String.StartOfString = &Char;
    String.LengthInChars = 1;

    return YoriWinEditInsertTextAtCursor(Edit, &String);
}


/**
 Delete the character before the cursor and move later characters into
 position.

 @param Edit Pointer to the edit control, indicating the current cursor
        location.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinEditBackspace(
    __in PYORIWIN_CTRL_EDIT Edit
    )
{
    if (YoriWinEditSelActive(&Edit->Ctrl)) {
        return YoriWinEditDeleteSelection(&Edit->Ctrl);
    }

    if (Edit->CursorOffset == 0) {
        return FALSE;
    }

    if (YoriWinEditDeleteTextRange(Edit, FALSE, Edit->CursorOffset - 1, Edit->CursorOffset)) {
        Edit->CursorOffset--;
        return TRUE;
    }
    return FALSE;
}

/**
 Delete the character at the cursor and move later characters into position.

 @param Edit Pointer to the edit control, indicating the current cursor
        location.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinEditDelete(
    __in PYORIWIN_CTRL_EDIT Edit
    )
{
    if (YoriWinEditSelActive(&Edit->Ctrl)) {
        return YoriWinEditDeleteSelection(&Edit->Ctrl);
    }

    if (Edit->CursorOffset == Edit->Text.LengthInChars) {
        return FALSE;
    }

    if (YoriWinEditDeleteTextRange(Edit, FALSE, Edit->CursorOffset, Edit->CursorOffset + 1)) {
        return TRUE;
    }
    return FALSE;
}

/**
 Handle a double-click within an edit control.  This is supposed to select a
 "word" which is delimited by a user controllable set of characters.

 @param Edit Pointer to the edit control.

 @param ViewportX The horizontal position in the control relative to its
        client area.
 */
VOID
YoriWinEditNotifyDoubleClick(
    __in PYORIWIN_CTRL_EDIT Edit,
    __in WORD ViewportX
    )
{
    YORI_ALLOC_SIZE_T NewCursorChar;
    YORI_STRING BreakChars;
    YORI_ALLOC_SIZE_T BeginRangeOffset;
    YORI_ALLOC_SIZE_T EndRangeOffset;

    //
    //  Translate the viewport location into a buffer location.
    //

    NewCursorChar = Edit->DisplayOffset + (YORI_ALLOC_SIZE_T)ViewportX;

    //
    //  If it's beyond the end of the line, there's nothing to select.
    //

    if (NewCursorChar >= Edit->Text.LengthInChars) {
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
    if (YoriLibFindLeftMostCharacter(&BreakChars, Edit->Text.StartOfString[BeginRangeOffset]) == NULL) {
        while (BeginRangeOffset > 0 &&
               YoriLibFindLeftMostCharacter(&BreakChars, Edit->Text.StartOfString[BeginRangeOffset - 1]) == NULL) {
            BeginRangeOffset--;
        }
    }

    //
    //  Search right looking for a delimiter or the end of the string.
    //

    EndRangeOffset = NewCursorChar;
    while (EndRangeOffset < Edit->Text.LengthInChars &&
           YoriLibFindLeftMostCharacter(&BreakChars, Edit->Text.StartOfString[EndRangeOffset]) == NULL) {
        EndRangeOffset++;
    }

    YoriLibFreeStringContents(&BreakChars);

    //
    //  If any range was found (ie., the user didn't click on a word
    //  delimiter) select the range.
    //

    if (EndRangeOffset > BeginRangeOffset) {
        YoriWinEditSetSelectionRange(&Edit->Ctrl, BeginRangeOffset, EndRangeOffset);
    }
}

/**
 Adjust the viewport and selection to reflect the mouse being dragged,
 potentially outside the control's client area while the button is held down,
 thereby extending the selection.

 @param Edit Pointer to the edit control.

 @param MousePos Specifies the mouse position.
 */
VOID
YoriWinEditScrollForMouseSelect(
    __in PYORIWIN_CTRL_EDIT Edit,
    __in PYORIWIN_BOUNDED_COORD MousePos
    )
{
    COORD ClientSize;
    YORI_ALLOC_SIZE_T NewCursorOffset;
    YORI_ALLOC_SIZE_T NewViewportLeft;

    YoriWinGetCtrlClientSize(&Edit->Ctrl, &ClientSize);

    NewViewportLeft = Edit->DisplayOffset;

    //
    //  Now find the cursor column.  This can be left of the viewport, right
    //  of the viewport, or any column within the viewport.  When in the
    //  viewport, this needs to be translated from a display location to
    //  a buffer location.
    //

    if (MousePos->Left) {
        if (NewViewportLeft > 0) {
            NewCursorOffset = NewViewportLeft - 1;
        } else {
            NewCursorOffset = 0;
        }
    } else if (MousePos->Right) {
        NewCursorOffset = NewViewportLeft + ClientSize.X + 1;
    } else {
        NewCursorOffset = NewViewportLeft + MousePos->Pos.X;
    }

    if (NewCursorOffset > Edit->Text.LengthInChars) {
        NewCursorOffset = Edit->Text.LengthInChars;
    }

    Edit->CursorOffset = NewCursorOffset;

    if (Edit->Selection.Active == YoriWinEditSelMouseTopDown ||
        Edit->Selection.Active == YoriWinEditSelMouseBottomUp) {
        YoriWinEditExtendSelToCursor(Edit);
    } else {
        YoriWinEditStartSelAtCursor(Edit, TRUE);
    }
    YoriWinEditEnsureCursorVisible(Edit);
    YoriWinEditPaint(Edit);
}

/**
 Process a key that may be an enhanced key.  Some of these keys can be either
 enhanced or non-enhanced.

 @param Edit Pointer to the edit control, indicating the current cursor
        location.

 @param Event Pointer to the event describing the state of the key being
        pressed.

 @return TRUE to indicate the key has been processed, FALSE if it is an
         unknown key.
 */
BOOLEAN
YoriWinEditProcessEnhKey(
    __in PYORIWIN_CTRL_EDIT Edit,
    __in PYORIWIN_EVENT Event
    )
{
    BOOLEAN Recognized;
    Recognized = FALSE;

    if (Event->u.KeyDown.VirtualKeyCode == VK_LEFT) {
        if (Event->u.KeyDown.CtrlMask & SHIFT_PRESSED) {
            YoriWinEditStartSelAtCursor(Edit, FALSE);
        } else if (YoriWinEditSelActive(&Edit->Ctrl)) {
            Edit->Selection.Active = YoriWinEditSelNotActive;
        }
        if (Edit->CursorOffset > 0) {
            Edit->CursorOffset--;
            if (Event->u.KeyDown.CtrlMask & SHIFT_PRESSED) {
                YoriWinEditExtendSelToCursor(Edit);
            }
            YoriWinEditEnsureCursorVisible(Edit);
        }
        YoriWinEditPaint(Edit);
        Recognized = TRUE;
    } else if (Event->u.KeyDown.VirtualKeyCode == VK_RIGHT) {
        if (Event->u.KeyDown.CtrlMask & SHIFT_PRESSED) {
            YoriWinEditStartSelAtCursor(Edit, FALSE);
        } else if (YoriWinEditSelActive(&Edit->Ctrl)) {
            Edit->Selection.Active = YoriWinEditSelNotActive;
        }
        if (Edit->CursorOffset < Edit->Text.LengthInChars) {
            Edit->CursorOffset++;
            if (Event->u.KeyDown.CtrlMask & SHIFT_PRESSED) {
                YoriWinEditExtendSelToCursor(Edit);
            }
            YoriWinEditEnsureCursorVisible(Edit);
        }
        YoriWinEditPaint(Edit);
        Recognized = TRUE;
    } else if (Event->u.KeyDown.VirtualKeyCode == VK_HOME) {
        if (Event->u.KeyDown.CtrlMask & SHIFT_PRESSED) {
            YoriWinEditStartSelAtCursor(Edit, FALSE);
        } else if (YoriWinEditSelActive(&Edit->Ctrl)) {
            Edit->Selection.Active = YoriWinEditSelNotActive;
        }
        if (Edit->CursorOffset != 0) {
            Edit->CursorOffset = 0;
            if (Event->u.KeyDown.CtrlMask & SHIFT_PRESSED) {
                YoriWinEditExtendSelToCursor(Edit);
            }
            YoriWinEditEnsureCursorVisible(Edit);
        }
        YoriWinEditPaint(Edit);
        Recognized = TRUE;
    } else if (Event->u.KeyDown.VirtualKeyCode == VK_END) {
        if (Event->u.KeyDown.CtrlMask & SHIFT_PRESSED) {
            YoriWinEditStartSelAtCursor(Edit, FALSE);
        } else if (YoriWinEditSelActive(&Edit->Ctrl)) {
            Edit->Selection.Active = YoriWinEditSelNotActive;
        }
        if (Edit->CursorOffset != Edit->Text.LengthInChars) {
            Edit->CursorOffset = Edit->Text.LengthInChars;
            if (Event->u.KeyDown.CtrlMask & SHIFT_PRESSED) {
                YoriWinEditExtendSelToCursor(Edit);
            }
            YoriWinEditEnsureCursorVisible(Edit);
        }
        YoriWinEditPaint(Edit);
        Recognized = TRUE;
    } else if (Event->u.KeyDown.VirtualKeyCode == VK_INSERT) {
        if (!Edit->ReadOnly) {
            YoriWinEditToggleInsert(Edit);
        }
        Recognized = TRUE;
    } else if (Event->u.KeyDown.VirtualKeyCode == VK_BACK) {

        if (!Edit->ReadOnly && YoriWinEditBackspace(Edit)) {
            YoriWinEditEnsureCursorVisible(Edit);
            YoriWinEditPaint(Edit);
        }
        Recognized = TRUE;

    } else if (Event->u.KeyDown.VirtualKeyCode == VK_DELETE) {

        if (!Edit->ReadOnly && YoriWinEditDelete(Edit)) {
            YoriWinEditEnsureCursorVisible(Edit);
            YoriWinEditPaint(Edit);
        }
        Recognized = TRUE;
    }

    return Recognized;
}

/**
 Process input events for a edit control.

 @param Ctrl Pointer to the edit control.

 @param Event Pointer to the input event.

 @return TRUE to indicate that the event was processed and no further
         processing should occur.  FALSE to indicate that regular processing
         should continue (although this does not imply that no processing
         has already occurred.)
 */
BOOLEAN
YoriWinEditEventHandler(
    __in PYORIWIN_CTRL Ctrl,
    __in PYORIWIN_EVENT Event
    )
{
    PYORIWIN_CTRL_EDIT Edit;

    Edit = CONTAINING_RECORD(Ctrl, YORIWIN_CTRL_EDIT, Ctrl);
    switch(Event->EventType) {
        case YoriWinEventGetFocus:
            YoriWinEditSetCursorVisible(Edit, TRUE);
            YoriWinEditPaint(Edit);
            break;
        case YoriWinEventLoseFocus:
            YoriWinEditSetCursorVisible(Edit, FALSE);
            YoriWinEditPaint(Edit);
            break;
        case YoriWinEventParentDestroyed:
            YoriWinEditClearUndo(Edit);
            YoriWinEditSetCursorVisible(Edit, FALSE);
            YoriLibFreeStringContents(&Edit->Text);
            YoriWinDestroyControl(Ctrl);
            YoriLibDereference(Edit);
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

                ASSERT(Edit->CursorOffset <= Edit->Text.LengthInChars);

                if (!YoriWinEditProcessEnhKey(Edit, Event)) {
                    if (Event->u.KeyDown.Char != '\0' &&
                        Event->u.KeyDown.Char != '\r' &&
                        Event->u.KeyDown.Char != '\n' &&
                        Event->u.KeyDown.Char != '\x1b' &&
                        Event->u.KeyDown.Char != '\t') {

                        if (!Edit->ReadOnly) {
                            YoriWinEditAddChar(Edit, Event->u.KeyDown.Char);
                            YoriWinEditEnsureCursorVisible(Edit);
                            YoriWinEditPaint(Edit);
                        }
                    }
                }
            } else if (Event->u.KeyDown.CtrlMask == LEFT_CTRL_PRESSED ||
                       Event->u.KeyDown.CtrlMask == RIGHT_CTRL_PRESSED) {

                if (Event->u.KeyDown.VirtualKeyCode == 'A') {
                    if (Edit->Text.LengthInChars > 0) {
                        YoriWinEditSetSelectionRange(Ctrl, 0, Edit->Text.LengthInChars);
                    }
                    return TRUE;
                } else if (Event->u.KeyDown.VirtualKeyCode == 'C') {
                    if (YoriWinEditCopySelectedText(Ctrl)) {
                        Edit->Selection.Active = YoriWinEditSelNotActive;
                        YoriWinEditEnsureCursorVisible(Edit);
                        YoriWinEditPaint(Edit);
                    }
                    return TRUE;
                } else if (Event->u.KeyDown.VirtualKeyCode == 'R') {
                    if (YoriWinEditRedo(Edit)) {
                        YoriWinEditEnsureCursorVisible(Edit);
                        YoriWinEditPaint(Edit);
                    }
                    return TRUE;
                } else if (Event->u.KeyDown.VirtualKeyCode == 'V') {
                    if (YoriWinEditPasteText(Ctrl)) {
                        YoriWinEditEnsureCursorVisible(Edit);
                        YoriWinEditPaint(Edit);
                    }
                    return TRUE;
                } else if (Event->u.KeyDown.VirtualKeyCode == 'X') {
                    if (YoriWinEditCutSelectedText(Ctrl)) {
                        YoriWinEditEnsureCursorVisible(Edit);
                        YoriWinEditPaint(Edit);
                    }
                    return TRUE;
                } else if (Event->u.KeyDown.VirtualKeyCode == 'Z') {
                    if (YoriWinEditUndo(Edit)) {
                        YoriWinEditEnsureCursorVisible(Edit);
                        YoriWinEditPaint(Edit);
                    }
                    return TRUE;
                }
            } else if (Event->u.KeyDown.CtrlMask == LEFT_ALT_PRESSED ||
                       Event->u.KeyDown.CtrlMask == (LEFT_ALT_PRESSED | ENHANCED_KEY)) {
                YoriLibBuildNumericKey(&Edit->NumericKeyValue, &Edit->NumericKeyType, Event->u.KeyDown.VirtualKeyCode, Event->u.KeyDown.VirtualScanCode);

            } else if (Event->u.KeyDown.CtrlMask == ENHANCED_KEY ||
                       Event->u.KeyDown.CtrlMask == (ENHANCED_KEY | SHIFT_PRESSED)) {
                YoriWinEditProcessEnhKey(Edit, Event);
            }
            break;

        case YoriWinEventKeyUp:
            if ((Event->u.KeyUp.CtrlMask & (RIGHT_ALT_PRESSED | LEFT_ALT_PRESSED)) == 0 &&
                !Edit->ReadOnly &&
                (Edit->NumericKeyValue != 0 ||
                 (Event->u.KeyUp.VirtualKeyCode == VK_MENU && Event->u.KeyUp.Char != 0))) {

                DWORD NumericKeyValue;
                TCHAR Char;

                NumericKeyValue = Edit->NumericKeyValue;
                if (NumericKeyValue == 0) {
                    Edit->NumericKeyType = YoriLibNumericKeyUnicode;
                    NumericKeyValue = Event->u.KeyUp.Char;
                }

                YoriLibTransNumKeyToChar(NumericKeyValue, Edit->NumericKeyType, &Char);
                Edit->NumericKeyValue = 0;
                Edit->NumericKeyType = YoriLibNumericKeyAscii;

                YoriWinEditAddChar(Edit, Event->u.KeyDown.Char);
                YoriWinEditEnsureCursorVisible(Edit);
                YoriWinEditPaint(Edit);
            }

            break;

        case YoriWinEventMouseDblClickClient:
            YoriWinEditNotifyDoubleClick(Edit, Event->u.MouseDown.Location.X);
            break;
        case YoriWinEventMouseDownClient:
            {
                YORI_ALLOC_SIZE_T ClickOffset;
                ClickOffset = Edit->DisplayOffset + Event->u.MouseDown.Location.X;
                if (ClickOffset > Edit->Text.LengthInChars) {
                    ClickOffset = Edit->Text.LengthInChars;
                }

                Edit->CursorOffset = ClickOffset;
                Edit->Selection.Active = YoriWinEditSelNotActive;
                YoriWinEditStartSelAtCursor(Edit, TRUE);
                Edit->MouseButtonDown = TRUE;

                YoriWinEditEnsureCursorVisible(Edit);
                YoriWinEditPaint(Edit);
            }
            break;

        case YoriWinEventMouseMoveClient:
            if (Edit->MouseButtonDown) {
                YORIWIN_BOUNDED_COORD ClientPos;
                ClientPos.Left = FALSE;
                ClientPos.Right = FALSE;
                ClientPos.Above = FALSE;
                ClientPos.Below = FALSE;
                ClientPos.Pos.X = Event->u.MouseMove.Location.X;
                ClientPos.Pos.Y = Event->u.MouseMove.Location.Y;

                YoriWinEditScrollForMouseSelect(Edit, &ClientPos);
            }
            break;

        case YoriWinEventMouseMoveNonCli:
            if (Edit->MouseButtonDown) {
                YORIWIN_BOUNDED_COORD Pos;
                YORIWIN_BOUNDED_COORD ClientPos;
                Pos.Left = FALSE;
                Pos.Right = FALSE;
                Pos.Above = FALSE;
                Pos.Below = FALSE;
                Pos.Pos.X = Event->u.MouseMove.Location.X;
                Pos.Pos.Y = Event->u.MouseMove.Location.Y;

                YoriWinBoundCoordInSubRegion(&Pos, &Ctrl->ClientRect, &ClientPos);

                YoriWinEditScrollForMouseSelect(Edit, &ClientPos);
            }
            break;
        case YoriWinEventMouseMoveOutsideWin:
            if (Edit->MouseButtonDown) {

                //
                //  Translate any coordinates that are present into client
                //  relative form.  Anything that's out of bounds will stay
                //  that way.
                //

                YORIWIN_BOUNDED_COORD ClientPos;
                YoriWinBoundCoordInSubRegion(&Event->u.MouseMoveOutsideWindow.Location, &Ctrl->ClientRect, &ClientPos);
                YoriWinEditScrollForMouseSelect(Edit, &ClientPos);
            }
            break;

        case YoriWinEventMouseUpClient:
        case YoriWinEventMouseUpNonCli:
        case YoriWinEventMouseUpOutsideWin:

            if (Edit->Selection.Active == YoriWinEditSelMouseTopDown ||
                Edit->Selection.Active == YoriWinEditSelMouseBottomUp) {

                Edit->Selection.Active = YoriWinEditSelMouseComplete;
                Edit->MouseButtonDown = FALSE;
            }
            break;
    }

    return FALSE;
}


/**
 Set the size and location of an edit control, and redraw the contents.

 @param CtrlHandle Pointer to the edit to resize or reposition.

 @param CtrlRect Specifies the new size and position of the edit.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinEditReposition(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in PSMALL_RECT CtrlRect
    )
{
    PYORIWIN_CTRL Ctrl = (PYORIWIN_CTRL)CtrlHandle;
    PYORIWIN_CTRL_EDIT Edit;
    WORD Height;

    Height = (WORD)(CtrlRect->Bottom - CtrlRect->Top + 1);
    if (Height != 3 && Height != 1) {
        return FALSE;
    }

    Ctrl = (PYORIWIN_CTRL)CtrlHandle;
    Edit = CONTAINING_RECORD(Ctrl, YORIWIN_CTRL_EDIT, Ctrl);

    if (!YoriWinControlReposition(Ctrl, CtrlRect)) {
        return FALSE;
    }

    YoriWinEditPaintNonClient(Edit);
    YoriWinEditPaint(Edit);
    return TRUE;
}


/**
 Create a edit control and add it to a window.  This is destroyed when the
 window is destroyed.

 @param ParentHandle Pointer to the parent control.

 @param Size Specifies the location and size of the edit control.

 @param InitialText Specifies the initial text in the edit control.

 @param Style Specifies style flags for the edit control.

 @return Pointer to the newly created control or NULL on failure.
 */
PYORIWIN_CTRL_HANDLE
YoriWinEditCreate(
    __in PYORIWIN_CTRL_HANDLE ParentHandle,
    __in PSMALL_RECT Size,
    __in PYORI_STRING InitialText,
    __in WORD Style
    )
{
    PYORIWIN_CTRL_EDIT Edit;
    PYORIWIN_CTRL Parent;
    WORD Height;
    PYORIWIN_WINDOW TopLevelWindow;
    PYORIWIN_WINMGR_HANDLE WinMgrHandle;

    Height = (WORD)(Size->Bottom - Size->Top + 1);
    if (Height != 3 && Height != 1) {
        return NULL;
    }

    Parent = (PYORIWIN_CTRL)ParentHandle;

    Edit = YoriLibReferencedMalloc(sizeof(YORIWIN_CTRL_EDIT));
    if (Edit == NULL) {
        return NULL;
    }

    ZeroMemory(Edit, sizeof(YORIWIN_CTRL_EDIT));

    YoriLibInitializeListHead(&Edit->Undo);
    YoriLibInitializeListHead(&Edit->Redo);

    if (Style & YORIWIN_EDIT_STY_RIGHT_ALIGN) {
        Edit->TextAlign = YoriWinTextAlignRight;
    } else if (Style & YORIWIN_EDIT_STY_CENTER) {
        Edit->TextAlign = YoriWinTextAlignCenter;
    }

    if (Style & YORIWIN_EDIT_STY_READ_ONLY) {
        Edit->ReadOnly = TRUE;
    }

    if (Style & YORIWIN_EDIT_STY_NUMERIC) {
        Edit->NumericOnly = TRUE;
    }

    if (!YoriLibCopyString(&Edit->Text, InitialText)) {
        YoriLibDereference(Edit);
        return NULL;
    }

    Edit->CursorOffset = Edit->Text.LengthInChars;
    Edit->Ctrl.NotifyEventFn = YoriWinEditEventHandler;
    if (!YoriWinCreateControl(Parent, Size, TRUE, TRUE, &Edit->Ctrl)) {
        YoriLibFreeStringContents(&Edit->Text);
        YoriLibDereference(Edit);
        return NULL;
    }

    Edit->TextAttributes = Edit->Ctrl.DefaultAttributes;
    Edit->InsertMode = TRUE;

    TopLevelWindow = YoriWinGetTopLevelWindow(Parent);
    WinMgrHandle = YoriWinGetWinMgrHandle(TopLevelWindow);
    Edit->SelectedTextAttributes = YoriWinMgrDefaultColorLookup(WinMgrHandle, YoriWinColorEditSelectedText);

    if (Parent->Parent != NULL) {
        Edit->Ctrl.RelativeToParentClient = FALSE;
    }

    if (Height == 1) {
        Edit->Ctrl.ClientRect.Left++;
        Edit->Ctrl.ClientRect.Right--;
    } else {
        Edit->Ctrl.ClientRect.Top++;
        Edit->Ctrl.ClientRect.Left++;
        Edit->Ctrl.ClientRect.Bottom--;
        Edit->Ctrl.ClientRect.Right--;
    }

    YoriWinEditPaintNonClient(Edit);
    YoriWinEditEnsureCursorVisible(Edit);
    YoriWinEditPaint(Edit);

    return &Edit->Ctrl;
}


// vim:sw=4:ts=4:et:
