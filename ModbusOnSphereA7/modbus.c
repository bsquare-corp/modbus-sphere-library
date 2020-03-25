/**
 * @file    modbus.c
 * @brief   A library for creating, sending and receiving modbus messages across Serial through the M4 or across TCP.
 *
 * @author  Copyright (c) Bsquare EMEA 2020. https://www.bsquare.com/
 *          Licensed under the MIT License.
*/

#include "modbus.h"
#include "../modbusCommon.h"
#include "../crc-util.h"
#include "epoll_timerfd_utilities.h"
#include <applibs/application.h>
#include <applibs/log.h>
#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>

static const char rtAppComponentId[] = "005180bc-402f-4cb3-a662-72937dbcde47";

/* Other definitions */
#define WRITE_RESPONSE_START 2
#define WRITE_RESPONSE_BYTES 4
#define TCP_LENGTH_MSB_OFFSET 4
#define TCP_LENGTH_LSB_OFFSET 5
#define TCP_HEADER_LENGTH 6
#define MODBUS_EXCEPTION_BIT 0x80

#define MESSAGE_HEADER_LENGTH 4

/* Values for overrun detection */
//#define BUFFER_CHECK_ON // Uncomment to turn buffer checking on.
#ifdef BUFFER_CHECK_ON
#define BUFFER_ZONE_SIZE 16
#define BUFFER_ZONE_VAL1 0xca
#define BUFFER_ZONE_VAL2 0xbc
#endif

// Helper define for filling the header - requires _a exists and is the right size.
#define SET_MODBUS_HEADER(_a, _b, _c, _d, _e)                                                                          \
    do                                                                                                                 \
    {                                                                                                                  \
        /*slave id*/                                                                                                   \
        _a[0] = (_b);                                                                                                  \
        /*function code*/                                                                                              \
        _a[1] = (_c);                                                                                                  \
        /*data (first address to write to in the device)*/                                                             \
        _a[2] = (uint8_t)(((_d) >> 8) & 0xFF);                                                                         \
        _a[3] = (uint8_t)((_d)&0xFF);                                                                                  \
        /*data (content to write to)*/                                                                                 \
        _a[4] = (uint8_t)(((_e) >> 8) & 0xFF);                                                                         \
        _a[5] = (uint8_t)((_e)&0xFF);                                                                                  \
    } while (0);

/*
Type: modbus_transport_type

Description:
    Determines the protocol used to transmit the message.

Values:
    tcp: Sending data using EtherNet.
    rtuOverTcp: Sending an rtu package using EtherNet
    rtu: Sending from the A7 to the M4 processors on the Microsoft� sphere.
*/
typedef enum
{
    tcp,
    rtuOverTcp,
    rtu
} modbusTransportType_t;
typedef enum
{
    success,
    failure,
    waiting
} messageHandlerState_t;
struct _TCP
{
    char *ip;
    uint16_t port;
};
struct _RTU
{
    uint16_t baudRate;
    uint8_t halfDuplexMode;
    uint8_t parityMode;
    uint8_t parityState;
    uint8_t stopBits;
    uint8_t wordLength;
};

union _connectData {
    struct _TCP TCP;
    struct _RTU RTU;
};

typedef enum
{
    Idle,
    SendingRequest,
    WaitingForResponse,
    DataReceived,
    TransactionFailed,
    Disconnected
} MODBUS_STATE;

struct _modbus_t
{
    modbusTransportType_t type;     // The method of data transfer being used
    union _connectData connectData; // Any additional data required for that transfer method.
    int fd;                         // The file descriptor
    uint16_t transactionId;         // Used to check TCP responses
    uint16_t lastTransactionId;     // Used to check for wraparound when overflowing the transaction identifier
    MODBUS_STATE state;             // The current state of a transaction
    uint16_t bufferedMessageLength; // The current length of the data written since the last successful read
    uint16_t pduLength;             // After a successful read it will be length of valid data in the pdu buffer
    bool isCFG;                     // Bool to let the device know to add a modbus header or a config header.
    uint8_t
        bufferedMessage[MAX_PDU_LENGTH]; // The buffer storing data since the last successful message from the device
#ifdef BUFFER_CHECK_ON
    uint8_t bufferZone1[BUFFER_ZONE_SIZE]; // Debug only - used to detect possible buffer overrun
#endif
    uint8_t pdu[MAX_PDU_LENGTH]; // The data buffer used to store the pdu received from the device
#ifdef BUFFER_CHECK_ON
    uint8_t bufferZone2[BUFFER_ZONE_SIZE]; // Debug only - used to detect possible buffer overrun
#endif
};
typedef struct _modbus_t *modbus_t;

/// Forward declarations
static modbus_t ModbusConnectIp(const char* ip, uint16_t port);
static void *EpollThread(void *ptr);
static bool ModBusWrite(modbus_t hndl, uint8_t *modBusPacket, uint16_t packetLength);
static messageHandlerState_t ModBusRead(modbus_t hndl);
static bool SendToSlave(modbus_t hndl, uint8_t *modBusADU, int pduLength);
static messageHandlerState_t MessageHandler(modbus_t handl, uint8_t *message, uint16_t inputLength);
static uint16_t GetFcodeLength(uint8_t fCode, uint8_t dataLength);
static bool WaitForData(modbus_t hndl, size_t timeout);
static uint16_t PduDataLength(modbus_t hndl, uint16_t expected);
static MODBUS_STATE NotReadyReason(modbus_t hndl);
#ifdef BUFFER_CHECK_ON
static void SetBufferZones(modbus_t hndl);
static bool BufferZonesValid(modbus_t hndl);
#endif
static bool WriteSerialConfig(modbus_t hndl, uint8_t *receivedMessage, size_t timeout);

/// Static variables used by whole modbus system
static int epollFd = -1;
static int sockFd = -1;
static pthread_t epollThreadId = NULL;
static bool epollThreadContinue = true;
static uint16_t transactionIdentifier = 0;

/// Publically available functions
bool ModbusInit(void)
{
    epollFd = CreateEpollFd();
    if (epollFd < 0)
    {
        return false;
    }
    // Set up epoll thread
    int err = pthread_create(&epollThreadId, NULL, &EpollThread, NULL);
    if (err != 0)
    {
        Log_Debug("Unable to create Modbus Epoll thread - %d\n", err);
        return false;
    }
    epollThreadContinue = true;
    return true;
}

