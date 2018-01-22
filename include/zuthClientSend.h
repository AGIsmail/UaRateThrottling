/*******************************************************************************
 * Copyright (C) 2018 Ahmed Ismail <aismail [at] protonmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 ******************************************************************************/

#include <open62541.h>
#include <jansson.h>
#include <zookeeper.h>

void ZUTH_jsonEncode_requestHeader(UA_RequestHeader *rr, json_t *jsonObject);

//void __ZUTH_Client_Service(zhandle_t *zh, const char *taskPath, UA_Client *client, const void *request, const UA_DataType *requestType,
//                            void *response, const UA_DataType *responseType);

/**
 * Attribute Service Set
 * ^^^^^^^^^^^^^^^^^^^^^ */
static UA_INLINE UA_ReadResponse
ZUTH_Client_Service_read(zhandle_t *zh,  char *taskPath, UA_Client *client, const UA_ReadRequest request) {
    UA_ReadResponse response;
    __ZUTH_Client_Service(zh, taskPath, client, &request, &UA_TYPES[UA_TYPES_READREQUEST],
                        &response, &UA_TYPES[UA_TYPES_READRESPONSE]);
    return response;
}

static UA_INLINE UA_WriteResponse
ZUTH_Client_Service_write(zhandle_t *zh,  char *taskPath, UA_Client *client, const UA_WriteRequest request) {
    UA_WriteResponse response;
    __ZUTH_Client_Service(zh, taskPath, client, &request, &UA_TYPES[UA_TYPES_WRITEREQUEST],
                        &response, &UA_TYPES[UA_TYPES_WRITERESPONSE]);
    return response;
}
