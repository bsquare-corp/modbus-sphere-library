/* Copyright (c) Microsoft Corporation. All rights reserved.
   Licensed under the MIT License. */

#include <stdbool.h>

#include "../modbusCommon.h"
#include "mt3620-baremetal.h"
#include "mt3620-gpio.h"
#include "mt3620-uart.h"

// This is the physical TX FIFO size, taken from the datasheet.
// To adjust the size of the in-memory FIFO, set TX_BUFFER_SIZE below.
#define TX_FIFO_DEPTH 16

// This must be able to hold a value which is strictly greater than TX_BUFFER_SIZE.
typedef uint16_t EnqCtrType;

// Buffer sizes must be a power of two, and less than 65536.
#define TX_BUFFER_SIZE 256
#define TX_BUFFER_MASK (TX_BUFFER_SIZE - 1)
#define RX_BUFFER_SIZE 32
#define RX_BUFFER_MASK (RX_BUFFER_SIZE - 1)

#define LSR_OFFSET 0x14
#define TEMT_MASK 0x40

#define HALF_DUPLEX_PIN 0 // GPIO pin to use for half duplex. Must be allowed in the manifest.
// using a scope on the actual pin chosen.
#define HALF_DUPLEX_TX_MODE true
#define HALF_DUPLEX_RX_MODE false

#define MAX_SUPPORTED_BAUD_RATE 115200
#define PARITY_BIT_ON 1 << 3
#define PARITY_BIT_EVEN 1 << 4
#define STOP_BITS_2 1 << 2
#define WORD_LENGTH_6 1
#define WORD_LENGTH_7 2
#define WORD_LENGTH_8 3

typedef struct
{
    uintptr_t baseAddr;
    int nvicIrq;
    // varaibles from set
    uint8_t lcr;
    uint8_t upperDivisor;
    uint8_t lowerDivisor;
    bool txStarted;
    bool oneByteLeft;
    uint8_t txBuffer[TX_BUFFER_SIZE];
    volatile EnqCtrType txEnqueuedBytes;
    volatile EnqCtrType txDequeuedBytes;

    Callback rxCallback;
    uint8_t rxBuffer[RX_BUFFER_SIZE];
    volatile EnqCtrType rxEnqueuedBytes;
    volatile EnqCtrType rxDequeuedBytes;
} UartInfo;

static UartInfo uarts[] = {
    // Sets UartCM4 to be ready to send data, and set its default settings to 115200-8-N-1.
    [UartCM4Debug] = {.baseAddr = 0x21040000,
                      .nvicIrq = 4,
                      .lcr = 0x03,
                      .upperDivisor = 0x00,
                      .lowerDivisor = 0x01,
                      .txStarted = false,
                      .oneByteLeft = false},
    // Sets UartIsu0 to be ready to send data, and set its default settings to 115200-8-N-1.
    [UartIsu0] = {.baseAddr = 0x38070500,
                  .nvicIrq = 47,
                  .lcr = 0x03,
                  .upperDivisor = 0x00,
                  .lowerDivisor = 0x01,
                  .txStarted = false,
                  .oneByteLeft = false},
};

static int halfDuplexUart = -1;
static bool halfDuplexEnabled = false;

static void Uart_HandleIrq(UartId id);
static void Uart_SetHalfDuplexMode(bool mode);

void Uart_Init(UartId id, Callback rxCallback)
{
    UartInfo *unit = &uarts[id];

    // Configure UART to use the settings provided by the A7.
    WriteReg32(unit->baseAddr, 0x0C, 0xBF);               // LCR (enable DLL, DLM)
    WriteReg32(unit->baseAddr, 0x08, 0x10);               // EFR (enable enhancement features)
    WriteReg32(unit->baseAddr, 0x24, 0x3);                // HIGHSPEED
    WriteReg32(unit->baseAddr, 0x04, unit->upperDivisor); // Divisor Latch (MS)
    WriteReg32(unit->baseAddr, 0x00, unit->lowerDivisor); // Divisor Latch (LS)
    WriteReg32(unit->baseAddr, 0x28, 224);                // SAMPLE_COUNT
    WriteReg32(unit->baseAddr, 0x2C, 110);                // SAMPLE_POINT
    WriteReg32(unit->baseAddr, 0x58, 0);                  // FRACDIV_M
    WriteReg32(unit->baseAddr, 0x54, 223);                // FRACDIV_L
    WriteReg32(unit->baseAddr, 0x0C, unit->lcr);          // LCR (8-bit word length)

    // FCR[RFTL] = 2 -> 12 element RX FIFO trigger
    // FCR[TFTL] = 1 -> 0 element TX FIFO trigger
    // FCR[CLRT] = 1 -> Clear Transmit FIFO
    // FCR[CLRR] = 1 -> Clear Receive FIFO
    // FCR[FIFOE] = 1 -> FIFO Enable
    const uint8_t fcr = (2U << 6) | (1U << 2) | (1U << 1) | (1U << 0);
    WriteReg32(unit->baseAddr, 0x08, fcr);

    // If an RX callback was supplied then enable the Receive Buffer Full Interrupt.
    if (rxCallback)
    {
        uarts[id].rxCallback = rxCallback;
        // IER[ERBGI] = 1 -> Enable Receiver Buffer Full Interrupt
        SetReg32(unit->baseAddr, 0x04, 0x01);
    }

    SetNvicPriority(unit->nvicIrq, UART_PRIORITY);
    EnableNvicInterrupt(unit->nvicIrq);
}

