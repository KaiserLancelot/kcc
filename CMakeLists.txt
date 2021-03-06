cmake_minimum_required(VERSION 3.12)

project(
  kcc
  VERSION 0.6.0
  LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
list(APPEND CMAKE_PREFIX_PATH "/usr/lib/llvm-11/lib/cmake/lld")

include(CTest)
include(Options)
include(Check)
include(ClangTidy)
include(CompilerOptions)
include(Format)
include(LTO)

set(THREADS_PREFER_PTHREAD_FLAG TRUE)

find_package(Clang REQUIRED CONFIG)
find_package(LLVM 11 REQUIRED CONFIG)
find_package(LLD REQUIRED CONFIG)
find_package(fmt REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(Boost REQUIRED)
find_package(ICU REQUIRED COMPONENTS uc)
find_package(Threads REQUIRED)

add_definitions(-DKCC_VERSION="${PROJECT_VERSION}" ${LLVM_DEFINITIONS})
include_directories(include ${LLVM_INCLUDE_DIRS})

set(PROGRAM_NAME ${PROJECT_NAME})
set(RUN_ARGS "-h")

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)
file(GLOB_RECURSE CLANG_FORMAT_SOURCES CONFIGURE_DEPENDS include/*.h src/*.cpp)
file(GLOB_RECURSE CMAKE_FORMAT_SOURCES CONFIGURE_DEPENDS cmake/*.cmake
     CMakeLists.txt)

add_executable(${PROGRAM_NAME} ${SOURCES})

include(Coverage)
if((CMAKE_BUILD_TYPE MATCHES "Debug") OR (CMAKE_BUILD_TYPE MATCHES
                                          "RelWithDebInfo"))
  add_definitions(-DDEV)
endif()

target_link_libraries(
  ${PROGRAM_NAME}
  ${CMAKE_THREAD_LIBS_INIT}
  ${ICU_LIBRARIES}
  clang-cpp
  LLVM
  lldELF
  lldCommon
  nlohmann_json::nlohmann_json
  fmt::fmt-header-only)

install(TARGETS ${PROGRAM_NAME} RUNTIME DESTINATION bin)

enable_testing()

set(TEST_BINARY_DIR ${CMAKE_BINARY_DIR}/bin)
file(MAKE_DIRECTORY ${TEST_BINARY_DIR})
set(TEST_OBJ_DIR ${CMAKE_BINARY_DIR}/objs)
file(MAKE_DIRECTORY ${TEST_OBJ_DIR})

add_test(NAME check_PROGRAM_NAME COMMAND ${PROGRAM_NAME} -help)

add_test(NAME COMPILE--testmain
         COMMAND ${PROGRAM_NAME} ${CMAKE_SOURCE_DIR}/tests/usual/testmain.c -O0
                 -g -std=gnu17 -c -o ${TEST_OBJ_DIR}/testmain.o)

add_test(NAME COMPILE--testmain--OPT
         COMMAND ${PROGRAM_NAME} ${CMAKE_SOURCE_DIR}/tests/usual/testmain.c -O3
                 -g -std=gnu17 -c -o ${TEST_OBJ_DIR}/testmain_opt.o)

aux_source_directory(${CMAKE_SOURCE_DIR}/tests/usual USUAL_FILES)

foreach(USUAL_FILE ${USUAL_FILES})
  get_filename_component(USUAL_FILE_NAME ${USUAL_FILE} NAME_WE)
  if(USUAL_FILE_NAME MATCHES "testmain")
    continue()
  endif()

  add_test(NAME COMPILE--${USUAL_FILE_NAME}
           COMMAND ${PROGRAM_NAME} ${USUAL_FILE} ${TEST_OBJ_DIR}/testmain.o -O0
                   -g -std=gnu17 -o ${TEST_BINARY_DIR}/${USUAL_FILE_NAME})
  add_test(NAME RUN--${USUAL_FILE_NAME}
           COMMAND ${TEST_BINARY_DIR}/${USUAL_FILE_NAME})
  set_tests_properties(COMPILE--${USUAL_FILE_NAME} PROPERTIES DEPENDS
                                                              COMPILE--testmain)
  set_tests_properties(RUN--${USUAL_FILE_NAME}
                       PROPERTIES DEPENDS COMPILE--${USUAL_FILE_NAME})

  add_test(NAME COMPILE--${USUAL_FILE_NAME}--OPT
           COMMAND ${PROGRAM_NAME} ${USUAL_FILE} ${TEST_OBJ_DIR}/testmain_opt.o
                   -O3 -std=gnu17 -o ${TEST_BINARY_DIR}/${USUAL_FILE_NAME}_opt)
  add_test(NAME RUN--${USUAL_FILE_NAME}--OPT
           COMMAND ${TEST_BINARY_DIR}/${USUAL_FILE_NAME}_opt)
  set_tests_properties(COMPILE--${USUAL_FILE_NAME}--OPT
                       PROPERTIES DEPENDS COMPILE--testmain--OPT)
  set_tests_properties(RUN--${USUAL_FILE_NAME}--OPT
                       PROPERTIES DEPENDS COMPILE--${USUAL_FILE_NAME}--OPT)

endforeach()

add_test(
  NAME "COMPILE--8CC"
  COMMAND
    ${PROGRAM_NAME} ${CMAKE_SOURCE_DIR}/tests/8cc/*.c -O0 -g -std=gnu17
    -DBUILD_DIR=\"${CMAKE_SOURCE_DIR}/tests/8cc\" -o ${TEST_BINARY_DIR}/8cc)
add_test(NAME check_8cc_executable COMMAND ${TEST_BINARY_DIR}/8cc -h)

add_test(
  NAME "COMPILE--8CC--OPT"
  COMMAND
    ${PROGRAM_NAME} ${CMAKE_SOURCE_DIR}/tests/8cc/*.c -O3 -std=gnu17
    -DBUILD_DIR=\"${CMAKE_SOURCE_DIR}/tests/8cc\" -o ${TEST_BINARY_DIR}/8cc_opt)
add_test(NAME check_8cc_opt_executable COMMAND ${TEST_BINARY_DIR}/8cc_opt -h)

set(TEST_OBJ_DIR ${CMAKE_BINARY_DIR}/objs)
file(MAKE_DIRECTORY ${TEST_OBJ_DIR})
aux_source_directory(${CMAKE_SOURCE_DIR}/tests/8cc 8CC_FILES)

foreach(8CC_FILE ${8CC_FILES})
  get_filename_component(8CC_FILE_NAME ${8CC_FILE} NAME_WE)

  add_test(
    NAME "COMPILE--${8CC_FILE_NAME}_8cc"
    COMMAND
      ${TEST_BINARY_DIR}/8cc ${8CC_FILE}
      -DBUILD_DIR=\"${CMAKE_SOURCE_DIR}/tests/8cc\" -c -o
      ${TEST_OBJ_DIR}/${8CC_FILE_NAME}.o)

  add_test(
    NAME "COMPILE--${8CC_FILE_NAME}_8cc_opt"
    COMMAND
      ${TEST_BINARY_DIR}/8cc_opt ${8CC_FILE}
      -DBUILD_DIR=\"${CMAKE_SOURCE_DIR}/tests/8cc\" -c -o
      ${TEST_OBJ_DIR}/${8CC_FILE_NAME}.o)
endforeach()

add_test(
  NAME "COMPILE--LUA"
  COMMAND
    ${PROGRAM_NAME} ${CMAKE_SOURCE_DIR}/tests/lua/*.c -O0 -g -std=gnu17
    -DLUA_USER_H=\"ltests.h\" -DLUA_USE_LINUX -DLUA_COMPAT_5_2 -ldl -lreadline
    -lm -o ${TEST_BINARY_DIR}/lua)
add_test(NAME check_lua_executable COMMAND ${TEST_BINARY_DIR}/lua -v)

add_test(
  NAME "COMPILE--LUA--OPT"
  COMMAND
    ${PROGRAM_NAME} ${CMAKE_SOURCE_DIR}/tests/lua/*.c -O3 -std=gnu17
    -DLUA_USER_H=\"ltests.h\" -DLUA_USE_LINUX -DLUA_COMPAT_5_2 -ldl -lreadline
    -lm -o ${TEST_BINARY_DIR}/lua_opt)
add_test(NAME check_lua_opt_executable COMMAND ${TEST_BINARY_DIR}/lua_opt -v)

add_test(
  NAME lua_test
  COMMAND ${TEST_BINARY_DIR}/lua ${CMAKE_SOURCE_DIR}/tests/lua/testes/all.lua
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/tests/lua/testes)

add_test(
  NAME lua_test_opt
  COMMAND ${TEST_BINARY_DIR}/lua_opt
          ${CMAKE_SOURCE_DIR}/tests/lua/testes/all.lua
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/tests/lua/testes)

add_test(
  NAME "COMPILE--SQLITE"
  COMMAND
    ${PROGRAM_NAME} ${CMAKE_SOURCE_DIR}/tests/sqlite/shell.c
    ${CMAKE_SOURCE_DIR}/tests/sqlite/sqlite3.c -o ${TEST_BINARY_DIR}/sqlite -O0
    -g -std=gnu17 -lpthread -ldl -lm -DSQLITE_DEFAULT_MEMSTATUS=0
    -DSQLITE_DQS=0 -DSQLITE_ENABLE_DBSTAT_VTAB -DSQLITE_ENABLE_FTS5
    -DSQLITE_ENABLE_GEOPOLY -DSQLITE_ENABLE_JSON1 -DSQLITE_ENABLE_RBU
    -DSQLITE_ENABLE_RTREE -DSQLITE_LIKE_DOESNT_MATCH_BLOBS
    -DSQLITE_MAX_EXPR_DEPTH=0 -DSQLITE_OMIT_DECLTYPE -DSQLITE_OMIT_DEPRECATED
    -DSQLITE_USE_ALLOCA -DSQLITE_ENABLE_MEMSYS5)
add_test(NAME check_sqlite_executable COMMAND ${TEST_BINARY_DIR}/sqlite
                                              -version)

add_test(
  NAME "COMPILE--SQLITE--OPT"
  COMMAND
    ${PROGRAM_NAME} ${CMAKE_SOURCE_DIR}/tests/sqlite/shell.c
    ${CMAKE_SOURCE_DIR}/tests/sqlite/sqlite3.c -o ${TEST_BINARY_DIR}/sqlite_opt
    -O3 -std=gnu17 -lpthread -ldl -lm -DNDEBUG -DSQLITE_DEFAULT_MEMSTATUS=0
    -DSQLITE_DQS=0 -DSQLITE_ENABLE_DBSTAT_VTAB -DSQLITE_ENABLE_FTS5
    -DSQLITE_ENABLE_GEOPOLY -DSQLITE_ENABLE_JSON1 -DSQLITE_ENABLE_RBU
    -DSQLITE_ENABLE_RTREE -DSQLITE_LIKE_DOESNT_MATCH_BLOBS
    -DSQLITE_MAX_EXPR_DEPTH=0 -DSQLITE_OMIT_DECLTYPE -DSQLITE_OMIT_DEPRECATED
    -DSQLITE_USE_ALLOCA -DSQLITE_ENABLE_MEMSYS5)
add_test(NAME check_sqlite_opt_executable COMMAND ${TEST_BINARY_DIR}/sqlite_opt
                                                  -version)

add_custom_target(test_all COMMAND ctest -j1 --output-on-failure)
