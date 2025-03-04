/**
 * @file /sh/exec.c
 *
 * Yori shell execute external program
 *
 * Copyright (c) 2017-2019 Malcolm J. Smith
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

#if DBG
/**
 If TRUE, use verbose output when invoking processes under a debugger.
 */
#define YORI_SH_DEBUG_DEBUGGER 0
#else
/**
 If TRUE, use verbose output when invoking processes under a debugger.
 */
#define YORI_SH_DEBUG_DEBUGGER 0
#endif

/**
 The smallest unit of memory that can have protection applied.  It's not
 super critical that this match the system page size - this is used to
 request smaller memory reads from the target.  So long as the system page
 size is a multiple of this value, the logic will still be correct.
 */
#define YORI_SH_MEMORY_PROTECTION_SIZE (4096)

/**
 Given a process that has finished execution, locate the environment block
 within the process and extract it into a string in the currently executing
 process.

 @param ProcessHandle The handle of the child process whose environment is
        requested.

 @param EnvString On successful completion, a newly allocated string
        containing the child process environment.

 @param CurrentDirectory On successful completion, a newly allocated string
        containing the child current directory.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
__success(return)
BOOL
YoriShSuckEnv(
    __in HANDLE ProcessHandle,
    __out PYORI_STRING EnvString,
    __out PYORI_STRING CurrentDirectory
    )
{
    PROCESS_BASIC_INFORMATION BasicInfo;

    LONG Status;
    DWORD dwBytesReturned;
    SIZE_T BytesReturned;
    DWORD EnvironmentBlockPageOffset;
    DWORD EnvCharsToMask;
    DWORD CurrentDirectoryCharsToRead;
    PVOID ProcessParamsBlockToRead;
    PVOID CurrentDirectoryToRead;
    PVOID EnvironmentBlockToRead;
    BOOL TargetProcess32BitPeb;
    DWORD OsVerMajor;
    DWORD OsVerMinor;
    DWORD OsBuildNumber;

    if (DllNtDll.pNtQueryInformationProcess == NULL) {
        return FALSE;
    }

    TargetProcess32BitPeb = YoriLibDoesProcessHave32BitPeb(ProcessHandle);

    Status = DllNtDll.pNtQueryInformationProcess(ProcessHandle, 0, &BasicInfo, sizeof(BasicInfo), &dwBytesReturned);
    if (Status != 0) {
        return FALSE;
    }

#if YORI_SH_DEBUG_DEBUGGER
    YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("Peb at %p, Target %i bit PEB\n"), BasicInfo.PebBaseAddress, TargetProcess32BitPeb?32:64);
#endif

    if (TargetProcess32BitPeb) {
        YORI_LIB_PEB32_NATIVE ProcessPeb;

        if (!ReadProcessMemory(ProcessHandle, BasicInfo.PebBaseAddress, &ProcessPeb, sizeof(ProcessPeb), &BytesReturned)) {
            return FALSE;
        }

#if YORI_SH_DEBUG_DEBUGGER
        YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("Peb contents:\n"));
        YoriLibHexDump((PUCHAR)&ProcessPeb,
                       0,
                       (DWORD)BytesReturned,
                       sizeof(DWORD), 
                       YORI_LIB_HEX_FLAG_DISPLAY_OFFSET);

        YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("ProcessParameters offset %x\n"), FIELD_OFFSET(YORI_LIB_PEB32_NATIVE, ProcessParameters));
#endif

        ProcessParamsBlockToRead = (PVOID)(ULONG_PTR)ProcessPeb.ProcessParameters;

    } else {
        YORI_LIB_PEB64 ProcessPeb;

        if (!ReadProcessMemory(ProcessHandle, BasicInfo.PebBaseAddress, &ProcessPeb, sizeof(ProcessPeb), &BytesReturned)) {
            return FALSE;
        }

#if YORI_SH_DEBUG_DEBUGGER
        YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("ProcessParameters offset %x\n"), FIELD_OFFSET(YORI_LIB_PEB64, ProcessParameters));
#endif

        ProcessParamsBlockToRead = (PVOID)(ULONG_PTR)ProcessPeb.ProcessParameters;
    }

#if YORI_SH_DEBUG_DEBUGGER
    YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("ProcessParameters at %p\n"), ProcessParamsBlockToRead);
#endif

    if (TargetProcess32BitPeb) {
        YORI_LIB_PROCESS_PARAMETERS32 ProcessParameters;

        if (!ReadProcessMemory(ProcessHandle, ProcessParamsBlockToRead, &ProcessParameters, sizeof(ProcessParameters), &BytesReturned)) {
            return FALSE;
        }

#if YORI_SH_DEBUG_DEBUGGER
        YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("ProcessParameters contents:\n"));
        YoriLibHexDump((PUCHAR)&ProcessParameters,
                       0,
                       (DWORD)BytesReturned,
                       sizeof(DWORD),
                       YORI_LIB_HEX_FLAG_DISPLAY_OFFSET);
#endif

        CurrentDirectoryToRead = (PVOID)(ULONG_PTR)ProcessParameters.CurrentDirectory;
        CurrentDirectoryCharsToRead = ProcessParameters.CurrentDirectoryLengthInBytes / sizeof(TCHAR);
        EnvironmentBlockToRead = (PVOID)(ULONG_PTR)ProcessParameters.EnvironmentBlock;
        EnvironmentBlockPageOffset = (YORI_SH_MEMORY_PROTECTION_SIZE - 1) & (DWORD)ProcessParameters.EnvironmentBlock;
    } else {
        YORI_LIB_PROCESS_PARAMETERS64 ProcessParameters;

        if (!ReadProcessMemory(ProcessHandle, ProcessParamsBlockToRead, &ProcessParameters, sizeof(ProcessParameters), &BytesReturned)) {
            return FALSE;
        }

        CurrentDirectoryToRead = (PVOID)(ULONG_PTR)ProcessParameters.CurrentDirectory;
        CurrentDirectoryCharsToRead = ProcessParameters.CurrentDirectoryLengthInBytes / sizeof(TCHAR);
        EnvironmentBlockToRead = (PVOID)(ULONG_PTR)ProcessParameters.EnvironmentBlock;
        EnvironmentBlockPageOffset = (YORI_SH_MEMORY_PROTECTION_SIZE - 1) & (DWORD)ProcessParameters.EnvironmentBlock;
    }

    EnvCharsToMask = EnvironmentBlockPageOffset / sizeof(TCHAR);
#if YORI_SH_DEBUG_DEBUGGER
    YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("EnvironmentBlock at %p PageOffset %04x EnvCharsToMask %04x\n"), EnvironmentBlockToRead, EnvironmentBlockPageOffset, EnvCharsToMask);
#endif

    //
    //  Attempt to read 64Kb of environment minus the offset from the
    //  page containing the environment.  This occurs because older
    //  versions of Windows don't record how large the block is.  As
    //  a result, this may be truncated, which is acceptable.
    //

    if (!YoriLibAllocateString(EnvString, 32 * 1024 - EnvCharsToMask)) {
        return FALSE;
    }

    //
    //  Loop issuing reads and decreasing the read size by one page each
    //  time if reads are failing due to invalid memory on the target
    //

    do {

        if (!ReadProcessMemory(ProcessHandle, EnvironmentBlockToRead, EnvString->StartOfString, EnvString->LengthAllocated * sizeof(TCHAR), &BytesReturned)) {
            DWORD Err = GetLastError();
            if (Err != ERROR_PARTIAL_COPY && Err != ERROR_NOACCESS) {
#if YORI_SH_DEBUG_DEBUGGER
                YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("ReadProcessMemory returned error %08x BytesReturned %i\n"), Err, BytesReturned);
#endif
                YoriLibFreeStringContents(EnvString);
                return FALSE;
            }

            if (EnvString->LengthAllocated * sizeof(TCHAR) < YORI_SH_MEMORY_PROTECTION_SIZE) {
#if YORI_SH_DEBUG_DEBUGGER
                YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("ReadProcessMemory failed to read less than a page\n"));
#endif
                YoriLibFreeStringContents(EnvString);
                return FALSE;
            }

            EnvString->LengthAllocated -= YORI_SH_MEMORY_PROTECTION_SIZE / sizeof(TCHAR);
        } else {

#if YORI_SH_DEBUG_DEBUGGER
            if (BytesReturned > 0x2000) {
                BytesReturned = 0x2000;
            }
            YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("Environment contents:\n"));
            YoriLibHexDump((PUCHAR)EnvString->StartOfString,
                           0,
                           (DWORD)BytesReturned,
                           sizeof(UCHAR),
                           YORI_LIB_HEX_FLAG_DISPLAY_OFFSET | YORI_LIB_HEX_FLAG_DISPLAY_CHARS);
#endif
            break;
        }

    } while (TRUE);

    //
    //  NT 3.1 describes the environment block in ANSI.  Although people love
    //  to criticize it, this is probably the worst quirk I've found in it
    //  yet.
    //

    YoriLibGetOsVersion(&OsVerMajor, &OsVerMinor, &OsBuildNumber);

    if (OsVerMajor == 3 && OsVerMinor == 10) {
        YORI_STRING UnicodeEnvString;

        if (!YoriLibAreAnsiEnvironmentStringsValid((PUCHAR)EnvString->StartOfString, EnvString->LengthAllocated, &UnicodeEnvString)) {
#if YORI_SH_DEBUG_DEBUGGER
            YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("EnvString not valid\n"));
#endif
            YoriLibFreeStringContents(EnvString);
            return FALSE;
        }

        YoriLibFreeStringContents(EnvString);
        memcpy(EnvString, &UnicodeEnvString, sizeof(YORI_STRING));
    } else {
        if (!YoriLibAreEnvironmentStringsValid(EnvString)) {
#if YORI_SH_DEBUG_DEBUGGER
            YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("EnvString not valid\n"));
#endif
            YoriLibFreeStringContents(EnvString);
            return FALSE;
        }
    }

#if YORI_SH_DEBUG_DEBUGGER
    YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("EnvString has %i chars\n"), EnvString->LengthInChars);
#endif

    if (EnvString->LengthInChars <= 2) {
#if YORI_SH_DEBUG_DEBUGGER
        YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("EnvString is empty\n"));
#endif
        YoriLibFreeStringContents(EnvString);
        return FALSE;
    }

    if (!YoriLibAllocateString(CurrentDirectory, CurrentDirectoryCharsToRead + 1)) {
        YoriLibFreeStringContents(EnvString);
        return FALSE;
    }

    if (!ReadProcessMemory(ProcessHandle, CurrentDirectoryToRead, CurrentDirectory->StartOfString, CurrentDirectoryCharsToRead * sizeof(TCHAR), &BytesReturned)) {
        YoriLibFreeStringContents(EnvString);
        return FALSE;
    }

    CurrentDirectory->LengthInChars = CurrentDirectoryCharsToRead;
    CurrentDirectory->StartOfString[CurrentDirectoryCharsToRead] = '\0';

    return TRUE;
}

/**
 Find a process in the list of known debugged child processes by its process
 ID.

 @param ListHead Pointer to the list of known processes.

 @param dwProcessId The process identifier of the process whose information is
        requested.

 @return Pointer to the information block about the child process.
 */
