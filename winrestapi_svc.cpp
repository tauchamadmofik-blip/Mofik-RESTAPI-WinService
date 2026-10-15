#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <thread>
#include <mutex>
#include <atomic>
#include <sqlite3.h>

#pragma comment(lib, "ws2_32.lib")

#define SERVICE_NAME L"Win64RESTService"
#define SERVICE_DESC L"Layanan REST API Windows Service untuk REST API ditenagai g++ CYGWIN"
#define PORT 1968

SERVICE_STATUS        g_ServiceStatus;
SERVICE_STATUS_HANDLE g_StatusHandle;
HANDLE                g_StopEvent = NULL;
std::mutex            g_LogMutex;
std::atomic<bool>     g_ServerRunning(false);
SOCKET                g_ListenSocket = INVALID_SOCKET;
std::wstring          g_AppDirectory = L"";

// Fungsi untuk mendapatkan direktori absolut tempat .exe berada
std::wstring GetExecutableDirectory() {
    WCHAR buffer[MAX_PATH];
    GetModuleFileNameW(NULL, buffer, MAX_PATH);
    std::wstring::size_type pos = std::wstring(buffer).find_last_of(L"\\/");
    if (pos != std::wstring::npos) {
        return std::wstring(buffer).substr(0, pos);
    }
    return L"";
}

// Konversi wstring ke string UTF-8 untuk SQLite & Log
std::string WStringToString(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

// Mendapatkan path lengkap file database di folder aplikasi
std::string GetDatabasePath() {
    std::wstring dbPathW = g_AppDirectory + L"\\config_db.sqlite";
    return WStringToString(dbPathW);
}

// Mendapatkan path lengkap file log di folder aplikasi
std::string GetLogFilePath() {
    std::wstring logPathW = g_AppDirectory + L"\\server.log";
    return WStringToString(logPathW);
}

// Fungsi Thread-Safe Logger ke File
void WriteLog(const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_LogMutex);
    std::ofstream logFile(GetLogFilePath(), std::ios::app);
    std::string outTimeStr;
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::tm* tm_local = std::localtime(&now_c);
    char timeBuf[32];
    std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", tm_local);
    outTimeStr = std::string(timeBuf);

    if (logFile.is_open()) {
        logFile << outTimeStr << "|" << msg << std::endl;
    }
}

// Fungsi Debugging Ganda: File Log + Windows Event Log
void LogEvent(const std::string& msg, WORD eventType = EVENTLOG_INFORMATION_TYPE) {
    WriteLog(msg);

    HANDLE hEventSource = RegisterEventSourceW(NULL, SERVICE_NAME);
    if (hEventSource) {
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, &msg[0], (int)msg.size(), NULL, 0);
        std::wstring wmsg(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, &msg[0], (int)msg.size(), &wmsg[0], size_needed);

        LPCWSTR pwszStrings[1] = { wmsg.c_str() };
        ReportEventW(hEventSource, eventType, 0, 0, NULL, 1, 0, pwszStrings, NULL);
        DeregisterEventSource(hEventSource);
    }
}

// Fungsi Deskripsi Service
void SetServiceDescription() {
    SC_HANDLE hSCManager = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (hSCManager) {
        SC_HANDLE hService = OpenServiceW(hSCManager, SERVICE_NAME, SERVICE_CHANGE_CONFIG);
        if (hService) {
            SERVICE_DESCRIPTIONW sd;
            sd.lpDescription = (LPWSTR)SERVICE_DESC;
            ChangeServiceConfig2W(hService, SERVICE_CONFIG_DESCRIPTION, &sd);
            CloseServiceHandle(hService);
        }
        CloseServiceHandle(hSCManager);
    }
}

// Fungsi untuk memastikan tabel ada, jika belum ada maka dibuat (Auto Create Table)
bool EnsureTableExists(sqlite3* db) {
    char* errMsg = 0;
    const char* sql = "CREATE TABLE IF NOT EXISTS CatatInOutRESTWeb ("
                      "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                      "keterangan TEXT, "
                      "waktu TEXT);";

    int rc = sqlite3_exec(db, sql, 0, 0, &errMsg);
    if (rc != SQLITE_OK) {
        LogEvent("Gagal memastikan/membuat tabel CatatInOutRESTWeb: " + std::string(errMsg), EVENTLOG_ERROR_TYPE);
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

// Inisialisasi Database saat Startup Service
void InitDatabase() {
    std::string dbPath = GetDatabasePath();
    sqlite3* db;
    int rc = sqlite3_open(dbPath.c_str(), &db);
    
    if (rc) {
        LogEvent("Gagal membuka database SQLite di path: " + dbPath + " - " + std::string(sqlite3_errmsg(db)), EVENTLOG_ERROR_TYPE);
        sqlite3_close(db);
        return;
    }

    if (EnsureTableExists(db)) {
        LogEvent("Tabel CatatInOutRESTWeb siap digunakan pada: " + dbPath);
    }

    sqlite3_close(db);
}

// Mendapatkan format waktu JSON server time
std::string GetFormattedServerTimeJSON() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::tm* tm_local = std::localtime(&now_c);

    const char* days[] = {"Minggu", "Senin", "Selasa", "Rabu", "Kamis", "Jumat", "Sabtu"};
    const char* months[] = {"Januari", "Februari", "Maret", "April", "Mei", "Juni", "Juli", "Agustus", "September", "Oktober", "November", "Desember"};

    std::string dayName = days[tm_local->tm_wday];
    int dayNum = tm_local->tm_mday;
    std::string monthName = months[tm_local->tm_mon];
    int yearNum = tm_local->tm_year + 1900;

    char dateStr[64];
    std::strftime(dateStr, sizeof(dateStr), "%Y-%m-%d %H:%M:%S", tm_local);

    std::ostringstream oss;
    oss << "{\"Waktu Server\":[\"" << dayName << ", " << dayNum << " " << monthName << " " << yearNum << "\"],[\"" << dateStr << "\"],[\"WIB\"]}";
    return oss.str();
}

// Menyimpan ID Petugas ke SQLite3 dengan path absolut
bool InsertLogToDB(const std::string& petugasId, std::string& outTimeStr) {
    std::string dbPath = GetDatabasePath();
    sqlite3* db;
    int rc = sqlite3_open(dbPath.c_str(), &db);
    if (rc) {
        LogEvent("Gagal buka DB untuk insert log di path: " + dbPath, EVENTLOG_ERROR_TYPE);
        return false;
    }

    if (!EnsureTableExists(db)) {
        sqlite3_close(db);
        return false;
    }

    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::tm* tm_local = std::localtime(&now_c);
    char timeBuf[32];
    std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", tm_local);
    outTimeStr = std::string(timeBuf);

    std::string sql = "INSERT INTO CatatInOutRESTWeb (keterangan, waktu) VALUES ('ID Petugas: " + petugasId + "', '" + outTimeStr + "');";
    char* errMsg = 0;
    rc = sqlite3_exec(db, sql.c_str(), 0, 0, &errMsg);
    
    if (rc != SQLITE_OK) {
        LogEvent("Gagal insert log DB: " + std::string(errMsg), EVENTLOG_ERROR_TYPE);
        sqlite3_free(errMsg);
        sqlite3_close(db);
        return false;
    }
    LogEvent("'ID Petugas: " + petugasId + "' berhasil ditambahkan ke Database");
    sqlite3_close(db);
    return true;
}

// Fungsi Pencarian ID Petugas di Database
std::string SearchLogById(const std::string& id) {
    std::string dbPath = GetDatabasePath();
    sqlite3* db;
    int rc = sqlite3_open(dbPath.c_str(), &db);
    if (rc) {
        return "{\"ID Petugas\": \"" + id + "\",\"hasilpencarian\":\"koneksi database gagal\"}";
    }

    EnsureTableExists(db);

    std::string query = "SELECT keterangan, waktu FROM CatatInOutRESTWeb WHERE id = ?;";
    sqlite3_stmt* stmt;
    rc = sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, NULL);
    
    if (rc != SQLITE_OK) {
        sqlite3_close(db);
        return "{\"ID Petugas\": \"" + id + "\",\"hasilpencarian\":\"query gagal\"}";
    }

    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_STATIC);

    std::string jsonResult = "";
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* keteranganText = sqlite3_column_text(stmt, 0);
        const unsigned char* waktuText = sqlite3_column_text(stmt, 1);

        std::string ket = keteranganText ? reinterpret_cast<const char*>(keteranganText) : "";
        std::string wkt = waktuText ? reinterpret_cast<const char*>(waktuText) : "";

        std::ostringstream oss;
        oss << "{\"ID Petugas\": \"" << id << "\","
            << "\"hasilpencarian\":\"ada\","
            << "\"keterangan\":\"" << ket << "\","
            << "\"waktucatat\":\"" << wkt << "\"}";
        jsonResult = oss.str();
    } else {
        jsonResult = "{\"ID Petugas\": \"" + id + "\",\"hasilpencarian\":\"data tidak ditemukan\"}";
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return jsonResult;
}

