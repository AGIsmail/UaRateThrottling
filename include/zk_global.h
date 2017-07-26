#include <open62541.h>
#include "src/hashtable/hashtable.h"
#include "src/hashtable/hashtable_itr.h"
//extern char zkUA_zkServerAddressSpacePath[1024];
extern UA_Boolean replicateNode;
extern zhandle_t *zkHandle;
extern struct hashtable *taskTable;
extern UA_Server *uaServerGlobal;
