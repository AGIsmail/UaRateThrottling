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

#include <signal.h>
#include <stdlib.h>
#include <open62541.h>
#include <nodeset.h>
#include <zk_cli.h>
#include <zk_global.h>
#include <zuthClientSend.h>
#include <pthread.h>
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
UA_Client *client;
char *taskPath;

/* Running threads counter is from
 * https://stackoverflow.com/questions/6154539/how-can-i-wait-for-any-all-pthreads-to-complete */
volatile int running_threads = 0;
pthread_mutex_t running_mutex = PTHREAD_MUTEX_INITIALIZER;

static void *readDateTime(void * nullValue) {

    UA_ReadValueId item;
    UA_ReadValueId_init(&item);
    item.nodeId = UA_NODEID_NUMERIC(0,
            UA_NS0ID_SERVER_SERVERSTATUS_CURRENTTIME);
    item.attributeId = UA_ATTRIBUTEID_VALUE;
    int n = 0;
    UA_ReadRequest request;
    UA_ReadRequest_init(&request);
    request.nodesToRead = &item;
    request.nodesToReadSize = 1;
    UA_ReadResponse response;
    while (n < 1) {
        fprintf(stderr, "init_UA_Client: calling ZUTH_Client_Service_read\n");
        /* Call the read function once */
        response = ZUTH_Client_Service_read(zh, taskPath, client, request);
        UA_DateTime raw_date = *(UA_DateTime*) response.results->value.data;
        UA_String string_date = UA_DateTime_toString(raw_date);
        fprintf(stderr, "init_UA_Client: string date is %.*s\n",
                (int) string_date.length, string_date.data);
        UA_String_deleteMembers(&string_date);

        n++;
        UA_ReadResponse_deleteMembers(&response);
    }


    pthread_mutex_lock(&running_mutex);
    running_threads--;
    pthread_mutex_unlock(&running_mutex);
    return NULL;
}
/**
 * init_UA_client:
 *
 */
static void init_UA_client(zkUA_Config *zkUAConfigs) {

    UA_StatusCode retval;

    /* Initialize new client handle */

    client = UA_Client_new(UA_ClientConfig_standard);
    /* Create server destination string */
    char *serverDst = calloc(65535, sizeof(char));
    snprintf(serverDst, 65535, "opc.tcp://%s:%lu", zkUAConfigs->hostname,
            zkUAConfigs->uaPort);
    fprintf(stderr, "init_UA_Client: Getting endpoints from %s\n", serverDst);

    /* Listing endpoints */
    UA_EndpointDescription* endpointArray = NULL;
    size_t endpointArraySize = 0;
    retval = UA_Client_getEndpoints(client, serverDst, &endpointArraySize,
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
    /* Use the raw readAttribute service to submit a task to zk */
    UA_String *endpointUrlUAString = UA_getClientEndpointUrl(client);
    /* Initialize the global variable with the server redundancy group
     * task queue path */
    zkUA_setGroupGuidPath(groupGuid);
    zkUA_setQueuePath();
    /* Encode the tasks path for the specific endpoint */
    taskPath = zkUA_encodeServerQueuePath(*endpointUrlUAString);

    /* create 100 threads to read the date and time 100 times concurrently
     * NOTE: asynchronous calls are not yet supported in open62541.
     * You should use GNU parallel instead to test server loading*/
    pthread_t pth[100];
    pthread_attr_t att[100];
    for (int n = 0; n < 1; n++) { /* No async calls = only 1 thread at a time */
        if (pthread_attr_init(&att[n]) != 0) {
            break;
        }
        if (pthread_attr_setscope(&att[n], PTHREAD_SCOPE_SYSTEM) != 0) {
            break;
        }
        if (pthread_attr_setdetachstate(&att[n], PTHREAD_CREATE_DETACHED)
                != 0) {
            break;
        }
        pthread_mutex_lock(&running_mutex);
        running_threads++;
        pthread_mutex_unlock(&running_mutex);
        if (pthread_create(&pth[n], &att[n], readDateTime, (void *) NULL)
                != 0) {
            fprintf(stderr,
                    "init_UA_Client: Could not initialize a thread to read the date and time from the UA Server\n");
            pthread_mutex_lock(&running_mutex);
            running_threads--;
            pthread_mutex_unlock(&running_mutex);
            break;
        }
    }

    /* wait for all threads to return */
    while (running_threads > 0) {
        sleep(1);
    }
    /* Disconnect and exit */
    UA_Client_disconnect(client);
    free(taskPath);
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
    init_UA_client(&zkUAConfigs);
    if (to_send != 0)
        fprintf(stderr, "Recvd %d responses for %d requests sent\n", recvd,
                sent);
    zookeeper_close(zh);
    return 0;
}