void Uart_HandleIrq4(void)
{
    Uart_HandleIrq(UartCM4Debug);
}

void Uart_HandleIrq47(void)
{
    Uart_HandleIrq(UartIsu0);
}

static void Uart_HandleIrq(UartId id)
{
    UartInfo *unit = &uarts[id];

    uint32_t iirId;
    do
    {
        // Interrupt Identification Register[IIR_ID]
        iirId = ReadReg32(unit->baseAddr, 0x08) & 0x1F;
        switch (iirId)
        {
        case 0x01: // No interrupt pending
            break;
            // The TX FIFO can accept more data.
        case 0x02: { // TX Holding Register Empty Interrupt
            EnqCtrType localEnqueuedBytes = unit->txEnqueuedBytes;
            EnqCtrType localDequeuedBytes = unit->txDequeuedBytes;

            // TX_OFFSET, holds number of bytes in TX FIFO.
            uint32_t txOffset = ReadReg32(unit->baseAddr, 0x6C);
            uint32_t spaceInTxFifo = TX_FIFO_DEPTH - txOffset;

            while (localDequeuedBytes != localEnqueuedBytes && spaceInTxFifo > 0)
            {
                EnqCtrType txIdx = localDequeuedBytes & TX_BUFFER_MASK;
                // TX Holding Register
                WriteReg32(unit->baseAddr, 0x00, unit->txBuffer[txIdx]);

                ++localDequeuedBytes;
                --spaceInTxFifo;
            }

            // If sent all enqueued data then disable TX interrupt.
            if (unit->txEnqueuedBytes == unit->txDequeuedBytes)
            {
                // Interrupt Enable Register
                ClearReg32(unit->baseAddr, 0x04, 0x02);
                unit->oneByteLeft = true;
            }
            unit->txDequeuedBytes = localDequeuedBytes;
        }
        break;

        // Read from the FIFO if it has passed its trigger level, or if a timeout
        // has occurred, meaning there is unread data still in the FIFO.
        case 0x0C:   // RX Data Timeout Interrupt
        case 0x04: { // RX Data Received Interrupt
            EnqCtrType localEnqueuedBytes = unit->rxEnqueuedBytes;
            EnqCtrType localDequeuedBytes = unit->rxDequeuedBytes;

            EnqCtrType availSpace;
            if (localEnqueuedBytes >= localDequeuedBytes)
            {
                availSpace = RX_BUFFER_SIZE - (localEnqueuedBytes - localDequeuedBytes);
            }
            // If counter wrapped around, work out true remaining space.
            else
            {
                availSpace = (localDequeuedBytes & RX_BUFFER_MASK) - localEnqueuedBytes;
            }

            // LSR[0] = 1 -> Data Ready
            while (availSpace > 0 && (ReadReg32(unit->baseAddr, 0x14) & 0x01))
            {
                EnqCtrType idx = localEnqueuedBytes & RX_BUFFER_MASK;
                // RX Buffer Register
                unit->rxBuffer[idx] = ReadReg32(unit->baseAddr, 0x00);

                ++localEnqueuedBytes;
                --availSpace;
            }

            unit->rxEnqueuedBytes = localEnqueuedBytes;

            if (unit->rxCallback)
            {
                unit->rxCallback();
            }
        }
        break;
        } // switch (iirId) {
    } while (iirId != 0x01);
}

