/**
 * @file    crc-util.h
 * @brief   A library to create, validate and append CRC values for modbus via serial.
 *
 * @author  Copyright (c) Bsquare EMEA 2019. https://www.bsquare.com/
 *          Licensed under the MIT License.
 */
#ifndef CRCUTIL_H
#define CRCUTIL_H

#include <stdint.h>
#include <stdbool.h>


/// <summary>
/// Validates the two CRC bytes of a modbus message. The generator for the CRC values to validate the message comes from https://www.modbustools.com/modbus_crc16.html
/// </summary>
/// <param name="message">The message with the CRC code to be validated</param>
/// <param name="inputLength">The length of the message</param>
/// <returns>true on success, or false on failure</returns>
bool ValidateCRC( uint8_t *message, int inputLength );


/// <summary>
/// Generates and appends the two CRC bytes of a modbus message to an array. The generator for the CRC values to append onto the message comes from https://www.modbustools.com/modbus_crc16.html
/// </summary>
/// <param name="message">The message to be given a CRC code</param>
/// <param name="inputLength">The length of the message</param>
/// <param name="maxInputLength">The size of the array storing the message</param>
/// <returns>true on success, or false on failure</returns>
bool AddCRC( uint8_t *message, int inputLength, int maxInputLength);
#endif /*CRCUTIL_H*/
