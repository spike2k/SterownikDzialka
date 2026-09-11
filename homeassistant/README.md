# Home Assistant — Sterownik działka EMS

## 1. Aktualizacja firmware ESP32

Nowy firmware publikuje potwierdzony stan trybu i przekaźników:

- `ems/sterownik-dzialka/mode/state`
- `ems/sterownik-dzialka/relay/{0..9}/state`

oraz przyjmuje zmianę trybu na `ems/sterownik-dzialka/mode/set` (`auto` albo `manual`).
Przełączniki przekaźników w HA są dzięki temu nieoptymistyczne i czekają na stan zwrotny z ESP32.

## 2. Pakiety encji i blueprint satelitów

Skopiuj oba pliki z `packages/` do `/config/packages/` oraz blueprint:

```text
homeassistant/packages/sterownik_dzialka.yaml
  -> /config/packages/sterownik_dzialka.yaml
homeassistant/packages/satelity_dzialka.yaml
  -> /config/packages/satelity_dzialka.yaml
homeassistant/blueprints/automation/dzialka/satelita_ems.yaml
  -> /config/blueprints/automation/dzialka/satelita_ems.yaml
```

W `/config/configuration.yaml` dodaj, jeśli jeszcze nie ma:

```yaml
homeassistant:
  packages: !include_dir_named packages
```

Jeśli istnieje już sekcja `homeassistant:`, dopisz do niej tylko linię `packages:` — nie twórz drugiej sekcji.
Następnie wybierz **Narzędzia deweloperskie → YAML → Sprawdź konfigurację** i uruchom ponownie HA.

## 3. Pulpit

Utwórz nowy pulpit, otwórz jego edytor YAML i wklej zawartość `dashboard_dzialka.yaml`.
Pulpit korzysta wyłącznie z kart wbudowanych w Home Assistant, bez HACS.

## 4. Studnia — pierwszy satelita OpenBeken

Gotowa konfiguracja znajduje się w `packages/satelity_dzialka.yaml`. Tworzy ona
dwie encje o celowo różnych rolach:

- `switch.dzialka_studnia` — logiczny przełącznik EMS widoczny na pulpicie,
- `switch.dzialka_studnia_sprzet` — fizyczny kanał 0 urządzenia OpenBeken.

OpenBeken używa bazowego tematu `studnia`:

- komenda fizyczna: `studnia/0/set`, payload `1` albo `0`,
- stan fizyczny: `studnia/0/get`, payload `1` albo `0`,
- dostępność: `studnia/connected`, payload `online` albo `offline`.

Na urządzeniu zostaw włączoną flagę 10 (publikacja stanu po połączeniu) i flagę
21 (retained dla kanałów mocy). W `autoexec.bat` zachowaj lokalny fail-safe:

```text
SetStartValue 0 0
AddChangeHandler NoMQTTTime > 300 SetChannel 0 0
```

Pierwsza linia wymusza bezpieczny stan po restarcie. Druga wyłącza kanał 0 po
pięciu minutach bez połączenia MQTT, niezależnie od działania HA.

Do czasu rekonfiguracji OpenBeken blueprint tłumaczy stare tematy urządzenia na
wspólny kontrakt: `cmnd/studnia/POWER`, `stat/studnia/POWER` i
`tele/studnia/LWT`. Po przełączeniu firmware satelity na ten kontrakt most HA
można usunąć. HA nie jest wtedy potrzebny do sterowania EMS ↔ satelita.

W panelu EMS ustaw wybrany odbiornik następująco:

- GPIO: puste / `-1`,
- MQTT: `studnia`,
- moc, priorytet, progi SOC i czasy: według obciążenia pompy.

## 5. Następne satelity: OpenBeken i WifiSmartSwitch

Każdy następny satelita dostaje unikalny, prosty identyfikator, np. `fontanna`,
`bojler` albo `pompa`. Skopiuj instancję automatyzacji w
`packages/satelity_dzialka.yaml` i ustaw:

```yaml
- id: dzialka_fontanna_most_ems
  alias: "Działka — most EMS satelity fontanna"
  use_blueprint:
    path: dzialka/satelita_ems.yaml
    input:
      satellite_id: fontanna
      physical_switch: switch.gniazdko_fontanna
```

`physical_switch` może być encją utworzoną ręcznie dla OpenBeken tak jak
`switch.dzialka_studnia_sprzet` albo encją dostarczoną przez WifiSmartSwitch.
Warstwa EMS pozostaje identyczna — rodzaj sprzętu nie wpływa na tematy EMS.

Jeżeli kolejny OpenBeken używa kanału 0, skopiuj także definicję fizycznego
przełącznika MQTT i zmień nazwę bazową urządzenia w tematach, identyfikatory
`unique_id`/`device.identifiers` oraz nazwę encji.

Sterowanie w okresie migracji przebiega następująco:

1. Sterownik EMS wybiera odbiornik o danym kluczu MQTT.
2. Publikuje `ON` lub `OFF` równolegle na stary temat i `cmnd/<id>/POWER`.
3. Blueprint HA tymczasowo włącza lub wyłącza fizyczną encję `switch`.
4. HA publikuje rzeczywisty stan na `stat/<id>/POWER` z flagą retain.
5. Docelowo urządzenie realizuje kroki 3–4 samo, także gdy HA nie działa.

Starszy plik `satellite_fontanna.example.yaml` pozostaje przykładem konfiguracji
bez blueprintu, ale dla nowych satelitów zalecany jest wspólny blueprint.

## 6. ACL Mosquitto

Konto używane przez Home Assistant musi mieć co najmniej dostęp do tematów EMS
oraz urządzeń, które obsługuje. Dla studni potrzebne są:

```conf
user <konto_mqtt_home_assistant>
topic read ems/sterownik-dzialka/load/+/set
topic read ems/sterownik-dzialka/status
topic readwrite ems/remote/#
topic readwrite cmnd/+/POWER
topic readwrite stat/+/POWER
topic readwrite tele/+/LWT
topic read studnia/0/get
topic read studnia/connected
topic write studnia/0/set
```

Konto samego OpenBeken może pozostać mocno ograniczone:

```conf
user studnia_bk
topic read studnia/+/set
topic read cmnd/studnia/POWER
topic write studnia/#
topic write stat/studnia/POWER
topic write tele/studnia/LWT
```

Konto EMS wymaga w okresie migracji poniższych dodatkowych reguł (oprócz
dotychczasowych tematów telemetrii, trybu i OTA):

```conf
user esp32_dzialka
topic write ems/sterownik-dzialka/load/+/set
topic write cmnd/+/POWER
topic read stat/+/POWER
topic read tele/+/LWT
topic write +/0/set
topic read +/0/get
topic read +/connected
```

Po zmianie ACL sprawdź konfigurację i przeładuj Mosquitto:

```bash
sudo mosquitto -c /etc/mosquitto/mosquitto.conf -t
sudo systemctl reload mosquitto
```

## 7. Starszy przykład fontanny

Po dodaniu smart-gniazdka skopiuj dwie automatyzacje z `satellite_fontanna.example.yaml` do
`/config/automations.yaml`. W obu miejscach zastąp `switch.gniazdko_fontanna` rzeczywistym identyfikatorem encji.

Sterowanie automatyczne działa następująco:

1. Sterownik EMS wybiera odbiornik o kluczu MQTT `fontanna`.
2. Publikuje `ON` lub `OFF` na `ems/sterownik-dzialka/load/fontanna/set`.
3. Automatyzacja HA włącza lub wyłącza smart-gniazdko.
4. HA publikuje jego rzeczywisty stan na `ems/remote/fontanna/state` z flagą retain.

Ręczne żądania wysyłane przez panel ESP trafiają na `ems/remote/fontanna/set` i korzystają z tego samego mostu.

Każdy następny satelita może dostać analogiczny identyfikator, np. `bojler`, `grzalka` lub `pompa`.
