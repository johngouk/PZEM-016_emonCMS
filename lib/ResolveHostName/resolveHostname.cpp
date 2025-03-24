#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>

#include "resolveHostname.h"
//#define _DEBUG_
#include <debug.h>

bool mdnsStarted = false;
bool resolveHostname(String hostname, IPAddress& theIPAddress){
    // Try mDNSfirst...
    IPAddress serverIP;
    _PF("resolveHostname: Host %s resolve starting\n", hostname.c_str());
    String l = ".local";
    int i = hostname.indexOf(l);
    if (i>=0){ // It's a ".local" domain => MDNS
      hostname.remove(i);
      // Resolves a possibly local hostname e.g. myhomeserver.local
      if (!mdnsStarted){
          _PL(F("resolveHostname: Starting MDNS..."));
          int r = mdns_init();
          int attempts = 1;
          while(r != ESP_OK && attempts < 5){
            delay(250);
            attempts ++;
            int r = mdns_init();
          }
          if (r != ESP_OK){
            _PF("resolveHostname: Unable to start MDNS - quitting");
            return false;
          }
          mdnsStarted = true;
          _PL("resolveHostname: MDNS started");
      }

      _PF("resolveHostname: Resolving host %s\n", hostname.c_str());
      serverIP = MDNS.queryHost(hostname.c_str());
      while (serverIP.toString() == "0.0.0.0") {
          delay(250);
          serverIP = MDNS.queryHost(hostname.c_str());
      }
      
      _PF("resolveHostname: Host address resolved locally: %s\n", serverIP.toString().c_str());
    } else {
      if (WiFi.hostByName(hostname.c_str(), serverIP)){
        // All good, non-local resolved 
        _PF("resolveHostname: Host %s resolved remotely: %s\n", hostname.c_str(),serverIP.toString().c_str());
      } else {
        _PF("resolveHostname: Host %s resolved remotely: %s\n", hostname.c_str(),serverIP.toString().c_str());
        return false;
      }
    }

    theIPAddress = serverIP;
    return true;
}