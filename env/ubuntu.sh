#!/bin/bash

sudo apt-get install -y make clang build-essential bison flex \
                        libpcre3-dev libpcre3

if [[ ! -d /usr/local/go ]]; then
	pushd /usr/local
	curl https://storage.googleapis.com/golang/go1.9.2.linux-amd64.tar.gz | sudo tar -xz
fi
