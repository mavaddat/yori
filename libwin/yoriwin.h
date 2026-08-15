/**
 * @file libwin/yoriwin.h
 *
 * Header for control and window toolkit routines that may be of value from
 * the shell as well as external tools.
 *
 * Copyright (c) 2019-2022 Malcolm J. Smith
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

/**
 Opaque pointer to a window manager.
 */
typedef PVOID PYORIWIN_WINMGR_HANDLE;

/**
 Opaque pointer to a window.
 */
typedef PVOID PYORIWIN_WINDOW_HANDLE;

/**
 Opaque pointer to a control.
 */
typedef PVOID PYORIWIN_CTRL_HANDLE;

/**
 A function prototype that can be invoked to deliver notification events
 for a specific control.
 */
typedef VOID YORIWIN_NOTIFY(PYORIWIN_CTRL_HANDLE);

/**
 A pointer to a function that can be invoked to deliver notification events
 for a specific control.
 */
typedef YORIWIN_NOTIFY *PYORIWIN_NOTIFY;

/**
 A list of possible color tables to use.
 */
typedef enum _YORIWIN_COLOR_TABLE_ID {
    YoriWinColorTableDefault = 0,
    YoriWinColorTableVga = 1,
    YoriWinColorTableNano = 2,
    YoriWinColorTableMono = 3
} YORIWIN_COLOR_TABLE_ID;

// BUTTON.C

BOOLEAN
YoriWinButtonReposition(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in PSMALL_RECT CtrlRect
    );

/**
 The button is the default button on the window.
 */
#define YORIWIN_BUTTON_STY_DEFAULT (0x0001)

/**
 The button is the cancel button on the window.
 */
#define YORIWIN_BUTTON_STY_CANCEL  (0x0002)

/**
 The button can never receive keyboard focus, but is still functional.
 */
#define YORIWIN_BUTTON_STY_NOFOCUS  (0x0004)

PYORIWIN_CTRL_HANDLE
YoriWinButtonCreate(
    __in PYORIWIN_WINDOW_HANDLE Parent,
    __in PSMALL_RECT Size,
    __in PCYORI_STRING Caption,
    __in WORD Style,
    __in_opt PYORIWIN_NOTIFY ClickCbk
    );

// CHECKBOX.C

BOOLEAN
YoriWinCheckboxIsChecked(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle
    );

BOOLEAN
YoriWinCheckboxReposition(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in PSMALL_RECT CtrlRect
    );

PYORIWIN_CTRL_HANDLE
YoriWinCheckboxCreate(
    __in PYORIWIN_WINDOW_HANDLE ParentHandle,
    __in PSMALL_RECT Size,
    __in PYORI_STRING Caption,
    __in WORD Style,
    __in_opt PYORIWIN_NOTIFY ToggleCbk
    );

// COMBO.C

__success(return)
BOOLEAN
YoriWinComboGetActiveOption(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __out PYORI_ALLOC_SIZE_T CurrentlyActiveIndex
    );

__success(return)
BOOLEAN
YoriWinComboSetActiveOption(
    __inout PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in YORI_ALLOC_SIZE_T ActiveOption
    );

__success(return)
BOOLEAN
YoriWinComboAddItems(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in PCYORI_STRING ListOptions,
    __in YORI_ALLOC_SIZE_T NumberOptions
    );

BOOLEAN
YoriWinComboReposition(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in PSMALL_RECT CtrlRect
    );

PYORIWIN_CTRL_HANDLE
YoriWinComboCreate(
    __in PYORIWIN_WINDOW_HANDLE ParentHandle,
    __in PSMALL_RECT Size,
    __in WORD LinesInList,
    __in PYORI_STRING Caption,
    __in WORD Style,
    __in_opt PYORIWIN_NOTIFY ClickCbk
    );

// CTRL.C

PYORIWIN_CTRL_HANDLE
YoriWinGetCtrlParent(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle
    );

DWORD_PTR
YoriWinGetCtrlId(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle
    );

VOID
YoriWinSetCtrlId(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in DWORD_PTR CtrlId
    );

