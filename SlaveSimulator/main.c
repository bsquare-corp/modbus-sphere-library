/**
 * @file    main.c
 * @brief   A program to simulate a modbus slave device communicating using rtu/tcp
 *
 * @author  Copyright (c) Bsquare EMEA 2020. https://www.bsquare.com/
 *          Licensed under the MIT License.
 */

#include "modbuscommands.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <sys/types.h>

#define IP "10.77.2.32"
#define PORT 8000

int main()
{
    //set up necessary variables
    SOCKET sockfd = INVALID_SOCKET;
    SOCKET connfd;
    int len;
    int result;
    int messageSize;
    struct sockaddr_in servaddr;
    struct sockaddr_in cli;
    WSADATA wsaData;
    uint8_t messageOut[256];
    uint8_t messageIn[256];
    int error = WSAStartup(MAKEWORD(1, 1), &wsaData);
    if (error != NO_ERROR)
    {
        printf("Startup failed: %d", error);
        return 1;
    }
    else
    {
        printf("Startup successful");
    }
    sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd == INVALID_SOCKET)
    {
        printf("Socket creation failed\n");
        return 1;
    }
    else
    {
        printf("Socket successfully created\n");
    }

    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr(IP);
    servaddr.sin_port = htons(PORT);


    // Binding newly created socket to given IP and verification 
    if ((bind(sockfd, (struct sockaddr*) & servaddr, sizeof(servaddr))) == SOCKET_ERROR) {
        printf("socket bind failed\n");
        return 1;
    }
    else
    {
        printf("Socket successfully binded\n");
    }
    // Now server is ready to listen and verification 
    if ((listen(sockfd, SOMAXCONN)) == SOCKET_ERROR) {
        printf("Listen failed\n");
        return 1;
    }
    else
    {
        printf("Server listening\n");
    }
    len = sizeof(cli);
    connfd = accept(sockfd, (struct sockaddr*) & cli, &len);
    if (connfd < 0)
    {
        printf("Server accept failed\n");
        return 1;
    }
    else
    {
        printf("Server accept successful\n");
    }
    while (1)
    {
        messageSize = recv(connfd, messageIn, sizeof(messageIn), 0);
        if (messageSize != SOCKET_ERROR)
        {
            result = processIncomingMessage(messageIn, messageSize, messageOut);
            if (result == 0)
            {
                if (AddCRC(messageOut, messageOut[2] + 3, 256))
                {
                    send(connfd, messageOut, messageOut[2] + 5, 0);
                }
                else
                {
                    printf("error: CRC failed");
                    return 1;
                }

            }
            else
            {
                messageOut[1] |= 0x80;
                messageOut[2] = result;
                if (AddCRC(messageOut, 3, 256))
                {
                    send(connfd, messageOut, 5, 0);
                }
                else
                {
                    printf("error: CRC failed");
                    return 1;
                }

            }
        }
        else
        {
            printf("error: %d\n", WSAGetLastError());
            return 1;
        }
    }

    //loop end
    return 0;
}


