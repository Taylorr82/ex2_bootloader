/*
 * Copyright (C) 2015  University of Alberta
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
/**
 * @file updater.c
 * @author Robert Taylor
 * @date 2021-07-20
 */

#include "updater.h"
#include "bl_eeprom.h"
#include "bl_flash.h"
#include "services.h"
#include <FreeRTOS.h>
#include <csp/csp.h>
#include <csp/csp_endian.h>
#include <main/system.h>
#include <os_task.h>
#include "service_utilities.h"
#include <FreeRTOS-Plus-CLI/FreeRTOS_CLI.h>
#include "printf.h"
#include "privileged_functions.h"



static char *error = "None";
/**
 * hex2int
 * take a hex string and convert it to a 32bit number (max 8 hex digits)
 */
uint32_t hex2int(const char *hex) {
    uint32_t val = 0;
    while (*hex) {
        // get current character then increment
        uint8_t byte = *hex++;
        // transform hex character to the 4bit equivalent number, using the ascii table indexes
        if (byte >= '0' && byte <= '9') byte = byte - '0';
        else if (byte >= 'a' && byte <='f') byte = byte - 'a' + 10;
        else if (byte >= 'A' && byte <='F') byte = byte - 'A' + 10;
        // shift 4 to make space for new digit, and add the 4 bits of the new digit
        val = (val << 4) | (byte & 0xF);
    }
    return val;
}

static BaseType_t prvAddrCommand(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString) {
    int32_t image_param_len = 0;
    const char *image = FreeRTOS_CLIGetParameter(
                    /* The command string itself. */
                    pcCommandString,
                    /* Return the next parameter. */
                    1,
                    /* Store the parameter string length. */
                    &image_param_len);
    if (image_param_len > 1) {
        snprintf(pcWriteBuffer, xWriteBufferLen, "Invalid Image Type\n");
        return pdFALSE;
    }

    int32_t addr_str_len;
    const char *addr_str = FreeRTOS_CLIGetParameter(
                    /* The command string itself. */
                    pcCommandString,
                    /* Return the next parameter. */
                    2,
                    /* Store the parameter string length. */
                    &addr_str_len);

    if (addr_str_len > 10) {
        snprintf(pcWriteBuffer, xWriteBufferLen, "Hex string too long\n");
        return pdFALSE;
    }

    const char *hex_values = addr_str + 2;
    uint32_t new_address = hex2int(hex_values);

    switch (*image) {
    case 'A':
    {
        if (new_address < APP_MINIMUM_ADDR) {
            snprintf(pcWriteBuffer, xWriteBufferLen, "Address too low for application\n");
            return pdFALSE;
        }
        image_info addr_info = {0};
        eeprom_get_app_info(&addr_info);
        addr_info.addr = new_address;
        eeprom_set_app_info(&addr_info);
        break;
    }
    case 'G':
    {
        if (new_address < GOLD_MINIMUM_ADDR) {
            snprintf(pcWriteBuffer, xWriteBufferLen, "Address too low for golden image\n");
            return pdFALSE;
        }
        image_info addr_info = {0};
        eeprom_get_golden_info(&addr_info);
        addr_info.addr = new_address;
        eeprom_set_golden_info(&addr_info);
        break;
    }
    default:
        snprintf(pcWriteBuffer, xWriteBufferLen, "Invalid Image Type\n");
        return pdFALSE;
    }
    snprintf(pcWriteBuffer, xWriteBufferLen, "Address updated\n");
    return pdFALSE;
}

static BaseType_t prvInfoCommand(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString) {
    int32_t image_param_len = 0;
    const char *image = FreeRTOS_CLIGetParameter(
                    /* The command string itself. */
                    pcCommandString,
                    /* Return the next parameter. */
                    1,
                    /* Store the parameter string length. */
                    &image_param_len);
    if (image_param_len > 1) {
        snprintf(pcWriteBuffer, xWriteBufferLen, "Invalid Image Type\n");
        return pdFALSE;
    }

    image_info info = {0};
    switch (*image) {
    case 'A':
        eeprom_get_app_info(&info);
        break;
    case 'G':
        eeprom_get_golden_info(&info);
        break;
    default:
        snprintf(pcWriteBuffer, xWriteBufferLen, "Invalid Image Type\n");
        return pdFALSE;
    }
    snprintf(pcWriteBuffer, xWriteBufferLen, "Exists: %d\nSize: %d\nAddr: 0x%08X\nCrc: %04X\n",
             info.exists,
             info.size,
             info.addr,
             info.crc);
    return pdFALSE;

}

