/**
 * @file    tcw241.h
 * @brief   Sends messages to and receives messages from the TCW241
 *          ethernet controller and passes the data on to the IoThub.
 *
 * @author  Copyright (c) Bsquare EMEA 2020. https://www.bsquare.com/
 *          Licensed under the MIT License.
 */

#pragma once

#include "modbus.h"

/*
* @brief Collect and store data on TCW241 registers
*/
void TCW241_ReadModbusData(modbus_t hndl);

/*
* @brief Send TCW241 data to IoT Hub
*/
void TCW241_SendModbusData(void);

/* Definitions of modbus registers for TCW241 Ethernet IO Module*/

// Relays (coils) are single bits
#define RELAY_COUNT	4
#define READ_RELAY_ADDRESS_1	100
#define READ_RELAY_ADDRESS_2	101
#define READ_RELAY_ADDRESS_3	102
#define READ_RELAY_ADDRESS_4	103
#define WRITE_RELAY_ADDRESS_1	READ_RELAY_ADDRESS_1
#define WRITE_RELAY_ADDRESS_2	READ_RELAY_ADDRESS_2
#define WRITE_RELAY_ADDRESS_3	READ_RELAY_ADDRESS_3
#define WRITE_RELAY_ADDRESS_4	READ_RELAY_ADDRESS_4

// Digital inputs are single bits
#define DIGITAL_INPUT_COUNT	4
#define READ_DIGITAL_INPUT_ADDRESS_1	100
#define READ_DIGITAL_INPUT_ADDRESS_2	101
#define READ_DIGITAL_INPUT_ADDRESS_3	102
#define READ_DIGITAL_INPUT_ADDRESS_4	103

// Analogue inputs are 32 bit floats, stored in two 16 bit registers
#define ANALOGUE_INPUT_COUNT 4
#define ANALOGUE_INPUT_ADDRESS_1 300
#define ANALOGUE_INPUT_ADDRESS_2 302
#define ANALOGUE_INPUT_ADDRESS_3 304
#define ANALOGUE_INPUT_ADDRESS_4 306

// All descriptions are 64 bytes stored in 32 16 bit registers.
#define RELAY_DESCRIPTION_SIZE	32
#define RELAY_1_DESCRIPTION_ADDRESS	1000
#define RELAY_2_DESCRIPTION_ADDRESS	1032
#define RELAY_3_DESCRIPTION_ADDRESS	1064
#define RELAY_4_DESCRIPTION_ADDRESS	1096

#define DIGITAL_INPUT_DESCRIPTION_SIZE	32t
#define DIGITAL_INPUT_1_DESCRIPTION_ADDRESS	3200
#define DIGITAL_INPUT_2_DESCRIPTION_ADDRESS	3232
#define DIGITAL_INPUT_3_DESCRIPTION_ADDRESS	3264
#define DIGITAL_INPUT_4_DESCRIPTION_ADDRESS	3296

#define ANALOG_INPUT_DESCRIPTION_SIZE	32
#define ANALOG_INPUT_1_DESCRIPTION_ADDRESS	7600
#define ANALOG_INPUT_2_DESCRIPTION_ADDRESS	7632
#define ANALOG_INPUT_3_DESCRIPTION_ADDRESS	7664
#define ANALOG_INPUT_4_DESCRIPTION_ADDRESS	7696

// Note - Offsets, multipliers and dimensions are not defined. See datasheet if desired.