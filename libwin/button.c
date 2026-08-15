/**
 * @file libwin/button.c
 *
 * Yori window button control
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
 A structure describing the contents of a button control.
 */
typedef struct _YORIWIN_CTRL_BUTTON {

    /**
     A common header for all controls
     */
    YORIWIN_CTRL Ctrl;

    /**
     Pointer to the child label control that renders the text within the
     button.
     */
    PYORIWIN_CTRL Label;

    /**
     A function to invoke when the button is clicked via any mechanism.
     */
    PYORIWIN_NOTIFY ClickCallback;

    /**
     The color to display text in when the button has focus or is pressed.
     */
    WORD SelectedTextAttributes;

    /**
     TRUE if the button is "pressed" as in the mouse is pressed on the
     button.  FALSE if the display is regular.
     */
    BOOLEAN PressedAppearance;

    /**
     TRUE if the button is currently implicitly invoked if the user presses
     enter on the window.
     */
    BOOLEAN EffectiveDefault;

    /**
     TRUE if the button is currently implicitly invoked if the user presses
     escape on the window.
     */
    BOOLEAN EffectiveCancel;

    /**
     TRUE if the button currently has focus, FALSE if another control has
     focus.
     */
    BOOLEAN HasFocus;

    /**
     TRUE if the button should not receive focus.  FALSE for a regular button
     that can receive focus via the tab key.
     */
    BOOLEAN DisableFocus;

} YORIWIN_CTRL_BUTTON, FAR *PYORIWIN_CTRL_BUTTON;

/**
 Draw the button with its current state applied.

 @param Button Pointer to the button to draw.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinButtonPaint(
    __in PYORIWIN_CTRL_BUTTON Button
    )
{
    SMALL_RECT BorderLocation;
    WORD WindowAttributes;
    WORD TextAttributes;
    WORD BorderFlags;
    WORD Height;

    Height = (WORD)(Button->Ctrl.FullRect.Bottom - Button->Ctrl.FullRect.Top + 1);

    BorderLocation.Left = 0;
    BorderLocation.Top = 0;
    BorderLocation.Right = (SHORT)(Button->Ctrl.FullRect.Right - Button->Ctrl.FullRect.Left);
    BorderLocation.Bottom = (SHORT)(Button->Ctrl.FullRect.Bottom - Button->Ctrl.FullRect.Top);

    BorderFlags = YORIWIN_BORDER_TYPE_RAISED;

    if (Button->PressedAppearance) {
        BorderFlags = YORIWIN_BORDER_TYPE_SUNKEN;
    }

    WindowAttributes = Button->Ctrl.DefaultAttributes;
    if (Height >= 3) {
        if (Button->EffectiveDefault || Button->HasFocus) {
            BorderFlags = (WORD)(BorderFlags | YORIWIN_BORDER_TYPE_DOUBLE);
        }
        YoriWinDrawBorderCtrl(&Button->Ctrl, &BorderLocation, WindowAttributes, BorderFlags);
    } else {
        if (Button->EffectiveDefault || Button->HasFocus) {
            BorderFlags = (WORD)(BorderFlags | YORIWIN_BORDER_BRIGHT);
        }
        YoriWinDrawSingleLineBorderCtrl(&Button->Ctrl, &BorderLocation, WindowAttributes, BorderFlags);
    }

    TextAttributes = WindowAttributes;
    if (Button->HasFocus || Button->PressedAppearance) {
        TextAttributes = Button->SelectedTextAttributes;
    }

    YoriWinLabelSetTextAttributes(Button->Label, TextAttributes);

    return TRUE;
}


/**
 Process input events for a button control.

 @param Ctrl Pointer to the button control.

 @param Event Pointer to the input event.

 @return TRUE to indicate that the event was processed and no further
         processing should occur.  FALSE to indicate that regular processing
         should continue (although this does not imply that no processing
         has already occurred.)
 */
