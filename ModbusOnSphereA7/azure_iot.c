/* Copyright (c) Microsoft Corporation. All rights reserved.
   Licensed under the MIT License. */

   // This file contains code copied mostly from main.c of Azure Sphere sample code, 
   // AzureIoT.  This includes code to establish connection with Azure IoT Hub
   // and to send data to IoT Hub periodically.

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

// Azure IoT SDK
#include <iothub_client_core_common.h>
#include <iothub_device_client_ll.h>
#include <iothub_client_options.h>
#include <iothubtransportmqtt.h>
#include <iothub.h>
#include <azure_sphere_provisioning.h>
#include <applibs/networking.h>
#include <applibs/log.h>

#include "epoll_timerfd_utilities.h"
#include "azure_iot.h"
#include "parson.h"


// Definitions for twin update callbacks
#define MAX_TWIN_CALLBACKS  10  // Modify if more are needed

typedef struct {
    char* property;     // desired property that will trigger the callback
    twinCallback fPtr;  // function to call when desired property is found
    TwinUpdateContext context;// Values to be passed back to the callback function
} TWIN_CALLBACK;

static size_t numCallbacksSet = 0;
TWIN_CALLBACK twinCallbackArray[MAX_TWIN_CALLBACKS] = { 0 };

// Azure IoT poll periods
const int AzureIoTDefaultPollPeriodSeconds = AZURE_IOT_DEFAULT_POLL_PERIOD;
static const int AzureIoTMinReconnectPeriodSeconds = AZURE_IOT_MIN_RECONNECT_PERIOD;
static const int AzureIoTMaxReconnectPeriodSeconds = AZURE_IOT_MAX_RECONNECT_PERIOD;
static int azureIoTPollPeriodSeconds = AzureIoTDefaultPollPeriodSeconds;

static IOTHUB_DEVICE_CLIENT_LL_HANDLE iothubClientHandle = NULL;
static const int keepalivePeriodSeconds = AZURE_IOT_KEEP_ALIVE_PERIOD;
static bool iothubAuthenticated = false;

// ScopeId for the Azure IoT Central application and DPS set in app_manifest.json, CmdArgs
char scopeId[SCOPEID_LENGTH];

// Used by main to create a timed function call
int azureTimerFd = -1;

// Forward declarations
static void AzureIoT_TwinCallback(DEVICE_TWIN_UPDATE_STATE updateState, const unsigned char* payload,
    size_t payloadSize, void* userContextCallback);

/// <summary>
///     Converts AZURE_SPHERE_PROV_RETURN_VALUE to a string.
/// </summary>
static const char* getAzureSphereProvisioningResultString(
    AZURE_SPHERE_PROV_RETURN_VALUE provisioningResult)
{
    switch (provisioningResult.result) {
    case AZURE_SPHERE_PROV_RESULT_OK:
        return "AZURE_SPHERE_PROV_RESULT_OK";
    case AZURE_SPHERE_PROV_RESULT_INVALID_PARAM:
        return "AZURE_SPHERE_PROV_RESULT_INVALID_PARAM";
    case AZURE_SPHERE_PROV_RESULT_NETWORK_NOT_READY:
        return "AZURE_SPHERE_PROV_RESULT_NETWORK_NOT_READY";
    case AZURE_SPHERE_PROV_RESULT_DEVICEAUTH_NOT_READY:
        return "AZURE_SPHERE_PROV_RESULT_DEVICEAUTH_NOT_READY";
    case AZURE_SPHERE_PROV_RESULT_PROV_DEVICE_ERROR:
        return "AZURE_SPHERE_PROV_RESULT_PROV_DEVICE_ERROR";
    case AZURE_SPHERE_PROV_RESULT_GENERIC_ERROR:
        return "AZURE_SPHERE_PROV_RESULT_GENERIC_ERROR";
    default:
        return "UNKNOWN_RETURN_VALUE";
    }
}

/// <summary>
///     Converts the IoT Hub connection status reason to a string.
/// </summary>
static const char* GetReasonString(IOTHUB_CLIENT_CONNECTION_STATUS_REASON reason)
{
    static char* reasonString = "unknown reason";
    switch (reason) {
    case IOTHUB_CLIENT_CONNECTION_EXPIRED_SAS_TOKEN:
        reasonString = "IOTHUB_CLIENT_CONNECTION_EXPIRED_SAS_TOKEN";
        break;
    case IOTHUB_CLIENT_CONNECTION_DEVICE_DISABLED:
        reasonString = "IOTHUB_CLIENT_CONNECTION_DEVICE_DISABLED";
        break;
    case IOTHUB_CLIENT_CONNECTION_BAD_CREDENTIAL:
        reasonString = "IOTHUB_CLIENT_CONNECTION_BAD_CREDENTIAL";
        break;
    case IOTHUB_CLIENT_CONNECTION_RETRY_EXPIRED:
        reasonString = "IOTHUB_CLIENT_CONNECTION_RETRY_EXPIRED";
        break;
    case IOTHUB_CLIENT_CONNECTION_NO_NETWORK:
        reasonString = "IOTHUB_CLIENT_CONNECTION_NO_NETWORK";
        break;
    case IOTHUB_CLIENT_CONNECTION_COMMUNICATION_ERROR:
        reasonString = "IOTHUB_CLIENT_CONNECTION_COMMUNICATION_ERROR";
        break;
    case IOTHUB_CLIENT_CONNECTION_OK:
        reasonString = "IOTHUB_CLIENT_CONNECTION_OK";
        break;
    }
    return reasonString;
}

/// <summary>
///     Sets the IoT Hub authentication state for the app
///     The SAS Token expires which will set the authentication state
/// </summary>
static void AzureIoT_ConnectionStatusCallback(IOTHUB_CLIENT_CONNECTION_STATUS result,
    IOTHUB_CLIENT_CONNECTION_STATUS_REASON reason,
    void* userContextCallback)
{
    iothubAuthenticated = (result == IOTHUB_CLIENT_CONNECTION_AUTHENTICATED);
    Log_Debug("IoT Hub Authenticated: %s\n", GetReasonString(reason));
}

/// <summary>
///     Callback confirming message delivered to IoT Hub.
/// </summary>
/// <param name="result">Message delivery status</param>
/// <param name="context">User specified context</param>
static void AzureIoT_SendMessageCallback(IOTHUB_CLIENT_CONFIRMATION_RESULT result, void* context)
{
    Log_Debug("INFO: Message received by IoT Hub. Result is: %d\n", result);
}

/// <summary>
///     Callback function invoked when a message is received from IoT Hub.
/// </summary>
/// <param name="message">The handle of the received message</param>
/// <param name="context">The user context specified at IoTHubDeviceClient_LL_SetMessageCallback()
/// invocation time</param>
/// <returns>Return value to indicates the message procession status (i.e. accepted, rejected,
/// abandoned)</returns>
static IOTHUBMESSAGE_DISPOSITION_RESULT AzureIoT_ReceiveMessageCallback(IOTHUB_MESSAGE_HANDLE message,
    void* context)
{
    Log_Debug("INFO: Received message '%s' from IoT Hub\n");

    return IOTHUBMESSAGE_ACCEPTED;
}

