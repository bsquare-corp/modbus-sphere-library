/**
 * @file    rtuovertcp.c
 * @brief   Sends messages to and receives messages from the simulator,
 *          and passes the data on to the IoThub.
 *
 * @author  Copyright (c) Bsquare EMEA 2020. https://www.bsquare.com/
 *          Licensed under the MIT License.
 */

#include "modbus.h"
#include "rtuovertcp.h"
#include "azure_iot.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <applibs/log.h>

#define DEFAULT_TIMEOUT 1000 // in ms

 //this decides whether the functions read and write records from files or coils on the simulator.
#define CHANGE_FILES 1 

 // Size for the buffer used for sending Modbus data
#define MODBUS_MESSAGE_BUFFER_SIZE 384

// Modbus data for simulators
static bool CoilStatusTelemetryData[COIL_COUNT];
static uint16_t FileRecordsTelemetryData[RECORD_COUNT];

/// <summary>
///     Change which coil is switched on. Read current status of coils and registers and store the results.
/// </summary>
/// <param name="hndl">Modbus handle to use</param>
void RtuOverTcp_ReadModbusData(modbus_t hndl)
{
    static uint16_t counter = 0;
    uint8_t dataWrite[RECORD_COUNT * 2];
    uint8_t dataRead[RECORD_COUNT * 2];
    uint16_t records[RECORD_COUNT];
    uint8_t messageArray[MODBUS_MESSAGE_BUFFER_SIZE];

    if (CHANGE_FILES)
    {
        int writeMessageLength;
        int readMessageLength;
        for (int i = 0; i < RECORD_COUNT; i++)
        {
            counter = (counter + 1) & 15;
            records[i] = counter;
        }
        writeMessageLength = WriteFileSubRequestBuilder(messageArray, 0, 4, 0, RECORD_COUNT, records);
        if (!WriteFile(hndl, 1, messageArray, writeMessageLength, dataWrite, 5000))
        {
            Log_Debug("Unable to write to file: %d, %s\n", messageArray[2], ModbusErrorToString(dataWrite[0]));
        }
        readMessageLength = ReadFileSubRequestBuilder(messageArray, 0, 4, 0, RECORD_COUNT);
        if (!ReadFile(hndl, 1, messageArray, readMessageLength, dataRead, DEFAULT_TIMEOUT))
        {
            Log_Debug("Unable to read from file: %d, %s\n", messageArray[4], ModbusErrorToString(dataRead[0]));
        }
        else
        {
            for (int i = 0; i < RECORD_COUNT; i++)
            {
                FileRecordsTelemetryData[i] = (dataRead[(2 * i) + 2] << 8) | (dataRead[(2 * i) + 3]);
            }
        }

    }
    else
    {
        // turn off one coil, and turn on the next.
        if (!WriteSingleCoil(hndl, 0, (uint16_t)(COIL_ADDRESS_1 + counter), 0, dataWrite, DEFAULT_TIMEOUT))
        {
            Log_Debug("Unable to write coils: %02x, %s\n", dataWrite[0], ModbusErrorToString(dataWrite[0]));
        }

        counter = (counter + 1) & 3;
        if (!WriteSingleCoil(hndl, 0, (uint16_t)(COIL_ADDRESS_1 + counter), 1, dataWrite, DEFAULT_TIMEOUT))
        {
            Log_Debug("Unable to write coils: %02x, %s\n", dataWrite[0], ModbusErrorToString(dataWrite[0]));
        }
        // Read Coil statuses
        if (!ReadCoils(hndl, 0, COIL_ADDRESS_1, COIL_COUNT, dataRead, DEFAULT_TIMEOUT))
        {
            Log_Debug("Unable to read coils: %02x, %s\n", dataRead[0], ModbusErrorToString(dataRead[0]));
        }
        else
        {
            uint8_t state = dataRead[0];
            for (int i = 0; i < COIL_COUNT; i++)
            {
                CoilStatusTelemetryData[i] = state & 1;
                Log_Debug("Relay status %d: %s\n", i + 1, CoilStatusTelemetryData[i] ? "On" : "Off");
                state = state >> 1;
            }
        }
    }
}

/// <summary>
///     Send Modbus data to IoT Hub.
/// </summary>
void RtuOverTcp_SendModbusData(void)
{
    // Telemetry data format template for both simulators
    static const char telemetryFormatTemplateIoTHub[] =
        "{ \"%s\": \"%u\", \"%s\": \"%u\", \"%s\": \"%u\", \"%s\": \"%u\"}";

    // Allocate memory for a telemetry message to Azure
    char* messageBuffer = malloc(MODBUS_MESSAGE_BUFFER_SIZE);
    if (messageBuffer == NULL) {
        Log_Debug("ERROR: not enough memory to send telemetry");
    }
    int len;
    if (CHANGE_FILES)
    {
        len = snprintf(messageBuffer, MODBUS_MESSAGE_BUFFER_SIZE, telemetryFormatTemplateIoTHub,
            "File_Record_1", FileRecordsTelemetryData[RECORD_ADDRESS_1],
            "File_Record_2", FileRecordsTelemetryData[RECORD_ADDRESS_2],
            "File_Record_3", FileRecordsTelemetryData[RECORD_ADDRESS_3],
            "File_Record_4", FileRecordsTelemetryData[RECORD_ADDRESS_4]);
    }
    else
    {
        len = snprintf(messageBuffer, MODBUS_MESSAGE_BUFFER_SIZE, telemetryFormatTemplateIoTHub,
            "Coil_Status_1", CoilStatusTelemetryData[COIL_ADDRESS_1],
            "Coil_Status_2", CoilStatusTelemetryData[COIL_ADDRESS_2],
            "Coil_Status_3", CoilStatusTelemetryData[COIL_ADDRESS_3],
            "Coil_Status_4", CoilStatusTelemetryData[COIL_ADDRESS_4]);
    }
    if (len < 0 || len >= MODBUS_MESSAGE_BUFFER_SIZE)
    {
        free(messageBuffer);
        return;
    }

    AzureIoT_SendMessage(messageBuffer);
    free(messageBuffer);
}