// Fungsi Update Data Berdasarkan ID
std::string UpdateLogById(const std::string& id, const std::string& keteranganBaru) {
    std::string dbPath = GetDatabasePath();
    sqlite3* db;
    int rc = sqlite3_open(dbPath.c_str(), &db);
    if (rc) {
        return "{\"success\": false, \"message\": \"Koneksi database gagal\"}";
    }

    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::tm* tm_local = std::localtime(&now_c);
    char timeBuf[32];
    std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", tm_local);
    std::string waktuUpdate(timeBuf);

    std::string query = "UPDATE CatatInOutRESTWeb SET keterangan = ?, waktu = ? WHERE id = ?;";
    sqlite3_stmt* stmt;
    rc = sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, NULL);
    
    if (rc != SQLITE_OK) {
        sqlite3_close(db);
        return "{\"success\": false, \"message\": \"Gagal menyiapkan query update\"}";
    }

    sqlite3_bind_text(stmt, 1, keteranganBaru.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, waktuUpdate.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, id.c_str(), -1, SQLITE_STATIC);

    sqlite3_step(stmt);
    int changes = sqlite3_changes(db);

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    if (changes > 0) {
        return "{\"success\": true, \"message\": \"Data dengan ID " + id + " berhasil diperbarui\", \"waktupembaruan\": \"" + waktuUpdate + "\"}";
    } else {
        return "{\"success\": false, \"message\": \"Data dengan ID " + id + " tidak ditemukan\"}";
    }
}

// Fungsi helper sederhana untuk mengekstrak nilai string dari JSON mentah berbasis key
std::string ExtractJsonValue(const std::string& jsonBody, const std::string& key) {
    std::string searchKey = "\"" + key + "\"";
    size_t keyPos = jsonBody.find(searchKey);
    if (keyPos == std::string::npos) return "";

    size_t colonPos = jsonBody.find(':', keyPos);
    if (colonPos == std::string::npos) return "";

    size_t firstQuote = jsonBody.find('"', colonPos);
    if (firstQuote == std::string::npos) return "";

    size_t secondQuote = jsonBody.find('"', firstQuote + 1);
    if (secondQuote == std::string::npos) return "";

    return jsonBody.substr(firstQuote + 1, secondQuote - firstQuote - 1);
}

// Handler HTTP Server Client Thread (Sepenuhnya Berbasis POST & JSON Body)
void HandleClient(SOCKET clientSocket) {
    char buffer[4096] = {0};
    int bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
    if (bytesReceived > 0) {
        std::string request(buffer);
        std::string responseBody;
        std::string contentType = "application/json; charset=utf-8";
        int statusCode = 200;

        size_t firstSpace = request.find(' ');
        size_t secondSpace = request.find(' ', firstSpace + 1);
        if (firstSpace != std::string::npos && secondSpace != std::string::npos) {
            std::string method = request.substr(0, firstSpace);
            std::string path = request.substr(firstSpace + 1, secondSpace - firstSpace - 1);

            // Pisahkan Header dan Body Request
            std::string body = "";
            size_t bodyPos = request.find("\r\n\r\n");
            if (bodyPos != std::string::npos) {
                body = request.substr(bodyPos + 4);
            }

            if (method == "POST" && path == "/") {
                responseBody = "{\"status\": \"running\", \"service\": \"Win64RESTService\", \"port\": 1968}";
            }
            else if (method == "POST" && path == "/api/time") {
                responseBody = GetFormattedServerTimeJSON();
            }
            else if (method == "POST" && path == "/api/logme") {
                std::string petugasId = ExtractJsonValue(body, "id");
                if (petugasId.empty()) petugasId = ExtractJsonValue(body, "petugas_id");

                // Bersihkan karakter ':' jika tak sengaja dikirim
                if (!petugasId.empty() && petugasId[0] == ':') {
                    petugasId = petugasId.substr(1);
                }

                if (!petugasId.empty()) {
                    std::string savedTime = "";
                    if (InsertLogToDB(petugasId, savedTime)) {
                        responseBody = "{\"success\": true, \"message\": \"Data petugas " + petugasId + " berhasil dicatat\", \"waktu\": \"" + savedTime + "\"}";
                    } else {
                        responseBody = "{\"success\": false, \"message\": \"Gagal menyimpan ke database\"}";
                        statusCode = 500;
                    }
                } else {
                    responseBody = "{\"success\": false, \"message\": \"Parameter 'id' tidak ditemukan dalam body JSON\"}";
                    statusCode = 400;
                }
            }
            else if (method == "POST" && path == "/api/cari") {
                std::string cariId = ExtractJsonValue(body, "id");
                if (!cariId.empty() && cariId[0] == ':') {
                    cariId = cariId.substr(1);
                }

                if (!cariId.empty()) {
                    responseBody = SearchLogById(cariId);
                } else {
                    responseBody = "{\"success\": false, \"message\": \"Parameter 'id' tidak ditemukan dalam body JSON\"}";
                    statusCode = 400;
                }
            }
            else if (method == "POST" && path == "/api/update") {
                std::string updateId = ExtractJsonValue(body, "id");
                if (!updateId.empty() && updateId[0] == ':') {
                    updateId = updateId.substr(1);
                }

                std::string keteranganBaru = ExtractJsonValue(body, "keterangan");

                if (!updateId.empty()) {
                    if (keteranganBaru.empty()) keteranganBaru = "Keterangan Diperbarui";
                    responseBody = UpdateLogById(updateId, keteranganBaru);
                } else {
                    responseBody = "{\"success\": false, \"message\": \"Parameter 'id' tidak ditemukan dalam body JSON\"}";
                    statusCode = 400;
                }
            }
            else {
                responseBody = "{\"error\": \"404 Not Found atau Method Harus Menggunakan POST\"}";
                statusCode = 404;
            }
        }

        std::ostringstream httpResponse;
        httpResponse << "HTTP/1.1 " << statusCode << (statusCode == 200 ? " OK\r\n" : " Not Found\r\n")
                     << "Content-Type: " << contentType << "\r\n"
                     << "Content-Length: " << responseBody.size() << "\r\n"
                     << "Connection: close\r\n\r\n"
                     << responseBody;

        send(clientSocket, httpResponse.str().c_str(), (int)httpResponse.str().size(), 0);
    }
    closesocket(clientSocket);
}

