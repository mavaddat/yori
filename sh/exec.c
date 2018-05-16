/**
 * @file sh/exec.c
 *
 * Yori shell execute external program
 *
 * Copyright (c) 2017 Malcolm J. Smith
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

#include "yori.h"

/**
 Capture the current handles used for stdin/stdout/stderr.

 @param RedirectContext Pointer to the block to capture current state into.
 */
VOID
YoriShCaptureRedirectContext(
    __out PYORI_PREVIOUS_REDIRECT_CONTEXT RedirectContext
    )
{
    RedirectContext->ResetInput = FALSE;
    RedirectContext->ResetOutput = FALSE;
    RedirectContext->ResetError = FALSE;
    RedirectContext->StdErrAndOutSame = FALSE;

    //
    //  Always duplicate as noninherited.  If we don't override one of the
    //  existing stdin/stdout/stderr values then the original inheritance
    //  is still in effect.  These are only used to restore our process.
    //

    RedirectContext->StdInput = GetStdHandle(STD_INPUT_HANDLE);
    RedirectContext->StdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    RedirectContext->StdError = GetStdHandle(STD_ERROR_HANDLE);
}

/**
 Revert the redirection context previously put in place by a call to
 @ref YoriShInitializeRedirection.

 @param PreviousRedirectContext The context to revert to.
 */
VOID
YoriShRevertRedirection(
    __in PYORI_PREVIOUS_REDIRECT_CONTEXT PreviousRedirectContext
    )
{
    YORI_PREVIOUS_REDIRECT_CONTEXT CurrentRedirectContext;

    SetConsoleCtrlHandler(NULL, TRUE);

    YoriShCaptureRedirectContext(&CurrentRedirectContext);

    if (PreviousRedirectContext->ResetInput) {
        SetStdHandle(STD_INPUT_HANDLE, PreviousRedirectContext->StdInput);
        CloseHandle(CurrentRedirectContext.StdInput);
    }

    if (PreviousRedirectContext->ResetOutput) {
        SetStdHandle(STD_OUTPUT_HANDLE, PreviousRedirectContext->StdOutput);
        CloseHandle(CurrentRedirectContext.StdOutput);
    }

    if (PreviousRedirectContext->ResetError) {
        SetStdHandle(STD_ERROR_HANDLE, PreviousRedirectContext->StdError);
        if (!PreviousRedirectContext->StdErrAndOutSame) {
            CloseHandle(CurrentRedirectContext.StdError);
        }
    }
}

/**
 Temporarily set this process to have the same stdin/stdout/stderr as a
 a program that it intends to launch.  Keep information about the current
 stdin/stdout/stderr in PreviousRedirectContext so that it can be restored
 with a later call to @ref YoriShRevertRedirection.

 @param ExecContext The context of the program whose stdin/stdout/stderr
        should be initialized.

 @param PrepareForBuiltIn If TRUE, leave Ctrl+C handling suppressed.  The
        builtin program is free to enable it if it has long running
        processing to perform, and it can install its own handler.

 @param PreviousRedirectContext On successful completion, this is populated
        with the current stdin/stdout/stderr information so that it can
        be restored later.

 @return TRUE to indicate that redirection has been completely initialized,
         FALSE if an error has occurred.
 */
