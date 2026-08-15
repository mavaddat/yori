/**
 * @file libwin/combo.c
 *
 * Yori window combo control
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
 A structure describing the contents of a combo control.
 */
typedef struct _YORIWIN_CTRL_COMBO {

    /**
     A common header for all controls
     */
    YORIWIN_CTRL Ctrl;

    /**
     Pointer to the child edit control that renders the text within the
     combo.
     */
    PYORIWIN_CTRL Edit;

    /**
     The set of options to display in the drop down list.
     */
    YORIWIN_ITEM_ARRAY ItemArray;

    /**
     A function to invoke when the item is changed via any mechanism.
     */
    PYORIWIN_NOTIFY ClickCallback;

    /**
     The index within ItemArray of the array element that is currently
     highlighted
     */
    YORI_ALLOC_SIZE_T ActiveOption;

    /**
     Set to TRUE if any option is activated.  FALSE if no item has been
     activated.
     */
    BOOLEAN ItemActive;

    /**
     The number of lines to display in the pulldown list.
     */
    WORD LinesInList;

} YORIWIN_CTRL_COMBO, FAR *PYORIWIN_CTRL_COMBO;

/**
 Returns the currently active option within the combo control.

 @param CtrlHandle Pointer to the combo control.

 @param CurrentlyActiveIndex On successful completion, populated with the
        currently selected combo item.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
__success(return)
BOOLEAN
YoriWinComboGetActiveOption(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __out PYORI_ALLOC_SIZE_T CurrentlyActiveIndex
    )
{
    PYORIWIN_CTRL Ctrl;
    PYORIWIN_CTRL_COMBO Combo;
    Ctrl = (PYORIWIN_CTRL)CtrlHandle;
    Combo = CONTAINING_RECORD(Ctrl, YORIWIN_CTRL_COMBO, Ctrl);
    if (!Combo->ItemActive) {
        return FALSE;
    }
    *CurrentlyActiveIndex = Combo->ActiveOption;
    return TRUE;
}

/**
 Set the currently selected option within the combo control.

 @param CtrlHandle Pointer to the combo control.

 @param ActiveOption Specifies the index of the item to select.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
__success(return)
BOOLEAN
YoriWinComboSetActiveOption(
    __inout PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in YORI_ALLOC_SIZE_T ActiveOption
    )
{
    PYORIWIN_CTRL Ctrl;
    PYORIWIN_CTRL_COMBO Combo;
    Ctrl = (PYORIWIN_CTRL)CtrlHandle;
    Combo = CONTAINING_RECORD(Ctrl, YORIWIN_CTRL_COMBO, Ctrl);

    if (ActiveOption < Combo->ItemArray.Count) {
        Combo->ItemActive = TRUE;
        Combo->ActiveOption = ActiveOption;
        YoriWinEditSetText(Combo->Edit, &Combo->ItemArray.Items[Combo->ActiveOption].String);
        return TRUE;
    }

    return FALSE;
}


/**
 A function to be invoked when an event of interest occurs when the list popup
 window is displayed.

 @param Ctrl Pointer to the window control.

 @param Event Pointer to the event information.

 @return TRUE to indicate that the event was processed and no further
         processing should occur.  FALSE to indicate regular processing
         should continue.
 */
