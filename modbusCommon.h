/**
 * @file    modbusCommon.h
 * @brief   Definitions for constants that are used in A7 and M4 modbus code.
 *
 * @author  Copyright (c) Bsquare EMEA 2019. https://www.bsquare.com/
 *          Licensed under the MIT License.
 */

#ifndef MODBUSCOMMON_H
#define MODBUSCOMMON_H

typedef enum
{
    UART_CFG_MESSAGE = 1,
    UART_CFG_MESSAGE_RESPONSE,
} serialPortMsgTypes;

typedef enum
{
    MODBUS_DATA_MESSAGE = 1
} modbusMsgTypes;

typedef enum
{
    UART = 1,
    MODBUS,
} messageProtocol;

/* Function codes */
#define READ_COILS 1
#define READ_DISCRETE_INPUTS 2
#define READ_MULTIPLE_HOLDING_REGISTERS 3
#define READ_INPUT_REGISTERS 4
#define WRITE_SINGLE_COIL 5
#define WRITE_SINGLE_HOLDING_REGISTER 6
#define READ_EXCEPTION_STATUS 7
#define WRITE_MULTIPLE_COILS 15
#define WRITE_MULTIPLE_HOLDING_REGISTERS 16
#define READ_FILE 20
#define WRITE_FILE 21

/* Exception codes */
#define ILLEGAL_FUNCTION 1
#define ILLEGAL_DATA_ADDRESS 2
#define ILLEGAL_DATA_VALUE 3
#define SLAVE_DEVICE_FAILURE 4
#define ACKNOWLEDGE 5
#define SLAVE_DEVICE_BUSY 6
#define NEGATIVE_ACKNOWLEDGE 7
#define MEMORY_PARITY_ERROR 8
#define GATEWAY_PATH_UNAVAILABLE 10
#define GATEWAY_TARGET_DEVICE_FAILED_TO_RESPOND 11
// Implementation specific error codes in addition to
// standard modbus exceptions
#define MODBUS_TIMEOUT 20
#define MESSAGE_SEND_FAIL 21
#define HANDLE_IN_USE 22
#define INVALID_RESPONSE 23

/* Supported baud rates */
#define BAUD_SET_300 384
#define BAUD_SET_600 192
#define BAUD_SET_1200 96
#define BAUD_SET_2400 48
#define BAUD_SET_4800 24
#define BAUD_SET_9600 12
#define BAUD_SET_14400 8
#define BAUD_SET_19200 6
#define BAUD_SET_38400 3
#define BAUD_SET_57600 2
#define BAUD_SET_115200 1

/* Corresponding register values and offsets for each uart configuration option */
#define UART_CFG_MESSAGE_LENGTH 7 

#define BAUD_RATE_OFFSET_UPPER 0
#define BAUD_RATE_OFFSET_LOWER 1
#define DUPLEX_MODE_OFFSET 2
#define PARITY_STATE_OFFSET 3
#define PARITY_MODE_OFFSET 4
#define STOP_BITS_OFFSET 5
#define WORD_LENGTH_OFFSET 6

#define HALF_DUPLEX_MODE 1
#define FULL_DUPLEX_MODE 0
#define PARITY_ON 1
#define PARITY_OFF 0
#define PARITY_EVEN 1
#define PARITY_ODD 0

/* ModBus specific lengths and offsets */
#define CRC_FOOTER_LENGTH 2
#define PDU_HEADER_LENGTH 3
#define ERROR_CODE_LENGTH 3
#define FCODE_RANGE 32
#define FCODE_ERROR_OFFSET 128
#define MAX_PDU_LENGTH 254

/* Offsets into message header */
#define PROTOCOL_OFFSET 0
#define COMMAND_OFFSET 1
#define HEADER_LENGTH_OFFSET 2

/* Command specific response data */
#define UART_CFG_MESSAGE_RESP_LENGTH 1
#define UART_CFG_MESSAGE_RESP_SUCCESS_OFFSET 0

#endif /* MODBUSCOMMON_H */
