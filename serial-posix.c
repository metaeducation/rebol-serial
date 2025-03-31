//
//  File: %serial-posix.c
//  Summary: "Device: Serial port access for Posix"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
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
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// A. TTY has many attributes. Refer to "man tcgetattr" for descriptions.
//
// B. The original code had an unimplemented Query_Serial() function.  All
//    it had in it was:
//
//        struct pollfd pfd;
//        pfd.events = POLLIN;
//        n = poll(&pfd, 1, 0);
//

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <termios.h>

#include "sys-core.h"

#include "req-serial.h"

typedef int TtyFileDescriptor;
typedef struct termios TtyAttributes;

typedef ssize_t SizeOrNegative;  // holds a size, or negative if error

#define MAX_SERIAL_PATH 128

const int speeds[] = {  // BXXX constants are defined in termios.h
    50, B50,
    75, B75,
    110, B110,
    134, B134,
    150, B150,
    200, B200,
    300, B300,
    600, B600,
    1200, B1200,
    1800, B1800,
    2400, B2400,
    4800, B4800,
    9600, B9600,
    19200, B19200,
    38400, B38400,
    57600, B57600,
    115200, B115200,
    230400, B230400,
    0
};


//=//// LOCAL FUNCTIONS ///////////////////////////////////////////////////=//


static Option(Error*) Trap_Get_Serial_Settings(
    Sink(TtyAttributes*) attr,
    TtyFileDescriptor ttyfd
){
    *attr = rebAlloc(TtyAttributes);
    if (tcgetattr(ttyfd, *attr) != 0) {
        rebFree(attr);
        Corrupt_Pointer_If_Debug(*attr);
        return Error_OS(errno);
    }
    return nullptr;
}


static Option(Error*) Trap_Set_Serial_Settings(
    TtyFileDescriptor ttyfd,
    SerialConnection *serial
){

    TtyAttributes attr;
    memset(&attr, 0, sizeof(attr));

  #if DEBUG_SERIAL_EXTENSION
    printf("setting attributes: baud_rate %d\n", serial->baud_rate);
  #endif

    int speed;

    for (Offset n = 0; true; n += 2) {
        if (speeds[n] == 0)  // invalid (used default CB115200 before)
            return Error_User("Invalid baud rate");

        if (serial->baud_rate == speeds[n]) {
            speed = speeds[n + 1];
            break;
        }
    }

    cfsetospeed(&attr, speed);  // output speed
    cfsetispeed(&attr, speed);  // input speed

    attr.c_cflag |= CREAD | CLOCAL;  // C-flags: control modes, see [A] above

    attr.c_cflag &= (~ CSIZE);  // clear data size bits

    switch (serial->data_bits) {
      case 5:
        attr.c_cflag |= CS5;
        break;

      case 6:
        attr.c_cflag |= CS6;
        break;

      case 7:
        attr.c_cflag |= CS7;
        break;

      data_bits_8_case:
      case 8:
        attr.c_cflag |= CS8;
        break;

      default:
        assert(false);
        goto data_bits_8_case;
    }

    switch (serial->parity) {
      parity_none_case:
      case SERIAL_PARITY_NONE:
        attr.c_cflag &= ~PARENB;
        break;

      case SERIAL_PARITY_ODD:
        attr.c_cflag |= PARENB;
        attr.c_cflag |= PARODD;
        break;

      case SERIAL_PARITY_EVEN:
        attr.c_cflag |= PARENB;
        attr.c_cflag &= ~PARODD;
        break;

      default:
        assert(false);
        goto parity_none_case;
    }

    switch (serial->stop_bits) {
      stop_bits_1_case:
      case 1:
        attr.c_cflag &= ~CSTOPB;
        break;

      case 2:
        attr.c_cflag |= CSTOPB;
        break;

      default:
        assert(false);
        goto stop_bits_1_case;
    }

  #ifdef CNEW_RTSCTS
    switch (serial->parity) {
      flow_control_none_case:
      case SERIAL_FLOW_CONTROL_NONE:
        break;

      case SERIAL_FLOW_CONTROL_HARDWARE:
        attr.c_cflag |= CNEW_RTSCTS;
        break;

      case SERIAL_FLOW_CONTROL_SOFTWARE:
        attr.c_cflag &= ~CNEW_RTSCTS;
        break;

      default:
        assert(false);
        goto flow_control_none_case;
    }
  #endif

    attr.c_lflag = 0;  // L-flags: local modes (raw, not ICANON)

    attr.c_iflag |= IGNPAR;  // I-flags: input modes

    attr.c_oflag = 0;  // O-flags: output modes

    attr.c_cc[VMIN]  = 0;  // control characters...
    attr.c_cc[VTIME] = 0;  // we use non-blocking IO, so...not needed (?)

    if (tcflush(ttyfd, TCIFLUSH) != 0)  // make sure OS queues are empty
        return Error_OS(errno);

    if (tcsetattr(ttyfd, TCSANOW, &attr) != 0)  // Set new attributes
        return Error_OS(errno);

    return nullptr;  // no error
}


