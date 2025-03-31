#define DEBUG_SERIAL_EXTENSION  0

typedef int32_t SerialBaudRate;

typedef enum {
    SERIAL_PARITY_NONE,
    SERIAL_PARITY_ODD,
    SERIAL_PARITY_EVEN
} SerialParity;

typedef enum {
    SERIAL_FLOW_CONTROL_NONE,
    SERIAL_FLOW_CONTROL_HARDWARE,
    SERIAL_FLOW_CONTROL_SOFTWARE
} SerialFlowControl;

struct SerialConnection {
    void* handle;  // TtyFileDescriptor on Linux, HANDLE on Windows
    RebolValue* path;  // device path string (in OS local format)
    void* prior_attr;  // termios: retain prev settings to revert on close
    SerialBaudRate baud_rate;
    uint8_t data_bits;  // 5, 6, 7 or 8
    SerialParity parity;
    uint8_t stop_bits;  // 1 or 2
    SerialFlowControl flow_control;

    Byte* data;
    Size length;
    Size actual;
};

extern SerialBaudRate Get_Serial_Max_Baud_Rate(void);
extern Option(Error*) Trap_Read_Serial(SerialConnection* serial);
extern Option(Error*) Trap_Open_Serial(SerialConnection* serial);
extern Option(Error*) Trap_Write_Serial(SerialConnection* serial);
extern Option(Error*) Trap_Close_Serial(SerialConnection* serial);
