/**
 * @file    modbus.h
 * @brief   A library for creating, sending and receiving modbus messages across Serial through the M4 or across TCP.
 *          Written using the following protocol spec: http://www.modbus.org/docs/Modbus_Application_Protocol_V1_1b.pdf
 *
 * @author  Copyright (c) Bsquare EMEA 2020. https://www.bsquare.com/
 *          Licensed under the MIT License.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct _modbus_t* modbus_t;

typedef struct _serialSetup
{
    uint16_t baudRate;
    uint8_t duplexMode;
    uint8_t parityMode;
    uint8_t parityState;
    uint8_t stopBits;
    uint8_t wordLength;
} serialSetup;


/// <summary>
/// Initialises the Epoll thread and sets up the relevant variables.
/// </summary>
/// <returns>true on success, or false on failure</returns>
bool ModbusInit( void );

/// <summary>
/// Closes the Epoll thread and cleans up relevant variables.
/// </summary>
void ModbusExit( void );


/// <summary>
/// Takes error code from a modbus message and returns the error message as a string.
/// </summary>
/// <param name="errorNo">The error code</param>
/// <returns>A string corresponding to the error code provided</returns>
const char* ModbusErrorToString( uint8_t errorNo );



/// <summary>
/// Creates and sets up a socket for TCP, and returns a message handle with all of the relevant information.
/// </summary>
/// <param name="ip">The IP address of the device to be connected to</param>
/// <param name="port">The port of the device to be connected to</param>
/// <returns>Modbus handle on success, or null on failure</returns>
modbus_t ModbusConnectTcp( const char* ip, uint16_t port );

/// <summary>
/// Creates and sets up a socket for RTU over TCP, and returns a message handle with all of the relevant information.
/// </summary>
/// <param name="ip">The IP address of the device to be connected to</param>
/// <param name="port">The port of the device to be connected to</param>
/// <returns>Modbus handle on success, or null on failure</returns>
modbus_t ModbusConnectRtuOverTcp( const char* ip, uint16_t port );



/// <summary>
/// Creates and set up a socket for serial data, and returns a message handle with all of the relevant information.
/// </summary>
/// <returns>Modbus handle on success, or null on failure</returns>
modbus_t ModbusConnectRtu( serialSetup setup, size_t timeout );


/// <summary>
/// Closes the connetion created by ModbusConnectIp/ModbusConnectRtu and frees the memory taken up by the handle.
/// </summary>
/// <param name="hndl">The modbus handle to be freed</param>
void ModbusClose( modbus_t hndl );


/*--------------------------READ FUNCTIONS----------------------------------*/


/// <summary>
/// Sends a request to read a variable number of coils.
/// </summary>
/// <param name="hndl">The message handle</param>
/// <param name="slaveID">Address of the slave device</param>
/// <param name="address">Address of the first coil to read on the device</param>
/// <param name="bitsToRead">Number of coils to read</param>
/// <param name="readArray">Pointer to an array to store read data in</param>
/// <param name="timeout">Time in milliseconds after which function will return an error if no response a has been received from the device</param>
/// <returns>true on success, or false on failure</returns>
bool ReadCoils( modbus_t hndl, uint8_t slaveID, uint16_t address, uint16_t bitsToRead, uint8_t* readArray, size_t timeout );


/// <summary>
/// Sends a request to read a variable number of discrete inputs.
/// </summary>
/// <param name="hndl">The message handle</param>
/// <param name="slaveID">Address of the slave device</param>
/// <param name="address">Address of the first discrete input to read on the device</param>
/// <param name="bitsToRead">Number of discrete input to read</param>
/// <param name="readArray">Pointer to an array to store read data in</param>
/// <param name="timeout">Time in milliseconds after which function will return an error if no response a has been received from the device</param>
/// <returns>true on success, or false on failure</returns>
bool ReadDiscreteInputs( modbus_t hndl, uint8_t slaveID, uint16_t address, uint16_t bitsToRead, uint8_t* readArray, size_t timeout );