BOOL
YoriShInitializeRedirection(
    __in PYORI_SINGLE_EXEC_CONTEXT ExecContext,
    __in BOOL PrepareForBuiltIn,
    __out PYORI_PREVIOUS_REDIRECT_CONTEXT PreviousRedirectContext
    )
{
    SECURITY_ATTRIBUTES InheritHandle;
    HANDLE Handle;

    ZeroMemory(&InheritHandle, sizeof(InheritHandle));
    InheritHandle.nLength = sizeof(InheritHandle);
    InheritHandle.bInheritHandle = TRUE;

    YoriShCaptureRedirectContext(PreviousRedirectContext);

    if (!PrepareForBuiltIn) {
        SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);
        SetConsoleCtrlHandler(NULL, FALSE);
    }

    //
    //  MSFIX Abort on failure? Should be able to just revert and return
    //

    if (ExecContext->StdInType == StdInTypeFile) {
        Handle = CreateFile(ExecContext->StdIn.File.FileName.StartOfString,
                            GENERIC_READ,
                            FILE_SHARE_DELETE | FILE_SHARE_READ,
                            &InheritHandle,
                            OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL,
                            NULL);

        if (Handle != INVALID_HANDLE_VALUE) {
            PreviousRedirectContext->ResetInput = TRUE;
            SetStdHandle(STD_INPUT_HANDLE, Handle);
        }
    } else if (ExecContext->StdInType == StdInTypeNull) {
        Handle = CreateFile(_T("NUL"),
                            GENERIC_READ,
                            FILE_SHARE_DELETE | FILE_SHARE_READ,
                            &InheritHandle,
                            OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL,
                            NULL);

        if (Handle != INVALID_HANDLE_VALUE) {
            PreviousRedirectContext->ResetInput = TRUE;
            SetStdHandle(STD_INPUT_HANDLE, Handle);
        }
    } else if (ExecContext->StdInType == StdInTypePipe) {
        if (ExecContext->StdIn.Pipe.PipeFromPriorProcess != NULL) {

            YoriLibMakeInheritableHandle(ExecContext->StdIn.Pipe.PipeFromPriorProcess,
                                         &ExecContext->StdIn.Pipe.PipeFromPriorProcess);

            PreviousRedirectContext->ResetInput = TRUE;
            SetStdHandle(STD_INPUT_HANDLE, ExecContext->StdIn.Pipe.PipeFromPriorProcess);
            ExecContext->StdIn.Pipe.PipeFromPriorProcess = NULL;
        }
    }


    if (ExecContext->StdOutType == StdOutTypeOverwrite) {
        Handle = CreateFile(ExecContext->StdOut.Overwrite.FileName.StartOfString,
                            GENERIC_WRITE,
                            FILE_SHARE_DELETE | FILE_SHARE_READ,
                            &InheritHandle,
                            CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL,
                            NULL);

        if (Handle != INVALID_HANDLE_VALUE) {
            PreviousRedirectContext->ResetOutput = TRUE;
            SetStdHandle(STD_OUTPUT_HANDLE, Handle);
        }

    } else if (ExecContext->StdOutType == StdOutTypeAppend) {
        Handle = CreateFile(ExecContext->StdOut.Append.FileName.StartOfString,
                            FILE_APPEND_DATA|SYNCHRONIZE,
                            FILE_SHARE_DELETE | FILE_SHARE_READ,
                            &InheritHandle,
                            OPEN_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL,
                            NULL);

        if (Handle != INVALID_HANDLE_VALUE) {
            PreviousRedirectContext->ResetOutput = TRUE;
            SetStdHandle(STD_OUTPUT_HANDLE, Handle);
        }
    } else if (ExecContext->StdOutType == StdOutTypeNull) {
        Handle = CreateFile(_T("NUL"),
                            GENERIC_WRITE,
                            FILE_SHARE_DELETE | FILE_SHARE_READ,
                            &InheritHandle,
                            CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL,
                            NULL);

        if (Handle != INVALID_HANDLE_VALUE) {
            PreviousRedirectContext->ResetOutput = TRUE;
            SetStdHandle(STD_OUTPUT_HANDLE, Handle);
        }
    } else if (ExecContext->StdOutType == StdOutTypePipe) {
        HANDLE ReadHandle;
        HANDLE WriteHandle;
        if (ExecContext->NextProgram != NULL &&
            ExecContext->NextProgram->StdInType == StdInTypePipe &&
            CreatePipe(&ReadHandle, &WriteHandle, NULL, 0)) {

            YoriLibMakeInheritableHandle(WriteHandle, &WriteHandle);

            PreviousRedirectContext->ResetOutput = TRUE;
            SetStdHandle(STD_OUTPUT_HANDLE, WriteHandle);
            ExecContext->NextProgram->StdIn.Pipe.PipeFromPriorProcess = ReadHandle;
        }
    } else if (ExecContext->StdOutType == StdOutTypeBuffer) {
        HANDLE ReadHandle;
        HANDLE WriteHandle;
        if (CreatePipe(&ReadHandle, &WriteHandle, NULL, 0)) {

            YoriLibMakeInheritableHandle(WriteHandle, &WriteHandle);

            PreviousRedirectContext->ResetOutput = TRUE;
            SetStdHandle(STD_OUTPUT_HANDLE, WriteHandle);
            ExecContext->StdOut.Buffer.PipeFromProcess = ReadHandle;
        }
    }


    if (ExecContext->StdErrType == StdErrTypeOverwrite) {
        Handle = CreateFile(ExecContext->StdErr.Overwrite.FileName.StartOfString,
                            GENERIC_WRITE,
                            FILE_SHARE_DELETE | FILE_SHARE_READ,
                            &InheritHandle,
                            CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL,
                            NULL);

        if (Handle != INVALID_HANDLE_VALUE) {
            PreviousRedirectContext->ResetError = TRUE;
            SetStdHandle(STD_ERROR_HANDLE, Handle);
        }

    } else if (ExecContext->StdErrType == StdErrTypeAppend) {
        Handle = CreateFile(ExecContext->StdErr.Append.FileName.StartOfString,
                            FILE_APPEND_DATA|SYNCHRONIZE,
                            FILE_SHARE_DELETE | FILE_SHARE_READ,
                            &InheritHandle,
                            OPEN_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL,
                            NULL);

        if (Handle != INVALID_HANDLE_VALUE) {
            PreviousRedirectContext->ResetError = TRUE;
            SetStdHandle(STD_ERROR_HANDLE, Handle);
        }
    } else if (ExecContext->StdErrType == StdErrTypeStdOut) {
        PreviousRedirectContext->ResetError = TRUE;
        PreviousRedirectContext->StdErrAndOutSame = TRUE;
        SetStdHandle(STD_ERROR_HANDLE, GetStdHandle(STD_OUTPUT_HANDLE));
    } else if (ExecContext->StdErrType == StdErrTypeNull) {
        Handle = CreateFile(_T("NUL"),
                            GENERIC_WRITE,
                            FILE_SHARE_DELETE | FILE_SHARE_READ,
                            &InheritHandle,
                            CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL,
                            NULL);

        if (Handle != INVALID_HANDLE_VALUE) {
            PreviousRedirectContext->ResetError = TRUE;
            SetStdHandle(STD_ERROR_HANDLE, Handle);
        }
    } else if (ExecContext->StdErrType == StdErrTypeBuffer) {
        HANDLE ReadHandle;
        HANDLE WriteHandle;
        if (CreatePipe(&ReadHandle, &WriteHandle, NULL, 0)) {

            YoriLibMakeInheritableHandle(WriteHandle, &WriteHandle);

            PreviousRedirectContext->ResetError = TRUE;
            SetStdHandle(STD_ERROR_HANDLE, WriteHandle);
            ExecContext->StdErr.Buffer.PipeFromProcess = ReadHandle;
        }
    }

    return TRUE;
}