BOOLEAN
YoriWinButtonEventHandler(
    __in PYORIWIN_CTRL Ctrl,
    __in PYORIWIN_EVENT Event
    )
{
    PYORIWIN_CTRL_BUTTON Button;
    PYORIWIN_WINDOW_HANDLE TopLevelWindow;
    Button = CONTAINING_RECORD(Ctrl, YORIWIN_CTRL_BUTTON, Ctrl);
    switch(Event->EventType) {
        case YoriWinEventKeyDown:
            if (Event->u.KeyDown.CtrlMask == 0) {
                if ((Event->u.KeyDown.VirtualKeyCode == VK_RETURN) ||
                    (Event->u.KeyDown.VirtualKeyCode == VK_SPACE)) {
                    if (Button->ClickCallback != NULL) {
                        Button->ClickCallback(&Button->Ctrl);
                    }
                } else if (Button->EffectiveCancel &&
                           (Event->u.KeyDown.VirtualKeyCode == VK_ESCAPE)) {
                    if (Button->ClickCallback != NULL) {
                        Button->ClickCallback(&Button->Ctrl);
                    }
                }
            }
            break;
        case YoriWinEventExecute:
            if (Button->ClickCallback != NULL) {
                Button->ClickCallback(&Button->Ctrl);
            }
            break;
        case YoriWinEventParentDestroyed:
            if (Button->Label->NotifyEventFn != NULL) {
                Button->Label->NotifyEventFn(Button->Label, Event);
            }
            YoriWinDestroyControl(Ctrl);
            YoriLibDereference(Button);
            break;
        case YoriWinEventMouseDownClient:
        case YoriWinEventMouseDownNonCli:
            Button->PressedAppearance = TRUE;
            YoriWinButtonPaint(Button);
            break;
        case YoriWinEventMouseUpClient:
        case YoriWinEventMouseUpNonCli:
            Button->PressedAppearance = FALSE;

            //
            //  Repaint and force drawing in the release state so the
            //  callback can display dialogs or other UI without the
            //  button appearing pressed
            //

            YoriWinButtonPaint(Button);
            TopLevelWindow = YoriWinGetTopLevelWindow(&Button->Ctrl);
            YoriWinDisplayWindowContents(TopLevelWindow);
            if (Button->ClickCallback != NULL) {
                Button->ClickCallback(&Button->Ctrl);
            }
            break;
        case YoriWinEventMouseUpOutsideWin:
            Button->PressedAppearance = FALSE;
            YoriWinButtonPaint(Button);
            break;
        case YoriWinEventGetEffectDefault:
            Button->EffectiveDefault = TRUE;
            YoriWinButtonPaint(Button);
            break;
        case YoriWinEventLoseEffectDefault:
            Button->EffectiveDefault = FALSE;
            YoriWinButtonPaint(Button);
            break;
        case YoriWinEventGetEffectCancel:
            Button->EffectiveCancel = TRUE;
            break;
        case YoriWinEventLoseEffectCancel:
            Button->EffectiveCancel = FALSE;
            break;
        case YoriWinEventLoseFocus:
            ASSERT(!Button->DisableFocus);
            ASSERT(Button->HasFocus);
            Button->HasFocus = FALSE;
            YoriWinRestoreDefaultControl(Button->Ctrl.Parent);
            YoriWinButtonPaint(Button);
            break;
        case YoriWinEventGetFocus:
            ASSERT(!Button->DisableFocus);
            ASSERT(!Button->HasFocus);
            Button->HasFocus = TRUE;
            YoriWinSuppressDefaultControl(Button->Ctrl.Parent);
            YoriWinButtonPaint(Button);
            break;
        case YoriWinEventDisplayAccelerators:
            if (Button->Label->NotifyEventFn != NULL) {
                Button->Label->NotifyEventFn(Button->Label, Event);
            }
            break;
        case YoriWinEventHideAccelerators:
            if (Button->Label->NotifyEventFn != NULL) {
                Button->Label->NotifyEventFn(Button->Label, Event);
            }
            break;
    }

    return FALSE;
}