PYORIWIN_CTRL_HANDLE
YoriWinFindControlById(
    __in PYORIWIN_CTRL_HANDLE ParentCtrl,
    __in DWORD_PTR CtrlId
    );

VOID
YoriWinGetCtrlClientSize(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __out PCOORD Size
    );

PVOID
YoriWinGetCtrlContext(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle
    );

VOID
YoriWinSetCtrlContext(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in PVOID Context
    );

BOOLEAN
YoriWinCtrlSetFocusOnMouseClick(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in BOOLEAN ReceiveFocusOnMouseClick
    );

// EDIT.C

/**
 The edit should left align text.
 */
#define YORIWIN_EDIT_STY_LEFT_ALIGN      (0x0000)

/**
 The edit should right align text.
 */
#define YORIWIN_EDIT_STY_RIGHT_ALIGN     (0x0001)

/**
 The edit should center text.
 */
#define YORIWIN_EDIT_STY_CENTER          (0x0002)

/**
 The edit should not allow, uhh, edits.  This allows it to operate like a
 label, but it can still do navigation, and get focus, etc.
 */
#define YORIWIN_EDIT_STY_READ_ONLY       (0x0004)

/**
 The edit should only accept numeric input.
 */
#define YORIWIN_EDIT_STY_NUMERIC         (0x0008)

BOOLEAN
YoriWinEditSelActive(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle
    );

BOOLEAN
YoriWinEditDeleteSelection(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle
    );

BOOLEAN
YoriWinEditGetSelectedText(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __out PYORI_STRING SelectedText
    );

VOID
YoriWinEditSetSelectionRange(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in YORI_ALLOC_SIZE_T StartOffset,
    __in YORI_ALLOC_SIZE_T EndOffset
    );

BOOLEAN
YoriWinEditGetText(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __inout PYORI_STRING Text
    );

BOOLEAN
YoriWinEditSetText(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in PYORI_STRING Text
    );

BOOLEAN
YoriWinEditReposition(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in PSMALL_RECT CtrlRect
    );

PYORIWIN_CTRL_HANDLE
YoriWinEditCreate(
    __in PYORIWIN_CTRL_HANDLE ParentHandle,
    __in PSMALL_RECT Size,
    __in PYORI_STRING InitialText,
    __in WORD Style
    );

// HEXEDIT.C

/**
 A function prototype that can be invoked to deliver notification events
 when the cursor is moved.
 */
typedef VOID FAR YORIWIN_NOTIFY_HEXEDIT_CURSOR(PYORIWIN_CTRL_HANDLE, YORI_MAX_UNSIGNED_T, DWORD);

/**
 A pointer to a function that can be invoked to deliver notification events
 when the cursor is moved.
 */
typedef YORIWIN_NOTIFY_HEXEDIT_CURSOR *PYORIWIN_NOTIFY_HEXEDIT_CURSOR;

/**
 The hex edit should display a vertical scroll bar.
 */
#define YORIWIN_HEXEDIT_STY_VSCROLL         (0x0001)

/**
 The hex edit should be read only.
 */
#define YORIWIN_HEXEDIT_STY_READONLY        (0x0002)

/**
 The hex edit should contain 32 bit offset values.
 */
#define YORIWIN_HEXEDIT_STY_OFFSET          (0x0004)

/**
 The hex edit should contain 64 bit offset values.
 */
#define YORIWIN_HEXEDIT_STY_LOFFSET         (0x0008)

/**
 The hex edit should contain a vertical seperator.
 */
#define YORIWIN_HEXEDIT_STY_VSEPERATOR      (0x0010)

BOOLEAN
YoriWinHexEditClear(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle
    );

DWORD
YoriWinHexEditGetBytesPerWord(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle
    );

BOOLEAN
YoriWinHexEditGetDataNoCopy(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __out PUCHAR *Buffer,
    __out PYORI_ALLOC_SIZE_T BufferLength
    );

__success(return)
BOOLEAN
YoriWinHexEditGetSelectedData(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __out PVOID * Data,
    __out PYORI_ALLOC_SIZE_T DataLength
    );

