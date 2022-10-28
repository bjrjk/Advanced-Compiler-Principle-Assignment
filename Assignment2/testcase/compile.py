#!/usr/bin/env python3
# coding = utf-8
import glob, os

testSources = glob.glob("test*.c")
for sourceName in testSources:
    sourceNameWithoutSuffix = sourceName[:-2]
    os.system("/usr/local/llvm10ra/bin/clang -emit-llvm -c -O0 -g3 %s.c -o %s.bc" % \
              (sourceNameWithoutSuffix, sourceNameWithoutSuffix))
