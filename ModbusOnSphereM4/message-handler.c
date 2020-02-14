/**
 * @file    buffer-management.c
 * @brief   Library to determine the length of incoming messages and hold them in buffers until complete messages have
 * been successfully received.
 *
 * @author  Copyright (c) Bsquare EMEA 2019. https://www.bsquare.com/
 *          Licensed under the MIT License.
 */

#include "message-handler.h"
#include "../modbusCommon.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define MESSAGE_HEADER_LENGTH 4

// read message and return it as a messageHandle
bool ReadA7Message(BufferHeader *inbound, BufferHeader *outbound, uint32_t sharedBufSize, messageHandle *message)
{
    int r = DequeueData(outbound, inbound, sharedBufSize, message->data, (uint32_t *)&message->length);
    if (r == -1 || message->length < PREFIX_LENGTH + MIN_HEADER_LENGTH)
    {
        return false;
    }
    return true;
}

uint8_t GetMessageProtocol(messageHandle *message)
{
    return message->data[PREFIX_LENGTH + PROTOCOL_OFFSET];
}

uint8_t GetMessageCommand(messageHandle *message)
{
    return message->data[PREFIX_LENGTH + COMMAND_OFFSET];
}

size_t GetMessageLength(messageHandle *message)
{
    return message->length - PREFIX_LENGTH - message->data[PREFIX_LENGTH + HEADER_LENGTH_OFFSET];
}

size_t GetMessageMaxLength(void)
{
    return MAX_MESSAGE_LENGTH - PREFIX_LENGTH - MESSAGE_HEADER_LENGTH;
}

uint8_t *GetMessagePrefixPtr(messageHandle *message)
{
    return &message->data[0];
}

uint8_t *GetMessageDataPtr(messageHandle *message)
{
    uint8_t hdrLength = message->data[PREFIX_LENGTH + HEADER_LENGTH_OFFSET];
    return &message->data[PREFIX_LENGTH + hdrLength];
}

void SetMessagePrefix(messageHandle *message, uint8_t *prefix)
{
    __builtin_memcpy(message->data, prefix, PREFIX_LENGTH);
}

void SetMessageProtocol(messageHandle *message, uint8_t protocol)
{
    message->data[PREFIX_LENGTH + PROTOCOL_OFFSET] = protocol;
}

void SetMessageCommand(messageHandle *message, uint8_t command)
{
    message->data[PREFIX_LENGTH + COMMAND_OFFSET] = command;
}

bool SetMessageData(messageHandle *message, uint8_t *body, size_t length)
{
    if (length > MAX_MESSAGE_LENGTH - MESSAGE_HEADER_LENGTH)
    {
        return false;
    }
    message->length = length + PREFIX_LENGTH + MESSAGE_HEADER_LENGTH;
    __builtin_memcpy(&message->data[PREFIX_LENGTH + MESSAGE_HEADER_LENGTH], body, length);
    return true;
}

void SetMessageLength(messageHandle *message, size_t length)
{
    message->length = length + PREFIX_LENGTH + MESSAGE_HEADER_LENGTH;
}

void SendA7Message(BufferHeader *inbound, BufferHeader *outbound, uint32_t sharedBufSize, messageHandle *message)
{
    EnqueueData(inbound, outbound, sharedBufSize, message->data, message->length);
}

void InitMessage(messageHandle* message) {
    message->length = PREFIX_LENGTH + MESSAGE_HEADER_LENGTH;
    message->data[PREFIX_LENGTH + HEADER_LENGTH_OFFSET] = MESSAGE_HEADER_LENGTH;
}