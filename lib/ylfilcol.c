/**
 * @file lib/ylfilcol.c
 *
 * Determine colors to apply to files
 *
 * This module implements string parsing and rule application to select a
 * given set of color attributes to render any particular file with.
 *
 * Copyright (c) 2014-2018 Malcolm J. Smith
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

/**
 The default color attributes to apply for file criteria if the user has not
 specified anything else in the environment.
 */
CONST
CHAR YoriLibDefaultFileColorString[] = 
    "fa&r,magenta;"
    "fa&D,lightmagenta;"
    "fa&R,green;"
    "fa&H,green;"
    "fa&S,green;"
    "fe=bat,lightred;"
    "fe=cmd,lightred;"
    "fe=com,lightcyan;"
    "fe=dll,cyan;"
    "fe=doc,white;"
    "fe=docx,white;"
    "fe=exe,lightcyan;"
    "fe=htm,white;"
    "fe=html,white;"
    "fe=pdf,white;"
    "fe=pl,red;"
    "fe=ppt,white;"
    "fe=pptx,white;"
    "fe=ps1,lightred;"
    "fe=psd1,red;"
    "fe=psm1,red;"
    "fe=sys,cyan;"
    "fe=xls,white;"
    "fe=xlsx,white;"
    "fe=ys1,lightred";

/**
 Return the default file color string for display in help texts.
 
 @return Pointer to a const NULL terminated ANSI string containing the default
         file color string.
 */
LPCSTR
YoriLibGetDefaultFileColorStr(VOID)
{
    return YoriLibDefaultFileColorString;
}


