cmake_minimum_required(VERSION 3.1.0)
project(assign2)
find_package(LLVM REQUIRED CONFIG HINTS ${LLVM_DIR} ${LLVM_DIR}/lib/cmake/llvm
	                NO_DEFAULT_PATH)

option(ASSIGNMENT_DEBUG_DUMP "ASSIGNMENT DEBUG DUMP" OFF)
IF(ASSIGNMENT_DEBUG_DUMP)
	add_definitions(-DASSIGNMENT_DEBUG_DUMP)
ENDIF(ASSIGNMENT_DEBUG_DUMP)

include_directories(${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS} SYSTEM)
link_directories(${LLVM_LIBRARY_DIRS})
message(STATUS "LLVM_LIB DIR : ${LLVM_LIBRARY_DIRS}")
set(LLVM_LINK_COMPONENTS
  LLVMCore
  LLVMIRReader
  LLVMPasses
  )

message(STATUS "LLVM LIBS : ${LLVM_LINK_COMPONENTS}")
# Support plugins.

add_executable(llvmassignment
  LLVMAssignment.cpp
  )

target_link_libraries(llvmassignment
	${LLVM_LINK_COMPONENTS}
	)
