#!/usr/bin/env python3
# coding = utf-8

import os

MAX_TESTCASE_ID = 29
TOTAL_TESTCASE_NUMBER = MAX_TESTCASE_ID + 1

passed_testcase = 0

os.system("rm -rf build && mkdir build && cd build && cmake -DCMAKE_BUILD_TYPE=Debug -DLLVM_DIR=/usr/local/llvm10ra ../.. && cmake --build .")

for i in range(0, MAX_TESTCASE_ID + 1):
    os.system("gcc lib_std.c test%02d.c -o test%02d.out" % (i, i))
    os.system("./test%02d.out > std%02d.txt" % (i, i))
    print("> /bin/bash -c \"timeout 1 build/ast-interpreter '$(cat test%02d.c)' > ans%02d.txt\"" % (i, i))
    os.system("/bin/bash -c \"timeout 1 build/ast-interpreter '$(cat test%02d.c)' 2> ans%02d.txt\"" % (i, i))
    ret = os.system("diff std%02d.txt ans%02d.txt" % (i, i))
    if ret == 0:
        print("Testcase %02d Passed!" % i)
        passed_testcase += 1
    else:
        print("Testcase %02d failed!" % i)

print("%d / %d testcase passed." % (passed_testcase, TOTAL_TESTCASE_NUMBER))
