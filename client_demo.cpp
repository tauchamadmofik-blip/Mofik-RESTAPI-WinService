#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <string>

#pragma comment(lib, "ws2_32.lib")

#define PORT 1968
#define SERVER_IP "127.0.0.1"

// Fungsi pembantu untuk mengirim HTTP POST Request ke Windows Service
std::string SendHttpPost(const std::string& endpoint, const std::string& jsonBody) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return "{\"error\": \"WSAStartup gagal\"}";
    }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        WSACleanup();
        return "{\"error\": \"Gagal membuat socket client\"}";
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &serverAddr.sin_addr);

    if (connect(sock, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        closesocket(sock);
        WSACleanup();
        return "{\"error\": \"Gagal terhubung ke server (Pastikan service sudah berjalan di port 1968)\"}";
    }

    // Buat HTTP/1.1 POST Request
    std::string request = "POST " + endpoint + " HTTP/1.1\r\n"
                          "Host: localhost:1968\r\n"
                          "Content-Type: application/json\r\n"
                          "Content-Length: " + std::to_string(jsonBody.length()) + "\r\n"
                          "Connection: close\r\n\r\n" + jsonBody;

    send(sock, request.c_str(), (int)request.length(), 0);

    // Terima respons dari server
    std::string response = "";
    char buffer[4096];
    int bytesReceived;
    while ((bytesReceived = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytesReceived] = '\0';
        response += buffer;
    }

    closesocket(sock);
    WSACleanup();

    // Pisahkan header HTTP dan ambil bagian Body JSON-nya saja
    size_t bodyPos = response.find("\r\n\r\n");
    if (bodyPos != std::string::npos) {
        return response.substr(bodyPos + 4);
    }

    return response;
}

void ShowMenu() {
    std::cout << "\n============================================\n";
    std::cout << "      WIN64 REST SERVICE - CLIENT DEMO      \n";
    std::cout << "============================================\n";
    std::cout << " [1] Cek Waktu Server (/api/time)\n";
    std::cout << " [2] Catat ID Petugas (/api/logme)\n";
    std::cout << " [3] Cari Data Berdasarkan ID (/api/cari)\n";
    std::cout << " [4] Update Keterangan Berdasarkan ID (/api/update)\n";
    std::cout << " [0] Keluar / Exit\n";
    std::cout << "--------------------------------------------\n";
    std::cout << "Pilih menu [0-4]: ";
}

int main() {
    // Set console agar mendukung output dengan baik
    std::system("cls");
    int pilihan;
    std::string inputId, inputKeterangan, jsonPayload, hasil;

    do {
        ShowMenu();
        if (!(std::cin >> pilihan)) {
            std::cin.clear();
            std::cin.ignore(10000, '\n');
            std::cout << "Input tidak valid. Masukkan angka 0-4.\n";
            continue;
        }
        std::cin.ignore(); // Membersihkan newline buffer

        switch (pilihan) {
            case 1:
                std::cout << "\n[INFO] Mengirim request ke /api/time...\n";
                hasil = SendHttpPost("/api/time", "{}");
                std::cout << "Respons Server:\n" << hasil << "\n";
                break;

            case 2:
                std::cout << "\nMasukkan ID Petugas yang ingin dicatat (contoh: 04082003): ";
                std::getline(std::cin, inputId);
                
                jsonPayload = "{\"id\":\"" + inputId + "\"}";
                std::cout << "[INFO] Mengirim request ke /api/logme...\n";
                hasil = SendHttpPost("/api/logme", jsonPayload);
                std::cout << "Respons Server:\n" << hasil << "\n";
                break;

            case 3:
                std::cout << "\nMasukkan ID yang ingin dicari (contoh: 3): ";
                std::getline(std::cin, inputId);

                jsonPayload = "{\"id\":\"" + inputId + "\"}";
                std::cout << "[INFO] Mengirim request ke /api/cari...\n";
                hasil = SendHttpPost("/api/cari", jsonPayload);
                std::cout << "Respons Server:\n" << hasil << "\n";
                break;

            case 4:
                std::cout << "\nMasukkan ID yang ingin di-update (contoh: 3): ";
                std::getline(std::cin, inputId);
                std::cout << "Masukkan Keterangan Baru: ";
                std::getline(std::cin, inputKeterangan);

                jsonPayload = "{\"id\":\"" + inputId + "\", \"keterangan\":\"" + inputKeterangan + "\"}";
                std::cout << "[INFO] Mengirim request ke /api/update...\n";
                hasil = SendHttpPost("/api/update", jsonPayload);
                std::cout << "Respons Server:\n" << hasil << "\n";
                break;

            case 0:
                std::cout << "\nKeluar dari aplikasi client demo. Terima kasih!\n";
                break;

            default:
                std::cout << "\nPilihan tidak valid. Silakan pilih 0 - 4.\n";
        }

        if (pilihan != 0) {
            std::cout << "\nTekan Enter untuk melanjutkan...";
            std::cin.get();
            std::system("cls");
        }

    } while (pilihan != 0);

    return 0;
}