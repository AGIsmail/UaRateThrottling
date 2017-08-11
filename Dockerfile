FROM ubuntu
RUN apt-get update && \
    apt-get install -y dh-autoreconf wget make \
    git openjdk-8-jdk libcppunit-dev libcppunit-doc \
    libtool patch libzookeeper-mt-dev libjansson-dev \
    libglib2.0-dev
#unzip wget curl docker jq coreutils
ADD . /etc/zkUAThrottle
WORKDIR /etc/zkUAThrottle
RUN make clean && make
CMD ["/bin/sh /etc/zkUAThrottle/zuthClient"]