BOOLEAN
YoriWinComboChildEvent(
    __in PYORIWIN_CTRL Ctrl,
    __in PYORIWIN_EVENT Event
    )
{
    PYORIWIN_WINDOW Window;
    PYORIWIN_CTRL ListCtrl;
    YORI_ALLOC_SIZE_T ActiveIndex;

    Window = YoriWinGetWindowFromWindowCtrl(Ctrl);
    ListCtrl = YoriWinFindControlById(Ctrl, 1L);
    if (ListCtrl == NULL) {
        return FALSE;
    }

    switch(Event->EventType) {
        case YoriWinEventKeyDown:
            if (Event->u.KeyDown.VirtualKeyCode == VK_ESCAPE) {
                YoriWinCloseWindow(Window, 0L);
            } else if (Event->u.KeyDown.VirtualKeyCode == VK_RETURN ||
                       Event->u.KeyDown.VirtualKeyCode == VK_TAB) {
                if (YoriWinListGetActiveOption(ListCtrl, &ActiveIndex)) {
                    YoriWinCloseWindow(Window, ActiveIndex + 1);
                }
            }
            break;
        case YoriWinEventMouseDownClient:
            if (YoriWinListGetActiveOption(ListCtrl, &ActiveIndex)) {
                YoriWinCloseWindow(Window, ActiveIndex + 1);
            }
            break;
        case YoriWinEventMouseDownOutsideWin:
            YoriWinCloseWindow(Window, 0L);
            break;
    }
    return FALSE;
}

/**
 When the downward arrow is activated, display the pulldown list populated
 with possible items.

 @param Combo Pointer to the combo control.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinComboDisplayPullDown(
    __in PYORIWIN_CTRL_COMBO Combo
    )
{
    PYORIWIN_CTRL Ctrl;
    COORD CtrlCoord;
    COORD ScreenCoord;
    PYORIWIN_WINDOW TopLevelWindow;
    PYORIWIN_WINMGR_HANDLE WinMgrHandle;
    PYORIWIN_WINDOW_HANDLE ComboChildWindow;
    PYORIWIN_CTRL List;
    SMALL_RECT ChildRect;
    SMALL_RECT ListRect;
    DWORD_PTR ChildResult;

    Ctrl = &Combo->Ctrl;

    CtrlCoord.X = 0;
    CtrlCoord.Y = 0;
    YoriWinTransCtrlCoordToScreen(Ctrl, FALSE, CtrlCoord, &ScreenCoord);
    TopLevelWindow = YoriWinGetTopLevelWindow(Ctrl);
    WinMgrHandle = YoriWinGetWinMgrHandle(TopLevelWindow);

    ChildRect.Left = ScreenCoord.X;
    ChildRect.Top = (SHORT)(ScreenCoord.Y + 1);
    ChildRect.Right = (SHORT)(ChildRect.Left + Ctrl->FullRect.Right - Ctrl->FullRect.Left);
    ChildRect.Bottom = (SHORT)(ChildRect.Top + Combo->LinesInList + 1);

    if (!YoriWinCreateWindowEx(WinMgrHandle, &ChildRect, 0, NULL, &ComboChildWindow)) {
        return FALSE;
    }

    ListRect.Left = 0;
    ListRect.Top = 0;
    ListRect.Right = (SHORT)(ChildRect.Right - ChildRect.Left);
    ListRect.Bottom = (SHORT)(ChildRect.Bottom - ChildRect.Top);
    List = YoriWinListCreate(ComboChildWindow, &ListRect, YORIWIN_LIST_STY_VSCROLL);
    if (List == NULL) {
        YoriWinDestroyWindow(ComboChildWindow);
        return FALSE;
    }

    YoriWinSetCtrlId(List, 1L);
    if (Combo->ItemArray.Count > 0) {
        YoriWinListAddItemArray(List, &Combo->ItemArray);
    }

    YoriWinSetCustomNotification(ComboChildWindow, YoriWinEventKeyDown, YoriWinComboChildEvent);
    YoriWinSetCustomNotification(ComboChildWindow, YoriWinEventMouseDownClient, YoriWinComboChildEvent);
    YoriWinSetCustomNotification(ComboChildWindow, YoriWinEventMouseDownOutsideWin, YoriWinComboChildEvent);
    ChildResult = 0;
    YoriWinMgrLockMouseExcl(WinMgrHandle, ComboChildWindow);
    if (!YoriWinProcessInputForWindow(ComboChildWindow, &ChildResult)) {
        ChildResult = 0;
    }

    YoriWinDestroyWindow(ComboChildWindow);

    //
    //  Don't change the text until the edit control has focus again, so it
    //  knows where to put the cursor.
    //

    if (ChildResult > 0) {
        if (YoriWinComboSetActiveOption(&Combo->Ctrl, (YORI_ALLOC_SIZE_T)(ChildResult - 1))) {
            if (Combo->ClickCallback != NULL) {
                Combo->ClickCallback(&Combo->Ctrl);
            }
        }
    }

    return TRUE;
}

/**
 Process input events for a combo control.

 @param Ctrl Pointer to the combo control.

 @param Event Pointer to the input event.

 @return TRUE to indicate that the event was processed and no further
         processing should occur.  FALSE to indicate that regular processing
         should continue (although this does not imply that no processing
         has already occurred.)
 */
