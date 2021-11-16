/* ESP32 based button

To do
* Enable click button during charge
* Switch smartconfig to WifiManager

Notes
* Charging will switch flipflop on in addition to making CHRG_PIN low
* Apparently some time (250ms) is needed between analogRead for battery measurement
* Battery measurement isn't possible while power +5v from FTDI is connected

LED flashes
* 2 red flashes - failed to send GET request
* 3 red flashes - failed to connect to wifi
* 4 red flashes - smartconfig failed 
* Blue - smartconfig waiting for credentials
* Light green - connecting to wifi and sending request

*/

#include <Arduino.h> 
#include <WiFi.h> /// FOR ESP32 
#include <HTTPClient.h> /// FOR ESP32 HTTP FOTA UPDATE ///
#include <HTTPUpdate.h> /// FOR ESP32 HTTP FOTA UPDATE ///
#include <WiFiClient.h> /// FOR ESP32 HTTP FOTA UPDATE ///
WiFiClient client;  /// FOR ESP32 HTTP FOTA UPDATE ///
//#include "driver/rtc_io.h" // support for internal pull-up on RTC pin

#include <EEPROM.h>
#define EEPROM_SIZE 512 //can be between 4 and 4096 bytes
#define DEVICE_NAME_EEPROM_ADDRESS  0
#define PRESS_COUNTER_EEPROM_ADDRESS  50

#define DISABLE_FLIPFLOP_PIN  22
#define VBAT_PIN  13
#define VUSB_PIN  25
#define CHRG_PIN  15 // LOW when battery is charging

#define LONG_PRESS_MS 2000
#define VERY_LONG_PRESS_MS 10000
#define SMART_CONFIG_TIMEOUT_MS 120000

#include <FastLED.h>
#define LED_DATA_PIN    32
#define LED_POWER_PIN    18
#define LED_TYPE    WS2811
#define COLOR_ORDER GRB
#define NUM_LEDS    12
#define LED_CONNECTING_AND_SENDING_GREEN  10

String requestURL = "http://your-server.com/buttonClicked";

CRGB leds[NUM_LEDS];

void smartConfig();
void printWifiStatus();

int updateAttempts = 0;

const String BTN_ID = "btn1";
const bool setNameEEPROM = true;

uint8_t VBAT;
uint8_t clickCounter;
String clickCounter_str;

void setup() {
  Serial.begin(115200);

  EEPROM.begin(EEPROM_SIZE);

  pinMode(LED_DATA_PIN, OUTPUT);
  pinMode(LED_POWER_PIN, OUTPUT);
  pinMode(DISABLE_FLIPFLOP_PIN, OUTPUT);
  
  digitalWrite(LED_POWER_PIN, LOW);

  FastLED.addLeds<LED_TYPE, LED_DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);

  // drainBattery(); // for testing purposes
  
  Serial.println("Button pressed");

  if (setNameEEPROM)
    setEEPROMvalue(DEVICE_NAME_EEPROM_ADDRESS,BTN_ID);
  
  Serial.print("Device ID: ");
  Serial.println(getEEPROMvalue(DEVICE_NAME_EEPROM_ADDRESS));

  Serial.print("VUSB: ");
  Serial.println(analogRead(VUSB_PIN));

  bool chargingState= digitalRead(CHRG_PIN);

  Serial.print("USB charging (1 = not charging; 0 = charging): ");
  Serial.println(chargingState);

  /******* DEVICE CHARGING ********/

  if (chargingState == 0) {

    Serial.println("Charging!");

    int d= 50;
    int f = 255;
    int p = 10;
    int startUp = 0;
    int startDown = 11;

    digitalWrite(LED_POWER_PIN, HIGH);
    delay(5);

    while (digitalRead(CHRG_PIN) == 0) {

      Serial.print("VBAT: ");
      VBAT = readBattery();
      Serial.print(VBAT);
      int batt_level_leds = map(VBAT,0,100,0,NUM_LEDS/2)+1;
      Serial.print("% Mapped to # LEDs: ");
      Serial.println(batt_level_leds);
      
      //Charging LED effect 
      
      for (int i=0 ; i<NUM_LEDS/2; i++) {  
        FastLED.clear();
        fill_solid(leds,batt_level_leds,CRGB(0,p,p));
        fill_solid(&(leds[NUM_LEDS - batt_level_leds]) ,batt_level_leds,CRGB(0,p,p));
        leds[startUp+i] = leds[startDown-i] = CRGB(0,f,f);
        FastLED.show();      
        delay(d);
      
      }

      FastLED.clear();
      fill_solid(leds,batt_level_leds,CRGB(0,p,p));
      fill_solid(&(leds[NUM_LEDS - batt_level_leds]) ,batt_level_leds,CRGB(0,p,p));
      FastLED.show();      

      delay(750);
        
    }
    
      // Turn ESP off
    Serial.print("Turning off");
    digitalWrite(DISABLE_FLIPFLOP_PIN  , HIGH);
    
  }
  
  /******* END DEVICE CHARGING ********/

  /******* LED FLASH ON ********/

  //increase press counter
  clickCounter = getEEPROMvalue(PRESS_COUNTER_EEPROM_ADDRESS).toInt();
  clickCounter++;
  
  if (clickCounter >= 32787) {
    clickCounter = 10000;
  }
  
  clickCounter_str = String(clickCounter);
  setEEPROMvalue(PRESS_COUNTER_EEPROM_ADDRESS,clickCounter_str);
  Serial.print("Press number "); 
  Serial.println(clickCounter_str);

  //LED effect parameters
  int d= 20;
  int f = 255;
  int p = LED_CONNECTING_AND_SENDING_GREEN;
  int startUp = 6;
  int startDown = 5;

  digitalWrite(LED_POWER_PIN, HIGH);
  delay(5);

  FastLED.clear();

  leds[startUp] = leds[startDown] = CRGB(0,f,0);    
  FastLED.show();      
  delay(d);
  
  for (int i=0 ; i<5; i++) {

    leds[startUp+i] = leds[startDown-i] = CRGB(0,p,0);
    leds[startUp+i+1] = leds[startDown-i-1] = CRGB(0,f,0);  
    FastLED.show();      
    delay(d);
  
  }

  leds[0] = leds[11] = CRGB(0,p,0);    
  FastLED.show();      

  /******* CONNECT AND SEND DATA *******/

  Serial.print("VBAT: ");
  VBAT = readBattery();
  Serial.print(VBAT);
  Serial.println("%");
      
  if (connectAndSend()) {
    // Request made successfully (200)

    /******* FLASH LED SUCCESS OFF *******/
  
    p=0;
  
    leds[startUp] = leds[startDown] = CRGB(0,f,0);    
    FastLED.show();      
    delay(d);
    
    for (int i=0 ; i<5; i++) {
  
      leds[startUp+i] = leds[startDown-i] = CRGB(0,p,0);
      leds[startUp+i+1] = leds[startDown-i-1] = CRGB(0,f,0);  
      FastLED.show();      
      delay(d);
    
    }
  
    leds[0] = leds[11] = CRGB(0,p,0);    
    FastLED.show();      
  
  }
  else
  {
    // Failed to make request
    /******* FLASH LED FAIL OFF *******/
 
    for (int i=0 ; i<2 ; i++) {
      fill_solid( leds, NUM_LEDS, CRGB::Red);
      FastLED.show();
      delay(200);
      FastLED.clear();
      FastLED.show();
      delay(100);        
    }
      
  }

  /******* TURN ESP OFF *******/
  Serial.print("Turning off");
  turnESPoff();
  
}

void loop() {
}


int connectAndSend(){ 

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Already connected to wifi, sending request");
    return sendRequest(getEEPROMvalue(DEVICE_NAME_EEPROM_ADDRESS),String(VBAT));
  }
  else 
  {
    // not connected to wifi - try to connect 
    Serial.println("Not connected to Wifi, trying to connect");

    //fake a successful connection
    delay(500);
    return true;

    if (!wifiConnect()) // if failed to connect
    {
      // run smartConfig
            
      smartConfig();

      // then try to connect again
      if (!wifiConnect())
        {
          //  if still fail to connect, indicate so and switch off
          Serial.println("Unable to connect to wifi. Giving up");
          for (int i=0 ; i<3 ; i++) {
            fill_solid( leds, NUM_LEDS, CRGB::Red);
            FastLED.show();
            delay(100);
            FastLED.clear();
            FastLED.show();
            delay(50);        
          }

          turnESPoff();
          return 0;

        }
    } 
    else
    {  
      //Wifi connection successful  
      Serial.println("Established wifi connection, sending request");  
      return sendRequest(getEEPROMvalue(DEVICE_NAME_EEPROM_ADDRESS),String(VBAT));
    }
    
  }

}

void turnESPoff() {

  digitalWrite(LED_POWER_PIN, LOW);        
  digitalWrite(DISABLE_FLIPFLOP_PIN  , HIGH);

}
  
bool wifiConnect()
{

  // smart config
  WiFi.begin();

  // or use hardcoded wifi credentials instead of smart config
//  WiFi.begin("SSID", "PASSWORD");
  
  Serial.print("WiFi Connecting.");
  for (int i = 0; i < 200; i++)
  {
    if (WiFi.status() == WL_CONNECTED)
    {
      printWifiStatus();
      return true;
    }
    else
    {
      Serial.print(".");
      //Serial.println(wstatus);
      delay(100);
    }
  }
  Serial.println("\nWifi Connect Failed!" );
  return false;

}

void smartConfig()
{
  digitalWrite(LED_POWER_PIN, HIGH);
  delay(5);
  fill_solid( leds, NUM_LEDS, CRGB(0,0,100));
  FastLED.show();

  WiFi.mode(WIFI_AP_STA);
  WiFi.beginSmartConfig();
  Serial.println("Start SmartConfig.");
  unsigned long startTime = millis();
  /* Wait for SmartConfig packet from mobile */
  Serial.println("Waiting for SmartConfig. Launch Mobile App (ex: ESP-TOUCH ) to progress SmartConfig.");
  while (!WiFi.smartConfigDone() ) {
    delay(50);
    Serial.print(".");
    if (millis() - startTime > SMART_CONFIG_TIMEOUT_MS) {
      // timeout connection, reset ESP      
      for (int i=0 ; i<4 ; i++) {
        fill_solid( leds, NUM_LEDS, CRGB::Red);
        FastLED.show();
        delay(100);
        FastLED.clear();
        FastLED.show();
        delay(50);        
      }

      // Turn circuit off
      Serial.println("Smartconfig timed out. Turning off");
      turnESPoff();

    }
  }
  
  fill_solid( leds, NUM_LEDS, CRGB(0,LED_CONNECTING_AND_SENDING_GREEN,0));
  FastLED.show();
 
  Serial.println("SmartConfig done.");
  
  WiFi.setAutoConnect(true);

}


int sendRequest(String buttonId, String VBAT_STR) {

  // Assuming we're connected
  
  Serial.println("Sending request (" + buttonId + ", "+VBAT_STR+")");

  HTTPClient http;

  String httpUrl = requestURL + "?id=" + buttonId + "&vbat=" + VBAT_STR + "&clickId=" + clickCounter_str;
  Serial.println(httpUrl);
  
  http.begin(httpUrl);
  int httpResponseCode = http.GET();  //Make the request

  Serial.print("GET response code: ");
  Serial.println(httpResponseCode);
  Serial.println(http.getString()); 
  http.end(); //Free the resources

  if (httpResponseCode >= 200) { 
    Serial.println("Update successful");
    updateAttempts=0;    
    return 1;

  }
  else {

    Serial.println("Update unsuccessful");
    updateAttempts++;

    if (updateAttempts > 3) {
      updateAttempts=0;
      Serial.println("Giving up");      
      return 0;
    }

    // try GET request again
    return sendRequest(buttonId,String(VBAT));
    
  }
}


void setEEPROMvalue(int eeprom_address,String data)
{
  int _size = data.length();
  int i;
  for(i=0;i<_size;i++)
  {
    EEPROM.write(eeprom_address+i,data[i]);
  }
  EEPROM.write(eeprom_address+_size,'\0');   //Add termination null character for String Data
  if (EEPROM.commit()) {
    Serial.print(data);
    Serial.print("written to EEPROM address ");
    Serial.println(eeprom_address);
  }
  else
    Serial.println("Failed to set device name in EEPROM");

}

String getEEPROMvalue(int eeprom_address)
{
  int i;
  char data[100]; //Max 100 Bytes
  int len=0;
  unsigned char k;
  k=EEPROM.read(eeprom_address);
  
  while(k != '\0' && len<EEPROM_SIZE-eeprom_address && k!=255)   //Read until null character
  {    
    k=EEPROM.read(eeprom_address+len);
    data[len]=k;
    len++;
  }
  data[len]='\0';
  return String(data);
}

void updateFirmware(String fw_update_url){
  
  Serial.print("***** Firmware update available: ");
  Serial.println(fw_update_url);

  WiFiClient client;
  Serial.print("***** Attempting to update firmware");
  
  t_httpUpdate_return ret = httpUpdate.update(client,fw_update_url);
  switch (ret) {
    case HTTP_UPDATE_FAILED:
      Serial.printf("***** HTTP_UPDATE_FAILD Error (%d): %s\n", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
      break;

    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("***** HTTP_UPDATE_NO_UPDATES");
      break;

    case HTTP_UPDATE_OK:
      Serial.println("***** Firmware updated successfully");
      break;
  }

}

void printWifiStatus() {
  
      Serial.println("\nWiFi Connected.");
      Serial.printf("SSID: %s\n", WiFi.SSID().c_str());
      Serial.printf("Password: %s\n", WiFi.psk().c_str());
      Serial.print("IP Address: ");
      Serial.println(WiFi.localIP());
}


uint8_t readBattery(){ 
  // this is for 100k+100k voltage divieder. Needs to be revised to 1M resistors!
  // 
  uint8_t percentage = 100;
  Serial.print("Dumping first read: ");
  Serial.println(analogRead(VBAT_PIN));
  delay(250);
  float voltage = analogRead(VBAT_PIN) / 4096.0 * 7.23;      // LOLIN D32 (no voltage divider need already fitted to board.or NODEMCU ESP32 with 100K+100K voltage divider
  Serial.println("Voltage = " + String(voltage));
  percentage = 2808.3808 * pow(voltage, 4) - 43560.9157 * pow(voltage, 3) + 252848.5888 * pow(voltage, 2) - 650767.4615 * voltage + 626532.5703;
  if (voltage > 4.19) percentage = 100;
  else if (voltage <= 3.50) percentage = 0;
  return percentage;
}


void drainBattery() {
  while (1) { //drain the battery
    
    digitalWrite(LED_POWER_PIN, HIGH);
    Serial.print("VBAT: ");
    int VBATx = readBattery();
    Serial.print(VBATx);
    int batt_level_ledsx = map(VBATx,0,100,0,NUM_LEDS/2)+1;
    Serial.print("% Mapped to # LEDs: ");
    Serial.println(batt_level_ledsx);
    Serial.print("AND: ");
    Serial.println(    NUM_LEDS - batt_level_ledsx);
    FastLED.clear();     
    fill_solid(leds,batt_level_ledsx,CRGB(255,255,255));
    fill_solid(&(leds[NUM_LEDS - batt_level_ledsx]) ,batt_level_ledsx,CRGB(255,255,255));
    FastLED.show();      
  }

}
