/* Copyright (c) Microsoft Corporation. All rights reserved.
   Licensed under the MIT License. */

// This sample C application for Azure Sphere sends simulated temperature data and 
// Modbus device data to the Azure IoT Hub.  This was ported from Azure Sphere 
// sample code, AzureIoT.  It leverages Modbus library implemented by Bsquare.

#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <getopt.h>

#include <sys/time.h>
#include <sys/socket.h>

#include <applibs/log.h>
#include <applibs/application.h>

#include "epoll_timerfd_utilities.h"
#include "modbus.h"
#include "../modbusCommon.h"
#include "tcw241.h"
#include "adam4150.h"
#include "rtuovertcp.h"

#include "azure_iot.h"

#define DEFAULT_ADAM4150_ID 5   // Slave ID of the device on the serial connection
#define DEVICE_LIMIT 5          // The number of devices that can be connected to at any one time

typedef enum
{
    tcp,
    rtuOverTcp,
    rtu,
    unconnected
} modbusTransportType_t;

typedef struct _deviceConnection
{
    modbusTransportType_t connectionType;
    char* address;
    modbus_t modbushndl;
} deviceConnection;

static int epollFd = -1;
static int timerFd = -1;
static int argNum;
deviceConnection argConnections[DEVICE_LIMIT];
static volatile sig_atomic_t terminationRequired = false;

static void TerminationHandler(int signalNumber);
static void TimerEventHandler(EventData *eventData);
static void AzureTimerEventHandler(EventData* eventData);
static int InitHandlers(void);
static void CloseHandlers(void);

/// <summary>
///     Signal handler for termination requests. This handler must be async-signal-safe.
/// </summary>
/// <param name="signalNumber">Reason for termination(currently unused)</param>
static void TerminationHandler(int signalNumber)
{
    // Don't use Log_Debug here, as it is not guaranteed to be async-signal-safe.
    terminationRequired = true;
}

/// <summary>
///     Handle send timer event by writing data to the real-time capable application.
/// </summary>
/// <param name="eventData">Pointer to the EventData Class</param>
static void TimerEventHandler(EventData *eventData)
{
    if (ConsumeTimerFdEvent(timerFd) != 0) {
        terminationRequired = true;
        return;
    }

    for (int i = 0; i < argNum; i++)
    {
        if ((argConnections[i].connectionType == tcp)&&(argConnections[i].modbushndl)) 
        {
            TCW241_ReadModbusData(argConnections[i].modbushndl);
            TCW241_SendModbusData();
        }
        else if ((argConnections[i].connectionType == rtu)&&(argConnections[i].modbushndl))
        {
            Adam4150_DigitalControl();
            Adam4150_UpdateDeviceTwin();
        }
        else if ((argConnections[i].connectionType == rtuOverTcp)&&(argConnections[i].modbushndl))
        {
            RtuOverTcp_ReadModbusData(argConnections[i].modbushndl);
            RtuOverTcp_SendModbusData();
        }
    }
}

/// <summary>
///		Handle Azure timer event by calling into Azure IoT wrapper.
/// </summary>
/// <param name="eventData">Pointer to the EventData Class that contains context data for the event handlers.</param>
static void AzureTimerEventHandler(EventData* eventData)
{
	if (ConsumeTimerFdEvent(azureTimerFd) != 0) {
		terminationRequired = true;
		return;
	}

	// Call into the IoT event handler which allows IoT Azure Hub related code to remain
	// in azure_iot.c.
	AzureIoT_EventHandler();
}

// event handler data structures. Only the event handler field needs to be populated.
static EventData timerEventData = { .eventHandler = &TimerEventHandler };
static EventData azureEventData = { .eventHandler = &AzureTimerEventHandler };

