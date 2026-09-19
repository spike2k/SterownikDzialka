# Home Assistant — Sterownik działka EMS

## 1. Aktualizacja firmware ESP32

Nowy firmware publikuje potwierdzony stan trybu i przekaźników:

- `ems/sterownik-dzialka/mode/state`
- `ems/sterownik-dzialka/relay/{0..9}/state`

oraz przyjmuje zmianę trybu na `ems/sterownik-dzialka/mode/set` (`auto` albo `manual`).
Przełączniki przekaźników w HA są dzięki temu nieoptymistyczne i czekają na stan zwrotny z ESP32.

## 2. Pakiet encji

Skopiuj pakiet do `/config/packages/`:

```text
homeassistant/packages/sterownik_dzialka.yaml
  -> /config/packages/sterownik_dzialka.yaml
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

## 4. Satelity MQTT

Gotowe encje znajdują się w `packages/sterownik_dzialka.yaml`. Każdy satelita
ma dokładnie jedną encję HA, która komunikuje się bezpośrednio z urządzeniem:

- `switch.dzialka_studnia` — kanał 0 OpenBeken,
- `switch.dzialka_fontanna` — Tasmota,
- `switch.dzialka_balia` — Tasmota,
- `switch.dzialka_bojler` — Tasmota po uruchomieniu urządzenia.

Sterownik EMS również komunikuje się bezpośrednio z satelitami. Nie instaluj
automatyzacji pośredniej ani blueprintu przekładającego polecenia EMS na encję
HA. Taki most dubluje polecenia i stany urządzeń Tasmota.

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

W panelu EMS ustaw wybrany odbiornik następująco:

- GPIO: puste / `-1`,
- MQTT: `studnia`,
- moc, priorytet, progi SOC i czasy: według obciążenia pompy.

## 5. Następne satelity

Każdy następny satelita dostaje unikalny, prosty identyfikator, np. `fontanna`,
`bojler` albo `pompa`. Dodaj dla niego jedną bezpośrednią encję MQTT do sekcji
`mqtt.switch` w `packages/sterownik_dzialka.yaml`.

Przykład ręcznej encji dla identyfikatora `balia`:

```yaml
- name: "Działka balia"
  default_entity_id: switch.dzialka_balia
  unique_id: dzialka_balia
  state_topic: "stat/balia/POWER"
  command_topic: "cmnd/balia/POWER"
  payload_on: "ON"
  payload_off: "OFF"
  optimistic: false
  qos: 1
  retain: false
  availability_topic: "tele/balia/LWT"
  payload_available: "Online"
  payload_not_available: "Offline"
  icon: mdi:hot-tub
```

Sterowanie przebiega następująco:

1. Sterownik EMS wybiera odbiornik o danym kluczu MQTT.
2. Publikuje bezpośrednie polecenie Tasmota `cmnd/<id>/POWER` oraz zgodnościowe
   polecenie OpenBeken `<id>/0/set`.
3. Urządzenie publikuje swój rzeczywisty stan; EMS i HA odbierają go niezależnie.
4. Awaria lub restart HA nie przerywa sterowania EMS ↔ satelita.

## 6. ACL Mosquitto

Konto używane przez Home Assistant potrzebuje dostępu do tematów encji, które
obsługuje. Dla obecnych satelitów potrzebne są:

```conf
user <konto_mqtt_home_assistant>
topic write cmnd/+/POWER
topic read stat/+/POWER
topic read tele/+/LWT
topic read studnia/0/get
topic read studnia/connected
topic write studnia/0/set
```

Wspólne konto satelitów bez TLS może obsługiwać urządzenia OpenBeken i Tasmota.
Wildcardy są ograniczone do przestrzeni `cmnd`, `stat` i `tele`; identyfikatory
OpenBeken są wymienione jawnie, aby reguła nie dawała zapisu do `ems/#`:

```conf
user studnia_bk
topic read cmnd/+/#
topic write stat/+/#
topic write tele/+/#

topic read studnia/+/set
topic write studnia/#
topic read fontanna/+/set
topic write fontanna/#
topic read balia/+/set
topic write balia/#
topic read awaryjny/+/set
topic write awaryjny/#
```

`awaryjny` jest identyfikatorem zapasowego Sonoff Basic. Urządzenia Tasmota
korzystają z `cmnd/<id>/#`, `stat/<id>/#` i `tele/<id>/#`; urządzenia OpenBeken
mogą nadal używać tematów `<id>/...`. Wspólne konto świadomie pozwala satelitom
publikować jako inny satelita. Gdy będzie potrzebna pełna izolacja, należy nadać
każdemu urządzeniu osobne konto i osobny zestaw reguł ACL.

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
