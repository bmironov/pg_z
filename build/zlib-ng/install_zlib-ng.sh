#!/usr/bin/env bash

# Install Native mode files only!
sudo cp ./zlib-ng-dist/zlib-ng-native/include/*.h /usr/local/include/
sudo cp -d ./zlib-ng-dist/zlib-ng-native/lib/libz-ng.* /usr/lib/x86_64-linux-gnu

# Update the dynamic linker cache so the system finds the new .so
sudo /sbin/ldconfig

# Validate that new library is installed correctly
/sbin/ldconfig -p | grep libz-ng
