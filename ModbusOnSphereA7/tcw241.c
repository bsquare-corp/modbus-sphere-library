/**
 * @file    tcw241.c
 * @brief   Sends messages to and receives messages from the TCW241
 *          ethernet controller and passes the data on to the IoThub.
 *
 * @author  Copyright (c) Bsquare EMEA 2020. https://www.bsquare.com/
 *          Licensed under the MIT License.
 */

#include "modbus.h"
#include "tcw241.h"
#include "azure_iot.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <applibs/log.h>

#define DEFAULT_TIMEOUT 1000 // in ms

// Size for the buffer used for sending Modbus data
#define MODBUS_MESSAGE_BUFFER_SIZE 384

// Mcros defined for sending Modbus data
#define RELAY_STATUS(value) (value ? "On" : "Off")
#define DIGITAL_INPUT_STATE(value) (value ? "Open" : "Close")

// Modbus data for TCW241
static bool TCW241RelayStatusTelemetryData[RELAY_COUNT];
static bool TCW241DigitalInputTelemetryData[DIGITAL_INPUT_COUNT];
static float TCW441AnalogInputTelemetryData[ANALOGUE_INPUT_COUNT];

/// <summary>
///     Change which coil is switched on. Read current status of coils and registers and store the results.
/// </summary>
/// <param name="hndl">Modbus handle to use</param>
void TCW241_ReadModbusData(modbus_t hndl)
{
    static uint16_t counterTCP = 0;
    uint8_t data[4];

    // turn off one coil, and turn on the next.
    if (!WriteSingleCoil(hndl, 0, (uint16_t)(WRITE_RELAY_ADDRESS_1 + counterTCP), 0, data, DEFAULT_TIMEOUT)) {
        Log_Debug("Unable to write coils: %02x, %s\n", data[0], ModbusErrorToString(data[0]));
    }
    
    counterTCP = (counterTCP + 1) & 3;
    if (!WriteSingleCoil(hndl, 0, (uint16_t)(WRITE_RELAY_ADDRESS_1 + counterTCP), 1, data, DEFAULT_TIMEOUT)) {
        Log_Debug("Unable to write coils: %02x, %s\n", data[0], ModbusErrorToString(data[0]));
    }

    // Read Coil statuses
    if (!ReadCoils(hndl, 0, READ_RELAY_ADDRESS_1, RELAY_COUNT, data, DEFAULT_TIMEOUT)) {
        Log_Debug("Unable to read coils: %02x, %s\n", data[0], ModbusErrorToString(data[0]));
    }
    else {
        uint8_t state = data[0];
        for (int i = 0; i < RELAY_COUNT; i++) {
            TCW241RelayStatusTelemetryData[i] = state & 1;
            Log_Debug("Relay status %d: %s\n", i + 1, TCW241RelayStatusTelemetryData[i] ? "On" : "Off");
            state = state >> 1;
        }
    }

    // Read the Digital Inputs
    if (!ReadDiscreteInputs(hndl, 0, READ_DIGITAL_INPUT_ADDRESS_1, DIGITAL_INPUT_COUNT, data, DEFAULT_TIMEOUT)) {
        Log_Debug("%s\n", ModbusErrorToString(data[0]));
        Log_Debug("Unable to read ReadDiscreteInputs: %02x, %s\n", data[0], ModbusErrorToString(data[0]));
    }
    else {
        uint8_t state = data[0];
        for (int i = 0; i < DIGITAL_INPUT_COUNT; i++) {
            TCW241DigitalInputTelemetryData[i] = state & 1;
            Log_Debug("Ditigal input %d: %s\n", i + 1, TCW241DigitalInputTelemetryData[i] ? "Open" : "Closed");
            state = state >> 1;
        }
    }

    // Read the Analogue inputs
    uint16_t doubleData[ANALOGUE_INPUT_COUNT * 2];
    if (!ReadMultipleHoldingRegisters(hndl, 0, ANALOGUE_INPUT_ADDRESS_1, ANALOGUE_INPUT_COUNT * 2, doubleData, DEFAULT_TIMEOUT)) {
        Log_Debug("Unable to read ReadMultipleHoldingRegisters: %04x, %s\n", doubleData[0], ModbusErrorToString(data[0]));
    }
    else {
        // This hardware reports values in two 16 bit words, making up a 32 bit float.
        // Endianness is a pain!!!
        for (int i = 0; i < ANALOGUE_INPUT_COUNT; i++) {
            float f;
            uint8_t* ptr = (uint8_t*)&f;
            memcpy(ptr + 2, &doubleData[i * 2], 2);
            memcpy(ptr, &doubleData[i * 2 + 1], 2);
            TCW441AnalogInputTelemetryData[i] = f;
            Log_Debug("Analogue register %d = %f\n", i + 1, TCW441AnalogInputTelemetryData[i]);
        }
    }
}

/// <summary>
///     Send TCW241 Modbus data to IoT Hub.
/// </summary>
void TCW241_SendModbusData(void)
{
    // Telemetry data format template specific for TCW241
    static const char telemetryFormatTemplateIoTHubTCW241[] =
        "{ \"%s\": \"%u\", \"%s\": \"%u\", \"%s\": \"%u\", \"%s\": \"%u\", "
        " \"%s\": \"%s\", \"%s\": \"%s\", \"%s\": \"%s\", \"%s\": \"%s\", "
        " \"%s\": \"%.4lf\", \"%s\": \"%.4lf\", \"%s\": \"%.4lf\", \"%s\": \"%.4lf\"}";

    // Allocate memory for a telemetry message to Azure
    char* messageBuffer = (char*)malloc(MODBUS_MESSAGE_BUFFER_SIZE);
    if (messageBuffer == NULL) {
        Log_Debug("ERROR: not enough memory to send telemetry");
    }

    int len = snprintf(messageBuffer, MODBUS_MESSAGE_BUFFER_SIZE, telemetryFormatTemplateIoTHubTCW241,
        "Relay status 1", TCW241RelayStatusTelemetryData[0],
        "Relay status 2", TCW241RelayStatusTelemetryData[1],
        "Relay status 3", TCW241RelayStatusTelemetryData[2],
        "Relay status 4", TCW241RelayStatusTelemetryData[3],
        "Digital Input 1", DIGITAL_INPUT_STATE(TCW241DigitalInputTelemetryData[0]),
        "Digital Input 2", DIGITAL_INPUT_STATE(TCW241DigitalInputTelemetryData[1]),
        "Digital Input 3", DIGITAL_INPUT_STATE(TCW241DigitalInputTelemetryData[2]),
        "Digital Input 4", DIGITAL_INPUT_STATE(TCW241DigitalInputTelemetryData[3]),
        "Analog Input 1", TCW441AnalogInputTelemetryData[0],
        "Analog Input 2", TCW441AnalogInputTelemetryData[1],
        "Analog Input 3", TCW441AnalogInputTelemetryData[2],
        "Analog Input 4", TCW441AnalogInputTelemetryData[3]);
    if (len < 0 || len >= MODBUS_MESSAGE_BUFFER_SIZE)
        return;

    AzureIoT_SendMessage(messageBuffer);
    free(messageBuffer);
}

