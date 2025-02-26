cmake_minimum_required(VERSION 3.30)
project(quickjs_jsi)

set(CMAKE_CXX_STANDARD 17)

include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY ${CMAKE_HOME_DIRECTORY}/deps/googletest
    GIT_TAG        v1.15.2
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE) # For Windows: Prevent overriding the parent project's compiler/linker settings
FetchContent_MakeAvailable(googletest)

include_directories("lib")


set(SRCS_FBJSI "lib/jsi/jsi.cpp")
set(SRCS_QUICKJS "lib/quickjs/cutils.c" "lib/quickjs/libbf.c" "lib/quickjs/libregexp.c" "lib/quickjs/libunicode.c" "lib/quickjs/quickjs.c")

set(SRCS_RUNTIME "lib/quickjs-jsi/QuickJSRuntime.cpp")
set(SRCS_TEST "lib/quickjs-jsi/QuickJSRuntime_test.cpp" "lib/quickjs-jsi/QuickJSRuntimeJSI_test.cpp" "lib/jsi/test/testlib.cpp")

add_compile_options(-DCONFIG_VERSION="test-ver")
add_compile_options(-DUSE_BF_DEC)

# Now simply link against gtest or gtest_main as needed. Eg
add_executable(quickjs-test ${SRCS_RUNTIME} ${SRCS_TEST} ${SRCS_QUICKJS} ${SRCS_FBJSI})
target_link_libraries(quickjs-test gtest_main)
add_test(NAME quickjs-test COMMAND quickjs-test)
