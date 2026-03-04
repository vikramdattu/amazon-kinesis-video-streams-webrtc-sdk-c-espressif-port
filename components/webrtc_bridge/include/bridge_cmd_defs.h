/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file bridge_cmd_defs.h
 * @brief Application-defined bridge command IDs and payload structures
 *
 * Add new command IDs and their corresponding request/response payload
 * structures here as needed. Each side (signaling_only / streaming_only)
 * registers handlers for the commands it can serve and sends commands
 * to query/control the remote side.
 */

/* -------------------------------------------------------------------------- */
/*  Command IDs                                                               */
/* -------------------------------------------------------------------------- */

#define BRIDGE_CMD_GET_RESOLUTION   0x0001  /**< Query camera resolution (P4 responds) */
#define BRIDGE_CMD_GET_SNAPSHOT     0x0003  /**< Capture a JPEG snapshot (P4 responds) */
#define BRIDGE_CMD_GET_ICE_SERVER   0x0004  /**< Request ICE server by index (C6 responds) */
#define BRIDGE_CMD_GET_TIME         0x0005  /**< Request current time from C6 (C6 responds) */

/* -------------------------------------------------------------------------- */
/*  Payload structures                                                        */
/* -------------------------------------------------------------------------- */

/**
 * @brief Response payload for BRIDGE_CMD_GET_RESOLUTION
 *
 * Sent by P4 (streaming_only) in response to a resolution query from C6.
 * No request payload is needed (req_data = NULL, req_len = 0).
 */
typedef struct {
    uint32_t width;     /**< Frame width in pixels */
    uint32_t height;    /**< Frame height in pixels */
} bridge_cmd_resolution_t;

/**
 * @brief Request payload for BRIDGE_CMD_GET_SNAPSHOT
 *
 * Sent by C6 (signaling_only) to request a JPEG snapshot from P4.
 */
typedef struct {
    uint8_t quality;    /**< JPEG quality 1-100 (0 = default 80) */
} bridge_cmd_snapshot_req_t;

#ifdef __cplusplus
}
#endif
