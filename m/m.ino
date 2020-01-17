#include "mcp_can.h"
#include <SPI.h>
#include <map>
#include <sstream> 
#include <string>

// MCP2515 STUFF
long unsigned int rxId;                     // CAN BUS - Identifier.
unsigned char len = 0;                      // CAN BUS - DLC size
unsigned char rxBuf[8];                     // CAN BUS - Data array.
char msgString[256];                        // Array to store serial string

char idString[8];                           // Array to store can id



#define CAN0_INT 4                              // MCP2515 INT  to pin 4
MCP_CAN CAN0(2);                               // MCP2515 CS   to pin 2

struct Msg {
  unsigned char len = 0;                      // CAN BUS - DLC size
  unsigned char rxBuf[8]; 
};

std::map<long unsigned int, Msg> msgDictionary;
std::map<long unsigned int, Msg>::iterator it;

void setup() 
{
    Serial.begin(115200);
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

void printCanMessage(char prefixString [], long unsigned int rxId, unsigned char len, unsigned char rxBuf[8])
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
void loop()
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
      
      printCanMessage("New event:     ", rxId, len, rxBuf);
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
          printCanMessage("Diffrent data: ", rxId, len, rxBuf);
          // printCanMessage("Second:        ", rxId, it->second.len, it->second.rxBuf);
        }
      }
      
    }
}