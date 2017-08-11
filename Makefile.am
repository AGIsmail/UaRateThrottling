AUTOMAKE_OPTIONS = foreign
INCLUDES = -I/usr/include/zookeeper -I/usr/include/ -L/usr/lib/x86_64-linux-gnu -I/usr/include/glib-2.0/ -I/usr/lib/x86_64-linux-gnu/glib-2.0/include/
AM_CPPFLAGS = -I${srcdir}/include
AM_CFLAGS = -Wall -Werror -std=gnu99 $(INCLUDES)
AM_CXXFLAGS = -Wall $(USEIPV6)
LIB_LDFLAGS = -no-undefined

ZUTH_SRC = include/open62541.h src/open62541.c \
	include/zk_cli.h src/zk_cli.c include/nodeset.h src/nodeset.c \
	include/zk_global.h include/zk_urlEncode.h src/zk_urlEncode.c \
	include/zuthClientSend.h src/zuthClientSend.c


HASHTABLE_SRC = src/hashtable/hashtable_itr.h src/hashtable/hashtable_itr.c \
    src/hashtable/hashtable_private.h src/hashtable/hashtable.h src/hashtable/hashtable.c
    
EXPORT_SYMBOLS = '(zoo_|zookeeper_|zhandle|Z|format_log_message|log_message|logLevel|deallocate_|allocate_|zerror|is_unrecoverable|UA_|zuth|zkUA|hashtable_|create_|json_)'

lib_LTLIBRARIES = libzuth.la
libzuth_la_SOURCES = $(ZUTH_SRC) $(HASHTABLE_SRC)
libzuth_la_LIBADD = -lzookeeper_mt
libzuth_la_LDFLAGS = $(LIB_LDFLAGS) -export-symbols-regex $(EXPORT_SYMBOLS) $(INCLUDES)

bin_PROGRAMS = zuthClient zuthServer
zuthClient_SOURCES = src/zuthClient.c # $(ZUTH_SRC)
zuthClient_LDADD = libzuth.la -lpthread -lzookeeper_mt -ljansson -lglib-2.0
zuthClient_CFLAGS = -DTHREADED -DINTERCEPT $(INCLUDES)

zuthServer_SOURCES = src/zuthServer.c #$(ZUTH_SRC)
zuthServer_LDADD = libzuth.la -lpthread -lzookeeper_mt -ljansson -lglib-2.0
zuthServer_CFLAGS = -DTHREADED -DINTERCEPT $(INCLUDES)

