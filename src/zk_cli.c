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

/**
 * Parts of this file are from Zookeeper's cli.c sample program
 * and are licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#include <zookeeper.h>
#include <proto.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#ifndef WIN32
#include <sys/time.h>
#include <unistd.h>
#include <sys/select.h>
#else
#include "winport.h"
int read(int _FileHandle, void * _DstBuf, unsigned int _MaxCharCount);
int write(int _Filehandle, const void * _Buf, unsigned int _MaxCharCount);
#define ctime_r(tctime, buffer) ctime_s (buffer, 40, tctime)
#endif

#include <time.h>
#include <errno.h>
#include <assert.h>

#ifdef YCA
#include <yca/yca.h>
#endif

#include <zk_cli.h>
#include <zk_global.h>
#include <zk_urlEncode.h>
#define _LL_CAST_ (long long)

static zhandle_t *zh;
static clientid_t myid;
static const char *clientIdFile = 0;
struct timeval startTime;
static int batchMode = 0;

static int to_send = 0;
static int sent = 0;
static int recvd = 0;

static int shutdownThisThing = 0;

char zkUA_zkServerQueuePath[1024];
char zkUA_zkGroupGuidPath[1024];
struct hashtable *taskTable;
UA_Server *uaServerGlobal;

/* Initializes the global UA_Server variable */
void zkUA_initializeUaServerGlobal(void *server) {
    uaServerGlobal = (UA_Server *) server;
}

/* Functions for manipulating the taskTable hashtable */
/* Free's the hashtable and any remaining values in it */
void zkUA_destroyTaskHashtable() {
    hashtable_destroy(taskTable, 1);
}
/* Mzxid hashtable getter function */
void *zkUA_returnTaskHashtable() {
    return (void *) taskTable;
}
/* Key generating (string hashing) function for taskTable hashtable */
unsigned int zkUA_hash(void *str) { /* source: http://www.cse.yorku.ca/~oz/hash.html  */
    unsigned int hash = 5381;
    int c;
    char *string = (char *) str;
    while ((c = *string++)) {
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    }
    return hash;
}
/* Function to determine key equality */
int zkUA_equalKeys(void *k1, void *k2) {
    return (0 == memcmp(k1, k2, sizeof(unsigned long)));
}
/* Function to initialize taskTable hashtable */
void zkUA_initializeTaskHashmap() {
    taskTable = create_hashtable(16, zkUA_hash, zkUA_equalKeys);
    if (NULL == taskTable)
        exit(-1);
}

/* Functions for manipulating the ZooKeeper address space path */
/* Getter function for the OPC UA Server's address space path on ZooKeeper. */
/*char *zkUA_zkServAddSpacePath() {
    return &zkUA_zkServerAddressSpacePath[0];
}*/
/* Creates a string with the path for an OPC UA node on ZooKeeper. */
/*char *zkUA_encodeZnodePath(const UA_NodeId *nodeId) {
     TODO: support encoding of different identifier types: string, guid, etc.
    char *nodeZkPath = calloc(65535, sizeof(char));
    char *nodeValue = calloc(65535, sizeof(char));
    snprintf(nodeValue, 65535, "ns=%d;i=%d", nodeId->namespaceIndex,
            nodeId->identifier.numeric);
    snprintf(nodeZkPath, 65535, "%s/%s", zkUA_zkServAddSpacePath(), nodeValue);
    free(nodeValue);
    return nodeZkPath;
}*/
void zkUA_setGroupGuidPath(char *groupGuid){
    char *zkGroupPath = (char *) calloc(65535, sizeof(char));
    snprintf(zkGroupPath, 65535, "/Servers/%s", groupGuid);
    /* Set the global Group Guid path variable */
    snprintf(&zkUA_zkGroupGuidPath[0], 1024, "%s", zkGroupPath);
    free(zkGroupPath);
}
char *zkUA_getGroupGuidPath(){
    return &zkUA_zkGroupGuidPath[0];
}

void zkUA_setQueuePath(){
    /* Initialize the Queue path for the redundancy group */
     char *zkQueuePath = (char *) calloc(65535, sizeof(char));
     snprintf(zkQueuePath, 65535, "%s/Queue", zkUA_getGroupGuidPath());
     /* Set the qlobal Queue path variable */
     snprintf(&zkUA_zkServerQueuePath[0], 1024, "%s", zkQueuePath);
     free(zkQueuePath);
}
char *zkUA_getQueuePath() {
    return &zkUA_zkServerQueuePath[0];
}

char *zkUA_getActivePath(){
    char *zkServerActivePath = (char *) calloc(65535, sizeof(char));
    snprintf(zkServerActivePath, 65535, "%s/Active", zkUA_getGroupGuidPath());
    return zkServerActivePath;
}

char *zkUA_setServerQueuePath(const UA_String endpointURL){
    char *urlString = calloc(65535, sizeof(char));
    snprintf(urlString, 65535, "%.*s", (int) endpointURL.length, endpointURL.data);
    char *encodedUrlString = zkUA_url_encode(urlString);
    char *queuePath = calloc(65535, sizeof(char));
    snprintf(queuePath, 65535, "%s/%s", zkUA_getQueuePath(), encodedUrlString);
    fprintf(stderr,"zkUA_setServerQueuePath: queuePath %s\n", queuePath);
    free(encodedUrlString);
    free(urlString);
    return queuePath;
}
char *zkUA_encodeServerQueuePath(const UA_String endpointURL) {
    char *encodedUrlString = zkUA_setServerQueuePath(endpointURL);
    char *taskPath = calloc(65535, sizeof(char));
    snprintf(taskPath, 65535, "%s/task-", encodedUrlString);
    fprintf(stderr,"zkUA_encodeServerQueuePath: taskPath %s\n", taskPath);
    free(encodedUrlString);
    return taskPath;
}


/* Initialize ZkServerQueuePath string and the path on ZooKeeper */
void zkUA_initializeZkServerQueuePath(char *groupGuid, zhandle_t *zh) {
    int rc = -1;
    int flags = 0;

    /* Set the Group Guid path */
    zkUA_setGroupGuidPath(groupGuid);
    /* Set the Global Queue path */
    zkUA_setQueuePath(groupGuid);
    /*Get the Global Queue path */
    char *gQPath = zkUA_getQueuePath();
    fprintf(stderr, "zkUA_initializeZkServerQueuePath: Global Queue Path %s\n", gQPath);
    /* Check that the Queue path exists for the redundancy set on zk */
    fprintf(stderr, "Checking if queue path exists on zk: %s\n", gQPath);
    struct Stat stat;
    if(zh) rc = zoo_exists(zh, gQPath, 0, &stat);
    if (rc == ZOK) {
        fprintf(stderr, "zkUA_initializeZkServerQueuePath: The path exists: %s \n", gQPath);
        return;
    } else if (rc != ZNONODE) {
        /* TODO: Handle rc = ZNOAUTH | ZBADARGUMENTS | ZINVALIDSTATE | ZMARSHALLINGERROR
         * Operation: Free memory and exit */
    }

    /* Otherwise, let's initialize the path */
    /* Create a permanent /Servers node */
    char *path = "/Servers";
    char *path_buffer = calloc(65535, sizeof(char));
    if(zh) rc = zoo_create(zh, path, "new", 3, &ZOO_OPEN_ACL_UNSAFE, flags,
                path_buffer, 65535);
    free(path_buffer);
    if (rc) {
        fprintf(stderr, "zkUA_initializeZkServerQueuePath: Error %d for %s\n",
                rc, path);
        zkUA_error2String(rc);
    }

    /* Create a path to hold the redundancy group metadata*/
    char *zkServerPath = zkUA_getGroupGuidPath();
    fprintf(stderr, "zkUA_initializeZkServerQueuePath: Pushing to zk: %s\n", zkServerPath);
    /* Push serverAddress URI to zk */
    if(zh) rc = zoo_acreate(zh, zkServerPath, " ", 3, &ZOO_OPEN_ACL_UNSAFE,
            flags, zkUA_my_string_completion_free_data, strdup(path));
    if (rc) {
        fprintf(stderr, "zkUA_initializeZkServerQueuePath: Error %d for %s\n", rc, path);
    }

    char *zkServerActivePath = zkUA_getActivePath();
    fprintf(stderr, "zkUA_initializeZkServerQueuePath: Pushing to zk: %s\n", zkServerActivePath);
    /* Push Active path to zk */
    if(zh) rc = zoo_acreate(zh, zkServerActivePath, " ", 3, &ZOO_OPEN_ACL_UNSAFE,
            flags, zkUA_my_string_completion_free_data, strdup(path));
    if (rc) {
        fprintf(stderr, "zkUA_initializeZkServerQueuePath: Error %d for %s\n", rc, path);
    }
    free(zkServerActivePath);

   /*  Create a path to hold the server task queueus*/
    if(zh) rc = zoo_acreate(zh, gQPath, " ", 3, &ZOO_OPEN_ACL_UNSAFE,
            flags, zkUA_my_string_completion_free_data,
            strdup(gQPath));
    fprintf(stderr, "zkUA_initializeZkServerQueuePath: Pushing to zk: %s\n", gQPath);
    if (rc) {
        fprintf(stderr, "zkUA_initializeZkServerQueuePath: Error %d for %s\n", rc, path);
    }
}