modbus_t ModbusConnectRtu(serialSetup setup, size_t timeout)
{
    modbus_t hndl = malloc(sizeof(struct _modbus_t));
    if (hndl)
    {
        memset(hndl, 0, sizeof(struct _modbus_t));
        // Open connection to real-time capable application.
        sockFd = Application_Socket(rtAppComponentId);
        if (sockFd == -1)
        {
            Log_Debug("Error: Unable to create Application socket: %d (%s)\n", errno, strerror(errno));
            free(hndl);
            return NULL;
        }
        struct epoll_event event;
        event.events = EPOLLIN | EPOLLRDHUP | EPOLLHUP;
        event.data.ptr = hndl;
        bool epollAddOk = true;

        if (epoll_ctl(epollFd, EPOLL_CTL_ADD, sockFd, &event) < 0)
        {
            // If the Add fails, retry with the Modify as the file descriptor has already been
            // added to the epoll set after it was removed by the kernel upon its closure.
            if (epoll_ctl(epollFd, EPOLL_CTL_MOD, sockFd, &event) < 0)
            {
                Log_Debug("Error: Unable to add socket to Epoll system: %d\n", errno);
                epollAddOk = false;
            }
        }
        if (epollAddOk)
        {
            hndl->type = rtu;
            hndl->fd = sockFd;
            hndl->state = Idle;
#ifdef BUFFER_CHECK_ON
            SetBufferZones(hndl);
#endif
            hndl->connectData.RTU.baudRate = setup.baudRate;
            hndl->connectData.RTU.halfDuplexMode = setup.duplexMode;
            hndl->connectData.RTU.parityState = setup.parityState;
            hndl->connectData.RTU.parityMode = setup.parityMode;
            hndl->connectData.RTU.wordLength = setup.wordLength;
            hndl->connectData.RTU.stopBits = setup.stopBits;
            uint8_t receivedMessage[4];
            Log_Debug("Config sent\n");
            WriteSerialConfig(hndl, receivedMessage, timeout);
        }
        else
        {
            close(sockFd);
            free(hndl);
            hndl = NULL;
            return NULL;
        }
    }
    return hndl;
}

modbus_t ModbusConnectRtuOverTcp(const char* ip, uint16_t port) {
    modbus_t hndl;
    hndl = ModbusConnectIp(ip, port);
    if (hndl) {
        hndl->type = rtuOverTcp;
    }
    return hndl;
}

modbus_t ModbusConnectTcp(const char* ip, uint16_t port) {
    modbus_t hndl;
    hndl = ModbusConnectIp(ip, port);
    if (hndl) {
        hndl->type = tcp;
    }
    return hndl;
}

static modbus_t ModbusConnectIp(const char *ip, uint16_t port)
{
    Log_Debug("Modbus TCP connecting to %s\n", ip);
    modbus_t hndl = (modbus_t)malloc(sizeof(struct _modbus_t));
    if (hndl)
    {
        memset(hndl, 0, sizeof(struct _modbus_t));
        int socket_desc;
        struct sockaddr_in server;

        // Create socket
        socket_desc = socket(AF_INET, SOCK_STREAM, 0);
        if (socket_desc == -1)
        {
            Log_Debug("Error: Could not create socket\n");
            free(hndl);
            return NULL;
        }

        server.sin_addr.s_addr = inet_addr(ip);
        server.sin_family = AF_INET;
        server.sin_port = htons(port);

        // Connect to remote server
        if (connect(socket_desc, (struct sockaddr *)&server, sizeof(server)) < 0)
        {
            Log_Debug("Error: Could not connect. errno: %d\n", errno);
            close(socket_desc);
            free(hndl);
            hndl = NULL;
            return NULL;
        }
        else
        {
            Log_Debug("Server successfully connected\n");
            struct epoll_event event;
            event.events = EPOLLIN | EPOLLRDHUP | EPOLLHUP;
            event.data.ptr = hndl;
            bool epollAddOk = true;

            if (epoll_ctl(epollFd, EPOLL_CTL_ADD, socket_desc, &event) < 0)
            {
                // If the Add fails, retry with the Modify as the file descriptor has already been
                // added to the epoll set after it was removed by the kernel upon its closure.
                if (epoll_ctl(epollFd, EPOLL_CTL_MOD, socket_desc, &event) < 0)
                {
                    Log_Debug("Error: Unable to add socket to Epoll system. errno %d\n", errno);
                    epollAddOk = false;
                }
            }
            if (epollAddOk)
            {
                hndl->connectData.TCP.ip = strdup(ip);
                hndl->connectData.TCP.port = port;
                hndl->lastTransactionId = 0;
                hndl->fd = socket_desc;
                hndl->state = Idle;
#ifdef BUFFER_CHECK_ON
                SetBufferZones(hndl);
#endif
            }
            else
            {
                close(socket_desc);
                free(hndl);
                hndl = NULL;
            }
        }

    }
    return hndl;
}

void ModbusClose(modbus_t hndl)
{
    if (hndl)
    {
        if (hndl->type == tcp)
        {
            if (hndl->connectData.TCP.ip)
            {
                free(hndl->connectData.TCP.ip);
            }
            // Remove callback from ePoll
            epoll_ctl(epollFd, EPOLL_CTL_DEL, hndl->fd, NULL);
            close(hndl->fd);
        }
        free(hndl);
    }
}

void ModbusExit(void)
{
    if (epollThreadId)
    {
        epollThreadContinue = false;
        pthread_join(epollThreadId, NULL);
    }
    if (epollFd >= 0)
    {
        close(epollFd);
    }
}

/*------Read------*/
bool ReadCoils(modbus_t hndl, uint8_t slaveID, uint16_t address, uint16_t bitsToRead, uint8_t *readArray,
               size_t timeout)
{
    // create structure to send
    uint8_t bytesToRead = (uint8_t)(bitsToRead / 8);
    if (bitsToRead % 8)
    {
        bytesToRead++;
    }
    uint8_t modBusMessage[6];

    if (hndl->state != Idle)
    {
        Log_Debug("Call to %s while Handle not Idle\n", __FUNCTION__);
        readArray[0] = NotReadyReason(hndl);
        return false;
    }

    SET_MODBUS_HEADER(modBusMessage, slaveID, READ_COILS, address, bitsToRead);
    hndl->isCFG = false;
    // write structure
    if (!ModBusWrite(hndl, modBusMessage, sizeof(modBusMessage)))
    {
        readArray[0] = MESSAGE_SEND_FAIL;
        return false;
    }
    // read response into array
    // deal with timeout due to no response
    if (!WaitForData(hndl, timeout))
    {
        readArray[0] = MODBUS_TIMEOUT;
        return false;
    }

    // if the response returns an exception, passes the exception code and returns false
    if (hndl->pdu[1] & MODBUS_EXCEPTION_BIT)
    {
        readArray[0] = hndl->pdu[2];
        return false;
    }
    else if (hndl->pdu[1] != READ_COILS)
    {
        Log_Debug("Error: Wrong Function code returned\n");
        readArray[0] = INVALID_RESPONSE;
        return false;
    }
    else
    {
        // copy the message to the array (with all other data stripped)
        memcpy(readArray, &hndl->pdu[PDU_HEADER_LENGTH], PduDataLength(hndl, bytesToRead));
    }
    // return true if success*/
    return true;
}

