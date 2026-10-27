@echo off
echo =============================================================
echo   Building TaskMaster V4 (P2P + SQLite + N-Layer + WinSock2)
echo =============================================================

:: Compile SQLite3 C library if object file does not exist
if not exist sqlite3.o (
    echo [1/2] Compiling SQLite3 amalgamation with gcc...
    gcc -O2 -c src/sqlite3.c -o sqlite3.o
    if %ERRORLEVEL% NEQ 0 (
        echo [ERROR] Failed to compile sqlite3.c
        exit /b 1
    )
) else (
    echo [1/2] Using existing sqlite3.o
)

echo [2/2] Compiling TaskMaster C++ modules and linking...
g++ -std=c++11 -Wall -O2 src/main.cpp src/repository/SqliteTaskRepository.cpp src/service/TaskService.cpp src/p2p/P2PManager.cpp src/server/HttpServer.cpp sqlite3.o -lws2_32 -o TaskManager.exe

if %ERRORLEVEL% EQU 0 (
    echo.
    echo =============================================================
    echo   [SUCCESS] Build completed successfully: TaskManager.exe
    echo =============================================================
    echo.
    echo Usage examples:
    echo   Run 1st Node:  TaskManager.exe 8080 9000 8888 tasks.db
    echo   Run 2nd Node:  TaskManager.exe 8081 9001 8888 tasks2.db
    echo.
    echo Web Browser: http://localhost:8080
) else (
    echo [ERROR] Compilation failed! Check errors above.
    exit /b 1
)
