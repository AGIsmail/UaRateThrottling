### Compiling
Based on a debootstrapped debian stretch VM:
Dependencies:
```sh
apt-get install -y dh-autoreconf wget make git openjdk-8-jdk libcppunit-dev libcppunit-doc libtool patch libzookeeper-mt-dev libjansson-dev libglib2.0-dev
```

### Don't upgrade open62541
Using open62541 version - commit: 03e272a01b137f9f0370bd378d4c1bdfac05a798
(Newer commit causes an issue with UA Binary encoding and decoding)

Compile:
```sh
ACLOCAL="aclocal -I /usr/share/aclocal" autoreconf -if
./configure && make