BOOLEAN
YoriWinHexEditDeleteSelection(
    __in PYORIWIN_CTRL_HANDLE HexEdit
    );

BOOLEAN
YoriWinHexEditGetModifyState(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle
    );

BOOLEAN
YoriWinHexEditReposition(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in PSMALL_RECT CtrlRect
    );

BOOLEAN
YoriWinHexEditSetBytesPerWord(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in UCHAR BytesPerWord
    );

BOOLEAN
YoriWinHexEditSetStyle(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in WORD NewStyle
    );

BOOLEAN
YoriWinHexEditSetCaption(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in PYORI_STRING Caption
    );

VOID
YoriWinHexEditSetColor(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in WORD Attributes,
    __in WORD SelectedAttributes
    );

BOOLEAN
YoriWinHexEditSetCursorCbk(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in PYORIWIN_NOTIFY_HEXEDIT_CURSOR NotifyCbk
    );

BOOLEAN
YoriWinHexEditSetDataNoCopy(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in PUCHAR NewBuffer,
    __in YORI_ALLOC_SIZE_T NewBufferAllocated,
    __in YORI_ALLOC_SIZE_T NewBufferValid
    );

BOOLEAN
YoriWinHexEditSetModifyState(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in BOOLEAN ModifyState
    );

BOOLEAN
YoriWinHexEditSetReadOnly(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in BOOLEAN NewReadOnlyState
    );

__success(return)
BOOLEAN
YoriWinHexEditSetCursorPoint(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in BOOLEAN AsChar,
    __in YORI_ALLOC_SIZE_T BufferOffset,
    __in UCHAR BitShift
    );

__success(return)
BOOLEAN
YoriWinHexEditGetCursorPoint(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __out PBOOLEAN AsChar,
    __out PYORI_ALLOC_SIZE_T BufferOffset,
    __out PUCHAR BitShift
    );

__success(return)
BOOLEAN
YoriWinHexEditGetVisCursorPoint(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __out PYORI_ALLOC_SIZE_T CursorOffset,
    __out PYORI_ALLOC_SIZE_T CursorLine
    );

VOID
YoriWinHexEditGetViewportPoint(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __out PYORI_ALLOC_SIZE_T ViewportLeft,
    __out PYORI_ALLOC_SIZE_T ViewportTop
    );

VOID
YoriWinHexEditSetViewportPoint(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in YORI_ALLOC_SIZE_T NewViewportLeft,
    __in YORI_ALLOC_SIZE_T NewViewportTop
    );

BOOLEAN
YoriWinHexEditSetVisBuffOffset(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in YORI_MAX_UNSIGNED_T VisualBufferOffset
    );

VOID
YoriWinHexEditClearSel(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle
    );

BOOLEAN
YoriWinHexEditSelActive(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle
    );

__success(return)
BOOLEAN
YoriWinHexEditSetSelectionRange(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in YORI_ALLOC_SIZE_T FirstByteOffset,
    __in YORI_ALLOC_SIZE_T LastByteOffset
    );

__success(return)
BOOLEAN
YoriWinHexEditDeleteData(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in YORI_ALLOC_SIZE_T DataOffset,
    __in YORI_ALLOC_SIZE_T Length
    );

__success(return)
BOOLEAN
YoriWinHexEditInsertData(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in YORI_ALLOC_SIZE_T DataOffset,
    __in PVOID Data,
    __in YORI_ALLOC_SIZE_T Length
    );

__success(return)
BOOLEAN
YoriWinHexEditReplaceData(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in YORI_ALLOC_SIZE_T DataOffset,
    __in PVOID Data,
    __in YORI_ALLOC_SIZE_T Length
    );

BOOLEAN
YoriWinHexEditCutSelectedData(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle
    );

BOOLEAN
YoriWinHexEditCopySelectedData(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle
    );

BOOLEAN
YoriWinHexEditPasteData(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle
    );