bool ReadDiscreteInputs(modbus_t hndl, uint8_t slaveID, uint16_t address, uint16_t bitsToRead, uint8_t *readArray,
                        size_t timeout)
{
    // create structure to send
    uint8_t bytesToRead = (uint8_t)(bitsToRead / 8);
    if (bitsToRead % 8)
    {
        bytesToRead++;
    }
    uint8_t modBusMessage[6];

    if (hndl->state != Idle)
    {
        Log_Debug("Call to %s while Handle not Idle\n", __FUNCTION__);
        readArray[0] = NotReadyReason(hndl);
        return false;
    }

    SET_MODBUS_HEADER(modBusMessage, slaveID, READ_DISCRETE_INPUTS, address, bitsToRead);

    hndl->isCFG = false;
    // write structure
    if (!ModBusWrite(hndl, modBusMessage, 6))
    {
        readArray[0] = MESSAGE_SEND_FAIL;
        return false;
    }
    // read response into array
    // deal with timeout due to no response
    if (!WaitForData(hndl, timeout))
    {
        readArray[0] = MODBUS_TIMEOUT;
        return false;
    }
    // if the response returns an exception, pass it through the read array and return false
    if (hndl->pdu[1] & MODBUS_EXCEPTION_BIT)
    {
        readArray[0] = hndl->pdu[2];
        return false;
    }
    else if (hndl->pdu[1] != READ_DISCRETE_INPUTS)
    {
        Log_Debug("Error: Wrong Function code returned\n");
        readArray[0] = INVALID_RESPONSE;
        return false;
    }
    else
    {
        // copy the message to the array (with all other data stripped)
        memcpy(readArray, &hndl->pdu[PDU_HEADER_LENGTH], PduDataLength(hndl, bytesToRead));
    }
    // return true if success
    return true;
}

bool ReadMultipleHoldingRegisters(modbus_t hndl, uint8_t slaveID, uint16_t address, uint16_t registersToRead,
                                  uint16_t *readArray, size_t timeout)
{
    // create structure to send
    uint8_t modBusMessage[6];

    if (hndl->state != Idle)
    {
        Log_Debug("Call to %s while Handle not Idle\n", __FUNCTION__);
        readArray[0] = NotReadyReason(hndl);
        return false;
    }

    SET_MODBUS_HEADER(modBusMessage, slaveID, READ_MULTIPLE_HOLDING_REGISTERS, address, registersToRead);

    hndl->isCFG = false;
    // write structure
    if (!ModBusWrite(hndl, modBusMessage, 6))
    {
        readArray[0] = MESSAGE_SEND_FAIL;
        return false;
    }
    // read response into array
    // deal with timeout due to no response
    if (!WaitForData(hndl, timeout))
    {
        readArray[0] = MODBUS_TIMEOUT;
        return false;
    }
    // if the response returns an exception, pass it through the read array and return false
    if (hndl->pdu[1] & MODBUS_EXCEPTION_BIT)
    {
        readArray[0] = hndl->pdu[2];
        return false;
    }
    else if (hndl->pdu[1] != READ_MULTIPLE_HOLDING_REGISTERS)
    {
        Log_Debug("Error: Wrong Function code returned\n");
        readArray[0] = INVALID_RESPONSE;
        return false;
    }
    else
    {
        // copy the message to the array (with all other data stripped)
        int dataLength = PduDataLength(hndl, (uint16_t)(registersToRead * 2)) / 2;
        for (int i = 0; i < dataLength; i++)
        {
            // Don't use memcpy to ensure correct endianness
            readArray[i] = (uint16_t)((hndl->pdu[(i * 2) + 3] << 8) | hndl->pdu[(i * 2) + 4]);
        }
    }
    // return true if success
    return true;
}

bool ReadInputRegisters(modbus_t hndl, uint8_t slaveID, uint16_t address, uint16_t registersToRead, uint16_t *readArray,
                        size_t timeout)
{
    // create structure to send
    uint8_t modBusMessage[6];

    if (hndl->state != Idle)
    {
        Log_Debug("Call to %s while Handle not Idle\n", __FUNCTION__);
        readArray[0] = NotReadyReason(hndl);
        return false;
    }

    SET_MODBUS_HEADER(modBusMessage, slaveID, READ_INPUT_REGISTERS, address, registersToRead);
    hndl->isCFG = false;
    // write structure
    if (!ModBusWrite(hndl, modBusMessage, 6))
    {
        readArray[0] = MESSAGE_SEND_FAIL;
        return false;
    }
    // read response into array
    if (!WaitForData(hndl, timeout))
    {
        readArray[0] = MODBUS_TIMEOUT;
        return false;
    }
    // if the response returns an exception, pass it through the read array and return false
    if (hndl->pdu[1] & MODBUS_EXCEPTION_BIT)
    {
        readArray[0] = hndl->pdu[2];
        return false;
    }
    else if (hndl->pdu[1] != READ_INPUT_REGISTERS)
    {
        Log_Debug("Error: Wrong Function code returned\n");
        readArray[0] = INVALID_RESPONSE;
        return false;
    }
    else
    {
        // copy the message to the array (with all other data stripped)
        int datasize = PduDataLength(hndl, (uint16_t)(registersToRead * 2)) / 2;
        for (int i = 0; i < datasize; i++)
        {
            // Don't use memcpy to ensure correct endianness
            readArray[i] = (uint16_t)((hndl->pdu[(i * 2) + 3] << 8) | hndl->pdu[(i * 2) + 4]);
        }
    }
    // return true if success
    return true;
}