/**
 Generate an allocated string containing the user's environment contents
 combined with any default.

 @param Custom Optionally points to a string of colors to include ahead of any
        defaults.

 @param Combined On successful completion, populated with a newly allocated
        string representing the entire set of file color criteria to apply
        in order.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
__success(return)
BOOL
YoriLibLoadCombinedFileColorStr(
    __in_opt PYORI_STRING Custom,
    __out PYORI_STRING Combined
    )
{
    YORI_ALLOC_SIZE_T ColorPrependLength = 0;
    YORI_ALLOC_SIZE_T ColorAppendLength  = 0;
    YORI_ALLOC_SIZE_T ColorReplaceLength = 0;
    YORI_ALLOC_SIZE_T ColorCustomLength = 0;
    YORI_ALLOC_SIZE_T i;
    DWORD CharsRequired;
    LPTSTR PrependVarName = _T("YORICOLORPREPEND");
    LPTSTR ReplaceVarName = _T("YORICOLORREPLACE");
    LPTSTR AppendVarName = _T("YORICOLORAPPEND");

    //
    //  Load any user specified colors from the environment.  Prepend values go before
    //  any default; replace values supersede any default; and append values go last.
    //  We need to insert semicolons between these values if they're specified.
    //
    //  First, count how big the allocation needs to be and allocate it.
    //

    ColorPrependLength = (YORI_ALLOC_SIZE_T)GetEnvironmentVariable(PrependVarName, NULL, 0);
    if (ColorPrependLength == 0) {
        PrependVarName = _T("SDIR_COLOR_PREPEND");
        ColorPrependLength = (YORI_ALLOC_SIZE_T)GetEnvironmentVariable(PrependVarName, NULL, 0);
    }
    ColorReplaceLength = (YORI_ALLOC_SIZE_T)GetEnvironmentVariable(ReplaceVarName, NULL, 0);
    if (ColorReplaceLength == 0) {
        ReplaceVarName = _T("SDIR_COLOR_REPLACE");
        ColorReplaceLength = (YORI_ALLOC_SIZE_T)GetEnvironmentVariable(ReplaceVarName, NULL, 0);
    }
    ColorAppendLength  = (YORI_ALLOC_SIZE_T)GetEnvironmentVariable(AppendVarName, NULL, 0);
    if (ColorAppendLength == 0) {
        AppendVarName = _T("SDIR_COLOR_APPEND");
        ColorAppendLength = (YORI_ALLOC_SIZE_T)GetEnvironmentVariable(AppendVarName, NULL, 0);
    }
    if (Custom != NULL) {
        ColorCustomLength = Custom->LengthInChars;
    }

    CharsRequired = 0;
    CharsRequired = CharsRequired + ColorPrependLength + 1 + ColorAppendLength + 1 + ColorReplaceLength + 1 + ColorCustomLength + sizeof(YoriLibDefaultFileColorString);

    if (!YoriLibIsSizeAllocatable(CharsRequired)) {
        return FALSE;
    }

    if (!YoriLibAllocateString(Combined, (YORI_ALLOC_SIZE_T)CharsRequired)) {
        return FALSE;
    }

    //
    //  Now, load any environment variables into the buffer.  If replace isn't
    //  specified, we use the hardcoded default.
    //

    i = 0;
    if (ColorPrependLength) {
        GetEnvironmentVariable(PrependVarName,
                               Combined->StartOfString,
                               ColorPrependLength);
        i = (YORI_ALLOC_SIZE_T)(i + ColorPrependLength);
        Combined->StartOfString[i - 1] = ';';
    }

    if (ColorCustomLength) {
        YoriLibSPrintfS(&Combined->StartOfString[i], ColorCustomLength + 1, _T("%y"), Custom);
        i = (YORI_ALLOC_SIZE_T)(i + ColorCustomLength);
        Combined->StartOfString[i] = ';';
        i++;
    }

    if (ColorReplaceLength) {
        GetEnvironmentVariable(ReplaceVarName,
                               &Combined->StartOfString[i],
                               ColorReplaceLength);
        i = (YORI_ALLOC_SIZE_T)(i + ColorReplaceLength - 1);
    } else {
        YoriLibSPrintfS(&Combined->StartOfString[i],
                        sizeof(YoriLibDefaultFileColorString) / sizeof(YoriLibDefaultFileColorString[0]),
                        _T("%hs"),
                        YoriLibDefaultFileColorString);
        i += sizeof(YoriLibDefaultFileColorString) / sizeof(YoriLibDefaultFileColorString[0]) - 1;
    }

    if (ColorAppendLength) {
        Combined->StartOfString[i] = ';';
        i += 1;
        GetEnvironmentVariable(AppendVarName,
                               &Combined->StartOfString[i],
                               ColorAppendLength);
        i = i + ColorAppendLength - 1;
    }

    Combined->LengthInChars = i;

    return TRUE;
}

/**
 The default colors to display file metadata with.
 */
const
CHAR YoriLibDefaultMetadataColorStr[] = 
    ";"
    "fs,yellow;"
    "mo,underline+lightblue;"
    "nf,lightgreen;"
    ;

/**
 Obtain a numeric color code given a (typically two character) string
 describing metadata of interest.  Note this routine has to reconstruct
 and reparse the criteria string on each call, so it is only useful for
 programs displaying a small amount of metadata color.

 @param RequestedAttributeCodeString Pointer to a Yori string containing
        the metadata character code to locate.

 @param Color On successful completion, populated with the color to display.

 @return TRUE to indicate success, implying that the attribute code was
         found either in the user's environment or the default string.  FALSE
         to indicate no color could be determined.
 */