/** Functions for manipulating config files **/
/* Free's the struct members storing the configs extracted from the config file. */
void free_zkUAConfigs(zkUA_Config *zkUAConfigs) {
    free(zkUAConfigs->hostname);
    free(zkUAConfigs->username);
    free(zkUAConfigs->password);
    free(zkUAConfigs->serverId);
    free(zkUAConfigs->zooKeeperQuorum);
}

/* Function to read the user-supplied config file */
void zkUA_readConfFile(char *confFileName, zkUA_Config *zkUAConfigs) {
    /* Read conf file */
    FILE *confFile;
    ssize_t chars = -1;
    /* Get the current working directory */
    char *cwd = zkUA_cleanStdoutFromCommand("pwd", &chars);
    char *confFilePath = calloc(65535, sizeof(char));
    snprintf(confFilePath, 65535, "%s/%s", cwd, confFileName);
    free(cwd);
    fprintf(stderr, "zkUA_readConfFile: Reading conf file: %s\n", confFilePath);
    /* Open the conf file */
    confFile = fopen(confFilePath, "r");
    if (confFile == NULL) {
        fprintf(stderr,
                "init_UA_Client: Error! Could not open the configuration file. Does it exist?\n");
        exit(-1);
    }
    /* Initialize the arguments to store the configurations */
    long int *uaPort = &zkUAConfigs->uaPort;
    *uaPort = 0;
    int *rSupport = &zkUAConfigs->rSupport;
    *rSupport = -1;
    int *state = &zkUAConfigs->state;
    *state = -1;
    /* Buffer for decoded availabilityPriority parameter */
    UA_Boolean *aPriority = &zkUAConfigs->aPriority;
    *aPriority = false;
    zkUAConfigs->hostname = calloc(65535, sizeof(char));
    zkUAConfigs->username = calloc(65535, sizeof(char));
    zkUAConfigs->password = calloc(65535, sizeof(char));
    zkUAConfigs->serverId = calloc(65535, sizeof(char));
    zkUAConfigs->zooKeeperQuorum = calloc(65535, sizeof(char));
    char *hostname = zkUAConfigs->hostname;
    char *username = zkUAConfigs->username;
    char *password = zkUAConfigs->password;
    char *serverId = zkUAConfigs->serverId;
    char *zooKeeperQuorum = zkUAConfigs->zooKeeperQuorum;
    /* Buffer for decoded GUID conf file parameter */
    UA_Guid_init(&zkUAConfigs->guid);
    UA_Guid *guid = &zkUAConfigs->guid;

    /* Initialize generic buffers for the configs */
    char *argument = calloc(65535, sizeof(char));
    char *argValue = calloc(65535, sizeof(char));
    /* Buffer for reading GUID from conf file */
    char *groupGuid = calloc(65535, sizeof(char));
    /* Initialize buffers for hex GUID decoding */
    char *data1 = calloc(10, sizeof(char));
    char *data2 = calloc(5, sizeof(char));
    char *data3 = calloc(5, sizeof(char));
    char data4[9][3];
    data1[8] = '\0';
    data2[4] = '\0';
    data3[4] = '\0';
    for (int dCnt = 0; dCnt < 8; dCnt++)
        data4[dCnt][2] = '\0';

    /* Extract the configurations */
    while (!feof(confFile)) {
        fscanf(confFile, "%s %s\n", argument, argValue);
        if (zkUA_startsWith(argument, "Hostname")) {
            memcpy(hostname, argValue, 65535);
            fprintf(stderr, "zkUA_readServerConfFile: confFile hostname %s\n",
                    hostname);
        } else if (zkUA_startsWith(argument, "PortNumber")) {
            *uaPort = strtol(argValue, NULL, 10);
            fprintf(stderr, "zkUA_readServerConfFile: confFile port %lu\n",
                    *uaPort);
        } else if (zkUA_startsWith(argument, "GroupGUID")) {
            /* Copy GUID into string buffer */
            memcpy(groupGuid, argValue, 65535);
            fprintf(stderr, "zkUA_readServerConfFile: confFile GroupGUID %s\n",
                    groupGuid);
            /* Split up GUID into strings equivalent to UA_Guid structure */
            memcpy(data1, argValue, sizeof(char) * 8);
            memcpy(data2, argValue + sizeof(char) * 8 + 1, sizeof(char) * 4);
            memcpy(data3, argValue + sizeof(char) * 14, sizeof(char) * 4);
            int dCnt2;
            int offset = 19;
            for (dCnt2 = 0; dCnt2 < 8; dCnt2++) {
                if (offset == 23)
                    offset++; /* skip the dash in the middle of the GUID */
                memcpy(&data4[dCnt2][0], argValue + offset, sizeof(char) * 2);
                offset += 2;
            }
            /* Convert GUID string from hex to decimal */
            zkUAConfigs->guid.data1 = strtoul(data1, NULL, 16);
            zkUAConfigs->guid.data2 = strtoul(data2, NULL, 16);
            zkUAConfigs->guid.data3 = strtoul(data3, NULL, 16);
            for (dCnt2 = 0; dCnt2 < 8; dCnt2++) {
                zkUAConfigs->guid.data4[dCnt2] = strtoul(&data4[dCnt2][0], NULL,
                        16);
            }
            fprintf(stderr,
                    "zkUA_readServerConfFile: Decoded GUID: "UA_PRINTF_GUID_FORMAT"\n",
                    UA_PRINTF_GUID_DATA(*guid));
        } else if (zkUA_startsWith(argument, "RedundancyType")) {
            if (zkUA_startsWith(argValue, "standalone"))
                *rSupport = 0;
            else if (zkUA_startsWith(argValue, "cold"))
                *rSupport = 1;
            else if (zkUA_startsWith(argValue, "warm"))
                *rSupport = 2;
            else if (zkUA_startsWith(argValue, "hot"))
                *rSupport = 3;
            else if (zkUA_startsWith(argValue, "transparent"))
                *rSupport = 4;
            else if (zkUA_startsWith(argValue, "hot+"))
                *rSupport = 5;
            else
                *rSupport = -1; /* Case should be handled by shutting down */
        } else if (zkUA_startsWith(argument, "State")) {
            if (zkUA_startsWith(argValue, "active")) {
                fprintf(stderr,
                        "zkUA_readServerConfFile: Node is in an active state.\n");
                *state = 1;
            } else {
                fprintf(stderr,
                        "zkUA_readServerConfFile: Node is in an inactive state.\n");
                *state = 0;
            }
        } else if (zkUA_startsWith(argument, "AvailabilityPriority")) {
            if (zkUA_startsWith(argValue, "true")) {
                *aPriority = true;
            } else
                *aPriority = false;
        } else if (zkUA_startsWith(argument, "Username")) {
            memcpy(username, argValue, 65535);
            fprintf(stderr, "zkUA_readServerConfFile: confFile username %s\n",
                    username);
        } else if (zkUA_startsWith(argument, "Password")) {
            memcpy(password, argValue, 65535);
            fprintf(stderr, "zkUA_readServerConfFile: confFile password %s\n",
                    password);
        } else if (zkUA_startsWith(argument, "ServerId")) {
            memcpy(serverId, argValue, 65535);
            fprintf(stderr, "zkUA_readServerConfFile: confFile serverId %s\n",
                    serverId);
        } else if (zkUA_startsWith(argument, "ZooKeeperQuorum")) {
            memcpy(zooKeeperQuorum, argValue, 65535);
            fprintf(stderr,
                    "zkUA_readServerConfFile: confFile zooKeeperQuorum %s\n\n",
                    zooKeeperQuorum);
        }
        memset(argument, 0, 65535);
        memset(argValue, 0, 65535);
    }
    /* free buffers */
    free(data1);
    free(data2);
    free(data3);
    free(argument);
    free(argValue);
    free(groupGuid);
    /* free buffers */
    free(confFilePath);
    fclose(confFile);

}

/* Start of code from linuxquestions */
/* Source: http://www.linuxquestions.org/questions/programming-9/c-c-popen-launch-process-in-specific-directory-620305/ */
char *getStdoutFromCommand(char * cmd, ssize_t * chars) {
    // setup
    FILE *fp = popen(cmd, "r");
    char *ln = NULL;
    size_t len = 0;
    *chars = -1;
    *chars = getline(&ln, &len, fp);
    pclose(fp);
    return (&ln[0]);
}

char *zkUA_cleanStdoutFromCommand(char *cmd, ssize_t *chars) {
    char *output;

    output = getStdoutFromCommand(cmd, chars);
    // Null terminate the end of the string
    if ((output)[*chars - 1] == '\n') {
        (output)[*chars - 1] = '\0';
        --*chars;
    }
    return output;
}

/* End of code from linuxquestions */

/* Function to convert a returned zookeeper status code from a client library call to a string */
void zkUA_error2String(int rc) {
    switch (rc) {
    case ZOK:
        fprintf(stderr,
                "zkUA_error2String: ZOK - operation completed successfully\n");
        break;
    case ZNONODE:
        fprintf(stderr,
                "zkUA_error2String: ZNONODE - the parent node does not exist.\n");
        break;
    case ZNODEEXISTS:
        fprintf(stderr,
                "zkUA_error2String: ZNODEEXISTS the node already exists\n");
        break;
    case ZNOAUTH:
        fprintf(stderr,
                "zkUA_error2String: ZNOAUTH the client does not have permission.\n");
        break;
    case ZNOCHILDRENFOREPHEMERALS:
        fprintf(stderr,
                "zkUA_error2String: ZNOCHILDRENFOREPHEMERALS cannot create children of ephemeral nodes.\n");
        break;
    case ZBADARGUMENTS:
        fprintf(stderr,
                "zkUA_error2String: ZBADARGUMENTS - invalid input parameters\n");
        break;
    case ZINVALIDSTATE:
        fprintf(stderr,
                "zkUA_error2String: ZINVALIDSTATE - zhandle state is either ZOO_SESSION_EXPIRED_STATE or ZOO_AUTH_FAILED_STATE\n");
        break;
    case ZMARSHALLINGERROR:
        fprintf(stderr,
                "zkUA_error2String: ZMARSHALLINGERROR - failed to marshall a request; possibly, out of memory\n");
        break;
    default:
        break;
    }
}
/* Function to decode a zookeeper connection state from a code to a printed string. */
const char* zkUA_state2String(int state) {
    if (state == 0)
        return "CLOSED_STATE";
    if (state == ZOO_CONNECTING_STATE)
        return "CONNECTING_STATE";
    if (state == ZOO_ASSOCIATING_STATE)
        return "ASSOCIATING_STATE";
    if (state == ZOO_CONNECTED_STATE)
        return "CONNECTED_STATE";
    if (state == ZOO_EXPIRED_SESSION_STATE)
        return "EXPIRED_SESSION_STATE";
    if (state == ZOO_AUTH_FAILED_STATE)
        return "AUTH_FAILED_STATE";

    return "INVALID_STATE";
}
/* Function to decode a watcher event type from a code to a printed string. */
const char* zkUA_type2String(int state) {
    if (state == ZOO_CREATED_EVENT)
        return "CREATED_EVENT";
    if (state == ZOO_DELETED_EVENT)
        return "DELETED_EVENT";
    if (state == ZOO_CHANGED_EVENT)
        return "CHANGED_EVENT";
    if (state == ZOO_CHILD_EVENT)
        return "CHILD_EVENT";
    if (state == ZOO_SESSION_EVENT)
        return "SESSION_EVENT";
    if (state == ZOO_NOTWATCHING_EVENT)
        return "NOTWATCHING_EVENT";

    return "UNKNOWN_EVENT_TYPE";
}