// TODO: Passive read still in progress
bool PassiveRead(modbus_t hndl, uint8_t *readArray, uint8_t bytesToRead, size_t timeout)
{
    if (!WaitForData(hndl, timeout))
    {
        readArray[0] = MODBUS_TIMEOUT;
        return false;
    }
    memcpy(readArray, &hndl->pdu[PDU_HEADER_LENGTH], (bytesToRead > hndl->pdu[2]) ? hndl->pdu[2] : bytesToRead);
    return false;
}

/*------Write------*/
bool WriteSingleCoil(modbus_t hndl, uint8_t slaveID, uint16_t address, bool bit, uint8_t *readArray, size_t timeout)
{
    // create structure to send
    uint8_t modBusMessage[6];

    if (hndl->state != Idle)
    {
        Log_Debug("Call to %s while Handle not Idle\n", __FUNCTION__);
        readArray[0] = NotReadyReason(hndl);
        return false;
    }

    SET_MODBUS_HEADER(modBusMessage, slaveID, WRITE_SINGLE_COIL, address, (bit) ? 0xff00 : 0x00);

    hndl->isCFG = false;
    // write structure
    if (!ModBusWrite(hndl, modBusMessage, 6))
    {
        readArray[0] = MESSAGE_SEND_FAIL;
        return false;
    }
    // read response into array
    // deal with timeout due to no response
    if (!WaitForData(hndl, timeout))
    {
        readArray[0] = MODBUS_TIMEOUT;
        return false;
    }

    // if the response returns an exception, pass it through the read array and return false
    if (hndl->pdu[1] & MODBUS_EXCEPTION_BIT)
    {
        readArray[0] = hndl->pdu[2];
        return false;
    }
    else if (hndl->pdu[1] != WRITE_SINGLE_COIL)
    {
        Log_Debug("Error: Wrong Function code returned\n");
        readArray[0] = INVALID_RESPONSE;
        return false;
    }
    else
    {
        // copy the message to the array (with all other data stripped)
        memcpy(readArray, &hndl->pdu[WRITE_RESPONSE_START], WRITE_RESPONSE_BYTES);
    }
    return true;
}

bool WriteSingleHoldingRegister(modbus_t hndl, uint8_t slaveID, uint16_t address, uint16_t mbRegister,
                                uint8_t *readArray, size_t timeout)
{
    // create structure to send
    uint8_t modBusMessage[6];

    if (hndl->state != Idle)
    {
        Log_Debug("Call to %s while Handle not Idle\n", __FUNCTION__);
        readArray[0] = NotReadyReason(hndl);
        return false;
    }

    SET_MODBUS_HEADER(modBusMessage, slaveID, WRITE_SINGLE_HOLDING_REGISTER, address, mbRegister);

    hndl->isCFG = false;
    // write structure
    if (!ModBusWrite(hndl, modBusMessage, 6))
    {
        readArray[0] = MESSAGE_SEND_FAIL;
        return false;
    }
    // read response into array
    if (!WaitForData(hndl, timeout))
    {
        readArray[0] = MODBUS_TIMEOUT;
        return false;
    }
    // if the response returns an exception, pass it through the read array and return false
    if (hndl->pdu[1] & MODBUS_EXCEPTION_BIT)
    {
        readArray[0] = hndl->pdu[2];
        return false;
    }
    else if (hndl->pdu[1] != WRITE_SINGLE_HOLDING_REGISTER)
    {
        Log_Debug("Error: Wrong Function code returned\n");
        readArray[0] = INVALID_RESPONSE;
        return false;
    }
    else
    {
        // copy the message to the array (with all other data stripped)
        memcpy(readArray, &hndl->pdu[WRITE_RESPONSE_START], WRITE_RESPONSE_BYTES);
    }
    return true;
}

bool WriteMultipleCoils(modbus_t hndl, uint8_t slaveID, uint16_t address, uint16_t numToWrite, uint8_t *bitArray,
                        uint8_t *readArray, size_t timeout)
{
    // create structure to send
    uint8_t dataByteCount = (uint8_t)(numToWrite / 8 + (numToWrite & 0x7) ? 1 : 0);
    uint8_t modBusMessage[MAX_PDU_LENGTH];

    if (hndl->state != Idle)
    {
        Log_Debug("Call to %s while Handle not Idle\n", __FUNCTION__);
        readArray[0] = NotReadyReason(hndl);
        return false;
    }

    SET_MODBUS_HEADER(modBusMessage, slaveID, WRITE_MULTIPLE_COILS, address, numToWrite);

    // data (number of bytes to write)
    modBusMessage[6] = dataByteCount;
    // data (content to write to)
    memcpy(&modBusMessage[7], bitArray, dataByteCount);
    hndl->isCFG = false;
    // write structure
    if (!ModBusWrite(hndl, modBusMessage, (uint16_t)(7 + dataByteCount)))
    {
        readArray[0] = MESSAGE_SEND_FAIL;
        return false;
    }
    // read response into array
    if (!WaitForData(hndl, timeout))
    {
        readArray[0] = MODBUS_TIMEOUT;
        return false;
    }
    // if the response returns an exception, pass it through the read array and return false
    if (hndl->pdu[1] & MODBUS_EXCEPTION_BIT)
    {
        readArray[0] = hndl->pdu[2];
        return false;
    }
    else if (hndl->pdu[1] != WRITE_MULTIPLE_COILS)
    {
        Log_Debug("Error: Wrong Function code returned\n");
        readArray[0] = INVALID_RESPONSE;
        return false;
    }
    else
    {
        // copy the message to the array (with all other data stripped)
        memcpy(readArray, &hndl->pdu[WRITE_RESPONSE_START], WRITE_RESPONSE_BYTES);
    }
    return true;
}