// Thread Utama HTTP Server Listener
void StartHttpServer() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        LogEvent("WSAStartup gagal pada HTTP Server.", EVENTLOG_ERROR_TYPE);
        return;
    }

    g_ListenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_ListenSocket == INVALID_SOCKET) {
        LogEvent("Gagal membuat socket HTTP.", EVENTLOG_ERROR_TYPE);
        WSACleanup();
        return;
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(PORT);

    if (bind(g_ListenSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        LogEvent("Gagal bind socket ke port 1968.", EVENTLOG_ERROR_TYPE);
        closesocket(g_ListenSocket);
        WSACleanup();
        return;
    }

    if (listen(g_ListenSocket, SOMAXCONN) == SOCKET_ERROR) {
        LogEvent("Gagal listen pada socket.", EVENTLOG_ERROR_TYPE);
        closesocket(g_ListenSocket);
        WSACleanup();
        return;
    }

    g_ServerRunning = true;
    LogEvent("HTTP Server berhasil berjalan pada port 1968.");

    while (g_ServerRunning) {
        SOCKET clientSocket = accept(g_ListenSocket, NULL, NULL);
        if (clientSocket == INVALID_SOCKET) {
            break;
        }
        std::thread(HandleClient, clientSocket).detach();
    }

    closesocket(g_ListenSocket);
    WSACleanup();
}

// Handler Kontrol Service Windows
VOID WINAPI ServiceCtrlHandler(DWORD request) {
    switch (request) {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
            LogEvent("Menerima sinyal penghentian service.");
            g_ServerRunning = false;
            if (g_ListenSocket != INVALID_SOCKET) {
                closesocket(g_ListenSocket);
            }
            g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
            SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
            SetEvent(g_StopEvent);
            LogEvent("Service berhasil dihentikan.");
            break;
        default:
            break;
    }
}

// Fungsi Utama Service Worker
VOID WINAPI ServiceMain(DWORD argc, LPTSTR* argv) {
    g_AppDirectory = GetExecutableDirectory();

    g_StatusHandle = RegisterServiceCtrlHandlerW(SERVICE_NAME, ServiceCtrlHandler);
    if (!g_StatusHandle) return;

    ZeroMemory(&g_ServiceStatus, sizeof(g_ServiceStatus));
    g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
    g_ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

    SetServiceDescription();
    LogEvent("Service berhasil dimulai. Working Directory: " + WStringToString(g_AppDirectory));
    InitDatabase();

    std::thread serverThread(StartHttpServer);
    serverThread.detach();

    g_StopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    WaitForSingleObject(g_StopEvent, INFINITE);

    g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
}

int main(int argc, char* argv[]) {
    g_AppDirectory = GetExecutableDirectory();

    SERVICE_TABLE_ENTRYW ServiceTable[] = {
        { (LPWSTR)SERVICE_NAME, (LPSERVICE_MAIN_FUNCTIONW)ServiceMain },
        { NULL, NULL }
    };

    if (!StartServiceCtrlDispatcherW(ServiceTable)) {
        std::cerr << "Executable ini harus dijalankan sebagai Service hubungi Administrator" << std::endl;
        return 1;
    }
    return 0;
}