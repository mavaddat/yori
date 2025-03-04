/**
 * @file lib/dyld.c
 *
 * Yori dynamically loaded OS function support
 *
 * Copyright (c) 2018-2021 Malcolm J. Smith
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
 Map a function name to a location in memory to store a function pointer.
 */
typedef struct _YORI_DLL_NAME_MAP {

    /**
     Pointer to a memory location to be updated with a function pointer when
     the specified function is resolved.
     */
    FARPROC * FnPtr;

    /**
     The name of the function to resolve.
     */
    LPCSTR FnName;
} YORI_DLL_NAME_MAP, *PYORI_DLL_NAME_MAP;

/**
 Load a DLL from the System32 directory.

 @param DllName Pointer to the name of the DLL to load.

 @return Module to the DLL, or NULL on failure.
 */
HMODULE
YoriLibLoadLibraryFromSystemDirectory(
    __in LPCTSTR DllName
    )
{
    DWORD LengthRequired;
    YORI_STRING YsDllName;
    YORI_STRING FullPath;
    HMODULE DllModule;

    YoriLibConstantString(&YsDllName, DllName);
    LengthRequired = GetSystemDirectory(NULL, 0);

    if (!YoriLibAllocateString(&FullPath, LengthRequired + 1 + YsDllName.LengthInChars + 1)) {
        return NULL;
    }

    FullPath.LengthInChars = GetSystemDirectory(FullPath.StartOfString, LengthRequired);
    if (FullPath.LengthInChars == 0) {
        YoriLibFreeStringContents(&FullPath);
        return NULL;
    }

    FullPath.LengthInChars += YoriLibSPrintf(&FullPath.StartOfString[FullPath.LengthInChars], _T("\\%y"), &YsDllName);

    DllModule = LoadLibrary(FullPath.StartOfString);

    YoriLibFreeStringContents(&FullPath);
    return DllModule;
}

/**
 A structure containing pointers to ntdll.dll functions that can be used if
 they are found but programs do not have a hard dependency on.
 */
YORI_NTDLL_FUNCTIONS DllNtDll;

/**
 Load pointers to all optional ntdll.dll functions.  Because ntdll.dll is
 effectively mandatory in any Win32 process, this uses GetModuleHandle rather
 than LoadLibrary and pointers are valid for the lifetime of the process.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOL
YoriLibLoadNtDllFunctions(VOID)
{
    if (DllNtDll.hDll != NULL) {
        return TRUE;
    }

    DllNtDll.hDll = GetModuleHandle(_T("NTDLL"));
    if (DllNtDll.hDll == NULL) {
        return FALSE;
    }
    DllNtDll.pNtQueryInformationFile = (PNT_QUERY_INFORMATION_FILE)GetProcAddress(DllNtDll.hDll, "NtQueryInformationFile");
    DllNtDll.pNtQueryInformationProcess = (PNT_QUERY_INFORMATION_PROCESS)GetProcAddress(DllNtDll.hDll, "NtQueryInformationProcess");
    DllNtDll.pNtQueryInformationThread = (PNT_QUERY_INFORMATION_THREAD)GetProcAddress(DllNtDll.hDll, "NtQueryInformationThread");
    DllNtDll.pNtQueryObject = (PNT_QUERY_OBJECT)GetProcAddress(DllNtDll.hDll, "NtQueryObject");
    DllNtDll.pNtQuerySystemInformation = (PNT_QUERY_SYSTEM_INFORMATION)GetProcAddress(DllNtDll.hDll, "NtQuerySystemInformation");
    DllNtDll.pNtSetInformationFile = (PNT_SET_INFORMATION_FILE)GetProcAddress(DllNtDll.hDll, "NtSetInformationFile");
    DllNtDll.pNtSystemDebugControl = (PNT_SYSTEM_DEBUG_CONTROL)GetProcAddress(DllNtDll.hDll, "NtSystemDebugControl");
    DllNtDll.pRtlGetLastNtStatus = (PRTL_GET_LAST_NT_STATUS)GetProcAddress(DllNtDll.hDll, "RtlGetLastNtStatus");
    return TRUE;
}

/**
 A structure containing pointers to kernel32.dll functions that can be used if
 they are found but programs do not have a hard dependency on.
 */
YORI_KERNEL32_FUNCTIONS DllKernel32;

