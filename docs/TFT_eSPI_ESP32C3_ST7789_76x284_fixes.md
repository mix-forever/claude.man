# TFT_eSPI + ST7789 76×284 na ESP32-C3 SuperMini — kompletny przewodnik poprawek

Dokument opisuje wszystkie wymagane zmiany, żeby biblioteka TFT_eSPI v2.5.43 poprawnie
obsługiwała wyświetlacz ST7789 o fizycznej rozdzielczości 76×284 px na module ESP32-C3 SuperMini,
w środowisku PlatformIO z platformą `espressif32@6.9.0` (ESP-IDF 5.5.2).

Każda poprawka jest osobna, ma konkretne uzasadnienie i dokładną instrukcję gdzie i co zmienić.

---

## Spis treści

1. [Środowisko i wersje](#1-środowisko-i-wersje)
2. [Schemat połączeń](#2-schemat-połączeń)
3. [platformio.ini — pułapki kompilacji i uploadu](#3-platformioini--pułapki-kompilacji-i-uploadu)
4. [Poprawka 1 — SPI_PORT dla IDF 5.x](#4-poprawka-1--spi_port-dla-idf-5x)
5. [Poprawka 2 — TFT_MISO na ESP32-C3](#5-poprawka-2--tft_miso-na-esp32-c3)
6. [Poprawka 3 — CGRAM offset dla panelu 76×284](#6-poprawka-3--cgram-offset-dla-panelu-76284)
7. [User_Setup.h — kompletna konfiguracja](#7-user_setuph--kompletna-konfiguracja)
8. [Podświetlenie BL — dlaczego tranzystor NPN](#8-podświetlenie-bl--dlaczego-tranzystor-npn)
9. [Kolejność inicjalizacji w kodzie](#9-kolejność-inicjalizacji-w-kodzie)
10. [Czego NIE robić — lista antypatternów](#10-czego-nie-robić--lista-antypatternów)
11. [Diagnostyka — co zrobić gdy ekran nie działa](#11-diagnostyka--co-zrobić-gdy-ekran-nie-działa)

---

## 1. Środowisko i wersje

| Składnik | Wersja / wartość |
|---|---|
| Platforma PlatformIO | `espressif32 @ 6.9.0` |
| ESP-IDF (wbudowany) | **5.5.2** |
| Arduino ESP32 core | 2.x (bundled z espressif32@6.9.0) |
| TFT_eSPI | `bodmer/TFT_eSPI @ ^2.5.43` |
| Board | `esp32-c3-devkitm-1` |
| MCU | ESP32-C3, 160 MHz, RISC-V |

> **Ważne:** Wersja ESP-IDF ma krytyczne znaczenie dla poprawki #1.
> Nowe platformy (espressif32 ≥ 6.x) używają IDF 5.x, w którym zmieniło się
> adresowanie rejestrów SPI. TFT_eSPI 2.5.43 nie uwzględnia tej zmiany.

---

## 2. Schemat połączeń

```
ESP32-C3 SuperMini          ST7789 76×284
─────────────────────────────────────────
GPIO 6  (MOSI/SDA)  ───────  SDA
GPIO 4  (SCLK/SCL)  ───────  SCL
GPIO 5  (CS)        ───────  CS
GPIO 7  (DC/RS)     ───────  DC
GPIO 8  (RST)       ───────  RST
GPIO 10 (BL ctrl)   ──┐
                      R 1kΩ
                      NPN base      ← patrz sekcja 8
GND                 ───────  GND
3.3V                ───────  VCC
```

### Dostępne GPIO na ESP32-C3 SuperMini

ESP32-C3 SuperMini ma tylko GPIO 0–10 i GPIO 18–21 wyprowadzone na złączu.

| GPIO | Uwagi |
|---|---|
| 0 | Strapping — nie używać jako wyjście przy boot |
| 2 | Wolny, ale ADC |
| 3 | Wolny — użyty jako TFT_MISO (fikcyjny, patrz poprawka #2) |
| 4 | TFT_SCLK ✓ |
| 5 | TFT_CS ✓ |
| 6 | TFT_MOSI ✓ |
| 7 | TFT_DC ✓ |
| 8 | TFT_RST — strapping pin, ale bezpieczny po boot |
| 9 | **BOOT button** — nie używać; pull-up do VCC, przytrzymanie = tryb flash |
| 10 | TFT_BL (przez tranzystor) ✓ |
| 18 | USB D- — nie używać |
| 19 | USB D+ — nie używać |
| 20 | UART0 RX |
| 21 | UART0 TX |

---

## 3. platformio.ini — pułapki kompilacji i uploadu

Kompletny, działający plik:

```ini
[env:esp32-c3-supermini]
platform               = espressif32@6.9.0
board                  = esp32-c3-devkitm-1
framework              = arduino

board_build.mcu        = esp32c3
board_build.f_cpu      = 160000000L
board_build.f_flash    = 80000000L
board_build.flash_mode = qio

build_flags =
    -DCORE_DEBUG_LEVEL=0

lib_deps =
    bodmer/TFT_eSPI @ ^2.5.43

monitor_speed  = 115200
monitor_port   = /dev/ttyACM0
upload_port    = /dev/ttyACM0
upload_speed   = 115200
upload_flags   = --no-stub
```

### Dlaczego `upload_flags = --no-stub`

ESP32-C3 SuperMini używa natywnego USB CDC (bezpośrednio z MCU, bez konwertera UART-USB).
Standardowy esptool zmienia prędkość transmisji przed flashowaniem — to powoduje rozłączenie
portu USB i upload się zawiesza. Flaga `--no-stub` pomija ten krok i trzyma się prędkości 115200 bps.

### Dlaczego `upload_speed = 115200`

Z tych samych powodów co powyżej. Wyższe prędkości powodują, że esptool wysyła komendę
zmiany prędkości, USB CDC się rozłącza, upload pada.

### Czego NIE dodawać do `build_flags`

| Flaga | Problem |
|---|---|
| `-DARDUINO_USB_CDC_ON_BOOT=1` | Remapuje `Serial` na HWCDC, ale `HardwareSerial.cpp` nadal próbuje użyć starego `Serial` → błąd kompilacji |
| `-DSMOOTH_FONT=1` | `Smooth_font.cpp` w TFT_eSPI używa `Serial` → ten sam problem |
| `-DUSER_SETUP_LOADED=1` | Pomija `User_Setup_Select.h`, ale konfiguracja musi być kompletna w `build_flags` — łatwo o pominięcie defineów. Bezpieczniejsze jest edytowanie `User_Setup.h` w katalogu libdeps bezpośrednio. |

### Dlaczego `platform = espressif32@6.9.0` a nie `platform = espressif32`

Bez wersji PlatformIO może pobrać fork `pioarduino`, który szuka skryptu `pioarduino-build.py`
i przerywa build z błędem "build script not found". Pinowanie do `@6.9.0` gwarantuje oryginalny,
stabilny pakiet.

---

## 4. Poprawka 1 — SPI_PORT dla IDF 5.x

### Problem

To jest **najważniejsza poprawka** — bez niej SPI nie wysyła żadnych danych do wyświetlacza,
mimo że kod się kompiluje i uruchamia bez błędów.

TFT_eSPI używa bezpośredniego dostępu do rejestrów sprzętowych SPI, żeby uniknąć narzutu
funkcji Arduino. Adresy tych rejestrów wylicza przez makro `REG_SPI_BASE(i)`.

W **IDF 5.x** to makro jest zdefiniowane jako:

```c
// soc/esp32c3/include/soc/soc.h w IDF 5.5.2:
#define REG_SPI_BASE(i)  (((i)==2) ? (DR_REG_SPI2_BASE) : (0))
```

Makro zwraca poprawny adres `0x60024000` **tylko gdy `i == 2`**.
Dla każdej innej wartości zwraca `0`.

TFT_eSPI definiuje:

```c
// TFT_eSPI_ESP32_C3.h:
#define SPI_PORT SPI2_HOST   // SPI2_HOST = 1 (z driver/spi_common.h)
```

Wyliczenie adresów rejestrów:

```c
// TFT_eSPI_ESP32_C3.c — zmienne globalne:
volatile uint32_t* _spi_cmd       = (volatile uint32_t*)(SPI_CMD_REG(SPI_PORT));
volatile uint32_t* _spi_mosi_dlen = (volatile uint32_t*)(SPI_MOSI_DLEN_REG(SPI_PORT));
volatile uint32_t* _spi_w         = (volatile uint32_t*)(SPI_W0_REG(SPI_PORT));
```

Z `SPI_PORT = 1`:

```c
SPI_CMD_REG(1)      = REG_SPI_BASE(1) + 0x00 = 0 + 0x00 = 0x0000_0000
SPI_MOSI_DLEN_REG(1)= REG_SPI_BASE(1) + 0x1C = 0 + 0x1C = 0x0000_001C
SPI_W0_REG(1)       = REG_SPI_BASE(1) + 0x98 = 0 + 0x98 = 0x0000_0098
```

Wskaźniki `_spi_cmd`, `_spi_mosi_dlen`, `_spi_w` wskazują na adresy `0x00`, `0x1C`, `0x98` —
czyli w przestrzeni pamięci podręcznej (cache bus), nie w rejestrach SPI2.
Każde makro `TFT_WRITE_BITS` zapisuje dane pod złe adresy. SPI hardware nigdy nie dostaje rozkazu
wysyłki. Wyświetlacz nie otrzymuje żadnych danych.

Symptomy:
- Ekran czarny lub biały (stan domyślny po zasileniu panelu ST7789)
- `tft.init()` "działa" (nie crashuje), ale wyświetlacz nie jest inicjalizowany
- Podświetlenie działa (sterowane GPIO, nie SPI)

### Poprawka

Plik: `.pio/libdeps/esp32-c3-supermini/TFT_eSPI/Processors/TFT_eSPI_ESP32_C3.h`

Znajdź linię (~71):

```cpp
// ESP32 specific SPI port selection - only SPI2_HOST available on C3
#define SPI_PORT SPI2_HOST
```

Zmień na:

```cpp
// IDF 5.x: REG_SPI_BASE(i) zwraca DR_REG_SPI2_BASE tylko gdy i==2.
// SPI2_HOST enum = 1, ale indeks rejestru dla SPI2 = 2.
#define SPI_PORT 2
```

Po tej zmianie:

```c
SPI_CMD_REG(2)      = REG_SPI_BASE(2) + 0x00 = 0x60024000  ✓
SPI_MOSI_DLEN_REG(2)= REG_SPI_BASE(2) + 0x1C = 0x6002401C  ✓
SPI_W0_REG(2)       = REG_SPI_BASE(2) + 0x98 = 0x60024098  ✓
```

Wskaźniki trafiają w faktyczne rejestry hardware SPI2 na ESP32-C3.

> **Uwaga:** Ta poprawka dotyczy pliku w katalogu `libdeps`, który jest generowany przez
> PlatformIO przy pierwszej kompilacji. Przy czystym `pio run --target clean` + reinstalacji
> bibliotek, plik zostanie nadpisany i poprawkę trzeba nanieść ponownie.
> Aby temu zapobiec, skopiuj bibliotekę do `lib/` w projekcie zamiast polegać na `libdeps`.

---

## 5. Poprawka 2 — TFT_MISO na ESP32-C3

### Problem

Wyświetlacz ST7789 jest write-only (brak linii MISO w interfejsie SPI).
Gdy nie zdefiniujesz `TFT_MISO` w `User_Setup.h`, TFT_eSPI ma osobną ścieżkę dla ESP32-C3.

W `TFT_eSPI_ESP32_C3.h`:

```cpp
#ifndef TFT_MISO
  #define TFT_MISO -1       // najpierw ustawia -1
#endif

// ...

#if defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32S2)
  #if (TFT_MISO == -1)
    #undef TFT_MISO
    #define TFT_MISO TFT_MOSI   // ← przypisuje MISO = MOSI = GPIO6
  #endif
#endif
```

Efekt: GPIO6 jest jednocześnie wyjściem MOSI i wejściem MISO. Arduino SPI skonfiguruje ten pin
w trybie INPUT/OUTPUT jednocześnie (half-duplex). Może to powodować konflikty bus-drive i
niestabilne zapisy, szczególnie przy dłuższych transferach.

### Poprawka

W pliku `User_Setup.h` zdefiniuj fikcyjny, nieużywany pin jako MISO:

```cpp
#define TFT_MISO  3   // GPIO3 — wolny pin, wyświetlacz nie ma MISO
```

GPIO3 jest skonfigurowany jako wejście (co jest poprawne dla MISO) i nie jest podłączony
do niczego. Linia MISO SPI pływa, ale ponieważ biblioteka nigdy nie czyta z wyświetlacza,
nie ma to żadnego znaczenia. GPIO6 pozostaje czystym wyjściem MOSI.

---

## 6. Poprawka 3 — CGRAM offset dla panelu 76×284

### Problem

Kontroler ST7789 ma wbudowane RAM na matrycę **240×320 pikseli**.
Fizyczny panel 76×284 jest przymocowany do narożnika tej matrycy, ale nie narożnika 0,0.
Żeby piksel (0,0) logicznie (lewy górny róg panelu) trafił na właściwą komórkę RAM kontrolera,
trzeba dodać offset do każdego polecenia CASET/RASET.

Dla panelu 76×284 w obróceniu 1 (landscape, logicznie 284×76):

```
CASET: startCol + x = 18 + x
RASET: startRow + y = 82 + y
```

Czyli pełny ekran w landscape to: CASET 18..301, RASET 82..157.

TFT_eSPI przechowuje te offsety w zmiennych `colstart` i `rowstart`, ustawianych
w funkcji `setRotation()`. Kod tej funkcji dla ST7789 jest w:

```
TFT_eSPI/TFT_Drivers/ST7789_Rotation.h
```

Dla paneli o `_init_width == 135` i kilku innych rozmiarach wpisy już istnieją.
Dla 76 — nie ma.

### Poprawka

Plik: `.pio/libdeps/esp32-c3-supermini/TFT_eSPI/TFT_Drivers/ST7789_Rotation.h`

Znajdź **case 1** (landscape) i dodaj blok `else if` przed domyślnym `else`:

```cpp
case 1:   // Landscape (Portrait + 90 stopni)
  // ... istniejące wpisy dla 135, 172, 170 ...
  else if(_init_width == 76)      // ← DODAJ
  {
      colstart = 18;
      rowstart = 82;
  }
  else
  {
      colstart = 0;
      rowstart = 0;
  }
```

To samo dodaj w **case 3** (landscape inverted):

```cpp
case 3:   // Inverted landscape
  // ... istniejące wpisy ...
  else if(_init_width == 76)      // ← DODAJ
  {
      colstart = 18;
      rowstart = 82;
  }
  else
  {
      colstart = 0;
      rowstart = 0;
  }
```

### Skąd wziąć wartości offsetu

Offset dla konkretnego panelu możesz znaleźć jedną z metod:

**a) Bit-bang SPI** — napisz własną implementację SPI w software, ręcznie wyślij do wyświetlacza
polecenia CASET/RASET z różnymi wartościami i obserwuj gdzie pojawia się kolor.
Zacznij od `colstart = 0, rowstart = 0` i zwiększaj, aż obraz trafi na panel.

**b) Dokumentacja modułu** — producent modułu czasem podaje offset w datasheet.
Dla typowych paneli ST7789:

| Panel | colstart (rot 1/3) | rowstart (rot 1/3) |
|---|---|---|
| 135×240 | 40 | 53 |
| 170×320 | 0 | 35 |
| 172×320 | 0 | 34 |
| **76×284** | **18** | **82** |
| 240×320 | 0 | 0 |

**c) Reverse engineering** — wyślij jeden piksel na (0,0) i patrz gdzie pojawia się na ekranie.
Mierz offset w pikselach.

### Wymagane definicje w User_Setup.h

Oprócz offsetu w kodzie rotacji, wymagane są dwa dodatkowe define'y:

```cpp
#define CGRAM_OFFSET     // włącza stosowanie colstart/rowstart w setAddrWindow()
```

Bez `CGRAM_OFFSET` funkcja `setAddrWindow()` ignoruje `colstart` i `rowstart`, nawet jeśli są
ustawione. Adresowanie zawsze zaczyna się od 0, obraz trafia poza panel.

---

## 7. User_Setup.h — kompletna konfiguracja

Biblioteka TFT_eSPI szuka konfiguracji w kilku miejscach.
**Rekomendowane podejście**: edytuj bezpośrednio `User_Setup.h` w katalogu libdeps:

```
.pio/libdeps/esp32-c3-supermini/TFT_eSPI/User_Setup.h
```

Kompletna, działająca zawartość pliku:

```cpp
// ST7789P3 76x284 — ESP32-C3 SuperMini

#define USER_SETUP_INFO "ST7789P3 76x284 ESP32-C3"

// ── Sterownik ─────────────────────────────────────────────────────────────────
#define ST7789_DRIVER

// ── Rozdzielczość (portrait-base, rotacja ustawiana w kodzie) ─────────────────
#define TFT_WIDTH   76     // fizyczna szerokość (krótszy bok)
#define TFT_HEIGHT  284    // fizyczna wysokość (dłuższy bok)
#define CGRAM_OFFSET       // wymagane dla małych paneli ST7789 — włącza offset CASET/RASET

// ── Kolejność kolorów ─────────────────────────────────────────────────────────
#define TFT_RGB_ORDER TFT_BGR   // ten panel używa BGR zamiast RGB
#define TFT_INVERSION_ON        // ten panel wymaga inwersji kolorów (INVON zamiast INVOFF)

// ── Piny SPI — ESP32-C3 SuperMini ────────────────────────────────────────────
#define TFT_MOSI  6    // SDA  — dane do wyświetlacza
#define TFT_MISO  3    // fikcyjny MISO (wyświetlacz jest write-only; patrz poprawka #2)
#define TFT_SCLK  4    // SCL  — zegar SPI
#define TFT_CS    5    // CS   — chip select
#define TFT_DC    7    // DC   — data/command
#define TFT_RST   8    // RST  — hardware reset
// BL sterowane ręcznie na GPIO10 przez tranzystor NPN

// ── Prędkość SPI ──────────────────────────────────────────────────────────────
#define SPI_FREQUENCY  27000000   // 27 MHz — stabilne z GPIO matrix routing

// ── Czcionki ──────────────────────────────────────────────────────────────────
#define LOAD_GLCD    // czcionka 5x7 (wbudowana, zawsze ładowana)
#define LOAD_FONT2   // czcionka 16px
#define LOAD_FONT4   // czcionka 26px
#define LOAD_FONT6   // czcionka 48px (tylko cyfry)
#define LOAD_FONT7   // czcionka 48px 7-segmentowa
#define LOAD_FONT8   // czcionka 75px (tylko cyfry)
#define LOAD_GFXFF   // obsługa czcionek Adafruit GFX
// NIE dodawaj SMOOTH_FONT — Smooth_font.cpp używa Serial, co powoduje konflikt na ESP32-C3
```

### Dlaczego TFT_RGB_ORDER TFT_BGR

Kontroler ST7789 może być skonfigurowany fabrycznie przez producenta modułu do pracy
w trybie BGR lub RGB. Panel 76×284 (wariant P3) używa BGR. Bez tego define'a czerwony
kolor (`TFT_RED = 0xF800`) wyświetli się jako niebieski, niebieski jako czerwony.

### Dlaczego TFT_INVERSION_ON

Niektóre panele ST7789 mają odwrócone działanie pikseli — komórka z wartością 0x0000
(czarny) świeci jako biały i odwrotnie. Definicja `TFT_INVERSION_ON` powoduje, że
biblioteka wysyła polecenie `INVON (0x21)` zamiast `INVOFF (0x20)` podczas inicjalizacji.

Jak sprawdzić która wartość jest prawidłowa: wywołaj `tft.fillScreen(TFT_BLACK)`.
Jeśli ekran jest biały — potrzebujesz `TFT_INVERSION_ON`. Jeśli czarny — `TFT_INVERSION_OFF`.

---

## 8. Podświetlenie BL — dlaczego tranzystor NPN

### Problem

Pin BL (backlight) wyświetlacza ST7789 **wymaga podłączenia do GND** żeby podświetlenie działało.
Nie jest to typowy pin aktywny-wysoko — jest podłączony do katody diod LED podświetlenia.

Bezpośrednie podłączenie GPIO ESP32 do BL jest problematyczne:
- GPIO ESP32 w stanie HIGH daje 3.3V, ale BL potrzebuje drogi do GND
- Bezpośrednie podłączenie GND → BL → zawsze włączone, bez sterowania

### Rozwiązanie: tranzystor NPN

```
ESP32 GPIO10 ──┤1kΩ├──► baza NPN (np. BC547, 2N2222, S8050)
                          emiter ──► GND
                          kolektor ──► BL pin wyświetlacza
```

Działanie:
- GPIO10 = HIGH (3.3V): przez rezystor 1kΩ płynie prąd do bazy → tranzystor się otwiera →
  kolektor zwiera BL do GND → podświetlenie **WŁĄCZONE**
- GPIO10 = LOW (0V): baza nie jest polaryzowana → tranzystor zamknięty →
  BL nie ma drogi do GND → podświetlenie **WYŁĄCZONE**

### Dlaczego nie GPIO9 dla BL

GPIO9 to przycisk BOOT na ESP32-C3 SuperMini. Ma zewnętrzny pull-up do 3.3V.
Gdybyś użył GPIO9 jako bazy tranzystora NPN (z rezystorem 1kΩ), prąd przez rezystor
przy HIGH=3.3V = (3.3 - 0.7V) / 1kΩ = 2.6mA — wystarczający do otwarcia tranzystora.
Ale przy starcie, prąd przez rezystor z pull-up = ~2.6mA do bazy → tranzystor się otwiera
i ściąga GPIO9 przez kolektor-emiter do GND. Strapping pin GPIO9 musi być HIGH podczas boot —
jeśli tranzystor go ściąga do GND, urządzenie wchodzi w tryb flash i nie bootuje normalnie.

### Inicjalizacja BL w kodzie

Podświetlenie **musi być włączone PRZED** wywołaniem `tft.init()`:

```cpp
void displayInit() {
    pinMode(PIN_TFT_BL, OUTPUT);
    displayBacklight(true);   // ← NAJPIERW BL
    tft.init();               // ← POTEM init
    tft.setRotation(DISPLAY_ROTATION);
    tft.fillScreen(TFT_BLACK);
}
```

Jeśli BL włączysz po `tft.init()`, panel może nie zainicjalizować się poprawnie —
logika wewnętrzna niektórych sterowników ST7789 czeka na stabilne zasilanie podświetlenia.

---

## 9. Kolejność inicjalizacji w kodzie

Prawidłowa kolejność operacji, której należy przestrzegać:

```cpp
void displayInit() {
    // 1. Skonfiguruj i włącz podświetlenie
    pinMode(PIN_TFT_BL, OUTPUT);
    displayBacklight(true);

    // 2. Inicjalizuj kontroler (wysyła komendy SLPOUT, COLMOD, MADCTL, INVON/OFF, DISPON)
    tft.init();

    // 3. Ustaw rotację — to ustawia colstart/rowstart (offset CGRAM)
    tft.setRotation(DISPLAY_ROTATION);  // 1 = landscape (284×76)

    // 4. Wyczyść ekran
    tft.fillScreen(TFT_BLACK);

    // 5. (opcjonalnie) Utwórz sprite do bufferowanego rysowania
    canvas.createSprite(DISPLAY_WIDTH, DISPLAY_HEIGHT);
    canvas.setColorDepth(16);
}
```

Rotacja **musi** być ustawiona przed rysowaniem, bo `setRotation()` ustawia `_xstart` i `_ystart`
(colstart/rowstart). Każde wywołanie `setAddrWindow()` (które jest podstawą `fillScreen`, `drawRect`
itp.) używa tych wartości do przeliczenia adresu w RAM kontrolera.

---

## 10. Czego NIE robić — lista antypatternów

| Co | Dlaczego nie |
|---|---|
| `#define SPI_PORT SPI2_HOST` w IDF 5.x | `SPI2_HOST = 1`, `REG_SPI_BASE(1) = 0` → adresy SPI = 0x00 → SPI nie wysyła danych |
| Brak `#define TFT_MISO` na ESP32-C3 | Biblioteka przypisuje `TFT_MISO = TFT_MOSI = GPIO6`, GPIO6 staje się jednocześnie IN i OUT |
| Brak `CGRAM_OFFSET` | `setAddrWindow()` ignoruje offset, obraz poza panelem |
| Brak `TFT_INVERSION_ON` | Kolory odwrócone — czarny = biały, czerwony = cyan itp. |
| Brak `TFT_RGB_ORDER TFT_BGR` | Kanały R i B zamienione — czerwony wygląda jak niebieski |
| `TFT_INVERSION_OFF` + `TFT_BGR` (konfiguracja projektu Amplifier na WROOM-32) | WROOM-32 działa, bo ma inną wersję panelu lub inne VCOM. Na ESP32-C3 z tym panelem wymagane INVERSION_ON |
| `#define SMOOTH_FONT` lub `-DSMOOTH_FONT=1` | `Smooth_font.cpp` używa `Serial` — błąd kompilacji na ESP32-C3 bez CDC |
| `-DARDUINO_USB_CDC_ON_BOOT=1` | Konflikty z `HardwareSerial` — błąd kompilacji |
| GPIO9 dla podświetlenia | Pull-up + tranzystor ściąga pin strapping → tryb flash przy boot |
| GPIO15, GPIO16, GPIO17... | Nie istnieją na ESP32-C3 SuperMini |
| `upload_speed = 921600` | CDC rozłącza się przy zmianie baud → upload zawiesza się |
| Brak `--no-stub` | Esptool wysyła polecenie zmiany baud → CDC rozłącza się → upload pada |

---

## 11. Diagnostyka — co zrobić gdy ekran nie działa

### Ekran czarny, podświetlenie nie świeci

Sprawdź układ tranzystora. Zmierz napięcie na kolektor-emiter przy GPIO=HIGH.
Sprawdź czy rezystor bazy ma ~1kΩ.

### Podświetlenie świeci, ekran czarny

Objawy: panel świeci (biały lub szary kolor), ale żadne kolory się nie pojawiają.

Prawdopodobna przyczyna: brak poprawki SPI_PORT (sekcja 4) lub błędna poprawka CGRAM.

Test: wywołaj `tft.fillScreen(TFT_RED)` i sprawdź czy ekran zmienia kolor.
Jeśli nie — SPI nie działa (poprawka #1). Jeśli zmienia, ale obraz jest przesunięty — problem z offsetem.

### Ekran biały (cały biały bez obrazu)

Dwa możliwe powody:
1. `TFT_INVERSION_ON` zamiast `TFT_INVERSION_OFF` przy prawidłowo działającym SPI,
   a `fillScreen(TFT_BLACK)` wyświetla białe piksele — to normalne z inwersją. Sprawdź `fillScreen(TFT_WHITE)` — powinien pokazać czarny.
2. Zepsuty/niezainicjalizowany panel — sprawdź połączenie RST.

### Obrazy poza ekranem / obraz ucięty

Brak CGRAM offset (poprawka #3) lub zły offset. Sprawdź że:
- `CGRAM_OFFSET` jest zdefiniowane w `User_Setup.h`
- W `ST7789_Rotation.h` jest blok dla `_init_width == 76`
- `colstart = 18`, `rowstart = 82` dla rotacji 1 i 3

### Kolory odwrócone (czerwony = cyan, niebieski = żółty)

Brak `TFT_INVERSION_ON` lub błędna wartość `TFT_RGB_ORDER`.

### Upload się zawiesza

Sprawdź `upload_speed = 115200` i `upload_flags = --no-stub` w platformio.ini.

### Kompilacja: "error: 'Serial' was not declared"

Usuń `-DARDUINO_USB_CDC_ON_BOOT=1` i `-DSMOOTH_FONT=1` z `build_flags`.

### "build script not found" podczas inicjalizacji platformy

Zmień `platform = espressif32` na `platform = espressif32@6.9.0`.

---

*Dokument oparty na praktycznym debugowaniu projektu ai_session_limits,
ESP32-C3 SuperMini + ST7789 76×284, TFT_eSPI 2.5.43, IDF 5.5.2.*