/**
 Wait for a process to terminate.  This is also a good opportunity for Yori
 to monitor for keyboard input that may be better handled by Yori than the
 foreground process.

 @param ExecContext Pointer to the context used to invoke the process, which
        includes information about whether it should be cancelled.

 @param hProcess The process to wait for.

 @param dwProcessId The process ID, which for some unknowable reason is used
        rather than the handle to send Ctrl+C signals to.
 */
VOID
YoriShWaitForProcessToTerminate(
    __in PYORI_SINGLE_EXEC_CONTEXT ExecContext,
    __in HANDLE hProcess,
    __in DWORD dwProcessId
    )
{
    HANDLE WaitOn[3];
    DWORD Result;
    PINPUT_RECORD InputRecords = NULL;
    DWORD RecordsNeeded;
    DWORD RecordsAllocated = 0;
    DWORD RecordsRead;
    DWORD Count;
    DWORD CtrlBCount = 0;
    DWORD Delay;
    BOOL CtrlBFoundThisPass;

    //
    //  By this point redirection has been established and then reverted.
    //  This should be dealing with the original input handle.
    //

    YoriLibCancelEnable();

    WaitOn[0] = hProcess;
    WaitOn[1] = YoriLibCancelGetEvent();
    WaitOn[2] = GetStdHandle(STD_INPUT_HANDLE);

    Delay = INFINITE;

    while (TRUE) {
        if (Delay == INFINITE) {
            Result = WaitForMultipleObjects(3, WaitOn, FALSE, Delay);
        } else {
            Result = WaitForMultipleObjects(2, WaitOn, FALSE, Delay);
        }
        if (Result == WAIT_OBJECT_0) {
            
            //
            //  Once the process has completed, if it's outputting to
            //  buffers, wait for the buffers to contain final data.
            //

            if (ExecContext->StdOutType == StdOutTypeBuffer &&
                ExecContext->StdOut.Buffer.ProcessBuffers != NULL)  {

                YoriShWaitForProcessBufferToFinalize(ExecContext->StdOut.Buffer.ProcessBuffers);
            }

            if (ExecContext->StdErrType == StdErrTypeBuffer &&
                ExecContext->StdErr.Buffer.ProcessBuffers != NULL) {

                YoriShWaitForProcessBufferToFinalize(ExecContext->StdErr.Buffer.ProcessBuffers);
            }
            break;
        }

        //
        //  If the user has hit Ctrl+C or Ctrl+Break, request the process
        //  to clean up gracefully and unwind.  Later on we'll try to kill
        //  all processes in the exec plan, so we don't need to try too hard
        //  at this point.
        //

        if (Result == (WAIT_OBJECT_0 + 1)) {
            GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, dwProcessId);
            break;
        }

        if (WaitForSingleObject(WaitOn[2], 0) == WAIT_TIMEOUT) {
            CtrlBCount = 0;
            Delay = INFINITE;
            continue;
        }

        //
        //  Check if there's pending input.  If there is, go have a look.
        //

        GetNumberOfConsoleInputEvents(GetStdHandle(STD_INPUT_HANDLE), &RecordsNeeded);

        if (RecordsNeeded > RecordsAllocated) {
            if (InputRecords != NULL) {
                YoriLibFree(InputRecords);
            }

            //
            //  Since the user is only ever adding input, overallocate to see
            //  if we can avoid a few allocations later.
            //

            RecordsNeeded += 10;

            InputRecords = YoriLibMalloc(RecordsNeeded * sizeof(INPUT_RECORD));
            if (InputRecords == NULL) {
                Sleep(50);
                continue;
            }

            RecordsAllocated = RecordsNeeded;
        }

        //
        //  Conceptually, the user is interacting with another process, so only
        //  peek at the input and try to leave it alone.  If we see a Ctrl+B,
        //  and the foreground process isn't paying any attention and leaves it
        //  in the input buffer for three passes, we may as well assume it was
        //  for us.
        //
        //  Leave all the input in the buffer so we can catch it later.
        //

        if (PeekConsoleInput(GetStdHandle(STD_INPUT_HANDLE), InputRecords, RecordsAllocated, &RecordsRead) && RecordsRead > 0) {

            CtrlBFoundThisPass = FALSE;

            for (Count = 0; Count < RecordsRead; Count++) {

                if (InputRecords[Count].EventType == KEY_EVENT &&
                    InputRecords[Count].Event.KeyEvent.bKeyDown &&
                    InputRecords[Count].Event.KeyEvent.wVirtualKeyCode == 'B') {

                    DWORD CtrlMask;
                    CtrlMask = InputRecords[Count].Event.KeyEvent.dwControlKeyState & (RIGHT_ALT_PRESSED | LEFT_ALT_PRESSED | RIGHT_CTRL_PRESSED | LEFT_CTRL_PRESSED);
                    if (CtrlMask == RIGHT_CTRL_PRESSED || CtrlMask == LEFT_CTRL_PRESSED) {
                        CtrlBFoundThisPass = TRUE;
                        break;
                    }
                }
            }

            if (CtrlBFoundThisPass) {
                if (CtrlBCount < 3) {
                    CtrlBCount++;
                    Delay = 30;
                    continue;
                }
                break;
            } else {
                CtrlBCount = 0;
            }

            Delay = 100;
        }
    }

    if (InputRecords != NULL) {
        YoriLibFree(InputRecords);
    }

    YoriLibCancelIgnore();
}

/**
 Try to launch a single program via ShellExecuteEx rather than CreateProcess.
 This is used when CreateProcess said elevation is needed, and that requires
 UI, so we need to call a function that can support UI...except since Win7
 we're unlikely to actually have any UI because the system components are
 capable of auto-elevating.  But, at least command prompts get to instantiate
 COM, because that's sure going to end well.

 @param ExecContext Pointer to the program to execute.

 @param ProcessInfo On successful completion, the process handle might be
        populated here.  It might not, because shell could decide to
        communicate with an existing process via DDE, and it doesn't tell us
        about the process it was talking to in that case.  In fairness, we
        probably shouldn't wait on a process that we didn't launch.
 
 @return TRUE to indicate success, FALSE on failure.
 */
BOOL
YoriShExecViaShellExecute(
    __in PYORI_SINGLE_EXEC_CONTEXT ExecContext,
    __out PPROCESS_INFORMATION ProcessInfo
    )
{
    LPTSTR Args = NULL;
    YORI_CMD_CONTEXT ArgContext;
    YORI_SHELLEXECUTEINFO sei;
    YORI_PREVIOUS_REDIRECT_CONTEXT PreviousRedirectContext;

    YoriLibLoadShell32Functions();

    //
    //  This really shouldn't fail.  We're only here because a process
    //  launch requires elevation, and if UAC is there, ShellExecuteEx had
    //  better be there too.
    //

    ASSERT(Shell32.pShellExecuteExW != NULL);
    if (Shell32.pShellExecuteExW == NULL) {
        return FALSE;
    }

    ZeroMemory(&sei, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS|SEE_MASK_FLAG_NO_UI|SEE_MASK_NOZONECHECKS|SEE_MASK_UNICODE;
    sei.lpFile = ExecContext->CmdToExec.ArgV[0].StartOfString;
    if (ExecContext->CmdToExec.ArgC > 1) {
        memcpy(&ArgContext, &ExecContext->CmdToExec, sizeof(YORI_CMD_CONTEXT));
        ArgContext.ArgC--;
        ArgContext.ArgV = &ArgContext.ArgV[1];
        ArgContext.ArgContexts = &ArgContext.ArgContexts[1];
        Args = YoriShBuildCmdlineFromCmdContext(&ArgContext, TRUE, NULL, NULL);
    }

    sei.lpParameters = Args;
    sei.nShow = SW_SHOWNORMAL;

    ZeroMemory(ProcessInfo, sizeof(PROCESS_INFORMATION));

    YoriShInitializeRedirection(ExecContext, FALSE, &PreviousRedirectContext);
    if (!Shell32.pShellExecuteExW(&sei)) {
        DWORD LastError = GetLastError();
        LPTSTR ErrText = YoriLibGetWinErrorText(LastError);
        YoriShRevertRedirection(&PreviousRedirectContext);
        if (Args != NULL) {
            YoriLibDereference(Args);
        }
        YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("ShellExecuteEx failed (%i): %s"), LastError, ErrText);
        YoriLibFreeWinErrorText(ErrText);
        return FALSE;
    }
    YoriShRevertRedirection(&PreviousRedirectContext);
    if (Args != NULL) {
        YoriLibDereference(Args);
    }

    ProcessInfo->hProcess = sei.hProcess;
    return TRUE;
}

/**
 Execute a single program.  If the execution is synchronous, this routine will
 wait for the program to complete and return its exit code.  If the execution
 is not synchronous, this routine will return without waiting and provide zero
 as a (not meaningful) exit code.

 @param ExecContext The context of a single program to execute.

 @return The exit code of the program, when executed synchronously.
 */