/**
 The set of functions to resolve from kernel32.dll.
 */
CONST YORI_DLL_NAME_MAP DllKernel32Symbols[] = {
    {(FARPROC *)&DllKernel32.pAddConsoleAliasW, "AddConsoleAliasW"},
    {(FARPROC *)&DllKernel32.pAssignProcessToJobObject, "AssignProcessToJobObject"},
    {(FARPROC *)&DllKernel32.pCreateHardLinkW, "CreateHardLinkW"},
    {(FARPROC *)&DllKernel32.pCreateJobObjectW, "CreateJobObjectW"},
    {(FARPROC *)&DllKernel32.pCreateSymbolicLinkW, "CreateSymbolicLinkW"},
    {(FARPROC *)&DllKernel32.pFindFirstStreamW, "FindFirstStreamW"},
    {(FARPROC *)&DllKernel32.pFindFirstVolumeW, "FindFirstVolumeW"},
    {(FARPROC *)&DllKernel32.pFindNextStreamW, "FindNextStreamW"},
    {(FARPROC *)&DllKernel32.pFindNextVolumeW, "FindNextVolumeW"},
    {(FARPROC *)&DllKernel32.pFindVolumeClose, "FindVolumeClose"},
    {(FARPROC *)&DllKernel32.pFreeEnvironmentStringsW, "FreeEnvironmentStringsW"},
    {(FARPROC *)&DllKernel32.pGetCompressedFileSizeW, "GetCompressedFileSizeW"},
    {(FARPROC *)&DllKernel32.pGetConsoleAliasesLengthW, "GetConsoleAliasesLengthW"},
    {(FARPROC *)&DllKernel32.pGetConsoleAliasesW, "GetConsoleAliasesW"},
    {(FARPROC *)&DllKernel32.pGetConsoleScreenBufferInfoEx, "GetConsoleScreenBufferInfoEx"},
    {(FARPROC *)&DllKernel32.pGetConsoleProcessList, "GetConsoleProcessList"},
    {(FARPROC *)&DllKernel32.pGetConsoleWindow, "GetConsoleWindow"},
    {(FARPROC *)&DllKernel32.pGetCurrentConsoleFontEx, "GetCurrentConsoleFontEx"},
    {(FARPROC *)&DllKernel32.pGetDiskFreeSpaceExW, "GetDiskFreeSpaceExW"},
    {(FARPROC *)&DllKernel32.pGetEnvironmentStrings, "GetEnvironmentStrings"},
    {(FARPROC *)&DllKernel32.pGetEnvironmentStringsW, "GetEnvironmentStringsW"},
    {(FARPROC *)&DllKernel32.pGetFileInformationByHandleEx, "GetFileInformationByHandleEx"},
    {(FARPROC *)&DllKernel32.pGetFinalPathNameByHandleW, "GetFinalPathNameByHandleW"},
    {(FARPROC *)&DllKernel32.pGetLogicalProcessorInformation, "GetLogicalProcessorInformation"},
    {(FARPROC *)&DllKernel32.pGetLogicalProcessorInformationEx, "GetLogicalProcessorInformationEx"},
    {(FARPROC *)&DllKernel32.pGetNativeSystemInfo, "GetNativeSystemInfo"},
    {(FARPROC *)&DllKernel32.pGetPrivateProfileSectionNamesW, "GetPrivateProfileSectionNamesW"},
    {(FARPROC *)&DllKernel32.pGetProcessIoCounters, "GetProcessIoCounters"},
    {(FARPROC *)&DllKernel32.pGetProductInfo, "GetProductInfo"},
    {(FARPROC *)&DllKernel32.pGetTickCount64, "GetTickCount64"},
    {(FARPROC *)&DllKernel32.pGetVersionExW, "GetVersionExW"},
    {(FARPROC *)&DllKernel32.pGetVolumePathNamesForVolumeNameW, "GetVolumePathNamesForVolumeNameW"},
    {(FARPROC *)&DllKernel32.pGetVolumePathNameW, "GetVolumePathNameW"},
    {(FARPROC *)&DllKernel32.pGlobalMemoryStatusEx, "GlobalMemoryStatusEx"},
    {(FARPROC *)&DllKernel32.pInterlockedCompareExchange, "InterlockedCompareExchange"},
    {(FARPROC *)&DllKernel32.pIsWow64Process, "IsWow64Process"},
    {(FARPROC *)&DllKernel32.pIsWow64Process2, "IsWow64Process2"},
    {(FARPROC *)&DllKernel32.pOpenThread, "OpenThread"},
    {(FARPROC *)&DllKernel32.pQueryFullProcessImageNameW, "QueryFullProcessImageNameW"},
    {(FARPROC *)&DllKernel32.pQueryInformationJobObject, "QueryInformationJobObject"},
    {(FARPROC *)&DllKernel32.pRegisterApplicationRestart, "RegisterApplicationRestart"},
    {(FARPROC *)&DllKernel32.pReplaceFileW, "ReplaceFileW"},
    {(FARPROC *)&DllKernel32.pRtlCaptureStackBackTrace, "RtlCaptureStackBackTrace"},
    {(FARPROC *)&DllKernel32.pSetConsoleScreenBufferInfoEx, "SetConsoleScreenBufferInfoEx"},
    {(FARPROC *)&DllKernel32.pSetCurrentConsoleFontEx, "SetCurrentConsoleFontEx"},
    {(FARPROC *)&DllKernel32.pSetFileInformationByHandle, "SetFileInformationByHandle"},
    {(FARPROC *)&DllKernel32.pSetInformationJobObject, "SetInformationJobObject"},
    {(FARPROC *)&DllKernel32.pWow64DisableWow64FsRedirection, "Wow64DisableWow64FsRedirection"},
    {(FARPROC *)&DllKernel32.pWow64GetThreadContext, "Wow64GetThreadContext"},
    {(FARPROC *)&DllKernel32.pWow64SetThreadContext, "Wow64SetThreadContext"},
};


