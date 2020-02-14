/**
 * @file    adam4150.c
 * @brief   Sends messages to and receives messages from the ADAM-4150
 *          Data Aquisition Module and passes the data on to the IoThub.
 *
 * @author  Copyright (c) Bsquare EMEA 2020. https://www.bsquare.com/
 *          Licensed under the MIT License.
 */

#pragma once

#include "modbus.h"

/// <summary>
/// Set the modbus handle and slave address of the device.
/// This can be updated at any time.
/// </summary>
/// <param name="hndl">The message handle</param>
/// <param name="slaveAddress">The address to write to</param>
void Adam4150_SetConfig(modbus_t hndl, uint8_t slaveAddress);

/// <summary>
/// Toggle each of the digital outputs in turn and
/// read the input status
/// </summary>
void Adam4150_DigitalControl(void);

/// <summary>
/// Send the values to the device twin
/// </summary>
void Adam4150_UpdateDeviceTwin(void);

/// <summary>
/// Connect a callback for each or the Output coils
///</summary>
void Adam4150_SetTwinUpdateCallbacks(void);