BOOLEAN
YoriWinComboEventHandler(
    __in PYORIWIN_CTRL Ctrl,
    __in PYORIWIN_EVENT Event
    )
{
    PYORIWIN_CTRL_COMBO Combo;
    Combo = CONTAINING_RECORD(Ctrl, YORIWIN_CTRL_COMBO, Ctrl);
    switch(Event->EventType) {
        case YoriWinEventKeyDown:
            if (Event->u.KeyDown.VirtualKeyCode == VK_DOWN) {
                YoriWinComboDisplayPullDown(Combo);
            }
            break;
        case YoriWinEventExecute:
            break;
        case YoriWinEventParentDestroyed:
            if (Combo->Edit->NotifyEventFn != NULL) {
                Combo->Edit->NotifyEventFn(Combo->Edit, Event);
            }
            YoriWinItemArrayCleanup(&Combo->ItemArray);
            YoriWinDestroyControl(Ctrl);
            YoriLibDereference(Combo);
            break;
        case YoriWinEventMouseDownNonCli:
            YoriWinComboDisplayPullDown(Combo);
            break;
        case YoriWinEventMouseDownClient:
            break;
        case YoriWinEventMouseUpClient:
        case YoriWinEventMouseUpNonCli:
            break;
        case YoriWinEventMouseUpOutsideWin:
            break;
        case YoriWinEventLoseFocus:
        case YoriWinEventGetFocus:
            if (Combo->Edit != NULL &&
                Combo->Edit->NotifyEventFn != NULL) {

                Combo->Edit->NotifyEventFn(Combo->Edit, Event);
            }
            break;
    }

    return FALSE;
}

