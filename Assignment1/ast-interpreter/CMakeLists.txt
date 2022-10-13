cmake_minimum_required(VERSION 3.1.0)
project(assign1)

find_package(Clang REQUIRED CONFIG HINTS ${LLVM_DIR} ${LLVM_DIR}/lib/cmake/clang NO_DEFAULT_PATH)

include_directories(${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS} SYSTEM)
link_directories(${LLVM_LIBRARY_DIRS})

file(GLOB SOURCE "*.cpp")

add_executable(ast-interpreter ${SOURCE})

option(ASSIGNMENT_DEBUG "ASSIGNMENT DEBUG MODE" OFF)
IF(ASSIGNMENT_DEBUG)
    add_definitions(-DASSIGNMENT_DEBUG)
ENDIF(ASSIGNMENT_DEBUG)

option(ASSIGNMENT_DEBUG_DUMP "ASSIGNMENT DEBUG DUMP" OFF)
IF(ASSIGNMENT_DEBUG_DUMP)
    add_definitions(-DASSIGNMENT_DEBUG_DUMP)
ENDIF(ASSIGNMENT_DEBUG_DUMP)

set( LLVM_LINK_COMPONENTS
  ${LLVM_TARGETS_TO_BUILD}
  Option
  Support
  )


target_link_libraries(ast-interpreter
  clangAST
  clangBasic
  clangFrontend
  clangTooling
  )

install(TARGETS ast-interpreter
  RUNTIME DESTINATION bin)