PYORIWIN_CTRL_HANDLE
YoriWinHexEditCreate(
    __in PYORIWIN_WINDOW_HANDLE ParentHandle,
    __in_opt PYORI_STRING Caption,
    __in PSMALL_RECT Size,
    __in UCHAR BytesPerWord,
    __in WORD Style
    );

// LABEL.C

/**
 The label should left align text.
 */
#define YORIWIN_LABEL_STY_LEFT_ALIGN      (0x0000)

/**
 The label should right align text.
 */
#define YORIWIN_LABEL_STY_RIGHT_ALIGN     (0x0001)

/**
 The label should center text.
 */
#define YORIWIN_LABEL_STY_CENTER          (0x0002)

/**
 The label should top align text.
 */
#define YORIWIN_LABEL_STY_TOP_ALIGN       (0x0000)

/**
 The label should bottom align text.
 */
#define YORIWIN_LABEL_STY_BOTTOM_ALIGN    (0x0004)

/**
 The label should vertically center text.
 */
#define YORIWIN_LABEL_STY_VCENTER         (0x0008)

/**
 The label should not parse accelerators.
 */
#define YORIWIN_LABEL_NO_ACCELERATOR      (0x0010)

PYORIWIN_CTRL_HANDLE
YoriWinLabelCreate(
    __in PYORIWIN_CTRL_HANDLE Parent,
    __in PSMALL_RECT Size,
    __in PCYORI_STRING Caption,
    __in WORD Style
    );

VOID
YoriWinLabelSetTextAttributes(
    __in PYORIWIN_CTRL_HANDLE Ctrl,
    __in WORD TextAttributes
    );

VOID
YoriWinLabelParseAccelerator(
    __in PCYORI_STRING RawString,
    __inout_opt PYORI_STRING ParsedString,
    __out_opt TCHAR* AcceleratorChar,
    __out_opt PYORI_ALLOC_SIZE_T HighlightOffset,
    __out_opt PYORI_ALLOC_SIZE_T DisplayLength
    );

YORI_ALLOC_SIZE_T
YoriWinLabelLinesNeededForText(
    __in PYORIWIN_WINMGR_HANDLE WinMgrHandle,
    __in PCYORI_STRING Text,
    __in YORI_ALLOC_SIZE_T CtrlWidth,
    __out_opt PYORI_ALLOC_SIZE_T MaximumWidth
    );

BOOLEAN
YoriWinLabelSetCaption(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in PCYORI_STRING Caption
    );

BOOLEAN
YoriWinLabelReposition(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in PSMALL_RECT CtrlRect
    );


// LIST.C

/**
 The list should display a vertical scroll bar.
 */
#define YORIWIN_LIST_STY_VSCROLL          (0x0001)

/**
 The list should support selection per row, not one per list
 */
#define YORIWIN_LIST_STY_MULTISELECT      (0x0002)

/**
 The list should clear selection when losing focus
 */
#define YORIWIN_LIST_STY_DESEL_FOCUS      (0x0004)

/**
 The list should display multiple items on one line
 */
#define YORIWIN_LIST_STY_HORIZONTAL       (0x0008)

/**
 The list should not have a border around the control
 */
#define YORIWIN_LIST_STY_NO_BORDER        (0x0010)

/**
 The list should display a horizontal scroll bar.
 */
#define YORIWIN_LIST_STY_HSCROLL          (0x0020)

/**
 The list should display a horizontal scroll bar but only when horizontal
 scrolling is possible.
 */
#define YORIWIN_LIST_STY_AUTO_HSCROLL     (0x0040)

PYORIWIN_CTRL_HANDLE
YoriWinListCreate(
    __in PYORIWIN_WINDOW_HANDLE Parent,
    __in PSMALL_RECT Size,
    __in WORD Style
    );

DWORD
YoriWinListGetItemCount(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle
    );

__success(return)
BOOLEAN
YoriWinListGetActiveOption(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __out PYORI_ALLOC_SIZE_T CurrentlyActiveIndex
    );

__success(return)
BOOLEAN
YoriWinListSetActiveOption(
    __inout PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in YORI_ALLOC_SIZE_T ActiveOption
    );

