/* Copyright (c) Microsoft Corporation. All rights reserved.
   Licensed under the MIT License. */

   // This file contains macros and function definitions for azure_iot.c.

#pragma once

#include <stddef.h>
#include <stdbool.h>
#include "parson.h"

// Definitions for twin update callbacks
typedef union {
    int intVal;
    float floatVal;
    void* pointerVal;
} TwinUpdateContext;

typedef void (*twinCallback)(JSON_Value* jPtr, TwinUpdateContext t);

// Macros for settingup periodic Azure timer thread and establish connection with IoT Hub. 
#define AZURE_IOT_DEFAULT_POLL_PERIOD	5
#define AZURE_IOT_MIN_RECONNECT_PERIOD	60
#define AZURE_IOT_MAX_RECONNECT_PERIOD	10*60
#define AZURE_IOT_KEEP_ALIVE_PERIOD		20

extern int azureTimerFd;
extern const int AzureIoTDefaultPollPeriodSeconds;

// ScopeId for the Azure IoT Central application, set in app_manifest.json, CmdArgs
#define SCOPEID_LENGTH 20
extern char scopeId[SCOPEID_LENGTH];

// Primary handler to deal with Azure IoT Hub connectivity and activities
/// <summary>
///     Generates a simulated Temperature and sends to IoT Hub.  
///		This is also a wrapper to allow Azure code to remain in one file
/// </summary>
void AzureIoT_EventHandler(void);


/// <summary>
///     Sends telemetry data to IoT Hub
/// </summary>
/// <param name="message">The message to send</param>
void AzureIoT_SendMessage(const char* message);


/// <summary>
///     Enqueues a report. The report is not sent immediately, but it is sent on the next invocation of
///     IoTHubDeviceClient_LL_DoWork().
///     The report should be a value JSON object with key value pairs
/// </summary>
/// <param name="properties">JSON string containing key value pairs</param>
void AzureIoT_TwinReportState(const char* properties);


/// <summary>
/// Add a callback to the list to be searched when the Twin is updated.
/// Returns true if added, false if not enough space to store it.
/// Callbacks cannot be removed once set.
/// It is recommended that callbacks return as quickly as possible and do not call blocking code.
/// e.g. extract and store the data required and set a flag to allow the user code to operate on it later.
///
/// Note - the property string is copied, so takes up memory.
/// </summary>
/// <param name ="property">Desired Property change that will trigger the callback</param>
/// <param name ="fPtr">The callback to use</param>
bool AzureIoT_AddTwinUpdateCallback(const char* property, twinCallback fPtr, TwinUpdateContext context);
