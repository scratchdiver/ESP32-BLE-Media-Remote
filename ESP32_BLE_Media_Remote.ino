//#include <dummy.h>

#include "esp_sleep.h"
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include "BLE2902.h"
#include "BLEHIDDevice.h"
#include "HIDTypes.h"
#include "HIDKeyboardTypes.h"
// See the following for generating UUIDs:
// https://www.uuidgenerator.net/

const uint8_t keyboardHidDescriptor[] = {
  0x05, 0x0c,                    // USAGE_PAGE (Consumer Devices)
  0x09, 0x01,                    // USAGE (Consumer Control)
  0xa1, 0x01,                    // COLLECTION (Application)
  0x85, 0x01,                    //   REPORT_ID (1)
  0x19, 0x00,                    //   USAGE_MINIMUM (Unassigned)
  0x2a, 0x3c, 0x02,              //   USAGE_MAXIMUM (AC Format)
  0x15, 0x00,                    //   LOGICAL_MINIMUM (0)
  0x26, 0x3c, 0x02,              //   LOGICAL_MAXIMUM (572)
  0x95, 0x01,                    //   REPORT_COUNT (1)
  0x75, 0x10,                    //   REPORT_SIZE (16)
  0x81, 0x00,                    //   INPUT (Data,Var,Abs)
  0xc0,                          // END_COLLECTION
  0x05, 0x01,                    // USAGE_PAGE (Generic Desktop)
  0x09, 0x06,                    // USAGE (Keyboard)
  0xa1, 0x01,                    // COLLECTION (Application)
  0x85, 0x02,                    //   REPORT_ID (2)
  0x05, 0x07,                    //   USAGE_PAGE (Keyboard)
  0x19, 0xe0,                    //   USAGE_MINIMUM (Keyboard LeftControl)
  0x29, 0xe7,                    //   USAGE_MAXIMUM (Keyboard Right GUI)
  0x15, 0x00,                    //   LOGICAL_MINIMUM (0)
  0x25, 0x01,                    //   LOGICAL_MAXIMUM (1)
  0x75, 0x01,                    //   REPORT_SIZE (1)
  0x95, 0x08,                    //   REPORT_COUNT (8)
  0x81, 0x02,                    //   INPUT (Data,Var,Abs)
  0x95, 0x01,                    //   REPORT_COUNT (1)
  0x75, 0x08,                    //   REPORT_SIZE (8)
  0x25, 0x65,                    //   LOGICAL_MAXIMUM (101)
  0x19, 0x00,                    //   USAGE_MINIMUM (Reserved (no event indicated))
  0x29, 0x65,                    //   USAGE_MAXIMUM (Keyboard Application)
  0x81, 0x00,                    //   INPUT (Data,Ary,Abs)
  0xc0                           // END_COLLECTION
};


typedef struct
{
 //uint8_t  reportId;                                 // Report ID = 0x02 (2)
  // Collection: CA:ConsumerControl
  uint16_t ConsumerControl;                          // Value = 0 to 572
} inputConsumer_t;
  
static uint8_t idleRate;           /* in 4 ms units */

typedef struct
{
// uint8_t  reportId;                                 // Report ID = 0x02 (2)
  // Collection: CA:Keyboard
  uint8_t  KB_KeyboardKeyboardLeftControl  : 1;       // Usage 0x000700E0: Keyboard Left Control, Value = 0 to 1
  uint8_t  KB_KeyboardKeyboardLeftShift  : 1;         // Usage 0x000700E1: Keyboard Left Shift, Value = 0 to 1
  uint8_t  KB_KeyboardKeyboardLeftAlt    : 1;           // Usage 0x000700E2: Keyboard Left Alt, Value = 0 to 1
  uint8_t  KB_KeyboardKeyboardLeftGui    : 1;           // Usage 0x000700E3: Keyboard Left GUI, Value = 0 to 1
  uint8_t  KB_KeyboardKeyboardRightControl : 1;      // Usage 0x000700E4: Keyboard Right Control, Value = 0 to 1
  uint8_t  KB_KeyboardKeyboardRightShift   : 1;        // Usage 0x000700E5: Keyboard Right Shift, Value = 0 to 1
  uint8_t  KB_KeyboardKeyboardRightAlt   : 1;          // Usage 0x000700E6: Keyboard Right Alt, Value = 0 to 1
  uint8_t  KB_KeyboardKeyboardRightGui   : 1;          // Usage 0x000700E7: Keyboard Right GUI, Value = 0 to 1
  uint8_t  Key;                                 // Value = 0 to 101
} inputKeyboard_t;

static inputConsumer_t consumer_Report{};
static inputKeyboard_t keyboard_report{}; // sent to PC

BLEHIDDevice* hid;
BLECharacteristic* input;
BLECharacteristic* output;
BLECharacteristic* inputVolume;
BLECharacteristic* outputVolume;
bool connected = false;

class MyCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer){
    connected = true;
    BLE2902* desc = (BLE2902*)input->getDescriptorByUUID(BLEUUID((uint16_t)0x2902));
    desc->setNotifications(true);
    
