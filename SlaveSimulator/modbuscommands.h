/**
 * @file    modbuscommands.h
 * @brief   Library for processing and responding to file read/write modbus requests.
 *
 * @author  Copyright (c) Bsquare EMEA 2020. https://www.bsquare.com/
 *          Licensed under the MIT License.
 */

#ifndef MODBUSCOMMANDS_H
#define MODBUSCOMMANDS_H

#include <stdint.h>
#include <stdbool.h>
int processIncomingMessage(uint8_t* messageIn, int messageSize, uint8_t* messageOut);
bool AddCRC(uint8_t* message, int inputLength, int maxInputLength);

#endif