//
//  File: %mod-serial.c
//  Summary: "serial port interface"
//  Section: ports
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
// A. This code was originally written to use EVENT! and the R3-Alpha device
//    model.  Ren-C ripped this out and replaced it with libuv.  However, at
//    time of writing the libuv event loop has not been exposed, e.g. by
//    a LIBUV-LOOP! type.  Until that is done, this code is just being kept
//    in a compiling state, with the hope of plugging into that someday.
//

#include "tmp-mod-serial.h"

#include "req-serial.h"

#define MAX_SERIAL_DEV_PATH 128

//
//  export serial-actor: native [
//
//  "Handler for OLDGENERIC dispatch on Serial PORT!s"
//
//      return: [any-value?]
//  ]
//
DECLARE_NATIVE(SERIAL_ACTOR)
{
    Value* port = ARG_N(1);
    const Symbol* verb = Level_Verb(LEVEL);

    VarList* ctx = Cell_Varlist(port);
    Value* spec = Varlist_Slot(ctx, STD_PORT_SPEC);

    Value* path = Obj_Value(spec, STD_PORT_SPEC_HEAD_REF);
    if (not Is_File(path))
        return FAIL(Error_Invalid_Spec_Raw(spec));

    Value* state = Varlist_Slot(ctx, STD_PORT_STATE);
    SerialConnection* serial = nullptr;  // in theory get from state...
    UNUSED(state);  // (SERIAL-RS232! will likely be its own datatype...)

  //=//// ACTIONS FOR UNOPENED SERIAL PORT ////////////////////////////////=//

    if (serial->handle == nullptr) {
        switch (Symbol_Id(verb)) {
          case SYM_OPEN_Q:
            return Init_False(OUT);

          case SYM_OPEN: {
            serial->path = rebValue(
                "try match [file! text!] pick", spec, "'serial-path"
            );  // !!! handle needs release somewhere...
            if (not serial->path)
                return "fail -{SERIAL-PATH must be FILE! or TEXT!}-";

            SerialBaudRate max_baud_rate = Get_Serial_Max_Baud_Rate();
            int baud_rate = rebUnboxInteger("any [",
                "try match integer pick", spec, "'serial-speed",
                "0"
            "]");
            if (baud_rate <= 0 or baud_rate > max_baud_rate)
                return rebDelegate("fail [",
                    "SERIAL-SPEED must be nonzero INTEGER!",
                    "up to", rebI(max_baud_rate),
                "]");
            serial->baud_rate = cast(SerialBaudRate, baud_rate);

            serial->data_bits = rebUnboxInteger("any [",
                "try match integer! pick", spec, "'serial-data-size"
                "0"
            "]");
            if (serial->data_bits < 5 or serial->data_bits > 8)
                return "fail -{DATA-SIZE must be INTEGER [5 .. 8]}-";

            serial->stop_bits = rebUnboxInteger(
                "try match integer! pick", spec, "'serial-stop-bits"
            );
            if (serial->stop_bits != 1 and serial->stop_bits != 2)
                return "fail -{STOP-BITS must be INTEGER [1 or 2]}-";

            int parity = rebUnboxInteger(
                "switch try pick", spec, "'serial-parity [",
                    " 'none [", rebI(SERIAL_PARITY_NONE), "]",
                    " 'odd [", rebI(SERIAL_PARITY_ODD), "]",
                    " 'even [", rebI(SERIAL_PARITY_EVEN), "]",
                "] else [-1]"
            );
            if (parity == -1)
                return "fail -{PARITY must be NONE/ODD/EVEN}-";
            serial->parity = cast(SerialParity, parity);

            int flow_control = rebUnboxInteger(
                "switch try pick", spec, "'serial-flow-control [",
                    "'none [", rebI(SERIAL_FLOW_CONTROL_NONE), "]",
                    "'hardware [", rebI(SERIAL_FLOW_CONTROL_HARDWARE), "]",
                    "'software [", rebI(SERIAL_FLOW_CONTROL_SOFTWARE), "]",
                "] else [-1]"
            );
            if (flow_control == -1)
                return ("fail -{FLOW-CONTROL must be NONE/HARDWARE/SOFTWARE}-");
            serial->flow_control = cast(SerialFlowControl, flow_control);

            Option(Error*) e = Trap_Open_Serial(serial);
            if (e)
                return unwrap e;

            return COPY(port); }

          case SYM_CLOSE:
            return COPY(port);

          default:
            return FAIL(Error_On_Port(SYM_NOT_OPEN, port, -12));
        }
    }

  //=//// ACTIONS FOR AN OPEN SERIAL PORT /////////////////////////////////=//

    switch (Symbol_Id(verb)) {
      case SYM_OPEN_Q:
        return Init_True(OUT);

      case SYM_READ: {
        INCLUDE_PARAMS_OF_READ;

        UNUSED(PARAM(SOURCE));

        if (Bool_ARG(PART) or Bool_ARG(SEEK))
            fail (Error_Bad_Refines_Raw());

        UNUSED(PARAM(STRING));  // handled in dispatcher
        UNUSED(PARAM(LINES));  // handled in dispatcher

        // Setup the read buffer (allocate a buffer if needed):
        Value* data = Varlist_Slot(ctx, STD_PORT_DATA);
        if (not Is_Blob(data))
            Init_Blob(data, Make_Binary(32000));

        Binary* bin = Cell_Binary_Known_Mutable(data);
        serial->length = Flex_Available_Space(bin);
        if (serial->length < 32000 / 2)
            Extend_Flex_If_Necessary(bin, 32000);

        serial->length = Flex_Available_Space(bin);
        serial->data = Binary_Tail(bin);  // write at tail
        serial->actual = 0;  // Actual for THIS read, not for total.

      #if DEBUG_SERIAL_EXTENSION
        printf("(max read length %d)", serial->length);
      #endif

        Option(Error*) e = Trap_Read_Serial(serial);  // can recv immediately
        if (e)
            return unwrap e;

        // !!! Incomplete reads need event loop interop, see [A] above

      #if DEBUG_SERIAL_EXTENSION
        for (len = 0; len < serial->actual; len++) {
            if (len % 16 == 0) printf("\n");
            printf("%02x ", serial->data[len]);
        }
        printf("\n");
      #endif

        return COPY(port); }

      case SYM_WRITE: {
        INCLUDE_PARAMS_OF_WRITE;

        UNUSED(PARAM(DESTINATION));

        if (Bool_ARG(SEEK) or Bool_ARG(APPEND) or Bool_ARG(LINES))
            return FAIL(Error_Bad_Refines_Raw());

        // Determine length. Clip :PART to size of BLOB! if needed.

        Element* data = Element_ARG(DATA);
        Length len = Cell_Series_Len_At(data);
        if (Bool_ARG(PART)) {
            REBLEN n = Int32s(ARG(PART), 0);
            if (n <= len)
                len = n;
        }

        Copy_Cell(Varlist_Slot(ctx, STD_PORT_DATA), data);  // keep it GC safe
        serial->length = len;
        serial->data = Cell_Blob_At_Known_Mutable(data);
        serial->actual = 0;

        // "send can happen immediately"
        //
        Option(Error*) e = Trap_Write_Serial(serial);  // can send immediately
        if (e)
            return unwrap e;

        // !!! Incomplete reads need event loop interop, see [A] above

        return COPY(port); }

      case SYM_CLOSE:
        if (serial->handle != nullptr) {  // !!! tolerate double closes?
            Option(Error*) e = Trap_Close_Serial(serial);
            if (e)
                return unwrap e;

            assert(serial->handle == nullptr);
        }
        return COPY(port);

      default:
        break;
    }

    return UNHANDLED;
}
