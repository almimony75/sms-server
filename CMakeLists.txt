cmake_minimum_required(VERSION 3.10)
project(sms_sse_server)

set(CMAKE_CXX_STANDARD 17)

add_executable(sms_server src/main.cpp)
target_include_directories(sms_server PRIVATE ${CMAKE_SOURCE_DIR}/include)

# Optional: Link with pthreads
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(sms_server PRIVATE Threads::Threads)