//=//// EXPORTED FUNCTIONS ////////////////////////////////////////////////=//


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
// serial.path = the /dev name for the serial port
// serial.baud = speed (baudrate)
//
Option(Error*) Trap_Open_Serial(SerialConnection* serial)
{
    assert(serial->path != nullptr);

    char path_utf8[MAX_SERIAL_PATH];
    Size size = rebSpellInto(
        path_utf8,
        MAX_SERIAL_PATH,
        serial->path
    );

    if (path_utf8[0] != '/') {  // relative path, insert `/dev` before slash
        memmove(path_utf8 + 4, path_utf8, size + 1);
        memcpy(path_utf8, "/dev", 4);
    }

    TtyFileDescriptor ttyfd = open(path_utf8, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (ttyfd == -1)
        return Error_OS(errno);

    TtyAttributes* prior_attr;
    Option(Error*) e_get = Trap_Get_Serial_Settings(&prior_attr, ttyfd);
    if (e_get) {
        close(ttyfd);
        return e_get;
    }
    serial->prior_attr = prior_attr;

    Option(Error*) e_set = Trap_Set_Serial_Settings(ttyfd, serial);
    if (e_set) {
        close(ttyfd);
        return e_set;
    }

    serial->handle = p_cast(void*, cast(intptr_t, ttyfd));
    return nullptr;
}


//
//  Trap_Read_Serial: C
//
Option(Error*) Trap_Read_Serial(SerialConnection* serial)
{
    assert(serial->handle != nullptr);
    TtyFileDescriptor ttyfd = cast(TtyFileDescriptor,
        i_cast(intptr_t, serial->handle)
    );

    SizeOrNegative result = read(ttyfd, serial->data, serial->length);

  #if DEBUG_SERIAL_EXTENSION
    printf("read %d ret: %d\n", serial->length, result);
  #endif

    if (result == -1)
        return Error_OS(errno);

    if (result == 0)
        fail ("The original implementation queued PENDING here");

    serial->actual = result;

    fail ("The original implementation posted a WAS-READ event here");
}


//
//  Trap_Write_Serial: C
//
Option(Error*) Trap_Write_Serial(SerialConnection* serial)
{
    assert(serial->handle != nullptr);
    TtyFileDescriptor ttyfd = cast(TtyFileDescriptor,
        i_cast(intptr_t, serial->handle)
    );

    Size len = serial->length - serial->actual;

    if (len <= 0)
        fail ("The original implementation returned DONE here");

    SizeOrNegative result = write(ttyfd, serial->data, len);

  #if DEBUG_SERIAL_EXTENSION
    printf("write %d ret: %d\n", len, result);
  #endif

    if (result == -1) {
        if (errno == EAGAIN)
            fail ("The original implementation queued PENDING here");

        return Error_OS(errno);
    }

    serial->actual += result;
    serial->data += result;

    if (serial->actual >= serial->length)
        fail ("The original implementation posted a WAS-WRITTEN event here");

    fail ("The original implementation queued PENDING here");
}


//
//  Trap_Close_Serial: C
//
Option(Error*) Trap_Close_Serial(SerialConnection* serial)
{
    assert(serial->handle != nullptr);

    TtyFileDescriptor ttyfd = cast(TtyFileDescriptor,
        i_cast(intptr_t, serial->handle)
    );

    TtyAttributes* prior_attr = cast(TtyAttributes*, serial->prior_attr);

    int ret = tcsetattr(ttyfd, TCSANOW, prior_attr);
    int errno_copy = errno;  // close() may change errno

    // !!! should we free serial->prior_attr?

    close(ttyfd);

    if (ret != 0)
        return Error_OS(errno_copy);

    serial->handle = nullptr;
    return nullptr;
}
