/**
 * @file sdir/init.c
 *
 * Colorful, sorted and optionally rich directory enumeration
 * for Windows.
 *
 * This module implements initialization support including argument parsing
 * and initializing default options.
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

#include "sdir.h"

/**
 Invoked when the user presses Ctrl+C or similar during program execution.

 @param dwHandlerType The type of event that the user initiated.

 @return TRUE to terminate processing of further handlers, FALSE to call the
         next handler in the chain.
 */
BOOL WINAPI
SdirCancelHandler (
    __in DWORD dwHandlerType
    )
{
    UNREFERENCED_PARAMETER(dwHandlerType);
    Opts->Cancelled = TRUE;
    return TRUE;
}

/**
 Initialize global application state.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOL
SdirAppInitialize()
{
    CONSOLE_SCREEN_BUFFER_INFO ScreenInfo;
    HANDLE hConsoleOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE ProcessToken;
    DWORD CurrentMode;

    Opts = YoriLibMalloc(sizeof(SDIR_OPTS));
    if (Opts == NULL) {
        return FALSE;
    }
    ZeroMemory(Opts, sizeof(SDIR_OPTS));

    Summary = YoriLibMalloc(sizeof(SDIR_SUMMARY));
    if (Summary == NULL) {
        return FALSE;
    }
    ZeroMemory(Summary, sizeof(SDIR_SUMMARY));

    //
    //  For simplicity, initialize this now.  On failure we restore to
    //  this value.  Hopefully we'll find the correct value before any
    //  failure can occur.
    //

    Opts->PreviousAttributes = SdirDefaultColor;

    //
    //  Check if we're talking to a console or some other kind of object
    //  (file, pipe, etc.)
    //

    if (!GetConsoleMode(hConsoleOutput, &CurrentMode)) {
        Opts->OutputWithConsoleApi = FALSE;
    } else {
        Opts->OutputHasAutoLineWrap = TRUE;
        Opts->OutputExtendedCharacters = TRUE;
        Opts->OutputWithConsoleApi = TRUE;
        Opts->EnablePause = TRUE;
    }

    //
    //  Try to determine the console width so we can size columns.  We
    //  try to use the window size, not the buffer size.
    //  If we're not talking to a console, this might fail, and we have
    //  to fall back to some kind of default.
    //

    if (!GetConsoleScreenBufferInfo(hConsoleOutput, &ScreenInfo)) {
        if (GetLastError() == ERROR_INVALID_HANDLE) {
            if (Opts->ConsoleWidth == 0) {
                Opts->ConsoleWidth = 100;
            }
            Opts->ConsoleBufferWidth = Opts->ConsoleWidth;
            Opts->EnablePause = FALSE;
        } else {
            SdirDisplayError(GetLastError(), _T("GetConsoleScreenBufferInfo"));
            return FALSE;
        }
    } else {
        Opts->ConsoleBufferWidth = 
        Opts->ConsoleWidth = ScreenInfo.dwSize.X;
        
        if (Opts->ConsoleWidth > (DWORD)(ScreenInfo.srWindow.Right - ScreenInfo.srWindow.Left + 1)) {
            Opts->ConsoleWidth = (DWORD)(ScreenInfo.srWindow.Right - ScreenInfo.srWindow.Left + 1);
        }
    
        Opts->ConsoleHeight = ScreenInfo.dwSize.Y;
        if (Opts->ConsoleHeight > (DWORD)(ScreenInfo.srWindow.Bottom - ScreenInfo.srWindow.Top + 1)) {
            Opts->ConsoleHeight = (DWORD)(ScreenInfo.srWindow.Bottom - ScreenInfo.srWindow.Top + 1);
        }

        //
        //  This is another kludge.  To make progress we need to be able
        //  to display one line of output between "press any key" prompts,
        //  so we need to assume the console can hold two lines.  If we
        //  don't make this assumption, we'll never go forward.
        //

        if (Opts->ConsoleHeight < 2) {
            Opts->ConsoleHeight = 2;
        }

        YoriLibSetColorToWin32(&Opts->PreviousAttributes, (UCHAR)(ScreenInfo.wAttributes & SDIR_ATTRIBUTE_FULLCOLOR_MASK));
    }

    SdirDefaultColor = YoriLibResolveWindowColorComponents(SdirDefaultColor, Opts->PreviousAttributes, TRUE);

    if (Opts->ConsoleWidth > SDIR_MAX_WIDTH) {
        Opts->ConsoleWidth = SDIR_MAX_WIDTH;
    }

    //
    //  When running on WOW64, we don't want file system redirection,
    //  because we want people to be able to enumerate those paths
    //

    if (Kernel32.pWow64DisableWow64FsRedirection) {
        PVOID DontCare;
        Kernel32.pWow64DisableWow64FsRedirection(&DontCare);
    }

    //
    //  Attempt to enable backup privilege.  This allows us to enumerate and recurse
    //  through objects which normally ACLs would prevent.
    //

    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &ProcessToken)) {
        struct {
            TOKEN_PRIVILEGES TokenPrivileges;
            LUID_AND_ATTRIBUTES BackupPrivilege;
        } PrivilegesToChange;

        LookupPrivilegeValue(NULL, SE_BACKUP_NAME, &PrivilegesToChange.TokenPrivileges.Privileges[0].Luid);

        PrivilegesToChange.TokenPrivileges.PrivilegeCount = 1;
        PrivilegesToChange.TokenPrivileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

        AdjustTokenPrivileges(ProcessToken, FALSE, (PTOKEN_PRIVILEGES)&PrivilegesToChange, sizeof(PrivilegesToChange), NULL, NULL);
        CloseHandle(ProcessToken);
    }

    //
    //  Grab the version of the running OS so we can highlight binaries that
    //  need a newer one
    //

    Opts->OsVersion = GetVersion();

    //
    //  Look for Ctrl+C to indicate execution should terminate.
    //  Not much we can do on failure.
    //

    SetConsoleCtrlHandler(SdirCancelHandler, TRUE);

    return TRUE;
}

/**
 After command line options have been processed, initialize in memory state
 to ensure we can fulfil the user's requests.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOL
SdirOptInitialize()
{
    ULONG i, j;

    //
    //  Calculate the amount of metadata in each column
    //

    Opts->MetadataWidth = 1;  // Column seperator

    //
    //  If no sorting algorithm was used, use the default that
    //  we prepopulated.
    //

    if (Opts->CurrentSort == 0) {
        Opts->CurrentSort++;
    }

    for (i = 0; i < SdirGetNumSdirOptions(); i++) {

        PSDIR_FEATURE Feature;

        Feature = SdirFeatureByOptionNumber(i);
       
        //
        //  If we're displaying or sorting, we need the data to use
        //

        if (Feature->Flags & SDIR_FEATURE_DISPLAY) {

            Feature->Flags |= SDIR_FEATURE_COLLECT;
        }

        for (j = 0; j < Opts->CurrentSort; j++) {

            if (SdirOptions[i].CompareFn == Opts->Sort[j].CompareFn) {
                Feature->Flags |= SDIR_FEATURE_COLLECT;
            }
        }

        //
        //  If we're displaying, we need space to display in
        //  

        if ((Feature->Flags & SDIR_FEATURE_DISPLAY) &&
               SdirOptions[i].WidthFn) {

            YORILIB_COLOR_ATTRIBUTES Attr = {0};

            Opts->MetadataWidth += SdirOptions[i].WidthFn(NULL, Attr, NULL);
        }
    }

    //
    //  If we need to count the average link size, we need to know the link
    //  count too
    //

    if (Opts->EnableAverageLinkSize) {
        Opts->FtLinkCount.Flags |= SDIR_FEATURE_COLLECT;
    }

    return TRUE;
}

/**
 If the request is for a recursive enumerate, populates the root of the
 enumeration path from a user string.  This implies resolving the user string
 back to a fully qualified path.

 @param SubDirPath The root of the recursive enumerate.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOL
SdirSetSubdirWalk(
    __in LPTSTR SubDirPath
    )
{
    YORI_STRING UserPath;
    YORI_STRING SubDirWalk;
    YoriLibInitEmptyString(&SubDirWalk);
    YoriLibConstantString(&UserPath, SubDirPath);
    if (!YoriLibUserStringToSingleFilePath(&UserPath, TRUE, &SubDirWalk)) {
        SdirDisplayError(GetLastError(), _T("YoriLibUserStringToSingleFilePath"));
        return FALSE;
    }

    Opts->SubDirWalk = SubDirWalk.StartOfString;

    return TRUE;
}


/**
 Process a single command line option and configure in memory state to
 correspond to it.

 @param Opt Pointer to a NULL terminated string from the user specifying the
        option.

 @return TRUE if the option was parsed successfully, FALSE if it was not
         understood or not processed successfully.
 */
