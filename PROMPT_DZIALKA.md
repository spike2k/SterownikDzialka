# Kontynuacja diagnostyki ANENJI na działce

Ten plik służy do rozpoczęcia nowej rozmowy z asystentem. Po sklonowaniu / `git pull` przeczytaj najpierw **`ANENJI_PROTOKOL.md`** (protokół, ramki HEX, mapa rejestrów, stan w terenie). Potem wklej prompt z sekcji poniżej.

## Pobranie projektu

Zmiany muszą być w commicie na GitHub. Następnie:

```bash
git clone https://github.com/spike2k/SterownikDzialka.git
cd SterownikDzialka
```

Jeżeli repozytorium jest już sklonowane: `git pull`.

PIO na Ubuntu często jest tu, nie w PATH Pythona:

```bash
~/.platformio/penv/bin/pio run -e esp32dev
~/.platformio/penv/bin/pio run -e esp32dev -t upload
~/.platformio/penv/bin/pio device monitor --environment esp32dev
```

Tester RS232 (osobny firmware): `-e anenji_probe` — tylko FC03 @ 9600; `l` tor MAX3232/kabla, `r` test falownika, `d` klon dongla, `9` podsłuch. Test parsera na hoście:

```bash
g++ -std=c++17 -I src -o /tmp/test_anenji test/test_anenji_protocol.cpp src/drivers/AnenjiProtocol.cpp && /tmp/test_anenji
```

Jeżeli Ubuntu nie otwiera USB: `sudo usermod -aG dialout NAZWA_UZYTKOWNIKA`, wylogować i zalogować.

## Prompt do wklejenia

```text
Kontynuujemy diagnostykę i wdrożenie komunikacji falownika ANENJI ANJ-4200W-24V w SterownikDzialka. Pracujesz w sklonowanym repo. Najpierw przeczytaj ANENJI_PROTOKOL.md, potem PROMPT_DZIALKA.md, README.md, src/drivers/AnenjiProtocol.h, AnenjiDriver.cpp i AnenjiProbe.cpp. Nie zakładaj, że napisy RX/TX producenta są z perspektywy falownika.

Sprzęt:
- ESP32 DevKit (WROOM), monitor USB 115200.
- Falownik ANENJI ANJ-4200W-24V.
- Fabryczny WiFi Plug Pro (EB-WF03-01 + MAX3232). Przy pracy ESP jako master dongle MUSI być wypięty.
- MAX3232 3,3 V między ESP a RJ45 falownika.
- Domyślnie firmware: RX=GPIO33, TX=GPIO32.
- Laptop Ubuntu na miejscu.

Co jest już pewne (podsłuch dongla, nie zgadujemy):
- Protokół: Modbus RTU, 9600 8N1, slave 1, wyłącznie FC03. Rodzina ISolar/EASUN SMG II / POW-HVM (esphome-smg-ii).
- Stare założenie SmartESS 2341 / 2400 / slave 5 / ~4502 jest BŁĘDNE (2400 = cisza).
- Dongle na tym RJ45 NIE wysyła wakeup 01 AA 06 DE A2.
- Load domu = rejestr 213 (W). Moc PV = rejestr 223 (W). 219 to napięcie PV ×0,1, nie waty. 208 to mostek, nie load.
- Ramki HEX i mapa: ANENJI_PROTOKOL.md oraz test/test_anenji_protocol.cpp.
- Parser i driver są w repo. Firmware nigdy nie wysyła FC06/FC10 — nie dodawaj zapisów bez mojej zgody.

Stan w terenie (ostatnia sesja):
- Zamiana RJ45 pin 1/2 = cisza. Przywrócony oryginał = ruch, ale złe ramki (00 02, FF, 00 00, FE 00 FC 00). Kierunek RJ45 zostawiamy.
- Falownik wcześniej odpowiadał śmieciami, ale UART ESP nie składał FC03; później pojawiła się całkowita cisza. Najpierw sprawdzić rzeczywiste kierunki RS232 miernikiem, bo loopback nie wykrywa zamiany TX/RX.
- hello() w AnenjiDriver: najpierw 9600 bez invert + FC03 100, potem invert=true, potem wakeup 01 AA, potem log „zamień TTL w panelu”.
- MQTT TLS error 5 jest osobnym tematem (cert/NTP).

Moje zadanie:
1. Wgrać anenji_probe (dongle wypięty).
2. `l` — zwora T1OUT–R1IN na module, potem zwora RJ45 pin 1–2 na końcu kabla. Oba @ 9600 na stałym torze RX33/TX32. Loopback sprawdza ciągłość, ale nie potwierdza kierunków danych.
3. Podłączyć falownik i uruchomić `r`. Szukać `[OK]`, `DEKOD` oraz `UART ERR`. Probe używa wyłącznie elektrycznie poprawnego toru RX33/TX32 bez inwersji; nie wykonuje już mylącej programowej zamiany GPIO.
4. Jeśli OK — `d` (klon dongla) i porównać PV/load z aplikacją. Potem wgrać esp32dev.
5. Sukces firmware: ANENJI OK PV=… load=…

Prowadź krótko, krok po kroku, z naciskiem na bezpieczne połączenia. Nie wykonuj komend zapisu do falownika.
```

## Co zabrać z terminala

Wklej blok od startu `ANENJI SMG-II` / `ANENJI hello` aż do kilku cykli poll. Nie wycinaj `FAIL`, zer ani `MQTT TLS error`. Przy probe: cały przebieg `l` / `r` / `d` wraz z `PODSUMOWANIE` i `DIAGNOZA`; przy `9` cały `PODSLUCH DONGLA` … `KONIEC PODSLUCHU`.