/// <summary>
/// Sends a request to read a variable number of holding registers.
/// </summary>
/// <param name="hndl">The message handle</param>
/// <param name="slaveID">Address of the slave device</param>
/// <param name="address">Address of first holding register to read on the device</param>
/// <param name="registerToRead">Number of registers to read</param>
/// <param name="readArray">Pointer to an array to store read data in</param>
/// <param name="timeout">Time in milliseconds after which function will return an error if no response a has been received from the device</param>
/// <returns>true on success, or false on failure</returns>
bool ReadMultipleHoldingRegisters( modbus_t hndl, uint8_t slaveID, uint16_t address, uint16_t registersToRead, uint16_t* readArray, size_t timeout );


/// <summary>
/// Sends a request to read a variable number of input registers.
/// </summary>
/// <param name="hndl">The message handle</param>
/// <param name="slaveID">Address of the slave device</param>
/// <param name="address">Address of first input register to read on the device</param>
/// <param name="registersToRead">Number of registers to read</param>
/// <param name="readArray">Pointer to an array to store read data in</param>
/// <param name="timeout">Time in milliseconds after which function will return an error if no response a has been received from the device</param>
/// <returns>true on success, or false on failure</returns>
bool ReadInputRegisters( modbus_t hndl, uint8_t slaveID, uint16_t address, uint16_t registersToRead, uint16_t *readArray, size_t timeout );

/// PassiveRead is still in progress
/// <summary>
/// Listens for any incoming messages that fit the handle, and passes it on to the user.
/// </summary>
/// <param name="hndl">The message handle</param>
/// <param name="readArray">Pointer to an array to store read data in</param>
/// <param name="bytesToRead">The number of bytes that are expected</param>
/// <param name="timeout">Time in milliseconds after which function will return an error if no response a has been received from the device</param>
/// <returns>true on success, or false on failure</returns>
bool PassiveRead( modbus_t hndl, uint8_t* readArray, uint8_t bytesToRead, size_t timeout );

/// <summary>
/// Sends a request to read from a file stored on the slave device.
/// </summary>
/// <param name="hndl">The message handle</param>
/// <param name="slaveID">Address of the slave device</param>
/// <param name="messageArray">The array that the read subrequests will be stored in</param>
/// <param name="messageLength">The length of the array of subrequests</param>
/// <param name="readArray">Pointer to an array to store read data in</param>
/// <param name="timeout">Time in milliseconds after which function will return an error if no response a has been received from the device</param>
/// <returns>true on success, or false on failure</returns>
bool ReadFile(modbus_t hndl, uint8_t slaveID, uint8_t* messageArray, uint8_t messageLength, uint8_t* readArray, size_t timeout);

/// <summary>
/// Creates and appends (if there is already a subrequest) a new subrequest to be placed into the ReadFile request message.
/// </summary>
/// <param name="messageArray">The array to store the subrequests</param>
/// <param name="currentMessageIndex">The sum of the lengths of the subrequests (0 if its the first/only subrequest)</param>
/// <param name="fileNumber">The ID number of the file to read from</param>
/// <param name="recordNumber">The index on the file to start reading from</param>
/// <param name="recordLength">How many pairs of bytes to read</param>
/// <returns>The new length of the messageArray (to be used for messageLength in ReadFile and currentMessageIndex if you want to use ReadFileSubRequestBuilder to add multiple requests in a single message)</returns>
uint8_t ReadFileSubRequestBuilder(uint8_t* messageArray, uint8_t currentMessageIndex, uint16_t fileNumber, uint16_t recordNumber, uint8_t recordLength);


/*-------------------------WRITE FUNCIONS-------------------------*/


/// <summary>
/// Sends a request to write to a single coil.
/// </summary>
/// <param name="hndl">The message handle</param>
/// <param name="slaveID">Address of the slave device</param>
/// <param name="address">Address of the coil to write to on the device</param>
/// <param name="bit">Value to write</param>
/// <param name="readArray">Pointer to an array to store the error code response if present</param>
/// <param name="timeout">Time in milliseconds after which function will return an error if no response a has been received from the device</param>
/// <returns>true on success, or false on failure</returns>
bool WriteSingleCoil( modbus_t hndl, uint8_t slaveID, uint16_t address, bool bit, uint8_t* readArray, size_t timeout );


