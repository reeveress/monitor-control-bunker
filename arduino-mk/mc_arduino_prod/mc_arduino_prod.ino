// This script, "sketch", is used to gather sensor data with an Arduino and 
// send it over the White Rabbit Network to a server inside a HERA container. 
// On boot, Arduino will request to get an IP address along with a latest
// sketch stored on the TFTP server in the container. Note that in order
// to get an IP, the Arduino needs to specify its MAC address - which does
// come with Arduino off the shelf. The initial bootloader MAC address
// is hardwired into the arduino-netboot software available in the
//  






//================ EEPROM Memory Map ================= 
// Address Byte       Data(8 bits)        Type                
//      0              ---- ----       MAC byte 0       
//      1              ---- ----       MAC byte 1       
//      2              ---- ----       MAC byte 2
//      3              ---- ----       MAC byte 3
//      4              ---- ----       MAC byte 4
//      5              ---- ----       MAC byte 5
//      6              ---- ----       Node ID 
//      7              ---- ----       Serial Low byte
//      8              ---- ----       Serial High byte
//      9              ---- ----       unassigned
//      10             ---- ----       unassigned
//      .              ---- ----       unassigned
//      ..             ---- ----       unassigned
//      ...            ---- ----       unassigned
//      1024           ---- ----       unassigned





#include <Adafruit_SleepyDog.h>
#include <EEPROM.h>
#include <Ethernet.h>
#include <SPI.h>
#include <Adafruit_MCP9808.h>
#include <Adafruit_HTU21DF.h>
#include <SparkFunSX1509.h> // Include SX1509 library





// Define Arduino pins 
#define SNAP_RELAY 2 
#define FEM 8
#define PAM 9
#define SNAPv2_0 3
#define SNAPv2_1 5
#define SNAPv2_2 6
#define SNAPv2_3 7
#define RESET 4



// I2C addresses for the two MCP9808 temperature sensors
#define TEMP_TOP 0x1B
#define TEMP_MID 0x1A
#define TEMP_BOT 0x18


//#define VERBOSE


IPAddress serverIp(10, 1, 1, 1); // Server ip address
EthernetUDP UdpRcv; // UDP object to receive packets
EthernetUDP UdpSnd; // UDP object to send packets
EthernetUDP UdpSer; // UDP object to print serial debug info

byte mac[6];
unsigned int rcvPort = 8888;  // Assign port to receive commands on
unsigned int sndPort = 8889;  // Assign port to send data on
unsigned int serPort = 8890;  // Assign port to print debug statements


// Initializing buffer and data variables for receiving packets from the server
int packetSize;
char packetBuffer[UDP_TX_PACKET_MAX_SIZE];
String command; // String for data


unsigned int EEPROM_SIZE = 1024;
unsigned int eeadr = 0; // MACburner.bin writes MAC addres to the first 6 addresses of EEPROM
unsigned int eeNodeAdr = 6; // EEPROM node ID address
unsigned int eeSerialAdrL = 7;
unsigned int eeSerialAdrH = 8;

// I2C digital i/o serial board
const byte SX1509_ADDRESS = 0x3E;
SX1509 io;


// Sensor objects
Adafruit_MCP9808 mcpTop = Adafruit_MCP9808(); 
Adafruit_MCP9808 mcpMid = Adafruit_MCP9808(); 
Adafruit_MCP9808 mcpBot = Adafruit_MCP9808(); 
Adafruit_HTU21DF htu = Adafruit_HTU21DF();



// struct for a UDP packet
struct sensors {
  float nodeID;
  float cpu_uptime = 0;
  float mcpTempTop = -99;
  float mcpTempMid = -99;
  float mcpTempBot = -99;
  float htuTemp = -99;
  float htuHumid = -99;
  bool snap_relay = false;
  bool fem = false;
  bool pam = false;
  bool snapv2_0 = false;
  bool snapv2_1 = false;
  bool snapv2_2 = false;
  bool snapv2_3 = false;
  byte serialLb;
  byte serialHb;
  byte mac[6];
} sensorArray;

void bootReset();
void serialUdp(String);
void parseUdpPacket();