bool Uart_EnqueueData(UartId id, const uint8_t *data, size_t length)
{
    UartInfo *unit = &uarts[id];

    EnqCtrType localEnqueuedBytes = unit->txEnqueuedBytes;
    EnqCtrType localDequeuedBytes = unit->txDequeuedBytes;

    EnqCtrType availSpace;
    if (localEnqueuedBytes >= localDequeuedBytes)
    {
        availSpace = TX_BUFFER_SIZE - (localEnqueuedBytes - localDequeuedBytes);
    }
    // If counter wrapped around, work out true remaining space.
    else
    {
        availSpace = (localDequeuedBytes & TX_BUFFER_MASK) - localEnqueuedBytes;
    }

    // If no available space then do not enable TX interrupt.
    if (availSpace == 0)
    {
        return false;
    }

    unit->txStarted = true;
    if (halfDuplexUart == id && halfDuplexEnabled)
    {
        Uart_SetHalfDuplexMode(HALF_DUPLEX_TX_MODE);
    }

    // Copy as much data as possible from the message to the buffer.
    // Any unqueued data will be lost.
    bool writeAll = (availSpace >= length);
    EnqCtrType bytesToWrite = writeAll ? length : availSpace;

    while (bytesToWrite--)
    {
        EnqCtrType idx = localEnqueuedBytes & TX_BUFFER_MASK;
        unit->txBuffer[idx] = *data++;
        ++localEnqueuedBytes;
    }

    // Block IRQs here because the the UART IRQ could already be enabled, and run
    // between updating txEnqueuedBytes and re-enabling the IRQ here. If that happened,
    // the IRQ could exhaust the software buffer and disable the TX interrupt, only
    // for it to be re-enabled here, in which case it would not get cleared because
    // there was no data to write to the TX FIFO.
    uint32_t prevPriBase = BlockIrqs();
    unit->txEnqueuedBytes = localEnqueuedBytes;
    // IER[ETBEI] = 1 -> Enable Transmitter Buffer Empty Interrupt
    SetReg32(unit->baseAddr, 0x04, 0x02);
    RestoreIrqs(prevPriBase);

    return writeAll;
}

size_t Uart_DequeueData(UartId id, uint8_t *buffer, size_t bufferSize)
{
    UartInfo *unit = &uarts[id];

    EnqCtrType localEnqueuedBytes = unit->rxEnqueuedBytes;
    EnqCtrType localDequeuedBytes = unit->rxDequeuedBytes;

    EnqCtrType availData;
    if (localEnqueuedBytes >= localDequeuedBytes)
    {
        availData = localEnqueuedBytes - localDequeuedBytes;
    }
    // Wraparound occurred so work out the true available data.
    else
    {
        availData = RX_BUFFER_SIZE - ((localDequeuedBytes & RX_BUFFER_MASK) - localEnqueuedBytes);
    }

    // This check is required to distinguish an empty buffer from a full buffer, because
    // in both cases the enqueue and dequeue indices point to the same index.
    if (availData == 0)
    {
        return 0;
    }

    EnqCtrType enqueueIndex = localEnqueuedBytes & RX_BUFFER_MASK;
    EnqCtrType dequeueIndex = localDequeuedBytes & RX_BUFFER_MASK;

    // If the available data does not wraparound use one memcpy...
    if (enqueueIndex > dequeueIndex)
    {
        __builtin_memcpy(buffer, &unit->rxBuffer[dequeueIndex], availData);
    }
    // ...otherwise copy data from end of buffer, then from start.
    else
    {
        size_t bytesFromEnd = RX_BUFFER_SIZE - dequeueIndex;
        __builtin_memcpy(buffer, &unit->rxBuffer[dequeueIndex], bytesFromEnd);
        __builtin_memcpy(buffer + bytesFromEnd, &unit->rxBuffer[0], enqueueIndex);
    }

    unit->rxDequeuedBytes += availData;
    return availData;
}

bool Uart_EnqueueString(UartId id, const char *msg)
{
    return Uart_EnqueueData(id, (const uint8_t *)msg, __builtin_strlen(msg));
}

static bool EnqueueIntegerAsStringWithBase(UartId id, int value, int base)
{
    // Maximum decimal length is minus sign followed by ten digits.
    char txt[1 + 10];
    char *p = txt;

    bool isNegative = value < 0;
    if (isNegative)
    {
        *p++ = '-';
    }

    static const char digits[] = "0123456789abcdef";
    do
    {
        *p++ = digits[__builtin_abs(value % base)];
        value /= base;
    } while (value);

    // Reverse the digits, not including any negative sign.
    char *low = isNegative ? &txt[1] : &txt[0];
    char *high = p - 1;
    while (low < high)
    {
        char tmp = *low;
        *low = *high;
        *high = tmp;
        ++low;
        --high;
    }

    return Uart_EnqueueData(id, (const uint8_t *)txt, p - txt);
}

bool Uart_EnqueueIntegerAsString(UartId id, int value)
{
    return EnqueueIntegerAsStringWithBase(id, value, 10);
}

