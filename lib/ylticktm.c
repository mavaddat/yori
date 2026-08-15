/**
 * @file lib/ylticktm.c
 *
 * Yori tick time routines
 *
 * Copyright (c) 2026 Malcolm J. Smith
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
 Assign one TickTime to another.
 
 @param TickTime1 The tick time to assign to.

 @param TickTime2 The tick time to assign from.
 */
VOID
YoriLibAssignTickTime(
    __out PYORI_TICK_TIME_T TickTime1,
    __in PYORI_TICK_TIME_T TickTime2
    )
{
#if _WIN32
    LONGLONG Temp;
    Temp = *TickTime2;
    *TickTime1 = Temp;
#else
    TickTime1->HighPart = TickTime2->HighPart;
    TickTime1->LowPart = TickTime2->LowPart;
#endif
}

/**
 Assign milliseconds to a TickTime.
 
 @param TickTime The tick time to assign to.

 @param TimeInMs The number of milliseconds to assign.
 */
VOID
YoriLibAssignMsToTickTime(
    __out PYORI_TICK_TIME_T TickTime,
    __in DWORD TimeInMs
    )
{
#if _WIN32
    LONGLONG Temp;
    Temp = TimeInMs;
    *TickTime = Temp * 1000L * 10L;
#else
    TickTime->HighPart = TimeInMs / 1000L;
    TickTime->LowPart = TimeInMs % 1000L;
#endif
}

/**
 Multiply a TickTime by a count.
 
 @param TickTime The tick time to multiply.

 @param Count The number to multiply by.
 */
VOID
YoriLibMultiplyTickTime(
    __inout PYORI_TICK_TIME_T TickTime,
    __in DWORD Count
    )
{
#if _WIN32
    LONGLONG llTickTime;
    llTickTime = *TickTime;
    *TickTime = llTickTime * Count;
#else
    DWORD LowPart;

    //
    //  Note this is lossy and would be better with "real" 64 bit math
    //
    LowPart = TickTime->LowPart * Count;
    TickTime->LowPart = LowPart % 1000L;
    TickTime->HighPart = (TickTime->HighPart * Count) + (LowPart / 1000L);
#endif
}

/**
 Add two TickTime values.
 
 @param TickTime1 A tick time to be added and contain the result of the
        addition.

 @param TickTime2 A tick time to add to TickTime1.
 */
VOID
YoriLibAddTickTime(
    __inout PYORI_TICK_TIME_T TickTime1,
    __in PYORI_TICK_TIME_T TickTime2
    )
{
#if _WIN32
    LONGLONG llTickTime;
    llTickTime = *TickTime1;
    *TickTime1 = llTickTime + (*TickTime2);
#else
    DWORD LowPart;

    LowPart = TickTime1->LowPart * TickTime2->LowPart;
    TickTime1->LowPart = LowPart % 1000L;
    TickTime1->HighPart = TickTime1->HighPart + TickTime2->HighPart + (LowPart / 1000L);
#endif
}

/**
 Return the difference between two TickTimes in milliseconds.

 @param TickTime1 A tick time which is assumed to be the most recent of the
        two.

 @param TickTime2 A tick time which is assumed to be the least recent of the
        two.

 @return The difference between the two, in milliseconds.  If TickTime2 is
         more recent than TickTime1, the result is negative.
 */
LONG
YoriLibTickTimeDifferenceInMs(
    __in PYORI_TICK_TIME_T TickTime1,
    __in PYORI_TICK_TIME_T TickTime2
    )
{
#if _WIN32
    LONGLONG llTickTime;
    llTickTime = *TickTime1 - *TickTime2;
    return (DWORD)(llTickTime / 1000 / 10);
#else
    LONG Seconds;
    LONG MilliSeconds;

    Seconds = TickTime1->HighPart - TickTime2->HighPart;
    MilliSeconds = TickTime1->LowPart - TickTime2->LowPart;

    return Seconds * 1000L + MilliSeconds;
#endif
}

/**
 Return if the first TickTime is less than the second.

 @param TickTime1 The first tick time to compare.

 @param TickTime2 The second tick time to compare.

 @return TRUE if TickTime1 is less than TickTime2.  Otherwise, returns FALSE.
 */
BOOLEAN
YoriLibTickTimeLessThan(
    __in PYORI_TICK_TIME_T TickTime1,
    __in PYORI_TICK_TIME_T TickTime2
    )
{
#if _WIN32
    if (*TickTime1 < *TickTime2) {
        return TRUE;
    }
    return FALSE;
#else
    if (TickTime1->HighPart < TickTime2->HighPart) {
        return TRUE;
    }

    if (TickTime1->HighPart == TickTime2->HighPart &&
        TickTime1->LowPart < TickTime2->LowPart) {
        return TRUE;
    }
    return FALSE;
#endif
}

/**
 Return the current system time in ticks.

 @param TickTime On successful completion, contains the system time in ticks.
 */
VOID
YoriLibGetSystemTimeAsTicks(
    __out PYORI_TICK_TIME_T TickTime
    )
{
    SYSTEMTIME CurrentSystemTime;
    FILETIME CurrentFileTime;
    GetSystemTime(&CurrentSystemTime);
    if (!SystemTimeToFileTime(&CurrentSystemTime, &CurrentFileTime)) {
        CurrentFileTime.dwLowDateTime = 0L;
        CurrentFileTime.dwHighDateTime = 0L;
    }
#if _WIN32
    {
        LARGE_INTEGER liTemp;
        liTemp.LowPart = CurrentFileTime.dwLowDateTime;
        liTemp.HighPart = CurrentFileTime.dwHighDateTime;
        *TickTime = liTemp.QuadPart;
    }
#else
    TickTime->LowPart = CurrentFileTime.dwLowDateTime;
    TickTime->HighPart = CurrentFileTime.dwHighDateTime;
#endif
}


// vim:sw=4:ts=4:et:
