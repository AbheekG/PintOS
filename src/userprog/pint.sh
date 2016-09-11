#!/bin/bash

cd ../examples
make
cd ../userprog

make clean
make
cd build
pintos-mkdisk fs.dsk 2
pintos -f -q
pintos -p ../../examples/echo -a echo -- -q
pintos run "echo 123 1 2 3 abhcsk"

