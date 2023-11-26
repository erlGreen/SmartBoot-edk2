#include <Uefi.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiBootManagerLib.h>
#include <Library/DevicePathLib.h>

#include <Protocol/LoadedImage.h>
#include <Protocol/DevicePath.h>
#include <Protocol/SerialIo.h>


#define DEFAULT_TIMEOUT 10000000    // 1s
#define CRC_POLYNOMIAL 0xD5
#define PING_PONG_RETRIES 5
#define ACK_TIMEOUT 50000000 // 5s
#define WAIT_FOR_INPUT_TIMEOUT 1000000  // 100ms
#define WRITE_READ_MAX_TRY_COUNT 5
#define ACK_ONLY 0xFFFFFFFFFFFFFFFF

#define PING 0x1
#define PONG 0x2
#define DATA_REQUEST 0x04
#define GET_BOOT_OPT 0x08

#define CONTROL_DATA 0xF0
#define DATA 0x0F

#define NACK 0xFF
#define ACK 0x00