bool Uart_EnqueueIntegerAsHexString(UartId id, uint32_t value)
{
    // EnqueueIntegerAsStringWithBase takes a signed integer, so print each half-word
    // separately, so the value is not interpreted as negative integer if bit 31 is set.
    if (value & 0xFFFF0000)
    {
        return EnqueueIntegerAsStringWithBase(id, (int)(value >> 16), 16) &&
               Uart_EnqueueIntegerAsHexStringWidth(id, value, 4);
    }

    return EnqueueIntegerAsStringWithBase(id, value & 0x0000FFFF, 16);
}

bool Uart_EnqueueIntegerAsHexStringWidth(UartId id, uint32_t value, size_t width)
{
    while (width)
    {
        uint32_t printNybble = (value >> ((width - 1) * 4)) & 0xF;
        if (!EnqueueIntegerAsStringWithBase(id, printNybble, 16))
        {
            return false;
        }
        --width;
    }

    return true;
}

void Uart_EnableHalfDuplex(UartId id)
{
    // Uses GPIO as a control for the approriate Uart.
    // 0-7: GpioBlock_GRP
    // 8-11: GpioBlock_PWM
    static bool firstTime = true;
    if (firstTime)
    {
        const uint8_t firstPin = HALF_DUPLEX_PIN & 0xfc;
        static const GpioBlock hdControlBlock = {.baseAddr = 0x38010000 + (0x10000 * firstPin / GPIO_PINS_PER_BLOCK),
                                                 .type = GpioBlock_GRP,
                                                 .firstPin = firstPin,
                                                 .pinCount = GPIO_PINS_PER_BLOCK};
        Mt3620_Gpio_AddBlock(&hdControlBlock);
        Mt3620_Gpio_ConfigurePinForOutput(HALF_DUPLEX_PIN);
        Uart_SetHalfDuplexMode(HALF_DUPLEX_RX_MODE);
        firstTime = false;
    }
    halfDuplexUart = id;
    halfDuplexEnabled = true;
}

void Uart_DisableHalfDuplex(UartId id)
{
    halfDuplexUart = -1;
    halfDuplexEnabled = false;
}

static void Uart_SetHalfDuplexMode(bool mode)
{
    Mt3620_Gpio_Write(HALF_DUPLEX_PIN, mode);
    // TODO Technically we should wait for 3.5 bits here for Modbus
}

// toggle the gpio off after a write
bool CheckForCompletedTranmission()
{
    UartInfo *unit = &uarts[halfDuplexUart];
    uint32_t temtStatus = ReadReg32(unit->baseAddr, LSR_OFFSET) & TEMT_MASK;
    if (unit->oneByteLeft == true && unit->txStarted)
    {
        if (temtStatus)
        {
            if (halfDuplexEnabled)
            {
                Uart_SetHalfDuplexMode(HALF_DUPLEX_RX_MODE);
            }
            unit->oneByteLeft = false;
            unit->txStarted = false;
            return true;
        }
        else
        {
            return false;
        }
    }
    else
    {
        return true;
    }
}

bool SetSerialConfig(uint8_t *configSetup, size_t len, UartId id, Callback callback)
{
    if (len < UART_CFG_MESSAGE_LENGTH)
    {
        return false;
    }

    UartInfo *unit = &uarts[id];
    unit->lcr = 0;

    unit->upperDivisor = configSetup[BAUD_RATE_OFFSET_UPPER];
    unit->lowerDivisor = configSetup[BAUD_RATE_OFFSET_LOWER];
    
    if (configSetup[DUPLEX_MODE_OFFSET])
    {
        Uart_EnableHalfDuplex(id);
    }

    // Default to parity disabled
    if (configSetup[PARITY_STATE_OFFSET])
    {
        unit->lcr |= PARITY_BIT_ON;
    }

    // Default to odd parity
    if (configSetup[PARITY_MODE_OFFSET])
    {
        unit->lcr |= PARITY_BIT_EVEN;
    }

    // Default to 1 stop bit
    if (configSetup[STOP_BITS_OFFSET] == 2)
    {
        unit->lcr |= STOP_BITS_2;
    }

    // Default to 5 bits per character
    if (configSetup[WORD_LENGTH_OFFSET] == 6)
    {
        unit->lcr |= WORD_LENGTH_6;
    }
    else if (configSetup[WORD_LENGTH_OFFSET] == 7)
    {
        unit->lcr |= WORD_LENGTH_7;
    }
    else if (configSetup[WORD_LENGTH_OFFSET] == 8)
    {
        unit->lcr |= WORD_LENGTH_8;
    }

    Uart_Init(id, callback);
    return true;
}
