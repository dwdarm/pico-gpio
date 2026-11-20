# RP2040 UART GPIO Expander

Firmware C untuk Raspberry Pi Pico (RP2040) dengan Pico SDK. Setelah reset,
semua pin expander berada dalam mode input tanpa pull internal.

## Sambungan

Gunakan UART 3,3 V dengan konfigurasi **115200 baud, 8N1, tanpa flow control**.
Jika host memakai logika 5 V, gunakan level shifter yang sesuai.

| Host | Raspberry Pi Pico |
| --- | --- |
| TX | GP1 / UART0 RX (pin fisik 2) |
| RX | GP0 / UART0 TX (pin fisik 1) |
| GND | GND (misalnya pin fisik 3) |

Pin expander yang tersedia adalah **GP2–GP22 dan GP26–GP28**. Angka dalam
perintah adalah nomor GPIO, misalnya `15` untuk GP15.

## Build dan flash

Dengan Pico SDK, CMake, dan toolchain Arm GNU sudah terpasang, jalankan dari
direktori proyek.

```sh
cmake -S . -B build -DPICO_SDK_PATH=/path/ke/pico-sdk
cmake --build build
```

Hubungkan Pico ke USB sambil menahan BOOTSEL. Salin
`build/rp2040_uart_gpio.uf2` ke drive `RPI-RP2`. Gunakan koneksi UART pada
tabel untuk mengirim perintah setelah Pico reboot.

## Protokol

Kirim satu baris ASCII per perintah. Akhiri dengan LF (`\n`), CR (`\r`),
atau CRLF. Firmware mengabaikan baris kosong dan mengakhiri setiap respons
dengan LF. Tunggu respons sebelum mengirim perintah berikutnya. Perintah dan
nama mode memakai huruf kapital. Tiap baris dapat memuat hingga 63 byte
sebelum penanda akhir baris.

| Perintah | Fungsi | Contoh respons |
| --- | --- | --- |
| `PING` | Memeriksa komunikasi | `OK PONG` |
| `MODE 14 IN` | Input tanpa pull internal | `OK` |
| `MODE 14 PULLUP` | Input dengan pull-up internal | `OK` |
| `MODE 15 OUT` | Mengaktifkan output digital, level awal 0 | `OK` |
| `WRITE 15 1` | Menulis 0 atau 1 ke pin mode OUT | `OK` |
| `READ 14` | Membaca level listrik pin saat perintah diproses | `OK 1` |
| `MODE 16 PWM` | Mengaktifkan PWM dengan duty awal 0% | `OK` |
| `PWM 16 250` | Mengatur duty 25% | `OK` |

Duty PWM menggunakan satuan permil. Nilai 0 berarti 0%, 500 berarti 50%,
dan 1000 berarti 100%. Frekuensi PWM tetap sekitar 1 kHz. Pada pin PWM,
`READ` mengambil sampel level logika sesaat. Mengirim ulang mode yang sedang
aktif mempertahankan level output atau duty yang telah disetel.

Dua GPIO dengan selisih 16 berbagi kanal PWM yang sama, misalnya GP2 dan GP18.
Jika GP2 sudah dalam mode PWM, ubah GP2 ke mode lain sebelum mengaktifkan PWM
di GP18. Kanal A dan B pada slice yang sama dapat memakai duty terpisah.

| Respons galat | Arti |
| --- | --- |
| `ERR COMMAND` | Nama perintah tidak dikenal |
| `ERR SYNTAX` | Jumlah argumen atau karakter tidak valid |
| `ERR LINE_TOO_LONG` | Baris terlalu panjang |
| `ERR PIN` | Nomor GPIO tidak valid atau tidak tersedia |
| `ERR MODE` | Mode tidak dikenal atau pin belum berada dalam mode yang diperlukan |
| `ERR VALUE` | Nilai di luar rentang atau bukan bilangan desimal |
| `ERR PWM_CHANNEL` | Kanal PWM sedang digunakan oleh GPIO pasangannya |

## Contoh host Python

Pasang `pyserial` dengan `python -m pip install pyserial` lalu sesuaikan
nama port dengan adaptor UART yang digunakan.

```python
import serial

with serial.Serial("/dev/ttyUSB0", 115200, timeout=1) as port:
    for command in ("PING", "MODE 14 PULLUP", "READ 14",
                    "MODE 15 OUT", "WRITE 15 1", "MODE 16 PWM", "PWM 16 250"):
        port.write((command + "\n").encode("ascii"))
        reply = port.readline()
        if not reply.endswith(b"\n"):
            raise TimeoutError(f"Respons tidak lengkap untuk {command}")
        response = reply.decode("ascii").strip()
        if not (response == "OK" or response.startswith("OK ")):
            raise RuntimeError(f"{command} -> {response}")
        print(command, "->", response)
```
