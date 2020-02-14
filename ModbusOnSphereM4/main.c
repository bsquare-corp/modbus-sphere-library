/* Copyright (c) Microsoft Corporation. All rights reserved.
   Licensed under the MIT License. */

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../modbusCommon.h"
#include "../crc-util.h"
#include "message-handler.h"
#include "mt3620-baremetal.h"
#include "mt3620-intercore.h"
#include "mt3620-timer.h"
#include "mt3620-uart.h"

#define UART_CONFIG_VALIDITY_OFFSET 0
#define timerCheckPeriod 10 // TODO This is the rate at which the M4 will poll for messages from the A7. Research

extern uint32_t StackTop; // &StackTop == end of TCM
                          // whether we can get an interrupt for this instead, or make it faster.
static uint8_t
    msgPrefix[PREFIX_LENGTH]; // Used to remember the last message data. This App will only receive from one other App.

static BufferHeader *outbound, *inbound;
static uint32_t sharedBufSize = 0;

static messageHandle UartIsu0RxBuffer = {0};

static _Noreturn void DefaultExceptionHandler(void);
static _Noreturn void RTCoreMain(void);

static void TimerIrq(void);
static void ReceiveCommandFromA7(void);

static void HandleUartIsu0RxIrq(void);
static void HandleUartIsu0RxIrqDeferred(void);

static void HandleUARTRequest(messageHandle *message);
static void HandleModbusRequest(messageHandle *message);
static size_t GetFcodeLength(uint8_t fCode, uint8_t dataLength);

typedef struct CallbackNode
{
    bool enqueued;
    struct CallbackNode *next;
    Callback cb;
} CallbackNode;

static void EnqueueCallback(CallbackNode *node);

// ARM DDI0403E.d SB1.5.2-3
// From SB1.5.3, "The Vector table must be naturally aligned to a power of two whose alignment
// value is greater than or equal to (Number of Exceptions supported x 4), with a minimum alignment
// of 128 bytes.". The array is aligned in linker.ld, using the dedicated section ".vector_table".

// The exception vector table contains a stack pointer, 15 exception handlers, and an entry for
// each interrupt.
#define INTERRUPT_COUNT 100 // from datasheet
#define EXCEPTION_COUNT (16 + INTERRUPT_COUNT)
#define INT_TO_EXC(i_) (16 + (i_))
const uintptr_t ExceptionVectorTable[EXCEPTION_COUNT] __attribute__((section(".vector_table")))
__attribute__((used)) = {[0] = (uintptr_t)&StackTop,                // Main Stack Pointer (MSP)
                         [1] = (uintptr_t)RTCoreMain,               // Reset
                         [2] = (uintptr_t)DefaultExceptionHandler,  // NMI
                         [3] = (uintptr_t)DefaultExceptionHandler,  // HardFault
                         [4] = (uintptr_t)DefaultExceptionHandler,  // MPU Fault
                         [5] = (uintptr_t)DefaultExceptionHandler,  // Bus Fault
                         [6] = (uintptr_t)DefaultExceptionHandler,  // Usage Fault
                         [11] = (uintptr_t)DefaultExceptionHandler, // SVCall
                         [12] = (uintptr_t)DefaultExceptionHandler, // Debug monitor
                         [14] = (uintptr_t)DefaultExceptionHandler, // PendSV
                         [15] = (uintptr_t)DefaultExceptionHandler, // SysTick

                         [INT_TO_EXC(0)] = (uintptr_t)DefaultExceptionHandler,
                         [INT_TO_EXC(1)] = (uintptr_t)Gpt_HandleIrq1,
                         [INT_TO_EXC(2)... INT_TO_EXC(3)] = (uintptr_t)DefaultExceptionHandler,
                         [INT_TO_EXC(4)] = (uintptr_t)Uart_HandleIrq4,
                         [INT_TO_EXC(5)... INT_TO_EXC(46)] = (uintptr_t)DefaultExceptionHandler,
                         [INT_TO_EXC(47)] = (uintptr_t)Uart_HandleIrq47,
                         [INT_TO_EXC(48)... INT_TO_EXC(INTERRUPT_COUNT - 1)] = (uintptr_t)DefaultExceptionHandler};
