/**
 * @file more/more.c
 *
 * Yori shell display file contents
 *
 * Copyright (c) 2017-2021 Malcolm J. Smith
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

#include "more.h"

/**
 Help text to display to the user.
 */
const
CHAR strMoreHelpText[] =
        "\n"
        "Output the contents of one or more files with paging and scrolling.\n"
        "\n"
        "MORE [-license] [-b] [-dd] [-f] [-s] [<file>...]\n"
        "\n"
        "   -b             Use basic search criteria for files only\n"
        "   -dd            Use the debug display\n"
        "   -f             Wait for more contents to be added to the file\n"
        "   -l             Display until Ctrl+Q, Scroll Lock, or pause\n"
        "   -s             Process files from all subdirectories\n";

/**
 Display usage text to the user.
 */
BOOL
MoreHelp(VOID)
{
    YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("More %i.%02i\n"), YORI_VER_MAJOR, YORI_VER_MINOR);
#if YORI_BUILD_ID
    YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("  Build %i\n"), YORI_BUILD_ID);
#endif
    YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("%hs"), strMoreHelpText);
    return TRUE;
}

#ifdef YORI_BUILTIN
/**
 The main entrypoint for the more builtin command.
 */
#define ENTRYPOINT YoriCmd_YMORE
#else
/**
 The main entrypoint for the more standalone application.
 */
#define ENTRYPOINT ymain
#endif

/**
 The main entrypoint for the more cmdlet.

 @param ArgC The number of arguments.

 @param ArgV An array of arguments.

 @return Exit code of the process, zero on success, nonzero on failure.
 */
DWORD
ENTRYPOINT(
    __in DWORD ArgC,
    __in YORI_STRING ArgV[]
    )
{
    BOOL ArgumentUnderstood;
    DWORD i;
    DWORD StartArg = 0;
    DWORD CurrentMode;
    DWORD Result;
    BOOLEAN Recursive = FALSE;
    BOOLEAN BasicEnumeration = FALSE;
    BOOL InitComplete;
    BOOLEAN DebugDisplay = FALSE;
    BOOLEAN SuspendPagination = FALSE;
    BOOLEAN WaitForMore = FALSE;
    MORE_CONTEXT MoreContext;
    YORI_STRING Arg;

    ZeroMemory(&MoreContext, sizeof(MoreContext));

    for (i = 1; i < ArgC; i++) {

        ArgumentUnderstood = FALSE;

        if (YoriLibIsCommandLineOption(&ArgV[i], &Arg)) {

            if (YoriLibCompareStringWithLiteralInsensitive(&Arg, _T("?")) == 0) {
                MoreHelp();
                return EXIT_SUCCESS;
            } else if (YoriLibCompareStringWithLiteralInsensitive(&Arg, _T("license")) == 0) {
                YoriLibDisplayMitLicense(_T("2017-2021"));
                return EXIT_SUCCESS;
            } else if (YoriLibCompareStringWithLiteralInsensitive(&Arg, _T("b")) == 0) {
                BasicEnumeration = TRUE;
                ArgumentUnderstood = TRUE;
            } else if (YoriLibCompareStringWithLiteralInsensitive(&Arg, _T("dd")) == 0) {
                DebugDisplay = TRUE;
                ArgumentUnderstood = TRUE;
            } else if (YoriLibCompareStringWithLiteralInsensitive(&Arg, _T("f")) == 0) {
                WaitForMore = TRUE;
                ArgumentUnderstood = TRUE;
            } else if (YoriLibCompareStringWithLiteralInsensitive(&Arg, _T("l")) == 0) {
                SuspendPagination = TRUE;
                ArgumentUnderstood = TRUE;
            } else if (YoriLibCompareStringWithLiteralInsensitive(&Arg, _T("s")) == 0) {
                Recursive = TRUE;
                ArgumentUnderstood = TRUE;
            } else if (YoriLibCompareStringWithLiteralInsensitive(&Arg, _T("-")) == 0) {
                StartArg = i + 1;
                ArgumentUnderstood = TRUE;
                break;
            }
        } else {
            ArgumentUnderstood = TRUE;
            StartArg = i;
            break;
        }

        if (!ArgumentUnderstood) {
            YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("Argument not understood, ignored: %y\n"), &ArgV[i]);
        }
    }

    if (!GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &CurrentMode)) {
        YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("more: output is not interactive console\n"));
        return EXIT_FAILURE;
    }

    //
    //  Attempt to enable backup privilege so an administrator can access more
    //  objects successfully.
    //

    YoriLibEnableBackupPrivilege();

    //
    //  Enabling cancel allows the process to terminate if it's waiting for
    //  input (so the pipe is active) but the user requests the process to
    //  exit.  This is needed for both builtin and non-builtin forms.
    //

    YoriLibCancelEnable();

    if (StartArg == 0 || StartArg == ArgC) {
        InitComplete = MoreInitContext(&MoreContext, 0, NULL, Recursive, BasicEnumeration, DebugDisplay, SuspendPagination, WaitForMore);
    } else {
        InitComplete = MoreInitContext(&MoreContext, ArgC-StartArg, &ArgV[StartArg], Recursive, BasicEnumeration, DebugDisplay, SuspendPagination, WaitForMore);
    }

    Result = EXIT_SUCCESS;
    if (!InitComplete) {
        MoreCleanupContext(&MoreContext);
        Result = EXIT_FAILURE;
    }

    if (Result == EXIT_SUCCESS) {
        MoreViewportDisplay(&MoreContext);
        MoreGracefulExit(&MoreContext);
    }

#if !YORI_BUILTIN
    YoriLibLineReadCleanupCache();
    YoriLibEmptyProcessClipboard();
#endif

    return EXIT_SUCCESS;
}

// vim:sw=4:ts=4:et:
