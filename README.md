# Advanced Compiler Principle Assignment

Full mark assignment of Advanced Compiler Principle course taught by Lian Li, ICT, CAS.

## Introduction

Detailed introduction in corresponding directory.

- Assignment 1 (AST_Interpreter)
    - Implement a simple interpreter based on Clang frontend.
        - Support a subset of C syntax.
        - Basic input & output of `int`, i.e. `GET()` and `PRINT()`.
        - Basic dynamic memory allocation, i.e. `MALLOC()` and `FREE()`.
- Assignment 2
    - Implement a flow-insensitive function pointer transitive closure calculation program using SSA representation in LLVM backend.
- Assignment 3
    - Implement flow-sensitive, field-insensitive and context-insensitive inter-procedure pointer analysis using SSA representation in LLVM backend.

## Testcases and judging

In every `assignment/testcase` folder, contains testcases, standard answers and judging scripts.

### Assignment 1

- `ast-interpreter`: Soft link to assignment executable.
- `judge.py`: Judge script using `ast-interpreter` to grade your assignment.
- `test%02d.c`: Testcases.
- `lib*.c`: Dependencies for judging.

WARNING: If you want to test your own implementation via this judge script, make sure to 
- Print a `\n` in `PRINT()` invocation
- Send your output to `STDOUT`.

### Assignment 2 and 3

- `llvmassignment`: Soft link to assignment executable.
- `compile.py`: Compile `test%02d.c` to `test%02d.bc` which can be read by assignment executable.
- `judge.py`: Judge script using `llvmassignment` to grade your assignment.
- `test%02d.c` and `test%02d.bc`: Testcases.
- `std%02d.txt`: Standard answers.

WARNING: If you want to test your own implementation via this judge script, make sure that 
- The output result is sorted according to line number as first key ascendingly, function name as second key lexicographically ascendingly.
- Sending your output to `STDOUT`.

## Macros

### Assignment 1

- `ASSIGNMENT_DEBUG`: Reading C filename instead of C code from command line interface argument.
- `ASSIGNMENT_DEBUG_DUMP`: Dump detailed debug messages.

### Assignment 2

- `ASSIGNMENT_DEBUG_DUMP`: Dump detailed debug messages.

### Assignment 3

- `ASSIGNMENT_DEBUG_DUMP`: Dump detailed debug messages.
- `INTRA_PROCEDURE_ANALYSIS`: Switch option to do intra- or inter-procedure analysis in program.

## Docker image

```bash
docker pull registry.cn-hangzhou.aliyuncs.com/test_tc/llvm_hw:0.2

docker pull lczxxx123/llvm_10_hw:0.2
```

## Reference command for doing assignment 

### Method 1: Copy code into container

1. Create the container and exit.
```bash
docker run -it llvm_10_hw:0.2 --name llvm_ACPA /bin/bash
exit
```

2. Copy codes and testcases into the container.
```bash
docker cp $ASSIGNMENT_FOLDER llvm_ACPA:/root/
docker cp $TESTCASES llvm_ACPA:/root/
```

3. Compile your assignment.
```bash
docker exec llvm_ACPA mkdir ${tempBuildFolder}
docker exec -w ${tempBuildFolder} llvm_ACPA
cmake -DLLVM_DIR=/usr/local/llvm10ra/ $ASSIGNMENT_FOLDER
docker exec -w ${tempBuildFolder} llvm_ACPA make
```

4. Run executable in container of path `${tempBuildFolder}`.
```bash
docker exec -it llvm_ACPA /bin/bash
```

### Method 2: Copy LLVM library to host
```bash
sudo docker cp llvm_ACPA:/usr/local/llvm10ra/ /usr/local/llvm10ra/
```