static BaseType_t prvErrorCommand(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString) {
    snprintf(pcWriteBuffer, xWriteBufferLen, "Error: %s\n", error);
    error = "None";
    return pdFALSE;
}


static BaseType_t prvVerifyAppCommand(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString) {
    if (verify_application()) {
        snprintf(pcWriteBuffer, xWriteBufferLen, "Success\n", error);

    } else {
        snprintf(pcWriteBuffer, xWriteBufferLen, "Failure\n", error);
    }
    return pdFALSE;
}

static BaseType_t prvVerifyGoldCommand(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString) {
    if (verify_golden()) {
        snprintf(pcWriteBuffer, xWriteBufferLen, "Success\n", error);
    } else {
        snprintf(pcWriteBuffer, xWriteBufferLen, "Failure\n", error);
    }
    return pdFALSE;
}

static const CLI_Command_Definition_t xAddrCommand = {"address", "address:\n\tSet address of image. Image is either G or A\n\tFirst parameter is image type, second parameter is address as hex\n", prvAddrCommand, 2};
static const CLI_Command_Definition_t xInfoCommand = {"info", "info:\n\tGet Image info. Image is either G or A\n", prvInfoCommand, 1};
static const CLI_Command_Definition_t xErrorCommand = {"error", "error:\n\tGet latest error in updater module\n", prvErrorCommand, 0};
static const CLI_Command_Definition_t xVerifyAppCommand = {"verifyapp", "verifyapp:\n\tVerify application image\n", prvVerifyAppCommand, 0};
static const CLI_Command_Definition_t xVerifyGoldCommand = {"verifygold", "error:\n\tVerify golden image\n", prvVerifyGoldCommand, 0};

/**
 * @brief
 *      Attempts to malloc a buffer
 * @detail
 *      This function will try to malloc smaller and smaller buffers until it succeeds
 *      In this case, having any buffer at all is more important than size
 * @param buf
 *      Pointer to the pointer to malloc the buffer in to
 * @return
 *      size of buffer that was allocated for
 */
void *get_buffer(int32_t size) {
    return pvPortMalloc(size);
}

/**
 * @brief
 *      Processes the incoming requests to decide what response is needed
 * @param conn
 *      Pointer to the connection on which to receive and send packets
 * @param packet
 *      The packet that was sent from the ground station
 * @return
 *      enum for return state
 */