;

static _Noreturn void DefaultExceptionHandler(void)
{
    for (;;)
    {
        // empty.
    }
}

static void TimerIrq(void)
{
    static CallbackNode cbn = {.enqueued = false, .cb = ReceiveCommandFromA7};
    EnqueueCallback(&cbn);
}

static void ReceiveCommandFromA7(void)
{
    messageHandle req;
    bool rc = ReadA7Message(inbound, outbound, sharedBufSize, &req);
    if (rc)
    {
        __builtin_memcpy(msgPrefix, GetMessagePrefixPtr(&req), PREFIX_LENGTH);
        switch (GetMessageProtocol(&req))
        {
        case UART:
            HandleUARTRequest(&req);
            break;
        case MODBUS:
            HandleModbusRequest(&req);
            break;
        default:
            break;
        }
    }
    Gpt_LaunchTimerMs(TimerGpt1, timerCheckPeriod, TimerIrq);
}

static void HandleUARTRequest(messageHandle *req)
{
    switch (GetMessageCommand(req))
    {
    case UART_CFG_MESSAGE: {
        bool rc = SetSerialConfig(GetMessageDataPtr(req), GetMessageLength(req), UartIsu0, HandleUartIsu0RxIrq);

        uint8_t data[UART_CFG_MESSAGE_RESP_LENGTH];
        data[UART_CFG_MESSAGE_RESP_SUCCESS_OFFSET] = (rc) ? 1 : 0;

        messageHandle resp;
        InitMessage(&resp);
        SetMessagePrefix(&resp, msgPrefix);
        SetMessageProtocol(&resp, (uint8_t)UART);
        SetMessageCommand(&resp, (uint8_t)UART_CFG_MESSAGE);
        SetMessageData(&resp, data, UART_CFG_MESSAGE_RESP_LENGTH);

        SendA7Message(inbound, outbound, sharedBufSize, &resp);
        break;
    }
    default:
        break;
    }
}

static void HandleModbusRequest(messageHandle *req)
{
    switch (GetMessageCommand(req))
    {
    case MODBUS_DATA_MESSAGE: {
        size_t length = GetMessageLength(req);
        if (length > MAX_PDU_LENGTH)
        {
            return;
        }

        uint8_t data[MAX_PDU_LENGTH + CRC_FOOTER_LENGTH];
        __builtin_memcpy(data, GetMessageDataPtr(req), length);
        if (AddCRC(data, length, MAX_PDU_LENGTH + CRC_FOOTER_LENGTH))
        {
            Uart_EnqueueData(UartIsu0, data, length + CRC_FOOTER_LENGTH);
        }
        break;
    }
    default:
        break;
    }
}

static void HandleUartIsu0RxIrq(void)
{
    static CallbackNode cbn = {.enqueued = false, .cb = HandleUartIsu0RxIrqDeferred};
    EnqueueCallback(&cbn);
}

static void HandleUartIsu0RxIrqDeferred(void)
{
    for (;;)
    {
        size_t currentLength = GetMessageLength(&UartIsu0RxBuffer);
        uint8_t *basePtr = GetMessageDataPtr(&UartIsu0RxBuffer);
        uint8_t *currentDataPtr = &basePtr[currentLength];

        size_t availBytes = Uart_DequeueData(UartIsu0, currentDataPtr, GetMessageMaxLength() - currentLength);

        if (availBytes == 0)
        {
            return;
        }

        // Push result to debug serial
        Uart_EnqueueString(UartCM4Debug, "UART received ");
        Uart_EnqueueIntegerAsString(UartCM4Debug, availBytes);
        Uart_EnqueueString(UartCM4Debug, " bytes: \'");
        Uart_EnqueueData(UartCM4Debug, currentDataPtr, availBytes);
        Uart_EnqueueString(UartCM4Debug, "\'.\r\n");

        currentLength += availBytes;
        SetMessageLength(&UartIsu0RxBuffer, currentLength);

        if (currentLength < PDU_HEADER_LENGTH)
        {
            continue;
        }

        size_t expectedLength = GetFcodeLength(basePtr[1], basePtr[2]);

        if (currentLength >= expectedLength)
        {
            if (ValidateCRC(basePtr, expectedLength + CRC_FOOTER_LENGTH))
            {
                messageHandle resp;
                SetMessagePrefix(&resp, msgPrefix);
                SetMessageProtocol(&resp, (uint8_t)MODBUS);
                SetMessageCommand(&resp, (uint8_t)MODBUS_DATA_MESSAGE);
                SetMessageData(&resp, basePtr, expectedLength);

                SendA7Message(inbound, outbound, sharedBufSize, &resp);
            }
            else
            {
                Uart_EnqueueString(UartCM4Debug, "Error: CRC Failure\n");
            }
            SetMessageLength(&UartIsu0RxBuffer, 0);
        }
    }
}

