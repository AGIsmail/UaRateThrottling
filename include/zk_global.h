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
#include "src/hashtable/hashtable.h"
#include "src/hashtable/hashtable_itr.h"
#include <zookeeper.h>

extern char zkUA_zkServerQueuePath[1024];
extern char zkUA_zkGroupGuidPath[1024];
extern UA_Boolean replicateNode;
extern zhandle_t *zkHandle;
extern struct hashtable *taskTable;
extern UA_Server *uaServerGlobal;