/// <summary>
///     Sets up the Azure IoT Hub connection (creates the iothubClientHandle)
///     When the SAS Token for a device expires the connection needs to be recreated
///     which is why this is not simply a one time call.
/// </summary>
static void AzureIoT_SetupClient(void)
{
    if (iothubAuthenticated && (iothubClientHandle != NULL))
        return;

    if (iothubClientHandle != NULL)
        IoTHubDeviceClient_LL_Destroy(iothubClientHandle);

    AZURE_SPHERE_PROV_RETURN_VALUE provResult =
        IoTHubDeviceClient_LL_CreateWithAzureSphereDeviceAuthProvisioning(scopeId, 10000,
            &iothubClientHandle);
    Log_Debug("IoTHubDeviceClient_LL_CreateWithAzureSphereDeviceAuthProvisioning returned '%s'.\n",
        getAzureSphereProvisioningResultString(provResult));

    if (provResult.result != AZURE_SPHERE_PROV_RESULT_OK) {

        // If we fail to connect, reduce the polling frequency, starting at
        // AzureIoTMinReconnectPeriodSeconds and with a backoff up to
        // AzureIoTMaxReconnectPeriodSeconds
        if (azureIoTPollPeriodSeconds == AzureIoTDefaultPollPeriodSeconds) {
            azureIoTPollPeriodSeconds = AzureIoTMinReconnectPeriodSeconds;
        }
        else {
            azureIoTPollPeriodSeconds *= 2;
            if (azureIoTPollPeriodSeconds > AzureIoTMaxReconnectPeriodSeconds) {
                azureIoTPollPeriodSeconds = AzureIoTMaxReconnectPeriodSeconds;
            }
        }

        struct timespec azureTelemetryPeriod = { azureIoTPollPeriodSeconds, 0 };
        SetTimerFdToPeriod(azureTimerFd, &azureTelemetryPeriod);

        Log_Debug("ERROR: failure to create IoTHub Handle - will retry in %i seconds.\n",
            azureIoTPollPeriodSeconds);
        return;
    }

    // Successfully connected, so make sure the polling frequency is back to the default
    struct timespec azureTelemetryPeriod = { AzureIoTDefaultPollPeriodSeconds, 0 };
    SetTimerFdToPeriod(azureTimerFd, &azureTelemetryPeriod);

    iothubAuthenticated = true;

    if (IoTHubDeviceClient_LL_SetOption(iothubClientHandle, OPTION_KEEP_ALIVE,
        &keepalivePeriodSeconds) != IOTHUB_CLIENT_OK) {
        Log_Debug("ERROR: failure setting option \"%s\"\n", OPTION_KEEP_ALIVE);
        return;
    }

    // Set callbacks for Message and Device Twin features
    IoTHubDeviceClient_LL_SetMessageCallback(iothubClientHandle, AzureIoT_ReceiveMessageCallback, NULL);
    IoTHubDeviceClient_LL_SetDeviceTwinCallback(iothubClientHandle, AzureIoT_TwinCallback, NULL);

    // Set callbacks for connection status related events.
    IoTHubDeviceClient_LL_SetConnectionStatusCallback(iothubClientHandle, AzureIoT_ConnectionStatusCallback, NULL);
}

/// <summary>
///     Callback invoked when a Device Twin update is received from IoT Hub.
///     This loops through properties registered and run the associated callback.
/// </summary>
/// <param name="payload">contains the Device Twin JSON document (desired and reported)</param>
/// <param name="payloadSize">size of the Device Twin JSON document</param>
static void AzureIoT_TwinCallback(DEVICE_TWIN_UPDATE_STATE updateState, const unsigned char* payload,
    size_t payloadSize, void* userContextCallback)
{
    size_t nullTerminatedJsonSize = payloadSize + 1;
    char* nullTerminatedJsonString = (char*)malloc(nullTerminatedJsonSize);
    if (nullTerminatedJsonString == NULL) {
        Log_Debug("ERROR: Could not allocate buffer for twin update payload.\n");
        abort();
    }

    // Copy the provided buffer to a null terminated buffer.
    memcpy(nullTerminatedJsonString, payload, payloadSize);
    // Add the null terminator at the end.
    nullTerminatedJsonString[nullTerminatedJsonSize - 1] = 0;

    JSON_Value* rootProperties = NULL;
    rootProperties = json_parse_string(nullTerminatedJsonString);
    if (rootProperties == NULL) {
        Log_Debug("WARNING: Cannot parse the string as JSON content.\n");
        goto cleanup;
    }

    JSON_Object* rootObject = json_value_get_object(rootProperties);
    JSON_Object* desiredProperties = json_object_dotget_object(rootObject, "desired");
    if (desiredProperties == NULL) {
        desiredProperties = rootObject;
    }

    // Handle the Device Twin Desired Properties here.
    // If any of the requested properties exist, then the callback is run.
    for (size_t i = 0; i < numCallbacksSet; i++) {
        // Validate anyway, event though we don't really need to
        if (twinCallbackArray[i].property && twinCallbackArray[i].fPtr) {
            JSON_Value* obj = json_object_get_value(desiredProperties, twinCallbackArray[i].property);
            if (obj != NULL) {
                twinCallbackArray[i].fPtr(obj, twinCallbackArray[i].context);
            }
        }
    }


cleanup:
    // Release the allocated memory.
    json_value_free(rootProperties);
    free(nullTerminatedJsonString);
}

