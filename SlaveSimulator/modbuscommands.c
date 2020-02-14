/**
 * @file    modbuscommands.c
 * @brief   Library for processing and responding to file read/write modbus requests.
 *
 * @author  Copyright (c) Bsquare EMEA 2020. https://www.bsquare.com/
 *          Licensed under the MIT License.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h> 
#include "modbuscommands.h"

#define HEADER_LENGTH 3
#define SUBREQUEST_LENGTH 7
#define REFERENCE_TYPE 6
#define FILE_NO_INDEX_UPPER 1
#define FILE_NO_INDEX_LOWER 2
#define RECORD_NO_INDEX_UPPER 3
#define RECORD_NO_INDEX_LOWER 4
#define RECORD_LENGTH_INDEX 6
#define NO_ERROR 0
#define ILLEGAL_FUNCTION 1
#define ILLEGAL_DATA_ADDRESS 2
#define ILLEGAL_DATA_VALUE 3

uint8_t fileStore[6][20000];

static int requestRead(uint8_t* messageIn, int messageSize, uint8_t* messageOut);
static int requestWrite(uint8_t* messageIn, int messageSize, uint8_t* messageOut);
static int fileRead(uint8_t* messageOut, int fileNo, int recordNo, int recordsToRead);
static int fileWrite(uint8_t* messageIn, uint8_t* messageOut, int fileNo, int recordNo, int recordsToWrite);
static uint16_t GetCRC(uint8_t* message, int inputLength);

//receive message from master and act accordingly
int processIncomingMessage(uint8_t* messageIn, int messageSize, uint8_t* messageOut)
{
    //add slave address
    messageOut[0] = messageIn[0];
    //add function code
    messageOut[1] = messageIn[1];
    if (messageIn[1] == 0x14)
    {
        return requestRead(messageIn, messageSize, messageOut);
    }
    else if (messageIn[1] == 0x15)
    {
        return requestWrite(messageIn, messageSize, messageOut);
    }
    else
    {
        return ILLEGAL_FUNCTION;
    }
}

static int requestRead(uint8_t* messageIn, int messageSize, uint8_t* messageOut)
{
    int ret;
    int totalLength = 0;
    int requestLength = messageIn[2];
    int recordsToRead;
    int fileNo;
    int recordNo;
    uint8_t* inPtr = &messageIn[HEADER_LENGTH];
    uint8_t* outPtr = &messageOut[HEADER_LENGTH];
    //validating the byte count according to modbus protocol specifications
    if (0x07 <= messageIn[2] && messageIn[2] <= 0xF5)
    {
        //loop until all requests are complete
        for (int i = 0; i < (messageIn[2] / 7); i++)
        {
            //check that the refence type is 6
            if (inPtr[0] == REFERENCE_TYPE)
            {
                //recordsToRead is how many pairs of bytes will need to be read from the file
                recordsToRead = inPtr[RECORD_LENGTH_INDEX];
                //use the file number to open the files
                fileNo = inPtr[FILE_NO_INDEX_LOWER] | (inPtr[FILE_NO_INDEX_UPPER] << 8);
                //read the record number from the file for the file request length
                recordNo = inPtr[RECORD_NO_INDEX_LOWER] | (inPtr[RECORD_NO_INDEX_UPPER] << 8);
                inPtr += SUBREQUEST_LENGTH;
                if (recordNo + recordsToRead < 10000)
                {
                    //add the file request length to the total response length
                    outPtr[0] = recordsToRead * 2;
                    outPtr[1] = 6;
                    printf("Reading %d records from the file %d. Starting from record %d\n", recordsToRead, fileNo, recordNo);
                    ret = fileRead(&outPtr[2], fileNo, recordNo, recordsToRead);
                    outPtr += (recordsToRead * 2) + 2;
                    totalLength += (recordsToRead * 2) + 2;
                }
                else
                {
                    printf("read request out of bounds\n");
                    return ILLEGAL_DATA_ADDRESS;
                }

            }
            else
            {
                printf("reference type is incorrect\n");
                return ILLEGAL_DATA_VALUE;
            }
        }
        messageOut[2] = totalLength;
        return ret;
    }
    else
    {
        return ILLEGAL_DATA_VALUE;
    }

}

static int requestWrite(uint8_t* messageIn, int messageSize, uint8_t* messageOut)
{
    int ret = 0;
    int dataRead = 0;
    int recordsToWrite;
    int fileNo;
    int recordNo;
    uint8_t* inPtr = &messageIn[HEADER_LENGTH];
    uint8_t* outPtr = &messageOut[HEADER_LENGTH];
    while (dataRead < messageIn[2])
    {
        //check that the refence type is 6
        if (inPtr[0] == REFERENCE_TYPE)
        {
            outPtr[0] = REFERENCE_TYPE;

            //use the file number to open the files
            fileNo = inPtr[FILE_NO_INDEX_LOWER] | (inPtr[FILE_NO_INDEX_UPPER] << 8);
            outPtr[FILE_NO_INDEX_UPPER] = inPtr[FILE_NO_INDEX_UPPER];
            outPtr[FILE_NO_INDEX_LOWER] = inPtr[FILE_NO_INDEX_LOWER];

            //write to the record number from the message for the file request length
            recordNo = inPtr[RECORD_NO_INDEX_LOWER] | (inPtr[RECORD_NO_INDEX_UPPER] << 8);
            outPtr[RECORD_NO_INDEX_UPPER] = inPtr[RECORD_NO_INDEX_UPPER];
            outPtr[RECORD_NO_INDEX_LOWER] = inPtr[RECORD_NO_INDEX_LOWER];

            //add the file request length to the total response length
            outPtr[5] = 0;
            recordsToWrite = inPtr[RECORD_LENGTH_INDEX];

            outPtr[RECORD_LENGTH_INDEX] = inPtr[RECORD_LENGTH_INDEX];
            if (recordNo + recordsToWrite < 10000)
            {
                printf("Writing %d records to the file %d. Starting from record %d\n", recordsToWrite, fileNo, recordNo);
                ret = fileWrite(&inPtr[7], &outPtr[7], fileNo, recordNo, recordsToWrite);
                if (ret != NO_ERROR)
                {
                    return ret;
                }
                //loop until all requests have been acted upon
                inPtr += (recordsToWrite * 2) + 7;
                outPtr += (recordsToWrite * 2) + 7;
                dataRead += (recordsToWrite * 2) + 7;
            }
            else
            {
                printf("write request out of bounds\n");
                return ILLEGAL_DATA_ADDRESS;
            }

        }
        else
        {
            printf("reference type is incorrect\n");
            return ILLEGAL_DATA_VALUE;
        }
    }
    messageOut[2] = dataRead;
    return ret;
}


static int fileRead(uint8_t* messageOut, int fileNo, int recordNo, int recordsToRead)
{
    printf("reading from file %d\n", fileNo);
    if (0 < fileNo && fileNo <= 6)
    {
        memcpy(messageOut, &fileStore[fileNo][recordNo * 2], recordsToRead * 2);
    }
    else
    {
        printf("Error: file does not exist\n");
        return ILLEGAL_DATA_ADDRESS;
    }
    return NO_ERROR;
}

static int fileWrite(uint8_t* messageIn, uint8_t* messageOut, int fileNo, int recordNo, int recordsToWrite)
{
    printf("writing to file %d\n", fileNo);
    if (0 < fileNo && fileNo <= 6)
    {
        memcpy(&fileStore[fileNo][recordNo * 2], messageIn, recordsToWrite * 2);
    }
    else
    {
        printf("Error: file does not exist\n");
        return ILLEGAL_DATA_ADDRESS;
    }
    memcpy(messageOut, messageIn, recordsToWrite * 2);
    return NO_ERROR;
}

static uint16_t GetCRC(uint8_t* message, int inputLength) {
    static const uint16_t CRCTable[256] = {
    0X0000, 0XC0C1, 0XC181, 0X0140, 0XC301, 0X03C0, 0X0280, 0XC241,
    0XC601, 0X06C0, 0X0780, 0XC741, 0X0500, 0XC5C1, 0XC481, 0X0440,
    0XCC01, 0X0CC0, 0X0D80, 0XCD41, 0X0F00, 0XCFC1, 0XCE81, 0X0E40,
    0X0A00, 0XCAC1, 0XCB81, 0X0B40, 0XC901, 0X09C0, 0X0880, 0XC841,
    0XD801, 0X18C0, 0X1980, 0XD941, 0X1B00, 0XDBC1, 0XDA81, 0X1A40,
    0X1E00, 0XDEC1, 0XDF81, 0X1F40, 0XDD01, 0X1DC0, 0X1C80, 0XDC41,
    0X1400, 0XD4C1, 0XD581, 0X1540, 0XD701, 0X17C0, 0X1680, 0XD641,
    0XD201, 0X12C0, 0X1380, 0XD341, 0X1100, 0XD1C1, 0XD081, 0X1040,
    0XF001, 0X30C0, 0X3180, 0XF141, 0X3300, 0XF3C1, 0XF281, 0X3240,
    0X3600, 0XF6C1, 0XF781, 0X3740, 0XF501, 0X35C0, 0X3480, 0XF441,
    0X3C00, 0XFCC1, 0XFD81, 0X3D40, 0XFF01, 0X3FC0, 0X3E80, 0XFE41,
    0XFA01, 0X3AC0, 0X3B80, 0XFB41, 0X3900, 0XF9C1, 0XF881, 0X3840,
    0X2800, 0XE8C1, 0XE981, 0X2940, 0XEB01, 0X2BC0, 0X2A80, 0XEA41,
    0XEE01, 0X2EC0, 0X2F80, 0XEF41, 0X2D00, 0XEDC1, 0XEC81, 0X2C40,
    0XE401, 0X24C0, 0X2580, 0XE541, 0X2700, 0XE7C1, 0XE681, 0X2640,
    0X2200, 0XE2C1, 0XE381, 0X2340, 0XE101, 0X21C0, 0X2080, 0XE041,
    0XA001, 0X60C0, 0X6180, 0XA141, 0X6300, 0XA3C1, 0XA281, 0X6240,
    0X6600, 0XA6C1, 0XA781, 0X6740, 0XA501, 0X65C0, 0X6480, 0XA441,
    0X6C00, 0XACC1, 0XAD81, 0X6D40, 0XAF01, 0X6FC0, 0X6E80, 0XAE41,
    0XAA01, 0X6AC0, 0X6B80, 0XAB41, 0X6900, 0XA9C1, 0XA881, 0X6840,
    0X7800, 0XB8C1, 0XB981, 0X7940, 0XBB01, 0X7BC0, 0X7A80, 0XBA41,
    0XBE01, 0X7EC0, 0X7F80, 0XBF41, 0X7D00, 0XBDC1, 0XBC81, 0X7C40,
    0XB401, 0X74C0, 0X7580, 0XB541, 0X7700, 0XB7C1, 0XB681, 0X7640,
    0X7200, 0XB2C1, 0XB381, 0X7340, 0XB101, 0X71C0, 0X7080, 0XB041,
    0X5000, 0X90C1, 0X9181, 0X5140, 0X9301, 0X53C0, 0X5280, 0X9241,
    0X9601, 0X56C0, 0X5780, 0X9741, 0X5500, 0X95C1, 0X9481, 0X5440,
    0X9C01, 0X5CC0, 0X5D80, 0X9D41, 0X5F00, 0X9FC1, 0X9E81, 0X5E40,
    0X5A00, 0X9AC1, 0X9B81, 0X5B40, 0X9901, 0X59C0, 0X5880, 0X9841,
    0X8801, 0X48C0, 0X4980, 0X8941, 0X4B00, 0X8BC1, 0X8A81, 0X4A40,
    0X4E00, 0X8EC1, 0X8F81, 0X4F40, 0X8D01, 0X4DC0, 0X4C80, 0X8C41,
    0X4400, 0X84C1, 0X8581, 0X4540, 0X8701, 0X47C0, 0X4680, 0X8641,
    0X8201, 0X42C0, 0X4380, 0X8341, 0X4100, 0X81C1, 0X8081, 0X4040 };
    uint8_t TempNo;
    uint16_t crcVal = 0xFFFF;
    for (int i = 0; i < inputLength; i++) {
        TempNo = message[i] ^ crcVal;
        crcVal >>= 8;
        crcVal ^= CRCTable[TempNo];
    }
    return crcVal;
}

bool AddCRC(uint8_t* message, int inputLength, int maxInputLength) {
    if (inputLength + 2 <= maxInputLength) {
        uint16_t crcVal = GetCRC(message, inputLength);
        message[inputLength] = (uint8_t)(crcVal & 0xFF);
        message[inputLength + 1] = (uint8_t)(crcVal >> 8);
        return true;
    }
    return false;
}