__success(return)
BOOL
YoriLibGetMetadataColor(
    __in PYORI_STRING RequestedAttributeCodeString,
    __out PYORILIB_COLOR_ATTRIBUTES Color
    )
{
    YORI_STRING CriteriaString;
    YORI_STRING Remaining;
    YORI_STRING Element;
    YORI_STRING FoundAttributeCodeString;
    YORI_STRING FoundColorString;
    LPTSTR Seperator;
    LPTSTR NextStart;
    YORILIB_COLOR_ATTRIBUTES FoundColor;
    YORILIB_COLOR_ATTRIBUTES WindowColor;
    YORI_ALLOC_SIZE_T CustomLength = 0;
    LPTSTR EnvVarName = _T("YORICOLORMETADATA");

    YoriLibInitEmptyString(&Remaining);
    YoriLibInitEmptyString(&Element);
    YoriLibInitEmptyString(&FoundAttributeCodeString);
    YoriLibInitEmptyString(&FoundColorString);

    //
    //  Query for any customizations.  If none are present with the new
    //  variable name, check if there are any with the old SDIR variable
    //  name.
    //

    CustomLength = (YORI_ALLOC_SIZE_T)GetEnvironmentVariable(EnvVarName, NULL, 0);
    if (CustomLength == 0) {
        EnvVarName = _T("SDIR_COLOR_METADATA");
        CustomLength = (YORI_ALLOC_SIZE_T)GetEnvironmentVariable(EnvVarName, NULL, 0);
    }

    if (!YoriLibAllocateString(&CriteriaString, CustomLength + sizeof(YoriLibDefaultMetadataColorStr))) {
        return FALSE;
    }

    if (CustomLength > 0) {
        CustomLength = (YORI_ALLOC_SIZE_T)GetEnvironmentVariable(EnvVarName, CriteriaString.StartOfString, CustomLength);
        CriteriaString.LengthInChars = CustomLength;
    }

    CriteriaString.LengthInChars = CriteriaString.LengthInChars + YoriLibSPrintf(&CriteriaString.StartOfString[CustomLength], _T("%hs"), YoriLibDefaultMetadataColorStr);

    Remaining.StartOfString = CriteriaString.StartOfString;
    Remaining.LengthInChars = CriteriaString.LengthInChars;

    while (TRUE) {
        NextStart = YoriLibFindLeftMostCharacter(&Remaining, ';');
        Element.StartOfString = Remaining.StartOfString;
        if (NextStart != NULL) {
            Element.LengthInChars = (YORI_ALLOC_SIZE_T)(NextStart - Element.StartOfString);
        } else {
            Element.LengthInChars = Remaining.LengthInChars;
        }

        YoriLibTrimSpaces(&Element);
        if (Element.LengthInChars > 0) {
            Seperator = YoriLibFindLeftMostCharacter(&Element, ',');
            if (Seperator != NULL) {
                FoundAttributeCodeString.StartOfString = Element.StartOfString;
                FoundAttributeCodeString.LengthInChars = (YORI_ALLOC_SIZE_T)(Seperator - Element.StartOfString);
                FoundColorString.StartOfString = Seperator + 1;
                FoundColorString.LengthInChars = (YORI_ALLOC_SIZE_T)(Element.LengthInChars - FoundAttributeCodeString.LengthInChars - 1);


                if (YoriLibCompareStringIns(RequestedAttributeCodeString, &FoundAttributeCodeString) == 0) {
                    YoriLibAttrFromString(&FoundColorString, &FoundColor);
                    WindowColor.Ctrl = 0;
                    WindowColor.Win32Attr = (UCHAR)YoriLibVtGetDefaultColor();
                    YoriLibResolveWindowColors(FoundColor, WindowColor, TRUE, &FoundColor);
                    YoriLibFreeStringContents(&CriteriaString);
                    Color->Ctrl = FoundColor.Ctrl;
                    Color->Win32Attr = FoundColor.Win32Attr;
                    return TRUE;
                }
            }
        }

        if (NextStart == NULL) {
            break;
        }

        Remaining.StartOfString = NextStart;
        Remaining.LengthInChars = (YORI_ALLOC_SIZE_T)(CriteriaString.LengthInChars - (YORI_ALLOC_SIZE_T)(NextStart - CriteriaString.StartOfString));

        if (Remaining.LengthInChars > 0) {
            Remaining.StartOfString++;
            Remaining.LengthInChars--;
        }

        if (Remaining.LengthInChars == 0) {
            break;
        }
    }

    YoriLibFreeStringContents(&CriteriaString);
    return FALSE;
}


// vim:sw=4:ts=4:et:
