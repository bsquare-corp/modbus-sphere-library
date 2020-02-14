/**
 * @file    adam4150.c
 * @brief   Sends messages to and receives messages from the ADAM-4150
 *          Data Aquisition Module and passes the data on to the IoThub.
 *
 * @author  Copyright (c) Bsquare EMEA 2020. https://www.bsquare.com/
 *          Licensed under the MIT License.
 */

#include "adam4150.h"
#include "azure_iot.h"
#include "parson.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <applibs/log.h>

#define DEFAULT_ADAM4150_TIMEOUT 500    // in ms

#define BASE_INPUT_ADDRESS  0
#define BASE_OUTPUT_ADDRESS 16
#define NUM_OUTPUTS 8
#define NUM_INPUTS 7

#define DIGITAL_STATE(value) (value ? "Open" : "Closed")
#define MAX_TWIN_UPDATE_SIZE 1024

typedef struct {
    modbus_t hndl;
    uint8_t slaveAddress;
} ADAM4150_CONFIG;

static ADAM4150_CONFIG config;

static bool digitalOutState[NUM_OUTPUTS];    // State of each digital outout
static bool digitalInState[NUM_INPUTS];     // State of digital inputs
static bool outputTwinUpdateRequired = true; // Always update on boot
static bool inputTwinUpdateRequired = true;  // Always update on boot

/// <summary>
/// Set the output to the requested value
/// </summary>
/// <param name="pin">Output to change</param>
/// <param name="state">State to set the output to</param>
static void SetOutput(uint8_t pin, bool state)
{
    if (!config.hndl) {
        Log_Debug("Adam4150 not yet configured\n");
        return;
    }
    uint8_t responseData[4];
    if (!WriteSingleCoil(config.hndl, config.slaveAddress, (uint16_t)(BASE_OUTPUT_ADDRESS + pin), state, responseData, DEFAULT_ADAM4150_TIMEOUT)) {
        Log_Debug("Unable to write coils: %s\n",  ModbusErrorToString(responseData[0]));
    }
    else {
        outputTwinUpdateRequired = true;
        digitalOutState[pin] = state;
    }
}

static void Adam4150_TwinUpdateCallback(JSON_Value* jPtr, TwinUpdateContext t)
{
    // context should contain the output to update as an integer
    // The string will either be a string or null (when removed from the twin's desired properties)
    const char *state = json_value_get_string(jPtr);
    if (state) {
        bool setState = true;
        if (strcmp(state, "Closed") == 0) {
            setState = false;
        }
        else if (strcmp(state, "Open") == 0) {
            setState = true;
        }
        else {
            Log_Debug("Invalid state for Output requested\n");
            return;
        }
        Log_Debug("Set Via twin: out%d to %s\n", t.intVal + 1, state);
        SetOutput((uint8_t)t.intVal, setState);
    }
}

/// <summary>
/// Set the modbus handle and slave address of the device.
/// This can be updated at any time.
/// </summary>
void Adam4150_SetConfig(modbus_t hndl, uint8_t slaveAddress)
{
    config.hndl = hndl;
    config.slaveAddress = slaveAddress;
}

/// <summary>
/// Toggle each of the digital outputs in turn and
/// read the input status
/// </summary>
void Adam4150_DigitalControl(void)
{
    static uint8_t counterRTU = 0;
    uint8_t data[4];
    // Write Coils
    counterRTU = (counterRTU + 1) & 7;
    bool newState = !digitalOutState[counterRTU];
    if (!config.hndl) {
        Log_Debug("Adam4150 not yet configured\n");
        return;
    }

    Log_Debug("Toggle coil %d %s\n", counterRTU, (newState)?"on":"off");
    SetOutput(counterRTU, newState);

    // Read digital inputs
    if (ReadDiscreteInputs(config.hndl, config.slaveAddress, BASE_INPUT_ADDRESS, NUM_INPUTS, data, DEFAULT_ADAM4150_TIMEOUT)) {
        // Put data into the digitalInState
        uint8_t values = data[0];
        for (size_t i = 0; i < NUM_INPUTS; i++) {
            if (digitalInState[i] != (values & 0x1)) {
                digitalInState[i] = values & 0x1;
                inputTwinUpdateRequired = true;
            }
            values >>= 1;
        }
    }
    else {
        Log_Debug("Unable to read Adam4150 inputs: %s\n", ModbusErrorToString(data[0]));
    }
}

/// <summary>
/// Send the values to the device twin
/// </summary>
void Adam4150_UpdateDeviceTwin(void)
{
    char twinUpdate[MAX_TWIN_UPDATE_SIZE];

    if (outputTwinUpdateRequired) {
        static char* outputTemplate = "{\"out1\":\"%s\",\"out2\":\"%s\",\"out3\":\"%s\",\"out4\":\"%s\",\"out5\":\"%s\",\"out6\":\"%s\",\"out7\":\"%s\",\"out8\":\"%s\"}";
        int thisPrint = snprintf(twinUpdate, MAX_TWIN_UPDATE_SIZE, outputTemplate,
            DIGITAL_STATE(digitalOutState[0]), DIGITAL_STATE(digitalOutState[1]),
            DIGITAL_STATE(digitalOutState[2]), DIGITAL_STATE(digitalOutState[3]),
            DIGITAL_STATE(digitalOutState[4]), DIGITAL_STATE(digitalOutState[5]),
            DIGITAL_STATE(digitalOutState[6]), DIGITAL_STATE(digitalOutState[7]));
        if (thisPrint >= MAX_TWIN_UPDATE_SIZE) {
            Log_Debug("Warning: Output twin update data too large\n");
        }
        else {
            AzureIoT_TwinReportState(twinUpdate);
            outputTwinUpdateRequired = false;
        }
    }

    // Digital values only sent if any changed since last time
    if (inputTwinUpdateRequired) {
        static char* inputTemplate = "{\"in1\":\"%s\",\"in2\":\"%s\",\"in3\":\"%s\",\"in4\":\"%s\",\"in5\":\"%s\",\"in6\":\"%s\",\"in7\":\"%s\"}";
        int thisPrint = snprintf(twinUpdate, MAX_TWIN_UPDATE_SIZE, inputTemplate,
            DIGITAL_STATE(digitalInState[0]), DIGITAL_STATE(digitalInState[1]),
            DIGITAL_STATE(digitalInState[2]), DIGITAL_STATE(digitalInState[3]),
            DIGITAL_STATE(digitalInState[4]), DIGITAL_STATE(digitalInState[5]),
            DIGITAL_STATE(digitalInState[6]));
        if (thisPrint >= MAX_TWIN_UPDATE_SIZE) {
            Log_Debug("Warning: Input twin update data too large\n");
        }
        else {
            AzureIoT_TwinReportState(twinUpdate);
            inputTwinUpdateRequired = false;
        }
    }
}

/// <summary>
/// Connect a callback for each or the Output coils
///</summary>
void Adam4150_SetTwinUpdateCallbacks(void)
{
    TwinUpdateContext t;
    for (int i = 0; i < 8; i++) {
        char twinValue[6];
        snprintf(twinValue, 6, "out%d", i+1);
        t.intVal = i;
        if (!AzureIoT_AddTwinUpdateCallback(twinValue, &Adam4150_TwinUpdateCallback, t)) {
            Log_Debug("Failed to set callback for twin update on '%s'", twinValue);
        }
    }
}
