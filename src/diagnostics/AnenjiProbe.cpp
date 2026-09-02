#include <Arduino.h>

namespace {
constexpr int kInverterRxPin = 33;  // ESP32 RX <- MAX3232 TTL TX
constexpr int kInverterTxPin = 32;  // ESP32 TX -> MAX3232 TTL RX
constexpr uint32_t kSniffDurationMs = 20000;
constexpr size_t kMaxResponse = 512;
constexpr size_t kMaxResults = 16;

HardwareSerial inverter(2);
HardwareSerial secondSniffer(1);

struct Capture {
  uint8_t data[kMaxResponse]{};
  size_t length = 0;
  bool overflow = false;
};

enum class ResultKind : uint8_t { None, Traffic, Valid };

struct TestResult {
  String name;
  ResultKind kind = ResultKind::None;
  size_t bytes = 0;
};

TestResult results[kMaxResults];
size_t resultCount = 0;

uint16_t modbusCrc(const uint8_t* data, size_t length) {
  uint16_t crc = 0xFFFF;
  for (size_t index = 0; index < length; ++index) {
    crc ^= data[index];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 1U) ? static_cast<uint16_t>((crc >> 1U) ^ 0xA001U)
                       : static_cast<uint16_t>(crc >> 1U);
    }
  }
  return crc;
}

uint16_t xmodemCrc(const uint8_t* data, size_t length) {
  uint16_t crc = 0;
  for (size_t index = 0; index < length; ++index) {
    crc ^= static_cast<uint16_t>(data[index]) << 8U;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x8000U) ? static_cast<uint16_t>((crc << 1U) ^ 0x1021U)
                            : static_cast<uint16_t>(crc << 1U);
    }
  }
  return crc;
}

uint8_t escapePipCrcByte(uint8_t value) {
  return (value == 0x28 || value == 0x0D || value == 0x0A) ? static_cast<uint8_t>(value + 1U) : value;
}

void configureUart(uint32_t baud) {
  inverter.end();
  delay(80);
  inverter.setRxBufferSize(kMaxResponse * 2);
  inverter.begin(baud, SERIAL_8N1, kInverterRxPin, kInverterTxPin);
  delay(180);
  while (inverter.available()) inverter.read();
}

Capture captureResponse(uint32_t timeoutMs = 1600, uint32_t idleMs = 180) {
  Capture capture;
  const uint32_t startedAt = millis();
  uint32_t lastByteAt = startedAt;
  bool started = false;

  while (millis() - startedAt < timeoutMs) {
    while (inverter.available()) {
      const int value = inverter.read();
      if (value < 0) break;
      started = true;
      lastByteAt = millis();
      if (capture.length < sizeof(capture.data)) {
        capture.data[capture.length++] = static_cast<uint8_t>(value);
      } else {
        capture.overflow = true;
      }
    }
    if (started && millis() - lastByteAt >= idleMs) break;
    delay(1);
  }
  return capture;
}

void printBytes(const char* label, const uint8_t* data, size_t length) {
  Serial.print(label);
  Serial.print(" [");
  Serial.print(length);
  Serial.print(" B] HEX: ");
  for (size_t index = 0; index < length; ++index) {
    if (data[index] < 0x10) Serial.print('0');
    Serial.print(data[index], HEX);
    if (index + 1 != length) Serial.print(' ');
  }
  Serial.println();
  Serial.print("ASCII: ");
  for (size_t index = 0; index < length; ++index) {
    const char glyph = static_cast<char>(data[index]);
    Serial.print(glyph >= 32 && glyph <= 126 ? glyph : '.');
  }
  Serial.println();
}

bool validModbus(const Capture& capture, uint8_t slave) {
  if (capture.length < 5 || capture.data[0] != slave) return false;
  if (capture.data[1] != 0x03 && capture.data[1] != 0x83) return false;
  const uint16_t received = static_cast<uint16_t>(capture.data[capture.length - 2]) |
                            (static_cast<uint16_t>(capture.data[capture.length - 1]) << 8U);
  return received == modbusCrc(capture.data, capture.length - 2);
}

bool valid8851(const Capture& capture) {
  if (capture.length < 10 || capture.data[0] != 0x88 || capture.data[1] != 0x51) return false;
  const uint16_t received = static_cast<uint16_t>(capture.data[capture.length - 2]) |
                            (static_cast<uint16_t>(capture.data[capture.length - 1]) << 8U);
  return received == modbusCrc(capture.data, capture.length - 2);
}

bool validPip(const Capture& capture) {
  if (capture.length < 4) return false;
  return capture.data[0] == '(' || capture.data[0] == 'A' || capture.data[0] == 'N';
}

void remember(const String& name, ResultKind kind, size_t bytes) {
  if (resultCount >= kMaxResults) return;
  results[resultCount++] = {name, kind, bytes};
}

void showCapture(const String& name, uint32_t baud, const Capture& capture, bool valid) {
  Serial.println();
  Serial.print("--- ");
  Serial.print(name);
  Serial.print(" @ ");
  Serial.print(baud);
  Serial.println(" 8N1 ---");
  if (!capture.length) {
    Serial.println("RX: cisza");
    remember(name + " @ " + String(baud), ResultKind::None, 0);
    return;
  }
  printBytes("RX", capture.data, capture.length);
  if (capture.overflow) Serial.println("UWAGA: odpowiedz dluzsza niz bufor; koniec zostal uciety.");
  Serial.println(valid ? "WYNIK: poprawna odpowiedz protokolu" : "WYNIK: sa bajty, ale format/CRC nie zostal potwierdzony");
  remember(name + " @ " + String(baud), valid ? ResultKind::Valid : ResultKind::Traffic, capture.length);
}

void sendModbusRead(uint32_t baud, uint8_t slave, uint16_t firstRegister, uint16_t registerCount) {
  configureUart(baud);
  uint8_t request[8] = {
      slave,
      0x03,
      static_cast<uint8_t>(firstRegister >> 8U),
      static_cast<uint8_t>(firstRegister & 0xFFU),
      static_cast<uint8_t>(registerCount >> 8U),
      static_cast<uint8_t>(registerCount & 0xFFU),
      0,
      0,
  };
  const uint16_t crc = modbusCrc(request, 6);
  request[6] = static_cast<uint8_t>(crc & 0xFFU);
  request[7] = static_cast<uint8_t>(crc >> 8U);

  const String name = "Modbus FC03 slave=" + String(slave) + " start=" + String(firstRegister);
  Serial.println();
  Serial.print("TX ");
  Serial.println(name);
  printBytes("TX", request, sizeof(request));
  inverter.write(request, sizeof(request));
  inverter.flush();
  const Capture capture = captureResponse();
  showCapture(name, baud, capture, validModbus(capture, slave));
}

void send8851(uint32_t baud, bool configuration) {
  configureUart(baud);
  const uint8_t stateRequest[] = {0x88, 0x51, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x4D, 0x08};
  const uint8_t configRequest[] = {0x88, 0x51, 0x00, 0x03, 0x02, 0x00, 0x00, 0x00, 0x4C, 0xB0};
  const uint8_t* request = configuration ? configRequest : stateRequest;
  const size_t length = configuration ? sizeof(configRequest) : sizeof(stateRequest);
  const String name = configuration ? "8851 config READ" : "8851 state READ";

  Serial.println();
  Serial.print("TX ");
  Serial.println(name);
  printBytes("TX", request, length);
  inverter.write(request, length);
  inverter.flush();
  const Capture capture = captureResponse(2200, 220);
  showCapture(name, baud, capture, valid8851(capture));
}

void sendPip(uint32_t baud, const char* command) {
  configureUart(baud);
  uint8_t request[24]{};
  const size_t commandLength = strlen(command);
  memcpy(request, command, commandLength);
  const uint16_t crc = xmodemCrc(reinterpret_cast<const uint8_t*>(command), commandLength);
  request[commandLength] = escapePipCrcByte(static_cast<uint8_t>(crc >> 8U));
  request[commandLength + 1] = escapePipCrcByte(static_cast<uint8_t>(crc & 0xFFU));
  request[commandLength + 2] = '\r';
  const size_t length = commandLength + 3;
  const String name = "PIP/Sumry " + String(command);

  Serial.println();
  Serial.print("TX ");
  Serial.println(name);
  printBytes("TX", request, length);
  inverter.write(request, length);
  inverter.flush();
  const Capture capture = captureResponse(1800, 220);
  showCapture(name, baud, capture, validPip(capture));
}

void passiveListen(uint32_t baud, uint32_t durationMs) {
  configureUart(baud);
  Serial.println();
  Serial.print("--- pasywny nasluch @ ");
  Serial.print(baud);
  Serial.print(" przez ");
  Serial.print(durationMs);
  Serial.println(" ms ---");
  const Capture capture = captureResponse(durationMs, 250);
  showCapture("pasywny nasluch", baud, capture, false);
}

struct SniffLine {
  HardwareSerial* uart;
  const char* name;
  uint8_t data[256]{};
  size_t length = 0;
  bool overflow = false;
  uint32_t firstByteAt = 0;
  uint32_t lastByteAt = 0;
};

void flushSniffLine(SniffLine& line, uint32_t startedAt) {
  if (!line.length && !line.overflow) return;
  Serial.print('[');
  Serial.print(line.firstByteAt - startedAt);
  Serial.print(" ms] ");
  printBytes(line.name, line.data, line.length);
  if (line.overflow) Serial.println("UWAGA: fragment byl dluzszy niz bufor i zostal uciety.");
  line.length = 0;
  line.overflow = false;
}

void pollSniffLine(SniffLine& line, uint32_t now) {
  while (line.uart->available()) {
    const int value = line.uart->read();
    if (value < 0) break;
    if (!line.length && !line.overflow) line.firstByteAt = now;
    line.lastByteAt = now;
    if (line.length < sizeof(line.data)) {
      line.data[line.length++] = static_cast<uint8_t>(value);
    } else {
      line.overflow = true;
    }
  }
}

void dualPassiveSniff(uint32_t baud) {
  inverter.end();
  secondSniffer.end();
  delay(80);

  // Oba piny sa tutaj tylko wejsciami. Nie podajemy zadnego sygnalu na dongle.
  inverter.setRxBufferSize(1024);
  secondSniffer.setRxBufferSize(1024);
  inverter.begin(baud, SERIAL_8N1, kInverterRxPin, -1);
  secondSniffer.begin(baud, SERIAL_8N1, kInverterTxPin, -1);
  delay(120);
  while (inverter.available()) inverter.read();
  while (secondSniffer.available()) secondSniffer.read();

  SniffLine lineA{&inverter, "PAD TX -> GPIO33"};
  SniffLine lineB{&secondSniffer, "PAD RX -> GPIO32"};
  const uint32_t startedAt = millis();
  const uint32_t frameGapMs = baud <= 2400 ? 25 : 10;

  Serial.println();
  Serial.println("================ PODSLUCH DONGLA ================");
  Serial.printf("Predkosc: %lu 8N1, czas: %lu s\n",
                static_cast<unsigned long>(baud),
                static_cast<unsigned long>(kSniffDurationMs / 1000));
  Serial.println("ESP tylko slucha. Dongle ma byc normalnie zasilony i polaczony z falownikiem.");
  Serial.println("GPIO33 <- pad TX, GPIO32 <- pad RX, GND <-> GND; bez MAX3232 i bez 3.3V.");

  while (millis() - startedAt < kSniffDurationMs) {
    const uint32_t now = millis();
    pollSniffLine(lineA, now);
    pollSniffLine(lineB, now);
    if ((lineA.length || lineA.overflow) && now - lineA.lastByteAt >= frameGapMs) flushSniffLine(lineA, startedAt);
    if ((lineB.length || lineB.overflow) && now - lineB.lastByteAt >= frameGapMs) flushSniffLine(lineB, startedAt);
    delay(1);
  }
  flushSniffLine(lineA, startedAt);
  flushSniffLine(lineB, startedAt);
  Serial.println("================ KONIEC PODSLUCHU ================");
  Serial.println("Wpisz s dla 2400 baud, 9 dla 9600 baud albo r dla aktywnego testu RS232.");
}

bool loopbackAt(uint32_t baud) {
  configureUart(baud);
  const uint8_t pattern[] = {0x55, 0xAA, 0x00, 0xFF, 0x11, 0x22, 0x33, 0xCC};
  Serial.println();
  Serial.print("MAX3232 loopback @ ");
  Serial.print(baud);
  Serial.println(" 8N1");
  printBytes("TX", pattern, sizeof(pattern));
  inverter.write(pattern, sizeof(pattern));
  inverter.flush();
  const Capture capture = captureResponse(1000, 120);
  if (capture.length) printBytes("RX", capture.data, capture.length);
  const bool ok = capture.length == sizeof(pattern) && memcmp(capture.data, pattern, sizeof(pattern)) == 0;
  Serial.println(ok ? "WYNIK LOOPBACK: OK" : "WYNIK LOOPBACK: BLAD");
  return ok;
}

void runLoopbackTest() {
  Serial.println();
  Serial.println("================ TEST MAX3232 ================");
  Serial.println("Odlacz RJ45 od falownika i zewrzyj T1OUT z R1IN po stronie RS232.");
  Serial.println("Nie zwieraj zasilania ani pinow po stronie TTL.");
  const bool ok2400 = loopbackAt(2400);
  const bool ok9600 = loopbackAt(9600);
  Serial.println();
  Serial.println(ok2400 && ok9600
                     ? "MAX3232 i UART dzialaja w obu kierunkach."
                     : "Tor ESP32/MAX3232 nie przeszedl testu; sprawdz T1IN/T1OUT/R1IN/R1OUT, VCC i GND.");
  Serial.println("================================================");
}

void printSummary() {
  Serial.println();
  Serial.println("================ PODSUMOWANIE ================");
  bool confirmed = false;
  bool traffic = false;
  for (size_t index = 0; index < resultCount; ++index) {
    Serial.print(results[index].kind == ResultKind::Valid ? "[OK]   " :
                 results[index].kind == ResultKind::Traffic ? "[BAJTY]" : "[CISZA]");
    Serial.print(' ');
    Serial.print(results[index].name);
    Serial.print("  (");
    Serial.print(results[index].bytes);
    Serial.println(" B)");
    confirmed |= results[index].kind == ResultKind::Valid;
    traffic |= results[index].kind == ResultKind::Traffic;
  }
  Serial.println();
  if (confirmed) {
    Serial.println("Mamy potwierdzony protokol. Skopiuj caly raport do rozmowy.");
  } else if (traffic) {
    Serial.println("Jest odpowiedz, ale parser jej nie rozpoznal. Skopiuj caly raport do rozmowy.");
  } else {
    Serial.println("Brak odpowiedzi. Sprawdz RX/TX, GND, MAX3232 i pinout RJ45.");
    Serial.println("Nie zamieniaj GPIO po stronie TTL bez sprawdzenia kierunku kanalow MAX3232.");
  }
  Serial.println("Wpisz r i Enter, aby powtorzyc test.");
  Serial.println("Wpisz s dla podsluchu obu pol TTL dongla @ 2400 lub 9 dla 9600.");
  Serial.println("================================================");
}

void runAllTests() {
  resultCount = 0;
  Serial.println();
  Serial.println("================================================");
  Serial.println("ANENJI RS232 PROBE - tylko komendy odczytowe");
  Serial.printf("ESP RX=%d, TX=%d, USB monitor=115200\n", kInverterRxPin, kInverterTxPin);
  Serial.println("Dongle WiFi ma byc wypiety. ESP laczy sie przez MAX3232.");
  Serial.println("================================================");

  passiveListen(9600, 900);
  passiveListen(2400, 900);

  // Najbardziej prawdopodobny wariant WiFi Plug Pro / SmartESS 2341.
  // Najpierw pojedynczy, potwierdzony rejestr 4502 przy referencyjnych 2400 baud.
  sendModbusRead(2400, 5, 4502, 1);
  sendModbusRead(2400, 5, 4502, 13);
  // Dalsze proby uwzgledniaja rozne interpretacje poczatku mapy rejestrow.
  sendModbusRead(9600, 5, 4500, 15);
  sendModbusRead(9600, 5, 4501, 15);
  sendModbusRead(2400, 5, 4500, 15);
  sendModbusRead(2400, 5, 4501, 15);

  // Niektore klony zachowuja domyslny adres Modbus 1.
  sendModbusRead(9600, 1, 4500, 15);

  // Druga spotykana rodzina WiFi Plug Pro.
  send8851(9600, false);
  send8851(9600, true);

  // Tekstowa rodzina Voltronic/PIP/Sumry.
  sendPip(2400, "QPIGS");
  sendPip(2400, "QMOD");
  sendPip(9600, "QPIGS");

  printSummary();
}
}

void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.println();
  Serial.println("ANENJI PROBE - tryb bezpiecznego oczekiwania (USB 115200)");
  Serial.println("s = bierny podsluch obu pol TTL dongla @ 2400 przez 20 s");
  Serial.println("9 = bierny podsluch obu pol TTL dongla @ 9600 przez 20 s");
  Serial.println("r = aktywne zapytania do falownika przez MAX3232 (dongle wypiety)");
  Serial.println("l = test petli MAX3232 (falownik i dongle wypiete)");
  Serial.println("ESP niczego nie nada, dopoki nie wybierzesz polecenia.");
}

void loop() {
  if (!Serial.available()) {
    delay(20);
    return;
  }
  const char command = static_cast<char>(Serial.read());
  while (Serial.available()) Serial.read();
  if (command == 'r' || command == 'R') runAllTests();
  if (command == 'l' || command == 'L') runLoopbackTest();
  if (command == 's' || command == 'S') dualPassiveSniff(2400);
  if (command == '9') dualPassiveSniff(9600);
}
