#include <Arduino.h>
#include <string.h>

#include "drivers/AnenjiProtocol.h"

namespace {
constexpr uint32_t kBaud = 9600;
constexpr int kDefaultRxPin = 33;  // ESP32 RX <- MAX3232 R1OUT
constexpr int kDefaultTxPin = 32;  // ESP32 TX -> MAX3232 T1IN
constexpr uint32_t kRxTimeoutMs = 800;
constexpr uint32_t kRxIdleMs = 40;
constexpr uint32_t kInterQueryMs = 40;
constexpr uint32_t kWakeupSettleMs = 200;
constexpr uint32_t kPassiveListenMs = 400;
constexpr uint32_t kSniffDurationMs = 40000;
constexpr uint32_t kLoopbackTimeoutMs = 1000;
constexpr uint32_t kDongleCycles = 3;
constexpr size_t kMaxResponse = 512;
constexpr size_t kMaxResults = 96;
constexpr size_t kMaxRegs = 32;

HardwareSerial inverter(2);
HardwareSerial secondSniffer(1);

volatile uint32_t uartBreakErrors = 0;
volatile uint32_t uartBufferFullErrors = 0;
volatile uint32_t uartFifoOverflowErrors = 0;
volatile uint32_t uartFrameErrors = 0;
volatile uint32_t uartParityErrors = 0;

struct UartConfig {
  int rxPin;
  int txPin;
  bool invert;
  const char* name;
};

struct Capture {
  uint8_t data[kMaxResponse]{};
  size_t length = 0;
  bool overflow = false;
  uint32_t firstByteMs = 0;
};

enum class ResultKind : uint8_t { Silence, Echo, Exception, Junk, Valid };

struct TestResult {
  String name;
  ResultKind kind = ResultKind::Silence;
  size_t bytes = 0;
};

struct RegRead {
  uint16_t start;
  uint16_t count;
  const char* name;
};

constexpr UartConfig kDefaultUart = {kDefaultRxPin, kDefaultTxPin, false,
                                     "RX33/TX32 invert=off"};

constexpr RegRead kHelloRead = {AnenjiProtocol::kFaultFirstRegister, AnenjiProtocol::kFaultRegisterCount,
                                "hello 100/3"};
constexpr RegRead kIdentityRead = {AnenjiProtocol::kIdentityFirstRegister,
                                   AnenjiProtocol::kIdentityRegisterCount, "identity 171/28"};
constexpr RegRead kLiveRead = {AnenjiProtocol::kLiveFirstRegister, AnenjiProtocol::kLiveRegisterCount,
                               "live 200/22"};
constexpr RegRead kStatusRead = {AnenjiProtocol::kStatusFirstRegister, AnenjiProtocol::kStatusRegisterCount,
                                 "status 223/13"};

constexpr RegRead kNoWakeReads[] = {kHelloRead, kIdentityRead, kLiveRead, kStatusRead};

constexpr RegRead kDumpCycle[] = {
    {100, 3, "fault 100/3"},
    {104, 1, "104/1"},
    {106, 1, "106/1"},
    {108, 3, "warning 108/3"},
    {171, 28, "identity 171/28"},
    {200, 22, "live 200/22"},
    {223, 13, "status 223/13"},
    {300, 12, "output 300/12"},
    {313, 5, "313/5"},
    {320, 20, "nameplate 320/20"},
    {641, 5, "rated 641/5"},
};

TestResult results[kMaxResults];
size_t resultCount = 0;

const char* kindLabel(ResultKind kind) {
  switch (kind) {
    case ResultKind::Valid:
      return "[OK]   ";
    case ResultKind::Exception:
      return "[WYJAT]";
    case ResultKind::Echo:
      return "[ECHO] ";
    case ResultKind::Junk:
      return "[BAJTY]";
    case ResultKind::Silence:
    default:
      return "[CISZA]";
  }
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
}

void printMenu() {
  Serial.println("l = test toru ESP/MAX3232/kabla (falownik i dongle wypiete, zwora)");
  Serial.println("r = test polaczenia z falownikiem na poprawnym torze RX33/TX32");
  Serial.println("d = klon dongla: cykl FC03 ze sniffu, 3 obroty, bez wakeup");
  Serial.println("9 = bierny podsluch obu pol TTL dongla @ 9600 FC03");
}

void resetUartErrors() {
  uartBreakErrors = 0;
  uartBufferFullErrors = 0;
  uartFifoOverflowErrors = 0;
  uartFrameErrors = 0;
  uartParityErrors = 0;
}

void onUartError(hardwareSerial_error_t error) {
  switch (error) {
    case UART_BREAK_ERROR:
      ++uartBreakErrors;
      break;
    case UART_BUFFER_FULL_ERROR:
      ++uartBufferFullErrors;
      break;
    case UART_FIFO_OVF_ERROR:
      ++uartFifoOverflowErrors;
      break;
    case UART_FRAME_ERROR:
      ++uartFrameErrors;
      break;
    case UART_PARITY_ERROR:
      ++uartParityErrors;
      break;
    case UART_NO_ERROR:
    default:
      break;
  }
}

void printUartErrors() {
  const uint32_t total = uartBreakErrors + uartBufferFullErrors + uartFifoOverflowErrors +
                         uartFrameErrors + uartParityErrors;
  if (!total) return;
  Serial.printf("UART ERR: frame=%lu parity=%lu break=%lu fifo=%lu buffer=%lu\n",
                static_cast<unsigned long>(uartFrameErrors),
                static_cast<unsigned long>(uartParityErrors),
                static_cast<unsigned long>(uartBreakErrors),
                static_cast<unsigned long>(uartFifoOverflowErrors),
                static_cast<unsigned long>(uartBufferFullErrors));
}

void remember(const String& name, ResultKind kind, size_t bytes) {
  if (resultCount >= kMaxResults) return;
  results[resultCount++] = {name, kind, bytes};
}

void openUart(const UartConfig& config) {
  inverter.end();
  delay(50);
  inverter.setRxBufferSize(kMaxResponse * 2);
  inverter.begin(kBaud, SERIAL_8N1, config.rxPin, config.txPin, config.invert);
  inverter.onReceiveError(onUartError);
  delay(80);
  while (inverter.available()) inverter.read();
  resetUartErrors();
}

void flushRx() {
  while (inverter.available()) inverter.read();
}

Capture captureResponse(uint32_t timeoutMs = kRxTimeoutMs, uint32_t idleMs = kRxIdleMs) {
  Capture capture;
  const uint32_t startedAt = millis();
  uint32_t lastByteAt = startedAt;
  bool started = false;

  while (millis() - startedAt < timeoutMs) {
    while (inverter.available()) {
      const int value = inverter.read();
      if (value < 0) break;
      if (!started) {
        capture.firstByteMs = millis() - startedAt;
        started = true;
      }
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

bool bytesEqual(const uint8_t* left, size_t leftLength, const uint8_t* right, size_t rightLength) {
  return left && right && leftLength == rightLength && memcmp(left, right, leftLength) == 0;
}

bool isAllValue(const Capture& capture, uint8_t value) {
  if (!capture.length) return false;
  for (size_t index = 0; index < capture.length; ++index) {
    if (capture.data[index] != value) return false;
  }
  return true;
}

bool containsPattern(const Capture& capture, const uint8_t* pattern, size_t patternLength) {
  if (capture.length < patternLength) return false;
  for (size_t offset = 0; offset + patternLength <= capture.length; ++offset) {
    if (memcmp(capture.data + offset, pattern, patternLength) == 0) return true;
  }
  return false;
}

bool looksLikeShiftedFc03(const Capture& capture) {
  return capture.length >= 2 && capture.data[0] == 0x00 && capture.data[1] == 0x02;
}

const char* garbageHint(const Capture& capture) {
  static const uint8_t kWakeJunk[] = {0xFE, 0x00, 0xFC, 0x00};
  if (looksLikeShiftedFc03(capture)) {
    return "start 00 02 ~= przesuniete 01 03 (blad ramki / zaklocenia, jak w terenie)";
  }
  if (isAllValue(capture, 0xFF) || capture.data[0] == 0xFF) {
    return "FF na starcie — jak po wakeup-first w terenie";
  }
  if (isAllValue(capture, 0x00)) {
    return "same zera — jak po wakeup-first w terenie";
  }
  if (containsPattern(capture, kWakeJunk, sizeof(kWakeJunk))) {
    return "FE 00 FC 00 — jak po wakeup-first w terenie";
  }
  return "format/CRC nie zgadza sie z FC03 slave 1";
}

bool hasExceptionFrame(const Capture& capture) {
  if (capture.length < 5) return false;
  for (size_t offset = 0; offset + 5 <= capture.length; ++offset) {
    if (capture.data[offset] == AnenjiProtocol::kSlave && capture.data[offset + 1] == 0x83 &&
        AnenjiProtocol::crcValid(capture.data + offset, 5)) {
      return true;
    }
  }
  return false;
}

bool anyValidFc03(const Capture& capture) {
  if (capture.length < 5) return false;
  for (size_t offset = 0; offset + 5 <= capture.length; ++offset) {
    if (capture.data[offset] != AnenjiProtocol::kSlave) continue;
    if (capture.data[offset + 1] != AnenjiProtocol::kReadHolding) continue;
    const uint8_t byteCount = capture.data[offset + 2];
    if (byteCount < 2 || (byteCount % 2U) != 0) continue;
    const size_t frameLength = 5U + static_cast<size_t>(byteCount);
    if (offset + frameLength > capture.length) continue;
    if (AnenjiProtocol::crcValid(capture.data + offset, frameLength)) return true;
  }
  return false;
}

ResultKind classifyRx(const uint8_t* tx, size_t txLength, const Capture& capture, uint16_t expectedCount) {
  if (!capture.length) return ResultKind::Silence;
  if (tx && txLength && bytesEqual(capture.data, capture.length, tx, txLength)) return ResultKind::Echo;
  if (expectedCount > 0 && expectedCount <= kMaxRegs) {
    uint16_t registers[kMaxRegs]{};
    if (AnenjiProtocol::extractReadHoldingResponse(capture.data, capture.length, AnenjiProtocol::kSlave,
                                                   expectedCount, registers, kMaxRegs)) {
      return ResultKind::Valid;
    }
  } else if (anyValidFc03(capture)) {
    return ResultKind::Valid;
  }
  if (hasExceptionFrame(capture)) return ResultKind::Exception;
  return ResultKind::Junk;
}

void printDecoded(uint16_t firstRegister, uint16_t registerCount, const Capture& capture) {
  uint16_t registers[kMaxRegs]{};
  if (!AnenjiProtocol::extractReadHoldingResponse(capture.data, capture.length, AnenjiProtocol::kSlave,
                                                  registerCount, registers, kMaxRegs)) {
    return;
  }
  if (firstRegister == AnenjiProtocol::kLiveFirstRegister &&
      registerCount == AnenjiProtocol::kLiveRegisterCount) {
    AnenjiProtocol::LiveReading live{};
    if (!AnenjiProtocol::decodeLiveBlock(registers, registerCount, live)) return;
    Serial.printf("DEKOD live: load=%.0fW Vac=%.1f Vbat=%.1f Ibat=%.1f Vpv=%.1f mostek=%.0fW %s\n",
                  live.loadPowerW, live.outputVoltageV, live.batteryVoltageV, live.batteryCurrentA,
                  live.pvVoltageV, live.inverterPowerW, AnenjiProtocol::operationModeName(live.operationMode));
  } else if (firstRegister == AnenjiProtocol::kStatusFirstRegister &&
             registerCount == AnenjiProtocol::kStatusRegisterCount) {
    AnenjiProtocol::LiveReading live{};
    if (!AnenjiProtocol::decodeStatusBlock(registers, registerCount, live)) return;
    Serial.printf("DEKOD status: PV=%.0fW load=%u%%\n", live.pvPowerW, live.loadPercent);
  }
}

void showCapture(const String& name, const Capture& capture, ResultKind kind, uint16_t firstRegister,
                 uint16_t registerCount) {
  Serial.println();
  Serial.print("--- ");
  Serial.print(name);
  Serial.print(" @ ");
  Serial.print(kBaud);
  Serial.println(" 8N1 ---");
  if (!capture.length) {
    Serial.println("RX: cisza");
    printUartErrors();
    remember(name, ResultKind::Silence, 0);
    return;
  }
  printBytes("RX", capture.data, capture.length);
  printUartErrors();
  if (capture.overflow) Serial.println("UWAGA: odpowiedz dluzsza niz bufor; koniec zostal uciety.");
  Serial.print("czas TX->RX: ");
  Serial.print(capture.firstByteMs);
  Serial.println(" ms (dongle ~14 ms)");
  switch (kind) {
    case ResultKind::Valid:
      Serial.println("WYNIK: FC03 slave 1 CRC OK");
      printDecoded(firstRegister, registerCount, capture);
      break;
    case ResultKind::Exception:
      Serial.println("WYNIK: wyjatek Modbus 0x83");
      break;
    case ResultKind::Echo:
      Serial.println("WYNIK: echo TX — zwora loopback albo zwarcie RJ45 pin 1-2");
      break;
    case ResultKind::Junk:
      Serial.print("WYNIK: smieci — ");
      Serial.println(garbageHint(capture));
      break;
    case ResultKind::Silence:
      break;
  }
  remember(name, kind, capture.length);
}

void queryFc03(const UartConfig& uart, const RegRead& read, bool reopen) {
  uint8_t request[AnenjiProtocol::kRequestSize]{};
  if (!AnenjiProtocol::buildReadHolding(request, sizeof(request), AnenjiProtocol::kSlave, read.start,
                                        read.count)) {
    Serial.println("blad budowy FC03");
    return;
  }
  if (reopen) openUart(uart);
  else flushRx();
  resetUartErrors();

  const String name = String(uart.name) + " " + read.name;
  Serial.println();
  Serial.print("TX ");
  Serial.println(name);
  printBytes("TX", request, sizeof(request));
  inverter.write(request, sizeof(request));
  inverter.flush();
  const Capture capture = captureResponse();
  showCapture(name, capture, classifyRx(request, sizeof(request), capture, read.count), read.start,
              read.count);
  delay(kInterQueryMs);
}

ResultKind queryFc03Kind(const UartConfig& uart, const RegRead& read, bool reopen) {
  const size_t before = resultCount;
  queryFc03(uart, read, reopen);
  if (resultCount == before) return ResultKind::Silence;
  return results[resultCount - 1].kind;
}

void sendWakeup(const UartConfig& uart, bool reopen) {
  if (reopen) openUart(uart);
  else flushRx();
  Serial.println();
  Serial.print("TX wakeup 01 AA (esphome USB-A; dongle RJ45 tego nie wysyla) @ ");
  Serial.println(uart.name);
  printBytes("TX", AnenjiProtocol::kWakeupFrame, AnenjiProtocol::kWakeupSize);
  inverter.write(AnenjiProtocol::kWakeupFrame, AnenjiProtocol::kWakeupSize);
  inverter.flush();
  delay(kWakeupSettleMs);
  const Capture capture = captureResponse(250, kRxIdleMs);
  const String name = String(uart.name) + " wakeup 01 AA";
  showCapture(name, capture, classifyRx(AnenjiProtocol::kWakeupFrame, AnenjiProtocol::kWakeupSize, capture, 0),
              0, 0);
}

void listenPassive(const UartConfig& uart) {
  openUart(uart);
  resetUartErrors();
  Serial.println();
  Serial.print("--- pasywny nasluch ");
  Serial.print(uart.name);
  Serial.print(" przez ");
  Serial.print(kPassiveListenMs);
  Serial.println(" ms ---");
  const Capture capture = captureResponse(kPassiveListenMs, kRxIdleMs);
  showCapture(String(uart.name) + " nasluch", capture, classifyRx(nullptr, 0, capture, 0), 0, 0);
}

void runReadList(const UartConfig& uart, const RegRead* reads, size_t count, bool reopenFirst) {
  for (size_t index = 0; index < count; ++index) {
    queryFc03(uart, reads[index], reopenFirst && index == 0);
  }
}

void tryLiveStatus(const UartConfig& uart) {
  queryFc03(uart, kLiveRead, false);
  queryFc03(uart, kStatusRead, false);
}

bool loopbackOnce(const UartConfig& uart, const char* label, const uint8_t* pattern, size_t length) {
  openUart(uart);
  Serial.println();
  Serial.print("loopback ");
  Serial.print(label);
  Serial.print(" @ ");
  Serial.println(uart.name);
  printBytes("TX", pattern, length);
  flushRx();
  resetUartErrors();
  inverter.write(pattern, length);
  inverter.flush();
  const Capture capture = captureResponse(kLoopbackTimeoutMs, kRxIdleMs);
  if (capture.length) printBytes("RX", capture.data, capture.length);
  else Serial.println("RX: cisza");
  printUartErrors();
  const bool ok = bytesEqual(capture.data, capture.length, pattern, length);
  Serial.println(ok ? "WYNIK LOOPBACK: OK" : "WYNIK LOOPBACK: BLAD");
  return ok;
}

bool loopbackConfig(const UartConfig& uart) {
  const uint8_t pattern[] = {0x55, 0xAA, 0x00, 0xFF, 0x11, 0x22, 0x33, 0xCC};
  uint8_t hello[AnenjiProtocol::kRequestSize]{};
  AnenjiProtocol::buildReadHolding(hello, sizeof(hello), AnenjiProtocol::kSlave,
                                   AnenjiProtocol::kFaultFirstRegister, AnenjiProtocol::kFaultRegisterCount);
  const bool patternOk = loopbackOnce(uart, "wzorzec 55 AA", pattern, sizeof(pattern));
  const bool frameOk = loopbackOnce(uart, "FC03 100/3", hello, sizeof(hello));
  return patternOk && frameOk;
}

void runLoopbackTest() {
  Serial.println();
  Serial.println("================ TEST TORU MAX3232 / KABLA ================");
  Serial.println("Falownik i dongle MUSZA byc wypiete. USB monitor 115200.");
  Serial.println("Nie zwieraj zasilania ani pinow TTL (R1OUT/T1IN).");
  Serial.println();
  Serial.println("Uruchom `l` dwa razy, ze zmiana zwory miedzy przebiegami:");
  Serial.println("  A. Chip: RJ45 odlaczony, zewrzyj T1OUT z R1IN na module MAX3232.");
  Serial.println("  B. Kabel: wtyk RJ45 w MAX3232, na drugim koncu zewrzyj pin 1 z pin 2.");
  Serial.println("Predkosc tylko 9600 8N1 (potwierdzona). Invert nie jest testowany w loopbacku.");
  Serial.println("===========================================================");

  const bool defaultOk = loopbackConfig(kDefaultUart);

  Serial.println();
  Serial.println("================ WYNIK TORU ================");
  if (defaultOk) {
    Serial.println("TTL zgodne z firmware: R1OUT->GPIO33, T1IN<-GPIO32.");
    Serial.println("Jesli to przebieg A: chip MAX3232 i UART ESP dzialaja.");
    Serial.println("Jesli to przebieg B: kabel RJ45 pin 1/2 tez przepuszcza oba kierunki.");
  } else {
    Serial.println("Brak petli na poprawnym torze RX33/TX32.");
    Serial.println("Przebieg A fail: ESP, VCC 3.3 V, GND, kondensatory charge-pump, T1OUT-R1IN.");
    Serial.println("Przebieg B fail przy A OK: przerwa w kablu albo zly pin na wtyku RJ45.");
  }
  Serial.println("================================================");
  printMenu();
}

void printDiagnosis() {
  bool confirmed = false;
  bool echo = false;
  bool junk = false;
  bool traffic = false;
  for (size_t index = 0; index < resultCount; ++index) {
    confirmed |= results[index].kind == ResultKind::Valid;
    echo |= results[index].kind == ResultKind::Echo;
    junk |= results[index].kind == ResultKind::Junk;
    traffic |= results[index].kind != ResultKind::Silence;
  }
  Serial.println();
  if (confirmed) {
    Serial.println("DIAGNOZA: mamy poprawne FC03. Skopiuj PODSUMOWANIE i DEKOD. Mozesz odpalic d.");
  } else if (echo && !junk) {
    Serial.println("DIAGNOZA: echo TX — zostawiona zwora loopback. Zdejmij ja i wpinaj falownik.");
  } else if (junk) {
    Serial.println("DIAGNOZA: falownik gada, ale UART nie sklada FC03.");
    Serial.println("Sprawdz UART ERR, mase, poziomy RS232 i prowadzenie kabla. Nie wlaczaj invert przez MAX3232.");
    Serial.println("Najpierw `l` (chip, potem kabel), potem `r` jeszcze raz.");
  } else if (traffic) {
    Serial.println("DIAGNOZA: sa bajty, ale nie FC03. Skopiuj caly raport.");
  } else {
    Serial.println("DIAGNOZA: cisza. Zrob `l` (chip A, kabel B), sprawdz mase i 3.3 V MAX3232.");
    Serial.println("Loopback nie sprawdza kierunkow. T1OUT MAX ma isc do wejscia falownika, TX falownika do R1IN.");
  }
}

void printSummary() {
  Serial.println();
  Serial.println("================ PODSUMOWANIE ================");
  if (!resultCount) {
    Serial.println("(brak probek)");
  }
  for (size_t index = 0; index < resultCount; ++index) {
    Serial.print(kindLabel(results[index].kind));
    Serial.print(' ');
    Serial.print(results[index].name);
    Serial.print("  (");
    Serial.print(results[index].bytes);
    Serial.println(" B)");
  }
  printDiagnosis();
  Serial.println("Wpisz r / d / l / 9.");
  Serial.println("================================================");
}

void runDataVariants(const UartConfig& uart) {
  Serial.println();
  Serial.print("=== warianty danych @ ");
  Serial.print(uart.name);
  Serial.println(" ===");

  Serial.println("-- bez wakeup (jak dongle) --");
  openUart(uart);
  runReadList(uart, kNoWakeReads, sizeof(kNoWakeReads) / sizeof(kNoWakeReads[0]), false);

  Serial.println("-- z wakeup 01 AA (esphome USB-A) --");
  sendWakeup(uart, false);
  queryFc03(uart, kHelloRead, false);

  Serial.println("-- cykl dump (znane dlugosci ze sniffu) --");
  runReadList(uart, kDumpCycle, sizeof(kDumpCycle) / sizeof(kDumpCycle[0]), false);
}

void runConnectionTest() {
  resultCount = 0;
  Serial.println();
  Serial.println("================================================");
  Serial.println("ANENJI PROBE - pelny test polaczenia");
  Serial.println("Format: Modbus RTU 9600 8N1 slave 1 wylacznie FC03");
  Serial.println("Dongle wypiety. MAX3232 3.3 V. T1OUT->wejscie falownika, wyjscie falownika->R1IN.");
  Serial.printf("Domyslnie RX=%d TX=%d (jak firmware)\n", kDefaultRxPin, kDefaultTxPin);
  Serial.println("================================================");

  Serial.println();
  Serial.println("=== jedyny poprawny tor RX33/TX32 invert=off ===");
  listenPassive(kDefaultUart);
  const ResultKind helloKind = queryFc03Kind(kDefaultUart, kHelloRead, false);
  if (helloKind == ResultKind::Valid) {
    Serial.println("hello OK — czytam live 200/22 i status 223/13");
    tryLiveStatus(kDefaultUart);
  }

  runDataVariants(kDefaultUart);

  printSummary();
}

void runDumpCycles(const UartConfig& uart, uint32_t cycles) {
  openUart(uart);
  for (uint32_t cycle = 1; cycle <= cycles; ++cycle) {
    Serial.println();
    Serial.print("=== cykl dongla ");
    Serial.print(cycle);
    Serial.print('/');
    Serial.print(cycles);
    Serial.print(" @ ");
    Serial.print(uart.name);
    Serial.println(" ===");
    runReadList(uart, kDumpCycle, sizeof(kDumpCycle) / sizeof(kDumpCycle[0]), false);
  }
}

void runDongleClone() {
  resultCount = 0;
  Serial.println();
  Serial.println("================================================");
  Serial.println("ANENJI PROBE - klon dongla (sniff WiFi Plug Pro)");
  Serial.println("9600 8N1 slave 1 FC03, RX=33 TX=32, bez invert, bez wakeup");
  Serial.println("Dongle wypiety. Sprawdz kierunki RS232; loopback nie wykrywa ich zamiany.");
  Serial.println("================================================");

  const ResultKind first = queryFc03Kind(kDefaultUart, kHelloRead, true);
  if (first == ResultKind::Junk || first == ResultKind::Silence) {
    Serial.println("Pierwszy 100/3 nie jest poprawnym FC03; pozostaje ten sam fizyczny tor.");
  }

  runDumpCycles(kDefaultUart, kDongleCycles);
  printSummary();
}

void classifySniffFrame(const uint8_t* data, size_t length) {
  if (length >= 8 && data[0] == AnenjiProtocol::kSlave && data[1] == AnenjiProtocol::kReadHolding &&
      length == AnenjiProtocol::kRequestSize && AnenjiProtocol::crcValid(data, length)) {
    const uint16_t start = (static_cast<uint16_t>(data[2]) << 8U) | data[3];
    const uint16_t count = (static_cast<uint16_t>(data[4]) << 8U) | data[5];
    Serial.printf("  zapytanie FC03 CRC OK start=%u count=%u\n", start, count);
    return;
  }
  if (length >= 5 && data[0] == AnenjiProtocol::kSlave && data[1] == AnenjiProtocol::kReadHolding &&
      AnenjiProtocol::crcValid(data, length)) {
    Serial.printf("  odpowiedz FC03 CRC OK byteCount=%u\n", data[2]);
    return;
  }
  if (length >= 5 && data[0] == AnenjiProtocol::kSlave && data[1] == 0x83 &&
      AnenjiProtocol::crcValid(data, length)) {
    Serial.println("  wyjatek FC83 CRC OK");
    return;
  }
  Capture fake;
  fake.length = length > sizeof(fake.data) ? sizeof(fake.data) : length;
  memcpy(fake.data, data, fake.length);
  Serial.print("  nie FC03 / zly CRC — ");
  Serial.println(garbageHint(fake));
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
  classifySniffFrame(line.data, line.length);
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

void dualPassiveSniff() {
  inverter.end();
  secondSniffer.end();
  delay(80);

  inverter.setRxBufferSize(1024);
  secondSniffer.setRxBufferSize(1024);
  inverter.begin(kBaud, SERIAL_8N1, kDefaultRxPin, -1);
  secondSniffer.begin(kBaud, SERIAL_8N1, kDefaultTxPin, -1);
  delay(120);
  while (inverter.available()) inverter.read();
  while (secondSniffer.available()) secondSniffer.read();

  SniffLine lineA{&inverter, "PAD TX -> GPIO33"};
  SniffLine lineB{&secondSniffer, "PAD RX -> GPIO32"};
  const uint32_t startedAt = millis();
  constexpr uint32_t kFrameGapMs = 10;

  Serial.println();
  Serial.println("================ PODSLUCH DONGLA ================");
  Serial.printf("Predkosc: %lu 8N1, czas: %lu s, parser FC03 slave 1\n",
                static_cast<unsigned long>(kBaud), static_cast<unsigned long>(kSniffDurationMs / 1000));
  Serial.println("ESP tylko slucha. Dongle zasilony i w pincie falownika.");
  Serial.println("GPIO33 <- pad TX, GPIO32 <- pad RX, GND <-> GND; bez MAX3232 i bez 3.3V.");

  while (millis() - startedAt < kSniffDurationMs) {
    const uint32_t now = millis();
    pollSniffLine(lineA, now);
    pollSniffLine(lineB, now);
    if ((lineA.length || lineA.overflow) && now - lineA.lastByteAt >= kFrameGapMs) {
      flushSniffLine(lineA, startedAt);
    }
    if ((lineB.length || lineB.overflow) && now - lineB.lastByteAt >= kFrameGapMs) {
      flushSniffLine(lineB, startedAt);
    }
    delay(1);
  }
  flushSniffLine(lineA, startedAt);
  flushSniffLine(lineB, startedAt);
  Serial.println("================ KONIEC PODSLUCHU ================");
  printMenu();
}
}

void setup() {
  Serial.begin(115200);
  // Uruchom UART od razu: TX GPIO32 ma stan idle HIGH, wiec T1OUT MAX3232
  // pozostaje ujemny także przed wybraniem polecenia z menu.
  openUart(kDefaultUart);
  delay(1000);
  Serial.println();
  Serial.println("ANENJI PROBE - oczekiwanie (USB 115200), tylko FC03 @ 9600");
  Serial.println("UART falownika aktywny od startu: GPIO32 TX idle=HIGH, MAX T1OUT powinien byc ujemny.");
  printMenu();
  Serial.println("ESP niczego nie nada, dopoki nie wybierzesz polecenia.");
}

void loop() {
  if (!Serial.available()) {
    delay(20);
    return;
  }
  const char command = static_cast<char>(Serial.read());
  while (Serial.available()) Serial.read();
  if (command == 'r' || command == 'R') runConnectionTest();
  else if (command == 'l' || command == 'L') runLoopbackTest();
  else if (command == 'd' || command == 'D') runDongleClone();
  else if (command == '9') dualPassiveSniff();
  else if (command == 's' || command == 'S') {
    Serial.println("2400 jest bledne (cisza w terenie). Uzyj 9 dla podsluchu @ 9600.");
  }
}
