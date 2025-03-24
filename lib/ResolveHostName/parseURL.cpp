#include <Arduino.h>
#include <base64.h>

//#define _DEBUG_

#include <debug.h>

#include <resolveHostname.h>
#include <parseURL.h>

/*
  Magic does eerything URL parser
  Given a URL, returns
    hostname - hostname for the HTTP header
    hostIP - IPAddress of host
    port - HTTP port number
    URI - for the HTTP header
*/
bool parseURL(String URL, String& hostname, IPAddress& hostIP, uint16_t& port, String& URI){
  // ipaddr = IPAddress(char* stringIpAddr)
  // WiFi.hostByName(const char* hostName, IPAddress &aResult)
  String _host;
  uint16_t _port = 0;
  //int32_t _connectTimeout = HTTPCLIENT_DEFAULT_TCP_TIMEOUT;
  // bool _reuse = true;
  // uint16_t _tcpTimeout = HTTPCLIENT_DEFAULT_TCP_TIMEOUT;
  // bool _useHTTP10 = false;
  // bool _secure = false;

  String _uri;
  String _protocol;
  // String _headers;
  // String _userAgent = "ESP32HTTPClient";
  String _base64Authorization;
  // String _authorizationType = "Basic";
  // String _acceptEncoding = "identity;q=1,chunked;q=0.1,*;q=0";
  String expectedProtocol[] = {"http", "https"};
  uint16_t expectedPorts[] = {80, 443}; // Matches the protocol names above
  _PL("ParseURL");
  _PF("ParseURL: URL: %s\n", URL.c_str());
  // <protocol>://user:password@<hostnameOrIP>:<port>/<URL>
  // check for : http: or https:
  int index = URL.indexOf(':');
  if (index < 0) {
    _PL("ParseURL: failed to parse protocol");
    return false;
  }
  // |--------v
  // <protocol>://user:password@<hostnameOrIP>:<port>@<auth>/<URI>
  _protocol = URL.substring(0, index);
  int protocolID = -1;
  for (int i = 0; i<sizeof(expectedProtocol); i++){
    _PF("ParseURL: Checking protocol %d given %s vs. %s\n", i, _protocol.c_str(), expectedProtocol[i].c_str())
    if (_protocol == expectedProtocol[i]){
        protocolID = i;
        break;
    };
  }
  if (protocolID <= -1 ) {
    _PF("ParseURL: unexpected protocol: %s\n", _protocol.c_str());
    return false;
  }
  // |----------->
  // <protocol>://user:password@<hostnameOrIP>:<port>@<auth>/<URI>
  URL.remove(0, (index + 3));  // remove http:// or https://

  // |---------------------------v
  // user:password@<hostnameOrIP>:<port>@<auth>/<URI>
  index = URL.indexOf('/');
  if (index == -1) {
    index = URL.length();
    URL += '/'; // No? Add it!
  }
  // |----------------------------------v
  // user:password@<hostnameOrIP>:<port>/<URI>
  _host = URL.substring(0, index);
  URL.remove(0, index);  // remove host part; it's now the URI

  // |------------v
  // user:password@<hostnameOrIP>:<port>
  // get Authorization
  index = _host.indexOf('@');
  if (index >= 0) { // There is an auth component
    // auth info
    // |------------v
    // user:password@<hostnameOrIP>:<port>
    String auth = _host; // Prepare "auth" string for surgery
    auth.substring(0, index);  // remove host part including @
    _host.remove(0, index+1);  // remove auth part including @
    // |-----------v
    // user:password
    _PF("ParseURL: auth %s\n", auth.c_str());
    _base64Authorization = base64::encode(auth);
    // Then what?? Someone pokes them in the Auth header
  }

  // get port
  // |-------------v
  // <hostnameOrIP>:<port>@<auth>
  index = _host.indexOf(':');
  String the_host;
  if (index >= 0) {
    // |-------------v
    // <hostnameOrIP>
    the_host = _host.substring(0, index);  // hostname
    // |-------------v
    // <port>
    _host.remove(0, (index + 1));          // remove hostname + :
    _port = _host.toInt();                 // get port
  } else {
    the_host = _host;
  }
  if (_port == 0){
    // None provided, use the defaults
    _port = expectedPorts[protocolID];
  }
  // Process URI to remove query params
  index = URL.indexOf('?');
  if (index >= 0){
    URL.substring(0, index); // WE aren't doing those!
  }
  if (resolveHostname(the_host, hostIP)){
    _PL("parseURL: Hostname resolved");
  }
  hostname = the_host;
  port = _port;
  URI = URL;

  return true;

}
