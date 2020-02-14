/**
 * @file    buffer-management.h
 * @brief   Library to determine the length of incoming messages and hold them in buffers until complete messages have
 * been successfully received.
 *
 * @author  Copyright (c) Bsquare EMEA 2019. https://www.bsquare.com/
 *          Licensed under the MIT License.
 */
#ifndef MESSAGEHANDLER_H
#define MESSAGEHANDLER_H

#include "../modbusCommon.h"
#include "mt3620-intercore.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAX_MESSAGE_LENGTH 1024 // Length of payload including header
#define PREFIX_LENGTH 20 // There are 20 bytes at the start of a message from the A7. 0-15 - GUID, 16-19 = reserved

#define MIN_HEADER_LENGTH 4

typedef struct _messageHandle
{
    size_t length;
    uint8_t data[PREFIX_LENGTH + MAX_MESSAGE_LENGTH];
} messageHandle;

/// <summary>
/// Checks to see if a message has arrived from the A7, if so it stores it in message for future use.
/// </summary>
/// <param name="inbound">A buffer header containing the read and write positions of incoming data</param>
/// <param name="outbound">A buffer header containing the read and write positions of outgoing data</param>
/// <param name="sharedBufSize">The size of the shared buffer between the A7 and M4 that is used for intercore communications</param>
/// <param name="message">The handle for the message to be stored in</param>
/// <returns>true on success, or false on failure</returns>
bool ReadA7Message(BufferHeader *inbound, BufferHeader *outbound, uint32_t sharedBufSize, messageHandle *message);

/// <summary>
/// A function to retrieve the protocol byte from the message.
/// </summary>
/// <param name="message">The message to be read</param>
/// <returns>The protocol byte of the message</returns>
uint8_t GetMessageProtocol(messageHandle *message);

/// <summary>
/// A function to retrieve the command byte from the message.
/// </summary>
/// <param name="message">The message to be read</param>
/// <returns>The command byte of the message</returns>
uint8_t GetMessageCommand(messageHandle *message);

/// <summary>
/// A function to retrieve the legnth of the message.
/// </summary>
/// <param name="message">The message to be read</param>
/// <returns>The length of the message</returns>
size_t GetMessageLength(messageHandle *message);

/// <summary>
/// A function to retrieve the maximum message length of a message.
/// </summary>
/// <returns>The maximum length of a message</returns>
size_t GetMessageMaxLength(void);

/// <summary>
/// A function to retriveve the pointer to the beginning of the message.
/// </summary>
/// <param name="message">The message to be read</param>
/// <returns>The pointer to the beginning of the message</returns>
uint8_t *GetMessageDataPtr(messageHandle *message);

/// <summary>
/// A function to retrieve the pointer to the message prefix (for intercore communications).
/// </summary>
/// <param name="message">The message to be read</param>
/// <returns>The pointer to the beginning of the prefix</returns>
uint8_t *GetMessagePrefixPtr(messageHandle *message);

/// <summary>
/// Set the protocol byte for an outbound message.
/// </summary>
/// <param name="message">The message to be written to</param>
/// <param name="protocol">The value to set the protocol byte to</param>
void SetMessageProtocol(messageHandle *message, uint8_t protocol);

/// <summary>
/// Set the command byte for an outbound message.
/// </summary>
/// <param name="message">The message to be written to</param>
/// <param name="command">The value to set the command byte to</param>
void SetMessageCommand(messageHandle *message, uint8_t command);

/// <summary>
/// Set the message prefix for an outgoing message (for intercore communications).
/// </summary>
/// <param name="message">The message to be written to</param>
/// <param name="prefix">A pointer to the beginning of the prefix</param>
void SetMessagePrefix(messageHandle *message, uint8_t *prefix);

/// <summary>
/// Set the message content for an outgoing message.
/// </summary>
/// <param name="message">The message to be written to</param>
/// <param name="body"> A pointer to the beginning of the data to be set</param>
/// <param name="length"> The length of the data to be set</param>
/// <returns>true on success, or false on failure</returns>
bool SetMessageData(messageHandle *message, uint8_t *body, size_t length);

/// <summary>
/// Set the message length for an outgoing message.
/// </summary>
/// <param name="message">The message to be written to</param>
/// <param name="length">The length of the message</param>
void SetMessageLength(messageHandle *message, size_t length);

/// <summary>
/// A function to send the data stored in the messagehandle to the A7.
/// </summary>
/// <param name="inbound">A buffer header containing the read and write positions of the receive buffer</param>
/// <param name="outbound">A buffer header containing the read and write positions of the sending buffer</param>
/// <param name="sharedBufSize">The size of the shared buffer between the A7 and M4 that is used for intercore communications</param>
/// <param name="message">The message to be sent</param>
void SendA7Message(BufferHeader *inbound, BufferHeader *outbound, uint32_t sharedBufSize, messageHandle *message);

/// <summary>
/// Initialize a messagehandle.
/// </summary>
/// <param name="message">The message to be initialized</param>
void InitMessage(messageHandle* message);

#endif /* MESSAGEHANDLER_H */
