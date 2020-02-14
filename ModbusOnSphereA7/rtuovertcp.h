/**
 * @file    rtuovertcp.h
 * @brief   Sends messages to and receives messages from the simulator, 
 *          and passes the data on to the IoThub.
 *
 * @author  Copyright (c) Bsquare EMEA 2020. https://www.bsquare.com/
 *          Licensed under the MIT License.
 */

#pragma once

#include "modbus.h"

/*
* @brief Collect and store data on the simulator
*/
void RtuOverTcp_ReadModbusData(modbus_t hndl);

/*
* @brief Send simulator data to IoT Hub
*/
void RtuOverTcp_SendModbusData(void);

/* Definitions of modbus registers for the simulator*/

// Coils are single bits, records are two byte pairs
#define COIL_COUNT  4
#define COIL_ADDRESS_1    0
#define COIL_ADDRESS_2    1
#define COIL_ADDRESS_3    2
#define COIL_ADDRESS_4    3

#define RECORD_COUNT 4
#define RECORD_ADDRESS_1    0
#define RECORD_ADDRESS_2    1
#define RECORD_ADDRESS_3    2
#define RECORD_ADDRESS_4    3
