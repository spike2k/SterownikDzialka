# ANENJI ANJ-4200W-24V — protokół, ramki, stan prac

Notatka do kontynuacji na innym komputerze. Szczegóły implementacji: `src/drivers/AnenjiProtocol.*`, `AnenjiDriver.*`. Prompt terenowy: `PROMPT_DZIALKA.md`.

**Zakaz:** bez wyraźnej zgody użytkownika nie wysyłać do falownika FC06/FC10 ani innych zapisów. Rejestry 300+ są R/W na magistrali — firmware czyta wyłącznie FC03.

## Potwierdzone z podsłuchu fabrycznego dongla

Źródło prawdy: bierny sniff TTL WiFi Plug Pro (EB-WF03-01), ESP GPIO33/32 jako wejścia, bez MAX3232, bez 3.3 V, bez DL.

| Hipoteza | Wynik |
|---|---|
| SmartESS 2341, 2400 8N1, slave 5, ~4502 | **błędna** — `s` (2400) = cisza |
| Native Modbus RTU SMG II / POW-HVM | **potwierdzona** — `9` (9600) |

- **9600 8N1**, slave **1**, wyłącznie **FC03**
- Dongle **nie** wysyła wakeup `01 AA 06 DE A2` (to wariant USB-A 6 kW z [esphome-smg-ii](https://github.com/syssi/esphome-smg-ii))
- Po resecie dongle: śmieci boot (`OAEM`, `0D 0A`, `00`/`FF`), Modbus od ~18,7 s
- Odstęp zapytanie→odpowiedź ~14 ms przy 9600

### Kierunki przy podsłuchu (dongle w falowniku)

Po poprawnym wpięciu obu pól:

- pad **TX → GPIO33** = zapytania dongla (master)
- pad **RX → GPIO32** = odpowiedzi falownika (slave)

Przy ESP jako master (dongle wypięty, MAX3232): **TX=GPIO32, RX=GPIO33** (odwrotnie niż sniff). RJ45 wg README: pin 1 = TX falownika → MAX3232 `R1IN`, pin 2 = RX falownika ← `T1OUT`, pin 8 = GND; TTL `R1OUT`→GPIO33, `T1IN`←GPIO32.

## Cykl odpytań dongla (FC03)

Te same komendy w kółko; zmieniają się tylko wartości na żywo.

| Start | Liczba | Znaczenie |
|---:|---:|---|
| 100 | 3 | fault — dongle zaczyna tutaj |
| 104, 106 | 1 | |
| 108 | 3 | warning |
| 171 | 28 | tożsamość / serial (`000000` w sniffach) |
| **200** | **22** | live 200–221 |
| **223** | **13** | moc PV, %, temperatury, SOC |
| 300 | 12 | tryb wyjścia / priorytet (**R/W — nie zapisujemy**) |
| 313 | 5 | |
| 320 | 20 | tabliczka 230 V / 50 Hz |
| 404, 420, 427, 450… | różne | konfiguracja |
| 641 | 5 | m.in. **643 = 4200 W** znamionowa |

Sterownik odpytuje tylko **100/3** (hello), potem **200/22** + **223/13**.

## Mapa live (skorygowana przy ~914 W)

| Rej. | Skala | Znaczenie |
|---:|---|---|
| 200 | — | nieudokumentowane (`0xB000` w sniffach) |
| 201 | enum | 0 PowerOn, 1 Standby, 2 Mains, **3 OffGrid**, 4 Bypass, 5 Charging, 6 Fault |
| 202 | ×0.1 V | napięcie sieci |
| 203 | ×0.01 Hz | częstotliwość sieci |
| 204 | W | moc sieci |
| 205–207 | V/A/Hz | mostek falownika |
| **208** | W | moc mostka — **nie** load domu |
| 210 | ×0.1 V | napięcie wyjścia |
| **213** | W | **moc czynna wyjścia = load domu** |
| 214 | VA | moc pozorna |
| 215 | ×0.1 V | Vbat |
| 216 | ×0.1 A **signed** | Ibat (ujemny = rozładunek) |
| 217 | W signed | moc baterii |
| 218 | — | ~360 V, prawdopodobnie szyna DC |
| **219** | ×0.1 V | **napięcie PV, nie waty** |
| 220 | ×0.1 A | prąd PV |
| **223** | W | **moc PV** (w sniffach nocnych 0 W) |
| 225 | % | % mocy znamionowej (21% ≈ 914/4200) |
| 226, 227 | °C | DCDC / inverter |
| 229 | % | SOC |
| 232 | ×0.1 A signed | ten sam prąd co 216 |

W sniffie ~90 W: 213=79 W, 208=90 W, 210=230.0 V, 215=27.0 V, 216=−5.6 A, 219=16.8 V.
W sniffie ~914 W: 213=914 W, 208=960 W, 210=231.2 V, 215=26.5 V, 216=−37.8 A, 219=16.6 V, 223=0 W, 225=21%.

## Rozpoznane ramki (HEX)

CRC Modbus RTU little-endian, zgodne z `AnenjiProtocol::crc16`.

### Zapytania dongla

```
FC03 100/3:  01 03 00 64 00 03 44 14
FC03 200/22: 01 03 00 C8 00 16 45 FA
FC03 223/13: 01 03 00 DF 00 0D B5 F5
```

Pusta odpowiedź 1 rejestr: `01 03 02 00 00 B8 44`.

Wakeup z esphome (CRC Modbus, ale **dongle RJ45 go nie wysyła**):

```
01 AA 06 DE A2
```

### Odpowiedź live ~90 W (49 B, 200/22)

```
01 03 2C B0 00 00 03 00 00 00 00 00 00 08 FC 00
0A 13 88 00 5A 00 00 08 FC 00 06 13 88 00 4F 00
8A 01 0E FF C8 FF 8F 0E 7D 00 A8 00 00 00 00 C9
E6
```

### Odpowiedź live ~914 W (49 B)

```
01 03 2C B0 00 00 03 00 00 00 00 00 00 09 08 00
2A 13 8E 03 C0 00 00 09 08 00 28 13 8E 03 92 03
9C 01 09 FE 86 FC 17 0E 0D 00 A6 00 00 00 00 C3
69
```

### Odpowiedź status ~914 W (31 B, 223/13)

```
01 03 1A 00 00 00 00 00 15 00 16 00 19 00 14 00
56 00 00 00 60 FE 86 00 00 00 00 00 00 1F 41
```

Parser i te ramki: `test/test_anenji_protocol.cpp`. Host:

```bash
g++ -std=c++17 -I src -o /tmp/test_anenji test/test_anenji_protocol.cpp src/drivers/AnenjiProtocol.cpp && /tmp/test_anenji
```

## Co jest w kodzie

- `AnenjiProtocol` — CRC, budowa/parsowanie FC03, dekodowanie 200/223, **bez helperów zapisu**
- `AnenjiDriver` — baud 9600 (`AppConfig.h`), poll live + status, moc 0–5000 W
- `hello()`: (1) UART bez invert + FC03 100, (2) `HardwareSerial` invert=true, (3) invert off + jeden wakeup `01 AA`, (4) log: zostaw RJ45, zamień TTL 32/33 w panelu
- `anenji_probe` — wyłącznie 9600 8N1 slave 1 FC03 (bez 2400/PIP/8851). `l` loopback chip+kabel, `r` macierz UART + wake/dump, `d` klon cyklu dongla, `9` podsłuch TTL z parserem FC03
- PIO: `~/.platformio/penv/bin/pio` (niekoniecznie `python3 -m platformio`)

## Stan w terenie (2026-09-02) — ESP + MAX3232, dongle wypięty

TTL: `R1OUT`→GPIO33, `T1IN`←GPIO32 (zgodnie z firmware).

| Próba | Wynik |
|---|---|
| Zamiana RJ45 pin 1/2 | **cisza** — zły kierunek RS232 |
| Przywrócony oryginalny RJ45 | **ruch, ale złe ramki** — kierunek RJ45 jest dobry |

Nie zamieniać ponownie pinów 1/2 na RJ45.

Śmieci z monitora (nie FC03):

- wcześniej ~20–24 B, start `00 02` (wygląda na przesunięte bity `01 03`), ogon `C0 84 FE 0E 0C` (~360 V szyny) — falownik gada, UART rozsynchronizowany
- po wakeup-first: `FF`, `00 00`, `FE 00 FC 00`, same zera, `FF FF 6F FF 00`

`MQTT TLS error 5` jest osobnym problemem (cert/NTP), nie RS232.

Te logi były sprzed invert-fallback w driverze. Teraz najpierw `anenji_probe` (`l`, potem `r`) — macierz UART robi invert i zamianę TTL sama. `esp32dev` dopiero po `[OK]`.

MAX3232 na **3.3 V**, wspólna masa, kondensatory charge-pump. GPIO32/33 na ESP32-WROVER to 32 kHz XTAL — DevKit WROOM powinien być OK.

## Następny krok (krótko)

1. `git pull`, wgrać `anenji_probe` (nie `esp32dev`).
2. `l` ze zworą `T1OUT`–`R1IN` na module, potem `l` ze zworą RJ45 pin 1–2 na końcu kabla. Falownik wypięty.
3. Podłączyć falownik (dongle wypięty, RJ45 oryginalny), `r`. Szukać `[OK]` / `DEKOD`.
4. Jeśli OK — `d` i porównać PV/load z aplikacją dongla.
5. Nadal śmieci po `r` → zostaw RJ45 1/2; probe sam próbuje invert i zamianę TTL.
6. Po sukcesie wgrać `esp32dev`. Nie ruszać zapisów do falownika.