/**
 Set the size and location of a button control, and redraw the contents.

 @param CtrlHandle Pointer to the button to resize or reposition.

 @param CtrlRect Specifies the new size and position of the button.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinButtonReposition(
    __in PYORIWIN_CTRL_HANDLE CtrlHandle,
    __in PSMALL_RECT CtrlRect
    )
{
    PYORIWIN_CTRL Ctrl = (PYORIWIN_CTRL)CtrlHandle;
    PYORIWIN_CTRL_BUTTON Button;
    WORD Height;

    Ctrl = (PYORIWIN_CTRL)CtrlHandle;
    Button = CONTAINING_RECORD(Ctrl, YORIWIN_CTRL_BUTTON, Ctrl);

    Height = (WORD)(CtrlRect->Bottom - CtrlRect->Top + 1);
    if (Height == 0 || Height == 2) {
        return FALSE;
    }

    //
    //  Reset the client area back to cover the entire control.
    //

    Button->Ctrl.ClientRect.Left = 0;
    Button->Ctrl.ClientRect.Top = 0;
    Button->Ctrl.ClientRect.Right = (SHORT)(Button->Ctrl.FullRect.Right - Button->Ctrl.FullRect.Left);
    Button->Ctrl.ClientRect.Bottom = (SHORT)(Button->Ctrl.FullRect.Bottom - Button->Ctrl.FullRect.Top);

    if (!YoriWinControlReposition(Ctrl, CtrlRect)) {
        return FALSE;
    }

    //
    //  Based on the height, calculate what the client offsets ought to be.
    //

    if (Height >= 3) {
        Button->Ctrl.ClientRect.Top++;
        Button->Ctrl.ClientRect.Left++;
        Button->Ctrl.ClientRect.Bottom--;
        Button->Ctrl.ClientRect.Right--;
    } else {
        Button->Ctrl.ClientRect.Left++;
        Button->Ctrl.ClientRect.Right--;
    }

    YoriWinLabelReposition(Button->Label, &Ctrl->ClientRect);

    YoriWinButtonPaint(Button);
    return TRUE;
}

/**
 Create a button control and add it to a window.  This is destroyed when the
 window is destroyed.

 @param ParentHandle Pointer to the parent window.

 @param Size Specifies the location and size of the button.

 @param Caption Specifies the text to display on the button.

 @param Style Specifies style flags for the button including whether it is the
        default or cancel button on a window.

 @param ClickCallback A function to invoke when the button is pressed.

 @return Pointer to the newly created control or NULL on failure.
 */
PYORIWIN_CTRL_HANDLE
YoriWinButtonCreate(
    __in PYORIWIN_WINDOW_HANDLE ParentHandle,
    __in PSMALL_RECT Size,
    __in PCYORI_STRING Caption,
    __in WORD Style,
    __in_opt PYORIWIN_NOTIFY ClickCallback
    )
{
    PYORIWIN_CTRL_BUTTON Button;
    PYORIWIN_WINDOW Parent;
    PYORIWIN_WINDOW TopLevelWindow;
    PYORIWIN_WINMGR_HANDLE WinMgrHandle;
    WORD Height;

    Parent = (PYORIWIN_WINDOW)ParentHandle;

    Height = (WORD)(Size->Bottom - Size->Top + 1);
    if (Height == 0 || Height == 2) {
        return FALSE;
    }

    Button = YoriLibReferencedMalloc(sizeof(YORIWIN_CTRL_BUTTON));
    if (Button == NULL) {
        return NULL;
    }

    ZeroMemory(Button, (DWORD)sizeof(YORIWIN_CTRL_BUTTON));
    if (Style & YORIWIN_BUTTON_STY_NOFOCUS) {
        Button->DisableFocus = TRUE;
    }

    Button->Ctrl.NotifyEventFn = YoriWinButtonEventHandler;
    if (!YoriWinCreateControl(Parent, Size, (BOOLEAN)!Button->DisableFocus, FALSE, &Button->Ctrl)) {
        YoriLibDereference(Button);
        return NULL;
    }

    if (Height >= 3) {
        Button->Ctrl.ClientRect.Top++;
        Button->Ctrl.ClientRect.Left++;
        Button->Ctrl.ClientRect.Bottom--;
        Button->Ctrl.ClientRect.Right--;
    } else {
        Button->Ctrl.ClientRect.Left++;
        Button->Ctrl.ClientRect.Right--;
    }

    Button->ClickCallback = ClickCallback;

    Button->Label = YoriWinLabelCreate(&Button->Ctrl, &Button->Ctrl.ClientRect, Caption, YORIWIN_LABEL_STY_VCENTER | YORIWIN_LABEL_STY_CENTER);
    if (Button->Label == NULL) {
        YoriWinDestroyControl(&Button->Ctrl);
        YoriLibDereference(Button);
        return NULL;
    }

    TopLevelWindow = YoriWinGetTopLevelWindow(Parent);
    WinMgrHandle = YoriWinGetWinMgrHandle(TopLevelWindow);

    Button->SelectedTextAttributes = YoriWinMgrDefaultColorLookup(WinMgrHandle, YoriWinColorControlSelected);

    //
    //  Once the label has parsed what the accelerator char is, steal it so
    //  the parent window will invoke the button control when it is used.
    //

    Button->Ctrl.AcceleratorChar = Button->Label->AcceleratorChar;

    YoriWinButtonPaint(Button);

    if (Style & YORIWIN_BUTTON_STY_DEFAULT) {
        YoriWinSetDefaultCtrl(Parent, &Button->Ctrl);
    }

    if (Style & YORIWIN_BUTTON_STY_CANCEL) {
        YoriWinSetCancelCtrl(Parent, &Button->Ctrl);
    }

    return &Button->Ctrl;
}


// vim:sw=4:ts=4:et:
