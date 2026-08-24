#include <ArduinoJson.h>
#include "bootlogo.h"
#include "mainscreen-landscape.h"
#include "watchscreen.h"
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <TFT_eSPI.h> // Graphics and font library for ILI9341 driver chip
#include <SPI.h>
#include <EEPROM.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

#define power_ctl_pin 16
#define validator_pin 5
#define sensor_pir_pin 4
#define TFT_BL   2           // TFT LED back-light control pin

TFT_eSPI tft = TFT_eSPI();  // Invoke library, pins defined in User_Setup.h

//---------------------------------------------------------------Time Var--------------------------------------------------
const long utcOffsetInSeconds = 25200;//<--------------offset GMT JAKARTA
String weekDays[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
String months[12] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
String weekdaynow = "";
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds);

//-----------------------------------------------------------------Analog Watch------------------------
float sx = 0, sy = 1, mx = 1, my = 0, hx = -1, hy = 0;    // Saved H, M, S x & y multipliers
float sdeg = 0, mdeg = 0, hdeg = 0;
uint16_t osx = 120, osy = 180, omx = 120, omy = 180, ohx = 120, ohy = 180; // Saved H, M, S x & y coords
uint16_t x0 = 0, x1 = 0, yy0 = 0, yy1 = 0;
uint32_t targetTime = 0;                    // Asynchronus 1 second timeout from millis()
int hh, mm, ss;  // Get H, M, S from compile time
boolean initial = 1;
//-----------------------------------------------------------------Server Var------------------------------------------------
String JSONSERVER_IP = "";
String POST_USERNAME = "";
String POST_PASSWORD = "";
String POST_DESKNAME = "";
String POST_BOOKDATE;
String POST_JSON_DATA;
String jsonData;
int httpCode = 0;
const char* jsName;
const char* jsEmail;
const char* jsDept;
const char* jsMessage;
//-----------------------------------------------------------------Main Var------------------------------------------------
const uint8_t CTR = 30; //Counting time remainder 30 Sec
const unsigned long T_Max_Rem = 30000;//-------------start reminder on from last 30 sec
unsigned long T_Limit = 180000;//------------<<periode of Active Relay Without PIR Interaction 1000 equals 1 second<<---------//
unsigned long T_Timer = 0;
unsigned long TCP_Timer = 0;
unsigned long PIR_T = 0;
unsigned long VAL_T = 0;
unsigned long TRSec = 0;
unsigned long TsigPir;
uint8_t flag_occupied = 0;
uint8_t fint_PIR = 0;
uint8_t fint_VAL = 0;
uint8_t STR = 0;
uint8_t TR;
uint8_t signalPIR = 1;
String ssidWifi;    //<---Stored Wifi SSID
String passwordWifi;//<---Stored Wifi Password
String greeting;

//-------------------------------------------------------------VOID INTERRUPT HERE ------------------------------------------------
void ICACHE_RAM_ATTR PIR_interrupt() {
  fint_PIR = 1;
}

void ICACHE_RAM_ATTR Validator_interrupt() {
  fint_VAL = 1;
}

//-----------------------------------------------------------------Get NTP TIME UPDATE-------------------------
void Serial_ntpupdate() {

  timeClient.update();
  // Serial.print(timeClient.getEpochTime());
  Serial.print(weekDays[timeClient.getDay()]);
  Serial.print(", ");
  Serial.print(timeClient.getHours());
  Serial.print(":");
  Serial.print(timeClient.getMinutes());
  Serial.print(":");
  Serial.println(timeClient.getSeconds());
}

//-----------------------------------------------------------------Get Booking Date UPDATE-------------------------
String getBookDate() {

  timeClient.update();
  const time_t epochTime = timeClient.getEpochTime();
  struct tm  ts;
  char   bufff[80];
  ts = *localtime(&epochTime);
  //strftime(bufff, sizeof(bufff), "%a %Y-%m-%d %H:%M:%S %Z", &ts);
  strftime(bufff, sizeof(bufff), "%Y-%m-%d %H:%M:%S", &ts);
  String bookDate = bufff;
  bookDate = bookDate.substring(0, 10) + "T" + bookDate.substring(11);
  return (bookDate);
}

void printDate() {
  timeClient.update();
  const time_t epochTime = timeClient.getEpochTime();
  struct tm  ts;
  char   bufff[20];
  ts = *localtime(&epochTime);

  //tft.setCursor(167, 21); //Portrait position
  tft.setCursor(247, 21); //landscape position
  tft.setTextSize(2);
  strftime(bufff, sizeof(bufff), "%d/%m", &ts);
  tft.print(bufff);
  //tft.setCursor(174, 38);//Portrait position
  tft.setCursor(254, 38);//Landscape position
  strftime(bufff, sizeof(bufff), "%Y", &ts);
  tft.print(bufff);
  tft.setTextSize(1);
}
void printDateWatch() {
  timeClient.update();
  const time_t epochTime = timeClient.getEpochTime();
  struct tm  ts;
  char   bufff[20];
  ts = *localtime(&epochTime);

  
  tft.setCursor(21, 217); //landscape position
  tft.setTextSize(2);
  strftime(bufff, sizeof(bufff), "%d-%m-%Y", &ts);
  tft.print(bufff);
  tft.setTextSize(1);
}
//--------------------------SET LCD-----------------------------------
void tft_init()
{
  tft.init();
  tft.setSwapBytes(true);
  tft.setRotation(0);     // Set display orientation Potrait
  tft.setTextSize(1);
  tft.fillScreen(TFT_BLACK); // Clear Up the screen
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
}

////--------------------------Vector Drawing Analog Watch Face Portrait-----------------------------------
//void drawAnalogWatchFace() {
//  tft.setCursor(0, 0);
//  tft.setTextColor(TFT_WHITE);
//  tft.fillCircle(120, 180, 100, TFT_WHITE);
//  tft.fillCircle(120, 180, 95, TFT_BLACK);
//
//  for (int i = 0; i < 360; i += 30) {
//    sx = cos((i - 90) * 0.0174532925);
//    sy = sin((i - 90) * 0.0174532925);
//    x0 = sx * 96 + 120;
//    yy0 = sy * 96 + 180;
//    x1 = sx * 88 + 120;
//    yy1 = sy * 88 + 180;
//    tft.drawLine(x0, yy0, x1, yy1, TFT_GREEN);
//  }
//
//  for (int i = 0; i < 360; i += 6) {
//    sx = cos((i - 90) * 0.0174532925);
//    sy = sin((i - 90) * 0.0174532925);
//    x0 = sx * 88 + 120;
//    yy0 = sy * 88 + 180;
//    tft.drawPixel(x0, yy0, TFT_WHITE);
//    if (i == 0 || i == 180) tft.fillCircle(x0, yy0, 2, TFT_WHITE);
//    if (i == 90 || i == 270) tft.fillCircle(x0, yy0, 2, TFT_WHITE);
//  }
//  targetTime = millis() + 1000;
//}
//--------------------------Vector Drawing Analog Watch Face Landscape-----------------------------------
void drawAnalogWatchFace() {
  int HorCenterPoint = 120;
  int VerCenterPoint = 100;
  tft.setCursor(0, 0);
  tft.setTextColor(TFT_WHITE);
  tft.fillCircle(HorCenterPoint, VerCenterPoint, 95, TFT_WHITE);
  tft.fillCircle(HorCenterPoint, VerCenterPoint, 90, TFT_BLACK);

  for (int i = 0; i < 360; i += 30) {
    sx = cos((i - 90) * 0.0174532925);
    sy = sin((i - 90) * 0.0174532925);
    x0 = sx * 91 + HorCenterPoint;
    yy0 = sy * 91 + VerCenterPoint;
    x1 = sx * 82 + HorCenterPoint;
    yy1 = sy * 82 + VerCenterPoint;
    tft.drawLine(x0, yy0, x1, yy1, TFT_GREEN);
  }

  for (int i = 0; i < 360; i += 6) {
    sx = cos((i - 90) * 0.0174532925);
    sy = sin((i - 90) * 0.0174532925);
    x0 = sx * 82 + HorCenterPoint;
    yy0 = sy * 82 + VerCenterPoint;
    tft.drawPixel(x0, yy0, TFT_WHITE);
    if (i == 0 || i == 180) tft.fillCircle(x0, yy0, 2, TFT_WHITE);
    if (i == 90 || i == 270) tft.fillCircle(x0, yy0, 2, TFT_WHITE);
  }
  targetTime = millis() + 1000;
}
//--------------------------Sync to ntp time -----------------------------------
void syncAnalogtime() {
  timeClient.update();
  
    
    String day = weekDays[timeClient.getDay()];
    if (weekdaynow != day){
       weekdaynow = day;
    tft.fillRect(0, 210, 320, 60, TFT_BLACK); //---------blankup day area x,y,width,height
//  tft.drawCentreString(day, 120, 285, 4);//-------Portrait Position
    tft.drawCentreString(day, 240, 213, 4);//-------Landscape Position
    printDateWatch();
  }
  hh = timeClient.getHours();
  mm = timeClient.getMinutes();
  ss = timeClient.getSeconds();

}

////--------------------------Vector Drawing Analog Watch Hand Portrait-----------------------------------
//void drawAnalogTime() {
//  if (targetTime < millis()) {
//    targetTime += 1000;
//    ss++;              // second
//    if (ss == 60) {
//      ss = 0;
//      mm++;            // minute
//      if (mm > 59) {
//        mm = 0;
//        hh++;          // hour
//        if (hh > 23) {
//          hh = 0;
//          printDate();
//        }
//      }
//    }
//
//    sdeg = ss * 6;
//    mdeg = mm * 6 + sdeg * 0.01666667;
//    hdeg = hh * 30 + mdeg * 0.0833333;
//    hx = cos((hdeg - 90) * 0.0174532925);
//    hy = sin((hdeg - 90) * 0.0174532925);
//    mx = cos((mdeg - 90) * 0.0174532925);
//    my = sin((mdeg - 90) * 0.0174532925);
//    sx = cos((sdeg - 90) * 0.0174532925);
//    sy = sin((sdeg - 90) * 0.0174532925);
//
//    if (ss == 0 || initial) {
//      initial = 0;
//      tft.drawLine(ohx, ohy, 120, 181, TFT_BLACK);
//      ohx = hx * 50 + 121;
//      ohy = hy * 50 + 181;
//      tft.drawLine(omx, omy, 120, 181, TFT_BLACK);
//      omx = mx * 78 + 120;
//      omy = my * 78 + 181;
//    }
//
//    tft.drawLine(osx, osy, 120, 181, TFT_BLACK);
//    osx = sx * 84 + 121;
//    osy = sy * 84 + 181;
//    tft.drawLine(osx, osy, 120, 181, TFT_RED);
//    tft.drawLine(ohx, ohy, 120, 181, TFT_WHITE);
//    tft.drawLine(omx, omy, 120, 181, TFT_WHITE);
//    tft.drawLine(osx, osy, 120, 181, TFT_RED);
//    tft.fillCircle(120, 181, 6, TFT_WHITE);
//    tft.fillCircle(120, 181, 3, TFT_RED);
//  }
//}
//--------------------------Vector Drawing Analog Watch Hand Landscape-----------------------------------
void drawAnalogTime() {

  int HorPivotPoint = 120;
  int VerPivotPoint = 100;

  if (targetTime < millis()) {
    targetTime += 1000;
    ss++;              // second
    if (ss == 60) {
      ss = 0;
      mm++;            // minute
      if (mm > 59) {
        mm = 0;
        hh++;          // hour
        if (hh > 23) {
          hh = 0;
          printDate();
        }
      }
    }

    sdeg = ss * 6;
    mdeg = mm * 6 + sdeg * 0.01666667;
    hdeg = hh * 30 + mdeg * 0.0833333;
    hx = cos((hdeg - 90) * 0.0174532925);
    hy = sin((hdeg - 90) * 0.0174532925);
    mx = cos((mdeg - 90) * 0.0174532925);
    my = sin((mdeg - 90) * 0.0174532925);
    sx = cos((sdeg - 90) * 0.0174532925);
    sy = sin((sdeg - 90) * 0.0174532925);

    if (ss == 0 || initial) {
      initial = 0;
      tft.drawLine(ohx, ohy, HorPivotPoint, VerPivotPoint, TFT_BLACK);
      ohx = hx * 45 + HorPivotPoint;
      ohy = hy * 45 + VerPivotPoint;
      tft.drawLine(omx, omy, HorPivotPoint, VerPivotPoint, TFT_BLACK);
      omx = mx * 60 + HorPivotPoint;
      omy = my * 60 + VerPivotPoint;
    }

    tft.drawLine(osx, osy, HorPivotPoint, VerPivotPoint, TFT_BLACK);
    osx = sx * 70 + HorPivotPoint;
    osy = sy * 70 + VerPivotPoint;
    tft.drawLine(osx, osy, HorPivotPoint, VerPivotPoint, TFT_RED);
    tft.drawLine(ohx, ohy, HorPivotPoint, VerPivotPoint, TFT_WHITE);
    tft.drawLine(omx, omy, HorPivotPoint, VerPivotPoint, TFT_WHITE);
    tft.drawLine(osx, osy, HorPivotPoint, VerPivotPoint, TFT_RED);
    tft.fillCircle(HorPivotPoint, VerPivotPoint, 6, TFT_WHITE);
    tft.fillCircle(HorPivotPoint, VerPivotPoint, 3, TFT_RED);
  }
}
//--------------------------ClearUp LCD-----------------------------------
void tft_clean()
{
  tft.fillScreen(TFT_BLACK); // Clear Up the screen
  tft.setCursor(0, 0);
}

//------------------------welcome Screen---------------------------------
void tft_bootlogo()
{
  delay(10);
  tft.pushImage(0, 0, PBLwidth , PBLheight, bootlogo);
}

//------------------------Main Screen---------------------------------
void tft_mainscreen()
{
  delay(10);
  tft.pushImage(0, 0, PMwidth , PMheight, mainscreen);
  printDate();
}
void tft_watchscreen()
{
  delay(10);
  tft.pushImage(0, 0, PWwidth , PWheight, watchscreen);
  printDateWatch();
  String day = weekDays[timeClient.getDay()];
  tft.drawCentreString(day, 240, 213, 4);//-------Landscape Position
  
}
//-------------------------------------------------------Conversion Char*String to Byte for IP and Mac Address Assignment From Serial Comm String Data--------------------------------------------------------------------------------------
void parseBytes(const char* str, char sep, byte* outputBytes, int maxBytes, int base) {
  for (int i = 0; i < maxBytes; i++) {
    outputBytes[i] = strtoul(str, NULL, base);  // Convert byte
    str = strchr(str, sep);             // Find next separator
    if (str == NULL || *str == '\0') {
      break;                            // No more separators, exit
    }
    str++;                              // Point to next character after separator
  }
}

void readServerParameter(){
  //----------------------------------------------------Flush SERVER Variable------
JSONSERVER_IP = "";
POST_USERNAME = "";
POST_PASSWORD = "";
POST_DESKNAME = "";
  
  //----------------------------------------------------always assign SERVER PARAMETER from eeprom from Here------
  int SIAlength = EEPROM.read(0x96);//Read Stored Length SERVER IP in address 0x97
  for (int i = 0; i < SIAlength; i++)
  {
    JSONSERVER_IP = JSONSERVER_IP + char(EEPROM.read(0x97 + i)); //Read one by one with starting address of 0x97
  }
  int SUNlength = EEPROM.read(0xFA);
  for (int i = 0; i < SUNlength; i++)
  {
    POST_USERNAME = POST_USERNAME + char(EEPROM.read(0xFB + i)); //Read one by one with starting address of 0xFB
  }
  int SPASlength = EEPROM.read(0x10C);
  for (int i = 0; i < SPASlength; i++)
  {
    POST_PASSWORD = POST_PASSWORD + char(EEPROM.read(0x10D + i)); //Read one by one with starting address of 0x11
  }
  int SPDNlength = EEPROM.read(0x121);
  for (int i = 0; i < SPDNlength; i++)
  {
    POST_DESKNAME = POST_DESKNAME + char(EEPROM.read(0x122 + i)); //Read one by one with starting address of 0x11
  }

  //----------------------------------------------------always assign ssid and password from eeprom Until Here------
  }
  
void setServerParameter() {
  
  Serial.println("Do You want to set Server Parameter?");
  Serial.println("Press (1) Enter = Yes!");
  Serial.println("press (2) or any key Enter = No!");
  Serial.print("Answer: " );

  unsigned long tNow = millis();
  unsigned long timecheck;

  while (!Serial.available() && millis() - tNow <= 5000) {
    timecheck = millis() - tNow;
  }
  int ianswer = Serial.read();
  Serial.println((char)ianswer);
  
//-----------------------------------------------------save SERVER USERNAME and PASSWORD and Table NAME to eeprom From Here-----------------------------
  if (timecheck < 4999) {
    if (ianswer == 49) {
      String Flusher = Serial.readStringUntil('\n');
      Serial.print("SERVER IP ADDRESS(MAX100Character): ");
      while (!Serial.available() > 0 ) {}
      JSONSERVER_IP = Serial.readStringUntil('\n');
      Serial.println(JSONSERVER_IP);
      int SIAlength = JSONSERVER_IP.length();
      for (int i = 0; i < SIAlength+1; i++) {
        if(i < SIAlength){EEPROM.write(0x97 + i, JSONSERVER_IP[i]);} //Write start at address 0x97
        if(i == SIAlength){EEPROM.write(0x97 + i, NULL);} //Write start at address 0x97
      }
      EEPROM.write(0x96, SIAlength);//Write ssid String length in address 0x96
      EEPROM.commit();    //Store data to EEPROM
      Serial.print("SERVER User Name(MAX20Character): ");
      while (!Serial.available() > 0 ) {}
      String POST_USERNAME = Serial.readStringUntil('\n');
      Serial.println(POST_USERNAME);
      int SUNlength = POST_USERNAME.length();
      for (int i = 0; i < SUNlength+1; i++) {
        if(i < SUNlength){EEPROM.write(0xFB + i, POST_USERNAME[i]);} //Write start at address 0xFB
        if(i == SUNlength){EEPROM.write(0xFB + i, NULL);}
      }
      EEPROM.write(0xFA, SUNlength);//Write ssid String length in address 0xFA
      EEPROM.commit();    //Store data to EEPROM
      
      Serial.print("SERVER Password(MAX16Character): ");
      while (!Serial.available() > 0 ) {}
      String POST_PASSWORD = Serial.readStringUntil('\n');
      Serial.println(POST_PASSWORD);
      int SPASlength = POST_PASSWORD.length();
      for (int i = 0; i < SPASlength+1; i++) {
        if(i < SPASlength){EEPROM.write(0x10D + i, POST_PASSWORD[i]);} //Write start at address 0xFB
        if(i == SPASlength){EEPROM.write(0x10D + i, NULL);}
      }
      EEPROM.write(0x10C, SPASlength);//Write ssid String length in address 0xFA
      EEPROM.commit();    //Store data to EEPROM

      Serial.print("DESK NAME(MAX16Character): ");
      while (!Serial.available() > 0 ) {}
      String POST_DESKNAME = Serial.readStringUntil('\n');
      Serial.println(POST_DESKNAME);
      int SPDNlength = POST_DESKNAME.length();
      for (int i = 0; i < SPDNlength+1; i++) {
        if(i < SPDNlength){EEPROM.write(0x122 + i, POST_DESKNAME[i]);} //Write start at address 0x122
        if(i == SPDNlength){EEPROM.write(0x122 + i, NULL);}
      }
      EEPROM.write(0x121, SPDNlength);//Write ssid String length in address 0xFA
      EEPROM.commit();    //Store data to EEPROM

      //-----------------------------------------------------save SERVER USERNAME and PASSWORD and Table NAME to eeprom until here-----------------------------
    }
  }
  else {
    Serial.println("TIMEOUT!");
    delay(500);
  }
}
//--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
void wifi_staticIPset()
{
  Serial.println("Do You want to set Local IP Automatic(DHCP) OR Manually?");
  Serial.println("(1) Yes!");
  Serial.println("(press 2 or any key) No!");

  unsigned long tNow = millis();
  unsigned long timecheck;

  while (!Serial.available() && millis() - tNow <= 5000) {
    timecheck = millis() - tNow;
  }

  int ianswer = Serial.read();
  Serial.println((char)ianswer);

  if (timecheck < 4999) {
    if (ianswer == 49) {
      String Flusher = Serial.readStringUntil('\n');   
      Serial.flush();

      Serial.println("Local IP ADDRESS Set To Manual or Automatic: ");
      Serial.println("(Press 1 and Enter)-------------> IP Set MANUAL");
      Serial.println("(Press 2 or any and Enter)------> IP Set AUTOMATIC(DHCP)");
      
      while (!Serial.available() > 0 ) {}
      int IP_MA = Serial.read();
      
      if (IP_MA == 49) {
            String Flusher = Serial.readStringUntil('\n');   
            Serial.flush();
//-------------------------------------------------------------------------------SET TO EEPROM IP IS SET TO MANUAL CONFIG
            EEPROM.write(0x143, IP_MA);//Write 1 in address 0x143 means IP address set manually
            EEPROM.commit();    //Store data to EEPROM
//-------------------------------------------------------------------------------SET TO EEPROM Device IP ADDRESS            
            Serial.print("Local IP ADDRESS: ");
            while (!Serial.available() > 0 ) {}
            String Static_IPaddress = Serial.readStringUntil('\n');
            Serial.println(Static_IPaddress);
            int A_len = Static_IPaddress.length();
            for (int i = 0; i < A_len + 1; i++) {
                if(i < A_len){EEPROM.write(0x145 + i, Static_IPaddress[i]);} //Write start at address 0x145
                }
            EEPROM.write(0x144, A_len);//Write IP length in address 0x144
            EEPROM.commit();    //Store data to EEPROM
            Serial.flush();
//-------------------------------------------------------------------------------SET TO EEPROM IP SUBNET MASK
            Serial.print("IP SubNet: ");
            while (!Serial.available() > 0) {}
            String Static_IPsubnet = Serial.readStringUntil('\n');
            Serial.println(Static_IPsubnet);
            int C_len = Static_IPsubnet.length();
            for (int i = 0; i < C_len + 1; i++) {
                if(i < C_len){EEPROM.write(0x156 + i, Static_IPsubnet[i]);} //Write start at address 0x156
                }
            EEPROM.write(0x155, C_len);//Write IPSubnet length in address 0x155
            EEPROM.commit();    //Store data to EEPROM
            Serial.flush();
//-------------------------------------------------------------------------------SET TO EEPROM GATEWAY
            Serial.print("IP Gateway: ");
            while (!Serial.available() > 0 ) {}
            String Static_IPgateway = Serial.readStringUntil('\n');
            Serial.println(Static_IPgateway);
            int B_len = Static_IPgateway.length();
            for (int i = 0; i < B_len + 1; i++) {
                if(i < B_len){EEPROM.write(0x167 + i, Static_IPgateway[i]);} //Write start at address 0x167
                }
            EEPROM.write(0x166, B_len);//Write IPGateway length in address 0x166
            EEPROM.commit();    //Store data to EEPROM
            Serial.flush();
//-------------------------------------------------------------------------------SET TO EEPROM DNS1           
            Serial.print("DNS 1 : ");
            while (!Serial.available() > 0 ) {}
            String Static_IPdns = Serial.readStringUntil('\n');
            Serial.println(Static_IPdns);
            int D_len = Static_IPdns.length();
            for (int i = 0; i < D_len + 1; i++) {
                if(i < D_len){EEPROM.write(0x178 + i, Static_IPdns[i]);} //Write start at address 0x178
                }
            EEPROM.write(0x177, D_len);//Write IP_DNS1 length in address 0x177
            EEPROM.commit();    //Store data to EEPROM
            Serial.flush();
//-------------------------------------------------------------------------------SET TO EEPROM DNS2                 
            Serial.print("DNS 2 : ");
            while (!Serial.available() > 0 ) {}
            String Static_IPdnssecondary = Serial.readStringUntil('\n');
            Serial.println(Static_IPdnssecondary);
            int E_len = Static_IPdnssecondary.length();
            for (int i = 0; i < E_len + 1; i++) {
                if(i < E_len){EEPROM.write(0x189 + i, Static_IPdnssecondary[i]);} //Write start at address 0x189
                }
            EEPROM.write(0x188, E_len);//Write IP_DNS2 length in address 0x188
            EEPROM.commit();    //Store data to EEPROM
            Serial.flush();
      }
//-------------------------------------------------------------------------------SET TO EEPROM DEVICE IP IS SET AUTOMATIC(DHCP)      
      else{
        EEPROM.write(0x143, 0);//Write 0 in address 0x143 means IP address set Automatic(DHCP)
        EEPROM.commit();    //Store data to EEPROM
        String Flusher = Serial.readStringUntil('\n');   
      }
    }
    else{String Flusher = Serial.readStringUntil('\n');}
  }
  else {
  String Flusher = Serial.readStringUntil('\n');
  Serial.println("TIMEOUT!");
  delay(200);
  }
}
      
void deviceIpSet(){
int IP_MA = EEPROM.read(0x143);//Read Stored IP address DHCP Status at memory address 0x143;
if(IP_MA == 49){
Serial.println("IP Setting is Set To Manual Configuration!"); 
//----------------------------------------------------Flush IP Variable------
String Static_IPaddress = "";
String Static_IPgateway = "";
String Static_IPsubnet = "";
String Static_IPdns = "";
String Static_IPdnssecondary = "";
char StaticIPaddress[16];
char StaticIPgateway[16];
char StaticIPsubnet[16];
char StaticIPdns[16];
char StaticIPdnssecondary[16];
//--------------------------------------------------------------------------------------Read Stored Device IP Address From Memory---------------  
  int A_len = EEPROM.read(0x144);//Read Stored Length Device IP in address 0x97
  for (int i = 0; i < A_len; i++)
  {
    Static_IPaddress = Static_IPaddress + char(EEPROM.read(0x145 + i)); //Read one by one with starting address of 0x97
  }
  
  int B_len = EEPROM.read(0x155);//Read Stored Length Subnet Mask IP in address 0x155
  for (int i = 0; i < B_len; i++)
  {
    Static_IPsubnet = Static_IPsubnet + char(EEPROM.read(0x156 + i)); //Read one by one with starting address of 0x156
  }
  
  int C_len = EEPROM.read(0x166);//Read Stored Length Gateway IP in address 0x166
  for (int i = 0; i < C_len; i++)
  {
    Static_IPgateway = Static_IPgateway + char(EEPROM.read(0x167 + i)); //Read one by one with starting address of 0x167
  }
  
  int D_len = EEPROM.read(0x177);//Read Stored Length DNS1 IP in address 0x177
  for (int i = 0; i < D_len; i++)
  {
    Static_IPdns = Static_IPdns + char(EEPROM.read(0x178 + i)); //Read one by one with starting address of 0x178
  }
  
  int E_len = EEPROM.read(0x188);//Read Stored Length DNS2 IP in address 0x188
  for (int i = 0; i < E_len; i++)
  {
    Static_IPdnssecondary = Static_IPdnssecondary + char(EEPROM.read(0x189 + i)); //Read one by one with starting address of 0x189
  }

  //-----------------------------------------------------------------------------------------------------
      Static_IPaddress.toCharArray(StaticIPaddress, A_len + 1);
      Static_IPsubnet.toCharArray(StaticIPsubnet, B_len + 1);
      Static_IPgateway.toCharArray(StaticIPgateway, C_len + 1);      
      Static_IPdns.toCharArray(StaticIPdns, D_len + 1);
      Static_IPdnssecondary.toCharArray(StaticIPdnssecondary, E_len + 1);
      byte byteIPAddress[4];
      byte byteIPgateway[4];
      byte byteIPsubnet[4];
      byte byteIPdns[4];
      byte byteIPdnssec[4];
      parseBytes(StaticIPaddress, '.', byteIPAddress, 4, 10);
      parseBytes(StaticIPgateway, '.', byteIPgateway, 4, 10);
      parseBytes(StaticIPsubnet, '.', byteIPsubnet, 4, 10);
      parseBytes(StaticIPdns, '.', byteIPdns, 4, 10);
      parseBytes(StaticIPdnssecondary, '.', byteIPdnssec, 4, 10);
      IPAddress local_IP(byteIPAddress[0], byteIPAddress[1], byteIPAddress[2], byteIPAddress[3]);
      IPAddress gateway(byteIPgateway[0], byteIPgateway[1], byteIPgateway[2], byteIPgateway[3]);
      IPAddress subnet(byteIPsubnet[0], byteIPsubnet[1], byteIPsubnet[2], byteIPsubnet[3]);
      IPAddress primaryDNS(byteIPdns[0], byteIPdns[1], byteIPdns[2], byteIPdns[3]);                //optional
      IPAddress secondaryDNS(byteIPdnssec[0], byteIPdnssec[1], byteIPdnssec[2], byteIPdnssec[3]);  //optional
      delay(2000); 
       
       if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS))
      {
        Serial.println("Static IP Failed To Configure");
        Serial.println("IP Setting is NOW Set To AUTOMATIC (DHCP) Configuration!");
        delay(2000);
      
      }
      else {
        Serial.println("Static IP Success To Configure");
        delay(2000);
        Serial.println(WiFi.localIP()); 
        Serial.println(WiFi.subnetMask()); 
        Serial.println(WiFi.gatewayIP()); 
               
      }
  }

  else{
    //------------------------------------------------------------------------chose ip set automaticly from DHCP Server----------------------------
    Serial.println("IP Setting is Set To AUTOMATIC (DHCP) Configuration!");
    }
}
//-------------------------------------------WIFI Scan Network SSID ------------------------
void scan_wifi() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(500);
  Serial.println("Scan start ... ");
  int nSSID = WiFi.scanNetworks();
  delay(500);
  Serial.print(nSSID);
  Serial.println(" network is found:");
  Serial.println();

  for (int i = 0; i < nSSID; i++) {
    Serial.print(i + 1);
    Serial.print(") ");
    Serial.println(WiFi.SSID(i));
    Serial.println();
    delay(100);
  }

  Serial.println("Select WiFi... ");
  String selectionSSID;
  int numSel = 0;
  unsigned long tNow = millis();
  unsigned long timecheck;

  while (!Serial.available() && millis() - tNow <= 5000) {
    timecheck = millis() - tNow;
  }

  while (Serial.available() > 0) {
    int bufer = Serial.read();
    Serial.flush();
    if (isDigit(bufer)) {
      selectionSSID += (char)bufer;
    }
    delay(100);

    if (bufer == 10) {
      delay(100);
      numSel = selectionSSID.toInt();
      Serial.println(numSel);
    }
  }

  if (timecheck < 4999) {
    if (numSel <= nSSID) {
      numSel = numSel - 1;
      ssidWifi = WiFi.SSID(numSel);
      //-----------------------------------------------------save WIFI ssid and password to eeprom From here-----------------------------
      int ssidlength = ssidWifi.length();
      for (int i = 0; i < ssidlength; i++) {
        EEPROM.write(0x32 + i, ssidWifi[i]); //Write start at address 0x32
      }
      EEPROM.write(0x10, ssidlength);//Write ssid String length in address 0x10
      EEPROM.commit();    //Store data to EEPROM

      Serial.println("Connect to SSID: ");
      Serial.println(ssidWifi);
      delay(3000);
      int passvalid = 0;
      int passlength = 0;

      while ( passvalid == 0 ) {
        Serial.println("PASSWORD: ");
        while (!Serial.available()) {}
        passwordWifi = Serial.readStringUntil('\n');
        passlength = passwordWifi.length();
        if (passlength > 32) {
          Serial.println("Password Too Long");
        }
        else {
          passvalid = 1;
        }
      }
      for (int i = 0; i < passlength; i++) {
        EEPROM.write(0x11 + i, passwordWifi[i]); //Write start at address 0x11
      }
      EEPROM.write(0x0F, passlength);//Write valid password length in address 0x0f
      EEPROM.commit(); //Store data to EEPROM

      //-----------------------------------------------------save WIFI ssid and password to eeprom until here-----------------------------
      wifi_staticIPset();     
      setServerParameter();
    }
    else {
      Serial.println("Selection Not Match");
    }
  }
  else {
    Serial.println("Timeout...");
  }
  delay(500);
}