BOOL
SdirParseOpt (
    __in LPTSTR Opt
    )
{
    BOOL OptParsed = FALSE;
    DWORD i, j;

    if (Opt[0] == 'a') {
        if (Opt[1] == 'l') {
            if (Opt[2] == 'n') {
                Opts->EnableAverageLinkSize = FALSE;
                OptParsed = TRUE;
            } else if (Opt[2] == '\0') {
                Opts->EnableAverageLinkSize = TRUE;
                OptParsed = TRUE;
            }
        }
    } else if (Opt[0] == 'b') {
        if (Opt[1] == 'r') {
            Opts->BriefRecurseDepth = SdirStringToNum32(&Opt[2], NULL);
            if (Opts->BriefRecurseDepth == 0) {
                Opts->BriefRecurseDepth = UINT_MAX;
            }
            OptParsed = TRUE;

            if (Opts->SubDirWalk == NULL) {
                if (!SdirSetSubdirWalk(_T("."))) {
                    return FALSE;
                }
            }
        } else if (Opt[1] == 's') {
            YORI_STRING YsSize;
            LARGE_INTEGER FileSize;
            if (Opts->BriefRecurseDepth == 0) {
                Opts->BriefRecurseDepth = UINT_MAX;
            }
            YoriLibConstantString(&YsSize, &Opt[2]);
            FileSize = YoriLibStringToFileSize(&YsSize);
            Opts->BriefRecurseSize = SdirFileSizeFromLargeInt(&FileSize);
            OptParsed = TRUE;

            if (Opts->SubDirWalk == NULL) {
                if (!SdirSetSubdirWalk(_T("."))) {
                    return FALSE;
                }
            }
        }
    } else if (Opt[0] == 'c') {
        if (Opt[1] == 'w') {
            Opts->ConsoleWidth = SdirStringToNum32(&Opt[2], NULL);
            if (Opts->ConsoleWidth > SDIR_MAX_WIDTH) {
                Opts->ConsoleWidth = SDIR_MAX_WIDTH;
            }
            if (!Opts->OutputHasAutoLineWrap) {
                Opts->ConsoleBufferWidth = Opts->ConsoleWidth;
            }
            OptParsed = TRUE;
        }
    } else if (Opt[0] == 'd') {
        for (i = 0; i < SdirGetNumSdirOptions(); i++) {
            if (_tcsicmp(&Opt[1], SdirOptions[i].Switch) == 0 &&
                SdirOptions[i].Default.Flags & SDIR_FEATURE_ALLOW_DISPLAY) {

                PSDIR_FEATURE Feature;

                Feature = SdirFeatureByOptionNumber(i);
                Feature->Flags |= SDIR_FEATURE_DISPLAY;
                OptParsed = TRUE;
                break;
            }
        }
    } else if (Opt[0] == 'h') {
        for (i = 0; i < SdirGetNumSdirOptions(); i++) {
            if (_tcsicmp(&Opt[1], SdirOptions[i].Switch) == 0 &&
                SdirOptions[i].Default.Flags & SDIR_FEATURE_ALLOW_DISPLAY) {

                PSDIR_FEATURE Feature;

                Feature = SdirFeatureByOptionNumber(i);
                Feature->Flags &= ~(SDIR_FEATURE_DISPLAY);
                OptParsed = TRUE;
                break;
            }
        }
    } else if (Opt[0] == 'f') {
        if (Opt[1] == 'c') {
            DWORD Len = (DWORD)_tcslen(&Opt[2]);
            if (Len > 0) {
                Opts->CustomFileColor = YoriLibMalloc((Len + 1) * sizeof(TCHAR));
                if (Opts->CustomFileColor == NULL) {
                    return FALSE;
                }
                YoriLibSPrintfS(Opts->CustomFileColor, Len + 1, _T("%s"), &Opt[2]);
                Opts->CustomFileColorLength = Len;
                OptParsed = TRUE;
            }
        } else if (Opt[1] == 'e') {
            DWORD Len = (DWORD)_tcslen(&Opt[2]);
            if (Len > 0) {
                Opts->CustomFileFilter = YoriLibMalloc((Len + 1) * sizeof(TCHAR));
                if (Opts->CustomFileFilter == NULL) {
                    return FALSE;
                }
                YoriLibSPrintfS(Opts->CustomFileFilter, Len + 1, _T("%s"), &Opt[2]);
                Opts->CustomFileFilterLength = Len;
                OptParsed = TRUE;
            }
        }
    } else if (Opt[0] == 'l') {
        if (Opt[1] == 'n') {
            Opts->TraverseLinks = FALSE;
            OptParsed = TRUE;
        } else if (Opt[1] == '\0') {
            Opts->TraverseLinks = TRUE;
            OptParsed = TRUE;
        }
    } else if (Opt[0] == 's' || Opt[0] == 'i') {

        //
        //  If we don't have space for it, don't even try
        //

        if (Opts->CurrentSort < sizeof(Opts->Sort)/sizeof(Opts->Sort[0])) {

            //
            //  Find which option it is
            //

            for (i = 0; i < SdirGetNumSdirOptions(); i++) {
                if (_tcsicmp(&Opt[1], SdirOptions[i].Switch) == 0 &&
                    SdirOptions[i].Default.Flags & SDIR_FEATURE_ALLOW_SORT) {

                    //
                    //  See if it's been specified before and silently discard
                    //  it.
                    //

                    for (j = 0; j < Opts->CurrentSort; j++) {
                        if (Opts->Sort[j].CompareFn == SdirOptions[i].CompareFn) {
                            OptParsed = TRUE;
                            break;
                        }
                    }

                    if (OptParsed) {
                        break;
                    }

                    Opts->Sort[Opts->CurrentSort].CompareFn = SdirOptions[i].CompareFn;

                    if (Opt[0] == 's') {
                        Opts->Sort[Opts->CurrentSort].CompareBreakCondition = SDIR_GREATER_THAN;
                        Opts->Sort[Opts->CurrentSort].CompareInverseCondition = SDIR_LESS_THAN;
                    } else {
                        Opts->Sort[Opts->CurrentSort].CompareBreakCondition = SDIR_LESS_THAN;
                        Opts->Sort[Opts->CurrentSort].CompareInverseCondition = SDIR_GREATER_THAN;
                    }
                    Opts->CurrentSort++;
                    OptParsed = TRUE;

                    break;
                }
            }

        } else {
        }
    } else if (Opt[0] == 'p') {
        if (Opt[1] == 'n') {
            Opts->EnablePause = FALSE;
            OptParsed = TRUE;
        } else if (Opt[1] == '\0') {
            if (Opts->OutputWithConsoleApi) {
                Opts->EnablePause = TRUE;
            }
            OptParsed = TRUE;
        }
    } else if (Opt[0] == 'r') {

        LPTSTR SubDirPath = &Opt[1];

        OptParsed = TRUE;
        if (SubDirPath[0] == '\0') {
            SubDirPath = _T(".");
        }

        if (!SdirSetSubdirWalk(SubDirPath)) {
            return FALSE;
        }
    } else if (Opt[0] == 't') {
        if (Opt[1] == 'n') {
            Opts->EnableNameTruncation = FALSE;
            OptParsed = TRUE;
        } else if (Opt[1] == '\0') {
            Opts->EnableNameTruncation = TRUE;
            OptParsed = TRUE;
        }
    } else if (Opt[0] == 'u') {
#ifdef UNICODE
        if (Opt[1] == 'n') {
            Opts->OutputExtendedCharacters = FALSE;
            OptParsed = TRUE;
        } else if (Opt[1] == '\0') {
            Opts->OutputExtendedCharacters = TRUE;
            OptParsed = TRUE;
        }
#endif
    }

    return OptParsed;
}

