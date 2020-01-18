#include <mcp_can.h>
#include <SPI.h>
#include <map>
#include <sstream> 
#include <string>
#include <IotWebConf.h>
#include <WiFiClient.h>
#include <HTTPUpdate.h>
#include <MQTT.h>

#pragma region 
// MCP2515 STUFF
long unsigned int rxId;                     // CAN BUS - Identifier.
unsigned char len = 0;                      // CAN BUS - DLC size
unsigned char rxBuf[8];                     // CAN BUS - Data array.
char msgString[256];                        // Array to store serial string

char topicString[128];                      // MQTT Topic string
char hexString[64];                         // MQTT Payload string


#define CAN0_INT 4                              // MCP2515 INT  to pin 4
MCP_CAN CAN0(2);                               // MCP2515 CS   to pin 2

struct Msg {
  unsigned char len = 0;                      // CAN BUS - DLC size
  unsigned char rxBuf[8]; 
};
std::map<long unsigned int, Msg> msgDictionary;
std::map<long unsigned int, Msg>::iterator it;
#pragma endregion


#pragma Configuration

const char appName[] = "can2mqtt";
const char wifiInitialApPassword[] = "qwerasdf";
#define STRING_LEN 128
#define CONFIG_VERSION "mqt1"
void wifiConnected();
void configSaved();
boolean formValidator();
DNSServer dnsServer;
WebServer server(80);
HTTPUpdateServer httpUpdater;
WiFiClient net;
char mqttServerValue[STRING_LEN];
char mqttUserNameValue[STRING_LEN];
char mqttUserPasswordValue[STRING_LEN];
IotWebConf iotWebConf(appName, &dnsServer, &server, wifiInitialApPassword, CONFIG_VERSION);
IotWebConfParameter mqttServerParam = IotWebConfParameter("MQTT server", "mqttServer", mqttServerValue, STRING_LEN);
IotWebConfParameter mqttUserNameParam = IotWebConfParameter("MQTT user", "mqttUser", mqttUserNameValue, STRING_LEN);
IotWebConfParameter mqttUserPasswordParam = IotWebConfParameter("MQTT password", "mqttPass", mqttUserPasswordValue, STRING_LEN, "password");
boolean needMqttConnect = false;
boolean needReset = false;
unsigned long lastReport = 0;
unsigned long lastMqttConnectionAttempt = 0;
#pragma endregion

MQTTClient mqttClient;



void processCANMessage();
void processMQTT();
void printCANMessage(char prefixString [], long unsigned int rxId, unsigned char len, unsigned char rxBuf[8]);
void setupCAN();
void setupConfiguration();

void setup() 
{
  Serial.begin(115200);
    
  setupCAN();
  setupConfiguration();

  mqttClient.begin(mqttServerValue, net);
  mqttClient.onMessage(mqttMessageReceived);
}


void loop()
{
  iotWebConf.doLoop();
  mqttClient.loop();

  
  processMQTT();
  processCANMessage();
}

void setupConfiguration() 
{
  iotWebConf.addParameter(&mqttServerParam);
  iotWebConf.addParameter(&mqttUserNameParam);
  iotWebConf.addParameter(&mqttUserPasswordParam);
  iotWebConf.setConfigSavedCallback(&configSaved);
  iotWebConf.setFormValidator(&formValidator);
  iotWebConf.setWifiConnectionCallback(&wifiConnected);
  iotWebConf.setupUpdateServer(&httpUpdater);
   boolean validConfig = iotWebConf.init();
  if (!validConfig)
  {
    mqttServerValue[0] = '\0';
    mqttUserNameValue[0] = '\0';
    mqttUserPasswordValue[0] = '\0';
  }
  server.on("/", handleRoot);
  server.on("/config", []{ iotWebConf.handleConfig(); });
  server.onNotFound([](){ iotWebConf.handleNotFound(); });
}
void handleRoot()
{
  if (iotWebConf.handleCaptivePortal())
  {
    return;
  }
  String s = "<!DOCTYPE html><html lang=\"en\"><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1, user-scalable=no\"/>";
  s += "<title>CAN2MQTT bridge</title></head><body>MQTT App demo";
  s += "<ul>";
  s += "<li>MQTT server: ";
  s += mqttServerValue;
  s += "</ul>";
  s += "Go to <a href='config'>configure page</a> to change values.";
  s += "</body></html>\n";

  server.send(200, "text/html", s);
}

void setupCAN()
{
  if (CAN0.begin(MCP_STDEXT, CAN_100KBPS, MCP_8MHZ) == CAN_OK)
    {
        Serial.println("MCP2515 Initialized Successfully!");
    }
    else
    {
        Serial.println("Error Initializing MCP2515...");
    }
                     
                     
  CAN0.init_Mask(0,1, 0xFFFFFFFF);
  CAN0.init_Filt(0,1, 0xFFBFA1E8);                

  // Setting MCP to operate normal mode - remove line below to use loopback (development scenario).
  CAN0.setMode(MCP_NORMAL);

  // Setting pin 2 to INPUT - Communication with MCP2515
  pinMode(CAN0_INT, INPUT);

  // Enable one shot transmit - TODO research
  CAN0.enOneShotTX();
  Serial.println("CANBus configured.");
}

void processCANMessage() 
{
  if (digitalRead(CAN0_INT)) // Continue only if MSG
  {
      return;
  }
  CAN0.readMsgBuf(&rxId, &len, rxBuf);
  Msg msgNew = Msg();
  msgNew.len = len;
  memcpy ( &msgNew.rxBuf, rxBuf, len);

  it = msgDictionary.find(rxId);
  bool differentData;
  if(it == msgDictionary.end()) // New event
  {
    msgDictionary[rxId] = msgNew;
    
    printCANMessage("New event:     ", rxId, len, rxBuf);
    // publishEvent(rxId, len, rxBuf);
  }
  else 
  {
    Msg messageFromDictionary = it->second;
    differentData = memcmp (&rxBuf, &messageFromDictionary.rxBuf, len) != 0;
    if(differentData)
    {
      it->second = msgNew;

      if(rxId != 0x8E405E17 && rxId != 0x8E605E17 && rxId != 0x8F005E17)
      {
        printCANMessage("Diffrent data: ", rxId, len, rxBuf);
        publishEvent(rxId, len, rxBuf);
        // printCanMessage("Second:        ", rxId, it->second.len, it->second.rxBuf);
      }
    }
  }
}

void sendMsg(unsigned long int id, unsigned char len, byte data[])
{
  byte sndStat = CAN0.sendMsgBuf(id, len, data);
  if (sndStat != CAN_OK)
  {
    int i = CAN0.getError();
    sprintf(msgString, "Error Sending Message.. %1d", i);

    Serial.println(msgString);
  }
}

void publishEvent(long unsigned int rxId, unsigned char len, unsigned char rxBuf[8])
{
  if(!mqttClient.connected())
  {
    Serial.println("Skipping publish. Not connected to MQTT.");
  }
  sprintf(topicString, "/device/%s/0x%lX", iotWebConf.getThingName(), rxId);
  String payload = "";
  
  for(byte i = 0; i<len; i++)
  {
    sprintf(hexString, " 0x%.2X", rxBuf[i]);
    payload += hexString;
  }
  
  mqttClient.publish(topicString, payload.substring(1)); // Skipping first char because of the whitespace at the beginning
}
void printCANMessage(char prefixString [], long unsigned int rxId, unsigned char len, unsigned char rxBuf[8])
{
  Serial.print(prefixString);
  sprintf(msgString, "Id: 0x%lX Data: ", rxId);
  Serial.print(msgString);
  for(byte i = 0; i<len; i++)
  {
      sprintf(msgString, " 0x%.2X", rxBuf[i]);
      Serial.print(msgString);
  }
  Serial.println();

}
void wifiConnected()
{
  needMqttConnect = true;
}

void configSaved()
{
  Serial.println("Configuration was updated.");
  needReset = true;
}

boolean formValidator()
{
  Serial.println("Validating form.");
  boolean valid = true;

  int l = server.arg(mqttServerParam.getId()).length();
  if (l < 3)
  {
    mqttServerParam.errorMessage = "Please provide at least 3 characters!";
    valid = false;
  }

  return valid;
}

void mqttMessageReceived(String &topic, String &payload)
{
  Serial.println("Incoming: " + topic + " - " + payload);
}


void processMQTT()
{
   mqttClient.loop();
  
  if (needMqttConnect)
  {
    if (connectMqtt())
    {
      needMqttConnect = false;
    }
  }
  else if ((iotWebConf.getState() == IOTWEBCONF_STATE_ONLINE) && (!mqttClient.connected()))
  {
    Serial.println("MQTT reconnect");
    connectMqtt();
  }
  
  if (needReset)
  {
    Serial.println("Rebooting after 1 second.");
    iotWebConf.delay(1000);
    ESP.restart();
  }
}

boolean connectMqtt() {
  unsigned long now = millis();
  if (1000 > now - lastMqttConnectionAttempt)
  {
    // Do not repeat within 1 sec.
    return false;
  }
  Serial.println("Connecting to MQTT server...");
  if (!connectMqttOptions()) {
    lastMqttConnectionAttempt = now;
    return false;
  }
  Serial.println("Connected!");

  mqttClient.subscribe("/test/action");
  return true;
}
boolean connectMqttOptions()
{
  boolean result;
  if (mqttUserPasswordValue[0] != '\0')
  {
    result = mqttClient.connect(iotWebConf.getThingName(), mqttUserNameValue, mqttUserPasswordValue);
  }
  else if (mqttUserNameValue[0] != '\0')
  {
    result = mqttClient.connect(iotWebConf.getThingName(), mqttUserNameValue);
  }
  else
  {
    result = mqttClient.connect(iotWebConf.getThingName());
  }
  return result;
}