void setup() {
  Watchdog.enable(8000);
  unsigned int startSetup = millis();
  
  
  // Initialize Serial port
  Serial.begin(57600);
  Serial.println("Running Setup..");

  
  // Setting pins appropriately. Very important to first deactivate the digital pins
  // because setting the pin as OUTPUT changes it's state and has caused problems with the reset pin 4 before
  // PSU that turns on White Rabbit is now hard wired to be turned on by the 5V Arduino signal, this prevents 
  // indefinite power cycle in case of a reset loop (which happens when ping/poke fail, the reset command was sent 
  // or all four sensors are off line.

  // Initialize and deactivate pins to avoid glitches
  // 8 way SNAP relay
  digitalWrite(SNAP_RELAY, LOW);
  pinMode(SNAP_RELAY, OUTPUT);
  digitalWrite(SNAP_RELAY, LOW);
   
  // FEM VAC pin: Active HIGH
  digitalWrite(FEM, LOW);
  pinMode(FEM, OUTPUT);
  digitalWrite(FEM, LOW);
  
  // PAM VAC pin: Active HIGH
  digitalWrite(PAM, LOW);
  pinMode(PAM, OUTPUT);
  digitalWrite(PAM, LOW);
  
  // SNAPv2_0: Active LOW so HIGH is off
  digitalWrite(SNAPv2_0, HIGH);
  pinMode(SNAPv2_0, OUTPUT);
  digitalWrite(SNAPv2_0, HIGH);

  // SNAPv2_1: Active LOW so HIGH is off
  digitalWrite(SNAPv2_1, HIGH);
  pinMode(SNAPv2_1, OUTPUT);
  digitalWrite(SNAPv2_1, HIGH);
  
  // SNAPv2_2: Active LOW so HIGH is off
  digitalWrite(SNAPv2_2, HIGH);
  pinMode(SNAPv2_2, OUTPUT);
  digitalWrite(SNAPv2_2, HIGH);

  // SNAPv2_3: Active LOW so HIGH is off
  digitalWrite(SNAPv2_3, HIGH);
  pinMode(SNAPv2_3, OUTPUT);
  digitalWrite(SNAPv2_3, HIGH);

  // Reset pin, Active LOW
  digitalWrite(RESET, HIGH);
  pinMode(RESET, OUTPUT); 
  digitalWrite(RESET, HIGH);
 
    
  // Read MAC address from EEPROM (burned previously with macBurner.bin sketch)
  for (int i = 0; i < 6; i++){
    mac[i] = EEPROM.read(eeadr);
    ++eeadr;
  }

  // Fill up the sensorArray struct's mac variable with the burned MAC address
  for (int i = 0; i < 6; i++){
    sensorArray.mac[i] = mac[i]; 
  }
  Watchdog.reset(); 
  // Start Ethernet connection, automatically tries to get IP using DHCP
  if (Ethernet.begin(mac) == 0) {
    Serial.println("Failed to configure Ethernet using DHCP, restarting sketch...");
    delay(10000);
  }
  Serial.println("Configured IP:");
  Serial.println(Ethernet.localIP());

  // Start UDP - HAS TO BE AFTER ETHERNET.BEGIN!!!!!!
  UdpRcv.begin(rcvPort);
  UdpSnd.begin(sndPort);
  UdpSer.begin(serPort);
  delay(1500); // delay to give time for initialization
  Watchdog.reset();
  
  // Now that UDP is initialized, serialUdp can be used
  serialUdp("Running Setup..."); 
  serialUdp("Contents of the sensorArray.mac:");
  serialUdp(String(mac[6]));
  serialUdp("Individual values:");
  serialUdp(String(mac[0]));
  serialUdp(String(mac[1]));
  serialUdp(String(mac[2]));
  serialUdp(String(mac[3]));
  serialUdp(String(mac[4]));
  serialUdp(String(mac[5]));

 

  // Read node ID from EEPROM (burned with MACburner.bin sketch) and assign it to struct nodeID member
  sensorArray.nodeID = EEPROM.read(eeNodeAdr);
  Serial.print("EEPROM contents:");
  serialUdp("EEPROM contents:");
  for (int i = 0; i < 8; i++) {
    Serial.print("Address: ");
    Serial.print(i);
    Serial.print("\t");
    Serial.print("Data: ");
    Serial.print(EEPROM.read(i));
    Serial.print("\t");   
    serialUdp("Address: ");
    serialUdp(String(i));
    serialUdp("Data: ");
    serialUdp(String(EEPROM.read(i)));
    
  }
  // Find Digital IO card and initialize its pin modes 
  if (io.begin(SX1509_ADDRESS)) {
      Serial.println("Digital IO card found!");
      serialUdp("Digital IO card found!");
      io.pinMode(0,INPUT);      // Serial
      io.digitalWrite(0,LOW);   // Activating PULL DOWN resistor on pin
      io.pinMode(1,INPUT);      //   .
      io.digitalWrite(1,LOW);   // Activating PULL DOWN resistor on pin
      io.pinMode(2,INPUT);      //   .
      io.digitalWrite(2,LOW);   // Activating PULL DOWN resistor on pin
      io.pinMode(3,INPUT);      //   .
      io.digitalWrite(3,LOW);   // Activating PULL DOWN resistor on pin
      io.pinMode(4,INPUT);      //   .
      io.digitalWrite(4,LOW);   // Activating PULL DOWN resistor on pin
      io.pinMode(5,INPUT);      //   .
      io.digitalWrite(5,LOW);   // Activating PULL DOWN resistor on pin
      io.pinMode(6,INPUT);      // Rev Number
      io.digitalWrite(6,LOW);   // Activating PULL DOWN resistor on pin
      io.pinMode(7,INPUT);      //   .
      io.digitalWrite(7,LOW);   // Activating PULL DOWN resistor on pin
      io.pinMode(8,INPUT);      // 1=production, 0=prototype
      io.digitalWrite(8,LOW);   // Activating PULL DOWN resistor on pin
      io.pinMode(9,INPUT);      //  TBD
      io.digitalWrite(9,LOW);   // Activating PULL DOWN resistor on pin
      io.pinMode(10,INPUT);     //   .
      io.digitalWrite(10,LOW);  // Activating PULL DOWN resistor on pin
      io.pinMode(11,INPUT);     //   .
      io.digitalWrite(11,LOW);  // Activating PULL DOWN resistor on pin
      io.pinMode(12,OUTPUT);    //   .
      io.pinMode(13,OUTPUT);    //   .
      io.pinMode(14,OUTPUT);    //   .
      io.pinMode(15,OUTPUT);    //   .
      
      for (int i=0; i<8; i++){
          sensorArray.serialLb |= io.digitalRead(i) << i; 
      }
      for (int i=8; i<15; i++){
          sensorArray.serialHb |= io.digitalRead(i) << i; 
      }
      EEPROM.write(eeSerialAdrL, sensorArray.serialLb);
      EEPROM.write(eeSerialAdrH, sensorArray.serialHb);
  }
  else {
    Serial.println("Digital io card not found"); 
    serialUdp("Digital io card not found"); 
  }

  unsigned int endSetup = millis();
  serialUdp("Time to run the Setup");
  serialUdp(String(endSetup-startSetup));

}

 
void loop() {
     
    unsigned int startLoop = millis();
    
    // Find top temp sensor and read its value
    if (mcpTop.begin(TEMP_TOP)) {
      sensorArray.mcpTempTop = mcpTop.readTempC();    
    }
    else {
      Serial.println("MCP9808 TOP not found");
#ifdef VERBOSE
      serialUdp("MCP9808 TOP not found");
#endif
      sensorArray.mcpTempTop = -99; 
    }
 
    
    // Find middle temp sensor and take read value
    if (mcpMid.begin(TEMP_MID)) {
      sensorArray.mcpTempMid = mcpMid.readTempC();
    }
    else {
      Serial.println("MCP9808 MID not found");
#ifdef VERBOSE
      serialUdp("MCP9808 MID not found"); 
#endif
      sensorArray.mcpTempMid = -99; 
    }

    
    // Find bot temp sensor and read its value
    if (mcpBot.begin(TEMP_BOT)) {
      sensorArray.mcpTempBot = mcpBot.readTempC();    
    }
    else {
      Serial.println("MCP9808 BOT not found");
#ifdef VERBOSE
      serialUdp("MCP9808 BOT not found");
#endif
      sensorArray.mcpTempBot = -99; 
    }


    // Read humidity and temperature from HTU21DF sensor
    if (htu.begin()) {
      sensorArray.htuTemp = htu.readTemperature();
      sensorArray.htuHumid = htu.readHumidity();
    }
    else {
      Serial.println("HTU21DF not found!");
#ifdef VERBOSE
      serialUdp("HTU21DF not found!");
#endif
      sensorArray.htuTemp = -99;
      sensorArray.htuHumid = -99;
    }
    // Calculate the cpu uptime since the last Setup in seconds.
    sensorArray.cpu_uptime = (millis())/1000;
 
    // Send UDP packet to the server ip address serverIp that's listening on port sndPort
    UdpSnd.beginPacket(serverIp, sndPort); // Initialize the packet send
    UdpSnd.write((byte *)&sensorArray, sizeof sensorArray); // Send the struct as UDP packet
    UdpSnd.endPacket(); // End the packet
    Serial.println("UDP packet sent...");
#ifdef VERBOSE
    serialUdp("UDP packet sent...");
#endif
   
    // Check if request was sent to Arduino
    packetSize = UdpRcv.parsePacket(); // Reads the packet size
    
    if(packetSize>0) { //if packetSize is >0, that means someone has sent a request
    parseUdpPacket();    
    } 

    // Renew DHCP lease - times out eventually if this is removed
    Ethernet.maintain();

    unsigned int endLoop = millis();
#ifdef VERBOSE
    serialUdp("Loop runtime:");
    serialUdp(String(endLoop-startLoop));
#endif
    delay(2000);
}





void parseUdpPacket(){

      UdpRcv.read(packetBuffer, UDP_TX_PACKET_MAX_SIZE); //Read the data request
      String command(packetBuffer); //Convert char array packetBuffer into a string called command
      
      if (command == "poke") {
          Serial.println("I've been poked!");
#ifdef VERBOSE
          serialUdp("I've been poked!");
#endif
          Watchdog.reset();
      }
      
      else if (command == "snapRelay_on") {
        digitalWrite(SNAP_RELAY, HIGH);
        sensorArray.snap_relay = true;
      }     
      
      else if (command == "snapRelay_off") {
        digitalWrite(SNAP_RELAY, LOW);
        sensorArray.snap_relay = false;
      }
      
      else if (command == "snapv2_0_on"){
        Serial.println("snapv2_0 on");
        digitalWrite(SNAPv2_0, LOW);
        sensorArray.snapv2_0 = true;
        }

      else if (command == "snapv2_0_off"){
        Serial.println("snapv2_0 off");
        digitalWrite(SNAPv2_0, HIGH);
        sensorArray.snapv2_0 = false;
        }

      else if (command == "snapv2_1_on"){
        Serial.println("snapv2_1 on");
        digitalWrite(SNAPv2_1, LOW);
        sensorArray.snapv2_1 = true;
        }

      else if (command == "snapv2_1_off"){
        Serial.println("snapv2_1 off");
        digitalWrite(SNAPv2_1, HIGH);
        sensorArray.snapv2_1 = false;
        }

      else if (command == "snapv2_2_on"){
        Serial.println("snapv2_2 on");
        digitalWrite(SNAPv2_2, LOW);
        sensorArray.snapv2_2 = true;
        }

      else if (command == "snapv2_2_off"){
        Serial.println("snapv2_2 off");
        digitalWrite(SNAPv2_2, HIGH);
        sensorArray.snapv2_2 = false;
        }

      else if (command == "snapv2_3_on"){
        Serial.println("snapv2_3 on");
        digitalWrite(SNAPv2_3, LOW);
        sensorArray.snapv2_3 = true;
        }

      else if (command == "snapv2_3_off"){
        Serial.println("snapv2_3 off");
        digitalWrite(SNAPv2_3, HIGH);
        sensorArray.snapv2_3 = false;
        }


      else if (command == "FEM_on") {
        digitalWrite(FEM, HIGH);
        sensorArray.fem = true;
      }
      
      else if (command == "FEM_off") {
        digitalWrite(FEM, LOW);
        sensorArray.fem = false;
      }
      
      else if (command == "PAM_on") {
        Serial.println("PAM on");
        digitalWrite(PAM, HIGH);
        sensorArray.pam = true;
      }
      
      else if (command == "PAM_off") {
        digitalWrite(PAM, LOW);
        sensorArray.pam = false;
      }
              
      else if (command == "reset") {
        bootReset();
      }
      else {
        serialUdp("Unknown command received..");
      }
      // Clear UDP packet buffer before sending another packet
      memset(packetBuffer, 0, UDP_TX_PACKET_MAX_SIZE);
       
    
}


void bootReset(){
   Serial.println("Resetting Bootloader..");
   serialUdp("Resetting Bootloader..");
   delay(500);
   digitalWrite(RESET, LOW);  
}


void serialUdp(String message){
    String debugMessage = String("NODE " + String(int(sensorArray.nodeID)) + ": " + message);
    UdpSer.beginPacket(serverIp, serPort);
    UdpSer.print(debugMessage);
    UdpSer.endPacket();
} 


