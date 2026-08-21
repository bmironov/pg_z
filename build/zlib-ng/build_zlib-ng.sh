#!/usr/bin/env bash

docker build -t zlib-ng-release .

docker create --name zlib_temp zlib-ng-release

# Extract Native mode files only!
mkdir -p ./zlib-ng-dist
docker cp zlib_temp:/opt/zlib-ng-native ./zlib-ng-dist/

docker rm zlib_temp
docker rmi zlib-ng-release
docker builder prune -f