DWORD
YoriShExecuteSingleProgram(
    __in PYORI_SINGLE_EXEC_CONTEXT ExecContext
    )
{
    LPTSTR CmdLine;
    PROCESS_INFORMATION ProcessInfo;
    STARTUPINFO StartupInfo;
    YORI_PREVIOUS_REDIRECT_CONTEXT PreviousRedirectContext;
    DWORD ExitCode = 0;
    DWORD CreationFlags = 0;
    LPTSTR szExt;
    BOOLEAN ExecProcess = TRUE;
    BOOLEAN LaunchFailed = FALSE;
    BOOLEAN LaunchViaShellExecute = FALSE;

    szExt = YoriLibFindRightMostCharacter(&ExecContext->CmdToExec.ArgV[0], '.');
    if (szExt != NULL) {
        YORI_STRING YsExt;

        YoriLibInitEmptyString(&YsExt);
        YsExt.StartOfString = szExt;
        YsExt.LengthInChars = ExecContext->CmdToExec.ArgV[0].LengthInChars - (DWORD)(szExt - ExecContext->CmdToExec.ArgV[0].StartOfString);

        if (YoriLibCompareStringWithLiteralInsensitive(&YsExt, _T(".com")) == 0) {
            if (YoriShExecuteNamedModuleInProc(ExecContext->CmdToExec.ArgV[0].StartOfString, ExecContext, &ExitCode)) {
                ExecProcess = FALSE;
            }
        } else if (YoriLibCompareStringWithLiteralInsensitive(&YsExt, _T(".ys1")) == 0) {
            ExecProcess = FALSE;
            ExitCode = YoriShBuckPass(ExecContext, 1, _T("ys"));
        } else if (YoriLibCompareStringWithLiteralInsensitive(&YsExt, _T(".cmd")) == 0 ||
                   YoriLibCompareStringWithLiteralInsensitive(&YsExt, _T(".bat")) == 0) {
            ExecProcess = FALSE;
            YoriShCheckIfArgNeedsQuotes(&ExecContext->CmdToExec, 0);
            ExitCode = YoriShBuckPass(ExecContext, 2, _T("cmd.exe"), _T("/c"));
        } else if (YoriLibCompareStringWithLiteralInsensitive(&YsExt, _T(".exe")) != 0) {
            LaunchViaShellExecute = TRUE;
        }
    }


    if (ExecProcess) {

        ZeroMemory(&ProcessInfo, sizeof(ProcessInfo));

        if (!LaunchViaShellExecute) {
            CmdLine = YoriShBuildCmdlineFromCmdContext(&ExecContext->CmdToExec, TRUE, NULL, NULL);
            if (CmdLine == NULL) {
                return 1;
            }
    
            memset(&StartupInfo, 0, sizeof(StartupInfo));
            StartupInfo.cb = sizeof(StartupInfo);
    
            if (ExecContext->RunOnSecondConsole) {
                CreationFlags |= CREATE_NEW_CONSOLE;
            }
            CreationFlags |= CREATE_NEW_PROCESS_GROUP;
    
            YoriShInitializeRedirection(ExecContext, FALSE, &PreviousRedirectContext);
    
            if (!CreateProcess(NULL, CmdLine, NULL, NULL, TRUE, CreationFlags, NULL, NULL, &StartupInfo, &ProcessInfo)) {
                DWORD LastError = GetLastError();
                YoriShRevertRedirection(&PreviousRedirectContext);
                YoriLibDereference(CmdLine);
                if (LastError == ERROR_ELEVATION_REQUIRED) {
                    LaunchViaShellExecute = TRUE;
                } else {
                    LPTSTR ErrText = YoriLibGetWinErrorText(LastError);
                    YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("CreateProcess failed (%i): %s"), LastError, ErrText);
                    YoriLibFreeWinErrorText(ErrText);
                    LaunchFailed = TRUE;
                }
            } else {
                YoriShRevertRedirection(&PreviousRedirectContext);
                YoriLibDereference(CmdLine);
            }
        }

        if (LaunchViaShellExecute) {
            if (!YoriShExecViaShellExecute(ExecContext, &ProcessInfo)) {
                LaunchFailed = TRUE;
            }
        }

        if (LaunchFailed) {
            if (ExecContext->StdOutType == StdOutTypePipe &&
                ExecContext->NextProgram != NULL &&
                ExecContext->NextProgram->StdInType == StdInTypePipe) {

                CloseHandle(ExecContext->NextProgram->StdIn.Pipe.PipeFromPriorProcess);
                ExecContext->NextProgram->StdIn.Pipe.PipeFromPriorProcess = NULL;
                ExecContext->NextProgramType = NextProgramExecNever;
            }

            return 1;
        }

        //
        //  If we're buffering output, start that process now.  If it succeeds,
        //  the pipe is owned by the buffer pump and shouldn't be torn down
        //  when the ExecContext is.
        //

        if (ExecContext->StdOutType == StdOutTypeBuffer ||
            ExecContext->StdErrType == StdErrTypeBuffer) {

            if (ExecContext->StdOut.Buffer.ProcessBuffers != NULL) {
                ASSERT(ExecContext->StdErrType != StdErrTypeBuffer);
                if (YoriShAppendToExistingProcessBuffer(ExecContext)) {
                    ExecContext->StdOut.Buffer.PipeFromProcess = NULL;
                } else {
                    ExecContext->StdOut.Buffer.ProcessBuffers = NULL;
                }
            } else {
                if (YoriShCreateNewProcessBuffer(ExecContext)) {
                    ExecContext->StdOut.Buffer.PipeFromProcess = NULL;
                    ExecContext->StdErr.Buffer.PipeFromProcess = NULL;
                }
            }
        }

        //
        //  We may not have a process handle but still be successful if
        //  ShellExecute decided to interact with an existing process
        //  rather than launch a new one.  This isn't going to be very
        //  common in any interactive shell, and it's clearly going to break
        //  things, but there's not much we can do about it from here.
        //

        if (ProcessInfo.hProcess != NULL) {
            ASSERT(ExecContext->hProcess == NULL);
            ExecContext->hProcess = ProcessInfo.hProcess;
            ExecContext->dwProcessId = ProcessInfo.dwProcessId;
            if (ExecContext->WaitForCompletion) {
                YoriShWaitForProcessToTerminate(ExecContext, ProcessInfo.hProcess, ProcessInfo.dwProcessId);
                GetExitCodeProcess(ProcessInfo.hProcess, &ExitCode);
            } else if (ExecContext->StdOutType != StdOutTypePipe) {
                if (YoriShCreateNewJob(ExecContext, ProcessInfo.hProcess, ProcessInfo.dwProcessId)) {
                    ExecContext->dwProcessId = 0;
                    ExecContext->hProcess = NULL;
                }
            }
        }

        if (ProcessInfo.hThread != NULL) {
            CloseHandle(ProcessInfo.hThread);
        }
    }
    return ExitCode;
}

