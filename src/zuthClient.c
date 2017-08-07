/**
 * open62541 libraries
 */
#include <signal.h>
#include <stdlib.h>
#include <open62541.h>
#include <nodeset.h>
#include <zk_cli.h>
#include <zk_global.h>
#include <zuthClientSend.h>
/**
 * ZooKeeper libraries
 */
#include <zookeeper.h>
#include <proto.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/select.h>
#include <time.h>
#include <errno.h>
#include <assert.h>

#ifdef YCA
#include <yca/yca.h>
#endif

#define _LL_CAST_ (long long)

static int verbose = 0;
UA_Logger logger = UA_Log_Stdout;
UA_Boolean running = true;

static zhandle_t *zh;
static clientid_t myid;
zhandle_t *zkHandle; /* Global variable */

static int to_send = 0;
static int sent = 0;
static int recvd = 0;
/**
 * init_UA_client:
 *
 */
static void init_UA_client(void* retval, zkUA_Config *zkUAConfigs) {

    UA_StatusCode *statuscode = (UA_StatusCode *) retval;

    /* Initialize new client handle */
    UA_Client *client;
    client = UA_Client_new(UA_ClientConfig_standard);
    /* Create server destination string */
    char *serverDst = calloc(65535, sizeof(char));
    snprintf(serverDst, 65535, "opc.tcp://%s:%lu", zkUAConfigs->hostname,
            zkUAConfigs->uaPort);
    fprintf(stderr, "init_UA_Client: Getting endpoints from %s\n", serverDst);

    /* Listing endpoints */
    UA_EndpointDescription* endpointArray = NULL;
    size_t endpointArraySize = 0;
    statuscode = UA_Client_getEndpoints(client, serverDst, &endpointArraySize,
            &endpointArray);
    if (retval != UA_STATUSCODE_GOOD) {
        UA_Array_delete(endpointArray, endpointArraySize,
                &UA_TYPES[UA_TYPES_ENDPOINTDESCRIPTION]);
        UA_Client_delete(client);
        free(serverDst);
        free_zkUAConfigs(zkUAConfigs);
        return;
    }
    printf("%i endpoints found\n", (int) endpointArraySize);
    for (size_t i = 0; i < endpointArraySize; i++) {
        printf("URL of endpoint %i is %.*s\n", (int) i,
                (int) endpointArray[i].endpointUrl.length,
                endpointArray[i].endpointUrl.data);
    }
    UA_Array_delete(endpointArray, endpointArraySize,
            &UA_TYPES[UA_TYPES_ENDPOINTDESCRIPTION]);

    /* Connect to a server */
    /* anonymous connect would be: retval = UA_Client_connect(client, "opc.tcp://localhost:16664"); */
    retval = UA_Client_connect_username(client, serverDst,
            zkUAConfigs->username, zkUAConfigs->password);
    if (retval != UA_STATUSCODE_GOOD) {
        free(serverDst);
        free_zkUAConfigs(zkUAConfigs);
        UA_Client_delete(client);
        return;
    }

    /* create groupGuid string */
    char *groupGuid = calloc(65535, sizeof(char));
    snprintf(groupGuid, 65535, UA_PRINTF_GUID_FORMAT,
            UA_PRINTF_GUID_DATA(zkUAConfigs->guid));
    /* Run service calls through zookeeper task assignment */
//    zkUA_UAServerAddressSpace(zh, client, serverDst, groupGuid);
    /* Use the raw read attribute to submit a task */

    UA_String *endpointUrlUAString = UA_getClientEndpointUrl(client);
    /* Initialize the global variable with the server redundancy group
     * task queue path */
    zkUA_setGroupGuidPath(groupGuid);
    zkUA_setQueuePath();
    /* Encode the tasks path for the specific endpoint */
    char *taskPath = zkUA_encodeServerQueuePath(*endpointUrlUAString);
    UA_ReadValueId item;
    UA_ReadValueId_init(&item);
    item.nodeId = UA_NODEID_NUMERIC(0,UA_NS0ID_SERVERSTATE);
    item.attributeId = UA_ATTRIBUTEID_DISPLAYNAME;
    UA_ReadRequest request;
    UA_ReadRequest_init(&request);
    request.nodesToRead = &item;
    request.nodesToReadSize = 1;
    UA_ReadResponse response = ZUTH_Client_Service_read(zh, taskPath, client, request);
//    fprintf(stderr, "zuthClient: taskPath is %s", taskPath);

    /* Disconnect and exit */
    UA_Client_disconnect(client);
    free(groupGuid);
    free(serverDst);
    free_zkUAConfigs(zkUAConfigs);
    UA_Client_delete(client);
}

int main() {

    /* Read the config file */
    zkUA_Config zkUAConfigs;
    zkUA_readConfFile("clientConf.txt", &zkUAConfigs);

    /* Initialize zk client */
    char buffer[4096];
    char p[2048];
    /* dummy certificate */
    strcpy(p, "dummy");
    verbose = 0;
    zoo_set_debug_level(ZOO_LOG_LEVEL_WARN);
    zoo_deterministic_conn_order(1); // enable deterministic order
    zh = zookeeper_init(zkUAConfigs.zooKeeperQuorum, zkUA_watcher, 30000, &myid,
            0, 0);
    if (!zh) {
        return errno;
    }
    zkHandle = zh; /* set global variable */

    UA_StatusCode *retval;
    init_UA_client((void *) retval, &zkUAConfigs);
    if (to_send != 0)
        fprintf(stderr, "Recvd %d responses for %d requests sent\n", recvd,
                sent);
    zookeeper_close(zh);
    return 0;
}
