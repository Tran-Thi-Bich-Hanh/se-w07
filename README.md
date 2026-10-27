# TaskMaster - Hệ Thống Quản Lý Công Việc P2P Phân Tán (C++ WinSock2 + SQLite + Web UI)

## 📌 Giới thiệu dự án
**TaskMaster** là một ứng dụng quản lý công việc (Task Management) phân tán hoàn chỉnh được viết bằng **C++11** trên nền tảng Windows, sử dụng **WinSock2** (`<winsock2.h>`), cơ sở dữ liệu **SQLite3** với chế độ WAL (Write-Ahead Logging), và giao diện Web tĩnh hiện đại chuẩn **Kanban Board** (2 cột).

Ứng dụng tích hợp trọn vẹn lộ trình tiến hóa kiến trúc qua 4 phiên bản:
1. **V1 (Local):** CRUD công việc, HTTP Server WinSock2 cơ bản.
2. **V2 (Client-Server Concurrency):** Đa luồng (Multi-threading worker threads), an toàn dữ liệu đồng thời (`std::mutex`).
3. **V3 (N-Layer & SQLite):** Tách lớp kiến trúc chuẩn N-Layer (Model - Repository - Service - Server - UI), chuyển lưu trữ sang SQLite (`tasks.db`), tự động migrate toàn bộ dữ liệu cũ từ `tasks.json`. Giao diện chuyển thành bảng **Kanban 2 cột** ("Cần Làm" và "Đã Hoàn Thành").
4. **V4 (P2P Decentralized):**
   - **UDP Broadcast Auto-Discovery (Port 8888):** Tự động quét và phát hiện địa chỉ IP của các máy tính trong mạng LAN mà không cần cấu hình IP thủ công.
   - **TCP Direct Synchronization (Port 9000):** Kết nối socket TCP trực tiếp giữa các peer để đồng bộ danh sách task.
   - **Conflict Resolution (Last Writer Wins - LWW):** Giải quyết xung đột dữ liệu bằng dấu thời gian (Unix timestamp ms `updatedAt`). Task có timestamp mới hơn sẽ luôn chiến thắng.
   - **Khả năng chịu lỗi phân tán (Fault Tolerance):** Khi server trung tâm hoặc một máy mất kết nối, các máy khác vẫn hoạt động độc lập bình thường và tự động đồng bộ lại ngay khi online.

---

## 🏗 Kiến trúc thư mục (N-Layer Architecture)

```
Week_7/
├── CMakeLists.txt                  # Cấu hình build chuẩn CMake (C++11, ws2_32)
├── build.bat                       # Script build tự động với MinGW gcc/g++
├── tasks.db                        # SQLite Database lưu trữ công việc
├── tasks.json                      # File dữ liệu V1 (tự động migrate sang SQLite)
├── src/
│   ├── main.cpp                    # Khởi tạo DI container, nạp cấu hình và chạy Server
│   ├── sqlite3.c                   # SQLite amalgamation C source
│   ├── sqlite3.h                   # SQLite header
│   ├── models/
│   │   └── Task.h                  # Cấu trúc Task (id, title, desc, done, isDeleted, timestamps)
│   ├── repository/
│   │   ├── ITaskRepository.h       # Interface trừu tượng hóa tầng Data Access
│   │   ├── SqliteTaskRepository.h  # Repository lưu trữ SQLite (thread-safe, WAL)
│   │   └── SqliteTaskRepository.cpp
│   ├── service/
│   │   ├── TaskService.h           # Tầng nghiệp vụ (CRUD, validation, LWW merge)
│   │   └── TaskService.cpp
│   ├── p2p/
│   │   ├── PeerNode.h              # Thông tin node mạng (nodeId, ip, httpPort, p2pPort)
│   │   ├── P2PManager.h            # Quản lý UDP Broadcast Discovery & TCP Sync
│   │   └── P2PManager.cpp
│   └── server/
│       ├── HttpServer.h            # Multi-threaded HTTP Server WinSock2
│       └── HttpServer.cpp
└── web/
    ├── index.html                  # Giao diện Kanban Board & P2P Monitor
    ├── style.css                   # Glassmorphism dark mode, glowing accents
    └── app.js                      # Fetch API, render Kanban, P2P polling
```

---

## 🚀 Hướng dẫn Biên dịch & Chạy

### 1. Biên dịch
Mở Terminal hoặc Command Prompt tại thư mục dự án và chạy:
```cmd
build.bat
```
Script sẽ tự động:
1. Dùng `gcc` biên dịch `sqlite3.c` thành `sqlite3.o`.
2. Dùng `g++` biên dịch tất cả các module C++ và liên kết thư viện mạng Windows `ws2_32`.
3. Tạo file thực thi `TaskManager.exe`.

Hoặc sử dụng CMake:
```cmd
cmake -B build
cmake --build build
```

---

### 2. Kiểm thử P2P trên cùng 1 máy tính (2 Node)
Bạn có thể mở 2 cửa sổ Command Prompt để giả lập 2 máy tính ngang hàng trong mạng LAN:

**Node 1 (Cửa sổ 1):**
```cmd
TaskManager.exe 8080 9000 8888 tasks1.db
```
- Web UI: http://localhost:8080
- P2P Sync Port: 9000

**Node 2 (Cửa sổ 2):**
```cmd
TaskManager.exe 8081 9001 8888 tasks2.db
```
- Web UI: http://localhost:8081
- P2P Sync Port: 9001

👉 **Hiện tượng:**
- Cả 2 node sẽ phát UDP Broadcast trên port 8888 và **tự động nhận diện IP/Port của nhau**.
- Trên giao diện Web của Node 1 sẽ hiển thị Node 2 trong danh sách `Peers trong LAN` và ngược lại.
- Khi tạo, sửa trạng thái hoặc xóa task ở Node 1, task sẽ lập tức được đẩy qua TCP Socket và xuất hiện trên bảng Kanban của Node 2 theo cơ chế **Last Writer Wins (LWW)**!

---

### 3. Kiểm thử P2P giữa 2 máy tính thật trong mạng LAN
Chỉ cần copy thư mục đã build sang máy tính thứ 2 (cùng kết nối chung mạng WiFi / Ethernet):
- **Máy 1:** Chạy `TaskManager.exe 8080 9000 8888`
- **Máy 2:** Chạy `TaskManager.exe 8080 9000 8888`
Hai máy sẽ tự tìm thấy nhau qua mạng LAN không cần nhập IP thủ công!
