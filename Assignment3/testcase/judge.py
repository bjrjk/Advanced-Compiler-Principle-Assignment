#!/usr/bin/env python3
# coding = utf-8

import os

MAX_TESTCASE_ID = 29
TOTAL_TESTCASE_NUMBER = MAX_TESTCASE_ID + 1

passed_testcase = 0

for i in range(0, TOTAL_TESTCASE_NUMBER):
    os.system("timeout 1 ./llvmassignment test%02d.bc > ans%02d.txt 2> /dev/null" % (i, i))
    ret = os.system("diff std%02d.txt ans%02d.txt" % (i, i))
    if ret == 0:
        print("Testcase %02d Passed!" % i)
        passed_testcase += 1
    else:
        print("Testcase %02d failed!" % i)

print("%d / %d testcase passed." % (passed_testcase, TOTAL_TESTCASE_NUMBER))