/**
 Cancel an exec plan.  This is invoked after the user hits Ctrl+C and attempts
 to terminate all outstanding processes associated with the request.

 @param ExecPlan The plan to terminate all outstanding processes in.
 */
VOID
YoriShCancelExecPlan(
    __in PYORI_EXEC_PLAN ExecPlan
    )
{
    PYORI_SINGLE_EXEC_CONTEXT ExecContext;

    //
    //  Loop and ask the processes nicely to terminate.
    //

    ExecContext = ExecPlan->FirstCmd;
    while (ExecContext != NULL) {
        if (ExecContext->hProcess != NULL) {
            if (WaitForSingleObject(ExecContext->hProcess, 0) != WAIT_OBJECT_0) {
                if (ExecContext->dwProcessId != 0) {
                    GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, ExecContext->dwProcessId);
                }
            }
        }
        ExecContext = ExecContext->NextProgram;
    }

    Sleep(50);

    //
    //  Loop again and ask the processes less nicely to terminate.
    //

    ExecContext = ExecPlan->FirstCmd;
    while (ExecContext != NULL) {
        if (ExecContext->hProcess != NULL) {
            if (WaitForSingleObject(ExecContext->hProcess, 0) != WAIT_OBJECT_0) {
                TerminateProcess(ExecContext->hProcess, 1);
            }
        }
        ExecContext = ExecContext->NextProgram;
    }
}


/**
 Execute an exec plan.  An exec plan has multiple processes, including
 different pipe and redirection operators.  Optionally return the result
 of any output buffered processes in the plan, to facilitate back quotes.

 @param ExecPlan Pointer to the exec plan to execute.

 @param OutputBuffer On successful completion, updated to point to the
        resulting output buffer.
 */
VOID
YoriShExecExecPlan(
    __in PYORI_EXEC_PLAN ExecPlan,
    __out_opt PVOID * OutputBuffer
    )
{
    PYORI_SINGLE_EXEC_CONTEXT ExecContext;
    PVOID PreviouslyObservedOutputBuffer = NULL;
    BOOL ExecutableFound;

    ExecContext = ExecPlan->FirstCmd;
    while (ExecContext != NULL) {


        //
        //  If some previous program in the plan has output to a buffer, use
        //  the same buffer for any later program which intends to output to
        //  a buffer.
        //

        if (ExecContext->StdOutType == StdOutTypeBuffer &&
            ExecContext->WaitForCompletion) {

            ExecContext->StdOut.Buffer.ProcessBuffers = PreviouslyObservedOutputBuffer;
        }
    
        if (YoriLibIsOperationCancelled()) {
            break;
        }

        if (!YoriShResolveCommandToExecutable(&ExecContext->CmdToExec, &ExecutableFound)) {
            break;
        }

        if (ExecutableFound) {
            g_ErrorLevel = YoriShExecuteSingleProgram(ExecContext);
        } else {
            g_ErrorLevel = YoriShBuiltIn(ExecContext);
        }

        //
        //  If the program output back to a shell owned buffer and we waited
        //  for it to complete, we can use the same buffer for later commands
        //  in the set.
        //

        if (ExecContext->StdOutType == StdOutTypeBuffer &&
            ExecContext->StdOut.Buffer.ProcessBuffers != NULL &&
            ExecContext->WaitForCompletion) {

            PreviouslyObservedOutputBuffer = ExecContext->StdOut.Buffer.ProcessBuffers;
        }

        //
        //  Determine which program to execute next, if any.
        //

        if (ExecContext->NextProgram != NULL) {
            switch(ExecContext->NextProgramType) {
                case NextProgramExecUnconditionally:
                    ExecContext = ExecContext->NextProgram;
                    break;
                case NextProgramExecConcurrently:
                    ExecContext = ExecContext->NextProgram;
                    break;
                case NextProgramExecOnFailure:
                    if (g_ErrorLevel != 0) {
                        ExecContext = ExecContext->NextProgram;
                    } else {
                        do {
                            ExecContext = ExecContext->NextProgram;
                        } while (ExecContext != NULL && (ExecContext->NextProgramType == NextProgramExecOnFailure || ExecContext->NextProgramType == NextProgramExecConcurrently));
                        if (ExecContext != NULL) {
                            ExecContext = ExecContext->NextProgram;
                        }
                    }
                    break;
                case NextProgramExecOnSuccess:
                    if (g_ErrorLevel == 0) {
                        ExecContext = ExecContext->NextProgram;
                    } else {
                        do {
                            ExecContext = ExecContext->NextProgram;
                        } while (ExecContext != NULL && (ExecContext->NextProgramType == NextProgramExecOnSuccess || ExecContext->NextProgramType == NextProgramExecConcurrently));
                        if (ExecContext != NULL) {
                            ExecContext = ExecContext->NextProgram;
                        }
                    }
                    break;
                case NextProgramExecNever:
                    ExecContext = NULL;
                    break;
                default:
                    ASSERT(!"YoriShParseCmdContextToExecPlan created a plan that YoriShExecExecPlan doesn't know how to execute");
                    ExecContext = NULL;
                    break;
            }
        } else {
            ExecContext = NULL;
        }
    }

    if (OutputBuffer != NULL) {
        *OutputBuffer = PreviouslyObservedOutputBuffer;
    }

    if (YoriLibIsOperationCancelled()) {
        YoriShCancelExecPlan(ExecPlan);
    }
}

/**
 Execute an expression and capture the output of the entire expression into
 a buffer.  This is used when evaluating backquoted expressions.

 @param Expression Pointer to a string describing the expression to execute.

 @param ProcessOutput On successful completion, populated with the result of
        the expression.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOL
YoriShExecuteExpressionAndCaptureOutput(
    __in PYORI_STRING Expression,
    __out PYORI_STRING ProcessOutput
    )
{
    YORI_EXEC_PLAN ExecPlan;
    PYORI_SINGLE_EXEC_CONTEXT ExecContext;
    YORI_CMD_CONTEXT CmdContext;
    PVOID OutputBuffer;

    //
    //  Parse the expression we're trying to execute.
    //

    if (!YoriShParseCmdlineToCmdContext(Expression, 0, &CmdContext)) {
        YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("Parse error\n"));
        return FALSE;
    }

    if (CmdContext.ArgC == 0) {
        YoriShFreeCmdContext(&CmdContext);
        return FALSE;
    }

    if (!YoriShParseCmdContextToExecPlan(&CmdContext, &ExecPlan, NULL, NULL, NULL)) {
        YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("Parse error\n"));
        YoriShFreeCmdContext(&CmdContext);
        return FALSE;
    }

    //
    //  If we're doing backquote evaluation, set the output back to a 
    //  shell owned buffer, and the process must wait.
    //

    ExecContext = ExecPlan.FirstCmd;
    while (ExecContext != NULL) {

        if (ExecContext->StdOutType == StdOutTypeDefault) {
            ExecContext->StdOutType = StdOutTypeBuffer;

            if (!ExecContext->WaitForCompletion &&
                ExecContext->NextProgramType != NextProgramExecUnconditionally) {

                ExecContext->WaitForCompletion = TRUE;
            }
        }
    
        ExecContext = ExecContext->NextProgram;
    }

    YoriShExecExecPlan(&ExecPlan, &OutputBuffer);

    YoriLibInitEmptyString(ProcessOutput);
    if (OutputBuffer != NULL) {

        YoriShGetProcessOutputBuffer(OutputBuffer, ProcessOutput);

        //
        //  Truncate any newlines from the output, which tools
        //  frequently emit but are of no value here
        //

        while (ProcessOutput->LengthInChars > 0 &&
               (ProcessOutput->StartOfString[ProcessOutput->LengthInChars - 1] == '\n' ||
                ProcessOutput->StartOfString[ProcessOutput->LengthInChars - 1] == '\r')) {

            ProcessOutput->LengthInChars--;
        }
    }

    YoriShFreeExecPlan(&ExecPlan);
    YoriShFreeCmdContext(&CmdContext);

    return TRUE;
}


/**
 Parse and execute all backquotes in an expression, potentially resulting
 in a new expression.  This will internally perform parsing and redirection,
 as well as execute multiple subprocesses as needed.

 @param Expression The string to execute.

 @param ResultingExpression On successful completion, updated to contain
        the final expression to evaluate.  This may be the same as Expression
        if no backquote expansion occurred.

 @return TRUE to indicate it was successfully executed, FALSE otherwise.
 */
BOOL
YoriShExpandBackquotes(
    __in PYORI_STRING Expression,
    __out PYORI_STRING ResultingExpression
    )
{
    YORI_STRING CurrentFullExpression;
    YORI_STRING CurrentExpressionSubset;

    YORI_STRING ProcessOutput;

    YORI_STRING InitialPortion;
    YORI_STRING TrailingPortion;
    YORI_STRING NewFullExpression;

    YoriLibInitEmptyString(&CurrentFullExpression);
    CurrentFullExpression.StartOfString = Expression->StartOfString;
    CurrentFullExpression.LengthInChars = Expression->LengthInChars;

    while(TRUE) {

        //
        //  MSFIX This logic currently rescans from the beginning.  Should
        //  we only rescan from the end of the previous scan so we don't
        //  create commands that can nest further backticks?
        //


        if (!YoriShFindBackquoteSubstring(&CurrentFullExpression, 0, TRUE, &CurrentExpressionSubset)) {
            break;
        }

        if (!YoriShExecuteExpressionAndCaptureOutput(&CurrentExpressionSubset, &ProcessOutput)) {
            break;
        }

        //
        //  Calculate the number of characters from before the first
        //  backquote, the number after the last backquote, and the
        //  number just obtained from the buffer.
        //

        YoriLibInitEmptyString(&InitialPortion);
        YoriLibInitEmptyString(&TrailingPortion);

        InitialPortion.StartOfString = CurrentFullExpression.StartOfString;
        InitialPortion.LengthInChars = (DWORD)(CurrentExpressionSubset.StartOfString - CurrentFullExpression.StartOfString - 1);

        TrailingPortion.StartOfString = &CurrentFullExpression.StartOfString[InitialPortion.LengthInChars + CurrentExpressionSubset.LengthInChars + 2];
        TrailingPortion.LengthInChars = CurrentFullExpression.LengthInChars - InitialPortion.LengthInChars - CurrentExpressionSubset.LengthInChars - 2;

        if (!YoriLibAllocateString(&NewFullExpression, InitialPortion.LengthInChars + ProcessOutput.LengthInChars + TrailingPortion.LengthInChars + 1)) {
            YoriLibFreeStringContents(&CurrentFullExpression);
            YoriLibFreeStringContents(&ProcessOutput);
            return FALSE;
        }

        NewFullExpression.LengthInChars = YoriLibSPrintf(NewFullExpression.StartOfString,
                                                         _T("%y%y%y"),
                                                         &InitialPortion,
                                                         &ProcessOutput,
                                                         &TrailingPortion);

        YoriLibFreeStringContents(&CurrentFullExpression);

        memcpy(&CurrentFullExpression, &NewFullExpression, sizeof(YORI_STRING));

        YoriLibFreeStringContents(&ProcessOutput);
    }

    memcpy(ResultingExpression, &CurrentFullExpression, sizeof(YORI_STRING));
    return TRUE;
}

/**
 Parse and execute a command string.  This will internally perform parsing
 and redirection, as well as execute multiple subprocesses as needed.  This
 specific function mainly deals with backquote evaluation, carving the
 expression up into multiple multi-program execution plans, and executing
 each.

 @param Expression The string to execute.

 @return TRUE to indicate it was successfully executed, FALSE otherwise.
 */
BOOL
YoriShExecuteExpression(
    __in PYORI_STRING Expression
    )
{
    YORI_EXEC_PLAN ExecPlan;
    YORI_CMD_CONTEXT CmdContext;
    YORI_STRING CurrentFullExpression;

    //
    //  Expand all backquotes.
    //

    if (!YoriShExpandBackquotes(Expression, &CurrentFullExpression)) {
        return FALSE;
    }

    ASSERT(CurrentFullExpression.StartOfString != Expression->StartOfString || CurrentFullExpression.MemoryToFree == NULL);

    //
    //  Parse the expression we're trying to execute.
    //

    if (!YoriShParseCmdlineToCmdContext(&CurrentFullExpression, 0, &CmdContext)) {
        YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("Parse error\n"));
        YoriLibFreeStringContents(&CurrentFullExpression);
        return FALSE;
    }

    if (CmdContext.ArgC == 0) {
        YoriShFreeCmdContext(&CmdContext);
        YoriLibFreeStringContents(&CurrentFullExpression);
        return FALSE;
    }

    if (!YoriShParseCmdContextToExecPlan(&CmdContext, &ExecPlan, NULL, NULL, NULL)) {
        YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("Parse error\n"));
        YoriLibFreeStringContents(&CurrentFullExpression);
        YoriShFreeCmdContext(&CmdContext);
        return FALSE;
    }

    YoriShExecExecPlan(&ExecPlan, NULL);

    YoriShFreeExecPlan(&ExecPlan);
    YoriShFreeCmdContext(&CmdContext);

    YoriLibFreeStringContents(&CurrentFullExpression);

    return TRUE;
}

// vim:sw=4:ts=4:et:
