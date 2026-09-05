# Test konwertera MAX3232 z DB9 i kabla ANENJI

Scenariusz dla konwertera RS232↔TTL z żeńskim DB9, adaptera DB9 męskiego z terminalami śrubowymi oraz testera PlatformIO `anenji_probe`.

## Zasada nadrzędna

Nie ufamy nazwom `RXD` i `TXD` na adapterze. Adapter opisuje numery styków DB9, ale nie musi opisywać kierunku z perspektywy konwertera. Kierunek ustalamy napięciem spoczynkowym:

- wyjście nadajnika RS232 ma względem GND napięcie ujemne, zwykle około `-5...-7 V` dla MAX3232;
- niepodłączone wejście odbiornika ma zwykle około `0 V`;
- terminal DB9, który ma ujemne napięcie, jest wyjściem konwertera i musi później trafić do wejścia falownika.

## 1. Potwierdzony pinout konkretnego adaptera użytkownika

Fabryczny nadruk adaptera jest lustrzanym odbiciem i nie wolno według niego podłączać przewodu. Po sprawdzeniu ciągłości i napięć obowiązują własne oznaczenia pozycji zacisków śrubowych:

```text
ADAPTER ŚRUBOWY — NUMERY POZYCJI ZACISKÓW UŻYTKOWNIKA

zacisk 1: GND
zacisk 3: RX / R1IN — wejście konwertera, około 0 V bez kabla
zacisk 4: TX / T1OUT — wyjście konwertera, około -6,2 V w spoczynku
```

Nie są to standardowe numery sygnałów DB9 — są to pozycje zacisków na tym konkretnym, błędnie opisanym adapterze. Podczas dalszych prac kieruj się wyłącznie własnymi oznaczeniami `1=GND`, `3=RX`, `4=TX`.

Przed każdym podłączeniem, przy wyłączonym zasilaniu, można ponownie potwierdzić ciągłość pomiędzy zaciskiem i właściwym stykiem wtyku. Pomiędzy zaciskami 1, 3 i 4 nie może być zwarcia.

## 2. Podłączenie TTL ESP32 do MAX3232

Połączenie według kierunku danych:

```text
ESP GPIO32 TX  -> wejście TTL nadajnika konwertera (zwykle RXD konwertera)
ESP GPIO33 RX  <- wyjście TTL odbiornika konwertera (zwykle TXD konwertera)
ESP 3.3 V      -> VCC konwertera
ESP GND        -> GND konwertera
```

Jeżeli producent wyraźnie podaje inny opis strony TTL, użyj jego kierunków. W razie niepewności nie zamieniaj przewodów pod napięciem. Podczas prób można dać rezystory szeregowe `1 kΩ` na obu liniach TTL.

Tester od razu inicjalizuje UART `9600 8N1`: GPIO32 ma stan spoczynkowy HIGH, więc wyjście RS232 konwertera powinno pozostawać ujemne.

## 3. Loopback samego konwertera i adaptera

Falownik oraz fabryczny dongle muszą być odłączone.

1. Wepnij męski adapter śrubowy bezpośrednio do żeńskiego DB9 konwertera.
2. Zewrzyj na adapterze tylko oznaczone przez siebie zaciski `3/RX` i `4/TX`.
3. Zacisku `1/GND` nie zwieraj z liniami danych.
4. Wgraj i otwórz tester:

```bash
python3 -m platformio run -e anenji_probe -t upload
python3 -m platformio device monitor -b 115200
```

5. Wpisz `l` i Enter.

Oba wzorce powinny zakończyć się:

```text
WYNIK LOOPBACK: OK
```

Po teście usuń zworę 3–4. Jeżeli loopback nie działa, nie podłączaj jeszcze falownika — sprawdź stronę TTL, zasilanie 3,3 V i masę.

## 4. Identyfikacja wejścia i wyjścia DB9 konwertera

Pozostaw konwerter zasilony i tester uruchomiony, ale bez zwory 3–4. Ustaw multimetr na napięcie stałe, zakres co najmniej ±20 V:

```text
czarna sonda -> zacisk 1/GND
czerwona     -> zacisk 3, potem zacisk 4
```

Potwierdzony wynik:

```text
zacisk 3: około 0 V   = WEJŚCIE konwertera RX / R1IN
zacisk 4: około -6,2 V = WYJŚCIE konwertera TX / T1OUT
```

To ustalenie jest ważniejsze niż nadruk `RXD/TXD`.

## 5. Budowa przewodu DB9↔RJ45

Znane kierunki falownika:

```text
RJ45 pin 1 = wyjście falownika
RJ45 pin 2 = wejście falownika
RJ45 pin 8 = GND
```

Połącz:

```text
adapter zacisk 3 RX (~0 V)       <- RJ45 pin 1, wyjście falownika
adapter zacisk 4 TX (-6,2 V)     -> RJ45 pin 2, wejście falownika
adapter zacisk 1 GND              -- RJ45 pin 8, GND falownika
```

Całe połączenie w jednym piktogramie:

```text
       ADAPTER / MAX3232                         FALOWNIK / RJ45

  zacisk 3  RX / R1IN  (~0 V)   <────────────── pin 1  TX falownika (~-13 V)
  zacisk 4  TX / T1OUT (-6,2 V) ──────────────> pin 2  RX falownika (~0 V)
  zacisk 1  GND                  ─────────────── pin 8  GND
```

### Numeracja wtyku RJ45 — dwa widoki

Widok **od strony wejścia przewodu**, złote styki u góry, zatrzask pod spodem:

```text
                    przewód wchodzi od tej strony

          ┌───────────────────────────────┐
numery:   │  1   2   3   4   5   6   7   8 │
funkcja:  │ OUT  IN   .   .   .   .   .  GND│
kolor:    │ pom ziel  .   .   .   .   . brąz│
          └───────────────────────────────┘
                    zatrzask pod spodem
```

Widok **czołowy od strony złotych styków**, czyli patrzymy w nos wtyku; zatrzask nadal pod spodem. Obraz jest lustrzany:

```text
          ┌───────────────────────────────┐
numery:   │  8   7   6   5   4   3   2   1 │
funkcja:  │ GND  .   .   .   .   .  IN  OUT│
kolor:    │ brąz .   .   .   .   . ziel pom│
          └───────────────────────────────┘
                    zatrzask pod spodem

OUT = wyjście z falownika do RX konwertera
IN  = wejście falownika z TX konwertera
```

Jeżeli używasz uzgodnionych kolorów:

```text
RJ45 pin 1: pomarańczowy -> zacisk 3 RX adaptera
RJ45 pin 2: zielony      <- zacisk 4 TX adaptera
RJ45 pin 8: brązowy      -> zacisk 1 GND adaptera
```

Nie skręcaj pomarańczowego z zielonym. Pomarańczowy skręć z biało-pomarańczowym GND, a zielony z biało-zielonym GND. Oba białe przewody połącz z brązowym GND przy samym RJ45 oraz z zaciskiem `1/GND` tego adaptera.

## 6. Sprawdzenie gotowego kabla bez falownika

Przy wyłączonym sprzęcie i zdjętej zworze adaptera 3–4 sprawdź ciągłość:

```text
adapter zacisk 3 RX  <-> RJ45 pin 1
adapter zacisk 4 TX  <-> RJ45 pin 2
adapter zacisk 1 GND <-> RJ45 pin 8
```

Pomiędzy trzema liniami nie może być zwarcia.

Następnie podłącz kabel do konwertera, a na wolnym końcu RJ45 zewrzyj pin 1 z pinem 2. Falownik nadal ma być odłączony. Wpisz `l`. Dwa wyniki `OK` potwierdzają przejście przez ESP, MAX3232, DB9, adapter i cały kabel.

Loopback nie potwierdza kierunków TX/RX — dlatego kierunki ustaliliśmy wcześniej pomiarem napięcia.

## 7. Kontrola falownika przed połączeniem

Nie podłączaj jeszcze przewodu. Przy włączonym falowniku zmierz napięcia na jego RJ45 względem pinu 8/GND:

```text
RJ45 pin 1: oczekiwane ujemne napięcie wyjścia, wcześniej około -13 V
RJ45 pin 2: oczekiwane około 0 V, wejście falownika
```

Jeżeli wyniki są odwrotne, zatrzymaj się i połącz później według zmierzonych kierunków, nie według numerów.

## 8. Podłączenie do falownika

1. Usuń wszystkie zwory loopback.
2. Wyłącz sprzęt na czas wkładania i przepinania przewodów.
3. Podłącz kabel do falownika.
4. Laptop pozostaw na baterii lub zasil ESP z powerbanku.
5. Po uruchomieniu, ale przed wysłaniem testu, obie linie RS232 powinny być ujemne: jedną napędza MAX3232, drugą falownik.
6. W monitorze wpisz `r`.

Prawidłowe zapytanie kontrolne:

```text
01 03 00 64 00 03 44 14
```

Sukces to `[OK]`, `FC03 slave 1 CRC OK` i później `DEKOD`. W raporcie zachowaj także linie `UART ERR`. Jeżeli jest zupełna cisza, ponownie sprawdź kierunki. Jeżeli są błędy `frame` albo odebrane śmieci, sprawdź masę, napięcia pod obciążeniem i prowadzenie przewodu.

## 9. Kiedy natychmiast przerwać

- wyjście falownika około `-13 V` po podłączeniu do wejścia MAX-a spada prawie do zera;
- dwa wyjścia zostały połączone ze sobą;
- konwerter lub przewody wyraźnie się nagrzewają;
- napięcie VCC konwertera nie wynosi około 3,3 V;
- brak wspólnej masy sygnałowej;
- nie wiadomo, czy zwora loopback została usunięta.

Nie wykonujemy żadnych zapisów FC06/FC10 do falownika.