PYORI_LIBSH_DEBUGGED_CHILD_PROCESS
YoriShFindDebuggedChildProcess(
    __in PYORI_LIST_ENTRY ListHead,
    __in DWORD dwProcessId
    )
{
    PYORI_LIST_ENTRY ListEntry;
    PYORI_LIBSH_DEBUGGED_CHILD_PROCESS Process;

    ListEntry = NULL;
    while (TRUE) {
        ListEntry = YoriLibGetNextListEntry(ListHead, ListEntry);
        if (ListEntry == NULL) {
            break;
        }

        Process = CONTAINING_RECORD(ListEntry, YORI_LIBSH_DEBUGGED_CHILD_PROCESS, ListEntry);
        if (Process->dwProcessId == dwProcessId) {
            return Process;
        }
    }

    return NULL;
}

/**
 A structure passed into a debugger thread to indicate which actions to
 perform.
 */
typedef struct _YORI_SH_DEBUG_THREAD_CONTEXT {

    /**
     A referenced ExecContext indicating the process to launch.
     */
    PYORI_LIBSH_SINGLE_EXEC_CONTEXT ExecContext;

    /**
     An event to signal once the process has been launched, indicating that
     redirection has been initiated, the process has started, and redirection
     has been reverted.  This indicates the calling thread is free to reason
     about stdin/stdout and console state.
     */
    HANDLE InitializedEvent;

} YORI_SH_DEBUG_THREAD_CONTEXT, *PYORI_SH_DEBUG_THREAD_CONTEXT;

/**
 Pump debug messages from a child process, and when the child process has
 completed execution, extract its environment and apply it to the currently
 executing process.

 @param Context Pointer to the exec context for the child process.

 @return Not meaningful.
 */
DWORD WINAPI
YoriShPumpProcessDebugEventsAndApplyEnvironmentOnExit(
    __in PVOID Context
    )
{
    PYORI_SH_DEBUG_THREAD_CONTEXT ThreadContext = (PYORI_SH_DEBUG_THREAD_CONTEXT)Context;
    PYORI_LIBSH_SINGLE_EXEC_CONTEXT ExecContext = ThreadContext->ExecContext;
    HANDLE InitializedEvent = ThreadContext->InitializedEvent;
    PYORI_LIBSH_DEBUGGED_CHILD_PROCESS DebuggedChild;
    YORI_STRING OriginalAliases;
    DWORD Err;
    BOOL HaveOriginalAliases;
    BOOL ApplyEnvironment = TRUE;
    BOOL FailedInRedirection = FALSE;

    YoriLibInitEmptyString(&OriginalAliases);
    HaveOriginalAliases = YoriShGetSystemAliasStrings(TRUE, &OriginalAliases);

    Err = YoriLibShCreateProcess(ExecContext, NULL, &FailedInRedirection);
    if (Err != NO_ERROR) {
        LPTSTR ErrText = YoriLibGetWinErrorText(Err);
        if (FailedInRedirection) {
            YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("Failed to initialize redirection: %s"), ErrText);
        } else {
            YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("CreateProcess failed: %s"), ErrText);
        }
        YoriLibFreeWinErrorText(ErrText);
        YoriLibShCleanupFailedProcessLaunch(ExecContext);
        if (HaveOriginalAliases) {
            YoriLibFreeStringContents(&OriginalAliases);
        }
        YoriLibShDereferenceExecContext(ExecContext, TRUE);
        SetEvent(InitializedEvent);
        return 0;
    }

    YoriLibShCommenceProcessBuffersIfNeeded(ExecContext);
    SetEvent(InitializedEvent);

    while (TRUE) {
        DEBUG_EVENT DbgEvent;
        DWORD dwContinueStatus;

        ZeroMemory(&DbgEvent, sizeof(DbgEvent));
        if (!WaitForDebugEvent(&DbgEvent, INFINITE)) {
            break;
        }

#if YORI_SH_DEBUG_DEBUGGER
        YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("DbgEvent Pid %x Tid %x Event %x\n"), DbgEvent.dwProcessId, DbgEvent.dwThreadId, DbgEvent.dwDebugEventCode);
#endif

        dwContinueStatus = DBG_CONTINUE;

        switch(DbgEvent.dwDebugEventCode) {
            case CREATE_PROCESS_DEBUG_EVENT:
                CloseHandle(DbgEvent.u.CreateProcessInfo.hFile);

                DebuggedChild = YoriLibReferencedMalloc(sizeof(YORI_LIBSH_DEBUGGED_CHILD_PROCESS));
                if (DebuggedChild == NULL) {
                    break;
                }

                ZeroMemory(DebuggedChild, sizeof(YORI_LIBSH_DEBUGGED_CHILD_PROCESS));
                if (!DuplicateHandle(GetCurrentProcess(), DbgEvent.u.CreateProcessInfo.hProcess, GetCurrentProcess(), &DebuggedChild->hProcess, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
                    YoriLibDereference(DebuggedChild);
                    break;
                }
                if (!DuplicateHandle(GetCurrentProcess(), DbgEvent.u.CreateProcessInfo.hThread, GetCurrentProcess(), &DebuggedChild->hInitialThread, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
                    CloseHandle(DebuggedChild->hProcess);
                    YoriLibDereference(DebuggedChild);
                    break;
                }

                DebuggedChild->dwProcessId = DbgEvent.dwProcessId;
                DebuggedChild->dwInitialThreadId = DbgEvent.dwThreadId;
#if YORI_SH_DEBUG_DEBUGGER
                YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("PROCESS CREATE recorded for Pid %x Tid %x\n"), DbgEvent.dwProcessId, DbgEvent.dwThreadId);
#endif

                YoriLibAppendList(&ExecContext->DebuggedChildren, &DebuggedChild->ListEntry);
                DebuggedChild = NULL;

                break;
            case EXIT_PROCESS_DEBUG_EVENT:
                DebuggedChild = YoriShFindDebuggedChildProcess(&ExecContext->DebuggedChildren, DbgEvent.dwProcessId);
                ASSERT(DebuggedChild != NULL);
                if (DebuggedChild != NULL) {
#if YORI_SH_DEBUG_DEBUGGER
                    YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("PROCESS DELETE recorded for Pid %x\n"), DbgEvent.dwProcessId);
#endif
                    YoriLibRemoveListItem(&DebuggedChild->ListEntry);
                    CloseHandle(DebuggedChild->hProcess);
                    CloseHandle(DebuggedChild->hInitialThread);
                    YoriLibDereference(DebuggedChild);
                    DebuggedChild = NULL;
                }
                break;
            case LOAD_DLL_DEBUG_EVENT:
#if YORI_SH_DEBUG_DEBUGGER
                if (DbgEvent.u.LoadDll.lpImageName != NULL) {
                    SIZE_T BytesReturned;
                    PVOID DllNamePtr;
                    TCHAR DllName[128];

                    if (ReadProcessMemory(ExecContext->hProcess, DbgEvent.u.LoadDll.lpImageName, &DllNamePtr, sizeof(DllNamePtr), &BytesReturned)) {
                        if (ReadProcessMemory(ExecContext->hProcess, DllNamePtr, &DllName, sizeof(DllName), &BytesReturned)) {
                            if (DbgEvent.u.LoadDll.fUnicode) {
                                YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("Dll loaded: %s\n"), DllName);
                            } else {
                                YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("Dll loaded: %hs\n"), DllName);
                            }
                        }
                    }
                }
#endif
                CloseHandle(DbgEvent.u.LoadDll.hFile);
                break;
            case EXCEPTION_DEBUG_EVENT:

                //
                //  Wow64 processes throw a breakpoint once 32 bit code starts
                //  running, and the debugger is expected to handle it.  The
                //  two codes are for breakpoint and x86 breakpoint
                //

#if YORI_SH_DEBUG_DEBUGGER
                YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("ExceptionCode %x\n"), DbgEvent.u.Exception.ExceptionRecord.ExceptionCode);
#endif
                dwContinueStatus = DBG_EXCEPTION_NOT_HANDLED;

                if (DbgEvent.u.Exception.ExceptionRecord.ExceptionCode == EXCEPTION_BREAKPOINT) {
                    dwContinueStatus = DBG_CONTINUE;
#if _M_MRX000
                    DebuggedChild = YoriShFindDebuggedChildProcess(&ExecContext->DebuggedChildren, DbgEvent.dwProcessId);
                    ASSERT(DebuggedChild != NULL);

                    //
                    //  MIPS appears to continue from the instruction that
                    //  raised the exception.  We want to skip over it, and
                    //  fortunately we know all instructions are 4 bytes.  We
                    //  only do this for the initial breakpoint, which is on
                    //  the initial thread; other threads would crash the
                    //  process if the debugger wasn't here, so let it die.
                    //

                    if (DbgEvent.dwThreadId == DebuggedChild->dwInitialThreadId) {
                        CONTEXT ThreadContext;
                        ZeroMemory(&ThreadContext, sizeof(ThreadContext));
                        ThreadContext.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
                        GetThreadContext(DebuggedChild->hInitialThread, &ThreadContext);
                        ThreadContext.Fir += 4;
                        SetThreadContext(DebuggedChild->hInitialThread, &ThreadContext);
                    } else {
                        dwContinueStatus = DBG_EXCEPTION_NOT_HANDLED;
                    }
#endif
                }

                if (DbgEvent.u.Exception.ExceptionRecord.ExceptionCode == 0x4000001F) {

                    dwContinueStatus = DBG_CONTINUE;
                }

                break;
        }

        if (DbgEvent.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT &&
            DbgEvent.dwProcessId == ExecContext->dwProcessId) {

            YORI_STRING EnvString;
            YORI_STRING CurrentDirectory;

            //
            //  If the user sent this task the background after starting it,
            //  the environment should not be applied anymore.
            //

            if (!ExecContext->CaptureEnvironmentOnExit) {
                ApplyEnvironment = FALSE;
            }

            if (ApplyEnvironment &&
                YoriShSuckEnv(ExecContext->hProcess, &EnvString, &CurrentDirectory)) {
                YoriShSetEnvironmentStrings(&EnvString);

                YoriLibSetCurrentDirectorySaveDriveCurrentDirectory(&CurrentDirectory);
                YoriLibFreeStringContents(&EnvString);
                YoriLibFreeStringContents(&CurrentDirectory);
            }
        }

        ContinueDebugEvent(DbgEvent.dwProcessId, DbgEvent.dwThreadId, dwContinueStatus);
        if (DbgEvent.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT &&
            DbgEvent.dwProcessId == ExecContext->dwProcessId) {
            break;
        }
    }

    WaitForSingleObject(ExecContext->hProcess, INFINITE);
    if (HaveOriginalAliases) {
        YORI_STRING NewAliases;
        if (ApplyEnvironment &&
            YoriShGetSystemAliasStrings(TRUE, &NewAliases)) {
            YoriShMergeChangedAliasStrings(TRUE, &OriginalAliases, &NewAliases);
            YoriLibFreeStringContents(&NewAliases);
        }
        YoriLibFreeStringContents(&OriginalAliases);
    }

    ExecContext->DebugPumpThreadFinished = TRUE;
    YoriLibShDereferenceExecContext(ExecContext, TRUE);
    return 0;
}


/**
 Wait for a process to terminate.  This is also a good opportunity for Yori
 to monitor for keyboard input that may be better handled by Yori than the
 foreground process.

 @param ExecContext Pointer to the context used to invoke the process, which
        includes information about whether it should be cancelled.
 */