/// <summary>
///     Sends telemetry data to IoT Hub
/// </summary>
/// <param name="message">The message to send</param>
void AzureIoT_SendMessage(const char* message)
{
    Log_Debug("Sending IoT Hub Message: %s\n", message);

    IOTHUB_MESSAGE_HANDLE messageHandle = IoTHubMessage_CreateFromString(message);

    if (messageHandle == 0) {
        Log_Debug("WARNING: unable to create a new IoTHubMessage\n");
        return;
    }
    if (IoTHubDeviceClient_LL_SendEventAsync(iothubClientHandle, messageHandle, AzureIoT_SendMessageCallback,
        /*&callback_param*/ 0) != IOTHUB_CLIENT_OK) {
        Log_Debug("WARNING: failed to hand over the message to IoTHubClient\n");
    }
    else {
        Log_Debug("INFO: IoTHubClient accepted the message for delivery\n");
    }

    IoTHubMessage_Destroy(messageHandle);
}

/// <summary>
///     Generates a simulated Temperature and sends to IoT Hub.  
///		This is also a wrapper to allow Azure code to remain in one file
/// </summary>
void AzureIoT_EventHandler(void) {

    bool isNetworkReady = false;
    if (Networking_IsNetworkingReady(&isNetworkReady) != -1) {
        if (isNetworkReady && !iothubAuthenticated) {
            AzureIoT_SetupClient();
        }
    }
    else {
        Log_Debug("Failed to get Network state\n");
    }

    if (iothubAuthenticated) {
        IoTHubDeviceClient_LL_DoWork(iothubClientHandle);
    }
}

/// <summary>
///     Enqueues a report. The report is not sent immediately, but it is sent on the next invocation of
///     IoTHubDeviceClient_LL_DoWork().
///     The report should be a value JSON object with key value pairs
/// </summary>
/// <param name="properties">JSON string containing key value pairs</param>
void AzureIoT_TwinReportState(const char* properties)
{
    if (iothubClientHandle == NULL) {
        Log_Debug("ERROR: client not initialized\n");
    }
    else {

        if (IoTHubDeviceClient_LL_SendReportedState(
            iothubClientHandle, (const unsigned char*)properties,
            strlen(properties), NULL, 0) != IOTHUB_CLIENT_OK) {
            Log_Debug("ERROR: failed to set reported state for '%s'\n", properties);
        }
        else {
            Log_Debug("INFO: Reported state for '%s'\n", properties);
        }
    }
}

/// <summary>
/// Add a callback to the list to be searched when the Twin is updated.
/// Returns true if added, false if not enough space to store it.
/// Callbacks cannot be removed once set.
/// It is recommended that callbacks return as quickly as possible and do not call blocking code.
/// e.g. extract and store the data required and set a flag to allow the user code to operate on it later.
///
/// Note - the property string is copied, so takes up memory.
/// </summary>
/// <param name ="property>Desired Property change that will trigger the callback</param>
/// <param name ="fPtr>The callback to use</param>
bool AzureIoT_AddTwinUpdateCallback(const char* property, twinCallback fPtr, TwinUpdateContext context)
{
    if (numCallbacksSet >= MAX_TWIN_CALLBACKS) {
        return false;
    }
    twinCallbackArray[numCallbacksSet].fPtr = fPtr;
    twinCallbackArray[numCallbacksSet].property = strdup(property);
    twinCallbackArray[numCallbacksSet].context = context;
    numCallbacksSet++;

    return true;
}
