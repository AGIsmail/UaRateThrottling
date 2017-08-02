/*
 * zuthClientSend.c
 *
 *  Created on: Jul 31, 2017
 *      Author: slint
 */

#include <open62541.h>
#include <zuthClientSend.h>
#include <zk_jsonEncode.h>
#include <jansson.h>
#include <zk_global.h>
#include <zk_cli.h>
//#include <zookeeper.h>


void ZUTH_jsonEncode_requestHeader(UA_RequestHeader *rr, json_t *jsonObject) {

    json_t *aToken = json_object();
    zkUA_jsonEncode_UA_NodeId(&rr->authenticationToken, aToken);
    json_t *timestamp = json_integer(rr->timestamp);
    json_t *requestHandle= json_integer(rr->requestHandle);
    json_t *returnDiagnostics = json_integer(rr->returnDiagnostics);
    json_t *auditEntryId = json_object();
    auditEntryId = zkUA_jsonEncode_UA_String(&rr->auditEntryId);
    json_t *timeoutHint = json_integer(rr->timeoutHint);
    json_t *additionalHeader= json_object();
    zkUA_jsonEncode_UA_ExtensionObject(&rr->additionalHeader, additionalHeader);

    json_t *reqHeader = json_object();
    json_object_set_new(reqHeader, "authenticationToken", aToken);
    json_object_set_new(reqHeader, "timestamp", timestamp);
    json_object_set_new(reqHeader, "requestHandle", requestHandle);
    json_object_set_new(reqHeader, "returnDiagnostics", returnDiagnostics);
    json_object_set_new(reqHeader, "auditEntryId", auditEntryId);
    json_object_set_new(reqHeader, "timeoutHint", timeoutHint);
    json_object_set_new(reqHeader, "additionalHeader", additionalHeader);

}

void
__ZUTH_Client_Service(zhandle_t *zh, const char *taskPath, UA_Client *client, const void *request, const UA_DataType *requestType,
                    void *response, const UA_DataType *responseType) {
    UA_init(response, responseType);
    UA_ResponseHeader *respHeader = (UA_ResponseHeader*)response;

    /* Make sure we have a valid session */
    UA_StatusCode retval = UA_Client_manuallyRenewSecureChannel(client);
    if(retval != UA_STATUSCODE_GOOD) {
        respHeader->serviceResult = retval;
        UA_setClientState(client, UA_CLIENTSTATE_ERRORED);
        return;
    }

    /* Adjusting the request header. The const attribute is violated, but we
     * only touch the following members: */
    UA_RequestHeader *rr = (UA_RequestHeader*)(uintptr_t)request;
    rr->authenticationToken = UA_getClientAuthToken(client); /* cleaned up at the end */

    rr->timestamp = UA_DateTime_now();
//    UA_UInt32 rHandle = UA_getClientRequestHandle(client);
    rr->requestHandle = UA_getClientRequestHandle(client);
    UA_setClientRequestHandle(client, rr->requestHandle + 1);
    /* Encode the requestHeader */
    json_t *rHeader = json_object();
    ZUTH_jsonEncode_requestHeader(rr, rHeader);
    UA_UInt32 requestId = UA_getClientRequestId(client);
    UA_setClientRequestId(client, requestId+1);
    json_t *jsonRequestId = json_integer(requestId);
    json_t *jsonRequest = json_object();
    json_object_set_new(jsonRequest, "Header", rHeader);
    json_object_set_new(jsonRequest, "requestId", jsonRequestId);
    /* Send the request to ZooKeeper*/
//
//    UA_LOG_DEBUG(client->config.logger, UA_LOGCATEGORY_CLIENT,
//                 "Sending a request of type %i", requestType->typeId.identifier.numeric);
//    char *queuePath = zkUA_encodeServerQueuePath(client->endpointUrl);
    char *s = json_dumps(jsonRequest, JSON_INDENT(1));
    char *path_buffer = calloc(65535, sizeof(char));
    int path_buffer_len = 65535;
    int flags = ZOO_SEQUENCE;
    int rc = zoo_create(zh, taskPath, s, strlen(s), &ZOO_OPEN_ACL_UNSAFE, flags, path_buffer, path_buffer_len);
    if(rc){
        fprintf(stderr, "ZUTH__UA_Client_Service: Could not create a task under the queue path %s - Task \n %s\n", taskPath, s);
    }
    free(s);
//    free(queuePath);

/*    retval = UA_SecureChannel_sendBinaryMessage(&client->channel, requestId, rr, requestType);
    if(retval != UA_STATUSCODE_GOOD) {
        if(retval == UA_STATUSCODE_BADENCODINGLIMITSEXCEEDED)
            respHeader->serviceResult = UA_STATUSCODE_BADREQUESTTOOLARGE;
        else
            respHeader->serviceResult = retval;
        client->state = UA_CLIENTSTATE_FAULTED;
        UA_NodeId_init(&rr->authenticationToken);
        return;
    }*/

    /* Prepare the response and the structure we give into processServiceResponse */
    ZUTH_receiveUAClientServiceResponse(client, request, requestType,
            response, responseType, requestId);
}


