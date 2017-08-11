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