BOOLEAN
YoriWinListIsOptionSelected(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in YORI_ALLOC_SIZE_T Index
    );

BOOLEAN
YoriWinListClearAllItems(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle
    );

__success(return)
BOOLEAN
YoriWinListAddItems(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in PCYORI_STRING ListOptions,
    __in YORI_ALLOC_SIZE_T NumberOptions
    );

BOOLEAN
YoriWinListGetItemText(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in YORI_ALLOC_SIZE_T Index,
    __inout PYORI_STRING Text
    );

BOOLEAN
YoriWinListReposition(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in PSMALL_RECT CtrlRect
    );

BOOLEAN
YoriWinListSetHorizItemWidth(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in WORD ItemWidth
    );

BOOLEAN
YoriWinListSetSelNotifyCbk(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in PYORIWIN_NOTIFY NotifyCbk
    );


// MENUBAR.C


/**
 Specifies an API representation of a menu.
 */
typedef struct _YORIWIN_MENU {

    /**
     An array of menu items contained within the menu.
     */
    struct _YORIWIN_MENU_ENTRY *Items;

    /**
     The number of menu items contained within the menu.
     */
    YORI_ALLOC_SIZE_T ItemCount;
} YORIWIN_MENU, FAR *PYORIWIN_MENU;

/**
 Indicates that the menu entry should be a horizontal seperator bar.
 */
#define YORIWIN_MENU_ENTRY_SEPERATOR (0x00000001)

/**
 Indicates that the menu entry should be disabled.
 */
#define YORIWIN_MENU_ENTRY_DISABLED (0x00000002)

/**
 Indicates that the menu entry should be checked.
 */
#define YORIWIN_MENU_ENTRY_CHECKED (0x00000004)

/**
 Specifies an API representation for a menu item within a menu bar control.
 */
typedef struct _YORIWIN_MENU_ENTRY {

    /**
     Specifies the string for this menu item.  Note this string may contain an
     ampersand to indicate which item is the accelerator character.
     */
    YORI_STRING Caption;

    /**
     A human readable form of the hotkey.
     */
    YORI_STRING Hotkey;

    /**
     Specifies a callback function to invoke when this item is activated.
     */
    PYORIWIN_NOTIFY NotifyCbk;

    /**
     Specifies any child menu associated with the menu item.  This structure
     indicates the number of child items and has an array of those items.
     */
    YORIWIN_MENU ChildMenu;

    /**
     Specifies flags associated with the menu item.
     */
    DWORD Flags;
} YORIWIN_MENU_ENTRY, FAR *PYORIWIN_MENU_ENTRY;

PYORIWIN_CTRL_HANDLE
YoriWinMenuBarCreate(
    __in PYORIWIN_CTRL_HANDLE ParentHandle,
    __in WORD Style
    );

BOOLEAN
YoriWinMenuBarAppendItems(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in PYORIWIN_MENU Items
    );

VOID
YoriWinMenuBarDisableMenuItem(
    __in PYORIWIN_CTRL_HANDLE ItemHandle
    );

VOID
YoriWinMenuBarEnableMenuItem(
    __in PYORIWIN_CTRL_HANDLE ItemHandle
    );

VOID
YoriWinMenuBarCheckMenuItem(
    __in PYORIWIN_CTRL_HANDLE ItemHandle
    );

VOID
YoriWinMenuBarUncheckMenuItem(
    __in PYORIWIN_CTRL_HANDLE ItemHandle
    );

PYORIWIN_CTRL_HANDLE
YoriWinMenuBarGetSubmenuHandle(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in_opt PYORIWIN_CTRL_HANDLE ParentItemHandle,
    __in DWORD SubIndex
    );

BOOLEAN
YoriWinMenuBarReposition(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in PSMALL_RECT CtrlRect
    );

// MLEDIT.C

/**
 A function prototype that can be invoked to deliver notification events
 when the cursor is moved.
 */
typedef VOID FAR YORIWIN_NOTIFY_MLEDIT_CURSOR(PYORIWIN_CTRL_HANDLE, DWORD, DWORD);