bool WriteMultipleHoldingRegisters(modbus_t hndl, uint8_t slaveID, uint16_t address, uint16_t numToWrite,
                                   uint16_t *registerArray, uint8_t *readArray, size_t timeout)
{
    // create structure to send
    uint8_t dataByteCount = (uint8_t)(numToWrite * 2);
    uint8_t modBusMessage[MAX_PDU_LENGTH];

    if (hndl->state != Idle)
    {
        Log_Debug("Call to %s while Handle not Idle\n", __FUNCTION__);
        readArray[0] = NotReadyReason(hndl);
        return false;
    }

    SET_MODBUS_HEADER(modBusMessage, slaveID, WRITE_MULTIPLE_HOLDING_REGISTERS, address, numToWrite);

    // data (number of bytes to write)
    modBusMessage[6] = dataByteCount;
    // data (content to write to)
    for (int i = 0; i < (dataByteCount / 2); i++)
    {
        // Don't use memcpy to ensure correct endianness
        modBusMessage[(2 * i) + 7] = (uint8_t)((registerArray[i] >> 8) & 0xFF);
        modBusMessage[(2 * i) + 8] = (uint8_t)(registerArray[i] & 0xFF);
    }
    hndl->isCFG = false;
    // write structure
    if (!ModBusWrite(hndl, modBusMessage, (uint8_t)(7 + dataByteCount)))
    {
        readArray[0] = MESSAGE_SEND_FAIL;
        return false;
    }
    // read response into array
    if (!WaitForData(hndl, timeout))
    {
        readArray[0] = MODBUS_TIMEOUT;
        return false;
    }
    // if the response returns an exception, pass it through the readArray and return false
    if (hndl->pdu[1] & MODBUS_EXCEPTION_BIT)
    {
        readArray[0] = hndl->pdu[2];
        return false;
    }
    else if (hndl->pdu[1] != WRITE_MULTIPLE_HOLDING_REGISTERS)
    {
        Log_Debug("Error: Wrong Function code returned\n");
        readArray[0] = INVALID_RESPONSE;
        return false;
    }
    else
    {
        // copy the message to the array (with all other data stripped)
        memcpy(readArray, &hndl->pdu[WRITE_RESPONSE_START], WRITE_RESPONSE_BYTES);
    }
    return true;
}

uint8_t ReadFileSubRequestBuilder(uint8_t* messageArray, uint8_t currentMessageIndex, uint16_t fileNumber, uint16_t recordNumber, uint8_t recordLength)
{
    //Modbus specification states that the reference type must be set to 6.
    messageArray[currentMessageIndex] = 6;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
    messageArray[currentMessageIndex + 1] = fileNumber >> 8;
    messageArray[currentMessageIndex + 2] = fileNumber & 0xFF;
    messageArray[currentMessageIndex + 3] = recordNumber >> 8;
    messageArray[currentMessageIndex + 4] = recordNumber & 0xFF;
    messageArray[currentMessageIndex + 5] = 0;
    messageArray[currentMessageIndex + 6] = recordLength;
    return currentMessageIndex + 7;
#pragma GCC diagnostic pop
}

bool ReadFile(modbus_t hndl, uint8_t slaveID, uint8_t* messageArray, uint8_t messageLength, uint8_t* readArray, size_t timeout)
{
    uint16_t expectedMessageLength=0;
    if (hndl->state != Idle)
    {
        Log_Debug("Call to %s while Handle not Idle\n", __FUNCTION__);
        readArray[0] = NotReadyReason(hndl);
        return false;
    }

    uint8_t modbusMessage[MAX_PDU_LENGTH + PDU_HEADER_LENGTH];
    modbusMessage[0] = slaveID;
    modbusMessage[1] = READ_FILE;
    modbusMessage[2] = messageLength;
    if(messageLength <  MAX_PDU_LENGTH)
    {
    memcpy(&modbusMessage[3], messageArray, messageLength);
    }
    else 
    {
        readArray[0] = MESSAGE_SEND_FAIL;
        return false;
    }
    for (int i = 0; i < messageLength; i++)
    {
    //finds the sum of the record lengths of each subrequest combined with length of the subrequests themselves to find the total expected length of the response (to help with validation)
        if (i % 7 == 6)
        {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
            expectedMessageLength += (messageArray[i] * 2) + 2;
#pragma GCC diagnostic pop
        }
    }

    hndl->isCFG = false;
    // write structure
    if (!ModBusWrite(hndl, modbusMessage, (uint16_t)(3 + messageLength)))
    {
        readArray[0] = MESSAGE_SEND_FAIL;
        return false;
    }
    // read response into array
    // deal with timeout due to no response
    if (!WaitForData(hndl, timeout))
    {
        readArray[0] = MODBUS_TIMEOUT;
        return false;
    }    
    // if the response returns an exception, pass it through the readArray and return false
    if (hndl->pdu[1] & MODBUS_EXCEPTION_BIT)
    {
        readArray[0] = hndl->pdu[2];
        return false;
    }
    else if (hndl->pdu[1] != READ_FILE)
    {
        Log_Debug("Error: Wrong Function code returned\n");
        readArray[0] = INVALID_RESPONSE;
        return false;
    }
    else
    {
        // copy the message to the array (with all other data stripped)
        memcpy(readArray, &hndl->pdu[PDU_HEADER_LENGTH], PduDataLength(hndl, expectedMessageLength));
        return true;
    }
}

uint8_t WriteFileSubRequestBuilder(uint8_t* messageArray, uint8_t currentMessageIndex, uint16_t fileNumber, uint16_t recordNumber, uint8_t recordLength, uint16_t* record)
{
    messageArray[currentMessageIndex] = 6;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
    messageArray[currentMessageIndex + 1] = fileNumber >> 8;
    messageArray[currentMessageIndex + 2] = fileNumber & 0xFF;
    messageArray[currentMessageIndex + 3] = recordNumber >> 8;
    messageArray[currentMessageIndex + 4] = recordNumber & 0xFF;
    messageArray[currentMessageIndex + 5] = 0;
    messageArray[currentMessageIndex + 6] = recordLength;
    for (int i = 0; i < recordLength; i++)
    {
        messageArray[currentMessageIndex + 7 + (i * 2)] = (record[i]>>8) & 0xFF;
        messageArray[currentMessageIndex + 8 + (i * 2)] = record[i] & 0xFF;
    }
    return currentMessageIndex + 7 + (recordLength * 2);
#pragma GCC diagnostic pop
}

bool WriteFile(modbus_t hndl, uint8_t slaveID, uint8_t* messageArray, uint8_t messageLength, uint8_t* readArray, size_t timeout) {
    if (hndl->state != Idle)
    {
        Log_Debug("Call to %s while Handle not Idle\n", __FUNCTION__);
        readArray[0] = NotReadyReason(hndl);
        return false;
    }

    uint8_t modbusMessage[MAX_PDU_LENGTH];
    modbusMessage[0] = slaveID;
    modbusMessage[1] = WRITE_FILE;
    modbusMessage[2] = messageLength;
    memcpy(&modbusMessage[3], messageArray, messageLength);


    hndl->isCFG = false;
    // write structure
    if (!ModBusWrite(hndl, modbusMessage, (uint16_t)(PDU_HEADER_LENGTH + messageLength)))
    {
        readArray[0] = MESSAGE_SEND_FAIL;
        return false;
    }
    // read response into array
    // deal with timeout due to no response
    if (!WaitForData(hndl, timeout))
    {
        readArray[0] = MODBUS_TIMEOUT;
        return false;
    }    
    // if the response returns an exception, pass it through the read array and return false
    if (hndl->pdu[1] & MODBUS_EXCEPTION_BIT)
    {
        readArray[0] = hndl->pdu[2];
        return false;
    }
    else if (hndl->pdu[1] != WRITE_FILE)
    {
        Log_Debug("Error: Wrong Function code returned\n");
        readArray[0] = INVALID_RESPONSE;
        return false;
    }
    else
    {
        // copy the message to the array (with all other data stripped)
        memcpy(readArray, &hndl->pdu[PDU_HEADER_LENGTH], PduDataLength(hndl, messageLength));
        return true;
    }
}