/// <summary>
/// Sends a request to write to a register.
/// </summary>
/// <param name="hndl">The message handle</param>
/// <param name="slaveID">Address of the slave device</param>
/// <param name="address">Address of the holding register to write to on the device</param>
/// <param name="register">Value to write</param>
/// <param name="readArray">Pointer to an array to store the error code response if present</param>
/// <param name="timeout">Time in milliseconds after which function will return an error if no response a has been received from the device</param>
/// <returns>true on success, or false on failure</returns>
bool WriteSingleHoldingRegister( modbus_t hndl, uint8_t slaveID, uint16_t address, uint16_t mbRegister, uint8_t* readArray, size_t timeout );


/// <summary>
/// Sends a request to write to a variable number of coils.
/// </summary>
/// <param name="hndl">The message handle</param>
/// <param name="slaveID">Address of the slave device</param>
/// <param name="address">Address of the first coil to write to on the device</param>
/// <param name="numToWrite">Number of bits to write</param>
/// <param name="bitArray">Pointer to the array of data to write from</param>
/// <param name="readArray">Pointer to an array to store the error code response if present</param>
/// <param name="timeout">Time in milliseconds after which function will return an error if no response a has been received from the device</param>
/// <returns>true on success, or false on failure</returns>
bool WriteMultipleCoils( modbus_t hndl, uint8_t slaveID, uint16_t address, uint16_t numToWrite, uint8_t* bitArray, uint8_t* readArray, size_t timeout );


/// <summary>
/// Sends a request to write to a variable number of holding registers.
/// </summary>
/// <param name="hndl">The message handle</param>
/// <param name="slaveID">Address of the slave device</param>
/// <param name="address">Address of the first register to write to</param>
/// <param name="numToWrite">Number of registers to write</param>
/// <param name="registerArray">Pointer to the array of data to write from</param>
/// <param name="readArray">Pointer to an array to store the error code response if present</param>
/// <param name="timeout">Time in milliseconds after which function will return an error if no response a has been received from the device</param>
/// <returns>true on success, or false on failure</returns>
bool WriteMultipleHoldingRegisters( modbus_t hndl, uint8_t slaveID, uint16_t address, uint16_t numToWrite, uint16_t *registerArray, uint8_t* readArray, size_t timeout );

/// <summary>
/// Sends a request to write to a file stored on the slave device.
/// </summary>
/// <param name="hndl">The message handle</param>
/// <param name="slaveID">Address of the slave device</param>
/// <param name="messageArray">The array that the write subrequests will be stored in</param>
/// <param name="messageLength">The length of the array of subrequests</param>
/// <param name="readArray">Pointer to an array to store the slave device's confirmation message</param>
/// <param name="timeout">Time in milliseconds after which function will return an error if no response a has been received from the device</param>
/// <returns>true on success, or false on failure</returns>
bool WriteFile(modbus_t hndl, uint8_t slaveID, uint8_t* messageArray, uint8_t messageLength, uint8_t* readArray, size_t timeout);

/// <summary>
/// Creates and appends (if there is already a subrequest) a new subrequest to be placed into the WriteFile request message.
/// </summary>
/// <param name="messageArray">The array to store the subrequests</param>
/// <param name="currentMessageIndex">The sum of the lengths of the subrequests (0 if its the first/only subrequest)</param>
/// <param name="fileNumber">The ID number of the file to write to</param>
/// <param name="recordNumber">The index on the file to start writing to</param>
/// <param name="recordLength">How many pairs of bytes to write</param>
/// <param name="record">The data to be written</param>
/// <returns>The new length of the messageArray (to be used for messageLength in WriteFile and currentMessageIndex if you want to use WriteFileSubRequestBuilder to add multiple requests in a single message)</returns>
uint8_t WriteFileSubRequestBuilder(uint8_t* messageArray, uint8_t currentMessageIndex, uint16_t fileNumber, uint16_t recordNumber, uint8_t recordLength, uint16_t* record);