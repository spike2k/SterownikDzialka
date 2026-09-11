#include "services/OtaUpdater.h"

#include <HTTPClient.h>
#include <Update.h>
#include <WiFiClientSecure.h>
#include <mbedtls/sha256.h>

#include "AppConfig.h"
#include "TlsCertificates.h"

namespace {
constexpr uint32_t kDownloadTimeoutMs = 15000;
constexpr size_t kDownloadBufferBytes = 4096;

String sha256Hex(const unsigned char hash[32]) {
  static constexpr char digits[] = "0123456789abcdef";
  char text[65];
  for (size_t index = 0; index < 32; ++index) {
    text[index * 2] = digits[hash[index] >> 4];
    text[index * 2 + 1] = digits[hash[index] & 0x0f];
  }
  text[64] = '\0';
  return String(text);
}
}

bool OtaUpdater::install(const char* expectedSha256, String& error) {
  return installFrom(Config::otaFirmwareUrl, expectedSha256, error);
}

bool OtaUpdater::installFrom(const char* firmwareUrl, const char* expectedSha256, String& error) {
  WiFiClientSecure tlsClient;
  tlsClient.setCACert(TlsCertificates::letsEncryptRootX1);
  tlsClient.setHandshakeTimeout(15);

  HTTPClient request;
  request.setConnectTimeout(kDownloadTimeoutMs);
  request.setTimeout(kDownloadTimeoutMs);
  if (!request.begin(tlsClient, firmwareUrl)) {
    error = "nie mozna otworzyc adresu HTTPS";
    return false;
  }

  const int responseCode = request.GET();
  if (responseCode != HTTP_CODE_OK) {
    error = "HTTP " + String(responseCode);
    request.end();
    return false;
  }

  const int firmwareSize = request.getSize();
  if (firmwareSize <= 0) {
    error = "serwer nie podal rozmiaru firmware";
    request.end();
    return false;
  }
  if (!Update.begin(static_cast<size_t>(firmwareSize), U_FLASH)) {
    error = Update.errorString();
    request.end();
    return false;
  }

  mbedtls_sha256_context shaContext;
  mbedtls_sha256_init(&shaContext);
  if (mbedtls_sha256_starts_ret(&shaContext, 0) != 0) {
    error = "nie mozna uruchomic SHA-256";
    Update.abort();
    request.end();
    mbedtls_sha256_free(&shaContext);
    return false;
  }

  WiFiClient* stream = request.getStreamPtr();
  uint8_t buffer[kDownloadBufferBytes];
  size_t totalWritten = 0;
  uint32_t lastDataMs = millis();
  bool downloadOk = true;

  while (totalWritten < static_cast<size_t>(firmwareSize)) {
    const size_t available = stream->available();
    if (available == 0) {
      if (millis() - lastDataMs >= kDownloadTimeoutMs) {
        error = "timeout pobierania";
        downloadOk = false;
        break;
      }
      delay(2);
      continue;
    }

    const size_t remaining = static_cast<size_t>(firmwareSize) - totalWritten;
    const size_t requested = min(available, min(remaining, sizeof(buffer)));
    const int bytesRead = stream->readBytes(buffer, requested);
    if (bytesRead <= 0) continue;
    lastDataMs = millis();

    if (mbedtls_sha256_update_ret(&shaContext, buffer, static_cast<size_t>(bytesRead)) != 0 ||
        Update.write(buffer, static_cast<size_t>(bytesRead)) != static_cast<size_t>(bytesRead)) {
      error = Update.hasError() ? Update.errorString() : "blad obliczania SHA-256";
      downloadOk = false;
      break;
    }
    totalWritten += static_cast<size_t>(bytesRead);
  }

  unsigned char hash[32];
  if (downloadOk && mbedtls_sha256_finish_ret(&shaContext, hash) != 0) {
    error = "nie mozna zakonczyc SHA-256";
    downloadOk = false;
  }
  mbedtls_sha256_free(&shaContext);
  request.end();

  if (downloadOk && sha256Hex(hash) != expectedSha256) {
    error = "SHA-256 pobranego pliku jest inny";
    downloadOk = false;
  }
  if (!downloadOk) {
    Update.abort();
    return false;
  }
  if (!Update.end() || !Update.isFinished()) {
    error = Update.errorString();
    return false;
  }
  return true;
}