static bool WriteSerialConfig(modbus_t hndl, uint8_t *receivedMessage, size_t timeout)
{
    uint8_t serialConfigMessage[7];
    serialConfigMessage[BAUD_RATE_OFFSET_UPPER] = (uint8_t)((hndl->connectData.RTU.baudRate >> 8) & 0xFF);
    serialConfigMessage[BAUD_RATE_OFFSET_LOWER] = (uint8_t)(hndl->connectData.RTU.baudRate & 0xFF);
    serialConfigMessage[DUPLEX_MODE_OFFSET] = hndl->connectData.RTU.halfDuplexMode;
    serialConfigMessage[PARITY_STATE_OFFSET] = hndl->connectData.RTU.parityState;
    serialConfigMessage[PARITY_MODE_OFFSET] = hndl->connectData.RTU.parityMode;
    serialConfigMessage[STOP_BITS_OFFSET] = hndl->connectData.RTU.stopBits;
    serialConfigMessage[WORD_LENGTH_OFFSET] = hndl->connectData.RTU.wordLength;
    hndl->isCFG = true;
    // write structure
    if (!ModBusWrite(hndl, serialConfigMessage, 7))
    {
        receivedMessage[0] = MESSAGE_SEND_FAIL;
        return false;
    }
    // read response into array
    // deal with timeout due to no response
    if (!WaitForData(hndl, timeout))
    {
        receivedMessage[0] = MODBUS_TIMEOUT;
        return false;
    }
    if (receivedMessage[0] != 0)
    {
        return false;
    }
    return true;
}

/// Static functions

#ifdef BUFFER_CHECK_ON
static void SetBufferZones(modbus_t hndl)
{
    for (size_t i = 0; i < BUFFER_ZONE_SIZE; i++)
    {
        hndl->bufferZone1[i] = BUFFER_ZONE_VAL1;
        hndl->bufferZone2[i] = BUFFER_ZONE_VAL2;
    }
}

static bool BufferZonesValid(modbus_t hndl)
{
    for (size_t i = 0; i < BUFFER_ZONE_SIZE; i++)
    {
        if (hndl->bufferZone1[i] != BUFFER_ZONE_VAL1)
        {
            return false;
        }
        if (hndl->bufferZone2[i] != BUFFER_ZONE_VAL2)
        {
            return false;
        }
    }
    return true;
}
#endif

static void *EpollThread(void *ptr)
{
    Log_Debug("Starting Modbus Thread\n");
    while (epollThreadContinue)
    {
        struct epoll_event event;
        int numEventsOccurred = epoll_wait(epollFd, &event, 1, 1000);

        if (numEventsOccurred == -1)
        {
            if (errno == EINTR)
            {
                // interrupted by signal, e.g. due to breakpoint being set; ignore
            }
            continue;
        }

        if (numEventsOccurred == 1 && event.data.ptr != NULL)
        {
            modbus_t mh = (modbus_t)event.data.ptr;
            if (mh->state == Disconnected) {
                // There may well be lots of interrupts - silently ignore them so
                // the debug output is not flooded
                continue;
            }

            if (event.events & EPOLLIN)
            {
#ifdef BUFFER_CHECK_ON
                if (!BufferZonesValid(mh))
                {
                    Log_Debug("Probably buffer overrun detected\n");
                }
#endif
                messageHandlerState_t mhsState = ModBusRead(mh);
                if (mhsState == success)
                {
                    mh->state = DataReceived;
                }
                else if (mhsState == failure)
                {
                    mh->state = TransactionFailed;
                }
            }
            if (event.events & (EPOLLRDHUP | EPOLLHUP))
            {
                Log_Debug("Error: EPOLLRDHUP or EPOLLHUP has returned true. Reconnect required.\n");
                mh->state = Disconnected;
            }
        }
    }
    Log_Debug("Exiting Modbus Thread\n");
    return NULL;
}

static bool ModBusWrite(modbus_t hndl, uint8_t *modBusPacket, uint16_t packetLength)
{
    // Attach MBAP header to turn modbus PDU to modbus ADU
    hndl->state = SendingRequest;
    hndl->pduLength = 0;

    if (hndl->type == tcp)
    {
        uint8_t modBusPacketTCP[MAX_PDU_LENGTH + TCP_HEADER_LENGTH];
        memcpy(&modBusPacketTCP[TCP_HEADER_LENGTH], modBusPacket, packetLength);
        modBusPacketTCP[0] = (uint8_t)((transactionIdentifier >> 8) & 0xFF);
        modBusPacketTCP[1] = (uint8_t)(transactionIdentifier & 0xFF);
        modBusPacketTCP[2] = 0x00;
        modBusPacketTCP[3] = 0x00;
        modBusPacketTCP[4] = (uint8_t)((packetLength >> 8) & 0xFF);
        modBusPacketTCP[5] = (uint8_t)(packetLength & 0xFF);
        return SendToSlave(hndl, modBusPacketTCP, packetLength + TCP_HEADER_LENGTH);
    }
    else if (hndl->type == rtuOverTcp) 
    {
        uint8_t modBusPacketRTU[MAX_PDU_LENGTH + CRC_FOOTER_LENGTH];
        memcpy(modBusPacketRTU, modBusPacket, packetLength);
        AddCRC(modBusPacketRTU, packetLength, MAX_PDU_LENGTH);
        return SendToSlave(hndl, modBusPacketRTU, packetLength + CRC_FOOTER_LENGTH);
    }
    else if (hndl->type == rtu)
    {
        // CRC footer for the RTU is appended on M4, so no additional work is required here.
        uint8_t modBusPacketRTU[MAX_PDU_LENGTH + MESSAGE_HEADER_LENGTH];
        memcpy(&modBusPacketRTU[MESSAGE_HEADER_LENGTH], modBusPacket, packetLength);
        if (hndl->isCFG)
        {
            modBusPacketRTU[PROTOCOL_OFFSET] = UART;
            modBusPacketRTU[COMMAND_OFFSET] = UART_CFG_MESSAGE;
        }
        else
        {
            modBusPacketRTU[PROTOCOL_OFFSET] = MODBUS;
            modBusPacketRTU[COMMAND_OFFSET] = MODBUS_DATA_MESSAGE;
        }
        modBusPacketRTU[HEADER_LENGTH_OFFSET] = MESSAGE_HEADER_LENGTH;

        return SendToSlave(hndl, modBusPacketRTU, packetLength + MESSAGE_HEADER_LENGTH);
    }
    Log_Debug("Error: Handle type is unknown.\n");
    return false;
}