/**
 Adds new items to the combo control.

 @param CtrlHandle Pointer to the combo control to add items to.

 @param ListOptions Pointer to an array of options to include in the list
        control.

 @param NumberOptions Specifies the number of options in the above array.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
__success(return)
BOOLEAN
YoriWinComboAddItems(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in PCYORI_STRING ListOptions,
    __in YORI_ALLOC_SIZE_T NumberOptions
    )
{
    PYORIWIN_CTRL Ctrl;
    PYORIWIN_CTRL_COMBO Combo;

    Ctrl = (PYORIWIN_CTRL)CtrlHandle;
    Combo = CONTAINING_RECORD(Ctrl, YORIWIN_CTRL_COMBO, Ctrl);

    if (!YoriWinItemArrayAddItems(&Combo->ItemArray, ListOptions, NumberOptions)) {
        return FALSE;
    }

    return TRUE;
}

/**
 Set the size and location of a combo control, and redraw the contents.

 @param CtrlHandle Pointer to the combo to resize or reposition.

 @param CtrlRect Specifies the new size and position of the combo.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinComboReposition(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in PSMALL_RECT CtrlRect
    )
{
    PYORIWIN_CTRL Ctrl = (PYORIWIN_CTRL)CtrlHandle;
    PYORIWIN_CTRL_COMBO Combo;
    CONST TCHAR* DownChars;
    PYORIWIN_WINMGR_HANDLE WinMgrHandle;

    Ctrl = (PYORIWIN_CTRL)CtrlHandle;
    Combo = CONTAINING_RECORD(Ctrl, YORIWIN_CTRL_COMBO, Ctrl);

    if (!YoriWinControlReposition(Ctrl, CtrlRect)) {
        return FALSE;
    }

    WinMgrHandle = YoriWinGetWinMgrHandle(YoriWinGetTopLevelWindow(Ctrl));
    DownChars = YoriWinGetDrawingCharacters(WinMgrHandle, YoriWinChrComboDown);

    YoriWinSetCtrlNonClientCell(&Combo->Ctrl, (SHORT)(Combo->Ctrl.ClientRect.Right + 1), 0, DownChars[0], Combo->Ctrl.DefaultAttributes);
    YoriWinEditReposition(Combo->Edit, &Combo->Ctrl.ClientRect);
    return TRUE;
}


/**
 Create a combo control and add it to a window.  This is destroyed when the
 window is destroyed.

 @param ParentHandle Pointer to the parent window.

 @param Size Specifies the location and size of the combo.

 @param LinesInList Specifies the number of lines to display in the dropdown
        list.  Note this refers to the number of entries in the list (ie., the
        client area of the list), which is less than the list will use to
        render to the screen due to the border.

 @param Caption Specifies the text to display on the combo.

 @param Style Specifies style flags for the combo including whether it is the
        default or cancel combo on a window.

 @param ClickCallback A function to invoke when the combo is pressed.

 @return Pointer to the newly created control or NULL on failure.
 */
PYORIWIN_CTRL_HANDLE
YoriWinComboCreate(
    __in PYORIWIN_WINDOW_HANDLE ParentHandle,
    __in PSMALL_RECT Size,
    __in WORD LinesInList,
    __in PYORI_STRING Caption,
    __in WORD Style,
    __in_opt PYORIWIN_NOTIFY ClickCallback
    )
{
    PYORIWIN_CTRL_COMBO Combo;
    PYORIWIN_WINDOW Parent;
    CONST TCHAR* DownChars;
    PYORIWIN_WINMGR_HANDLE WinMgrHandle;

    Parent = (PYORIWIN_WINDOW)ParentHandle;

    UNREFERENCED_PARAMETER(Style);

    Combo = YoriLibReferencedMalloc(sizeof(YORIWIN_CTRL_COMBO));
    if (Combo == NULL) {
        return NULL;
    }

    ZeroMemory(Combo, (DWORD)sizeof(YORIWIN_CTRL_COMBO));

    YoriWinItemArrayInitialize(&Combo->ItemArray);

    Combo->Ctrl.NotifyEventFn = YoriWinComboEventHandler;
    if (!YoriWinCreateControl(Parent, Size, TRUE, FALSE, &Combo->Ctrl)) {
        YoriLibDereference(Combo);
        return NULL;
    }

    WinMgrHandle = YoriWinGetWinMgrHandle(YoriWinGetTopLevelWindow(&Combo->Ctrl));
    DownChars = YoriWinGetDrawingCharacters(WinMgrHandle, YoriWinChrComboDown);


    YoriWinSetCtrlNonClientCell(&Combo->Ctrl, Combo->Ctrl.ClientRect.Right, 0, DownChars[0], Combo->Ctrl.DefaultAttributes);

    Combo->Ctrl.ClientRect.Right--;

    Combo->Edit = YoriWinEditCreate(&Combo->Ctrl, &Combo->Ctrl.ClientRect, Caption, YORIWIN_EDIT_STY_READ_ONLY);
    if (Combo->Edit == NULL) {
        YoriWinDestroyControl(&Combo->Ctrl);
        YoriLibDereference(Combo);
        return NULL;
    }

    Combo->LinesInList = LinesInList;
    Combo->ClickCallback = ClickCallback;

    return &Combo->Ctrl;
}


// vim:sw=4:ts=4:et:
