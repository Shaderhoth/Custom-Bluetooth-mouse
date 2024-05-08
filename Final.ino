#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLEHIDDevice.h>
#include <HIDTypes.h>
#include <CirquePinnacle.h>
#include <set>

#define ID_KEYBOARD 2
#define ID_MOUSE 1
#define MODIFIER_LEFT_CTRL 0x01
#define MODIFIER_LEFT_SHIFT 0x02
#define MODIFIER_LEFT_ALT 0x04


class BLEHIDDeviceHandler;
class MyServerCallbacks;


class MyServerCallbacks : public BLEServerCallbacks {
public:
    void onConnect(BLEServer* pServer) override;
    void onDisconnect(BLEServer* pServer) override;
};



PinnacleTouchI2C trackpad(17);
RelativeReport data;

std::set<int> pressedKeys;
int8_t dx = 0;
int8_t dy = 0;
int8_t ds = 0;
const int numKeys = 8;
const int keyPins[numKeys] = { 33, 26, 14, 13, 32, 25, 27, 12 };
bool lastKeyStates[numKeys] = { false };
uint8_t currentModifiers = 0;
const uint8_t keyToHidCode[numKeys] = { 0x4B, 0x50, 0x2C, 0x29, 0x00, 0x00, 0x00, 0x4C };


unsigned long lastDebounceTime[numKeys] = { 0 };
unsigned long debounceDelay = 10;

class BLEHIDDeviceHandler {
public:
  BLEHIDDeviceHandler()
    : hidDevice(nullptr), inputMouse(nullptr), inputKeyboard(nullptr) {}

  void setupDevice(const char* deviceName) {
    BLEDevice::init(deviceName);
    BLEServer* pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());


    for (int i = 0; i < numKeys; i++) {
      pinMode(keyPins[i], INPUT_PULLUP);
    }
    // Initialize the HID device as part of the setup
    this->setupHIDDevice(pServer);
    
    // Start advertising
    startAdvertising(pServer);

  }



  void setupHIDDevice(BLEServer* pServer) {
    hidDevice = new BLEHIDDevice(pServer);
    inputMouse = hidDevice->inputReport(ID_MOUSE);        // Mouse report
    inputKeyboard = hidDevice->inputReport(ID_KEYBOARD);  // Keyboard report
    configureHID();
  }


  void updateFromTrackpad() {
    if (trackpad.available()) {

      uint8_t prevButtonStates = data.buttons;

      trackpad.read(&data);  // get new data

      // edge detection for binary button data
      uint8_t buttonsChanged = prevButtonStates ^ data.buttons;
      if (buttonsChanged) {
        uint8_t toggledOff = buttonsChanged ^ (data.buttons & buttonsChanged);
        uint8_t toggledOn = buttonsChanged ^ toggledOff;
        if (toggledOn) {
          Serial.println(F("Mouse On"));
        }
        if (toggledOff) {
          Serial.println(F("Mouse Off"));
        }
      }

      if (data.x || data.y || data.scroll) {
        updateMouseReport(data.x, data.y, data.scroll);
      }
    } 
  }

  void loop() {
    unsigned long currentTime = millis();
    for (int i = 0; i < numKeys; i++) {
      bool currentState = digitalRead(keyPins[i]) == LOW;
      if ((currentTime - lastDebounceTime[i]) > debounceDelay) {
        if (currentState != lastKeyStates[i]) {
          lastDebounceTime[i] = currentTime;

          lastKeyStates[i] = currentState;
          Serial.print("Key ");
          if (currentState) {
            Serial.print("pressed: ");
            Serial.println(i);

            handleKeyPress(i);
          } else {
            Serial.print("released: ");
            Serial.println(i);
            handleKeyRelease(i);
          }
        }
      }
    }
  }

private:
  BLEHIDDevice* hidDevice;
  BLECharacteristic* inputMouse;
  BLECharacteristic* inputKeyboard;


  void configureHID() {
    // HID Device Configuration
    hidDevice->manufacturer()->setValue("David");
    hidDevice->pnp(0x02, 0xe502, 0xa111, 0x0210);
    hidDevice->hidInfo(0x00, 0x02);
    const uint8_t hidReportDescriptor[] = {
      // Mouse Report Descriptor
      0x05, 0x01,  // USAGE_PAGE (Generic Desktop)
      0x09, 0x02,  // USAGE (Mouse)
      0xA1, 0x01,  // COLLECTION (Application)
      0x85, 0x01,  // REPORT_ID (1 for Mouse)
      
      // Reserved byte
      0x95, 0x01,  // REPORT_COUNT (1)
      0x75, 0x08,  // REPORT_SIZE (8)
      0x81, 0x01,  // INPUT (Cnst,Ary,Abs)
      0x09, 0x01,  // USAGE (Pointer)
      0xA1, 0x00,  // COLLECTION (Physical)
      // Mouse buttons
      0x05, 0x09,  // USAGE_PAGE (Button)
      0x19, 0x01,  // USAGE_MINIMUM (Button 1)
      0x29, 0x03,  // USAGE_MAXIMUM (Button 3)
      0x15, 0x00,  // LOGICAL_MINIMUM (0)
      0x25, 0x01,  // LOGICAL_MAXIMUM (1)
      0x95, 0x03,  // REPORT_COUNT (3)
      0x75, 0x01,  // REPORT_SIZE (1)
      0x81, 0x02,  // INPUT (Data,Var,Abs)
      // Padding for the buttons (5 bits)
      0x95, 0x01,  // REPORT_COUNT (1)
      0x75, 0x05,  // REPORT_SIZE (5)
      0x81, 0x03,  // INPUT (Cnst,Var,Abs)
      // X and Y axis
      0x05, 0x01,  // USAGE_PAGE (Generic Desktop)
      0x09, 0x30,  // USAGE (X)
      0x09, 0x31,  // USAGE (Y)
      0x15, 0x81,  // LOGICAL_MINIMUM (-127)
      0x25, 0x7F,  // LOGICAL_MAXIMUM (127)
      0x75, 0x08,  // REPORT_SIZE (8)
      0x95, 0x02,  // REPORT_COUNT (2)
      0x81, 0x06,  // INPUT (Data,Var,Rel)
      // Wheel
      0x09, 0x38,  // USAGE (Wheel)
      0x15, 0x81,  // LOGICAL_MINIMUM (-127)
      0x25, 0x7F,  // LOGICAL_MAXIMUM (127)
      0x75, 0x08,  // REPORT_SIZE (8)
      0x95, 0x01,  // REPORT_COUNT (1)
      0x81, 0x06,  // INPUT (Data,Var,Rel)
      0xC0,        // END_COLLECTION
      0xC0,        // END_COLLECTION

      // Keyboard Report Descriptor
      0x05, 0x01,  // USAGE_PAGE (Generic Desktop)
      0x09, 0x06,  // USAGE (Keyboard)
      0xA1, 0x01,  // COLLECTION (Application)
      0x85, 0x02,  // REPORT_ID (2 for Keyboard)
      // Reserved byte
      0x95, 0x01,  // REPORT_COUNT (1)
      0x75, 0x08,  // REPORT_SIZE (8)
      0x81, 0x01,  // INPUT (Cnst,Ary,Abs)
      // Modifier keys
      0x05, 0x07,  // USAGE_PAGE (Keyboard/Keypad)
      0x19, 0xE0,  // USAGE_MINIMUM (Keyboard LeftControl)
      0x29, 0xE7,  // USAGE_MAXIMUM (Keyboard Right GUI)
      0x15, 0x00,  // LOGICAL_MINIMUM (0)
      0x25, 0x01,  // LOGICAL_MAXIMUM (1)
      0x75, 0x01,  // REPORT_SIZE (1)
      0x95, 0x08,  // REPORT_COUNT (8)
      0x81, 0x02,  // INPUT (Data,Var,Abs)
      // Reserved byte
      0x95, 0x01,  // REPORT_COUNT (1)
      0x75, 0x08,  // REPORT_SIZE (8)
      0x81, 0x01,  // INPUT (Cnst,Ary,Abs)
      // LED output report
      0x95, 0x05,  // REPORT_COUNT (5)
      0x75, 0x01,  // REPORT_SIZE (1)
      0x05, 0x08,  // USAGE_PAGE (LEDs)
      0x19, 0x01,  // USAGE_MINIMUM (Num Lock)
      0x29, 0x05,  // USAGE_MAXIMUM (Kana)
      0x91, 0x02,  // OUTPUT (Data,Var,Abs)
      // LED padding
      0x95, 0x01,  // REPORT_COUNT (1)
      0x75, 0x03,  // REPORT_SIZE (3)
      0x91, 0x03,  // OUTPUT (Cnst,Var,Abs)
      // Keycodes
      0x95, 0x06,  // REPORT_COUNT (6)
      0x75, 0x08,  // REPORT_SIZE (8)
      0x15, 0x00,  // LOGICAL_MINIMUM (0)
      0x25, 0x65,  // LOGICAL_MAXIMUM (101)
      0x05, 0x07,  // USAGE_PAGE (Keyboard/Keypad)
      0x19, 0x00,  // USAGE_MINIMUM (Reserved (no event indicated))
      0x29, 0x65,  // USAGE_MAXIMUM (Keyboard Application)
      0x81, 0x00,  // INPUT (Data,Ary,Abs)
      0xC0         // END_COLLECTION
    };


    hidDevice->reportMap(const_cast<uint8_t*>(hidReportDescriptor), sizeof(hidReportDescriptor));
    hidDevice->startServices();
    hidDevice->setBatteryLevel(100);
  }

  void startAdvertising(BLEServer* pServer) {
    BLEAdvertising* pAdvertising = pServer->getAdvertising();
    pAdvertising->setAppearance(HID_MOUSE);
    pAdvertising->addServiceUUID(hidDevice->hidService()->getUUID());
    pAdvertising->start();
  }

  void updateMouseReport(int8_t x, int8_t y, int8_t s) {
    uint8_t buttonMask = 0;

    if (pressedKeys.find(0) != pressedKeys.end()) {  
      buttonMask |= 0x01;                            
    }
    if (pressedKeys.find(1) != pressedKeys.end()) {  
      buttonMask |= 0x02;                            
    }
    if (pressedKeys.find(7) != pressedKeys.end()) {  
      buttonMask |= 0x04;                            
    }

    sendMouseReport(buttonMask, x, y, s);
  }

  void sendMouseReport(uint8_t buttonMask, int8_t x, int8_t y, int8_t s) {
    uint8_t report[5] = { ID_MOUSE, buttonMask, x, -y, s };
    inputMouse->setValue(report, sizeof(report));
    inputMouse->notify();
  }


  void sendKeyboardReport() {
    uint8_t report[9] = { ID_KEYBOARD, currentModifiers, 0, 0, 0, 0, 0, 0, 0 };
    int i = 3; 
    for (int keyIndex : pressedKeys) {
      if (keyIndex < 4 || keyIndex > 6) {        // Exclude modifier keys as they are handled separately
        if (i < 9 && keyIndex < numKeys) { 
          report[i++] = keyToHidCode[keyIndex];  // Map keyIndex to HID keycode
        }
      }
    }
    inputKeyboard->setValue(report, sizeof(report));
    inputKeyboard->notify();
  }


  void handleKeyPress(int keyIndex) {

    bool isModifier = false;
    uint8_t modifierMask = 0;

    switch (keyIndex) {
      case 0:  // Left Click
      case 1:  // Right Click
      case 7:  // Middle Click
        pressedKeys.insert(keyIndex);
        updateMouseReport(0, 0, 0);
        return;
        break;
      case 2:  // Space Key
        break;
      case 3:  // Escape Key
        break;
      case 4:  // CTRL
        modifierMask = MODIFIER_LEFT_CTRL;
        isModifier = true;
        break;
      case 5:  // Shift
        modifierMask = MODIFIER_LEFT_SHIFT;
        isModifier = true;
        break;
      case 6:  // Alt
        modifierMask = MODIFIER_LEFT_ALT;
        isModifier = true;
        break;
      default:
        break;
    }


    if (isModifier) {
      currentModifiers |= modifierMask;
    } else {
      pressedKeys.insert(keyIndex);
    }

    if (keyIndex > 1 && keyIndex < 7) {
      sendKeyboardReport();
    }
  }

  void handleKeyRelease(int keyIndex) {
    bool isModifier = false;
    uint8_t modifierMask = 0;

    switch (keyIndex) {

      case 0:  // Left Click
      case 1:  // Right Click
      case 7:  // Middle Click
        pressedKeys.erase(keyIndex);
        updateMouseReport(0, 0, 0);
        return;
      case 4:  // CTRL
        modifierMask = MODIFIER_LEFT_CTRL;
        isModifier = true;
        break;
      case 5:  // Shift
        modifierMask = MODIFIER_LEFT_SHIFT;
        isModifier = true;
        break;
      case 6:  // Alt
        modifierMask = MODIFIER_LEFT_ALT;
        isModifier = true;
        break;
    }

    if (isModifier) {
      currentModifiers &= ~modifierMask;
    } else {
      pressedKeys.erase(keyIndex);
    }


    // Handle mouse button release
    if (keyIndex == 0 || keyIndex == 1 || keyIndex == 7) {
      updateMouseReport(0, 0, 0);
    } else {
      sendKeyboardReport();
    }
  }
};

BLEHIDDeviceHandler hidDeviceHandler;

void MyServerCallbacks::onConnect(BLEServer* pServer) {
    Serial.println("Device connected");
}

void MyServerCallbacks::onDisconnect(BLEServer* pServer) {
    Serial.println("Device disconnected");
}

void setup() {
  Serial.begin(115200);

  while (!Serial) {}

  Serial.println(F("Started"));
  while (!trackpad.begin()) {
    Serial.println(F("Cirque Pinnacle not responding!"));
  }
  Serial.println(F("CirquePinnacle/examples/relative_mode"));
  trackpad.setDataMode(PINNACLE_RELATIVE);
  trackpad.relativeModeConfig();  // Uses default config
  Serial.println(F("Touch the trackpad to see the data."));
      
  hidDeviceHandler.setupDevice("ESP32 Mouse");
}

void loop() {
  hidDeviceHandler.loop();
  hidDeviceHandler.updateFromTrackpad();
}
