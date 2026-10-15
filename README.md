# Win64 REST Service & SQLite Logger

Layanan **Windows Service** native berbasis C++ (Win32 API) yang berjalan di latar belakang sebagai REST API pada port **1968**, ditenagai oleh **Winsock2** dan terintegrasi dengan database **SQLite3** untuk pencatatan log otomatis secara *multi-threaded*.

## Fitur API (POST Method & JSON Body)
Semua endpoint beroperasi menggunakan method **POST** dengan mengirimkan payload berformat JSON:
* **`POST /api`** -> Cek status server yang sedang berjalan.
* **`POST /api/time`** -> Mendapatkan informasi waktu server lokal (WIB).
* **`POST /api/logme`** -> Merekam ID petugas baru ke database SQLite (`{"id": "04082001"}`).
* **`POST /api/cari`** -> Mencari riwayat data berdasarkan ID (`{"id": "3"}`).
* **`POST /api/update`** -> Memperbarui keterangan data berdasarkan ID (`{"id": "3", "keterangan": "Keterangan Baru"}`).

---

## Cara Kompilasi Menggunakan Make
Pastikan Anda memiliki MinGW/GCC dan pustaka SQLite3 terinstal, lalu jalankan perintah berikut di terminal:
```bash
make