void zkUA_watcher(zhandle_t *zzh, int type, int state, const char *path,
        void* context) {
    /* Be careful using zh here rather than zzh - as this may be mt code
     * the client lib may call the watcher before zookeeper_init returns */

    fprintf(stderr, "Watcher %s state = %s", zkUA_type2String(type),
            zkUA_state2String(state));
    if (path && strlen(path) > 0) {
        fprintf(stderr, " for path %s", path);
    }
    fprintf(stderr, "\n");

    if (type == ZOO_SESSION_EVENT) {
        if (state == ZOO_CONNECTED_STATE) {
            const clientid_t *id = zoo_client_id(zzh);
            if (myid.client_id == 0 || myid.client_id != id->client_id) {
                myid = *id;
                fprintf(stderr, "Got a new session id: 0x%llx\n",
                _LL_CAST_ myid.client_id);
                if (clientIdFile) {
                    FILE *fh = fopen(clientIdFile, "w");
                    if (!fh) {
                        perror(clientIdFile);
                    } else {
                        int rc = fwrite(&myid, sizeof(myid), 1, fh);
                        if (rc != sizeof(myid)) {
                            perror("writing client id");
                        }
                        fclose(fh);
                    }
                }
            }
        } else if (state == ZOO_AUTH_FAILED_STATE) {
            fprintf(stderr, "Authentication failure. Shutting down...\n");
            zookeeper_close(zzh);
            shutdownThisThing = 1;
            zh = 0;
        } else if (state == ZOO_EXPIRED_SESSION_STATE) {
            fprintf(stderr, "Session expired. Shutting down...\n");
            zookeeper_close(zzh);
            shutdownThisThing = 1;
            zh = 0;
        }
    }
}