/**
 Parse command line arguments and configure in memory state.

 @param argc Count of arguments.

 @param argv Array of NULL terminated string arguments.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOL
SdirParseArgs (
    __in int argc,
    __in_ecount(argc) LPTSTR argv[]
    )
{
    ULONG  CurrentArg;
    BOOL   OptParsed;
    BOOL   DisplayUsage;

    TCHAR  EnvOpts[200];
    LPTSTR Opt;
    LPTSTR TokContext = NULL;
    DWORD  i;

    //
    //  Default to name sorting.  If something else is specified
    //  we clobber this entry.
    //

    Opts->CurrentSort = 0;
    Opts->Sort[0].CompareFn = SdirCompareFileName;
    Opts->Sort[0].CompareBreakCondition = SDIR_GREATER_THAN;
    Opts->Sort[0].CompareInverseCondition = SDIR_LESS_THAN;

    Opts->EnableNameTruncation = TRUE;

    Opts->SubDirWalk = NULL;

    //
    //  Default to ANSI files
    //

    if (!Opts->OutputWithConsoleApi) {
        Opts->OutputExtendedCharacters = FALSE;
    } else {
        Opts->OutputExtendedCharacters = TRUE;
    }

    for (i = 0; i < SdirGetNumSdirOptions(); i++) {
        PSDIR_FEATURE Feature;

        Feature = SdirFeatureByOptionNumber(i);
        Feature->Flags = SdirOptions[i].Default.Flags;
        Feature->HighlightColor = YoriLibResolveWindowColorComponents(SdirOptions[i].Default.HighlightColor, Opts->PreviousAttributes, TRUE);
    }

    if (GetEnvironmentVariable(_T("SDIR_OPTS"), EnvOpts, sizeof(EnvOpts)/sizeof(EnvOpts[0]))) {
        EnvOpts[sizeof(EnvOpts)/sizeof(EnvOpts[0]) - 1] = '\0';
        Opt = _tcstok_s(EnvOpts, _T(" "), &TokContext);

        while (Opt) {

            if (YoriLibIsCommandLineOptionChar(Opt[0])) {

                Opt++;

                OptParsed = FALSE;

                OptParsed = SdirParseOpt(Opt);

                if (!OptParsed) {
                    SdirWriteString(_T("Unknown environment option: "));
                    SdirWriteString(Opt);
                    SdirWriteString(_T("\n"));
                    SdirUsage(argc, argv);
                    return FALSE;
                }
            }

            Opt = _tcstok_s(NULL, _T(" "), &TokContext);
        }
    }

    for (CurrentArg = 1; CurrentArg < (ULONG)argc; CurrentArg++) {

        if (YoriLibIsCommandLineOptionChar(argv[CurrentArg][0])) {
            Opt = &argv[CurrentArg][1];

            OptParsed = FALSE;
            DisplayUsage = FALSE;

            OptParsed = SdirParseOpt(Opt);

            if (_tcsicmp(Opt, _T("help")) == 0) {
                DisplayUsage = TRUE;
                OptParsed = TRUE;
            } else if (_tcsicmp(Opt, _T("?")) == 0) {
                DisplayUsage = TRUE;
                OptParsed = TRUE;
            } else if (_tcsicmp(Opt, _T("v")) == 0) {
                DisplayUsage = TRUE;
                OptParsed = TRUE;
            }

            if (DisplayUsage || !OptParsed) {
                if (!OptParsed) {
                    SdirWriteString(_T("Unknown argument: "));
                    SdirWriteString(argv[CurrentArg]);
                    SdirWriteString(_T("\n"));
                }
                SdirUsage(argc, argv);
                return FALSE;
            }
        }
    }

    return TRUE;
}

/**
 Initialize the application, parsing all arguments and configuring global
 state ready for execution.

 @param argc Count of arguments.

 @param argv Array of NULL terminated string arguments.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOL
SdirInit(
    __in int argc,
    __in_ecount(argc) LPTSTR argv[]
    )
{
    if (!YoriLibLoadKernel32Functions()) {
        return FALSE;
    }

    if (!SdirAppInitialize()) {
        return FALSE;
    }

    if (!SdirParseArgs(argc, argv)) {
        return FALSE;
    }

    if (!SdirOptInitialize()) {
        return FALSE;
    }

    if (!SdirParseAttributeApplyString()) {
        return FALSE;
    }

    if (!SdirParseMetadataAttributeString()) {
        return FALSE;
    }

    return TRUE;
}

// vim:sw=4:ts=4:et:
