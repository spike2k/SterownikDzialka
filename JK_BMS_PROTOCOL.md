# JK-BMS BD6A24S12P — BLE, ramki i test terenowy

Stan implementacji: 2026-09-05. Kod: `src/drivers/JkBmsBleDriver.*`, `JkBmsProtocol.*`; tester: środowisko PlatformIO `jk_ble_probe`.

## Zakres i źródła

- Docelowy BMS: **JK BD6A24S12P**.
- Transport: **Bluetooth Low Energy (BLE)**, klientem jest ESP32.
- Implementacja jest lekka i natywna dla Arduino-ESP32; nie zawiera ESPHome.
- Podstawą mapy jest projekt [`syssi/esphome-jk-bms`](https://github.com/syssi/esphome-jk-bms), w szczególności jego [dokumentacja BLE](https://github.com/syssi/esphome-jk-bms/blob/main/docs/protocol-design-ble.md) i [decoder `jk_bms_ble.cpp`](https://github.com/syssi/esphome-jk-bms/blob/main/components/jk_bms_ble/jk_bms_ble.cpp).

Projekt źródłowy wymienia egzemplarze BD6A24S12P HW 11.XW / SW 11.36 i 11.42 jako działające po BLE z `JK02_32S`. **HIPOTEZA:** nasz egzemplarz używa tego samego wariantu. Potwierdzeniem będzie dopiero poprawna ramka z rzeczywistego BMS i spójność napięcia pakietu z sumą cel.

## BLE

Główny profil znany ze źródeł:

| Element | UUID | Zastosowanie |
|---|---|---|
| Service | `0xFFE0` | usługa JK-BMS |
| Characteristic | `0xFFE1` | zapis poleceń i notify |

W nowszym module BLE źródła pokazują dwie charakterystyki o tym samym UUID: uchwyt `0x0003` ma zapis, a `0x0005` notify (CCCD `0x0006`). Driver nie polega na numerach uchwytów: wybiera `FFE1` po właściwościach write oraz notify/indicate. W starszym module spotykane są również `FFE2` oraz dodatkowa usługa `F000FFC0-0451-4000-B000-000000000000`; jest to informacja diagnostyczna, a nie potwierdzony profil naszego urządzenia.

Wyszukiwanie działa po skonfigurowanym MAC albo automatycznie po nazwie zawierającej `JK`, `JIKONG` lub `BMS`, reklamowanej usłudze FFE0 oraz znanych prefiksach `20:21:11` / `C8:47:8C`. MAC można wpisać w panelu WWW. Dla probe można ustawić `EMS_JK_BMS_MAC` w lokalnym `secrets.h`; pusty oznacza AUTO.

## Polecenia odczytu

Po subskrypcji notify driver wysyła do **BMS** dwie 20-bajtowe komendy:

- `0x97` — żądanie informacji o urządzeniu (odpowiedź typu `0x03`),
- `0x96` — żądanie ustawień/uruchomienie strumienia statusu (odpowiedzi `0x01`, potem okresowe `0x02`).

Format TX: nagłówek `AA 55 90 EB`, kod w bajcie 4, długość w bajcie 5, dane 6–15, licznik 16, rezerwa 17–18, suma modulo 256 w bajcie 19. Implementacja nie zawiera poleceń zmiany parametrów, MOS ani konfiguracji BMS. Nie dodano też żadnej transmisji Pylontech do falownika. Driver ANENJI nadal wykonuje wyłącznie istniejące odczyty FC03 — bez FC06/FC10.

## Odpowiedzi i składanie ramek

- Początek RX: `55 AA EB 90`.
- Typ: bajt 4 (`0x01` ustawienia, `0x02` dane baterii, `0x03` urządzenie).
- Logiczna długość używanych ramek: **300 bajtów**; CRC/suma jest w bajcie 299.
- Suma kontrolna: suma bajtów 0–298 modulo 256.
- Notify BLE dzieli ramkę na fragmenty zależne od MTU. Składacz wyszukuje nagłówek także przez granicę fragmentów, zbiera dokładnie 300 bajtów, odrzuca zły nagłówek/CRC i synchronizuje się ponownie na kolejnym nagłówku.
- Po 15 s bez poprawnych danych połączenie jest uznawane za nieaktywne; skanowanie i łączenie są automatycznie ponawiane.

Skan jest asynchroniczny, a operacje ustanowienia GATT wykonuje osobne zadanie FreeRTOS. `tick()` nie zawiera długiej pętli ani blokującego oczekiwania, dzięki czemu główna pętla nadal obsługuje Wi-Fi, WWW, MQTT i RS232 ANENJI.

## Rozpoznawanie wariantu

Tryb AUTO ocenia trzy formaty:

- `JK02_24S`: do 24 cel, pola pakietu od offsetu 118;
- `JK02_32S`: do 32 cel, pola za blokiem cel przesunięte o 32 bajty (napięcie pakietu od 150);
- `JK04`: napięcia cel jako 32-bitowe IEEE-754.

Dla JK02 kandydat musi mieć fizycznie sensowne napięcia, SOC i prąd, a napięcie pakietu ma zgadzać się z sumą aktywnych cel. Przy remisie parser zwraca `UNKNOWN`, zamiast zgadywać. Probe pozwala wtedy wpisać `p 24`, `p 32`, `p 04` albo wrócić przez `p auto`.

**HIPOTEZA:** autodetekcja wybierze `JK02_32S` dla BD6A24S12P. Obsługa JK04 w tym etapie służy głównie rozpoznaniu formatu i napięć cel; pełne mapowanie pozostałych pól JK04 nie jest potwierdzone dla docelowego BMS.

## Mapa dekodowanych pól JK02

Offsety w kolumnie 24S; dla oznaczonych pól 32S dodaje `+32`. Wszystkie liczby są little-endian.

| Pole | JK02_24S | JK02_32S | Typ / skala |
|---|---:|---:|---|
| napięcia cel | 6–53 | 6–69 | `uint16 × 0.001 V` |
| napięcie pakietu | 118 | 150 | `uint32 × 0.001 V` |
| prąd | 126 | 158 | `int32 × 0.001 A` |
| temperatura T1/T2 | 130/132 | 162/164 | `int16 × 0.1 °C` |
| alarmy | 136 | 166 | 24S `uint16`, 32S `uint32` |
| stan balansowania | 140 | 172 | `0=OFF`, inne=ON |
| SOC | 141 | 173 | `%` |
| charge MOS | 166 | 198 | `bool` |
| discharge MOS | 167 | 199 | `bool` |
| temperatury T3/T4/T5 | — | 258/256/254 | `int16 × 0.1 °C` |

Moc signed jest liczona jako `packVoltageV × currentA`, ponieważ surowe pole mocy JK02 jest bez znaku. Min, max, delta oraz numery cel są liczone z poprawnych napięć, nie przyjmowane bezkrytycznie z ramki.

## `BatteryData` i przyszły emulator

`src/core/BatteryData.h` jest niezależny od BLE i zawiera napięcie, signed prąd/moc, SOC, 5 temperatur, 32 cele, min/max/delta, numery cel, MOS-y, balansowanie, alarmy, czas aktualizacji, online i wariant protokołu. W przyszłości ten sam model może zasilać driver UART.

`BatteryData -> BatteryEmulator -> PylontechEmulator -> RS485/CAN` jest przygotowany interfejsem `BatteryEmulator`. Obecny `PylontechEmulator` niczego nie wysyła; zachowuje tylko wcześniejszy bierny licznik aktywności RX.

## Test terenowy

1. Wyłącz aplikację telefonu JK-BMS lub rozłącz ją z BMS — wiele modułów przyjmuje tylko jednego klienta BLE.
2. Zbuduj i wgraj probe:

   ```powershell
   C:\Users\rozma\.platformio\penv\Scripts\platformio.exe run -e jk_ble_probe -t upload
   C:\Users\rozma\.platformio\penv\Scripts\platformio.exe device monitor -b 115200
   ```

3. Zachowaj log od `JK-BMS BLE PROBE`. Powinny pojawić się urządzenia `name/MAC/RSSI`, oznaczenie `[JK-BMS?]`, `JK-BMS CONNECTED`, lista services/characteristics, `JK NOTIFY` w HEX, `JK FRAME OK`, informacje urządzenia i czytelny blok `JK BATTERY DATA`.
4. Porównaj PACK, CURRENT, SOC i cele z aplikacją JK. Znak prądu wg źródłowej mapy: dodatni = ładowanie, ujemny = rozładowanie; **do potwierdzenia na naszym BMS**.
5. Jeśli `Protocol: UNKNOWN`, po zebraniu ramek spróbuj kolejno `p 32`, `p 24`; `p 04` tylko gdy dane wyglądają na float/JK04. Nie uznawaj wariantu za potwierdzony, jeśli suma cel, napięcie pakietu i SOC są niespójne.
6. Do analizy wklej cały log od startu do co najmniej dwóch pełnych bloków danych, zwłaszcza: wszystkie linie skanu, services/chars, pierwsze notify HEX, komunikaty CRC, model/HW/SW, wariant AUTO oraz wynik po ewentualnym wymuszeniu.

Po potwierdzeniu probe wgraj główny firmware `esp32dev`. Układ partycji `min_spiffs.csv` jest wymagany, bo BLE + TLS + WWW przekracza domyślną partycję aplikacji; pełny upload zapisuje właściwą tablicę partycji.
