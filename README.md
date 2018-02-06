# Throttled Service Calls in OPC UA

This code accompanies the accepted IEEE ICIT'18 paper "Throttled Service Calls in OPC UA". The paper explains the architecture, data model, and implementation. 
The paper will be added to the docs folder upon publication.

This code is for PoC purposes and should not be used in production.

### Compiling
Based on a debootstrapped debian stretch VM:

Dependencies:
```sh
apt-get install -y dh-autoreconf wget make git openjdk-8-jdk libcppunit-dev libcppunit-doc libtool patch libzookeeper-mt-dev libjansson-dev libglib2.0-dev
```

Using open62541 version - commit: 03e272a01b137f9f0370bd378d4c1bdfac05a798

Compile:
```sh
ACLOCAL="aclocal -I /usr/share/aclocal" autoreconf -if
./configure && make
```

### Running a Client & Server

The ZooKeeper server/quorum should already be running.

Configure the zkUA configuration file (clientConf.txt and or serverConf.txt) and execute:
```sh
./zuthServer
./zuthClient
```

### Limitations

At the time of this writing, the base library for OPC UA (Open62541) does not support asynchronous calls.
