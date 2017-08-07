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

static clientid_t myid;

static int to_send = 0;
static int sent = 0;
static int recvd = 0;

static int verbose = 0;

void intHandler(int signum) {
    running = false;
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
            /* Extract the task ID */
            /* Get data of each task and execute it sequentially */
//            zkUA_handleNewTasks(zzh, zkUA_zkServAddSpacePath(), server);
//            zkUA_UA_Server_replicateZk(zzh, zkUA_zkServAddSpacePath(), server);
            fprintf(stderr,
                    "zkUA_queueWatcher: A node was created or deleted under %s - replicating full addressSpace\n",
                    termPath);
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
    snprintf(serverTaskPath, 65535, "%s/%s", zkUA_getQueuePath(), encodedServerUri);
    char *path_buffer = calloc(65535, sizeof(char));
    int path_buffer_len = 65535;
    fprintf(stderr, "init_ZUTH_Server: Pushing task path %s\n", serverTaskPath);
    rc = zoo_create(zh, serverTaskPath, " ", 3, &ZOO_OPEN_ACL_UNSAFE,
             flags, path_buffer, path_buffer_len);
     if (rc!=ZOK && rc!=ZNODEEXISTS){
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
     fprintf(stderr, "init_ZUTH_Server: Pushing server is active to zk: %s\n", serverActivePath);
     rc = zoo_create(zh, serverActivePath, " ", 3, &ZOO_OPEN_ACL_UNSAFE,
              flags, path_buffer, path_buffer_len); /* I don't care about path_buffer to memset it */
      if (rc!=ZOK) {
          fprintf(stderr, "init_ZUTH_Server: Error %d for %s - Exiting\n", rc, serverActivePath);
          zkUA_error2String(rc);
          intHandler(SIGINT);
      }
      free(activePath);
      free(serverActivePath);
    /* Initialize the hashmap that holds the newest task's ID for each session ID*/
    zkUA_initializeTaskHashmap();
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

/*    switch (zkUAConfigs->rSupport) {
    case (2):  Warm Redundancy
    case (3): {  Hot redundancy
         Replicate or initialize addressSpace
        zoo_aget_children(zh, zkUA_zkServAddSpacePath(), 0,
                zkUA_checkAddressSpaceExists, &zkUAConfigs->guid);
        if (!zkUAConfigs->state) {  inactive server - await activation signal from the failover controller
             write server status as suspended
            zkUA_writeServerStatus(3);
        }
        break;
    }
    case (0):  Standalone server
    case (1):  Cold redundancy
    case (4):  Transparent Redundancy
    case (5): {  Hot+ Redundancy
        if (zkUAConfigs->state) {  active server
            zoo_aget_children(zh, zkUA_zkServAddSpacePath(), 0,
                    zkUA_checkAddressSpaceExists, &zkUAConfigs->guid);
             create thread to monitor changes to address space and apply them locally
        } else {  inactive server - error
            fprintf(stderr,
                    "init_UA_server: Error! Initialized as an inactive server. Exiting...\n");
            pthread_exit(&statuscode);  race?
        }
        break;
    }
    }
*/
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
    zh = zookeeper_init(zkUAConfigs.zooKeeperQuorum,
            zkUA_queueWatcher, 30000, &myid, 0, 0);
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
