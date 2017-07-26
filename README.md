### Compiling
Based on a debootstrapped debian stretch VM:
Dependencies:
```sh
apt-get install -y dh-autoreconf wget make git openjdk-8-jdk libcppunit-dev libcppunit-doc libtool patch libzookeeper-mt-dev libjansson-dev
```
Using open62541 version - commit: 3702ffe7f3c720fd9e82a5177265a0fa960fb457

Compile:
```sh
ACLOCAL="aclocal -I /usr/share/aclocal" autoreconf -if
./configure && make

