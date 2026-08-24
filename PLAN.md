# Spotify Lyrics Display

## Arsitektur

Spotify Desktop Flatpak menyediakan status playback melalui MPRIS. Script
`tools/spotify_bridge.py` membaca metadata tersebut menggunakan `gdbus`, lalu
mengirim track ID, play/pause, posisi, dan durasi melalui USB serial. NodeMCU
merender metadata dan progress pada TFT ILI9341.

Jalur USB dipilih agar tidak memerlukan Bluetooth, Wi-Fi, Spotify Web API,
OAuth, touchscreen, atau tombol fisik.

## Tahapan

1. Validasi bridge MPRIS dan tampilan metadata/progress secara end-to-end.
2. Pengguna memberikan teks lirik yang berhak digunakan.
3. Catat timestamp per baris terhadap versi Spotify berdurasi 160 detik.
4. Simpan lirik dan timestamp di flash NodeMCU menggunakan `PROGMEM`.
5. Render tiga baris: sebelumnya redup, aktif kuning, berikutnya redup.
6. Gunakan posisi MPRIS sebagai sumber waktu sehingga pause dan seek langsung
   tersinkronisasi.

## Menjalankan

1. Upload firmware dengan `pio run -j 1 --target upload`.
2. Buka Spotify Desktop dan putar lagu.
3. Jalankan `python3 tools/spotify_bridge.py --port /dev/ttyUSB0`.
4. Hentikan bridge dengan `Ctrl+C` sebelum upload firmware berikutnya agar port
   serial tidak terkunci.

## Batasan

- Spotify/MPRIS tidak menyediakan teks lirik bertimestamp kepada firmware.
- Lirik lengkap tidak disalin otomatis; teks harus diberikan pengguna.
- `TFT_eSPI` harus tetap di versi `2.3.70` dan flash harus menggunakan DIO untuk
  menghindari reset loop pada board ini.