static CallbackNode *volatile callbacks = NULL;

static void EnqueueCallback(CallbackNode *node)
{
    uint32_t prevBasePri = BlockIrqs();
    if (!node->enqueued)
    {
        CallbackNode *prevHead = callbacks;
        node->enqueued = true;
        callbacks = node;
        node->next = prevHead;
    }
    RestoreIrqs(prevBasePri);
}

static void InvokeCallbacks(void)
{
    CallbackNode *node;
    do
    {
        uint32_t prevBasePri = BlockIrqs();
        node = callbacks;
        if (node)
        {
            node->enqueued = false;
            callbacks = node->next;
        }
        RestoreIrqs(prevBasePri);

        if (node)
        {
            (*node->cb)();
        }
    } while (node);
}

static _Noreturn void RTCoreMain(void)
{
    // SCB->VTOR = ExceptionVectorTable
    WriteReg32(SCB_BASE, 0x08, (uint32_t)ExceptionVectorTable);

    Uart_Init(UartCM4Debug, NULL); // No Rx on M4 debug serial as no pins connected.
    Uart_EnqueueString(UartCM4Debug, "--------------------------------\r\n");
    Uart_EnqueueString(UartCM4Debug, "Modbus Bare Metal App\r\n");
    Uart_EnqueueString(UartCM4Debug, "App built on: " __DATE__ " " __TIME__ "\r\n");
    Uart_EnqueueString(UartCM4Debug,
                       "For testing Install a loopback header on ISU0. A7 data sent should be echoed back.\r\n");

    if (GetIntercoreBuffers(&outbound, &inbound, &sharedBufSize) == -1)
    {
        for (;;)
        {
            // empty.
        }
    }
    Gpt_Init();
    Gpt_LaunchTimerMs(TimerGpt1, timerCheckPeriod, TimerIrq);
    InitMessage(&UartIsu0RxBuffer);
    bool checkComplete = false;
    for (;;)
    {
        __asm__("wfi");
        InvokeCallbacks();
        while (!checkComplete)
        {
            checkComplete = CheckForCompletedTranmission();
        }
        checkComplete = false;
    }
}



static size_t GetFcodeLength(uint8_t fCode, uint8_t dataLength)
{
    if ((fCode > FCODE_ERROR_OFFSET) && (FCODE_ERROR_OFFSET + FCODE_RANGE))
    {
        // Error response length is always three bytes
        return ERROR_CODE_LENGTH;
    }
    switch (fCode)
    {
    case READ_COILS:
        return PDU_HEADER_LENGTH + dataLength;
    case READ_DISCRETE_INPUTS:
        return PDU_HEADER_LENGTH + dataLength;
    case READ_MULTIPLE_HOLDING_REGISTERS:
        return PDU_HEADER_LENGTH + dataLength;
    case READ_INPUT_REGISTERS:
        return PDU_HEADER_LENGTH + dataLength;
    case WRITE_SINGLE_COIL:
        return PDU_HEADER_LENGTH + 3;
    case WRITE_SINGLE_HOLDING_REGISTER:
        return PDU_HEADER_LENGTH + 3;
    case READ_EXCEPTION_STATUS:
        return PDU_HEADER_LENGTH;
    case WRITE_MULTIPLE_COILS:
        return PDU_HEADER_LENGTH + 3;
    case WRITE_MULTIPLE_HOLDING_REGISTERS:
        return PDU_HEADER_LENGTH + 3;

    default:
        return 0;
    }
}