//------------------------------------------------------------------------Connecting to WIFI---------------------------------------------------
void wifi_connect()
{
  //----------------------------------------------------Flush ssid and password Variable------
  ssidWifi = "";
  passwordWifi = "";
  //----------------------------------------------------always assign ssid and password from eeprom from Here------
  int ssidlength = EEPROM.read(0x10);
  for (int i = 0; i < ssidlength; i++)
  {
    ssidWifi = ssidWifi + char(EEPROM.read(0x32 + i)); //Read one by one with starting address of 0x32
  }
  int passlength = EEPROM.read(0x0F);
  for (int i = 0; i < passlength; i++)
  {
    passwordWifi = passwordWifi + char(EEPROM.read(0x11 + i)); //Read one by one with starting address of 0x11
  }
  //----------------------------------------------------always assign ssid and password from eeprom Until Here------

  // start connecting to a WiFi network

  Serial.print("Connecting to SSID: ");
  Serial.println(ssidWifi);
  WiFi.mode(WIFI_STA);
  WiFi.hostname("SmartDeskStation"); //-------------<< Change The Name Of ESP Device Name Show in Network Connected Device List
  WiFi.begin(ssidWifi, passwordWifi);

  tft.setTextColor(TFT_RED);
  tft.setCursor(0, 300);
  tft.println ("LOADING");
  tft.setCursor(0, 310);
  while (WiFi.status() != WL_CONNECTED) { //-----------------<< Always Check For WiFi Status Connection,
    int tNow = millis();
    int xr = 0;
    while ((WiFi.status() != WL_CONNECTED) && (millis() - tNow <= 15000)) { //----------------<< if under 10 Second already connect then exit
      delay(62);
      Serial.print(".");
      tft.drawLine(xr, 310, xr, 314, TFT_BLACK);
      xr++;
    }
    while ((WiFi.status() != WL_CONNECTED) && (millis() - tNow > 15000)) { //----------------<< if More than 10 Second still not connected then try to reconnect
      Serial.println("WiFi Failed To Connect");
      delay(500);
      WiFi.begin(ssidWifi, passwordWifi); // Try Reconnecting to WIFI
      delay(500);
      Serial.println("Reconnecting.");
      //tft.println("Reconnecting.");
      tNow = millis();
    }
  }
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(0, 0);
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  return;
}

void pirIndicatorOn() {
//  tft.fillCircle(230, 308, 6, TFT_BLUE);//-----------Portrait Position
//  tft.fillCircle(230, 308, 5, TFT_RED);//-----------Portrait Position
 
  tft.fillCircle(10, 190, 6, TFT_BLUE);//-----------Landscape Position
  tft.fillCircle(10, 190, 5, TFT_RED);//-----------Landscape Position
  signalPIR = 1;
  TsigPir = millis();
}

void sendHttpPost() {

  WiFiClient client;  //call object wificlient
  HTTPClient http;    //call object httpclient
  char postUsername[50];
  char postPassword[50];
  int UN_len = POST_USERNAME.length() + 1;
  int PA_len = POST_PASSWORD.length() + 1;
  POST_USERNAME.toCharArray(postUsername, UN_len);
  POST_PASSWORD.toCharArray(postPassword, PA_len);

//POST_DESKNAME = "DES-IT-7-0401";
//POST_BOOKDATE = "2021-07-10T00:00:00";

//POST_DESKNAME = "DES-FA-8-0001";
//POST_BOOKDATE = "2021-07-25T00:00:00";
  
  POST_BOOKDATE = getBookDate();
  POST_JSON_DATA = "{\"opt\":\"getDeskBooker\", \"deskName\" : \"" + POST_DESKNAME + "\", \"bookDate\" : \"" + POST_BOOKDATE + "\"}";
 // Serial.println(POST_JSON_DATA);
  http.begin(client, "http://" + JSONSERVER_IP + "/api/");
  http.addHeader("Content-Type", "application/json");
  http.setUserAgent  ("Nauchara-SmartDesk");
  http.setAuthorization(postUsername, postPassword);
  http.setTimeout(3000);     //------------------set wait 1sec(fastest), not wait too long or too short, default is around 5sec when http.getstring()
  Serial.print("[HTTP] POST...\n");
  httpCode = http.POST(POST_JSON_DATA);

  if (httpCode > 0) {//---------------------------------------------httpCode will be negative on error or value will be 200
    Serial.printf("[HTTP] RETURN POST... code: %d\n", httpCode);// --------HTTP header has been send and Server response header has been handled
    if (httpCode == HTTP_CODE_OK) {//------------------------------ file found at server
      const String payload = http.getString();
      jsonData = payload.substring(1, payload.length() - 1);
      StaticJsonDocument<320> doc;
      deserializeJson(doc, jsonData);
      jsName = doc["name"];
      jsEmail = doc["email"];
      jsDept = doc["departmentname"];
      jsMessage = doc["message"];
      greeting = jsName;
      greeting = "Hallo " + greeting + ",";
    }
  }
  else {
    Serial.printf("[HTTP] POST... failed, error: %s\n", http.errorToString(httpCode).c_str());
  }
  http.end();
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//--------------------------------------------------------------------------------------------------------------------VOID SETUP HERE----------------------------------------------------

void setup() {
  //--------------------------SET PINOUT-----------------------------------
  ///////////////////////SENSOR CONNECTION TO GPIO AND SET AS INTERRUPT PIN////////////////
  pinMode(sensor_pir_pin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(sensor_pir_pin), PIR_interrupt, RISING);
  pinMode(validator_pin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(validator_pin), Validator_interrupt, RISING);

  pinMode(power_ctl_pin, OUTPUT);
//  digitalWrite(power_ctl_pin, LOW);   // Active HIGH
  digitalWrite(power_ctl_pin, HIGH);   // Active LOW

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);   // Active HIGH

  //-----------------------------------------------------------------------
  flag_occupied = 0;
  T_Timer  = 0;
  TCP_Timer = 0;
  TR = CTR;
  tft_init();
  
  //----------------------------------------show booting logo screen---------------
  tft_bootlogo();

  //--------------------------SET SERIAL COMM-----------------------------------
  Serial.begin(9600);  // Set Serial Baudrate to 9600
  delay(500);

  //--------------------------SET MEMORY EEPROM-----------------------------------
  EEPROM.begin(512);  //Initialize EEPROM
   
  //-------------------------INITIAL WIFI CONNECTION------------------------------------
  delay(50);
  scan_wifi();        //Scan wifi network
  deviceIpSet();      //Set Device IP
  wifi_connect();     //Conneting to Preassigned Wifi or default will connect to last connection credential take from memoryNv
  timeClient.begin(); //NTPtime start
  delay(50);
//-------------------------INITIAL LCD Screen Orientation------------------------------------
 // tft.setRotation(0);     // Set display orientation Potrait
  tft.setRotation(1);     // Set display orientation Lanscape
  tft_mainscreen();
  
  readServerParameter();
  //tft.drawCentreString("SYSTEM READY", 120, 79, 2);//-------Portrait Position
  tft.drawCentreString("SYSTEM READY", 160, 80, 2);//-------Landscape Position
  delay(2500);       //delay for user reading status on LCD
  digitalWrite(TFT_BL, LOW);   // Active HIGH
}
//--------------------------------------------------------------------------------------------------------------------End Void SETUP HERE--------------------------------------------------
//void(* resetFunc) (void) = 0;
//resetFunc();
//--------------------------------------------------------------------------------------------------------------------VOID LOOP HERE--------------------------------------------------------------------------------------------------------------------------------------
void loop() {
  //--------------------------------------------------------------------------always check for wifi disconnection problem, to do reconnection.--------------------------------------------
  if (WiFi.status() != WL_CONNECTED) {
    tft_mainscreen();
    digitalWrite(TFT_BL, HIGH);
    Serial.println("WIFI Disconnected");
    //tft.drawCentreString("System Disconnected", 120, 79, 2);//------Portrait Position
    tft.drawCentreString("System Disconnected", 160, 90, 2);//------Landscape Position
    wifi_connect();
    delay(1000);
    digitalWrite(TFT_BL, LOW);
  }
  
  //------------------------------------------------------------------------------------------ELECTRICAL SWITCH CONTROL-------------------------------------------------------------------
  //----------------------------------------------------------------------------------------Handling interrupt HERE----------------------
  //-------------------------------------------------------------------------------------PIR interrupt---------------
  if (((millis() - TsigPir) > 1000) && (signalPIR == 1)) { //--------------signal PIR indicator Terminator
    signalPIR = 0;
    //tft.fillCircle(230, 308, 6, TFT_BLUE);//--------Portrait Position
    tft.fillCircle(10, 190, 6, TFT_BLUE);//--------Landscape Position
  }

  if ((fint_PIR == 1) && (flag_occupied == 0)) {
    delay(500);//-------essential delay for external input signal anti bouncing.
    tft_mainscreen();
    digitalWrite(TFT_BL, HIGH); //---turn on backlight
    Serial_ntpupdate();
    //-----------------------------------------------------------------TCP Communication---------------------------------------------------------------------
    sendHttpPost();
    if (httpCode > 0) {
      if (jsonData.length() < 5) {
//        tft.drawCentreString("Table Vacant", 120, 140, 4);//---------------------------Portrait Position
//        tft.drawCentreString("Please Book Your Table", 120, 160, 2);//-----------------Portrait Position
        tft.drawCentreString("Table Vacant", 160, 80, 4);//---------------------------Landscape Position
        tft.drawCentreString("Please Book Your Table", 160, 100, 2);//-----------------Landscape Position
      }
      else {
        String tftName = jsName;
        String tftEmail = jsEmail;
        String tftDept = jsDept;
        tftName    = "Name      : " + tftName;
        tftEmail   = "Email     : " + tftEmail;
        tftDept    = "Department: " + tftDept;
        //tft.drawCentreString("Table Booked", 120, 79, 4); //------Print WELCOME At the center alignment ("string",x,y,fontsize)//------Portrait Position
        tft.drawCentreString("Table Booked", 160, 80, 4); //------Print WELCOME At the center alignment ("string",x,y,fontsize)//------Landscape Position
        tft.setTextSize(1);
        tft.setCursor(30, 110);
        tft.print(tftName);
        tft.setCursor(30, 120);
        tft.print(tftEmail);
        tft.setCursor(30, 130);
        tft.print(tftDept);
      }
      Serial.print("This Table Is Booked By:");
      Serial.println(jsName);
      Serial.print("Email:");
      Serial.println(jsEmail);
      Serial.print("Department:");
      Serial.println(jsDept);
      Serial.println("");
      Serial.println(jsMessage);
      Serial.println("");
      Serial.println(">>");
      Serial.println(jsonData);
      Serial.print("Byte Receive:");
      Serial.println(jsonData.length() + 2);
      Serial.println(">>");
    }
    //---------------------------------------------------------------------------------------------------------------end of tcp communication----------

    delay(3000);//-----------------delay some few second to show status, give user a time to read//
    digitalWrite(TFT_BL, LOW); //----turn off the backlight
    fint_PIR = 0; //-----reset pir interrupt flag
  }

  if ((fint_PIR == 1) && (flag_occupied == 1) && (STR == 0)) { //if PIR trigger and table ocupied, respon need only refresh timer
    STR = 1; //<------------------state time reminder
    delay(500);//-------essential delay for external input signal anti bouncing.
    T_Timer = millis(); //--------reset Counter Time periode
    TR = CTR; //--------reset time remaining counter
    fint_PIR = 0; //-----reset pir interrupt flag
    tft_watchscreen();
    drawAnalogWatchFace();
    syncAnalogtime();
    pirIndicatorOn();
  }

  if ((fint_PIR == 1) && (flag_occupied == 1) && (STR == 1)) { //if PIR trigger and table ocupied  and not in state T reminder, respon need only refresh timer
    delay(25);//-------essential delay for external input signal anti bouncing.
    T_Timer = millis(); //--------reset Counter Time periode
    TR = CTR; //--------reset time remaining counter
    fint_PIR = 0; //-----reset pir interrupt flag
    syncAnalogtime();
    pirIndicatorOn();
  }

  //-----------------------------------------------------------------------------------validator interrupt--------------------
  if ((fint_VAL == 1) && (flag_occupied == 0)) {
   // delay(100);//-------essential delay for external input signal anti bouncing.
    flag_occupied = 1;
    T_Timer = millis(); //--------reset Counter Time periode
    TRSec = millis(); //--------reset Counter 1 second refference
    TR = CTR; //--------reset time remaining counter
    tft_mainscreen();
    digitalWrite(TFT_BL, HIGH); //---turn on backlight
//    digitalWrite(power_ctl_pin, HIGH);
    digitalWrite(power_ctl_pin, LOW);//-----------<<<Used to Relay turn on by validator and turn off by timer<<<-------//
    //------------------------------------------------------------------TCP Communication---------------------------------------------------------------------
    sendHttpPost();
    //---------------------------------------------------------------End of TCP communication-----------------------------------------------------------------

//    tft.drawCentreString("WELCOME", 120, 79, 4); //------Print WELCOME At the center alignment ("string",x,y,fontsize)Portrait Position
//    tft.drawString(greeting, 5, 130, 2);//----Portrait Position
//    tft.drawString("DESK ELECTRICAL READY TO USE.", 5, 150, 2);//----Portrait Position

    tft.drawCentreString("WELCOME", 160, 79, 4); //------Print WELCOME At the center alignment ("string",x,y,fontsize)Landscape Position
    tft.drawString(greeting, 60, 130, 2);//----Landscape Position
    tft.drawString("DESK ELECTRICAL READY TO USE.", 60, 150, 2);//----Landscape Position
    
    tft.setTextSize(1);
    delay(5000);//--------------------5sec(5000) show welcome screen for user read LCD
    fint_VAL = 0;  //-----reset flag interrupt
    tft_watchscreen();
    drawAnalogWatchFace();
    syncAnalogtime();
  }

  if ((fint_VAL == 1) && (flag_occupied == 1)) {
    delay(500);//-------essential delay for external input signal anti bouncing.
    tft_mainscreen();
//    digitalWrite(power_ctl_pin, LOW);
    digitalWrite(power_ctl_pin, HIGH);//-----------<<<Used to both turn on and off by validator<<<-------//
    flag_occupied = 0;
    //tft.drawCentreString("GOODBYE", 120, 130, 4); //------Print GOODBYE At the center alignment ("string",x,y,fontsize)Portrait
    tft.drawCentreString("GOODBYE", 160, 100, 4); //------Print GOODBYE At the center alignment ("string",x,y,fontsize)Landscape
    tft.setTextSize(1);
    delay(6000);
    digitalWrite(TFT_BL, LOW);
    fint_VAL = 0; //-----reset flag interrupt
  }
  //-------------------------------------------------------------//Waiting Timer//---------------------------------------
  if (flag_occupied == 1) {
    //-------------------------------------------------------------------------------------State User Using Table Electric without remind the time left
    if ((millis() - T_Timer) <= (T_Limit - T_Max_Rem)) {
      drawAnalogTime();
      STR = 1;
    }
    //-------------------------------------------------------------------------------------State Remind User About time Left Using Table Electric
    if ((millis() - T_Timer) > (T_Limit - T_Max_Rem)) {
      STR = 0;//<------------------state time reminder
      if (millis() - TRSec >= 1000) {
        tft_mainscreen();
        //tft.drawCentreString("TIME REMAINING:", 120, 150, 4); //------Print warning remining time Portrait Position
        tft.drawCentreString("TIME REMAINING:", 160, 110, 4); //------Print warning remining time Landscape Position
        
       //tft.setCursor(110, 180);//------Portrait Position
        tft.setCursor(150, 140);//------Landscape Position
        tft.setTextSize(3);
        tft.print(TR);
        tft.setTextSize(1);
        TRSec = millis();
        TR--;
      }
    }
    //-------------------------------------------------------------------------------------State Out of remaining time
    if (TR <= 0 ) {          //(millis() - T_Timer >= T_Limit){
      delay(1000);
      T_Timer = millis(); //--------reset Counter Time periode
      TR = CTR; //--------reset time remaining counter
      tft_mainscreen();
      //tft.drawCentreString("GOODBYE", 120, 130, 4); //------Print GOODBYE At the center alignment ("string",x,y,fontsize)Portrait Position
      tft.drawCentreString("GOODBYE", 160, 100, 4); //------Print GOODBYE At the center alignment ("string",x,y,fontsize)Landscape Position
      flag_occupied = 0;
      fint_VAL = 0; //-----reset flag interrupt
      delay(2000);
//      digitalWrite(power_ctl_pin, LOW);
      digitalWrite(power_ctl_pin, HIGH);//-----------<<<Used to both turn on and off by validator<<<-------//
      digitalWrite(TFT_BL, LOW);
    }
  }

  //----------------------------------------------------------------------------------------------------------END---------------------------------------------------------------------------------------------
}
