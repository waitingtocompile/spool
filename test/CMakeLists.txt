
include (FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/609281088cfefc76f9d0ce82e1ff6c30cc3591e5.zip
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_executable(spool_test spool_test.cpp)
target_link_libraries(spool_test PUBLIC gtest_main Threads::Threads)
target_include_directories(spool_test PUBLIC "${PROJECT_SOURCE_DIR}/src/spool")



include(GoogleTest)
gtest_discover_tests(spool_test)