static messageHandlerState_t ModBusRead(modbus_t hndl)
{
    uint8_t message[MAX_PDU_LENGTH];
    
    int bytesReceived = recv(hndl->fd, message, sizeof(message), 0);
    return MessageHandler(hndl, message, (uint16_t)bytesReceived);
}

const char *ModbusErrorToString(uint8_t errorNo)
{
    switch (errorNo)
    {
    case ILLEGAL_FUNCTION:
        return "Exception: Illegal Function";
    case ILLEGAL_DATA_ADDRESS:
        return "Exception: Illegal data address";
    case ILLEGAL_DATA_VALUE:
        return "Exception: Illegal data value";
    case SLAVE_DEVICE_FAILURE:
        return "Exception: Slave device failure";
    case ACKNOWLEDGE:
        return "Exception: Acknowledge";
    case SLAVE_DEVICE_BUSY:
        return "Exception: Slave device busy";
    case NEGATIVE_ACKNOWLEDGE:
        return "Exception: Negative acknowledge";
    case MEMORY_PARITY_ERROR:
        return "Exception: Memory parity error";
    case GATEWAY_PATH_UNAVAILABLE:
        return "Exception: Gateway path unavailable";
    case GATEWAY_TARGET_DEVICE_FAILED_TO_RESPOND:
        return "Exception: Gateway target device failed to respond";
    case MODBUS_TIMEOUT:
        return "Exception: Timeout - Slave device failed to respond";
    case MESSAGE_SEND_FAIL:
        return "Exception: Message has failed to send";
    case HANDLE_IN_USE:
        return "Exception: Handle in Use";
    case INVALID_RESPONSE:
        return "Exception: Wrong Function Code returned from device";
    case DEVICE_DISCONNECTED:
        return "Exception: Device Disconnected - reconnect required";
    default:
        return "Exception: Unknown exception";
    }
}

static bool SendToSlave(modbus_t hndl, uint8_t *modBusADU, int pduLength)
{
    if (pduLength == send(hndl->fd, modBusADU, (size_t)pduLength, 0))
    {
        hndl->transactionId = transactionIdentifier++;
        hndl->state = WaitingForResponse;
        return true;
    }
    else
    {
        hndl->state = Idle;
        return false;
    }
}