/// <summary>
///     Set up SIGTERM termination handler and event handlers for send timer
///     and to receive data from real-time capable application.
/// </summary>
/// <returns>0 on success, or -1 on failure</returns>
static int InitHandlers(void)
{
  
    struct sigaction action;
    memset(&action, 0, sizeof(struct sigaction));
    action.sa_handler = TerminationHandler;
    sigaction(SIGTERM, &action, NULL);

    epollFd = CreateEpollFd();
    if (epollFd < 0) {
        return -1;
    }
    bool connectionMade = false;
    for (int i = 0; i < argNum; i++)
    {
        if (argConnections[i].connectionType == tcp) {
            argConnections[i].modbushndl = ModbusConnectTcp(argConnections[i].address, 502);
            if(argConnections[i].modbushndl) {
                connectionMade = true;
                Log_Debug("tcp connection made\n");
            }
        }
        else if (argConnections[i].connectionType == rtuOverTcp)
        {
            argConnections[i].modbushndl = ModbusConnectRtuOverTcp(argConnections[i].address, 8000);
            if(argConnections[i].modbushndl) {
                connectionMade = true;
                Log_Debug("rtu over tcp connection made\n");
            }
        }
        else if (argConnections[i].connectionType == rtu)
        {
            serialSetup rtuSetup;
            rtuSetup.baudRate = BAUD_SET_9600;
            rtuSetup.duplexMode = HALF_DUPLEX_MODE;
            rtuSetup.parityMode = PARITY_ODD;
            rtuSetup.parityState = PARITY_OFF;
            rtuSetup.stopBits = 1;
            rtuSetup.wordLength = 8;
            argConnections[i].modbushndl = ModbusConnectRtu(rtuSetup, 400);
            if(argConnections[i].modbushndl) {
                connectionMade = true;
                Log_Debug("rtu connection made\n");
                Adam4150_SetConfig(argConnections[i].modbushndl, DEFAULT_ADAM4150_ID);
                Adam4150_SetTwinUpdateCallbacks();
            }
        }
    }
    if (connectionMade){
            // Register 10 second timer to read data from the Modbus device
        static const struct timespec sendPeriod = { .tv_sec = 10, .tv_nsec = 0 };
        timerFd = CreateTimerFdAndAddToEpoll(epollFd, &sendPeriod, &timerEventData, EPOLLIN);
        if (timerFd < 0) {
            return -1;
        }
        RegisterEventHandlerToEpoll(epollFd, timerFd, &timerEventData, EPOLLIN);
        // Register timer to periodically handle Azure IoT Hub events.
        struct timespec azureTelemetryPeriod = { AzureIoTDefaultPollPeriodSeconds, 0 };
        azureTimerFd =
        CreateTimerFdAndAddToEpoll(epollFd, &azureTelemetryPeriod, &azureEventData, EPOLLIN);
        if (azureTimerFd < 0) {
            return -1;
        }
        RegisterEventHandlerToEpoll(epollFd, azureTimerFd, &azureEventData, EPOLLIN);
    }
    else {
        Log_Debug("Failed to connect to any device\n");
        return -1;
    }



    return 0;
}

/// <summary>
///     Clean up the resources previously allocated.
/// </summary>
static void CloseHandlers(void)
{
    Log_Debug("Closing file descriptors.\n");
    for (int i = 0; i < argNum; i++)
    {
        ModbusClose(argConnections[i].modbushndl);
    }
    CloseFdAndPrintError(timerFd, "Timer");
    CloseFdAndPrintError(epollFd, "Epoll");
}

int main(int argc, char* argv[])
{
    Log_Debug("High-level Modbus application.\n");
    char opt=0;
    int i=0;
    for(int j = 0; j<argc; j++)
    {
        if (argv[j][0] == '-') {
            opt = argv[j][1];
            switch (opt) {
            case 't':
                j++;
                if ((j <= argc) && (argv[j][0] != '-')) {
                    argConnections[i].connectionType = tcp;
                    argConnections[i].address = argv[j];
                    i++;
                }
                break;
            case 'o':
                j++;
                if ((j <= argc) && (argv[j][0] != '-')) {
                    argConnections[i].connectionType = rtuOverTcp;
                    argConnections[i].address = argv[j];
                    i++;
                }
                break;
            case 'r':
                argConnections[i].connectionType = rtu;
                i++;
                break;
            default:
                Log_Debug("Not a valid argument.\n"
                    "Valid arguments:\n"
                    "For a TCP connection: -t [IP address]\n"
                    "For an RTU over TCP connection: -o [IP address]\n"
                    "For an RTU connection: -r\n");
                break;
            }
        }
    }
    argNum = i;

	if ((argc > 1)&&(argv[1][0] != '-')) {
		Log_Debug("Setting Azure Scope ID %s\n", argv[1]);
		strncpy(scopeId, argv[1], SCOPEID_LENGTH);
	}
	else {
        Log_Debug("ScopeId needs to be the first argument set in the app_manifest 'CmdArgs'\n");
		return -1;
	}

    Log_Debug("Uses Modbus TCP to communicate with TCW241.\n");
    Log_Debug("Uses Modbus RTU to communicate with ADAM4150.\n");

    if (!ModbusInit()) {
        Log_Debug("Unable to Initialise Modbus\n");
        return 0;
    }

    if (InitHandlers() != 0) {
        terminationRequired = true;
    }

	// Main loop
    while (!terminationRequired) {
        if (WaitForEventAndCallHandler(epollFd) != 0) {
            terminationRequired = true;
        }
    }

    CloseHandlers();
    ModbusExit();
    Log_Debug("Application exiting.\n");
    return 0;
}
