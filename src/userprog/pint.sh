#!/bin/bash

cd ../examples
make
cd ../userprog

make clean
make
cd build
pintos-mkdisk fs.dsk 2
pintos -f -q
#pintos -p ../../examples/hello -a hello -- -q
#pintos run "hello"
make check