/**
 A pointer to a function that can be invoked to deliver notification events
 when the cursor is moved.
 */
typedef YORIWIN_NOTIFY_MLEDIT_CURSOR *PYORIWIN_NOTIFY_MLEDIT_CURSOR;

/**
 The multiline edit should display a vertical scroll bar.
 */
#define YORIWIN_MLEDIT_STY_VSCROLLBAR  (0x0001)

/**
 The multiline edit should be read only.
 */
#define YORIWIN_MLEDIT_STY_READ_ONLY   (0x0002)

BOOLEAN
YoriWinMlEditSelectionActive(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle
    );

BOOLEAN
YoriWinMlEditClear(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle
    );

BOOLEAN
YoriWinMlEditDeleteSelection(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle
    );

__success(return)
BOOLEAN
YoriWinMlEditGetSelectedText(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in PYORI_STRING NewlineString,
    __out PYORI_STRING SelectedText
    );

BOOLEAN
YoriWinMlEditInsertTextAtCursor(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in PYORI_STRING Text
    );

BOOLEAN
YoriWinMlEditAddLinesNoDataCopy(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in PYORI_STRING NewLines,
    __in YORI_ALLOC_SIZE_T NewLineCount
    );

BOOLEAN
YoriWinMlEditCopySelectedText(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle
    );

BOOLEAN
YoriWinMlEditCutSelectedText(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle
    );

BOOLEAN
YoriWinMlEditPasteText(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle
    );

VOID
YoriWinMlEditSetSelectionRange(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in YORI_ALLOC_SIZE_T StartLine,
    __in YORI_ALLOC_SIZE_T StartOffset,
    __in YORI_ALLOC_SIZE_T EndLine,
    __in YORI_ALLOC_SIZE_T EndOffset
    );

__success(return)
BOOLEAN
YoriWinMlEditGetSelectionRange(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __out PYORI_ALLOC_SIZE_T StartLine,
    __out PYORI_ALLOC_SIZE_T StartOffset,
    __out PYORI_ALLOC_SIZE_T EndLine,
    __out PYORI_ALLOC_SIZE_T EndOffset
    );

VOID
YoriWinMlEditSetColor(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in WORD Attributes,
    __in WORD SelectedAttributes
    );

BOOLEAN
YoriWinMlEditGetAutoIndent(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __out_opt PBOOLEAN AutoIndentEnabled,
    __out_opt PBOOLEAN AutoIndentActive,
    __out_opt PYORI_ALLOC_SIZE_T AutoIndentActiveLine,
    __out_opt PYORI_ALLOC_SIZE_T AutoIndentActiveLength
    );

YORI_ALLOC_SIZE_T
YoriWinMlEditGetLineCount(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle
    );

PYORI_STRING
YoriWinMlEditGetLineByIndex(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in YORI_ALLOC_SIZE_T Index
    );

VOID
YoriWinMlEditGetCursorPoint(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __out PYORI_ALLOC_SIZE_T CursorOffset,
    __out PYORI_ALLOC_SIZE_T CursorLine
    );

VOID
YoriWinMlEditSetCursorPoint(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in YORI_ALLOC_SIZE_T NewCursorOffset,
    __in YORI_ALLOC_SIZE_T NewCursorLine
    );

VOID
YoriWinMlEditGetViewportPoint(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __out PYORI_ALLOC_SIZE_T ViewportLeft,
    __out PYORI_ALLOC_SIZE_T ViewportTop
    );

VOID
YoriWinMlEditSetViewportPoint(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in YORI_ALLOC_SIZE_T NewViewportLeft,
    __in YORI_ALLOC_SIZE_T NewViewportTop
    );

BOOLEAN
YoriWinMlEditSetCaption(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in PYORI_STRING Caption
    );

BOOLEAN
YoriWinMlEditSetModifyState(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in BOOLEAN ModifyState
    );

__success(return)
BOOLEAN
YoriWinMlEditGetTabWidth(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __out PYORI_ALLOC_SIZE_T TabWidth
    );