    BLE2902* descv = (BLE2902*)inputVolume->getDescriptorByUUID(BLEUUID((uint16_t)0x2902));
    descv->setNotifications(true);
  }

  void onDisconnect(BLEServer* pServer){
    connected = false;
    BLE2902* desc = (BLE2902*)input->getDescriptorByUUID(BLEUUID((uint16_t)0x2902));
    desc->setNotifications(false);
    
    BLE2902* descv = (BLE2902*)inputVolume->getDescriptorByUUID(BLEUUID((uint16_t)0x2902));
    descv->setNotifications(false);
  }
};

const int ledPin = 2;

const int pinVolUp = 4;
const int pinVolDown = 15;
const int pinMute = 33;

unsigned long sleepTime;

bool wake_vol_up = false;
bool wake_vol_down = false;
bool wake_mute = false;

bool wake_delay_send = false;

void taskServer(void*){
  BLEDevice::init("Media Remote");
  
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyCallbacks());

  hid = new BLEHIDDevice(pServer);
  inputVolume = hid->inputReport(1); // <-- input REPORTID from report map
  outputVolume = hid->outputReport(1); // <-- output REPORTID from report map

  
  input = hid->inputReport(2); // <-- input REPORTID from report map
  output = hid->outputReport(2); // <-- output REPORTID from report map

  std::string name = "RosemontSoftWorks";
  hid->manufacturer()->setValue(name);

  hid->pnp(0x02, 0xe502, 0xa111, 0x0210);
  hid->hidInfo(0x00,0x02);

  hid->reportMap((uint8_t*)keyboardHidDescriptor, sizeof(keyboardHidDescriptor));
  hid->startServices();


  BLESecurity *pSecurity = new BLESecurity();
//  pSecurity->setKeySize();
  pSecurity->setAuthenticationMode(ESP_LE_AUTH_BOND);


  BLEAdvertising *pAdvertising = pServer->getAdvertising();
  pAdvertising->setAppearance(HID_KEYBOARD);
  pAdvertising->addServiceUUID(hid->hidService()->getUUID());
  pAdvertising->start();
  hid->setBatteryLevel(7);

  delay(portMAX_DELAY);
}

void sendVolumeChange(int controlValue) {
  if(connected){
    if (wake_delay_send) {
      wake_delay_send = false;
      delay(250);
    }
    inputConsumer_t b{};
//   b.reportId = 0x01;

    b.ConsumerControl = controlValue;
    inputVolume->setValue((uint8_t*)&b,sizeof(b));
    inputVolume->notify();   

    inputVolume->setValue((uint8_t*)&consumer_Report,sizeof(consumer_Report));
    inputVolume->notify();  
  }
}

void print_wakeup_reason(esp_sleep_wakeup_cause_t wakeup_reason){

  switch(wakeup_reason)
  {
    case ESP_SLEEP_WAKEUP_EXT0 : Serial.println("Wakeup caused by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1 : Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER : Serial.println("Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : Serial.println("Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP : Serial.println("Wakeup caused by ULP program"); break;
    default : Serial.printf("Wakeup was not caused by deep sleep: %d\n",wakeup_reason); break;
  }
}

void setup() {
  Serial.begin(115200);
  
  pinMode(ledPin, OUTPUT);
  pinMode(pinVolUp, INPUT);
  pinMode(pinVolDown, INPUT);
  pinMode(pinMute, INPUT);

  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT1) {
    // determine which button was pushed to cause wake up
    uint64_t wake_pins = esp_sleep_get_ext1_wakeup_status();
    wake_vol_up = (wake_pins & GPIO_SEL_4) != 0;
    wake_vol_down = (wake_pins & GPIO_SEL_15) != 0;
    wake_mute = (wake_pins & GPIO_SEL_33) != 0;
    wake_delay_send = true;
  }
  print_wakeup_reason(wakeup_reason);

  Serial.println("Starting BLE Task");
  xTaskCreate(taskServer, "server", 20000, NULL, 5, NULL);

  sleepTime = millis() + 5000;
  esp_sleep_enable_ext1_wakeup(GPIO_SEL_33 | GPIO_SEL_4 | GPIO_SEL_15 ,ESP_EXT1_WAKEUP_ANY_HIGH); 
}

void loop() {
  static bool volDirUp = true;
  if (connected && (digitalRead(pinVolUp) || wake_vol_up)) {
    wake_vol_up = false;
    digitalWrite(ledPin, HIGH);
    sendVolumeChange(0xE9);
    delay(250);
    digitalWrite(ledPin, LOW);
    sleepTime = millis() + 5000;
  }
  if (connected && (digitalRead(pinVolDown) || wake_vol_down)) {
    wake_vol_down = false;
    digitalWrite(ledPin, HIGH);
    sendVolumeChange(0xEA);
    delay(250);
    digitalWrite(ledPin, LOW);
    sleepTime = millis() + 5000;
  }
  if (connected && (digitalRead(pinMute) || wake_mute)) {
    wake_mute = false;
    digitalWrite(ledPin, HIGH);
    sendVolumeChange(0xE2);
    delay(1000);
    digitalWrite(ledPin, LOW);
    sleepTime = millis() + 5000;
  }

  if (millis() >= sleepTime) {
    Serial.println("Entering deep sleep");
    delay(100);
    esp_deep_sleep_start();
  }
}
