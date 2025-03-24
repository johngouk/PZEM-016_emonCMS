#include <Arduino.h>

bool parseURL(String URL, String& hostname, IPAddress& hostIP, uint16_t& port, String& URI);