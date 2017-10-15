#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
#include <Time.h>
#include <TimeLib.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

#define ssid  ""
#define password ""

#define MAX_ATTEMPTS_FOR_TIME 5
#define MAX_ATTEMPTS 30

#define REDLED D6
#define WHITELED D5
#define GREENLED D8
#define BLUELED D7

#define IST 19800

#define CHECK_SWITCH_TIME 60000 

// every 10 mins
#define CHECK_SCHEDULES 600000

#define MAX_SCHEDULES 5

const char *host = "sheets.googleapis.com";
const int httpsPort = 443;

// connect to a 2 column google sheet with the following layout 
// |Start Time    | End Time
// | 1730         | 2215
// | 530          | 615
// | 745          | 753
// | 1730         | 1750
// replace the sheet id and your api key
const char *sheetsURI = "/v4/spreadsheets/YOUR-SHEET_ID/values/Sheet1!A2:B7?key=YOURKEY";


const size_t MAX_CONTENT_SIZE = 512;

String localIP = "";

unsigned int localPort = 2390;      // local port to listen for UDP packets
IPAddress timeServerIP; // time.nist.gov NTP server address
const char* ntpServerName = "time.nist.gov";
const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message
byte packetBuffer[ NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets

// A UDP instance to let us send and receive packets over UDP
WiFiUDP udp;

time_t timeVal; 
ESP8266WebServer server;

unsigned long ledCounter = 0, switchCounter =0, counter = 0, scheduleCount = 0;
const String INDEX_PAGE = "<!DOCTYPE html>\r\n<html>\r\n<head> \r\n<title>On/Off Switch </title>\r\n</head>\r\n<style> \r\n.button_on {\r\n  display: block;\r\n  height:100px;\r\n  width:80%;\r\n  padding: 15px 25px;\r\n  font-size: 24px;\r\n  font-size: 4.0vw;\r\n  margin-left:auto;\r\n  margin-right:auto;\r\n  margin-bottom:25px;\r\n  margin-top:auto;  \r\n  cursor: pointer;\r\n  text-align: center;\r\n  text-decoration: none;\r\n  outline: none;\r\n  color: #fff;\r\n  background-color: GREEN;\r\n  border: none;\r\n  border-radius: 15px;\r\n  box-shadow: 0 9px #999;\r\n}\r\n\r\n.button_off {\r\n  display: block;\r\n  height:100px;\r\n  width:80%;  \r\n  padding: 15px 25px;\r\n  font-size: 24px;\r\n  font-size: 4.0vw;\r\n  margin-left:auto;\r\n  margin-right:auto;  \r\n  margin-bottom:auto;\r\n  margin-top:auto;  \r\n  cursor: pointer;\r\n  text-align: center;\r\n  text-decoration: none;\r\n  outline: none;\r\n  color: #fff;\r\n  background-color: RED;\r\n  border: none;\r\n  border-radius: 15px;\r\n  box-shadow: 0 9px #999;\r\n}\r\n.button_on:hover {background-color: #3e8e41}\r\n.button_off:hover {background-color: #ff3341}\r\n\r\n.button_off:active {\r\n  background-color: #ff3341;\r\n  box-shadow: 0 5px #666;\r\n  transform: translateY(4px);\r\n\r\n}\r\n\r\n.button_on:active {\r\n  background-color: #3e8e41;\r\n  box-shadow: 0 5px #666;\r\n  transform: translateY(4px);\r\n\r\n}\r\n</style>\r\n<body>\r\n\r\n<form action=\"/\" method=\"post\">\r\n<table>\r\n  <button name=\"submit\" align=\"center\" class=\"button_on\" type=\"submit\" value=\"ON\"> ON </button>\r\n  <button name=\"submit\" align=\"center\" class=\"button_off\" type=\"submit\" value=\"OFF\"> OFF </button> \r\n </table>\r\n</form> \r\n\r\n</body>\r\n</html>";
int pins [] = {REDLED, GREENLED, WHITELED, BLUELED};
unsigned int startTime, stopTime; 

typedef struct {
  int startTime;
  int endTime;
} t_schedule;

int g_definedSchedules = 0;
t_schedule g_schedules[MAX_SCHEDULES];

void setup()
{
  int attempt = 0;

  pinMode(D1, OUTPUT);
  pinMode(D5, OUTPUT);
  pinMode(D6, OUTPUT);
  pinMode(D7, OUTPUT);
  pinMode(D8, OUTPUT);

  timeVal = 0;  

  if(connectToWifi()) {
    Serial.println("Connected to Wifi");
    digitalWrite(GREENLED, LOW);

    while(attempt < MAX_ATTEMPTS_FOR_TIME) {
      timeVal = getTime();
      if(timeVal) // got time !
        break;

      attempt++; 
    }

    Serial.print("Get time value : ");
    Serial.println(timeVal);    
  } else {
    digitalWrite(D1, HIGH);
  }

  scheduleCount = switchCounter = ledCounter = millis();

  if(timeVal > 0) {
    setTime(timeVal);
  }

  setSyncProvider(getTime);
  setSyncInterval(600);    

  setSchedules(host, sheetsURI);
}

void loop() 
{
    server.handleClient(); 

    if(millis() - switchCounter > CHECK_SWITCH_TIME) { // check every 1 minute(s)
      unsigned int t = hour()*100 + minute();

      if(isTimeInSchedule(t)) {
        digitalWrite(D1, LOW); // turn ON on NC at relay

        digitalWrite(D0, HIGH);

        Serial.print("Turning ON LED due to time :");
        Serial.println(t);

      } else {
        digitalWrite(D1, HIGH);

        digitalWrite(D0, LOW);        
        Serial.print("Turning OFF LED due to time :");
        Serial.println(t);        
      }

      switchCounter = millis();
    }

    // gets leds to dance by seconds 
    if(millis() - ledCounter > (150-2*second())) {
      counter++; 

      if(minute()%2) {
        digitalWrite(pins[counter%4], !digitalRead(pins[counter%4]));
      } else {
        digitalWrite(pins[3-counter%4], !digitalRead(pins[3-counter%4]));
      }
      ledCounter = millis();
    }

    if(millis() - scheduleCount > CHECK_SCHEDULES) {
      // get schedules from google sheets
      setSchedules(host, sheetsURI);
      scheduleCount = millis();
    }
}

bool connectToWifi() {
  byte maxAttempts = 0;

  IPAddress ip(192, 168, 1, 201);    
  IPAddress gateway(192, 168, 1, 1);
  IPAddress subnet(255, 255, 255, 0);

  WiFi.config(ip, gateway, subnet); // try to assign a static IP address to the device


  WiFi.begin(ssid, password);
  Serial.begin(115200);
  while(WiFi.status() != WL_CONNECTED) {
    if(maxAttempts++ >= MAX_ATTEMPTS) {
      Serial.println("Could not connect to Wifi ! ");
      return false;
    }

    Serial.print(".");

    for(int i = 0; i < 10; i++) {
      digitalWrite(GREENLED, HIGH);
      digitalWrite(REDLED, HIGH);        
      delay(20);

      digitalWrite(GREENLED, LOW);
      digitalWrite(REDLED, LOW);
      delay(20);
    }

  }
  Serial.println("");
  Serial.println("IP Address:");
  Serial.println(WiFi.localIP());

  server.on("/", handleRoot);
  server.begin();

  Serial.println("Starting UDP");
  udp.begin(localPort);
  Serial.print("Local port: ");

  Serial.println(udp.localPort());  

  return true;
}

void handleRoot()
{
  if (server.hasArg("submit")) {
    handleSubmit();
  }
  else {
    Serial.print("Going to index page, current value : ");
    Serial.println(digitalRead(D1));
    server.send(200, "text/html", INDEX_PAGE);
  }
}

void handleSubmit()
{

  String submit = server.arg("submit");

  if(submit == "ON") {
    Serial.print("Got message : ");
    Serial.println(submit);
    digitalWrite(D1, LOW); // turn ON on NC at relay
  } else if (submit == "OFF") {
    Serial.print("Got message : ");
    Serial.println(submit);
    digitalWrite(D1, HIGH); // turn OFF on NC at relay
  }
  server.send(200, "text/html", INDEX_PAGE);
}

time_t getTime() 
{
  WiFi.hostByName(ntpServerName, timeServerIP); 
  sendNTPpacket(timeServerIP); // send an NTP packet to a time server

  // wait to see if a reply is available
  delay(4000);

  int cb = udp.parsePacket();

  if (!cb) {
    Serial.println("no packet yet, recursively trying again");
    return 0;
  }
  else {
    Serial.print("packet received, length=");
    Serial.println(cb);
    // We've received a packet, read the data from it
    udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer

    //the timestamp starts at byte 40 of the received packet and is four bytes,
    // or two words, long. First, esxtract the two words:
    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
    // combine the four bytes (two words) into a long integer
    // this is NTP time (seconds since Jan 1 1900):
    unsigned long secsSince1900 = highWord << 16 | lowWord;

    Serial.print("Seconds since Jan 1 1900 = " );
    Serial.println(secsSince1900);

    // now convert NTP time into everyday time:
    Serial.print("Unix time = ");
    // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
    const unsigned long seventyYears = 2208988800UL;
    // subtract seventy years:
    unsigned long epoch = secsSince1900 - seventyYears;
    // print Unix time:
    Serial.println(epoch);

    epoch += IST; // IST ahead by 5 hrs 30 mins (19800 seconds)

    return epoch;
  } 
}

// send an NTP request to the time server at the given address
unsigned long sendNTPpacket(IPAddress& address)
{
  Serial.println("sending NTP packet...");
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  udp.beginPacket(address, 123); //NTP requests are to port 123
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();
}

boolean syncTimeSheetFromGoogleSheets(const char* host, const char* uri) {
  Serial.print("Trying to connect to ");
  Serial.println(host);

  // Use WiFiClient 
  WiFiClientSecure client;
  if (!client.connect(host, httpsPort)) {
    Serial.print("Failed to connect to :");
    Serial.println(host);
    return false;
  }

  Serial.println("connected !....");

  client.print(String("GET ") + uri + " HTTP/1.1\r\n" +
             "Host: " + host + "\r\n" +
             "User-Agent: ESP8266\r\n" +
             "Accept: */*\r\n" +
             "Connection: close\r\n\r\n");

  // bypass HTTP headers
  while (client.connected()) {
    String line = client.readStringUntil('\n');
    Serial.print( "Header: ");
    Serial.println(line);
    if (line == "\r") {
      break;
    }
  }

  const size_t BUFFER_SIZE =
      JSON_OBJECT_SIZE(8)    // the root object has 8 elements
      + MAX_CONTENT_SIZE;    // additional space for strings

  // Allocate a temporary memory pool
  DynamicJsonBuffer jsonBuffer(BUFFER_SIZE);

  JsonObject& root = jsonBuffer.parseObject(client);

  if (!root.success()) {
    Serial.println("JSON parsing failed!");
    return false;
  }  

  Serial.println("Parsing a success ! ");

  int index = 0;
  int times[2], tindex = 0;

  g_definedSchedules = 0;

  JsonArray& scheduleArray = root["values"];
  for (JsonArray& t : scheduleArray) {
    tindex = 0;
    for (JsonVariant o : t) {
      Serial.print("Array : ");
      Serial.println(o.as<int>());  

      times[tindex++] =  o.as<int>();
    }  
    Serial.println(tindex);
    g_schedules[g_definedSchedules].startTime = times[0];
    g_schedules[g_definedSchedules].endTime = times[1];

    g_definedSchedules++; 

    if(g_definedSchedules > MAX_SCHEDULES-1)  
      break;
  }

  for(int i = 0; i <g_definedSchedules;i++ ) {
    Serial.print("Scheule : ");
    Serial.print(g_schedules[i].startTime) ;
    Serial.print(", ");
    Serial.println(g_schedules[i].endTime) ;
  }

  return true;
}

boolean isTimeInSchedule(unsigned int t)
{
  boolean inSchedule = false; 

  for(int i = 0; i <g_definedSchedules;i++ ) {
    if(t >= g_schedules[i].startTime && t <= g_schedules[i].endTime) {
      inSchedule = true;
      break;
    }
  }

  return inSchedule;
}

void setSchedules(const char* host, const char* uri) 
{
  if(syncTimeSheetFromGoogleSheets(host, uri)) {
    Serial.println("Successfully synched time schedules");
  } else {
    Serial.println("Setting default schedules");

    // set to default hard-coded schedules 
    g_schedules[0].startTime = 1730;
    g_schedules[0].endTime = 2215;   

    g_definedSchedules = 1; // define just one schedule
  }
}