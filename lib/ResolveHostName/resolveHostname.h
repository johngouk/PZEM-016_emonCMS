#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>

bool resolveHostname(String hostname, IPAddress& theIPAddress);