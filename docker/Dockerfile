# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements. See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership. The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License. You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied. See the License for the
# specific language governing permissions and limitations
# under the License.
#

# First stage: the build environment
# Edge required for rocksdb
FROM alpine:3.5 AS builder
MAINTAINER Apache NiFi <dev@nifi.apache.org>

ARG UID
ARG GID
ARG MINIFI_VERSION
ARG MINIFI_SOURCE_CODE

# Install the system dependencies needed for a build
RUN apk --update --no-cache upgrade && apk --update --no-cache add gcc \
	g++ \
	make \
	bison \
	flex \
	flex-dev \
	wget \
	gdb \
	musl-dev \
	boost-dev \
	vim \
	util-linux-dev \
	curl-dev \
	cmake \
	git \
	unzip \
	gpsd-dev \
	libressl-dev \
	zlib-dev \
	python-dev

ENV USER minificpp
ENV MINIFI_BASE_DIR /opt/minifi

# Setup minificpp user
RUN addgroup -g $GID $USER && adduser -u $UID -D -G $USER -g "" $USER
RUN mkdir -p $MINIFI_BASE_DIR 

ADD $MINIFI_SOURCE_CODE $MINIFI_BASE_DIR
RUN chown -R $USER:$USER $MINIFI_BASE_DIR

USER $USER

ENV MINIFI_HOME $MINIFI_BASE_DIR/nifi-minifi-cpp-$MINIFI_VERSION

# Perform the build
RUN cd $MINIFI_BASE_DIR \
	&& mkdir build \
	&& cd build \
	&& cmake .. \
	&& make -j8 package \
	&& tar -xzvf $MINIFI_BASE_DIR/build/nifi-minifi-cpp-$MINIFI_VERSION-bin.tar.gz -C $MINIFI_BASE_DIR

# Second stage: the runtime image
# Edge required for rocksdb
FROM alpine:3.5

ARG UID
ARG GID
ARG MINIFI_VERSION
ARG MINIFI_SOURCE_CODE

# Add testing repo for rocksdb
RUN echo 'http://dl-cdn.alpinelinux.org/alpine/edge/testing' >> /etc/apk/repositories

RUN apk --update --no-cache upgrade && apk add --update --no-cache \
	util-linux \
	curl \
	unzip \
	gpsd \
	libressl \
	python \
	zlib

# Start MiNiFi CPP in the foreground
ENV USER minificpp
ENV MINIFI_BASE_DIR /opt/minifi
ENV MINIFI_HOME $MINIFI_BASE_DIR/nifi-minifi-cpp-$MINIFI_VERSION

RUN addgroup -g $GID $USER && adduser -u $UID -D -G $USER -g "" $USER
RUN mkdir -p $MINIFI_BASE_DIR 

# Copy built minifi distribution from builder
COPY --from=builder ${MINIFI_HOME} ${MINIFI_HOME}
RUN chown -R ${USER}:${USER} /opt/minifi

USER $USER

WORKDIR ${MINIFI_HOME}
CMD ./bin/minifi.sh run
