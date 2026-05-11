include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/v1.14.0.zip
    DOWNLOAD_EXTRACT_TIMESTAMP true
)
FetchContent_MakeAvailable(googletest)

add_executable(test_timer test_timer.cpp)
target_link_libraries(test_timer PRIVATE timer_lib GTest::gtest_main)