/*
* Reads messages to determine the full message length, once it knows it has a full message it sets it in the handle and
returns the length.
* This function returns zero unless a full message has been received.
If the complete message will fit into the pdu buffer, it is copied in. If not, then zero is returned and the message
discarded.
*/
static messageHandlerState_t MessageHandler(modbus_t hndl, uint8_t *message, uint16_t inputLength)
{
    messageHandlerState_t ret = waiting;

    if (hndl->state != WaitingForResponse)
    {
        Log_Debug("Warning: Data received while not waiting for response. Discarding data.\n");
        hndl->bufferedMessageLength = 0;
        return ret;
    }
    // writing to buffer
    if (hndl->bufferedMessageLength + inputLength < MAX_PDU_LENGTH)
    {
        memcpy(&hndl->bufferedMessage[hndl->bufferedMessageLength], message, (size_t)inputLength);
        hndl->bufferedMessageLength = (uint16_t)(hndl->bufferedMessageLength + inputLength);
    }
    else
    {
        Log_Debug("Error: Message longer than %d bytes, discarding data\n", MAX_PDU_LENGTH);
        hndl->bufferedMessageLength = 0;
        return ret;
    }

    size_t minLength = 0;             // How much data do we need to find the message length?
    size_t fCodeOffset = 0;           // Position of Function Code in the message.
    size_t pduLengthOffset = 0;       // Position of data length (where used)
    size_t transportHeaderLength = 0; // Length of header removed when returning data
    size_t transportFooterLength = 0; // Length of footer removed when returning data
    bool checkTransaction = false;
    bool checkCRC = false;

    if (hndl->type == rtu)
    {
        minLength = MESSAGE_HEADER_LENGTH + PDU_HEADER_LENGTH;
        fCodeOffset = MESSAGE_HEADER_LENGTH + 1;
        pduLengthOffset = fCodeOffset + 1;
        transportHeaderLength = MESSAGE_HEADER_LENGTH;
    }
    else if (hndl->type == rtuOverTcp) {
        minLength = CRC_FOOTER_LENGTH + PDU_HEADER_LENGTH;
        fCodeOffset = 1;
        pduLengthOffset = fCodeOffset + 1;
        transportFooterLength = CRC_FOOTER_LENGTH;
        checkCRC = true;
    }
    else if (hndl->type == tcp)
    {
        minLength = TCP_HEADER_LENGTH + PDU_HEADER_LENGTH;
        fCodeOffset = TCP_HEADER_LENGTH + 1;
        pduLengthOffset = fCodeOffset + 1;
        transportHeaderLength = TCP_HEADER_LENGTH;
        checkTransaction = true;
    }
    else
    {
        Log_Debug("Error: Type not set to valid value (%d), discarding data\n", hndl->type);
        hndl->bufferedMessageLength = 0;
        return ret;
    }

    // reading buffer
    uint16_t pduMessageLength = 0;
    bool fullMessageAvailable = false;
    if (hndl->bufferedMessageLength >= minLength || hndl->type == rtu)
    {
        if (hndl->isCFG)
        {
            pduMessageLength = UART_CFG_MESSAGE_RESP_LENGTH;
        }
        else
        {
            pduMessageLength =
                (uint16_t)(GetFcodeLength(hndl->bufferedMessage[fCodeOffset], hndl->bufferedMessage[pduLengthOffset]));
        }
        if (hndl->bufferedMessageLength >= pduMessageLength + transportHeaderLength + transportFooterLength)
        {
            fullMessageAvailable = true;
        }
    }

    if (pduMessageLength > MAX_PDU_LENGTH)
    {
        Log_Debug("It broke here!\n");
    }

    if (fullMessageAvailable)
    {
        bool isTransactionTooLow = false;
        bool crcFailed = false;

        // First two bytes of the message in the TCP header are the ID.
        uint16_t rxTransaction = (uint16_t)(hndl->bufferedMessage[0] << 8 | hndl->bufferedMessage[1]);
        if (checkTransaction)
        {
            //Checks to see if the received message id is less than the expected message ID.
            if(hndl->transactionId != rxTransaction) 
            {
                //Detects if a wraparound has occured.
                if(hndl->transactionId > hndl->lastTransactionId)
                {
                    //If the received message is not between the last successfully received message and the expected message then it has not been
                    //requested yet and we should abort, otherwise we should get rid of the message and continue looking for the requested one.
                    if ((rxTransaction > hndl->transactionId) || (rxTransaction <= hndl->lastTransactionId))
                    {
                        //we are receiving a message from the future. panic.
                        Log_Debug("Transaction ID received has not been requested yet. Expect 0x%04x, got 0x%04x. Message discarded, search failed.\n",
                            hndl->transactionId, rxTransaction);
                        hndl->bufferedMessageLength = 0;
                        return failure;
                    }
                    else if (rxTransaction < hndl->transactionId)
                    {
                        //we are receiving a message from the past, keep checking until you find the correct one.
                        Log_Debug("Transaction ID belongs to a request that has timed out. Expect 0x%04x, got 0x%04x. Message discarded and search continued.\n",
                            hndl->transactionId, rxTransaction);
                        isTransactionTooLow = true;
                        ret = waiting;
                    }
                }
                else
                {
                    //If a wraparound has occured and a message is between the last successfully received message and the expected message then 
                    //its request has likely timed out and we should continue looking for the currently reqsted one, otherwise we should abort
                    //the search.
                    if((rxTransaction >= hndl->lastTransactionId) || (rxTransaction < hndl->transactionId))
                    {
                        //we are receiving a message from the past, keep checking until you find the correct one.
                        Log_Debug("Transaction ID belongs to a request that has timed out. Expect 0x%04x, got 0x%04x. Message discarded and search continued.\n",
                            hndl->transactionId, rxTransaction);
                        isTransactionTooLow = true;
                        ret = waiting;
                    }
                    else if (rxTransaction > hndl->transactionId)
                    {
                        //we are receiving a message from the future. panic
                        Log_Debug("Transaction ID received has not been requested yet. Expect 0x%04x, got 0x%04x. Message discarded, search failed.\n",
                            hndl->transactionId, rxTransaction);
                        hndl->bufferedMessageLength = 0;
                        return failure;
                    }
                }
            }
        }
        if (checkCRC)
            if (!ValidateCRC(hndl->bufferedMessage, pduMessageLength + CRC_FOOTER_LENGTH)) {
                Log_Debug("CRC check failed. Message discarded.\n");
                crcFailed = true;
            }
        // Pass back only the PDU portion of the message
        if (pduMessageLength <= MAX_PDU_LENGTH && !isTransactionTooLow && !crcFailed)
        {
            hndl->pduLength = pduMessageLength;
            hndl->lastTransactionId = rxTransaction;
            memcpy(hndl->pdu, &hndl->bufferedMessage[transportHeaderLength], pduMessageLength);
            ret = success;
        }
        // Keep data not part of this message by shifting it to the beginning of the buffer
        size_t totalMessageLength = pduMessageLength + transportHeaderLength + transportFooterLength;
        size_t remainingDataLength = (hndl->bufferedMessageLength - totalMessageLength);

        if (remainingDataLength > 0)
        {
            memmove(hndl->bufferedMessage, &hndl->bufferedMessage[totalMessageLength], remainingDataLength);
        }
        // Update the message size to match what is valid data in the buffer
        hndl->bufferedMessageLength = (uint16_t)remainingDataLength;
    }
    return ret;
}





static uint16_t GetFcodeLength(uint8_t fCode, uint8_t dataLength)
{
    if ((fCode > FCODE_ERROR_OFFSET) && (fCode <= FCODE_ERROR_OFFSET + FCODE_RANGE))
    {
        // Error response length is always three bytes
        return ERROR_CODE_LENGTH;
    }
    switch (fCode)
    {
    case READ_COILS:
        return (uint16_t)(PDU_HEADER_LENGTH + dataLength);
    case READ_DISCRETE_INPUTS:
        return (uint16_t)(PDU_HEADER_LENGTH + dataLength);
    case READ_MULTIPLE_HOLDING_REGISTERS:
        return (uint16_t)(PDU_HEADER_LENGTH + dataLength);
    case READ_INPUT_REGISTERS:
        return (uint16_t)(PDU_HEADER_LENGTH + dataLength);
    case READ_FILE:
        return (uint16_t)(PDU_HEADER_LENGTH + dataLength);
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
    case WRITE_FILE:
        return (uint16_t)(PDU_HEADER_LENGTH + dataLength);

    default:
        Log_Debug("Error: Unsupported function code.\n");
        return 0;
    }
}

/* timeout measured in milliseconds. A value of zero means never timeout.
 * Returns true if data is received, false on timeout.
 */
static bool WaitForData(modbus_t hndl, size_t timeout)
{
    bool retval = true;
    size_t counter = 0;

    while ((hndl->state != DataReceived) && (hndl->state != TransactionFailed) 
        && (hndl->state != Disconnected))
    {
        struct timespec t = {.tv_sec = 0, .tv_nsec = 100000};

        nanosleep(&t, NULL);

        if (timeout > 0 && counter++ > timeout)
        {
            break;
        }
    }

    if (hndl->state != DataReceived)
    {
        retval = false;
    }
    // The request is finished or timed out, so set state back to Idle
    hndl->state = Idle;

    return retval;
}

static uint16_t PduDataLength(modbus_t hndl, uint16_t expected)
{
    if (hndl->pduLength != expected + PDU_HEADER_LENGTH)
    {
        Log_Debug("Warning: Got %d bytes in pdu when expecting %d\n", hndl->pduLength, expected + PDU_HEADER_LENGTH);
    }
    return (uint16_t)(hndl->pduLength - PDU_HEADER_LENGTH);
}

static MODBUS_STATE NotReadyReason(modbus_t hndl)
{
    if (hndl->state == Disconnected) {
        return DEVICE_DISCONNECTED;
    }
    return HANDLE_IN_USE;
}