void zkUA_dumpStat(const struct Stat *stat) {
    char tctimes[40];
    char tmtimes[40];
    time_t tctime;
    time_t tmtime;

    if (!stat) {
        fprintf(stderr, "null\n");
        return;
    }
    tctime = stat->ctime / 1000;
    tmtime = stat->mtime / 1000;

    ctime_r(&tmtime, tmtimes);
    ctime_r(&tctime, tctimes);

    fprintf(stderr, "\tctime = %s\tczxid=%llx\n"
            "\tmtime=%s\tmzxid=%llx\n"
            "\tversion=%x\taversion=%x\n"
            "\tephemeralOwner = %llx\n", tctimes, _LL_CAST_ stat->czxid,
            tmtimes,
            _LL_CAST_ stat->mzxid, (unsigned int) stat->version,
            (unsigned int) stat->aversion,
            _LL_CAST_ stat->ephemeralOwner);
}

void zkUA_my_string_completion(int rc, const char *name, const void *data) {
    fprintf(stderr, "[%s]: rc = %d\n", (char*) (data == 0 ? "null" : data), rc);
    if (!rc) {
        fprintf(stderr, "\tname = %s\n", name);
    }
    if (batchMode)
        shutdownThisThing = 1;
}

void zkUA_my_string_completion_free_data(int rc, const char *name,
        const void *data) {
    zkUA_my_string_completion(rc, name, data);
    free((void*) data);
}

void zkUA_my_data_completion(int rc, const char *value, int value_len,
        const struct Stat *stat, const void *data) {
    struct timeval tv;
    int sec;
    int usec;
    gettimeofday(&tv, 0);
    sec = tv.tv_sec - startTime.tv_sec;
    usec = tv.tv_usec - startTime.tv_usec;
    fprintf(stderr, "time = %d msec\n", sec * 1000 + usec / 1000);
    fprintf(stderr, "%s: rc = %d\n", (char*) data, rc);
    if (value) {
        fprintf(stderr, " value_len = %d\n", value_len);
        assert(write(2, value, value_len) == value_len);
    }
    fprintf(stderr, "\nStat:\n");
    zkUA_dumpStat(stat);
    free((void*) data);
    if (batchMode)
        shutdownThisThing = 1;
}

void zkUA_my_silent_data_completion(int rc, const char *value, int value_len,
        const struct Stat *stat, const void *data) {
    recvd++;
    fprintf(stderr, "Data completion %s rc = %d\n", (char*) data, rc);
    free((void*) data);
    if (recvd == to_send) {
        fprintf(stderr, "Recvd %d responses for %d requests sent\n", recvd,
                to_send);
        if (batchMode)
            shutdownThisThing = 1;
    }
}

void zkUA_my_strings_completion(int rc, const struct String_vector *strings,
        const void *data) {
    struct timeval tv;
    int sec;
    int usec;
    int i;

    gettimeofday(&tv, 0);
    sec = tv.tv_sec - startTime.tv_sec;
    usec = tv.tv_usec - startTime.tv_usec;
    fprintf(stderr, "time = %d msec\n", sec * 1000 + usec / 1000);
    fprintf(stderr, "%s: rc = %d\n", (char*) data, rc);
    if (strings)
        for (i = 0; i < strings->count; i++) {
            fprintf(stderr, "\t%s\n", strings->data[i]);
        }
    free((void*) data);
    gettimeofday(&tv, 0);
    sec = tv.tv_sec - startTime.tv_sec;
    usec = tv.tv_usec - startTime.tv_usec;
    fprintf(stderr, "time = %d msec\n", sec * 1000 + usec / 1000);
    if (batchMode)
        shutdownThisThing = 1;
}

void zkUA_my_strings_stat_completion(int rc,
        const struct String_vector *strings, const struct Stat *stat,
        const void *data) {
    zkUA_my_strings_completion(rc, strings, data);
    zkUA_dumpStat(stat);
    if (batchMode)
        shutdownThisThing = 1;
}

void zkUA_my_void_completion(int rc, const void *data) {
    fprintf(stderr, "%s: rc = %d\n", (char*) data, rc);
    free((void*) data);
    if (batchMode)
        shutdownThisThing = 1;
}

void zkUA_my_stat_completion(int rc, const struct Stat *stat, const void *data) {
    fprintf(stderr, "%s: rc = %d Stat:\n", (char*) data, rc);
    zkUA_dumpStat(stat);
    free((void*) data);
    if (batchMode)
        shutdownThisThing = 1;
}

void zkUA_my_silent_stat_completion(int rc, const struct Stat *stat,
        const void *data) {
    //    fprintf(stderr, "State completion: [%s] rc = %d\n", (char*)data, rc);
    sent++;
    free((void*) data);
}
/* Compares the beginning of a string with a supplied prefix. */
int zkUA_startsWith(const char *line, const char *prefix) {
    int len = strlen(prefix);
    return strncmp(line, prefix, len) == 0;
}

