/**
 * @file libwin/border.c
 *
 * Yori display rectangle that could constitute a border on a window or
 * control
 *
 * Copyright (c) 2019 Malcolm J. Smith
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
 Returns the bright shade for a highlight

 @param OriginalAttributes The original color to get the bright shade for

 @return The bright shade of the specified color
 */
WORD
YoriWinBorderGetDarkAttributes(
    __in WORD OriginalAttributes
    )
{
    WORD Forecolor;
    Forecolor = (WORD)(OriginalAttributes & 0x0F);
    if (Forecolor == 0) {
        return (WORD)((OriginalAttributes & 0xf0) | FOREGROUND_INTENSITY);
    }
    return (WORD)((OriginalAttributes & 0xf0) | FOREGROUND_INTENSITY);
}

/**
 Returns the dark shade for a shadow

 @param OriginalAttributes The original color to get the dark shade for

 @return The dark shade of the specified color
 */
WORD
YoriWinBorderGetLightAttributes(
    __in WORD OriginalAttributes
    )
{
    WORD Forecolor;
    Forecolor = (WORD)(OriginalAttributes & 0x0F);
    if (Forecolor == 0) {
        return (WORD)(OriginalAttributes | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
    }
    return (WORD)(OriginalAttributes | FOREGROUND_INTENSITY);
}

/**
 Given the requested border style and attributes, calculate the bright and
 dark attributes as well as characters to use for the border.

 @param WinMgrHandle Pointer to the window manager.

 @param Attributes The base attributes of the region.

 @param BorderType The border style to apply.

 @param TopAttributes Pointer to a variable to receive the highlight color.

 @param BottomAttributes Pointer to a variable to receive the dark color.

 @param BorderChars Pointer to a pointer which will be updated to point to
        the set of characters to use to render the border.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinTransAttrAndBorderStyle(
    __in PYORIWIN_WINMGR_HANDLE WinMgrHandle,
    __in WORD Attributes,
    __in WORD BorderType,
    __out PWORD TopAttributes,
    __out PWORD BottomAttributes,
    __out CONST TCHAR** BorderChars
    )
{ 
    WORD ThreeDMask;
    WORD BorderStyleMask;
    YORIWIN_CHARACTERS CharSet;

    ThreeDMask = (WORD)(BorderType & YORIWIN_BORDER_THREED_MASK);
    *TopAttributes = *BottomAttributes = Attributes;
    switch(ThreeDMask) {
        case YORIWIN_BORDER_TYPE_RAISED:
            *TopAttributes = YoriWinBorderGetLightAttributes(Attributes);
            *BottomAttributes = YoriWinBorderGetDarkAttributes(Attributes);
            break;
        case YORIWIN_BORDER_TYPE_SUNKEN:
            *TopAttributes = YoriWinBorderGetDarkAttributes(Attributes);
            *BottomAttributes = YoriWinBorderGetLightAttributes(Attributes);
            break;
    }

    CharSet = YoriWinChrSingleLineBorder;
    BorderStyleMask = (WORD)(BorderType & YORIWIN_BORDER_STYLE_MASK);
    switch(BorderStyleMask) {
        case YORIWIN_BORDER_TYPE_DOUBLE:
            CharSet = YoriWinChrDoubleLineBorder;
            break;
        case YORIWIN_BORDER_TYPE_SOLID_FULL:
            CharSet = YoriWinChrFullSolidBorder;
            break;
        case YORIWIN_BORDER_TYPE_SOLID_HALF:
            CharSet = YoriWinChrHalfSolidBorder;
            break;
    }

    *BorderChars = YoriWinGetDrawingCharacters(WinMgrHandle, CharSet);

    return TRUE;
}

/**
 Draw a rectangle on the control with the specified coordinates.

 @param Ctrl Pointer to the control to draw the rectangle on.

 @param Dimensions The dimensions of the rectangle to draw.

 @param Attributes The color to use for the border.

 @param BorderType Specifies if the border should be single or double line,
        raised, lowered, or flat

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinDrawBorderCtrl(
    __inout PYORIWIN_CTRL Ctrl,
    __in PSMALL_RECT Dimensions,
    __in WORD Attributes,
    __in WORD BorderType
    )
{
    WORD RowIndex;
    WORD CellIndex;
    WORD TopAttributes;
    WORD BottomAttributes;
    CONST TCHAR* BorderChars;
    PYORIWIN_WINMGR_HANDLE WinMgrHandle;

    WinMgrHandle = YoriWinGetWinMgrHandle(YoriWinGetTopLevelWindow(Ctrl));

    YoriWinTransAttrAndBorderStyle(WinMgrHandle, Attributes, BorderType, &TopAttributes, &BottomAttributes, &BorderChars);

    YoriWinSetCtrlNonClientCell(Ctrl, Dimensions->Left, Dimensions->Top, BorderChars[YORIWIN_DRAW_TOP_LEFT], TopAttributes);
    for (CellIndex = (WORD)(Dimensions->Left + 1); CellIndex < (WORD)Dimensions->Right; CellIndex++) {
        YoriWinSetCtrlNonClientCell(Ctrl, CellIndex, Dimensions->Top, BorderChars[YORIWIN_DRAW_TOP_LINE], TopAttributes);
    }
    YoriWinSetCtrlNonClientCell(Ctrl, Dimensions->Right, Dimensions->Top, BorderChars[YORIWIN_DRAW_TOP_RIGHT], BottomAttributes);

    for (RowIndex = (WORD)(Dimensions->Top + 1); RowIndex <= (WORD)(Dimensions->Bottom - 1); RowIndex++) {
        YoriWinSetCtrlNonClientCell(Ctrl, Dimensions->Left, RowIndex, BorderChars[YORIWIN_DRAW_LEFT_LINE], TopAttributes);
        YoriWinSetCtrlNonClientCell(Ctrl, Dimensions->Right, RowIndex, BorderChars[YORIWIN_DRAW_RIGHT_LINE], BottomAttributes);
    }
    YoriWinSetCtrlNonClientCell(Ctrl, Dimensions->Left, Dimensions->Bottom, BorderChars[YORIWIN_DRAW_BOTTOM_LEFT], TopAttributes);
    for (CellIndex = (WORD)(Dimensions->Left + 1); CellIndex < (WORD)Dimensions->Right; CellIndex++) {
        YoriWinSetCtrlNonClientCell(Ctrl, CellIndex, Dimensions->Bottom, BorderChars[YORIWIN_DRAW_BOTTOM_LINE], BottomAttributes);
    }
    YoriWinSetCtrlNonClientCell(Ctrl, Dimensions->Right, Dimensions->Bottom, BorderChars[YORIWIN_DRAW_BOTTOM_RIGHT], BottomAttributes);

    return TRUE;
}

/**
 Draw a vertical split on the top and bottom of a border.  This routine
 assumes the border has already been drawn.

 @param Ctrl Pointer to the control to draw the split markers on.

 @param Dimensions The dimensions of the border, used to determine the top
        and bottom locations for split characters.

 @param SplitOffset The horizontal offset to draw the split bar.

 @param Attributes The color to use for the border.

 @param BorderType Specifies if the border should be single or double line,
        raised, lowered, or flat

 @param MiddleAttributes On successful completion, updated to contain the
        attributes to use to draw the split bar within the control's client
        area.

 @param MiddleChar On successful completion, updated to contain the
        character to use to draw the split bar within the control's client
        area.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinDrawVerticalSplitCtrl(
    __inout PYORIWIN_CTRL Ctrl,
    __in PSMALL_RECT Dimensions,
    __in WORD SplitOffset,
    __in WORD Attributes,
    __in WORD BorderType,
    __out PWORD MiddleAttributes,
    __out PTCHAR MiddleChar
    )
{
    WORD TopAttributes;
    WORD BottomAttributes;
    CONST TCHAR* BorderChars;
    PYORIWIN_WINMGR_HANDLE WinMgrHandle;

    WinMgrHandle = YoriWinGetWinMgrHandle(YoriWinGetTopLevelWindow(Ctrl));

    YoriWinTransAttrAndBorderStyle(WinMgrHandle, Attributes, BorderType, &TopAttributes, &BottomAttributes, &BorderChars);
    YoriWinSetCtrlNonClientCell(Ctrl, SplitOffset, Dimensions->Top, BorderChars[YORIWIN_DRAW_TOP_T], TopAttributes);
    YoriWinSetCtrlNonClientCell(Ctrl, SplitOffset, Dimensions->Bottom, BorderChars[YORIWIN_DRAW_BOTTOM_T], BottomAttributes);
    *MiddleAttributes = TopAttributes;
    *MiddleChar = BorderChars[YORIWIN_DRAW_MIDDLE_VERT_LINE];
    return TRUE;
}

/**
 Draw characters on the edge of a single line control to represent a limited
 border.

 @param Ctrl Pointer to the control to draw the rectangle on.

 @param Dimensions The dimensions of the rectangle to draw.

 @param Attributes The color to use for the border.

 @param BorderType Specifies if the border should be single or double line,
        raised, lowered, or flat.  This is used within this function to change
        color without changing characters or 3D appearence.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinDrawSingleLineBorderCtrl(
    __inout PYORIWIN_CTRL Ctrl,
    __in PSMALL_RECT Dimensions,
    __in WORD Attributes,
    __in WORD BorderType
    )
{
    PYORIWIN_WINMGR_HANDLE WinMgrHandle;
    WORD AttributesToUse;
    WORD BorderStyleMask;
    CONST TCHAR* BorderChars;

    if (Dimensions->Top != Dimensions->Bottom) {
        return FALSE;
    }

    WinMgrHandle = YoriWinGetWinMgrHandle(YoriWinGetTopLevelWindow(Ctrl));

    AttributesToUse = Attributes;
    BorderChars = YoriWinGetDrawingCharacters(WinMgrHandle, YoriWinChrOneLineSingleBorder);
    BorderStyleMask = (WORD)(BorderType & YORIWIN_BORDER_STYLE_MASK);
    if (BorderStyleMask == YORIWIN_BORDER_TYPE_DOUBLE) {
        BorderChars = YoriWinGetDrawingCharacters(WinMgrHandle, YoriWinChrOneLineDoubleBorder);
    }

    if (BorderType & YORIWIN_BORDER_BRIGHT) {
        AttributesToUse = YoriWinBorderGetLightAttributes(AttributesToUse);
    }

    YoriWinSetCtrlNonClientCell(Ctrl, Dimensions->Left, Dimensions->Top, BorderChars[0], AttributesToUse);
    YoriWinSetCtrlNonClientCell(Ctrl, Dimensions->Right, Dimensions->Top, BorderChars[1], AttributesToUse);

    return TRUE;
}

// vim:sw=4:ts=4:et:
