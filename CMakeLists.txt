cmake_minimum_required(VERSION 3.10)
project(TaskManager VERSION 4.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(src)

set(SOURCES
    src/main.cpp
    src/sqlite3.c
    src/repository/SqliteTaskRepository.cpp
    src/service/TaskService.cpp
    src/p2p/P2PManager.cpp
    src/server/HttpServer.cpp
)

set(HEADERS
    src/sqlite3.h
    src/models/Task.h
    src/repository/ITaskRepository.h
    src/repository/SqliteTaskRepository.h
    src/service/TaskService.h
    src/p2p/PeerNode.h
    src/p2p/P2PManager.h
    src/server/HttpServer.h
)

add_executable(TaskManager ${SOURCES} ${HEADERS})

# Link Windows Socket 2 library
target_link_libraries(TaskManager PRIVATE ws2_32)
