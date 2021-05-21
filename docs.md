# Dokumentacja techniczna projektu "Serwer FTP na esp32"

- [Dokumentacja techniczna projektu "Serwer FTP na esp32"](#dokumentacja-techniczna-projektu-serwer-ftp-na-esp32)
  - [Uruchomienie](#uruchomienie)
    - [Wymagania](#wymagania)
    - [Konfiguracja](#konfiguracja)
    - [Kompilacja](#kompilacja)
    - [Połączenie](#połączenie)
  - [Struktura projektu](#struktura-projektu)
    - [`src/main.cpp`](#srcmaincpp)
    - [`lib/FTPServer/FTPServer.h`](#libftpserverftpserverh)
    - [`lib/MPU6050/MPU6050.h`](#libmpu6050mpu6050h)
    - [`lib/RFIDReader/RFIDReader.h`](#librfidreaderrfidreaderh)

## Uruchomienie

### Wymagania

Hardware:

- Moduł rozwojowy ESP-32, na przykład ESP-WROOM-32
- Czytnik kart SD z interfejsem SPI
- Układ MPU6050 - żyroskop i akcelerometr
- Układ RC522 – czytnik kart RFID 13.56MHz
- Dioda RGB (wspólna katoda)
- Fotorezystor
- Rezystory: 220Ω, 2.7kΩ

Polecany software:

- VSCode z rozszerzeniem PlatformIO lub Arduino IDE

### Konfiguracja

Przed wgraniem programu na esp32 wymagana jest wcześniejsza konfiguracja. Należy skopiować plik `src/credentials.h.sample` i nazwać go `src/credentials.h`. W utworzonym pliku konieczna jest zmiana konfiguracji połączenia z siecią wifi, wymaganej do działania z serwerem ftp.

```
const char* ssid = "********";
const char* password = "********";
```

Wymagana jest zmiana wartości `ssid` na nazwę sieci wifi, oraz `password` na hasło. Po zmianie można przejść do kompilacji.

### Kompilacja

Do wgrania programu polecamy wykorzystać program VSCode z rozszerzeniem platformio. Po złożeniu układu według instrukcji użytkownika, i podłączeniu esp32 do komputera za pomocą kabla usb, należy przejść do zakładki PlatformIO, a następnie wybrać opcję _Upload and Monitor_. Program może zapytać o adres urządzenia - wybieramy odpowiednie. Po wgraniu programu na ekranie wyświetli się adres ip serwera ftp - możemy nawiązać z nim połączenie za pomocą klienta ftp.

### Połączenie

Zalecane jest użycie oprogramowania Filezilla do testowania połączenia z serwerem ftp. Przed połączeniem konieczna jest zmiana liczby jednoczesnych połączeń opisana w instrukcji użytkownika.

## Struktura projektu

### `src/main.cpp`

W tym pliku inicjalizowane są wszystkie funkcje programu - połączenie z wifi, odczyty z sensorów, obsługa karty RFID oraz serwer ftp. Zaimplementowane jest również wykrywanie anomalii - odpowiedzialne są za nie funkcje `void LightThread(void *params)` oraz `void AccThread(void *params)` odpowiednio dla światła i ruchu. Kiedy urządzenie jest w stanie zabezpieczonym, wykrycie anomalii powoduje wyczyszczenie zawartości karty sd.

### `lib/FTPServer/FTPServer.h`

Plik zawiera implementację serwera FTP.

Możliwe stany serwera FTP:
- RESET - resetowanie połączenia - rozpoczęcie pracy od początku
- WAIT_CONNECTION - oczekiwanie na połączenie
- IDLE
- WAIT_USERNAME - oczekiwanie na podanie nazwy użytkownika
- WAIT_PASSWORD - oczekiwanie na podanie hasła
- WAIT_COMMAND  - oczekiwanie na podanie komendy

Zdarzenia obsługiwane są w pętli `void mainFTPLoop()`.

Najważniejszą funkcją serwera jest `boolean processCommand(String command, String params)`, zajmująca się przetwarzaniem komend użytkownika.

Obsługiwane komendy:

- ABOR
- CDUP
- CWD
- DELE
- FEAT
- LIST
- MKD
- MLSD
- MODE
- NOOP
- PASV
- PWD
- QUIT
- RETR
- RMD
- RNFR
- RNTO
- STOR
- STRU
- SYST
- TYPE

Pozwala to wykonywać wszystkie podstawowe operacje na serwerze ftp, pod warunkiem wykorzystywania jednego wątku klienta.

### `lib/MPU6050/MPU6050.h`

Plik jest odpowiedzialnyt za komunikację z modułem MPU6050 i wykrywanie anomalii dotyczących położenia urządzenia. Po uruchomieniu urządzenie jest kalibrowane, a nastepnie w pętli obliczane są zmiany w położeniu. Gdy te będą zbyt duże, funkcja `boolean checkForAnomalies()` zwraca `true`, co powoduje uruchomienie działań niszczących dane na karcie, jesli urządzenie jest w odpowiednim stanie.

### `lib/RFIDReader/RFIDReader.h`

Plik jest odpowiedzialny za komunikację z czytnikiem kart RFID. Zawiera również listę zaufanych kart (legitymacji studenckich i tokenów RFID mogących zmieniać stan urządzenia). Sprawdzenia następują w funkcjach `bool verifyLoop()` oraz `bool isValidID(byte uid[10])`.