__success(return)
BOOLEAN
YoriWinMlEditSetTabWidth(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in YORI_ALLOC_SIZE_T TabWidth
    );

VOID
YoriWinMlEditSetAutoIndent(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in BOOLEAN AutoIndentEnabled
    );

VOID
YoriWinMlEditSetExpandTab(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in BOOLEAN ExpandTabEnabled
    );

VOID
YoriWinMlEditSetTradNavigation(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in BOOLEAN TradNavigationEnabled
    );

BOOLEAN
YoriWinMlEditGetModifyState(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle
    );

BOOLEAN
YoriWinMlEditSetCursorNotifyCbk(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in PYORIWIN_NOTIFY_MLEDIT_CURSOR NotifyCbk
    );

BOOLEAN
YoriWinMlEditIsUndoAvailable(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle
    );

BOOLEAN
YoriWinMlEditIsRedoAvailable(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle
    );

BOOLEAN
YoriWinMlEditUndo(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle
    );

BOOLEAN
YoriWinMlEditRedo(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle
    );

BOOLEAN
YoriWinMlEditReposition(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in PSMALL_RECT CtrlRect
    );

BOOLEAN
YoriWinMlEditSetReadOnly(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in BOOLEAN NewReadOnlyState
    );

PYORIWIN_CTRL_HANDLE
YoriWinMlEditCreate(
    __in PYORIWIN_WINDOW_HANDLE ParentHandle,
    __in_opt PYORI_STRING Caption,
    __in PSMALL_RECT Size,
    __in WORD Style
    );

// RADIO.C

BOOLEAN
YoriWinRadioIsSelected(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle
    );

VOID
YoriWinRadioSelect(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle
    );

BOOLEAN
YoriWinRadioReposition(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in PSMALL_RECT CtrlRect
    );

PYORIWIN_CTRL_HANDLE
YoriWinRadioCreate(
    __in PYORIWIN_WINDOW_HANDLE ParentHandle,
    __in PSMALL_RECT Size,
    __in PYORI_STRING Caption,
    __in_opt PYORIWIN_CTRL_HANDLE FirstRadioControl,
    __in WORD Style,
    __in_opt PYORIWIN_NOTIFY ToggleCbk
    );

// WINDOW.C

/**
 A function prototype that can be invoked to deliver notification events
 when the window manager size changes.
 */
typedef VOID FAR YORIWIN_NOTIFY_WINMGR_RESIZE(PYORIWIN_WINDOW_HANDLE, PSMALL_RECT, PSMALL_RECT);

/**
 A pointer to a function that can be invoked to deliver notification events
 when the window manager size changes.
 */
typedef YORIWIN_NOTIFY_WINMGR_RESIZE *PYORIWIN_NOTIFY_WINMGR_RESIZE;

VOID
YoriWinCloseWindow(
    __in PYORIWIN_WINDOW_HANDLE WindowHandle,
    __in DWORD_PTR Result
    );

VOID
YoriWinDestroyWindow(
    __in PYORIWIN_WINDOW_HANDLE WindowHandle
    );

BOOLEAN
YoriWinDisplayWindowContents(
    __in PYORIWIN_WINDOW_HANDLE Window
    );

VOID
YoriWinSetFocus(
    __in PYORIWIN_WINDOW_HANDLE Window,
    __in_opt PYORIWIN_CTRL_HANDLE Ctrl
    );

BOOLEAN
YoriWinSetWinMgrResizeNotifyCbk(
    __in PYORIWIN_WINDOW_HANDLE WindowHandle,
    __in PYORIWIN_NOTIFY_WINMGR_RESIZE NotifyCbk
    );

PYORIWIN_WINMGR_HANDLE
YoriWinGetWinMgrHandle(
    __in PYORIWIN_WINDOW_HANDLE WindowHandle
    );

PYORIWIN_CTRL_HANDLE
YoriWinGetCtrlFromWindow(
    __in PYORIWIN_WINDOW_HANDLE WindowHandle
    );

PYORIWIN_WINDOW_HANDLE
YoriWinGetWindowFromWindowCtrl(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle
    );

