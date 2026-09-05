# Sterownik Działka — ESP32 EMS

Modularne serce systemu zarządzania energią dla działki. Projekt używa PlatformIO i frameworka Arduino. Domyślnie kompiluje się w trybie symulacji, więc do uruchomienia panelu nie są potrzebne JK BMS ani falownik ANENJI.

## Funkcje pierwszej wersji

- dane JK BMS: SOC, napięcie, prąd i napięcia cel (parser do uzupełnienia; bez atrap),
- telemetria ANENJI: PV i obciążenie (parser do uzupełnienia; bez atrap),
- osobna warstwa RS485 pod późniejszą emulację Pylontech,
- konfiguracja Wi-Fi z panelu WWW, zapis w NVS; awaryjny AP `SterownikDzialka-Setup`,
- MQTT przez TLS: publikacja telemetrii, Last Will oraz sterowanie lokalnymi i zdalnymi odbiornikami,
- HTTP: prosty interfejs dla zdalnych urządzeń,
- dziesięć kanałów odbiorników (GPIO lokalnie albo MQTT po identyfikatorze), priorytet, moc (W), tryb Auto/Manual i rozdział nadwyżki PV,
- wejścia stanu (opcjonalne GPIO) przygotowane pod przyszłe czujniki,
- sprzętowy watchdog i fail-safe wyłączający odbiorniki po utracie telemetrii,
- dashboard: duży SOC, kierunek ładowania, przepływ mocy, dryf cel i ikony błędów połączeń.

## BOM centralki

| Element | Ilość | Uwagi |
|---|---:|---|
| ESP32 DevKit | 1 | główny sterownik |
| Izolowany RS485 ↔ TTL 3,3 V | 1 | JK BMS; auto kierunek (TX/RX), pełna izolacja zalecana |
| RS485 ↔ TTL 3,3 V | 1 | osobny konwerter pod Pylontech, auto kierunek |
| MAX3232 3,3 V | 1 | RS232 do ANENJI |
| Moduł przekaźników 4× | 1 | wejścia zgodne z 3,3 V; optoizolacja zalecana |
| Zasilacz 230 V → 5 V, 2–3 A | 1 | izolowane zasilanie centralki |
| Styczniki | wg potrzeb | obowiązkowo dla pomp, grzałek i większych odbiorników |
| Bezpieczniki, obudowa, złączki DIN | wg potrzeb | osobne zabezpieczenie elektroniki i obwodów mocy |
| Skrętka ekranowana | wg potrzeb | połączenia RS485 |

Przekaźniki PCB nie powinny bezpośrednio przełączać dużych obciążeń 230 V. Do takich odbiorników należy użyć prawidłowo dobranych styczników i zabezpieczeń, a montaż sieciowy zlecić osobie z odpowiednimi kwalifikacjami.

## Pinout startowy

| Funkcja | GPIO ESP32 | Uwagi |
|---|---:|---|
| Przekaźniki / odbiorniki 1–4 | 16, 17, 18, 19 | sterowanie lokalne, konfigurowalne z panelu |
| JK RS485 RX/TX | 27, 26 | odczyt BMS, konwerter auto-kierunek |
| ANENJI RS232 RX/TX | 33, 32 | MAX3232 do falownika |
| Pylontech RS485 RX/TX | 22, 23 | emulacja / komunikacja, auto-kierunek |
| Wejścia stanu 1–4 | wyłączone (−1) | odczyt stanów; np. 4, 13, 14, 15 z pull-up |

Piny można zmienić w panelu WWW (koło zębate). Zmiana UART wymaga restartu ESP. GPIO 6–11 zajmuje flash, 34–39 są tylko wejściami. Moduł przekaźników jest domyślnie aktywny stanem niskim.

Kanał odbiornika jest aktywny, gdy ma GPIO ≥ 0 albo wypełniony identyfikator MQTT (np. `fontanna`). Oba puste = slot wyłączony. W Auto nadwyżka `pvW − loadW + moc lokalnych ON − rezerwa` jest rozdzielana od priorytetu 1 w dół; kanał z mocą 0 W jest pomijany. Histereza i minimalny czas przełączenia ograniczają cykanie styków. Satelita MQTT nasłuchuje `ems/sterownik-dzialka/load/{id}/set` oraz `ems/sterownik-dzialka/status` i gaśnie przy `offline`.

## Konfiguracja

1. Opcjonalnie skopiuj `include/secrets.example.h` do `include/secrets.h` — to tylko wartości startowe przy pierwszym uruchomieniu.
2. Po starcie wejdź w panel (IP w monitorze szeregowym albo `http://192.168.4.1` w trybie AP) i w konfiguracji wpisz SSID oraz hasło Wi-Fi.
3. Ustawienia lądują w NVS i przeżywają restart; `secrets.h` nie jest potrzebny na co dzień.
4. Nie dodawaj `secrets.h` do repozytorium — plik jest ignorowany przez Git.

Dla brokera projektu ustaw host `mqtt.ele365.eu`, port `8883` i konto `esp32_dzialka`. Firmware ufa zakotwiczeniu ISRG Root X1, sprawdza nazwę hosta i ważność certyfikatu po synchronizacji czasu NTP. Hasło urządzenia wpisz w panelu WWW albo lokalnym `include/secrets.h`; nigdy w pliku śledzonym przez Git.

Bez SSID ESP uruchamia otwarty punkt dostępowy `SterownikDzialka-Setup`. Jeśli sieć domowa nie wstanie w 25 s, AP wraca jako awaryjne wejście do panelu.

## Kompilacja i uruchomienie

W Cursor/VS Code zainstaluj rozszerzenie PlatformIO IDE i otwórz katalog projektu. Z terminala (wariant działający również wtedy, gdy `pio` nie jest dodane do `PATH`):

```powershell
python -m platformio run
python -m platformio run -t upload
python -m platformio device monitor
```

Polecenie `upload` uruchamiaj dopiero po podłączeniu właściwego ESP32 i sprawdzeniu pinów. Domyślnie `EMS_SIMULATION=0` — panel pokazuje błędy połączeń (BMS, falownik, Pylontech), a nie sztuczne wartości. Parsery protokołów są w `src/drivers`.

### Jednorazowy test protokołu ANENJI

Środowisko `anenji_probe` wgrywa samodzielny tester RS232 zamiast normalnego sterownika. Tester mówi wyłącznie **Modbus RTU 9600 8N1, slave 1, FC03** (protokół potwierdzony podsłuchem dongla). Nie wysyła FC06/FC10. Odpowiedzi to HEX plus klasyfikacja `[OK]` / `[BAJTY]` / `[ECHO]` / `[CISZA]`.

Szczegółowa procedura dla konwertera z żeńskim DB9 i męskiego adaptera śrubowego znajduje się w [`TEST_MAX3232_DB9.md`](TEST_MAX3232_DB9.md). Obejmuje kontrolę pinów multimetrem, identyfikację rzeczywistego wyjścia po napięciu ujemnym, loopback samego konwertera i całego kabla oraz bezpieczne podłączenie falownika.

1. Wypnij fabryczny WiFi Plug Pro — podczas testu nie może być drugim urządzeniem nadrzędnym na RS232.
2. Połącz falownik z ESP32 przez MAX3232 3,3 V. Kierunki: wyjście falownika (wg dotychczasowych pomiarów pin 1 RJ45) → `R1IN`, wejście falownika (pin 2 RJ45) ← `T1OUT`, pin 8 = GND. TTL: `R1OUT` → GPIO 33, GPIO 32 → `T1IN`. Przy nowym kablu potwierdź kierunki napięciem spoczynkowym zgodnie z `TEST_MAX3232_DB9.md`; nie zamieniaj pinów w ciemno. Oznaczenia RX/TX producenta bywają z perspektywy kabla/dongla.
3. Zbuduj i wgraj tester:

```powershell
python -m platformio run -e anenji_probe
python -m platformio run -e anenji_probe -t upload
python -m platformio device monitor -b 115200
```

Po starcie tester czeka i niczego nie nadaje. Komendy:

- **`l`** — test toru ESP ↔ MAX3232 ↔ kabel. Falownik i dongle wypięte. Uruchom dwa razy: **A.** zewrzyj `T1OUT`–`R1IN` na module (chip); **B.** wtyk RJ45 w MAX3232, na drugim końcu zewrzyj pin 1 z pinem 2 (kabel). Tylko 9600 i stały tor GPIO 33/32. Wynik ma być `WYNIK LOOPBACK: OK`; pamiętaj, że loopback sprawdza ciągłość, ale nie kierunki TX/RX.
- **`r`** — pełny test z falownikiem na jedynym poprawnym torze `RX=GPIO33`, `TX=GPIO32`, bez inwersji; następnie warianty danych bez wakeup, z wakeup `01 AA` i cykl dump. UART uruchamia się już przy starcie testera, aby GPIO32 nie pływało, a `T1OUT` MAX3232 pozostawało ujemne w spoczynku.
- **`d`** — klon dongla: ten sam cykl FC03 co w sniffie, 3 obroty, bez wakeup. Przy OK wypisuje `DEKOD` (PV/load).
- **`9`** — bierny podsłuch obu pól TTL dongla @ 9600 z parserem FC03. Dongle w falowniku, **bez MAX3232**, tylko GND + pad TX→GPIO33 + pad RX→GPIO32; nie łącz 3.3 V ani `DL`.

Skopiuj cały blok `PODSUMOWANIE` oraz `DIAGNOZA`, w tym ewentualną linię `UART ERR`. Sukces = `[OK]` i linia `DEKOD`. Cisza wszędzie → najpierw `l`, a potem sprawdzenie rzeczywistych kierunków RS232 miernikiem; loopback nie wykrywa zamiany TX/RX.

Po diagnostyce normalny firmware przywraca:

```powershell
python -m platformio run -e esp32dev -t upload
```

## MQTT i API

- telemetria retained: `ems/sterownik-dzialka/state`; częste próbki korzystają z QoS 0,
- dostępność retained: `ems/sterownik-dzialka/status`; `online` po połączeniu i Last Will `offline` z QoS 1,
- odbiorniki MQTT (Auto i Manual): `ems/sterownik-dzialka/load/{id}/set`, payload `ON` lub `OFF`, bez retain; ponowienie co 30 s,
- sterowanie lokalnym GPIO w trybie Manual: `ems/sterownik-dzialka/relay/0/set` do `/9/set`, payload `ON` lub `OFF`, subskrypcja QoS 1 i bez retain,
- polecenia do satelitów (ręczny bypass): `ems/remote/{device}/set`, bez retain,
- stan panelu: `GET /api/state`,
- ustawienia: `GET /api/settings`, `POST /api/settings`,
- restart: `POST /api/reboot`,
- tryb: `POST /api/mode?value=auto|manual`,
- odbiornik: `POST /api/relay?id=0&state=1`,
- urządzenie MQTT: `POST /api/remote?device=fontanna&state=1`,
- urządzenie HTTP: `POST /api/remote?url=http://adres/endpoint&state=1`.

Interfejs JSON i wyraźnie oddzielone moduły pozwalają później dołożyć chmurę, uwierzytelnione API oraz aplikację Ionic bez przebudowy sterowników sprzętowych.

## Struktura

```text
include/             konfiguracja i lokalne sekrety
src/core/            model danych i logika przekaźników
src/drivers/         JK BMS, ANENJI, Pylontech
src/services/        Wi-Fi, MQTT, zdalne HTTP
src/web/             lokalny panel i REST API
src/main.cpp         składanie modułów i główna pętla
```
