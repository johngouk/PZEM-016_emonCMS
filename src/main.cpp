/*
  ESP32 program to collect energy data from PZEM-016 over Modbus
  and publish it to MQTT in a form acceptable to EmonCMS 
  i.e. "emon/ASHP/<subjectName>" (I use ASHP as my Node name in EmonCMS).

  Change Log:
  2024-08-05  Initial version with single JSON message
  2024-08-05  Revised for individual messages, one per Topic, and to use appropriate names
              for the topics, so they match my existing setup. That way I don't lose all
              my previous data when I have to reconfigure the Inputs/Feeds

*/

#include <Arduino.h>

// Update these with values suitable for your network.

const char* ssid = "mySSID";
const char* password = "myPassword";
const char* mqtt_server = "mqttserver.local";
#define mqtt_user "mqttUser"
#define mqtt_pwd "mqttPwd"
const char* baseTopic = "emon/ASHP/";
unsigned long lastMsg = 0;
#define MSG_BUFFER_SIZE	(50)
char msg[MSG_BUFFER_SIZE];
int value = 0;


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

WiFiClient espClient;
PubSubClient client(espClient);

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

#define MAX_MSG_SIZE 128
char jsonbuff[MAX_MSG_SIZE] = "{\0";
#define MAX_VALUE_SIZE 20
char valueBuf[MAX_VALUE_SIZE] = "\0";

void setup_wifi() {

  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("[connecting to ");
  Serial.print(ssid);
  Serial.println(" ]");

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  randomSeed(micros());

  Serial.println("");
  Serial.print("[wifi connected: ");
  Serial.print("IP address: ");
  Serial.print(WiFi.localIP());
  Serial.println(" ]");
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (unsigned int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println("]");

}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.printf("Client state = %d\n", client.state());
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str())) //, mqtt_user, mqtt_pwd) 
    {
      Serial.println("connected");
      // ... and resubscribe
      client.subscribe("inTopic");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
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
  if (result) {
    Serial.println("[modbus read successful]");
    Serial.print("[hex: ");
    for (int i = 0; i < 8; i++)
    {
        Serial.printf(" 0x%04x ", dataBuf[i]);
    }
    Serial.println("]");
    float values [6];
    values[0] = dataBuf[0] * 0.1;                           // Voltage(0.1V)
    values[1] = (dataBuf[1] + (dataBuf[2] << 16)) * 0.001;  // Current(0.001A)
    values[2] = (dataBuf[3] + (dataBuf[4] << 16)) * 0.1;    // Power(0.1W)
    values[3] = (dataBuf[5] + (dataBuf[6] << 16));          // Energy(1Wh)
    values[4] = dataBuf[7] * 0.1;                           // Frequency
	  values[5] = dataBuf[8] * 0.01;							// Power Factor
    Serial.print("[values: ");
    String mqtt_topic = baseTopic;
    for (int i = 0;i<6;i++){
      Serial.printf(" %f ", values[i]);
      snprintf(jsonbuff + strlen(jsonbuff), MAX_MSG_SIZE - strlen(jsonbuff), "\"%s\":%.3f,", valueNames[i], values[i]);
      mqtt_topic.concat(valueNames[i]);
      snprintf(valueBuf + strlen(valueBuf), MAX_VALUE_SIZE - strlen(valueBuf), "%.3f", values[i]);
      client.publish(mqtt_topic.c_str(), valueBuf);
      mqtt_topic = baseTopic;
      strcpy(valueBuf, "\0x");
    }
    jsonbuff[strlen(jsonbuff) - 1] = '}';
    Serial.println("]");
    Serial.println(jsonbuff);
    client.publish("test/PowerData", jsonbuff);
    strcpy(jsonbuff, "{\0");


  } else {
    Serial.println("[error reading modbus]");
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


void setup() {
  Serial.begin(115200);

  Serial.println("[started]");
  Serial.println("[modbus starting]");
  startModbus();
  Serial.println("[modbus started]");

  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(mqtt_callback);

}

void loop() {
  bool result;
  Serial.println("[loop]");
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  result = readModbusValues();

  Serial.flush();
  delay(10000);

}
