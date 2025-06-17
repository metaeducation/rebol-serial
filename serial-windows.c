//
//  file: %serial-windows.c
//  summary: "Device: Serial port access for Windows"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2013 REBOL Technologies
// Copyright 2013-2017 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// This code is being kept compiling, but does not work at present.
//
// See README.md for notes about this extension.
//

#define WIN32_LEAN_AND_MEAN  // trim down the Win32 headers
#include <windows.h>
#undef VOID  // used for better purpose
#undef OUT  // used for better purpose

#include <assert.h>

#include "sys-core.h"

#include "req-serial.h"

#define MAX_SERIAL_DEV_PATH 128

const int speeds[] = {
    110, CBR_110,
    300, CBR_300,
    600, CBR_600,
    1200, CBR_1200,
    2400, CBR_2400,
    4800, CBR_4800,
    9600, CBR_9600,
    14400, CBR_14400,
    19200, CBR_19200,
    38400, CBR_38400,
    57600, CBR_57600,
    115200, CBR_115200,
    128000, CBR_128000,
    230400, CBR_256000,
    0
};


//
//  Get_Serial_Max_Baud_Rate: C
//
SerialBaudRate Get_Serial_Max_Baud_Rate(void) {
    int max = 0;
    for (int n = 0; speeds[n] != 0; n += 2)
        max = speeds[n];
    return max;
}


//
//  Trap_Open_Serial: C
//
// !!! This doesn't seem to heed serial->flow_control or serial->data_bits.
//
// 1. serial->path should be prefixed with "\\.\" to allow for higher COM
//    port numbers
//
// 2. !!! Comment said "add in timeouts? currently unused".  This may suggest
//    a question of whether the request itself have some way of asking for
//    custom timeouts, while the initialization of the timeouts below is the
//    same for every request.
//
//    http://msdn.microsoft.com/en-us/library/windows/desktop/aa363190%28v=vs.85%29.aspx
//
Option(Error*) Trap_Open_Serial(SerialConnection* serial)
{
    assert(serial->path != nullptr);

    WCHAR fullpath[MAX_SERIAL_DEV_PATH] = L"\\\\.\\";  // high port nums [1]

    Length buf_left = MAX_SERIAL_DEV_PATH - wcslen(fullpath) - 1;
    Length chars_appended = rebSpellIntoWide(
        &fullpath[wcslen(fullpath)],  // concatenate to end of buffer
        buf_left,  // space, minus terminator
        serial->path
    );
    if (chars_appended > buf_left)
        return Error_User("Serial path too long for MAX_SERIAL_DEV_PATH}");

    HANDLE h = CreateFile(
        fullpath,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );
    if (h == INVALID_HANDLE_VALUE)
        return Error_OS(GetLastError());

    DCB dcbSerialParams;
    memset(&dcbSerialParams, '\0', sizeof(dcbSerialParams));
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

    if (not GetCommState(h, &dcbSerialParams)) {
        CloseHandle(h);
        return Error_OS(GetLastError());
    }

    for (Offset n = 0; true; n += 2) {
        if (speeds[n] == 0)  // invalid (used default CBR_115200 before)
            return Error_User("Invalid baud rate");

        if (serial->baud_rate == speeds[n]) {
            dcbSerialParams.BaudRate = speeds[n+1];
            break;
        }
    }

    dcbSerialParams.ByteSize = serial->data_bits;
    if (serial->stop_bits == 1) {
      stop_bits_1_case:
        dcbSerialParams.StopBits = ONESTOPBIT;
    }
    else if (serial->stop_bits == 2)
        dcbSerialParams.StopBits = TWOSTOPBITS;
    else {
        assert(false);
        goto stop_bits_1_case;
    }

    switch (serial->parity) {
      parity_none_case:
      case SERIAL_PARITY_NONE:
        dcbSerialParams.Parity = NOPARITY;
        break;

      case SERIAL_PARITY_ODD:
        dcbSerialParams.Parity = ODDPARITY;
        break;

      case SERIAL_PARITY_EVEN:
        dcbSerialParams.Parity = EVENPARITY;
        break;

      default:
        assert(false);
        goto parity_none_case;
    }

    if (not SetCommState(h, &dcbSerialParams)) {
        CloseHandle(h);
        return Error_OS(GetLastError());
    }

    if (not PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR)) {  // clean buffers
        CloseHandle(h);
        return Error_OS(GetLastError());
    }

    COMMTIMEOUTS timeouts;  // comment "add in timeouts? currently unused" [2]
    memset(&timeouts, '\0', sizeof(timeouts));
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = 0;
    timeouts.WriteTotalTimeoutMultiplier = 1;  // !!! should this be 0?
    timeouts.WriteTotalTimeoutConstant = 1;  // !!! should this be 0?

    if (not SetCommTimeouts(h, &timeouts)) {
        CloseHandle(h);
        return Error_OS(GetLastError());
    }

    serial->handle = h;
    return nullptr;  // no error
}


//
//  Trap_Close_Serial: C
//
Option(Error*) Trap_Close_Serial(SerialConnection* serial)
{
    assert(serial->handle != nullptr);

    if (not CloseHandle(serial->handle))
        return Error_OS(GetLastError());

    return nullptr;  // no error
}


//
//  Trap_Read_Serial: C
//
Option(Error*) Trap_Read_Serial(SerialConnection* serial)
{
    assert(serial->handle != nullptr);

  #if DEBUG_SERIAL_EXTENSION
    printf("reading %d bytes\n", serial->length);
  #endif

    DWORD result;
    LPOVERLAPPED overlapped = nullptr;
    if (not ReadFile(
        serial->handle, serial->data, serial->length, &result, overlapped
    )){
        return Error_OS(GetLastError());
    }

    if (result == 0)
        fail ("The original implementation queued PENDING here");

    serial->actual = result;

  #if DEBUG_SERIAL_EXTENSION
    printf("read %d ret: %d\n", serial->length, serial->actual);
  #endif

    fail ("The original implementation posted a WAS-READ event here");
}


//
//  Trap_Write_Serial: C
//
Option(Error*) Trap_Write_Serial(SerialConnection* serial)
{
    assert(serial->handle != nullptr);

    Size len = serial->length - serial->actual;

    if (len <= 0)
        fail ("The original implementation returned DONE here");

    DWORD result;
    LPOVERLAPPED overlapped = nullptr;
    if (not WriteFile(
        serial->handle, serial->data, len, &result, overlapped
    )){
        rebFail_OS (GetLastError());
    }

  #if DEBUG_SERIAL_EXTENSION
    printf("write %d ret: %d\n", serial->length, serial->actual);
  #endif

    serial->actual += result;
    serial->data += result;

    if (serial->actual >= serial->length)
        fail ("The original implementation posted a WAS-WRITTEN event here");

    fail ("The original implementation queued PENDING here");
}