VOID
YoriShWaitForProcessToTerminate(
    __in PYORI_LIBSH_SINGLE_EXEC_CONTEXT ExecContext
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
    DWORD LoseFocusCount = 0;
    DWORD Delay;
    BOOLEAN CtrlBFoundThisPass;
    BOOLEAN LoseFocusFoundThisPass;

    //
    //  If the child isn't running under a debugger, by this point redirection
    //  has been established and then reverted.  This should be dealing with
    //  the original input handle.  If it's running under a debugger, we haven't
    //  started redirecting yet.
    //

    if (ExecContext->CaptureEnvironmentOnExit) {
        YORI_SH_DEBUG_THREAD_CONTEXT ThreadContext;
        DWORD ThreadId;

        //
        //  Because the debugger thread needs to initialize redirection,
        //  start the thread and wait for it to indicate this process is done.
        //
        //  This thread can't reason about the stdin handle and console state
        //  until that is finished.
        //

        ThreadContext.InitializedEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
        if (ThreadContext.InitializedEvent == NULL) {
            YoriLibCancelEnable();
            YoriLibCancelIgnore();
            return;
        }

        YoriLibShReferenceExecContext(ExecContext);
        ThreadContext.ExecContext = ExecContext;
        ExecContext->hDebuggerThread = CreateThread(NULL, 0, YoriShPumpProcessDebugEventsAndApplyEnvironmentOnExit, &ThreadContext, 0, &ThreadId);
        if (ExecContext->hDebuggerThread == NULL) {
            YoriLibShDereferenceExecContext(ExecContext, TRUE);
            CloseHandle(ThreadContext.InitializedEvent);
            YoriLibCancelEnable();
            YoriLibCancelIgnore();
            return;
        }

        WaitForSingleObject(ThreadContext.InitializedEvent, INFINITE);
        CloseHandle(ThreadContext.InitializedEvent);

        WaitOn[0] = ExecContext->hDebuggerThread;
    } else {
        ASSERT(ExecContext->hProcess != NULL);
        WaitOn[0] = ExecContext->hProcess;
    }

    YoriLibCancelEnable();
    WaitOn[1] = YoriLibCancelGetEvent();
    WaitOn[2] = GetStdHandle(STD_INPUT_HANDLE);

    Delay = INFINITE;

    while (TRUE) {
        if (YoriShGlobal.ImplicitSynchronousTaskActive) {
            Result = WaitForMultipleObjects(2, WaitOn, FALSE, Delay);
        } else if (Delay == INFINITE) {
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

                YoriLibShWaitForProcessBufferToFinalize(ExecContext->StdOut.Buffer.ProcessBuffers);
            }

            if (ExecContext->StdErrType == StdErrTypeBuffer &&
                ExecContext->StdErr.Buffer.ProcessBuffers != NULL) {

                YoriLibShWaitForProcessBufferToFinalize(ExecContext->StdErr.Buffer.ProcessBuffers);
            }
            break;
        }

        //
        //  If the user has hit Ctrl+C or Ctrl+Break, request the process
        //  to clean up gracefully and unwind.  Later on we'll try to kill
        //  all processes in the exec plan, so we don't need to try too hard
        //  at this point.  If the process doesn't exist, which happens when
        //  we're trying to launch it as a debuggee, wait a bit to see if it
        //  comes into existence.  If launching it totally failed, the debug
        //  thread will terminate and we'll exit; if it succeeds, we'll get
        //  to cancel it again here.
        //

        if (Result == (WAIT_OBJECT_0 + 1)) {
            if (ExecContext->TerminateGracefully && ExecContext->dwProcessId != 0) {
                GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, ExecContext->dwProcessId);
                break;
            } else if (ExecContext->hProcess != NULL) {
                TerminateProcess(ExecContext->hProcess, 1);
                break;
            } else {
                Sleep(50);
            }
        }

        if (YoriShGlobal.ImplicitSynchronousTaskActive ||
            WaitForSingleObject(WaitOn[2], 0) == WAIT_TIMEOUT) {

            CtrlBCount = 0;
            LoseFocusCount = 0;
            Delay = INFINITE;
            continue;
        }

        //
        //  Check if there's pending input.  If there is, go have a look.
        //

        GetNumberOfConsoleInputEvents(GetStdHandle(STD_INPUT_HANDLE), &RecordsNeeded);

        if (RecordsNeeded > RecordsAllocated || InputRecords == NULL) {
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
                RecordsAllocated = 0;
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
            LoseFocusFoundThisPass = FALSE;

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
                } else if (InputRecords[Count].EventType == FOCUS_EVENT &&
                           !InputRecords[Count].Event.FocusEvent.bSetFocus) {

                    LoseFocusFoundThisPass = TRUE;
                }
            }

            Delay = 100;

            if (CtrlBFoundThisPass) {
                if (CtrlBCount < 3) {
                    CtrlBCount++;
                    Delay = 30;
                    continue;
                } else {

                    //
                    //  If a process is being moved to the background, don't
                    //  suck back any environment later when it completes.
                    //  Note this is a race condition, since that logic
                    //  is occurring on a different thread that is processing
                    //  debug messages while this code is running.  For the
                    //  same reason though, if process termination is racing
                    //  with observing Ctrl+B, either outcome is possible.
                    //

                    ExecContext->CaptureEnvironmentOnExit = FALSE;

                    //
                    //  If the taskbar is showing an active task, clear it.
                    //  We don't really know if the task failed or succeeded,
                    //  but we do know the user is interacting with this
                    //  console, so flashing the taskbar a random color is
                    //  not helpful or desirable.
                    //

                    YoriShSetWindowState(YORI_SH_TASK_COMPLETE);

                    break;
                }
            } else {
                CtrlBCount = 0;
            }

            if (LoseFocusFoundThisPass) {
                if (LoseFocusCount < 3) {
                    LoseFocusCount++;
                    Delay = 30;
                } else {
                    if (!ExecContext->SuppressTaskCompletion &&
                        !ExecContext->TaskCompletionDisplayed &&
                        !YoriLibIsExecutableGui(&ExecContext->CmdToExec.ArgV[0])) {

                        ExecContext->TaskCompletionDisplayed = TRUE;
                        YoriShSetWindowState(YORI_SH_TASK_IN_PROGRESS);
                    }
                }
            } else {
                LoseFocusCount = 0;
            }
        }
    }

    if (InputRecords != NULL) {
        YoriLibFree(InputRecords);
    }

    YoriLibCancelIgnore();
}

/**
 Try to launch a single program via ShellExecuteEx rather than CreateProcess.
 This is used to open URLs, documents and scripts, as well as when
 CreateProcess said elevation is needed.

 @param ExecContext Pointer to the program to execute.

 @param ProcessInfo On successful completion, the process handle might be
        populated here.  It might not, because shell could decide to
        communicate with an existing process via DDE, and it doesn't tell us
        about the process it was talking to in that case.  In fairness, we
        probably shouldn't wait on a process that we didn't launch.
 
 @return TRUE to indicate success, FALSE on failure.
 */
