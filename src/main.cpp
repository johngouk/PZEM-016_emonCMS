/*
  ESP32 program to collect energy data from PZEM-016 over Modbus
  and publish it to MQTT in a form acceptable to EmonCMS 
  i.e. "emon/ASHP/<subjectName>" (I use ASHP as my Node name in EmonCMS).

  Change Log:
  2024-08-05  Initial version with single JSON message
  2024-08-05  Revised for individual messages, one per Topic, and to use appropriate names
              for the topics, so they match my existing setup. That way I don't lose all
              my previous data when I have to reconfigure the Inputs/Feeds
  2025-03-15  Added heap size logging 
  2025-06-23  Improved MQTT Hub and MQTT Client connection checks, added WDT just in case!

*/

#include <Arduino.h>

#include <multi_heap.h> // Used to get heap size data
#include <esp_task_wdt.h> // The WatchDog Timer, used to make sure this doesn't freeze up

const int WDT_TIMEOUT_SETUP = 60;
const int WDT_TIMEOUT_LOOP = 20;

// Loop control timer values
const int delayIntervalSec = 10;
const int delayIntervalmSec = delayIntervalSec * 1000;
const int pollsPerDay = 60 / delayIntervalSec * 24 * 60;
//const int pollsPerDay = 5; // Test value!
int pollCount = 0;


// Update these with values suitable for your network.

const char* ssid = "norcot";
const char* password = "nor265cot";
const char* mqtt_server = "emonpi.local";
const int mqtt_port = 1883;
const char* mqtt_user = "emonpi";
const char* mqtt_pwd = "emonpimqtt2016";
const char* baseTopic = "emon/ASHP/";

/*
const char* mqtt_server = "test.mosquitto.org";
const int mqtt_port = 1884;
const char* mqtt_user = "rw";
const char* mqtt_pwd = "readwrite";
const char* baseTopic = "gouk/";
*/

#include <ModbusRTUMaster.h>
#ifdef ESP8266
#include <ESP8266WiFi.h>
#include <SoftwareSerial.h>
#define SERIAL_CONFIG (SWSERIAL_8N1)
#define SERIAL_FLUSH_TX_ONLY // empty, as SoftwareSerial.flush() takes no parameter
#define MODBUS_RX D6
#define MODBUS_TX D7
SoftwareSerial modbusSerial(MODBUS_RX, MODBUS_TX);
ModbusRTUMaster modbus(modbusSerial);
#elif ESP32
#include <WiFi.h>
HardwareSerial modbusSerial(2);
ModbusRTUMaster modbus(modbusSerial);
#define SERIAL_CONFIG (SERIAL_8N1)
#define SERIAL_FLUSH_TX_ONLY false
#define MODBUS_RX 16
#define MODBUS_TX 17
#endif

#define MODBUS_SLAVE_ADDR 1
#define MODBUS_REG_START 0
#define MODBUS_REG_COUNT 9

#include <PubSubClient.h>

#include <resolveHostname.h>

WiFiClient mqttHub;
//#define MQTT_VERSION MQTT_VERSION_3_1_1
PubSubClient mqttClient(mqttHub);

IPAddress mqtt_ip;

const char * valueNames[] = {
    "voltage",
    "current_b",
    "power_b",
    "energy_forward_b",
    "frequency",
    "power_factor_b"
};

// Don't use this other than to define how long the buffer needs to be!
struct energyData {
    int16_t voltage;
    int16_t current[2];
    int16_t power[2];
    int16_t energy[2];
    int16_t freq;
    int16_t pf;
};
// which is here
uint16_t dataBuf[sizeof(energyData)/2];

#define MAX_MSG_SIZE 250
char jsonbuff[MAX_MSG_SIZE] = "{\0";
#define MAX_VALUE_SIZE 50
char valueBuf[MAX_VALUE_SIZE] = "\0";

void setup_wifi() {

  //delay(5);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("[setup_wifi] connecting to ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  randomSeed(micros());

  Serial.println("");
  Serial.print("[setup_wifi] wifi connected: ");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  /*
  int result = espClient.connect(mqtt_server, mqtt_port);
  if (result){
    IPAddress remoteIP = espClient.remoteIP();
    Serial.printf("[setup_wifi] TestMQTTConnect: RemoteIP: %s\n", remoteIP.toString().c_str());
    espClient.stop();
  }
  */
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("[mqtt_callback] Message arrived: ");
  Serial.print(topic);
  Serial.print(" ");
  for (unsigned int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println("]");

}

bool reconnect() {
  // Loop until we're reconnected
  int attempts = 0;
  const int maxAttempts = 3;
  //Serial.printf("[reconnect] True: %d False: %d\n", true, false);
  Serial.printf("[reconnect] start: MQTT Hub connected %d; MQTT Client connected %d \n", mqttHub.connected(), mqttClient.connected());
  while ((!mqttClient.connected())&&(attempts<maxAttempts)) {
    attempts++;
    // Serial.printf("[reconnect] Client state = %d Attempt %d\n", client.state(), attempts);
    Serial.printf("[reconnect] Attempting MQTT Client connection %d ...", attempts);
    // Create a random client ID
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (mqttClient.connect(clientId.c_str(), mqtt_user, mqtt_pwd) && (mqttClient.connected()))
    {
      Serial.println("connected");
      // ... and resubscribe
      mqttClient.subscribe("inTopic");
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
  Serial.printf("[reconnect] end: MQTT Hub connected %d; MQTT Client connected %d \n", mqttHub.connected(), mqttClient.connected());
  if (mqttClient.connected()){
    IPAddress remoteIP = mqttHub.remoteIP();
    Serial.printf("[reconnect] MQTT Hub IP: %s\n", remoteIP.toString().c_str());
    return true;
  } else {
    return false;
  }
}

void startModbus() {
    modbus.begin(9600, SERIAL_CONFIG, MODBUS_RX, MODBUS_TX);
    modbus.setTimeout(200);
}

bool readModbusValues(){
  // Test data to check printf works
  uint16_t dummy = 0x0101;
  for (int i = 0; i < MODBUS_REG_COUNT; i++) {
    dataBuf[i] = dummy;
    dummy = dummy + 0x0101;
  }
  bool result = 0;
  result = modbus.readInputRegisters(MODBUS_SLAVE_ADDR, MODBUS_REG_START, dataBuf, MODBUS_REG_COUNT);
  /* */
  // Fake it out for now
  //result = true;
  /* */
  if (result) {
    Serial.println("[readModbusValues] modbus read successful");
    Serial.print("[readModbusValues] hex: ");
    for (int i = 0; i < 8; i++)
    {
        Serial.printf(" 0x%04x ", dataBuf[i]);
    }
    Serial.println("");
    float values [6];
    values[0] = dataBuf[0] * 0.1;                           // Voltage(0.1V)
    values[1] = (dataBuf[1] + (dataBuf[2] << 16)) * 0.001;  // Current(0.001A)
    values[2] = (dataBuf[3] + (dataBuf[4] << 16)) * 0.1;    // Power(0.1W)
    values[3] = (dataBuf[5] + (dataBuf[6] << 16)) * 0.001;  // Energy(1Wh -> kWh)
    values[4] = dataBuf[7] * 0.1;                           // Frequency
	  values[5] = dataBuf[8] * 0.01;							            // Power Factor
    /*
    // Faking it
    values[0] = 249.1;                           // Voltage(0.1V)
    values[1] = 10.123;     // Current(0.001A)
    values[2] = 1234.1;     // Power(0.1W)
    values[3] = 3500.123;   // Energy(1Wh -> kWh)
    values[4] = 49.1;       // Frequency
	  values[5] = 93.1;       // Power Factor
    */
   Serial.print("[readModbusValues] values: ");
    String mqtt_topic = baseTopic;
    for (int i = 0;i<6;i++){
      Serial.printf(" %f ", values[i]);
      snprintf(jsonbuff + strlen(jsonbuff), MAX_MSG_SIZE - strlen(jsonbuff), "\"%s\":%.3f,", valueNames[i], values[i]);
      mqtt_topic.concat(valueNames[i]);
      snprintf(valueBuf + strlen(valueBuf), MAX_VALUE_SIZE - strlen(valueBuf), "%.3f", values[i]);
      mqttClient.publish(mqtt_topic.c_str(), valueBuf);
      Serial.printf("Topic:%s Data:%s\n", mqtt_topic.c_str(), valueBuf);
      mqtt_topic = baseTopic;
      strcpy(valueBuf, "\0");
    }
    jsonbuff[strlen(jsonbuff) - 1] = '}';
    Serial.println("]");
    Serial.printf("[readModbusValues] JSON: %s\n", jsonbuff);
    mqttClient.publish("test/PowerData", jsonbuff);
    strcpy(jsonbuff, "{\0");

  } else {
    Serial.print("[readModbusValues] error reading modbus");
    if (modbus.getTimeoutFlag() == true) {
      Serial.print(": Timeout");
      modbus.clearTimeoutFlag();
    }
    else if (modbus.getExceptionResponse() != 0) {
      Serial.print(": Exception Response ");
      Serial.print(modbus.getExceptionResponse());
      switch (modbus.getExceptionResponse()) {
        case 1:
          Serial.print(" (Illegal Function)");
          break;
        case 2:
          Serial.print(" (Illegal Data Address)");
          break;
        case 3:
          Serial.print(" (Illegal Data Value)");
          break;
        case 4:
          Serial.print(" (Server Device Failure)");
          break;
        default:
          Serial.print(" (Uncommon Exception Response)");
          break;
      }
      modbus.clearExceptionResponse();
    }
    Serial.println();
  } 
  return result;

}

bool setup_MQTT(){
  Serial.printf("[setup_MQTT] MQTT: Host: %s Port: %d User: %s Pwd: %s\n", mqtt_server, mqtt_port, mqtt_user, mqtt_pwd);
  mqttClient.setCallback(mqtt_callback);
  bool result = resolveHostname(mqtt_server, mqtt_ip);
  if (result) {
    Serial.printf("[setup_MQTT] MQTT Hub IP: %s\n", mqtt_ip.toString().c_str());
    mqttClient.setServer(mqtt_ip, mqtt_port); // Need to use mDNS directly, WiFiClient doesn't work properly
    mqttHub.connect(mqtt_ip, mqtt_port);
  } else {
    Serial.printf("[setup_MQTT] Using MQTT Hub hostname! %s \n", mqtt_server);
    mqttClient.setServer(mqtt_server, mqtt_port); // Ah fuck it
    mqttHub.connect(mqtt_server, mqtt_port);
  }
  Serial.printf("[setup_MQTT] MQTT Hub Connected: %d MQTT Client Connected: %d\n", mqttHub.connected(), mqttClient.connected());
  if (!mqttHub.connected()){
    Serial.println("[setup_MQTT] Unable to connect to MQTT Hub - exiting");
    return false;
  } else
    return true;
}

void setup() {
  delay(5000);
  Serial.begin(115200);

  Serial.println("[setup] started");
  randomSeed(micros());

  Serial.printf("[setup] configuring WDT %d seconds\n", WDT_TIMEOUT_SETUP);
  Serial.flush();
  esp_task_wdt_init(WDT_TIMEOUT_SETUP, true); //enable panic so ESP32 restarts
  Serial.printf("[setup] adding task to WDT \n");
  Serial.flush();
  esp_task_wdt_add(NULL); //add current thread to WDT watch

  Serial.println("[setup] modbus starting");
  startModbus();
  Serial.println("[setup] modbus started");

  setup_wifi();
  if (!setup_MQTT()) ESP.restart();; // Sets up MQTT client, including TCP connection to server

  Serial.println("[setup] resetting WDT...");
  esp_task_wdt_init((WDT_TIMEOUT_LOOP + delayIntervalSec), true); //enable panic so ESP32 restarts
  esp_task_wdt_add(NULL); //add current thread to WDT watch

}

void reportHeap(int loc){
  multi_heap_info_t info;
  heap_caps_get_info(&info, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT); // internal RAM, memory capable to store data or to create new task
  // total currently free in all non-continues blocks
  // minimum free ever
  // largest continues block to allocate big array
  Serial.printf("[heap %d] Total free:%d Min. free: %d Largest free block: %d\n", loc, 
                info.total_free_bytes, info.minimum_free_bytes, info.largest_free_block);
}

void loop() {
  time_t start = millis();
  esp_task_wdt_reset(); // Phew, we made around another loop!
  Serial.printf("[loop] start: %d\n", start);
  reportHeap(1);
  bool result;
  Serial.println("[loop]");
  if (!mqttClient.connected()) {
    if (not(reconnect())) {
      // Restart!! for safety - do this now so it takes maybe 10 secs...
      ESP.restart();
    };
  }
  mqttClient.loop(); // Let MQTT Client do its thing
  reportHeap(2);
  result = readModbusValues();
  reportHeap(3);
  Serial.flush();
  pollCount += 1;
  if (pollCount >= pollsPerDay){
    // Restart!! for safety - do this now so it takes maybe 10 secs...
    Serial.println("Poll count reached - executing restart");
    Serial.flush();
    //delay(WDT_TIMEOUT_LOOP * 2 * 1000); // Make sure WDT blows - TEST!
    ESP.restart();
  }
  Serial.printf("[loop] time millis: %d\n", millis() - start);
  delay(delayIntervalmSec - (millis() - start));
}