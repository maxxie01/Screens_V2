/*
   Homey Arduino library
   Usage example for use with ESP8266

   Most of the code in this file comes from the ethernet usage examples included with Arduino
   Also the code at https://www.arduino.cc/en/Tutorial/BlinkWithoutDelay has been included to
   show you how to read sensors and emit triggers without using the delay function.

*/

//Controling screens, curtains, sun screens
//based on the Wemos D1

//#include <FS.h>                 //this needs to be first, or it all crashes and burns... needs to create more customfields on WiFi
//#include <ArduinoJson.h>        //https://github.com/bblanchon/ArduinoJson
#include <ESP8266WebServer.h>   //create webserver for first WiFi initialisatie
#include <WiFiManager.h>        //create webserver for first WiFi initialisatie
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <Homey.h>
#include <EEPROM.h>

typedef enum { UP, DOWN, IDLE } state;

//WiFi settings => if Wifi settings empty then connect to AP 192.168.1.4
String HomeyDeviceName = "Curtain";  //name of the device, will by expand with ESP ID like <HomeyDeviceName>_<ESP ID>
WiFiManager wifiManager;

//Relais or motorshield
#define RELAIS_UP     D1 //up or open
#define RELAIS_DOWN   D2 //down or close
#define RELAIS_ON     HIGH //put high or low to the digital output
#define RELAIS_OFF    LOW  //put high or low to the digital output

//buttons for manual control, between digtal pin and GND
#define BUTTON_UP     D6 //up or open
#define BUTTON_DOWN   D7 //down or close
#define BUTTON_TIME  1000 //milliseconds hold button
unsigned long buttonTimeout = 0;
const unsigned long buttonInterval = 500;

//end stops
#define ENDSTOP_UP    D3
#define ENDSTOP_DOWN  D4

//Global variables for Homey loop
unsigned long previousMillis = 0;
const unsigned long interval = 1000; //Interval in milliseconds

//Global variables for program
#define MINIMUM_TIME 1000 //minimum time the motor will run
int upTimeMax = 0;     //maximum time the motor needs to go from bottom to top (milliseconds)
int downTimeMax = 0;   //maximum time the motor needs to go from top to bottom (milliseconds)
state statusDevice = IDLE;
unsigned long timeToGo = 0; //time to run for the motor
unsigned long timeOffset = 0; //time when action up or down is started
int dimHomey = 0;
int dimCurrent = 0;
boolean doHomeyUpdate = false; //if Homey dim capability must update


void setup() {
  //Enable serial port
  Serial.begin(57600);
  Serial.println("Setup start");

   Serial.println("Connect to wifi");
  //Connect to network
  String deviceName = HomeyDeviceName + "_" + String(ESP.getChipId()); //Generate device name based on ID
  Serial.print("Starting wifiManager: ");
  Serial.println(deviceName);
  wifiManager.autoConnect(deviceName.c_str(), ""); //Start wifiManager

  //config ports for buttons
  pinMode(BUTTON_UP, INPUT_PULLUP);
  pinMode(BUTTON_DOWN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_UP), buttonInterruptUp, FALLING);
  attachInterrupt(digitalPinToInterrupt(BUTTON_DOWN), buttonInterruptDown, FALLING);
  
  //config ports for relais
  pinMode(RELAIS_UP, OUTPUT);
  pinMode(RELAIS_DOWN, OUTPUT);
  digitalWrite(RELAIS_UP, RELAIS_OFF);
  digitalWrite(RELAIS_DOWN, RELAIS_OFF);

  //config ports for endstops
  pinMode(ENDSTOP_UP, INPUT_PULLUP);
  pinMode(ENDSTOP_DOWN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ENDSTOP_UP), EndstopInterruptUp, FALLING);
  attachInterrupt(digitalPinToInterrupt(ENDSTOP_DOWN), EndstopInterruptDown, FALLING);
    
  //Start Homey library
  Homey.begin(deviceName);
  Homey.setClass("windowcoverings");
  Homey.addCapability("windowcoverings_state", windowcoverings_state);    //set/get, enum, up/idle/down
  Homey.addCapability("dim", windowcoverings_dim);                        //set/get, decimal 0.00 - 1.00
  //Homey.addCapability("windowcoverings_set", windowcoverings_dim);                        //set/get, decimal 0.00 - 1.00

  Homey.addAction("SetMaxTimeUp", SetMaxTimeUp);   
  Homey.addAction("SetMaxTimeDown", SetMaxTimeDown);   

  //read up en down time from eeprom
  EEPROM.begin(4);
  upTimeMax =   max(int(word(EEPROM.read(0),EEPROM.read(1))), MINIMUM_TIME); //minimal 10 second
  downTimeMax = max(int(word(EEPROM.read(2),EEPROM.read(3))), MINIMUM_TIME); //minimal 10 second
  
  Serial.println("Setup completed");
}

  
void loop() {
  //Handle incoming connections
  Homey.loop();
  /* Note:
      The Homey.loop(); function needs to be called as often as possible.
      Failing to do so will cause connection problems and instability.
      Avoid using the delay function at all times. Instead please use the
      method explaind on the following page on the Arduino website:
      https://www.arduino.cc/en/Tutorial/BlinkWithoutDelay
  */

  //This is the 'blink without delay' code
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis > interval) {
    previousMillis = currentMillis;
    //(This code will be executed every <interval> milliseconds.)

    if(statusDevice != IDLE){
      if(statusDevice == DOWN) dimCurrent = myMap(millis() - timeOffset, 0, downTimeMax, 0, 100);
      if(statusDevice == UP) dimCurrent = myMap(millis() - timeOffset, 0, upTimeMax, 100, 0);
      if(timeToGo < millis()) setStatus(IDLE);
      doHomeyUpdate = true;
      printStatus();
    }

    if(doHomeyUpdate){
      double tmpDimCurrent = dimCurrent / 100.0;
      Homey.setCapabilityValue("dim",tmpDimCurrent); //must be from 0.00 to 1.00
      doHomeyUpdate = false;
    }
              
    //Emit a trigger to Homey
    //bool success = Homey.trigger("mytrigger", "Hello world");
    /* Note:
     *  The first argument to the emit function is the name of the trigger  
     *  this name has to match the name used in the flow editor
     *  
     *  The second argument to the emit function is the argument.
     *  An argument can be one of the following:
     *     - a string
     *     - an integer (int)
     *     - a floating point number (float or double),
     *     - a boolean (bool)
     *     - nothing (void)
     *  
     *  Make sure to select the right type of flow card to match the type
     *  of argument sent to Homey.
     *  For a string argument the "text" flowcard is used.
     *  For an integer or floating point number the "number" flowcard is used.
     *  For the boolean argument the "boolean" flowcard is used.
     *  And when no argument is supplied the flowcard without argument is used.
     *  
     */
  /* Note:
   *  
   *  The argument will always be received as a String.
   *  If you sent a number or boolean from homey then you can convert
   *  the value into the type you want as follows:
   *  
   *   - Integer number: "int value = Homey.value.toInt();"
   *   - Floating point number: "float value = Homey.value.toFloat();"
   *   - Boolean: "bool value = Homey.value.toInt();"
   *   - String: "String value = Homey.value;"
   *  
   * In case something goes wrong while executing your action
   * you can return an error to the Homey flow by calling
   * Homey.returnError("<message>");
   */
  }
}


void setStatus(state statusToSet){
  digitalWrite(RELAIS_UP, RELAIS_OFF); 
  digitalWrite(RELAIS_DOWN, RELAIS_OFF);
  Serial.println("set relais off");
  
  //if change from up <=> down => wait for 0.5 seconds before change the direction
  if(statusToSet != IDLE && statusDevice != IDLE && statusToSet != statusDevice){
    Serial.println("** time to wait untile switch engine **");
    delay(500);
  }
  
  switch(statusToSet){
    case UP:
      digitalWrite(RELAIS_UP, RELAIS_ON);
      //fix time on current position
      timeOffset = millis() - myMap(dimCurrent, 100, 0, 0, upTimeMax);
      Serial.println("set relais up");
      break;
    case DOWN:
      digitalWrite(RELAIS_DOWN, RELAIS_ON);
      //fix time on current position
      timeOffset = millis() - myMap(dimCurrent, 0, 100, 0, downTimeMax);
      Serial.println("set relais down");
      break;
  }
  Serial.println("switch statusDevice from [" + enumName(statusDevice) + "] to [" + enumName(statusToSet) + "]");
  statusDevice = statusToSet;
}


void setDim(int dimToSet){
  if(dimToSet > dimCurrent){
    //status => down
    setStatus(DOWN);
    timeToGo = timeOffset + myMap(dimToSet, 0, 100, 0, downTimeMax);
  }
  if(dimToSet < dimCurrent){
    //status => up
    setStatus(UP);
    timeToGo = timeOffset + myMap(dimToSet, 100, 0, 0, upTimeMax);
  }
  if(dimToSet == dimCurrent){
    setStatus(IDLE);
  }
  Serial.println("dimToSet[" + String(dimToSet) + "] timeOffset[" + String(timeOffset) + "] timeToGo[" + timeToGo + "]");
}


void printStatus(){
  Serial.print(" -");
  Serial.print(" dimCurrent[" + String(dimCurrent) + "]");
  Serial.print(" timeOffset[" + String(timeOffset) + "]");
  Serial.print(" millis[" + String(millis()) + "]");
  Serial.print(" timeToGo[" + String(timeToGo) + "]");
  Serial.print(" statusDevice[" + enumName(statusDevice) + "]");
  Serial.print(" downTimeMax[" + String(downTimeMax) + "]");
  Serial.print(" upTimeMax[" + String(upTimeMax) + "]");
  Serial.println("");
}


//homey action on state change
void windowcoverings_state(){
  String tmpHomey = Homey.value; //up, down, idle
  Serial.println("windowcoverings_state[" + tmpHomey + "]");
  if(tmpHomey == "up") setDim(0);
  if(tmpHomey == "down") setDim(100);
  if(tmpHomey == "idle") setDim(dimCurrent);
}

//homey action on dim change
void windowcoverings_dim(){
  int tmpHomey = Homey.value.toFloat() * 100;
  Serial.println("windowcoverings_dim[" + String(tmpHomey) + "]");
  setDim(tmpHomey);
  //Homey.setCapabilityValue("windowcoverings_state", statusDevice);
}

//homey action on SetMaxTimeUp
void SetMaxTimeUp(){
  int tmpHomey = Homey.value.toInt();
  upTimeMax = max(tmpHomey, MINIMUM_TIME); //minimal 1 second
  EEPROM.write(0,highByte(upTimeMax));
  EEPROM.write(1,lowByte(upTimeMax));
  EEPROM.commit();
}

//homey action on SetMaxTimeDown
void SetMaxTimeDown(){
  int tmpHomey = Homey.value.toInt();
  downTimeMax = max(tmpHomey, MINIMUM_TIME); //minimal 1 second
  EEPROM.write(2, highByte(downTimeMax));
  EEPROM.write(3, lowByte(downTimeMax));
  EEPROM.commit();
}


String enumName(state tmpStatusDevice){
  switch(tmpStatusDevice){
    case UP:
      return "UP";
      break;
    case DOWN:
      return "DOWN";
      break;
    case IDLE:
      return "IDLE";
      break;
  }
  return "unknown enum";
}

int myMap(int fromValue, int fromLow, int fromHigh, int toLow, int toHigh){
  int returnValue = map(fromValue, fromLow, fromHigh, toLow, toHigh);
  if(toLow < toHigh){
    if(returnValue < toLow) return toLow;
    if(returnValue > toHigh) return toHigh;
  }else{
    if(returnValue > toLow) return toLow;
    if(returnValue < toHigh) return toHigh;
  }
  return returnValue;
}

void buttonInterruptUp() {
  if(buttonTimeout > millis()) return;
  if(statusDevice == IDLE) setDim(0);
  if(statusDevice == DOWN) setDim(dimCurrent);
  buttonTimeout = millis() + buttonInterval;
}

void buttonInterruptDown() {
  if(buttonTimeout > millis()) return;
  if(statusDevice == IDLE) setDim(100);
  if(statusDevice == UP) setDim(dimCurrent);
  buttonTimeout = millis() + buttonInterval;
}

void EndstopInterruptUp(){
  if(buttonTimeout > millis()) return;
  if(statusDevice == DOWN) return;
  dimCurrent = 0;
  setDim(dimCurrent);
  doHomeyUpdate = true;
  buttonTimeout = millis() + buttonInterval; 
}
void EndstopInterruptDown(){
  if(buttonTimeout > millis()) return;
  if(statusDevice == UP) return;
  dimCurrent = 100;
  setDim(dimCurrent);
  doHomeyUpdate = true;
  buttonTimeout = millis() + buttonInterval;   
}