SAT_returnState updater_app(csp_packet_t *packet) {
    uint8_t ser_subtype = (uint8_t)packet->data[SUBSERVICE_BYTE];
    int8_t status = 0;
    uint8_t oReturnCheck = 0;

    switch (ser_subtype) {
    case INITIALIZE_UPDATE:
        image_info app_info = {0};
        cnv8_32(&packet->data[IN_DATA_BYTE], &app_info.addr);
        cnv8_32(&packet->data[IN_DATA_BYTE + 4], &app_info.size);
        cnv8_16(&packet->data[IN_DATA_BYTE + 8], &app_info.crc);

        oReturnCheck = BLInternalFlashStartAddrCheck(app_info.addr, app_info.size);
        if (!oReturnCheck) {
            error = "Flash addr invalid";
            status = -1;
            break;
        }

        oReturnCheck = 0;
        oReturnCheck = Fapi_BlockErase(app_info.addr, app_info.size);
        if (oReturnCheck) {
            error = "Could not erase block";
            status = -1;
            break;
        }

        app_info.exists = EXISTS_FLAG;
        eeprom_set_app_info(&app_info);
        status = 0;
        update.initialized = true;
        update.start_address = app_info.addr;
        update.next_address = update.start_address;
        update.size = app_info.size;
        break;

    case PROGRAM_BLOCK :
        if (update.initialized == false) {
            status = -1;
            break;
        }
        uint16_t size;
        cnv8_16(&packet->data[IN_DATA_BYTE + 4], &size);
        uint32_t address;
        cnv8_32(&packet->data[IN_DATA_BYTE], &address);
        if (address != (update.next_address)) {
            error = "Block out of order";
            status = -1;
            break;
        }
        uint32_t flash_destination = address;

        uint8_t bank = address < 0x00200000 ? 0 : 1;
        uint8_t *buf = &packet->data[IN_DATA_BYTE + 6];

        oReturnCheck = Fapi_BlockProgram(bank, flash_destination, (unsigned long)buf, size);
        if (oReturnCheck) {
            error = "Failed to write to block";
            status = -1;
            break;
        }
        update.next_address = address + size;
        break;

    case END_UPDATE:
        break;

    case RESUME_UPDATE:
        break;

    case ERASE_APP:
        eeprom_get_app_info(&app_info);
        app_info.exists = 0;
        eeprom_set_app_info(&app_info);
        set_packet_length(packet, sizeof(int8_t) + 1);
        status = 0;
        break;

    case VERIFY_APPLICATION_IMAGE:
        if (verify_application() != true) {
            status = -1;
            break;
        }
        set_packet_length(packet, sizeof(int8_t) + 1);
        status = 0;
        break;

    case VERIFY_GOLDEN_IMAGE:
        if (verify_golden() != true) {
            status = -1;
            break;
        }
        set_packet_length(packet, sizeof(int8_t) + 1);
        status = 0;
        break;

    default:
        error = "No such subservice\n";
        status = -1;
    }
    if (status == -1) {
        set_packet_length(packet, sizeof(int8_t) + 1);
    }
    set_packet_length(packet, sizeof(int8_t) + 1);
    memcpy(&packet->data[STATUS_BYTE], &status, sizeof(int8_t));
    return SATR_OK;
}

/**
 * @brief
 *      FreeRTOS updater server task
 * @details
 *      Accepts incoming updater service packets and executes the application
 * @param void* param
 * @return None
 */
void updater_service(void *param) {

    csp_socket_t *sock;
    sock = csp_socket(CSP_SO_RDPREQ); // require RDP connection
    csp_bind(sock, TC_UPDATER_SERVICE);
    csp_listen(sock, SERVICE_BACKLOG_LEN);
    for (;;) {
        csp_conn_t *conn;
        csp_packet_t *packet;

        // wait for connection, timeout
        if ((conn = csp_accept(sock, DELAY_WAIT_TIMEOUT)) == NULL) {
            /* timeout */
            continue;
        }

        while ((packet = csp_read(conn, 50)) != NULL) {
            if (updater_app(packet) != SATR_OK) {
                // something went wrong, this shouldn't happen
                csp_buffer_free(packet);
            } else {
                if (!csp_send(conn, packet, 50)) {
                    csp_buffer_free(packet);
                }
            }
        }
        csp_close(conn);
    }
}

/**
 * @brief
 *      Start the updater server task
 * @details
 *      Starts the FreeRTOS task responsible for managing software updates
 * @param None
 * @return SAT_returnState
 *      success report
 */
SAT_returnState start_updater_service(void) {
    TaskHandle_t svc_tsk;
    FreeRTOS_CLIRegisterCommand(&xAddrCommand);
    FreeRTOS_CLIRegisterCommand(&xInfoCommand);
    FreeRTOS_CLIRegisterCommand(&xErrorCommand);
    FreeRTOS_CLIRegisterCommand(&xVerifyAppCommand);
    FreeRTOS_CLIRegisterCommand(&xVerifyGoldCommand);

    if (xTaskCreate((TaskFunction_t)updater_service, "updater_service", 300, NULL, NORMAL_SERVICE_PRIO  | portPRIVILEGE_BIT,
                    &svc_tsk) != pdPASS) {
        ex2_log("FAILED TO CREATE TASK updater_service\n");
        return SATR_ERROR;
    }
    ex2_log("Updater service started\n");
    return SATR_OK;
}