/**
 Display a single line border around the window.
 */
#define YORIWIN_WIN_STY_BORDER_SINGLE      (0x0001)

/**
 Display a double line border around the window.
 */
#define YORIWIN_WIN_STY_BORDER_DOUBLE      (0x0002)

/**
 Display a solid shadow under the window.
 */
#define YORIWIN_WIN_STY_SHADOW_SOLID       (0x0004)

/**
 Display a transparent shadow under the window.
 */
#define YORIWIN_WIN_STY_SHADOW_TRANS       (0x0008)

__success(return)
BOOLEAN
YoriWinCreateWindowEx(
    __in PYORIWIN_WINMGR_HANDLE WinMgrHandle,
    __in PSMALL_RECT WindowRect,
    __in WORD Style,
    __in_opt PCYORI_STRING Title,
    __out PYORIWIN_WINDOW_HANDLE *OutWindow
    );

__success(return)
BOOLEAN
YoriWinCreateWindow(
    __in PYORIWIN_WINMGR_HANDLE WinMgrHandle,
    __in WORD MinimumWidth,
    __in WORD MinimumHeight,
    __in WORD DesiredWidth,
    __in WORD DesiredHeight,
    __in WORD Style,
    __in_opt PCYORI_STRING Title,
    __out PYORIWIN_WINDOW_HANDLE *OutWindow
    );

__success(return)
BOOLEAN
YoriWinDetermineWindowRect(
    __in PYORIWIN_WINMGR_HANDLE WinMgrHandle,
    __in WORD MinimumWidth,
    __in WORD MinimumHeight,
    __in WORD DesiredWidth,
    __in WORD DesiredHeight,
    __in WORD DesiredLeft,
    __in WORD DesiredTop,
    __in WORD Style,
    __out PSMALL_RECT WindowRect
    );

BOOLEAN
YoriWinWindowReposition(
    __in PYORIWIN_WINDOW_HANDLE WindowHandle,
    __in PSMALL_RECT WindowRect
    );

VOID
YoriWinGetClientSize(
    __in PYORIWIN_WINDOW_HANDLE Window,
    __out PCOORD Size
    );

VOID
YoriWinEnableNonAltAccelerators(
    __in PYORIWIN_WINDOW_HANDLE WindowHandle,
    __in BOOLEAN EnableNonAltAccelerators
    );

__success(return)
BOOLEAN
YoriWinProcessInputForWindow(
    __in PYORIWIN_WINDOW_HANDLE WindowHandle,
    __out_opt PDWORD_PTR Result
    );

// WINMGR.C

__success(return)
BOOLEAN
YoriWinOpenWinMgr(
    __in BOOLEAN UseAlternateBuffer,
    __in YORIWIN_COLOR_TABLE_ID ColorTableId,
    __out PYORIWIN_WINMGR_HANDLE *WinMgrHandle
    );

VOID
YoriWinMgrSetAsciiDrawing(
    __in PYORIWIN_WINMGR_HANDLE WinMgrHandle,
    __in BOOLEAN UseAsciiDrawing
    );

__success(return)
BOOLEAN
YoriWinGetWinMgrDimensions(
    __in PYORIWIN_WINMGR_HANDLE WinMgrHandle,
    __out PCOORD Size
    );

__success(return)
BOOLEAN
YoriWinGetWinMgrPoint(
    __in PYORIWIN_WINMGR_HANDLE WinMgrHandle,
    __out PSMALL_RECT Rect
    );

__success(return)
BOOLEAN
YoriWinGetWinMgrInitCursorPoint(
    __in PYORIWIN_WINMGR_HANDLE WinMgrHandle,
    __out PCOORD CursorPoint
    );

VOID
YoriWinCloseWinMgr(
    __in PYORIWIN_WINMGR_HANDLE WinMgrHandle
    );

__success(return)
BOOLEAN
YoriWinMgrProcessAllEvents(
    __in PYORIWIN_WINMGR_HANDLE WinMgrHandle
    );

// vim:sw=4:ts=4:et:
