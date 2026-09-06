#pragma once

#include <Arduino.h>
#include "core/Telemetry.h"

String jsonEscape(const char* text);
void appendInverterJson(String& json, const Telemetry& telemetry);
void appendBmsJson(String& json, const Telemetry& telemetry);
String mqttTelemetryJson(const Telemetry& telemetry);
