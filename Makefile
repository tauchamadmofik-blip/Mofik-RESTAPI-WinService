# ==========================================================
# Makefile untuk Win64 REST Service & Client Demo (MinGW / GCC)
# ==========================================================

CXX = g++
CXXFLAGS = -std=c++17 -Wall
LIBS_SVC = -lws2_32 -lsqlite3
LIBS_CLI = -lws2_32

# Target Utama (Membangun Keduanya)
all: winrestapi_svc.exe client_demo.exe

# Membangun Windows Service Server
winrestapi_svc.exe: winrestapi_svc.cpp
	@echo "[BUILD] Mengompilasi Windows Service Server..."
	$(CXX) $(CXXFLAGS) winrestapi_svc.cpp -o winrestapi_svc.exe $(LIBS_SVC) -static

# Membangun Aplikasi Klien Demo
client_demo.exe: client_demo.cpp
	@echo "[BUILD] Mengompilasi Klien Demo..."
	$(CXX) $(CXXFLAGS) client_demo.cpp -o client_demo.exe $(LIBS_CLI)

# Membersihkan file binary dan log/database runtime
clean:
	@echo "[CLEAN] Menghapus file executable dan runtime..."
	rm -f winrestapi_svc.exe client_demo.exe server.log config_db.sqlite

# Perintah bantu untuk menjalankan client demo
run-client: client_demo.exe
	./client_demo.exe