/**
 Load pointers to all optional kernel32.dll functions.  Because kernel32.dll is
 effectively mandatory in any Win32 process, this uses GetModuleHandle rather
 than LoadLibrary and pointers are valid for the lifetime of the process.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOL
YoriLibLoadKernel32Functions(VOID)
{
    DWORD Count;
    if (DllKernel32.hDll != NULL) {
        return TRUE;
    }

    DllKernel32.hDll = GetModuleHandle(_T("KERNEL32"));
    if (DllKernel32.hDll == NULL) {
        return FALSE;
    }

    for (Count = 0; Count < sizeof(DllKernel32Symbols)/sizeof(DllKernel32Symbols[0]); Count++) {
        *(DllKernel32Symbols[Count].FnPtr) = GetProcAddress(DllKernel32.hDll, DllKernel32Symbols[Count].FnName);
    }

    return TRUE;
}

/**
 A structure containing pointers to ctl3d32.dll functions that can be used if
 they are found but programs do not have a hard dependency on.
 */
YORI_CTL3D_FUNCTIONS DllCtl3d;

/**
 Load pointers to all optional Ctl3d32.dll functions.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOL
YoriLibLoadCtl3d32Functions(VOID)
{

    if (DllCtl3d.hDll != NULL) {
        return TRUE;
    }

    DllCtl3d.hDll = YoriLibLoadLibraryFromSystemDirectory(_T("CTL3D32.DLL"));
    if (DllCtl3d.hDll == NULL) {
        return FALSE;
    }

    DllCtl3d.pCtl3dAutoSubclass = (PCTL3D_AUTOSUBCLASS)GetProcAddress(DllCtl3d.hDll, "Ctl3dAutoSubclass");
    DllCtl3d.pCtl3dRegister = (PCTL3D_REGISTER)GetProcAddress(DllCtl3d.hDll, "Ctl3dRegister");

    return TRUE;
}

/**
 A structure containing pointers to dbghelp.dll functions that can be used if
 they are found but programs do not have a hard dependency on.
 */
YORI_DBGHELP_FUNCTIONS DllDbgHelp;

/**
 Load pointers to all optional dbghelp.dll functions.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOL
YoriLibLoadDbgHelpFunctions(VOID)
{

    if (DllDbgHelp.hDll != NULL) {
        return TRUE;
    }

    DllDbgHelp.hDll = YoriLibLoadLibraryFromSystemDirectory(_T("DBGHELP.DLL"));
    if (DllDbgHelp.hDll == NULL) {
        return FALSE;
    }

    DllDbgHelp.pMiniDumpWriteDump = (PMINI_DUMP_WRITE_DUMP)GetProcAddress(DllDbgHelp.hDll, "MiniDumpWriteDump");

    return TRUE;
}

/**
 A structure containing pointers to imagehlp.dll functions that can be used if
 they are found but programs do not have a hard dependency on.
 */
YORI_IMAGEHLP_FUNCTIONS DllImageHlp;

/**
 Load pointers to all optional imagehlp.dll functions.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOL
YoriLibLoadImageHlpFunctions(VOID)
{

    if (DllImageHlp.hDll != NULL) {
        return TRUE;
    }

    DllImageHlp.hDll = YoriLibLoadLibraryFromSystemDirectory(_T("IMAGEHLP.DLL"));
    if (DllImageHlp.hDll == NULL) {
        return FALSE;
    }

    DllImageHlp.pMapFileAndCheckSumW = (PMAP_FILE_AND_CHECKSUMW)GetProcAddress(DllImageHlp.hDll, "MapFileAndCheckSumW");

    return TRUE;
}

/**
 A structure containing pointers to ole32.dll functions that can be used if
 they are found but programs do not have a hard dependency on.
 */
YORI_OLE32_FUNCTIONS DllOle32;

/**
 Load pointers to all optional ole32.dll functions.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOL
YoriLibLoadOle32Functions(VOID)
{

    if (DllOle32.hDll != NULL) {
        return TRUE;
    }

    DllOle32.hDll = YoriLibLoadLibraryFromSystemDirectory(_T("OLE32.DLL"));
    if (DllOle32.hDll == NULL) {
        return FALSE;
    }

    DllOle32.pCoCreateInstance = (PCO_CREATE_INSTANCE)GetProcAddress(DllOle32.hDll, "CoCreateInstance");
    DllOle32.pCoInitialize = (PCO_INITIALIZE)GetProcAddress(DllOle32.hDll, "CoInitialize");
    DllOle32.pCoTaskMemFree = (PCO_TASK_MEM_FREE)GetProcAddress(DllOle32.hDll, "CoTaskMemFree");

    return TRUE;
}

/**
 A structure containing pointers to psapi.dll functions that can be used if
 they are found but programs do not have a hard dependency on.
 */
YORI_PSAPI_FUNCTIONS DllPsapi;

/**
 Load pointers to all optional psapi.dll functions.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOL
YoriLibLoadPsapiFunctions(VOID)
{

    if (DllPsapi.hDll != NULL) {
        return TRUE;
    }

    DllPsapi.hDll = YoriLibLoadLibraryFromSystemDirectory(_T("PSAPI.DLL"));
    if (DllPsapi.hDll == NULL) {
        return FALSE;
    }

    DllPsapi.pGetModuleFileNameExW = (PGET_MODULE_FILE_NAME_EXW)GetProcAddress(DllPsapi.hDll, "GetModuleFileNameExW");

    return TRUE;
}

/**
 GUID to fetch the downloads folder on Vista+.
 */
const GUID FOLDERID_Downloads = { 0x374de290, 0x123f, 0x4565, { 0x91, 0x64, 0x39, 0xc4, 0x92, 0x5e, 0x46, 0x7b } };

/**
 A structure containing pointers to shell32.dll functions that can be used if
 they are found but programs do not have a hard dependency on.
 */
YORI_SHELL32_FUNCTIONS DllShell32;

/**
 Load pointers to all optional shell32.dll functions.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOL
YoriLibLoadShell32Functions(VOID)
{

    if (DllShell32.hDll != NULL) {
        return TRUE;
    }

    DllShell32.hDll = YoriLibLoadLibraryFromSystemDirectory(_T("SHELL32.DLL"));
    if (DllShell32.hDll == NULL) {
        return FALSE;
    }

    DllShell32.pSHAppBarMessage = (PSH_APP_BAR_MESSAGE)GetProcAddress(DllShell32.hDll, "SHAppBarMessage");
    DllShell32.pSHBrowseForFolderW = (PSH_BROWSE_FOR_FOLDERW)GetProcAddress(DllShell32.hDll, "SHBrowseForFolderW");
    DllShell32.pSHFileOperationW = (PSH_FILE_OPERATIONW)GetProcAddress(DllShell32.hDll, "SHFileOperationW");
    DllShell32.pSHGetKnownFolderPath = (PSH_GET_KNOWN_FOLDER_PATH)GetProcAddress(DllShell32.hDll, "SHGetKnownFolderPath");
    DllShell32.pSHGetPathFromIDListW = (PSH_GET_PATH_FROM_ID_LISTW)GetProcAddress(DllShell32.hDll, "SHGetPathFromIDListW");
    DllShell32.pSHGetSpecialFolderPathW = (PSH_GET_SPECIAL_FOLDER_PATHW)GetProcAddress(DllShell32.hDll, "SHGetSpecialFolderPathW");
    DllShell32.pShellExecuteExW = (PSHELL_EXECUTE_EXW)GetProcAddress(DllShell32.hDll, "ShellExecuteExW");
    DllShell32.pShellExecuteW = (PSHELL_EXECUTEW)GetProcAddress(DllShell32.hDll, "ShellExecuteW");
    return TRUE;
}

/**
 A structure containing pointers to shfolder.dll functions that can be used if
 they are found but programs do not have a hard dependency on.
 */
YORI_SHFOLDER_FUNCTIONS DllShfolder;

/**
 Load pointers to all optional shfolder.dll functions.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOL
YoriLibLoadShfolderFunctions(VOID)
{

    if (DllShfolder.hDll != NULL) {
        return TRUE;
    }

    DllShfolder.hDll = YoriLibLoadLibraryFromSystemDirectory(_T("SHFOLDER.DLL"));
    if (DllShfolder.hDll == NULL) {
        return FALSE;
    }

    DllShfolder.pSHGetFolderPathW = (PSH_GET_FOLDER_PATHW)GetProcAddress(DllShfolder.hDll, "SHGetFolderPathW");
    return TRUE;
}

/**
 A structure containing pointers to user32.dll functions that can be used if
 they are found but programs do not have a hard dependency on.
 */
YORI_USER32_FUNCTIONS DllUser32;

/**
 Load pointers to all optional user32.dll functions.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOL
YoriLibLoadUser32Functions(VOID)
{
    if (DllUser32.hDll != NULL) {
        return TRUE;
    }

    DllUser32.hDll = YoriLibLoadLibraryFromSystemDirectory(_T("USER32.DLL"));
    if (DllUser32.hDll == NULL) {
        return FALSE;
    }

    DllUser32.pCascadeWindows = (PCASCADE_WINDOWS)GetProcAddress(DllUser32.hDll, "CascadeWindows");
    DllUser32.pCloseClipboard = (PCLOSE_CLIPBOARD)GetProcAddress(DllUser32.hDll, "CloseClipboard");
    DllUser32.pDdeClientTransaction = (PDDE_CLIENT_TRANSACTION)GetProcAddress(DllUser32.hDll, "DdeClientTransaction");
    DllUser32.pDdeConnect = (PDDE_CONNECT)GetProcAddress(DllUser32.hDll, "DdeConnect");
    DllUser32.pDdeCreateDataHandle = (PDDE_CREATE_DATA_HANDLE)GetProcAddress(DllUser32.hDll, "DdeCreateDataHandle");
    DllUser32.pDdeCreateStringHandleW = (PDDE_CREATE_STRING_HANDLEW)GetProcAddress(DllUser32.hDll, "DdeCreateStringHandleW");
    DllUser32.pDdeDisconnect = (PDDE_DISCONNECT)GetProcAddress(DllUser32.hDll, "DdeDisconnect");
    DllUser32.pDdeFreeStringHandle = (PDDE_FREE_STRING_HANDLE)GetProcAddress(DllUser32.hDll, "DdeFreeStringHandle");
    DllUser32.pDdeInitializeW = (PDDE_INITIALIZEW)GetProcAddress(DllUser32.hDll, "DdeInitializeW");
    DllUser32.pDdeUninitialize = (PDDE_UNINITIALIZE)GetProcAddress(DllUser32.hDll, "DdeUninitialize");
    DllUser32.pEmptyClipboard = (PEMPTY_CLIPBOARD)GetProcAddress(DllUser32.hDll, "EmptyClipboard");
    DllUser32.pEnumClipboardFormats = (PENUM_CLIPBOARD_FORMATS)GetProcAddress(DllUser32.hDll, "EnumClipboardFormats");
    DllUser32.pExitWindowsEx = (PEXIT_WINDOWS_EX)GetProcAddress(DllUser32.hDll, "ExitWindowsEx");
    DllUser32.pFindWindowW = (PFIND_WINDOWW)GetProcAddress(DllUser32.hDll, "FindWindowW");
    DllUser32.pGetClipboardData = (PGET_CLIPBOARD_DATA)GetProcAddress(DllUser32.hDll, "GetClipboardData");
    DllUser32.pGetClipboardFormatNameW = (PGET_CLIPBOARD_FORMAT_NAMEW)GetProcAddress(DllUser32.hDll, "GetClipboardFormatNameW");
    DllUser32.pGetClientRect = (PGET_CLIENT_RECT)GetProcAddress(DllUser32.hDll, "GetClientRect");
    DllUser32.pGetDesktopWindow = (PGET_DESKTOP_WINDOW)GetProcAddress(DllUser32.hDll, "GetDesktopWindow");
    DllUser32.pGetKeyboardLayout = (PGET_KEYBOARD_LAYOUT)GetProcAddress(DllUser32.hDll, "GetKeyboardLayout");
    DllUser32.pGetWindowRect = (PGET_WINDOW_RECT)GetProcAddress(DllUser32.hDll, "GetWindowRect");
    DllUser32.pLockWorkStation = (PLOCK_WORKSTATION)GetProcAddress(DllUser32.hDll, "LockWorkStation");
    DllUser32.pMoveWindow = (PMOVE_WINDOW)GetProcAddress(DllUser32.hDll, "MoveWindow");
    DllUser32.pOpenClipboard = (POPEN_CLIPBOARD)GetProcAddress(DllUser32.hDll, "OpenClipboard");
    DllUser32.pRegisterClipboardFormatW = (PREGISTER_CLIPBOARD_FORMATW)GetProcAddress(DllUser32.hDll, "RegisterClipboardFormatW");
    DllUser32.pRegisterShellHookWindow = (PREGISTER_SHELL_HOOK_WINDOW)GetProcAddress(DllUser32.hDll, "RegisterShellHookWindow");
    DllUser32.pSendMessageTimeoutW = (PSEND_MESSAGE_TIMEOUTW)GetProcAddress(DllUser32.hDll, "SendMessageTimeoutW");
    DllUser32.pSetClipboardData = (PSET_CLIPBOARD_DATA)GetProcAddress(DllUser32.hDll, "SetClipboardData");
    DllUser32.pSetForegroundWindow = (PSET_FOREGROUND_WINDOW)GetProcAddress(DllUser32.hDll, "SetForegroundWindow");
    DllUser32.pSetWindowTextW = (PSET_WINDOW_TEXTW)GetProcAddress(DllUser32.hDll, "SetWindowTextW");
    DllUser32.pShowWindow = (PSHOW_WINDOW)GetProcAddress(DllUser32.hDll, "ShowWindow");
    DllUser32.pTileWindows = (PTILE_WINDOWS)GetProcAddress(DllUser32.hDll, "TileWindows");

    return TRUE;
}

/**
 A structure containing pointers to version.dll functions that can be used if
 they are found but programs do not have a hard dependency on.
 */
YORI_VERSION_FUNCTIONS DllVersion;

/**
 Load pointers to all optional version.dll functions.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOL
YoriLibLoadVersionFunctions(VOID)
{

    if (DllVersion.hDll != NULL) {
        return TRUE;
    }

    DllVersion.hDll = YoriLibLoadLibraryFromSystemDirectory(_T("VERSION.DLL"));
    if (DllVersion.hDll == NULL) {
        return FALSE;
    }

    DllVersion.pGetFileVersionInfoSizeW = (PGET_FILE_VERSION_INFO_SIZEW)GetProcAddress(DllVersion.hDll, "GetFileVersionInfoSizeW");
    DllVersion.pGetFileVersionInfoW = (PGET_FILE_VERSION_INFOW)GetProcAddress(DllVersion.hDll, "GetFileVersionInfoW");
    DllVersion.pVerQueryValueW = (PVER_QUERY_VALUEW)GetProcAddress(DllVersion.hDll, "VerQueryValueW");

    return TRUE;
}

/**
 GUID for an unknown virtual storage implementations.
 */
const GUID VIRTUAL_STORAGE_TYPE_VENDOR_UNKNOWN = { 0x00000000, 0x0000, 0x0000, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } };

/**
 GUID for Microsoft provided virtual storage implementations.
 */
const GUID VIRTUAL_STORAGE_TYPE_VENDOR_MICROSOFT = { 0xec984aec, 0xa0f9, 0x47e9, { 0x90, 0x1f, 0x71, 0x41, 0x5a, 0x66, 0x34, 0x5b } };

/**
 A structure containing pointers to virtdisk.dll functions that can be used if
 they are found but programs do not have a hard dependency on.
 */
YORI_VIRTDISK_FUNCTIONS DllVirtDisk;

/**
 Load pointers to all optional virtdisk.dll functions.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOL
YoriLibLoadVirtDiskFunctions(VOID)
{

    if (DllVirtDisk.hDll != NULL) {
        return TRUE;
    }

    DllVirtDisk.hDll = YoriLibLoadLibraryFromSystemDirectory(_T("VIRTDISK.DLL"));
    if (DllVirtDisk.hDll == NULL) {
        return FALSE;
    }

    DllVirtDisk.pAttachVirtualDisk = (PATTACH_VIRTUAL_DISK)GetProcAddress(DllVirtDisk.hDll, "AttachVirtualDisk");
    DllVirtDisk.pCompactVirtualDisk = (PCOMPACT_VIRTUAL_DISK)GetProcAddress(DllVirtDisk.hDll, "CompactVirtualDisk");
    DllVirtDisk.pCreateVirtualDisk = (PCREATE_VIRTUAL_DISK)GetProcAddress(DllVirtDisk.hDll, "CreateVirtualDisk");
    DllVirtDisk.pDetachVirtualDisk = (PDETACH_VIRTUAL_DISK)GetProcAddress(DllVirtDisk.hDll, "DetachVirtualDisk");
    DllVirtDisk.pExpandVirtualDisk = (PEXPAND_VIRTUAL_DISK)GetProcAddress(DllVirtDisk.hDll, "ExpandVirtualDisk");
    DllVirtDisk.pGetVirtualDiskPhysicalPath = (PGET_VIRTUAL_DISK_PHYSICAL_PATH)GetProcAddress(DllVirtDisk.hDll, "GetVirtualDiskPhysicalPath");
    DllVirtDisk.pOpenVirtualDisk = (POPEN_VIRTUAL_DISK)GetProcAddress(DllVirtDisk.hDll, "OpenVirtualDisk");
    DllVirtDisk.pMergeVirtualDisk = (PMERGE_VIRTUAL_DISK)GetProcAddress(DllVirtDisk.hDll, "MergeVirtualDisk");
    DllVirtDisk.pResizeVirtualDisk = (PRESIZE_VIRTUAL_DISK)GetProcAddress(DllVirtDisk.hDll, "ResizeVirtualDisk");

    return TRUE;
}

/**
 A structure containing pointers to winbrand.dll functions that can be used if
 they are found but programs do not have a hard dependency on.
 */
YORI_WINBRAND_FUNCTIONS DllWinBrand;

/**
 Load pointers to all optional WinBrand.dll functions.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOL
YoriLibLoadWinBrandFunctions(VOID)
{

    if (DllWinBrand.hDll != NULL) {
        return TRUE;
    }

    DllWinBrand.hDll = YoriLibLoadLibraryFromSystemDirectory(_T("WINBRAND.DLL"));
    if (DllWinBrand.hDll == NULL) {
        return FALSE;
    }

    DllWinBrand.pBrandingFormatString = (PBRANDING_FORMAT_STRING)GetProcAddress(DllWinBrand.hDll, "BrandingFormatString");
    return TRUE;
}

/**
 A structure containing pointers to wtsapi32.dll functions that can be used if
 they are found but programs do not have a hard dependency on.
 */
YORI_WTSAPI32_FUNCTIONS DllWtsApi32;

/**
 Load pointers to all optional WtsApi32.dll functions.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOL
YoriLibLoadWtsApi32Functions(VOID)
{

    if (DllWtsApi32.hDll != NULL) {
        return TRUE;
    }

    DllWtsApi32.hDll = YoriLibLoadLibraryFromSystemDirectory(_T("WTSAPI32.DLL"));
    if (DllWtsApi32.hDll == NULL) {
        return FALSE;
    }

    DllWtsApi32.pWTSDisconnectSession = (PWTS_DISCONNECT_SESSION)GetProcAddress(DllWtsApi32.hDll, "WTSDisconnectSession");

    return TRUE;
}


// vim:sw=4:ts=4:et:
