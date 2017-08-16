/**
 * open62541 libraries
 */
#include <signal.h>
#include <stdlib.h>
#include <open62541.h>
#include <nodeset.h>
#include <zk_cli.h>
#include <pthread.h>
#include <zk_urlEncode.h>
#include <jansson.h>
#include <glib.h>
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

/**
 * Declarations for open62541
 */
UA_Logger logger = UA_Log_Stdout;
UA_Boolean running = true;
UA_Server *server;
UA_ServerNetworkLayer nl;
zhandle_t *zh;
zhandle_t *zkHandle; /* Global variable */
char *serverQueuePath;
static clientid_t myid;

static int to_send = 0;
static int sent = 0;
static int recvd = 0;

static int verbose = 0;

void intHandler(int signum) {
    running = false;
}

void zuth_extractTaskId(const void *tId, char *taskId){
    const char *task = "task-";
    /* locate the task substring */
    char *locTask = strstr(*(char *const *) tId, task);
    /* determine the size of the task digits */
    size_t sizeOfTask = strlen(locTask) - strlen(task);
    /* copy the task Id into the store */
    strncpy(taskId, locTask + strlen(task), sizeOfTask);
}

static int taskIdCmp(const void *tId1, const void *tId2){
    char *taskId1 = calloc(65535, sizeof(char));
    char *taskId2 = calloc(65535, sizeof(char));

    zuth_extractTaskId(tId1, taskId1);
    zuth_extractTaskId(tId2, taskId2);

    /* convert the task Id's from string to long long */
    long long tI1, tI2;
    tI1 = strtoll(taskId1, NULL, 10);
    tI2 = strtoll(taskId2, NULL, 10);

    /* compare the task Id's */
    if (tI1 != tI2){
        free(taskId1);
        free(taskId2);
        if (tI1 < tI2)
            return -1;
        if (tI1 > tI2)
            return 1;
    } else fprintf(stderr, "taskIdCmp: Two tasks with the same Id! \n %s \n %s \n\n", tId1, tId2);
    return 0;

}

void zuth_executeRequest(char *stringRequest){

    int rc = 0;

    /* Get the task's data */
    int buffer_len = 65535;
    char *buffer = calloc(buffer_len, sizeof(char));
    struct Stat stat;
    rc = zoo_get(zh, stringRequest, 0, (char *) buffer, &buffer_len, &stat);
    /* Lood the root json object */
    json_error_t error;
    json_t *jsonRoot = json_loads(buffer, JSON_DISABLE_EOF_CHECK, &error);
    if (!jsonRoot) {
        fprintf(stderr,
                "zuth_executeRequest: Unable to load root object from retrieved JSON document - error: on line %d: %s \n stringRequest is %s\n",
                error.line, error.text, stringRequest);
        return;
    }
    /* Get the length of the ByteArray */
    json_t *jOffset = json_object_get(jsonRoot, "offset");
    size_t offset = json_integer_value(jOffset);
    /* Get the base64 encoded UA_ByteArray (encodedBinary) string */
    json_t *jString = json_object_get(jsonRoot, "dst");
    if(!jString){
        fprintf(stderr, "zuth_executeRequest: Unable to load dst string\n");
        return;
    }
    /* Convert the ByteArray from a JSON object to a string */
    char *conv = json_string_value(jString);
    /* Convert the ByteArray from a base64 encoded string into UA_ByteArray (unsigned char) */
    UA_ByteString *dst = UA_ByteString_new();
    UA_ByteString_init(dst);
    dst->length = offset;
    dst->data = g_base64_decode((const char *) conv, &dst->length);
    /* Decode the channelId */
    json_t *jChannel = json_object_get(jsonRoot, "channelId");
    /* Get the secureChannel of the client from the channelId */
    UA_SecureChannel *channel = ZUTH_getSecureChannel(server,
            json_integer_value(jChannel));
    /* Decode the requestId */
    json_t *jReqId = json_object_get(jsonRoot, "requestId");
    UA_UInt32 requestId = json_integer_value(jReqId);
    /* process the message */
    UA_SecureChannel_processChunks(channel, dst,
         (void*)UA_Server_processSecureChannelMessage, server);
    /* Delete the task on zk */
    rc = zoo_delete(zh, stringRequest,-1);
    if(rc){
        fprintf(stderr,"zoo_delete of task failed: %s", stringRequest);
        zkUA_error2String(rc);
    }
}

void zuth_processTasks(int rc, const struct String_vector *strings, const void *data){
    if (rc!=ZOK){
        fprintf(stderr, "ZooKeeper error upon processing tasks\n");
        intHandler(SIGINT);
    }
    fprintf(stderr, "zuth_ProcessTasks: Processing Tasks from path %s..\n", data);

    char *pathToTask = calloc(65535, sizeof(char));
    /* loop through all of the returned zk children */
     if (strings) {
         /* Sort array of returned children so that we add the nodes in ascending order of
          node IDs (assuming of course that a child will never have a smaller NodeId than a parent) */
         qsort(strings->data, strings->count, sizeof(char *), taskIdCmp);
         for (int i = 0; i < (strings->count); i++) {
             memset(pathToTask, 0, 65535);
             snprintf(pathToTask, 65535,"%s/%s", (char *) data, strings->data[i]);
             fprintf(stderr, "zuth_processTasks:\t%s\n",
                     pathToTask);
             zuth_executeRequest(pathToTask);
         }
     }

    free(pathToTask);
    free(data);
}
static void zkUA_queueWatcher(zhandle_t *zzh, int type, int state,
        const char *path, void* context) {

    UA_StatusCode sCode = UA_STATUSCODE_GOOD;
    /* Be careful using zh here rather than zzh - as this may be mt code
     * the client lib may call the watcher before zookeeper_init returns */
    fprintf(stderr, "Watcher %s state = %s", zkUA_type2String(type),
            zkUA_state2String(state));
    /* A task should only be added to the queue.
     * It should not be removed or changed after it has been added to the queue.
     * No duplicate tasks permitted (Keep a key-value hashtable, key is the sessionId,
     * value is the latest sequence number for that session. No tasks permitted with a
     * sequence number equal to or less than the stored sequence number).
     */
    if (path && strlen(path) > 0) {
        fprintf(stderr, " for path %s\n", path);
        char *termPath = calloc(65535, sizeof(char));
        snprintf(termPath, 65535, "%s", path);

        if (type == ZOO_CHILD_EVENT) {
            /* A task was created/deleted */
            /* Get the tasks list */
            fprintf(stderr,
                    "zkUA_queueWatcher: A node was created or deleted under %s\n",
                    termPath);
            struct String_vector strings;
            /* Only get the queue and start processing the new tasks if the
             * serviceLevel is Good. Otherwise, wait till it is good.
             */
            UA_NodeId nodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER_SERVICELEVEL);
            UA_Variant *outValue = UA_Variant_new();
            UA_Int32 value = 0;
            UA_Variant_setScalarCopy(outValue, &value, &UA_TYPES[UA_TYPES_INT32]);
            while(value < 200 /* 200-255 is healthy. See OPC UA Part 4*/){
                UA_StatusCode retval;
                retval = UA_Server_readValue(server, nodeId, outValue);
//                if(retval == UA_STATUSCODE_GOOD && UA_Variant_isScalar(outValue) && outValue->type == &UA_TYPES[UA_TYPES_INT32]){
                    value = *(UA_Int32 *)outValue->data;
                    printf("zkUA_queueWatcher: serviceLevel is: %i\n", value);
//                }
            }
            UA_Variant_delete(outValue);
            /* ServiceLevel is healthy, process queue */
            zoo_aget_children(zh, serverQueuePath, 1 /*watch*/, zuth_processTasks, strdup(serverQueuePath));
        }
        free(termPath);
    }
    fprintf(stderr, "\n");
    if (type == ZOO_SESSION_EVENT) {
        if (state == ZOO_CONNECTED_STATE) {
            const clientid_t *id = zoo_client_id(zzh);
            if (myid.client_id == 0 || myid.client_id != id->client_id) {
                myid = *id;
                fprintf(stderr, "Got a new session id: 0x%llx\n",
                _LL_CAST_ myid.client_id);
            }
        } else if (state == ZOO_AUTH_FAILED_STATE) {
            fprintf(stderr, "Authentication failure. Shutting down...\n");
            zookeeper_close(zzh);
            zh = 0;
        } else if (state == ZOO_EXPIRED_SESSION_STATE) {
            fprintf(stderr, "Session expired. Shutting down...\n");
            zookeeper_close(zzh);
            zh = 0;
        }
    }
}

static void init_ZUTH_Server(void *retval, zkUA_Config *zkUAConfigs) {

    UA_StatusCode *statuscode = (UA_StatusCode *) retval;
    int flags = 0;
    int rc;

    /* Convert Guid to string */
    char *groupGuid = calloc(65535, sizeof(char));
    snprintf(groupGuid, 65535, UA_PRINTF_GUID_FORMAT,
            UA_PRINTF_GUID_DATA(zkUAConfigs->guid));
    /* Check that the Queue path exists for the redundancy set on zk
     * and initialize it if it does not */
    zkUA_initializeZkServerQueuePath(groupGuid, zh);

    /* Initialize the tasks assignment path for this NTR server */
    char *serverUri = calloc(65535, sizeof(char));
    snprintf(serverUri, 65535, "opc.tcp://%s:%lu", zkUAConfigs->hostname,
            zkUAConfigs->uaPort); /* using config file hostname & port */
    char *encodedServerUri = (char *) zkUA_url_encode(serverUri);

    char *serverTaskPath = calloc(65535, sizeof(char));
    snprintf(serverTaskPath, 65535, "%s/%s", zkUA_getQueuePath(),
            encodedServerUri);
    char *path_buffer = calloc(65535, sizeof(char));
    int path_buffer_len = 65535;
    fprintf(stderr, "init_ZUTH_Server: Pushing task path %s\n", serverTaskPath);
    rc = zoo_create(zh, serverTaskPath, " ", 3, &ZOO_OPEN_ACL_UNSAFE, flags,
            path_buffer, path_buffer_len);
    if (rc != ZOK && rc != ZNODEEXISTS) {
        fprintf(stderr,
                "init_ZUTH_Server: Error %d (node exists is %d) for %s  - Exiting\n",
                rc, ZNODEEXISTS, serverTaskPath);
        zkUA_error2String(rc);
        intHandler(SIGINT);
    }
    /* Pushing server is active EPH znode */
    flags = 0;
    flags |= ZOO_EPHEMERAL;
    char *activePath = zkUA_getActivePath();
    char *serverActivePath = calloc(65535, sizeof(char));
    snprintf(serverActivePath, 65535, "%s/%s", activePath, encodedServerUri);
    fprintf(stderr, "init_ZUTH_Server: Pushing server is active to zk: %s\n",
            serverActivePath);
    rc = zoo_create(zh, serverActivePath, " ", 3, &ZOO_OPEN_ACL_UNSAFE, flags,
            path_buffer, path_buffer_len); /* I don't care about path_buffer to memset it */
    if (rc != ZOK) {
        fprintf(stderr, "init_ZUTH_Server: Error %d for %s - Exiting\n", rc,
                serverActivePath);
        zkUA_error2String(rc);
        intHandler(SIGINT);
    }
    free(activePath);
    free(serverActivePath);
    /* Initialize the hashmap that holds the newest task's ID for each session ID*/
    zkUA_initializeTaskHashmap(); // ONLY in case you want to do complex multi-threaded processing of tasks
    /* Get the tasks and set a watch */
    UA_String endpointURL = UA_String_fromChars(serverUri);
    serverQueuePath= zkUA_setServerQueuePath(endpointURL);
    struct String_vector strings;
    fprintf(stderr, "init_ZUTH_Server: Setting a watch on %s\n", serverQueuePath);
    zoo_aget_children(zh, serverQueuePath, 1 /*watch*/, zuth_processTasks, strdup(serverQueuePath));
    /* initialize the server */
    UA_ServerConfig config = UA_ServerConfig_standard;
    nl = UA_ServerNetworkLayerTCP(UA_ConnectionConfig_standard,
            zkUAConfigs->uaPort);
    config.networkLayers = &nl;
    config.networkLayersSize = 1;
    /* creates the server, namespaces, endpoints, sets the security configs etc.
     *  using the UA_ServerConfig defined above */
    server = UA_Server_new(config);
    /* More initializations */
    zkUA_initializeUaServerGlobal((void *) server);
    /* start server */
    statuscode = UA_Server_run(server, &running); //UA_blocks until running=false
    fprintf(stderr, "init_ZUTH_Server: Exiting after server run\n");
    /* ctrl-c received -> clean up */
    UA_Server_delete(server);
    nl.deleteMembers(&nl);
    zkUA_destroyTaskHashtable();
    free(groupGuid);
    free(serverUri);
    free(serverTaskPath);
    free(path_buffer);
    free_zkUAConfigs(zkUAConfigs);
    fprintf(stderr, "init_ZUTH_Server: Exiting with code %d\n", statuscode);
}

int main() {
    /* catches ctrl-c */
    struct sigaction sigIntHandler;
    sigIntHandler.sa_handler = intHandler;
    sigemptyset(&sigIntHandler.sa_mask);
    sigIntHandler.sa_flags = 0;
    sigaction(SIGINT, &sigIntHandler, NULL);

    /* Read the config file */
    zkUA_Config zkUAConfigs;
    zkUA_readConfFile("serverConf.txt", &zkUAConfigs);

    char buffer[4096];
    char p[2048];
    /* dummy certificate */
    strcpy(p, "dummy");
    verbose = 0;
    zoo_set_debug_level(ZOO_LOG_LEVEL_WARN);
    zoo_deterministic_conn_order(1); // enable deterministic order
    /* set global zookeeper handle variable */
    zh = zookeeper_init(zkUAConfigs.zooKeeperQuorum, zkUA_queueWatcher, 30000,
            &myid, 0, 0);
    zkHandle = zh;
    if (!zh) {
        return errno;
    }

    UA_StatusCode *retval;
    init_ZUTH_Server((void *) retval, &zkUAConfigs);
    if (to_send != 0)
        fprintf(stderr, "Recvd %d responses for %d requests sent\n", recvd,
                sent);
    zookeeper_close(zh);
    return 0;
}