__success(return)
BOOL
YoriShExecViaShellExecute(
    __in PYORI_LIBSH_SINGLE_EXEC_CONTEXT ExecContext,
    __out PPROCESS_INFORMATION ProcessInfo
    )
{
    YORI_STRING Args;
    YORI_LIBSH_CMD_CONTEXT ArgContext;
    YORI_SHELLEXECUTEINFO sei;
    YORI_LIBSH_PREVIOUS_REDIRECT_CONTEXT PreviousRedirectContext;
    DWORD LastError;

    YoriLibLoadShell32Functions();

    //
    //  This function is called for two reasons.  It might be because a
    //  process launch requires elevation, in which case ShellExecuteEx
    //  should exist because any OS with UAC will have it.  For NT 3.51,
    //  ShellExecuteEx exists but fails, and before that, it's not even
    //  there.  This code has to handle each case.
    //

    if (DllShell32.pShellExecuteExW == NULL && DllShell32.pShellExecuteW == NULL) {
        return FALSE;
    }

    ZeroMemory(&sei, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS |
                SEE_MASK_FLAG_NO_UI |
                SEE_MASK_NOZONECHECKS |
                SEE_MASK_UNICODE |
                SEE_MASK_NO_CONSOLE;

    sei.lpFile = ExecContext->CmdToExec.ArgV[0].StartOfString;
    YoriLibInitEmptyString(&Args);
    if (ExecContext->CmdToExec.ArgC > 1) {
        memcpy(&ArgContext, &ExecContext->CmdToExec, sizeof(YORI_LIBSH_CMD_CONTEXT));
        ArgContext.ArgC--;
        ArgContext.ArgV = &ArgContext.ArgV[1];
        ArgContext.ArgContexts = &ArgContext.ArgContexts[1];
        YoriLibShBuildCmdlineFromCmdContext(&ArgContext, &Args, !ExecContext->IncludeEscapesAsLiteral, NULL, NULL);
    }

    sei.lpParameters = Args.StartOfString;
    sei.nShow = SW_SHOWNORMAL;

    ZeroMemory(ProcessInfo, sizeof(PROCESS_INFORMATION));

    LastError = YoriLibShInitializeRedirection(ExecContext, FALSE, &PreviousRedirectContext);
    if (LastError != ERROR_SUCCESS) {
        YoriLibFreeStringContents(&Args);
        return FALSE;
    }

    LastError = ERROR_SUCCESS;
    if (DllShell32.pShellExecuteExW != NULL) {
        if (!DllShell32.pShellExecuteExW(&sei)) {
            LastError = GetLastError();
        }
    }

    if (DllShell32.pShellExecuteExW == NULL ||
        LastError == ERROR_CALL_NOT_IMPLEMENTED) {

        HINSTANCE hInst;
        hInst = DllShell32.pShellExecuteW(NULL, NULL, sei.lpFile, sei.lpParameters, _T("."), sei.nShow);

        LastError = ERROR_SUCCESS;
        if (hInst < (HINSTANCE)(DWORD_PTR)32) {
            switch((DWORD_PTR)hInst) {
                case ERROR_FILE_NOT_FOUND:
                case ERROR_PATH_NOT_FOUND:
                case ERROR_ACCESS_DENIED:
                case ERROR_BAD_FORMAT:
                    LastError = (DWORD)(DWORD_PTR)hInst;
                    break;
                case SE_ERR_SHARE:
                    LastError = ERROR_SHARING_VIOLATION;
                    break;
                default:
                    LastError = ERROR_TOO_MANY_OPEN_FILES;
                    break;
            }
        }
    }

    YoriLibShRevertRedirection(&PreviousRedirectContext);
    YoriLibFreeStringContents(&Args);

    if (LastError != ERROR_SUCCESS) {
        LPTSTR ErrText;
        ErrText = YoriLibGetWinErrorText(LastError);
        YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("ShellExecuteEx failed (%i): %s"), LastError, ErrText);
        YoriLibFreeWinErrorText(ErrText);
        return FALSE;
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
    __in PYORI_LIBSH_SINGLE_EXEC_CONTEXT ExecContext
    )
{
    DWORD ExitCode = 0;
    LPTSTR szExt;
    BOOLEAN ExecProcess = TRUE;
    BOOLEAN LaunchFailed = FALSE;
    BOOLEAN LaunchViaShellExecute = FALSE;

    if (YoriLibIsPathUrl(&ExecContext->CmdToExec.ArgV[0])) {
        LaunchViaShellExecute = TRUE;
        ExecContext->SuppressTaskCompletion = TRUE;
    } else {
        szExt = YoriLibFindRightMostCharacter(&ExecContext->CmdToExec.ArgV[0], '.');
        if (szExt != NULL) {
            YORI_STRING YsExt;

            YoriLibInitEmptyString(&YsExt);
            YsExt.StartOfString = szExt;
            YsExt.LengthInChars = ExecContext->CmdToExec.ArgV[0].LengthInChars - (DWORD)(szExt - ExecContext->CmdToExec.ArgV[0].StartOfString);

            if (YoriLibCompareStringWithLiteralInsensitive(&YsExt, _T(".com")) == 0) {
                if (YoriShExecuteNamedModuleInProc(ExecContext->CmdToExec.ArgV[0].StartOfString, ExecContext, &ExitCode)) {
                    ExecProcess = FALSE;
                } else {
                    ExitCode = 0;
                }
            } else if (YoriLibCompareStringWithLiteralInsensitive(&YsExt, _T(".ys1")) == 0) {
                ExecProcess = FALSE;
                YoriLibShCheckIfArgNeedsQuotes(&ExecContext->CmdToExec, 0);
                ExitCode = YoriShBuckPass(ExecContext, 1, _T("ys"));
            } else if (YoriLibCompareStringWithLiteralInsensitive(&YsExt, _T(".cmd")) == 0 ||
                       YoriLibCompareStringWithLiteralInsensitive(&YsExt, _T(".bat")) == 0) {
                ExecProcess = FALSE;
                YoriLibShCheckIfArgNeedsQuotes(&ExecContext->CmdToExec, 0);
                if (ExecContext->WaitForCompletion) {
                    ExecContext->CaptureEnvironmentOnExit = TRUE;
                }
                ExitCode = YoriShBuckPassToCmd(ExecContext);
            } else if (YoriLibCompareStringWithLiteralInsensitive(&YsExt, _T(".exe")) != 0) {
                LaunchViaShellExecute = TRUE;
                ExecContext->SuppressTaskCompletion = TRUE;
            }
        }
    }

    if (ExecProcess) {

        BOOL FailedInRedirection = FALSE;

        if (!LaunchViaShellExecute && !ExecContext->CaptureEnvironmentOnExit) {
            DWORD Err = YoriLibShCreateProcess(ExecContext, NULL, &FailedInRedirection);

            if (Err != NO_ERROR) {
                if (Err == ERROR_ELEVATION_REQUIRED) {
                    LaunchViaShellExecute = TRUE;
                } else {
                    LPTSTR ErrText = YoriLibGetWinErrorText(Err);
                    if (FailedInRedirection) {
                        YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("Failed to initialize redirection: %s"), ErrText);
                    } else {
                        YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("CreateProcess failed: %s"), ErrText);
                    }
                    YoriLibFreeWinErrorText(ErrText);
                    LaunchFailed = TRUE;
                }
            }
        }

        if (LaunchViaShellExecute) {
            PROCESS_INFORMATION ProcessInfo;

            ZeroMemory(&ProcessInfo, sizeof(ProcessInfo));

            if (!YoriShExecViaShellExecute(ExecContext, &ProcessInfo)) {
                LaunchFailed = TRUE;
            } else {
                ExecContext->hProcess = ProcessInfo.hProcess;
                ExecContext->hPrimaryThread = ProcessInfo.hThread;
                ExecContext->dwProcessId = ProcessInfo.dwProcessId;
            }
        }

        if (LaunchFailed) {
            YoriLibShCleanupFailedProcessLaunch(ExecContext);
            return 1;
        }

        if (!ExecContext->CaptureEnvironmentOnExit) {
            YoriLibShCommenceProcessBuffersIfNeeded(ExecContext);
        }

        //
        //  We may not have a process handle but still be successful if
        //  ShellExecute decided to interact with an existing process
        //  rather than launch a new one.  This isn't going to be very
        //  common in any interactive shell, and it's clearly going to break
        //  things, but there's not much we can do about it from here.
        //
        //  When launching under a debugger, the launch occurs from the
        //  debugging thread, so a process handle may not be present
        //  until the call to wait on it.
        //

        if (ExecContext->hProcess != NULL || ExecContext->CaptureEnvironmentOnExit) {
            if (ExecContext->CaptureEnvironmentOnExit) {
                ASSERT(ExecContext->WaitForCompletion);
                ExecContext->WaitForCompletion = TRUE;
            }
            if (ExecContext->WaitForCompletion) {
                YoriShWaitForProcessToTerminate(ExecContext);
                if (ExecContext->hProcess != NULL) {
                    GetExitCodeProcess(ExecContext->hProcess, &ExitCode);
                } else {
                    ExitCode = EXIT_FAILURE;
                }
            } else if (ExecContext->StdOutType != StdOutTypePipe) {
                ASSERT(!ExecContext->CaptureEnvironmentOnExit);
                if (YoriShCreateNewJob(ExecContext, ExecContext->hProcess, ExecContext->dwProcessId)) {
                    ExecContext->dwProcessId = 0;
                    ExecContext->hProcess = NULL;
                }
            }
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
    __in PYORI_LIBSH_EXEC_PLAN ExecPlan
    )
{
    PYORI_LIBSH_SINGLE_EXEC_CONTEXT ExecContext;

    //
    //  Loop and ask the processes nicely to terminate.
    //

    ExecContext = ExecPlan->FirstCmd;
    while (ExecContext != NULL) {
        if (ExecContext->hProcess != NULL) {
            if (WaitForSingleObject(ExecContext->hProcess, 0) != WAIT_OBJECT_0) {
                if (ExecContext->TerminateGracefully &&
                    ExecContext->dwProcessId != 0) {

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

    //
    //  Loop waiting for any debugger threads to terminate.  These are
    //  referencing the ExecContext so it's important that they're
    //  terminated before we start freeing them.
    //

    ExecContext = ExecPlan->FirstCmd;
    while (ExecContext != NULL) {
        if (ExecContext->hDebuggerThread != NULL) {
            WaitForSingleObject(ExecContext->hDebuggerThread, INFINITE);
        }
        ExecContext = ExecContext->NextProgram;
    }
}

/**
 Execute a single command by invoking the YORISPEC executable and telling it
 to execute the command string.  This is used when an expression is compound
 but cannot wait (eg. a & b &) such that something has to wait for a to
 finish before executing b but input is supposed to continute immediately.  It
 is also used when a builtin is being executed without waiting, so that long
 running tasks can be builtin to the shell executable and still have non-
 waiting semantics on request.

 @param ExecContext Pointer to the single program string to execute.  This is
        prepended with the YORISPEC executable and executed.
 */
VOID
YoriShExecViaSubshell(
    __in PYORI_LIBSH_SINGLE_EXEC_CONTEXT ExecContext
    )
{
    YORI_STRING PathToYori;

    YoriLibInitEmptyString(&PathToYori);
    if (YoriShAllocateAndGetEnvironmentVariable(_T("YORISPEC"), &PathToYori, NULL)) {

        YoriShGlobal.ErrorLevel = YoriShBuckPass(ExecContext, 2, PathToYori.StartOfString, _T("/ss"));
        YoriLibFreeStringContents(&PathToYori);
        return;
    } else {
        YoriShGlobal.ErrorLevel = EXIT_FAILURE;
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
    __in PYORI_LIBSH_EXEC_PLAN ExecPlan,
    __out_opt PVOID * OutputBuffer
    )
{
    PYORI_LIBSH_SINGLE_EXEC_CONTEXT ExecContext;
    PVOID PreviouslyObservedOutputBuffer = NULL;
    BOOL ExecutableFound;

    //
    //  If a plan requires executing multiple tasks without waiting, hand the
    //  request to a subshell so we can execute a single thing without
    //  waiting and let it schedule the tasks
    //

    if (OutputBuffer == NULL &&
        ExecPlan->NumberCommands > 1 &&
        !ExecPlan->WaitForCompletion) {

        YoriShExecViaSubshell(&ExecPlan->EntireCmd);
        return;
    }

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

        if (YoriLibIsPathUrl(&ExecContext->CmdToExec.ArgV[0])) {
            YoriShGlobal.ErrorLevel = YoriShExecuteSingleProgram(ExecContext);
        } else {
            if (!YoriShResolveCommandToExecutable(&ExecContext->CmdToExec, &ExecutableFound)) {
                break;
            }

            if (ExecutableFound) {
                YoriShGlobal.ErrorLevel = YoriShExecuteSingleProgram(ExecContext);
            } else if (ExecPlan->NumberCommands == 1 && !ExecPlan->WaitForCompletion) {
                YoriShExecViaSubshell(ExecContext);
                if (OutputBuffer != NULL) {
                    *OutputBuffer = NULL;
                }
                return;
            } else {
                YoriShGlobal.ErrorLevel = YoriShBuiltIn(ExecContext);
            }
        }

        if (ExecContext->TaskCompletionDisplayed) {
            ExecPlan->TaskCompletionDisplayed = TRUE;
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
                    if (YoriShGlobal.ErrorLevel != 0) {
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
                    if (YoriShGlobal.ErrorLevel == 0) {
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
__success(return)
BOOL
YoriShExecuteExpressionAndCaptureOutput(
    __in PYORI_STRING Expression,
    __out PYORI_STRING ProcessOutput
    )
{
    YORI_LIBSH_EXEC_PLAN ExecPlan;
    PYORI_LIBSH_SINGLE_EXEC_CONTEXT ExecContext;
    YORI_LIBSH_CMD_CONTEXT CmdContext;
    PVOID OutputBuffer;
    DWORD Index;

    //
    //  Parse the expression we're trying to execute.
    //

    if (!YoriLibShParseCmdlineToCmdContext(Expression, 0, &CmdContext)) {
        YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("Parse error\n"));
        return FALSE;
    }

    if (CmdContext.ArgC == 0) {
        YoriLibShFreeCmdContext(&CmdContext);
        return FALSE;
    }

    if (!YoriShExpandEnvironmentInCmdContext(&CmdContext)) {
        YoriLibShFreeCmdContext(&CmdContext);
        return FALSE;
    }

    if (!YoriLibShParseCmdContextToExecPlan(&CmdContext, &ExecPlan, NULL, NULL, NULL, NULL)) {
        YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("Parse error\n"));
        YoriLibShFreeCmdContext(&CmdContext);
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

        if (!YoriLibShGetProcessOutputBuffer(OutputBuffer, ProcessOutput)) {
            YoriLibInitEmptyString(ProcessOutput);
        }

        //
        //  Truncate any newlines from the output, which tools
        //  frequently emit but are of no value here
        //

        while (ProcessOutput->LengthInChars > 0 &&
               (ProcessOutput->StartOfString[ProcessOutput->LengthInChars - 1] == '\n' ||
                ProcessOutput->StartOfString[ProcessOutput->LengthInChars - 1] == '\r')) {

            ProcessOutput->LengthInChars--;
        }

        //
        //  Convert any remaining newlines to spaces
        //

        for (Index = 0; Index < ProcessOutput->LengthInChars; Index++) {
            if ((ProcessOutput->StartOfString[Index] == '\n' ||
                 ProcessOutput->StartOfString[Index] == '\r')) {

                ProcessOutput->StartOfString[Index] = ' ';
            }
        }
    }

    YoriLibShFreeExecPlan(&ExecPlan);
    YoriLibShFreeCmdContext(&CmdContext);

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
__success(return)
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

    DWORD CharsInBackquotePrefix;

    YoriLibInitEmptyString(&CurrentFullExpression);
    CurrentFullExpression.StartOfString = Expression->StartOfString;
    CurrentFullExpression.LengthInChars = Expression->LengthInChars;

    while(TRUE) {

        //
        //  MSFIX This logic currently rescans from the beginning.  Should
        //  we only rescan from the end of the previous scan so we don't
        //  create commands that can nest further backticks?
        //

        if (!YoriLibShFindNextBackquoteSubstring(&CurrentFullExpression, &CurrentExpressionSubset, &CharsInBackquotePrefix)) {
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
        InitialPortion.LengthInChars = (DWORD)(CurrentExpressionSubset.StartOfString - CurrentFullExpression.StartOfString - CharsInBackquotePrefix);

        TrailingPortion.StartOfString = &CurrentFullExpression.StartOfString[InitialPortion.LengthInChars + CurrentExpressionSubset.LengthInChars + 1 + CharsInBackquotePrefix];
        TrailingPortion.LengthInChars = CurrentFullExpression.LengthInChars - InitialPortion.LengthInChars - CurrentExpressionSubset.LengthInChars - 1 - CharsInBackquotePrefix;

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
__success(return)
BOOL
YoriShExecuteExpression(
    __in PYORI_STRING Expression
    )
{
    YORI_LIBSH_EXEC_PLAN ExecPlan;
    YORI_LIBSH_CMD_CONTEXT CmdContext;
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

    if (!YoriLibShParseCmdlineToCmdContext(&CurrentFullExpression, 0, &CmdContext)) {
        YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("Parse error\n"));
        YoriLibFreeStringContents(&CurrentFullExpression);
        return FALSE;
    }

    if (CmdContext.ArgC == 0) {
        YoriLibShFreeCmdContext(&CmdContext);
        YoriLibFreeStringContents(&CurrentFullExpression);
        return FALSE;
    }

    if (!YoriShExpandEnvironmentInCmdContext(&CmdContext)) {
        YoriLibShFreeCmdContext(&CmdContext);
        YoriLibFreeStringContents(&CurrentFullExpression);
        return FALSE;
    }

    if (!YoriLibShParseCmdContextToExecPlan(&CmdContext, &ExecPlan, NULL, NULL, NULL, NULL)) {
        YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("Parse error\n"));
        YoriLibFreeStringContents(&CurrentFullExpression);
        YoriLibShFreeCmdContext(&CmdContext);
        return FALSE;
    }

    YoriShExecExecPlan(&ExecPlan, NULL);

    YoriLibShFreeExecPlan(&ExecPlan);
    YoriLibShFreeCmdContext(&CmdContext);

    YoriLibFreeStringContents(&CurrentFullExpression);

    return TRUE;
}

// vim:sw=4:ts=4:et:
