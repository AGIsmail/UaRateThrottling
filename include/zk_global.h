#include <open62541.h>
#include "src/hashtable/hashtable.h"
#include "src/hashtable/hashtable_itr.h"
#include <zookeeper.h>

extern char zkUA_zkServerQueuePath[1024];
extern char zkUA_zkGroupGuidPath[1024];
extern UA_Boolean replicateNode;
extern zhandle_t *zkHandle;
extern struct hashtable *taskTable;
extern UA_Server *uaServerGlobal;
