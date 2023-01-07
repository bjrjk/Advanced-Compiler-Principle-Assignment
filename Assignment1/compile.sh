#!/bin/bash
mkdir output
pushd output
cmake -DLLVM_DIR=/usr/local/llvm10ra/ -DASSIGNMENT_DEBUG=1 ..
make
popd
