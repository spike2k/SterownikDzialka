# Kontynuacja diagnostyki ANENJI na działce

Ten plik służy do rozpoczęcia nowej rozmowy z asystentem na laptopie z Ubuntu. Po sklonowaniu aktualnego repozytorium otwórz projekt w Codex/Cursor, rozpocznij rozmowę w kontekście tego katalogu i wklej cały tekst z sekcji **Prompt do wklejenia**.

## Pobranie projektu na Ubuntu

Zmiany diagnostyczne muszą być wcześniej zapisane w commicie i wysłane do GitHub. Następnie na laptopie:

```bash
git clone https://github.com/spike2k/SterownikDzialka.git
cd SterownikDzialka
```

Jeżeli repozytorium jest już sklonowane:

```bash
cd SterownikDzialka
git pull
```

Sprawdzenie, zbudowanie i wgranie testera:

```bash
python3 -m platformio run -e anenji_probe
python3 -m platformio device list
python3 -m platformio run -e anenji_probe -t upload
python3 -m platformio device monitor -b 115200
```

Jeżeli Python nie znajduje PlatformIO:

```bash
python3 -m pip install --user platformio
```

Jeżeli Ubuntu nie pozwala otworzyć portu USB, dodaj swoje konto do grupy `dialout`, wyloguj się i zaloguj ponownie:

```bash
sudo usermod -aG dialout NAZWA_UZYTKOWNIKA
```

## Prompt do wklejenia

```text
Kontynuujemy diagnostykę komunikacji falownika ANENJI ANJ-4200W-24V z projektu SterownikDzialka. Pracujesz w sklonowanym repozytorium i najpierw przeczytaj PROMPT_DZIALKA.md, README.md, platformio.ini oraz src/diagnostics/AnenjiProbe.cpp. Nie zakładaj, że oznaczenia RX/TX producenta są podane z perspektywy falownika — rozumuj według rzeczywistego kierunku danych.

Sprzęt:
- ESP32 DevKit, monitor USB 115200.
- Falownik ANENJI ANJ-4200W-24V.
- Fabryczny WiFi Plug Pro z EB-WF03-01, MAX3232 i watchdogiem NE555/AIP555.
- ESP32 używa GPIO33 i GPIO32 do diagnostyki.
- Laptop na działce działa pod Ubuntu.

Co już ustaliliśmy:
- Najbardziej prawdopodobny protokół to Modbus RTU/SmartESS 2341, 2400 baud, 8N1, adres urządzenia 5, rejestry od okolic 4502.
- Pierwszy aktywny test przez osobny MAX3232 nie uzyskał prawidłowej odpowiedzi. Jedyny odebrany fragment `00 00 00 FC` nie był pełną ani poprawną ramką Modbus.
- Możliwe przyczyny to zamienione kierunki RS232, niewłaściwe wpięcie w MAX3232 albo inny wariant protokołu.
- Działający fabryczny dongle jest teraz traktowany jako źródło prawdy: chcemy biernie podejrzeć zarówno jego zapytania, jak i odpowiedzi falownika.

Aktualny tester `anenji_probe` po uruchomieniu niczego sam nie nadaje. Polecenia monitora:
- `s` — równoczesny bierny podsłuch obu pól UART TTL przez 20 sekund przy 2400 baud,
- `9` — taki sam podsłuch przy 9600 baud,
- `r` — aktywne zapytania przez MAX3232; wolno użyć tylko po wypięciu fabrycznego dongla,
- `l` — loopback osobnego MAX3232; falownik i dongle mają być wypięte.

Połączenie do biernego podsłuchu działającego dongla:
- dongle pozostaje normalnie podłączony i zasilany przez falownik,
- GND dongla -> GND ESP32,
- pole TX dongla -> GPIO33 ESP32,
- pole RX dongla -> GPIO32 ESP32,
- nie podłączamy 3.3V,
- nie dotykamy DL,
- nie używamy MAX3232 między polami TTL dongla i ESP32,
- najlepiej zastosować po jednym rezystorze szeregowym 4,7-10 kOhm na liniach TX i RX.

Na płytce są dwa zestawy punktów: `GND, TX, RX` oraz `DL, GND, RX, TX, 3.3V`. Przy wyłączonym donglu sprawdzamy miernikiem ciągłość między odpowiadającymi sobie GND, TX i RX. Jeżeli są połączone, wybieramy wygodniejszy komplet. Jeżeli nie są połączone, podsłuchujemy oba zestawy kolejno. Przed dołączeniem ESP mierzymy napięcia linii względem GND; dla strony TTL powinny pozostawać w zakresie około 0-3,3 V. Przy napięciu wyższym lub ujemnym przerywamy i najpierw identyfikujemy punkt.

Moje zadanie w terenie:
1. Wgrać środowisko `anenji_probe`.
2. Z fabrycznym donglem połączonym z falownikiem wykonać `s`.
3. Jeżeli dane są nieczytelne albo panuje cisza, wykonać `9`.
4. Skopiować pełny raport zawierający obie linie, HEX, ASCII i znaczniki czasu.
5. W razie potrzeby powtórzyć próbę po wywołaniu odświeżenia danych w aplikacji dongla albo po bezpiecznym restarcie samego połączenia.

Kiedy wkleję raport, przeanalizuj osobno obie linie, wyszukaj kompletne ramki, sprawdź CRC Modbus i ustal prędkość, adres slave, funkcje oraz rejestry. Nie zmieniaj jeszcze głównego sterownika na podstawie pojedynczych przypadkowych bajtów. Gdy protokół będzie potwierdzony, zaktualizuj `src/drivers/AnenjiDriver.cpp`, dodaj rozsądne logowanie diagnostyczne i testy/parser ramek, a następnie zbuduj oba środowiska PlatformIO. Zachowaj lokalne zabezpieczenia sterownika i nie wykonuj komend zapisu do falownika bez mojej wyraźnej zgody.

Za chwilę wkleję wynik terminala albo opiszę, na którym kroku jestem. Prowadź mnie krótko, krok po kroku, z naciskiem na bezpieczne połączenia.
```

## Co zabrać z terminala

Do nowej rozmowy najlepiej wkleić cały blok od `PODSLUCH DONGLA` do `KONIEC PODSLUCHU`. Nie wycinaj zer, powtarzających się ramek ani linii, które wyglądają na śmieci — ich odstępy i powtarzalność również pomagają rozpoznać prędkość oraz protokół.
