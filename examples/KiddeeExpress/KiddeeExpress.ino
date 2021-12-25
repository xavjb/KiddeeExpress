#include <Servo.h>
#include <Wire.h>
#include "KiddeeExpress.h"
#include <Ultrasonic.h>
#include <Stepper.h>
#include <SPI.h>
#include <U8x8lib.h>
//IR
#include <Arduino.h>
#define IR_INPUT_PIN    2
#include "TinyIRReceiver.cpp.h"
#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)
#include <IRremote.h>
//
//#include <Arduino_ST7789_Fast.h>
#include <LiquidCrystal_PCF8574.h>
//#include <HPMA115S0.h>
#include "PMS.h"
#if defined(__AVR__)
#include <avr/wdt.h>
#endif

#define ARDUINO_INSTANCE_ID         1

#define I2C_WRITE                   B00000000
#define I2C_READ                    B00001000
#define I2C_READ_CONTINUOUSLY       B00010000
#define I2C_STOP_READING            B00011000
#define I2C_READ_WRITE_MODE_MASK    B00011000
#define I2C_10BIT_ADDRESS_MODE_MASK B00100000
#define I2C_END_TX_MASK             B01000000
#define I2C_STOP_TX                 1
#define I2C_RESTART_TX              0
#define I2C_MAX_QUERIES             8
#define I2C_REGISTER_NOT_SPECIFIED  -1

// the minimum interval for sampling analog input
#define MINIMUM_SAMPLING_INTERVAL 1

#define INTER_PING_INTERVAL 40 // 40 ms.

// SYSEX command sub specifiers

#if defined(__AVR__)
#define TONE_TONE 0
#define TONE_NO_TONE 1
#endif

#define STEPPER_CONFIGURE 0
#define STEPPER_STEP 1
#define STEPPER_LIBRARY_VERSION 2

// DHT Sensor definitions
#define DHT_INTER_PING_INTERVAL 2000 // 2000 ms.
#define DHTLIB_ERROR_TIMEOUT    -2
#define DHTLIB_ERROR_CHECKSUM   -1
#define DHTLIB_OK                0
#define DHTLIB_TIMEOUT (F_CPU/40000)

// Honeywell Sensor definitions
#define SER_MODE               1
#define PM25_INTER_PING_INTERVAL 10 // 10 ms.

//LCD definitions
#define LCD_BEGIN                   0x00
#define LCD_HOME                    0x01
#define LCD_CLEAR                   0x02
#define LCD_BACKLIGHT               0x03
#define LCD_CURSOR                  0x04
#define LCD_BLINK                   0x05
#define LCD_DISPLAY                 0x06
#define LCD_SETCURSOR               0x07
#define LCD_LSCROLL                 0x08
#define LCD_RSCROLL                 0x09
#define LCD_PRINT                   0x7F

//OLED definitions
#define OLED_INIT              0x00
#define OLED_CLEARDISPLAY      0x01
#define OLED_FILLDISPLAY       0x02
#define OLED_CLEARLINE         0x03
#define OLED_SETCURSOR         0x04
#define OLED_SETINVERSEFONT    0x05
#define OLED_SETFONT           0x06
#define OLED_SETTEXTSIZE       0x07
#define OLED_PRINT             0x08
#define OLED_DRAWTILE          0x09
#define OLED_SETCONTRAST       0x0A
#define OLED_SETPOWERSAVE      0x0B
#define OLED_SETFLIPMODE       0x0C
#define OLED_DRAW2TILE         0x0D
#define OLED_DRAW4TILE         0x0E

//IR definintions
#define IR_BEGIN               0x00
#define IR_SEND                0x01
#define IR_BEGINSENDER         0x02
#define IR_INTER_PING_INTERVAL 50 // 50 ms
/*==============================================================================
   GLOBAL VARIABLES
  ============================================================================*/

#ifdef FIRMATA_SERIAL_FEATURE
SerialFirmata serialFeature;
#endif

/* analog inputs */
int analogInputsToReport = 0; // bitwise array to store pin reporting

/* digital input ports */
byte reportPINs[TOTAL_PORTS];       // 1 = report this port, 0 = silence
byte previousPINs[TOTAL_PORTS];     // previous 8 bits sent

/* pins configuration */
byte portConfigInputs[TOTAL_PORTS]; // each bit: 1 = pin in INPUT, 0 = anything else

/* timer variables */
unsigned long currentMillis;        // store the current value from millis()
unsigned long previousMillis;       // for comparison with currentMillis
unsigned int samplingInterval = 19; // how often to run the main loop (in ms)
#if defined(__AVR__)
  unsigned long previousKeepAliveMillis = 0;
  unsigned int keepAliveInterval = 0;
#endif

/* i2c data */
struct i2c_device_info {
  byte addr;
  int reg;
  byte bytes;
  byte stopTX;
};

/* for i2c read continuous more */
i2c_device_info query[I2C_MAX_QUERIES];

byte i2cRxData[64];
boolean isI2CEnabled = false;
signed char queryIndex = -1;
// default delay time between i2c read request and Wire.requestFrom()
unsigned int i2cReadDelayTime = 0;

Servo servos[MAX_SERVOS];
byte servoPinMap[TOTAL_PINS];
byte detachedServos[MAX_SERVOS];
byte detachedServoCount = 0;
byte servoCount = 0;

boolean isResetting = false;

// Forward declare a few functions to avoid compiler errors with older versions
// of the Arduino IDE.
void setPinModeCallback(byte, int);
void reportAnalogCallback(byte analogPin, int value);
void sysexCallback(byte, byte, byte*);

/* utility functions */
void wireWrite(byte data)
{
#if ARDUINO >= 100
  Wire.write((byte)data);
#else
  Wire.send(data);
#endif
}

byte wireRead(void)
{
#if ARDUINO >= 100
  return Wire.read();
#else
  return Wire.receive();
#endif
}

// Ping variables
int numLoops = 0 ;
int pingLoopCounter = 0 ;

int numActiveSonars = 0 ; // number of sonars attached
uint8_t sonarPinNumbers[MAX_SONARS] ;
int nextSonar = 0 ; // index into sonars[] for next device

// array to hold up to 6 instances of sonar devices
Ultrasonic *sonars[MAX_SONARS] ;

uint8_t sonarTriggerPin;
uint8_t sonarEchoPin ;
uint8_t currentSonar = 0;            // Keeps track of which sensor is active.

uint8_t pingInterval = 33 ;  // Milliseconds between sensor pings (29ms is about the min to avoid
// cross- sensor echo).
//byte sonarMSB, sonarLSB ;


// Stepper Motor
Stepper *stepper = NULL;

// DHT sensors
int numActiveDHTs = 0 ; // number of DHTs attached
uint8_t DHT_PinNumbers[MAX_DHTS] ;
uint8_t DHT_WakeUpDelay[MAX_DHTS] ;
uint8_t DHT_TYPE[MAX_DHTS] ;

uint8_t nextDHT = 0 ; // index into dht[] for next device
uint8_t currentDHT = 0;            // Keeps track of which sensor is active.

int dhtNumLoops = 0;
int dhtLoopCounter = 0;

uint8_t _bits[5];  // buffer to receive data

//PM25
//Check if Serial1 exists, if not then use software serial
#ifndef HAVE_HWSERIAL1
  #include <SoftwareSerial.h>
  SoftwareSerial Serial1(5, 6); //RX, TX
#endif
PMS pms(Serial1);
PMS::DATA pms_data;
bool PM25_started = 0;
int pm25LoopCounter = 0;
int pm25NumLoops = 0;
//unsigned int pm2_5, pm10;
//bool firstPM25 = 0;

//LCD
LiquidCrystal_PCF8574 lcd(0x3F);
bool LCD_started = 0;

//OLED
U8X8_SH1106_128X64_NONAME_HW_I2C u8x8(/* reset=*/ U8X8_PIN_NONE); //A5 SCL, A4 SDA
//LiquidCrystal_PCF8574 OLED(0x3F);
bool OLED_started = 0;
uint8_t cx = 0, cy = 0; //Cursor position
uint8_t textSize = 0;

//IR
bool IR_started = 0;
int IRLoopCounter = 0;
int IRNumLoops = 0;
bool IR_interruptFlag = false;

////Error Messages stored in PROGMEM to save SRAM
//const char PING1[] PROGMEM = "PING_CONFIG Err:Too many sonars";
const char string_0[] PROGMEM = "Max servos reached";
const char string_1[] PROGMEM = "I2C:Too many bytes input";
const char string_2[] PROGMEM = "I2C:Too few bytes input";
const char string_3[] PROGMEM = "Unknown pin mode";
const char string_4[] PROGMEM = "too many queries";
const char string_5[] PROGMEM = "Not enough data";
const char string_6[] PROGMEM = "PING_CONFIG Err:Too many sonars";
const char string_7[] PROGMEM = "STEP CONFIG Err:Wrong Num of args";
const char string_8[] PROGMEM = "STEP OP Err:MOT NOT CONFIGURED";
const char string_9[] PROGMEM = "STEP FIRMWARE VER Err:NO MOT CONFIGURED";
const char string_10[] PROGMEM = "STEP CONFIG Err:UNKNOWN CMD";
const char string_11[] PROGMEM = "ERR:UNKNOWN DHT TYPE";
const char string_12[] PROGMEM = "DHT_CONFIG Err:Too many devices";
const char *const string_table[] PROGMEM = {string_0, string_1, string_2, string_3, string_4, string_5, string_6, string_7, string_8, string_9, string_10, string_11, string_12};
char msgBuffer[40];
/*==============================================================================
   FUNCTIONS
  ============================================================================*/

void setDrawCursor(uint8_t x, uint8_t y)
{
  cx = x;
  cy = y;
}

void printText(char text)
{
  bool width = false, height = false;
  uint8_t nx, ny, lenStr;
  char printData[2] = {text,'\0'};
  
  switch (textSize)
  {
    case 0:
      u8x8.drawString(cx, cy, printData);
      break;
    case 1:
      u8x8.draw1x2String(cx, cy, printData);
      height = true;
      break;
    case 2:
      u8x8.draw2x2String(cx, cy, printData);
      width = true;
      height = true;
      break;
  }
  
  cx += 1+width;
  if (cx > 15)
  {
    cy += 1+height;
    if (cy > 7)
    {
      cy = 0;
    }
    cx = 0;
  }
}

void attachServo(byte pin, int minPulse, int maxPulse)
{
  if (servoCount < MAX_SERVOS) {
    // reuse indexes of detached servos until all have been reallocated
    if (detachedServoCount > 0) {
      servoPinMap[pin] = detachedServos[detachedServoCount - 1];
      if (detachedServoCount > 0) detachedServoCount--;
    } else {
      servoPinMap[pin] = servoCount;
      servoCount++;
    }
    if (minPulse > 0 && maxPulse > 0) {
      servos[servoPinMap[pin]].attach(PIN_TO_DIGITAL(pin), minPulse, maxPulse);
    } else {
      servos[servoPinMap[pin]].attach(PIN_TO_DIGITAL(pin));
    }
  } else {
    strcpy_P(msgBuffer, (char *)pgm_read_word(&(string_table[0])));
    Firmata.sendString(msgBuffer);
//    Firmata.sendString("Max servos reached");
  }
}

void detachServo(byte pin)
{
  servos[servoPinMap[pin]].detach();
  // if we're detaching the last servo, decrement the count
  // otherwise store the index of the detached servo
  if (servoPinMap[pin] == servoCount && servoCount > 0) {
    servoCount--;
  } else if (servoCount > 0) {
    // keep track of detached servos because we want to reuse their indexes
    // before incrementing the count of attached servos
    detachedServoCount++;
    detachedServos[detachedServoCount - 1] = servoPinMap[pin];
  }

  servoPinMap[pin] = 255;
}

void enableI2CPins()
{
  byte i;
  // is there a faster way to do this? would probaby require importing
  // Arduino.h to get SCL and SDA pins
  for (i = 0; i < TOTAL_PINS; i++) {
    if (IS_PIN_I2C(i)) {
      // mark pins as i2c so they are ignore in non i2c data requests
      setPinModeCallback(i, PIN_MODE_I2C);
    }
  }

  isI2CEnabled = true;

  Wire.begin();
}

/* disable the i2c pins so they can be used for other functions */
void disableI2CPins() {
  isI2CEnabled = false;
  // disable read continuous mode for all devices
  queryIndex = -1;
}

void readAndReportData(byte address, int theRegister, byte numBytes, byte stopTX) {
  // allow I2C requests that don't require a register read
  // for example, some devices using an interrupt pin to signify new data available
  // do not always require the register read so upon interrupt you call Wire.requestFrom()
  if (theRegister != I2C_REGISTER_NOT_SPECIFIED) {
    Wire.beginTransmission(address);
    wireWrite((byte)theRegister);
    Wire.endTransmission(stopTX); // default = true
    // do not set a value of 0
    if (i2cReadDelayTime > 0) {
      // delay is necessary for some devices such as WiiNunchuck
      delayMicroseconds(i2cReadDelayTime);
    }
  } else {
    theRegister = 0;  // fill the register with a dummy value
  }

  Wire.requestFrom(address, numBytes);  // all bytes are returned in requestFrom

  // check to be sure correct number of bytes were returned by slave
  if (numBytes < Wire.available()) {
    strcpy_P(msgBuffer, (char *)pgm_read_word(&(string_table[1])));
    Firmata.sendString(msgBuffer);
    //Firmata.sendString("I2C:Too many bytes input");
  } else if (numBytes > Wire.available()) {
    strcpy_P(msgBuffer, (char *)pgm_read_word(&(string_table[2])));
    Firmata.sendString(msgBuffer);
    //Firmata.sendString("I2C:Too few bytes input");
  }

  i2cRxData[0] = address;
  i2cRxData[1] = theRegister;

  for (int i = 0; i < numBytes && Wire.available(); i++) {
    i2cRxData[2 + i] = wireRead();
  }

  // send slave address, register and received bytes
  Firmata.sendSysex(SYSEX_I2C_REPLY, numBytes + 2, i2cRxData);
}

void outputPort(byte portNumber, byte portValue, byte forceSend)
{
  // pins not configured as INPUT are cleared to zeros
  portValue = portValue & portConfigInputs[portNumber];
  // only send if the value is different than previously sent
  if (forceSend || previousPINs[portNumber] != portValue) {
    Firmata.sendDigitalPort(portNumber, portValue);
    previousPINs[portNumber] = portValue;
  }
}

/* -----------------------------------------------------------------------------
   check all the active digital inputs for change of state, then add any events
   to the Serial output queue using Serial.print() */
void checkDigitalInputs(void)
{
  /* Using non-looping code allows constants to be given to readPort().
     The compiler will apply substantial optimizations if the inputs
     to readPort() are compile-time constants. */
  if (TOTAL_PORTS > 0 && reportPINs[0]) outputPort(0, readPort(0, portConfigInputs[0]), false);
  if (TOTAL_PORTS > 1 && reportPINs[1]) outputPort(1, readPort(1, portConfigInputs[1]), false);
  if (TOTAL_PORTS > 2 && reportPINs[2]) outputPort(2, readPort(2, portConfigInputs[2]), false);
  if (TOTAL_PORTS > 3 && reportPINs[3]) outputPort(3, readPort(3, portConfigInputs[3]), false);
  if (TOTAL_PORTS > 4 && reportPINs[4]) outputPort(4, readPort(4, portConfigInputs[4]), false);
  if (TOTAL_PORTS > 5 && reportPINs[5]) outputPort(5, readPort(5, portConfigInputs[5]), false);
  if (TOTAL_PORTS > 6 && reportPINs[6]) outputPort(6, readPort(6, portConfigInputs[6]), false);
  if (TOTAL_PORTS > 7 && reportPINs[7]) outputPort(7, readPort(7, portConfigInputs[7]), false);
  if (TOTAL_PORTS > 8 && reportPINs[8]) outputPort(8, readPort(8, portConfigInputs[8]), false);
  if (TOTAL_PORTS > 9 && reportPINs[9]) outputPort(9, readPort(9, portConfigInputs[9]), false);
  if (TOTAL_PORTS > 10 && reportPINs[10]) outputPort(10, readPort(10, portConfigInputs[10]), false);
  if (TOTAL_PORTS > 11 && reportPINs[11]) outputPort(11, readPort(11, portConfigInputs[11]), false);
  if (TOTAL_PORTS > 12 && reportPINs[12]) outputPort(12, readPort(12, portConfigInputs[12]), false);
  if (TOTAL_PORTS > 13 && reportPINs[13]) outputPort(13, readPort(13, portConfigInputs[13]), false);
  if (TOTAL_PORTS > 14 && reportPINs[14]) outputPort(14, readPort(14, portConfigInputs[14]), false);
  if (TOTAL_PORTS > 15 && reportPINs[15]) outputPort(15, readPort(15, portConfigInputs[15]), false);
}

// -----------------------------------------------------------------------------
/* sets the pin mode to the correct state and sets the relevant bits in the
   two bit-arrays that track Digital I/O and PWM status
*/
void setPinModeCallback(byte pin, int mode)
{

  if (Firmata.getPinMode(pin) == PIN_MODE_IGNORE)
    return;

  if (Firmata.getPinMode(pin) == PIN_MODE_I2C && isI2CEnabled && mode != PIN_MODE_I2C) {
    // disable i2c so pins can be used for other functions
    // the following if statements should reconfigure the pins properly
    disableI2CPins();
  }
  if (IS_PIN_DIGITAL(pin) && mode != PIN_MODE_SERVO) {
    if (servoPinMap[pin] < MAX_SERVOS && servos[servoPinMap[pin]].attached()) {
      detachServo(pin);
    }
  }
  if (IS_PIN_ANALOG(pin)) {
    reportAnalogCallback(PIN_TO_ANALOG(pin), mode == PIN_MODE_ANALOG ? 1 : 0); // turn on/off reporting
  }
  if (IS_PIN_DIGITAL(pin)) {
    if (mode == INPUT || mode == PIN_MODE_PULLUP) {
      portConfigInputs[pin / 8] |= (1 << (pin & 7));
    } else {
      portConfigInputs[pin / 8] &= ~(1 << (pin & 7));
    }
  }
  Firmata.setPinState(pin, 0);
  switch (mode) {
    case PIN_MODE_ANALOG:
      if (IS_PIN_ANALOG(pin)) {
        if (IS_PIN_DIGITAL(pin)) {
          pinMode(PIN_TO_DIGITAL(pin), INPUT);    // disable output driver
#if ARDUINO <= 100
          // deprecated since Arduino 1.0.1 - TODO: drop support in Firmata 2.6
          digitalWrite(PIN_TO_DIGITAL(pin), LOW); // disable internal pull-ups
#endif
        }
        Firmata.setPinMode(pin, PIN_MODE_ANALOG);
      }
      break;
    case INPUT:
      if (IS_PIN_DIGITAL(pin)) {
        pinMode(PIN_TO_DIGITAL(pin), INPUT);    // disable output driver
#if ARDUINO <= 100
        // deprecated since Arduino 1.0.1 - TODO: drop support in Firmata 2.6
        digitalWrite(PIN_TO_DIGITAL(pin), LOW); // disable internal pull-ups
#endif
        Firmata.setPinMode(pin, INPUT);
      }
      break;
    case PIN_MODE_PULLUP:
      if (IS_PIN_DIGITAL(pin)) {
        pinMode(PIN_TO_DIGITAL(pin), INPUT_PULLUP);
        Firmata.setPinMode(pin, PIN_MODE_PULLUP);
        Firmata.setPinState(pin, 1);
      }
      break;
    case OUTPUT:
      if (IS_PIN_DIGITAL(pin)) {
        if (Firmata.getPinMode(pin) == PIN_MODE_PWM) {
          // Disable PWM if pin mode was previously set to PWM.
          digitalWrite(PIN_TO_DIGITAL(pin), LOW);
        }
        pinMode(PIN_TO_DIGITAL(pin), OUTPUT);
        Firmata.setPinMode(pin, OUTPUT);
      }
      break;
    case PIN_MODE_PWM:
      if (IS_PIN_PWM(pin)) {
        pinMode(PIN_TO_PWM(pin), OUTPUT);
        analogWrite(PIN_TO_PWM(pin), 0);
        Firmata.setPinMode(pin, PIN_MODE_PWM);
      }
      break;
    case PIN_MODE_SERVO:
      if (IS_PIN_DIGITAL(pin)) {
        Firmata.setPinMode(pin, PIN_MODE_SERVO);
        if (servoPinMap[pin] == 255 || !servos[servoPinMap[pin]].attached()) {
          // pass -1 for min and max pulse values to use default values set
          // by Servo library
          attachServo(pin, -1, -1);
        }
      }
      break;
    case PIN_MODE_I2C:
      if (IS_PIN_I2C(pin)) {
        // mark the pin as i2c
        // the user must call I2C_CONFIG to enable I2C for a device
        Firmata.setPinMode(pin, PIN_MODE_I2C);
      }
      break;
    case PIN_MODE_SERIAL:
#ifdef FIRMATA_SERIAL_FEATURE
      serialFeature.handlePinMode(pin, PIN_MODE_SERIAL);
#endif
      break;
#if defined(__AVR__)
    case PIN_MODE_TONE:
      Firmata.setPinMode(pin, PIN_MODE_TONE);
      break ;
#endif
    case PIN_MODE_SONAR:
      Firmata.setPinMode(pin, PIN_MODE_SONAR);
      break ;
    case PIN_MODE_DHT:
      Firmata.setPinMode(pin, PIN_MODE_DHT);
      break ;
    case PIN_MODE_STEPPER:
      Firmata.setPinMode(pin, PIN_MODE_STEPPER);
      break ;
    default:
      strcpy_P(msgBuffer, (char *)pgm_read_word(&(string_table[3])));
      Firmata.sendString(msgBuffer);
      //Firmata.sendString("Unknown pin mode"); // TODO: put error msgs in EEPROM
      break ;
  }
  // TODO: save status to EEPROM here, if changed
}

/*
   Sets the value of an individual pin. Useful if you want to set a pin value but
   are not tracking the digital port state.
   Can only be used on pins configured as OUTPUT.
   Cannot be used to enable pull-ups on Digital INPUT pins.
*/
void setPinValueCallback(byte pin, int value)
{
  if (pin < TOTAL_PINS && IS_PIN_DIGITAL(pin)) {
    if (Firmata.getPinMode(pin) == OUTPUT) {
      Firmata.setPinState(pin, value);
      digitalWrite(PIN_TO_DIGITAL(pin), value);
    }
  }
}

void analogWriteCallback(byte pin, int value)
{
  if (pin < TOTAL_PINS) {
    switch (Firmata.getPinMode(pin)) {
      case PIN_MODE_SERVO:
        if (IS_PIN_DIGITAL(pin))
          servos[servoPinMap[pin]].write(value);
        Firmata.setPinState(pin, value);
        break;
      case PIN_MODE_PWM:
        if (IS_PIN_PWM(pin))
          analogWrite(PIN_TO_PWM(pin), value);
        Firmata.setPinState(pin, value);
        break;
    }
  }
}

void digitalWriteCallback(byte port, int value)
{
  byte pin, lastPin, pinValue, mask = 1, pinWriteMask = 0;

  if (port < TOTAL_PORTS) {
    // create a mask of the pins on this port that are writable.
    lastPin = port * 8 + 8;
    if (lastPin > TOTAL_PINS) lastPin = TOTAL_PINS;
    for (pin = port * 8; pin < lastPin; pin++) {
      // do not disturb non-digital pins (eg, Rx & Tx)
      if (IS_PIN_DIGITAL(pin)) {
        // do not touch pins in PWM, ANALOG, SERVO or other modes
        if (Firmata.getPinMode(pin) == OUTPUT || Firmata.getPinMode(pin) == INPUT) {
          pinValue = ((byte)value & mask) ? 1 : 0;
          if (Firmata.getPinMode(pin) == OUTPUT) {
            pinWriteMask |= mask;
          } else if (Firmata.getPinMode(pin) == INPUT && pinValue == 1 && Firmata.getPinState(pin) != 1) {
            // only handle INPUT here for backwards compatibility
#if ARDUINO > 100
            pinMode(pin, INPUT_PULLUP);
#else
            // only write to the INPUT pin to enable pullups if Arduino v1.0.0 or earlier
            pinWriteMask |= mask;
#endif
          }
          Firmata.setPinState(pin, pinValue);
        }
      }
      mask = mask << 1;
    }
    writePort(port, (byte)value, pinWriteMask);
  }
}


// -----------------------------------------------------------------------------
/* sets bits in a bit array (int) to toggle the reporting of the analogIns
*/
//void FirmataClass::setAnalogPinReporting(byte pin, byte state) {
//}
void reportAnalogCallback(byte analogPin, int value)
{
  if (analogPin < TOTAL_ANALOG_PINS) {
    if (value == 0) {
      analogInputsToReport = analogInputsToReport & ~ (1 << analogPin);
    } else {
      analogInputsToReport = analogInputsToReport | (1 << analogPin);
      // prevent during system reset or all analog pin values will be reported
      // which may report noise for unconnected analog pins
      if (!isResetting) {
        // Send pin value immediately. This is helpful when connected via
        // ethernet, wi-fi or bluetooth so pin states can be known upon
        // reconnecting.
        Firmata.sendAnalog(analogPin, analogRead(analogPin));
      }
    }
  }
  // TODO: save status to EEPROM here, if changed
}

void reportDigitalCallback(byte port, int value)
{
  if (port < TOTAL_PORTS) {
    reportPINs[port] = (byte)value;
    // Send port value immediately. This is helpful when connected via
    // ethernet, wi-fi or bluetooth so pin states can be known upon
    // reconnecting.
    if (value) outputPort(port, readPort(port, portConfigInputs[port]), true);
  }
  // do not disable analog reporting on these 8 pins, to allow some
  // pins used for digital, others analog.  Instead, allow both types
  // of reporting to be enabled, but check if the pin is configured
  // as analog when sampling the analog inputs.  Likewise, while
  // scanning digital pins, portConfigInputs will mask off values from any
  // pins configured as analog
}

/*==============================================================================
   SYSEX-BASED commands
  ============================================================================*/

void sysexCallback(byte command, byte argc, byte *argv)
{
  byte mode;
  byte stopTX;
  byte slaveAddress;
  byte data;
  int slaveRegister;
  unsigned int delayTime;
  byte pin ;
  int frequency ;
  int duration ;

//  Uncomment to display the received command for debugging purposes
//  if(command != RU_THERE && command != CAPABILITY_QUERY && command != SAMPLING_INTERVAL)
//  {
//    printData((char*)"Command:", command);
//    for(byte i = 0; i < sizeof(argv); i++)
//    {
//      printData((char*)"argv:", argv[i]);
//    }
//  }
  
  switch (command) {
    case PM25_CONFIG:
      if(argv[0] == 0) {
        PM25_started = true;
//        firstPM25 = 0;
        #ifndef HAVE_HWSERIAL1
          Serial1.listen();
        #endif
        pms.wakeUp();
      }
      else
      {
//        pms.sleep();
        #ifndef HAVE_HWSERIAL1
          Serial1.end();
        #endif
        PM25_started = false;
      }
      break;
    case LCD_CONFIG:
      switch (argv[0]) {
        case LCD_BEGIN:
          if (!isI2CEnabled) {
            enableI2CPins();
          }
          Wire.beginTransmission(0x3F);
          if(Wire.endTransmission() == true)
          {
            Firmata.sendString("LCD not found");
            break;
          }
          LCD_started = true;
          lcd.begin(16, 2);
          break;
        case LCD_HOME:
          if(LCD_started)
          {
            lcd.home();
          }
          break;
        case LCD_CLEAR:
          if(LCD_started)
          {
            lcd.clear();
          }
          break;
        case LCD_BACKLIGHT:
          if(LCD_started && argc == 2)
          {
            if(argv[1])
            {
              lcd.setBacklight(255);
            }
            else
            {
              lcd.setBacklight(0);
            }
          }
          break;
        case LCD_CURSOR:
          if(LCD_started && argc == 2)
          {
            if(argv[1])
            {
              lcd.cursor();
            }
            else
            {
              lcd.noCursor();
            }
          }
          break;
        case LCD_BLINK:
          if(LCD_started && argc == 2)
          {
            if(argv[1])
            {
              lcd.blink();
            }
            else
            {
              lcd.noBlink();
            }
          }
          break;
        case LCD_DISPLAY:
          if(LCD_started && argc == 2)
          {
            if(argv[1])
            {
              lcd.display();
            }
            else
            {
              lcd.noDisplay();
            }
          }
          break;
        case LCD_SETCURSOR:
          if(LCD_started && argc == 3)
          {
            lcd.setCursor(argv[1], argv[2]);
          }
          break;
        case LCD_LSCROLL:
          if(LCD_started)
          {
            lcd.scrollDisplayLeft();
          }
          break;
        case LCD_RSCROLL:
          if(LCD_started)
          {
            lcd.scrollDisplayRight();
          }
          break;
        case LCD_PRINT:
          if(LCD_started)
          {
            for (byte i = 1; i < argc; i += 2) 
            {
              data = argv[i] + (argv[i + 1] << 7);
              lcd.print((char)data);
            }    
          }
          break;
      }
      break;
    case OLED_CONFIG:
      switch (argv[0]) {
        case OLED_INIT:
          OLED_started = true;
          if (!isI2CEnabled) {
            enableI2CPins();
          }
          u8x8.setBusClock(400000);
          u8x8.begin();
          u8x8.setFont(u8x8_font_amstrad_cpc_extended_f); 
          break;
        case OLED_CLEARDISPLAY:
          if(OLED_started)
          {
            u8x8.clearDisplay();
          }
          break;
        case OLED_FILLDISPLAY:
          if(OLED_started)
          {
            u8x8.fillDisplay();
          }
          break;
        case OLED_CLEARLINE:
          if(OLED_started && argc == 2)
          {
            u8x8.clearLine(argv[1]);
          }
          break;
        case OLED_SETCURSOR:
          if(OLED_started && argc == 3)
          {
            setDrawCursor(argv[1],argv[2]);
          }
          break;
        case OLED_SETINVERSEFONT:
          if(OLED_started && argc == 2)
          {
            u8x8.setInverseFont((bool)argv[1]);
          }
          break;
//        case OLED_SETFONT:
//          if(OLED_started && argc == 12)
//          {
//            OLED.drawLine(argv[1] + (argv[2] << 7), argv[3] + (argv[4] << 7), argv[5] + (argv[6] << 7), argv[7] + (argv[8] << 7), argv[9] + (argv[10] << 7) + (argv[11] << 14));
//          }
//          break;
        case OLED_SETTEXTSIZE:
          if(OLED_started && argc == 2)
          {
            textSize = argv[1];
          }
          break;
        case OLED_PRINT:
          if(OLED_started)
          {
            for (byte i = 1; i < argc; i += 2) 
            {
              data = argv[i] + (argv[i + 1] << 7);
              printText((char)data);
            }
          }
          break;
        case OLED_DRAWTILE:
          if(OLED_started)
          {
            uint8_t tileBuffer[8], buffIndex = 0;
            for(byte i = 1; i < argc; i += 2)
            {
              tileBuffer[buffIndex] = argv[i] + (argv[i + 1] << 7);
              buffIndex++;
            }
            u8x8.drawTile(cx, cy, 1, tileBuffer);
            cx++;
            if (cx > 15)
            {
              cy++;
              if (cy > 7)
              {
                cy = 0;
              }
              cx = 0;
            }
          }
          break;
        case OLED_DRAW2TILE:
          if(OLED_started)
          {
            uint8_t tileBuffer[16], buffIndex = 0;
            for(byte i = 1; i < argc; i += 2)
            {
              tileBuffer[buffIndex] = argv[i] + (argv[i + 1] << 7);
              buffIndex++;
            }
            u8x8.drawTile(cx, cy, 2, tileBuffer);
            cx += 2;
            if (cx > 15)
            {
              cy++;
              if (cy > 7)
              {
                cy = 0;
              }
              cx = 0;
            }
          }
          break;
        case OLED_DRAW4TILE:
          if(OLED_started)
          {
            uint8_t tileBuffer[32], buffIndex = 0;
            for(byte i = 1; i < argc; i += 2)
            {
              tileBuffer[buffIndex] = argv[i] + (argv[i + 1] << 7);
              buffIndex++;
            }
            u8x8.drawTile(cx, cy, 4, tileBuffer);
            cx += 4;
            if (cx > 15)
            {
              cy++;
              if (cy > 7)
              {
                cy = 0;
              }
              cx = 0;
            }
          }
          break;
        case OLED_SETCONTRAST:
          if(OLED_started && argc == 3)
          {
            u8x8.setContrast(argv[1] + (argv[2] << 7));
          }
          break;
        case OLED_SETPOWERSAVE:
          if(OLED_started && argc == 2)
          {
            u8x8.setPowerSave(argv[1]);
          }
          break;
        case OLED_SETFLIPMODE:
          if(OLED_started && argc == 2)
          {
            u8x8.setFlipMode(argv[1]);
          }
          break;
      }
      break;
    case IR_CONFIG:
      switch (argv[0]) {
        case IR_BEGIN:
          IR_started = true;
          initPCIInterruptForTinyReceiver();
          break;
        case IR_BEGINSENDER:
          IrSender.begin(false);
          break;
        case IR_SEND:
          if(argc == 5){
            uint16_t IRaddress = argv[1] + (argv[2] << 7);
            uint16_t IRdata = argv[3] + (argv[4] << 7);
            IrSender.sendNEC(IRaddress, IRdata, 0);
          }
          break;
      }
      break;
    case RU_THERE:
      Firmata.write(START_SYSEX);
      Firmata.write((byte)I_AM_HERE);
      Firmata.write((byte)ARDUINO_INSTANCE_ID);
      Firmata.write(END_SYSEX);
      break ;
    case I2C_REQUEST:
      mode = argv[1] & I2C_READ_WRITE_MODE_MASK;
      if (argv[1] & I2C_10BIT_ADDRESS_MODE_MASK) {
        //Firmata.sendString("No 10-bit address allowed");
        return;
      }
      else {
        slaveAddress = argv[0];
      }

      // need to invert the logic here since 0 will be default for client
      // libraries that have not updated to add support for restart tx
      if (argv[1] & I2C_END_TX_MASK) {
        stopTX = I2C_RESTART_TX;
      }
      else {
        stopTX = I2C_STOP_TX; // default
      }

      switch (mode) {
        case I2C_WRITE:
          Wire.beginTransmission(slaveAddress);
          for (byte i = 2; i < argc; i += 2) {
            data = argv[i] + (argv[i + 1] << 7);
            wireWrite(data);
          }
          Wire.endTransmission();
          delayMicroseconds(70);
          break;
        case I2C_READ:
          if (argc == 6) {
            // a slave register is specified
            slaveRegister = argv[2] + (argv[3] << 7);
            data = argv[4] + (argv[5] << 7);  // bytes to read
          }
          else {
            // a slave register is NOT specified
            slaveRegister = I2C_REGISTER_NOT_SPECIFIED;
            data = argv[2] + (argv[3] << 7);  // bytes to read
          }
          readAndReportData(slaveAddress, (int)slaveRegister, data, stopTX);
          break;
        case I2C_READ_CONTINUOUSLY:
          if ((queryIndex + 1) >= I2C_MAX_QUERIES) {
            // too many queries, just ignore
            strcpy_P(msgBuffer, (char *)pgm_read_word(&(string_table[4])));
            Firmata.sendString(msgBuffer);
            //Firmata.sendString("too many queries");
            break;
          }
          if (argc == 6) {
            // a slave register is specified
            slaveRegister = argv[2] + (argv[3] << 7);
            data = argv[4] + (argv[5] << 7);  // bytes to read
          }
          else {
            // a slave register is NOT specified
            slaveRegister = (int)I2C_REGISTER_NOT_SPECIFIED;
            data = argv[2] + (argv[3] << 7);  // bytes to read
          }
          queryIndex++;
          query[queryIndex].addr = slaveAddress;
          query[queryIndex].reg = slaveRegister;
          query[queryIndex].bytes = data;
          query[queryIndex].stopTX = stopTX;
          break;
        case I2C_STOP_READING:
          byte queryIndexToSkip;
          // if read continuous mode is enabled for only 1 i2c device, disable
          // read continuous reporting for that device
          if (queryIndex <= 0) {
            queryIndex = -1;
          } else {
            queryIndexToSkip = 0;
            // if read continuous mode is enabled for multiple devices,
            // determine which device to stop reading and remove it's data from
            // the array, shifiting other array data to fill the space
            for (byte i = 0; i < queryIndex + 1; i++) {
              if (query[i].addr == slaveAddress) {
                queryIndexToSkip = i;
                break;
              }
            }

            for (byte i = queryIndexToSkip; i < queryIndex + 1; i++) {
              if (i < I2C_MAX_QUERIES) {
                query[i].addr = query[i + 1].addr;
                query[i].reg = query[i + 1].reg;
                query[i].bytes = query[i + 1].bytes;
                query[i].stopTX = query[i + 1].stopTX;
              }
            }
            queryIndex--;
          }
          break;
        default:
          break;
      }
      break;
    case I2C_CONFIG:
      delayTime = (argv[0] + (argv[1] << 7));

      if (argc > 1 && delayTime > 0) {
        i2cReadDelayTime = delayTime;
      }

      if (!isI2CEnabled) {
        enableI2CPins();
      }
      
      break;
    case SERVO_CONFIG:
      if (argc > 4) {
        // these vars are here for clarity, they'll optimized away by the compiler
        byte pin = argv[0];
        int minPulse = argv[1] + (argv[2] << 7);
        int maxPulse = argv[3] + (argv[4] << 7);

        if (IS_PIN_DIGITAL(pin)) {
          if (servoPinMap[pin] < MAX_SERVOS && servos[servoPinMap[pin]].attached()) {
            detachServo(pin);
          }
          attachServo(pin, minPulse, maxPulse);
          setPinModeCallback(pin, PIN_MODE_SERVO);
        }
      }
      break;
#if defined(__AVR__)
    case KEEP_ALIVE:
      keepAliveInterval = argv[0] + (argv[1] << 7);
      previousKeepAliveMillis = millis();
      break;
#endif
    case SAMPLING_INTERVAL:
      if (argc > 1) {
        samplingInterval = argv[0] + (argv[1] << 7);
        if (samplingInterval < MINIMUM_SAMPLING_INTERVAL) {
          samplingInterval = MINIMUM_SAMPLING_INTERVAL;
        }
        /* calculate number of loops per ping */
        numLoops = INTER_PING_INTERVAL / samplingInterval ;
        /* calculate number of loops between each sample of DHT data */
        dhtNumLoops = DHT_INTER_PING_INTERVAL / samplingInterval ;
        /* calculate number of loops between each sample of PWM data */
        pm25NumLoops = PM25_INTER_PING_INTERVAL / samplingInterval ;
        /* calculate number of loops between each sample of IR data */
        IRNumLoops = IR_INTER_PING_INTERVAL / samplingInterval ;
      }
      else {
        strcpy_P(msgBuffer, (char *)pgm_read_word(&(string_table[5])));
        Firmata.sendString(msgBuffer);
//        Firmata.sendString("Not enough data");
      }
      break;
    case EXTENDED_ANALOG:
      if (argc > 1) {
        int val = argv[1];
        if (argc > 2) val |= (argv[2] << 7);
        if (argc > 3) val |= (argv[3] << 14);
        analogWriteCallback(argv[0], val);
      }
      break;
    case CAPABILITY_QUERY:
      Firmata.write(START_SYSEX);
      Firmata.write(CAPABILITY_RESPONSE);
      for (byte pin = 0; pin < TOTAL_PINS; pin++) {
        if (IS_PIN_DIGITAL(pin)) {
          Firmata.write((byte)INPUT);
          Firmata.write(1);
          Firmata.write((byte)PIN_MODE_PULLUP);
          Firmata.write(1);
          Firmata.write((byte)OUTPUT);
          Firmata.write(1);
          Firmata.write((byte)PIN_MODE_STEPPER);
          Firmata.write(1);
          Firmata.write((byte)PIN_MODE_SONAR);
          Firmata.write(1);
          Firmata.write((byte)PIN_MODE_DHT);
          Firmata.write(1);

#if defined(__AVR__)
          Firmata.write((byte)PIN_MODE_TONE);
          Firmata.write(1);
#endif
        }
        if (IS_PIN_ANALOG(pin)) {
          Firmata.write(PIN_MODE_ANALOG);
          Firmata.write(10); // 10 = 10-bit resolution
        }
        if (IS_PIN_PWM(pin)) {
          Firmata.write(PIN_MODE_PWM);
          Firmata.write(DEFAULT_PWM_RESOLUTION);
        }
        if (IS_PIN_DIGITAL(pin)) {
          Firmata.write(PIN_MODE_SERVO);
          Firmata.write(14);
        }
        if (IS_PIN_I2C(pin)) {
          Firmata.write(PIN_MODE_I2C);
          Firmata.write(1);  // TODO: could assign a number to map to SCL or SDA
        }
#ifdef FIRMATA_SERIAL_FEATURE
        serialFeature.handleCapability(pin);
#endif
        Firmata.write(127);
      }
      Firmata.write(END_SYSEX);
      break;
    case PIN_STATE_QUERY:
      if (argc > 0) {
        byte pin = argv[0];
        Firmata.write(START_SYSEX);
        Firmata.write(PIN_STATE_RESPONSE);
        Firmata.write(pin);
        if (pin < TOTAL_PINS) {
          Firmata.write(Firmata.getPinMode(pin));
          Firmata.write((byte)Firmata.getPinState(pin) & 0x7F);
          if (Firmata.getPinState(pin) & 0xFF80) Firmata.write((byte)(Firmata.getPinState(pin) >> 7) & 0x7F);
          if (Firmata.getPinState(pin) & 0xC000) Firmata.write((byte)(Firmata.getPinState(pin) >> 14) & 0x7F);
        }
        Firmata.write(END_SYSEX);
      }
      break;
    case ANALOG_MAPPING_QUERY:
      Firmata.write(START_SYSEX);
      Firmata.write(ANALOG_MAPPING_RESPONSE);
      for (byte pin = 0; pin < TOTAL_PINS; pin++) {
        Firmata.write(IS_PIN_ANALOG(pin) ? PIN_TO_ANALOG(pin) : 127);
      }
      Firmata.write(END_SYSEX);
      break;

    case SERIAL_MESSAGE:
#ifdef FIRMATA_SERIAL_FEATURE
      serialFeature.handleSysex(command, argc, argv);
#endif
      break;

#if defined(__AVR__)
    case TONE_DATA:
      byte toneCommand, pin;
      int frequency, duration;

      toneCommand = argv[0];
      pin = argv[1];

      if (toneCommand == TONE_TONE) {
        frequency = argv[2] + (argv[3] << 7);
        // duration is currently limited to 16,383 ms
        duration = argv[4] + (argv[5] << 7);
        tone(pin, frequency, duration);
      }
      else if (toneCommand == TONE_NO_TONE) {
        noTone(pin);
      }
      break ;
#endif
    // arg0 = trigger pin
    // arg1 = echo pin
    // arg2 = timeout_lsb
    // arg3 = timeout_msb
    case SONAR_CONFIG :
      unsigned long timeout ;
      if ( numActiveSonars < MAX_SONARS)
      {
        sonarTriggerPin = argv[0] ;
        sonarEchoPin = argv[1] ;

        timeout = argv[2] + (argv[3] << 7 ) ;
        sonarPinNumbers[numActiveSonars] = sonarTriggerPin ;

        setPinModeCallback(sonarTriggerPin, PIN_MODE_SONAR);
        setPinModeCallback(sonarEchoPin, PIN_MODE_SONAR);
        sonars[numActiveSonars] = new Ultrasonic(sonarTriggerPin, sonarEchoPin, timeout) ;
        numActiveSonars++;
//        printData((char*)"arg = ", numActiveSonars) ;
      }
      else {
        strcpy_P(msgBuffer, (char *)pgm_read_word(&(string_table[6])));
        Firmata.sendString(msgBuffer);
//        Firmata.sendString("PING_CONFIG Err:Too many sonars");
      }
      break ;

    case STEPPER_DATA:
      // determine if this a STEPPER_CONFIGURE command or STEPPER_OPERATE command
      if (argv[0] == STEPPER_CONFIGURE)
      {
        int numSteps = argv[1] + (argv[2] << 7);
        int pin1 = argv[3] ;
        int pin2 = argv[4] ;
        if ( argc == 5 )
        {
          // two pin motor
          stepper = new Stepper(numSteps, pin1, pin2) ;
        }
        else if (argc == 7 ) // 4 wire motor
        {
          int pin3 = argv[5] ;
          int pin4 = argv[6] ;
          stepper =  new Stepper(numSteps, pin1, pin2, pin3, pin4) ;
        }
        else
        {
          strcpy_P(msgBuffer, (char *)pgm_read_word(&(string_table[7])));
          Firmata.sendString(msgBuffer);
//          Firmata.sendString("STEP CONFIG Err:Wrong Num of args");
//          printData((char*)"argc = ", argc) ;
        }
      }
      else if ( argv[0] == STEPPER_STEP )
      {
        long speed = (long)argv[1] | ((long)argv[2] << 7) | ((long)argv[3] << 14);
        int numSteps = argv[4] + (argv[5] << 7);
        int direction = argv[6] ;
        if (stepper != NULL )
        {
          stepper->setSpeed(speed) ;
          if (direction == 0 )
          {
            numSteps *= -1 ;
          }
          stepper->step(numSteps) ;
        }
        else
        {
          strcpy_P(msgBuffer, (char *)pgm_read_word(&(string_table[8])));
          Firmata.sendString(msgBuffer);
//          Firmata.sendString("STEP OP Err:MOT NOT CONFIGURED");
        }
      }
      else if ( argv[0] == STEPPER_LIBRARY_VERSION )
      {
        if ( stepper != NULL )
        {
          int version = stepper->version() ;
          Firmata.write(START_SYSEX);
          Firmata.write(STEPPER_DATA);
          Firmata.write(version & 0x7F);
          Firmata.write(version >> 7);
          Firmata.write(END_SYSEX);
        }
        else
        {
          // did not find a configured stepper
          strcpy_P(msgBuffer, (char *)pgm_read_word(&(string_table[9])));
          Firmata.sendString(msgBuffer);
//          Firmata.sendString("STEP FIRMWARE VER Err:NO MOT CONFIGURED");
        }
        break ;
      }
      else
      {
        strcpy_P(msgBuffer, (char *)pgm_read_word(&(string_table[10])));
        Firmata.sendString(msgBuffer);
//        Firmata.sendString("STEP CONFIG Err:UNKNOWN CMD");
      }
      break ;
    case DHT_CONFIG:
      int DHT_Pin = argv[0];
      int DHT_type = argv[1];

      if ( numActiveDHTs < MAX_DHTS)
      {
        if (DHT_type == 22)
        {
          DHT_WakeUpDelay[numActiveDHTs] = 1;
        }
        else if (DHT_type == 11)
        {
          DHT_WakeUpDelay[numActiveDHTs] = 18;
        }
        else
        {
          strcpy_P(msgBuffer, (char *)pgm_read_word(&(string_table[11])));
          Firmata.sendString(msgBuffer);
//          Firmata.sendString("ERR:UNKNOWN DHT TYPE");
          break;
        }
        // test the sensor
        DHT_PinNumbers[numActiveDHTs] = DHT_Pin ;
        DHT_TYPE[numActiveDHTs] = DHT_type;

        setPinModeCallback(DHT_Pin, PIN_MODE_DHT);
        int rv = readDhtSensor(numActiveDHTs);
        if (rv == DHTLIB_OK)
        {
          numActiveDHTs++ ;
          dhtNumLoops = dhtNumLoops / numActiveDHTs ;
          // all okay
        }
        else
        {
          // send the message back with an error status
          Firmata.write(START_SYSEX);
          Firmata.write(DHT_DATA) ;
          Firmata.write(DHT_Pin) ;
          Firmata.write(DHT_type) ;
          for (uint8_t i = 0; i < sizeof(_bits) - 1; ++i) {
            Firmata.write(_bits[i] & 0x7f);
            Firmata.write(_bits[i] >> 7 & 0x7f);
          }
          Firmata.write(abs(rv));
          Firmata.write(1);
          Firmata.write(END_SYSEX);
        }
        break ;
      }
      else {
        strcpy_P(msgBuffer, (char *)pgm_read_word(&(string_table[12])));
        Firmata.sendString(msgBuffer);
//        Firmata.sendString("DHT_CONFIG Err:Too many devices");
      }
      break;
  }
}

/*==============================================================================
   SETUP()
  ============================================================================*/

void systemResetCallback()
{
  isResetting = true;

  // initialize a defalt state
  // TODO: option to load config from EEPROM instead of default

#ifdef FIRMATA_SERIAL_FEATURE
  serialFeature.reset();
#endif
  
  if (isI2CEnabled) {
    disableI2CPins();
  }

  for (byte i = 0; i < TOTAL_PORTS; i++) {
    reportPINs[i] = false;    // by default, reporting off
    portConfigInputs[i] = 0;  // until activated
    previousPINs[i] = 0;
  }

  for (byte i = 0; i < TOTAL_PINS; i++) {
    // pins with analog capability default to analog input
    // otherwise, pins default to digital output
    if (IS_PIN_ANALOG(i)) {
      // turns off pullup, configures everything
      setPinModeCallback(i, PIN_MODE_ANALOG);
    }
#if defined(__AVR__)
    else if ( IS_PIN_TONE(i)) {
      noTone(i) ;
    }
#endif
    else {
      // sets the output to 0, configures portConfigInputs
      setPinModeCallback(i, OUTPUT);
    }

    servoPinMap[i] = 255;
  }
  // stop pinging
  numActiveSonars = 0 ;
  for (int i = 0; i < MAX_SONARS; i++) {
    sonarPinNumbers[i] = PIN_MODE_IGNORE ;
    if ( sonars[i] ) {
      sonars[i] = NULL ;
    }
  }
  numActiveSonars = 0 ;

  // by default, do not report any analog inputs
  analogInputsToReport = 0;

  detachedServoCount = 0;
  servoCount = 0;

  // stop pinging DHT
  numActiveDHTs = 0;

  if(PM25_started) pms.sleep();
  #ifndef HAVE_HWSERIAL1
    Serial1.end();
  #endif
  PM25_started = false;
  OLED_started = false;
  LCD_started = false;

  /* send digital inputs to set the initial state on the host computer,
    since once in the loop(), this firmware will only send on change */
  /*
    TODO: this can never execute, since no pins default to digital input
        but it will be needed when/if we support EEPROM stored config
    for (byte i=0; i < TOTAL_PORTS; i++) {
    outputPort(i, readPort(i, portConfigInputs[i]), true);
    }
  */
  isResetting = false;
}

void setup()
{
  Firmata.setFirmwareVersion(FIRMATA_FIRMWARE_MAJOR_VERSION, FIRMATA_FIRMWARE_MINOR_VERSION);

  Firmata.attach(ANALOG_MESSAGE, analogWriteCallback);
  Firmata.attach(DIGITAL_MESSAGE, digitalWriteCallback);
  Firmata.attach(REPORT_ANALOG, reportAnalogCallback);
  Firmata.attach(REPORT_DIGITAL, reportDigitalCallback);
  Firmata.attach(SET_PIN_MODE, setPinModeCallback);
  Firmata.attach(SET_DIGITAL_PIN_VALUE, setPinValueCallback);
  Firmata.attach(START_SYSEX, sysexCallback);
  Firmata.attach(SYSTEM_RESET, systemResetCallback);

  // to use a port other than Serial, such as Serial1 on an Arduino Leonardo or Mega,
  // Call begin(baud) on the alternate serial port and pass it to Firmata to begin like this:
  // Serial1.begin(115200);
  // Firmata.begin(Serial1);
  // However do not do this if you are using SERIAL_MESSAGE
  Serial1.begin(9600);
//  while (!Serial1) {
//    ; // wait for softwareserial port to connect. Needed for ATmega32u4-based boards and Arduino 101
//  }
  Firmata.begin(115200);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for ATmega32u4-based boards and Arduino 101
  }
  #ifndef HAVE_HWSERIAL1
    Serial1.end();
  #endif
  systemResetCallback();  // reset to default config
}

/*==============================================================================
   LOOP()
  ============================================================================*/
void loop()
{
  byte pin, analogPin;

  /* DIGITALREAD - as fast as possible, check for changes and output them to the
     FTDI buffer using Serial.print()  */
  checkDigitalInputs();

  /* STREAMREAD - processing incoming messagse as soon as possible, while still
     checking digital inputs.  */
  while (Firmata.available())
    Firmata.processInput();

  // TODO - ensure that Stream buffer doesn't go over 60 bytes

  currentMillis = millis();
  if (currentMillis - previousMillis > samplingInterval) {
    previousMillis += samplingInterval;

    if ( pingLoopCounter++ > numLoops)
    {
      pingLoopCounter = 0 ;
      if (numActiveSonars)
      {
        unsigned int distance = sonars[nextSonar]->read();
        currentSonar = nextSonar ;
        if ( nextSonar++ >= numActiveSonars - 1)
        {
          nextSonar = 0 ;
        }
        byte sonarLSB = distance & 0x7f ;
        byte sonarMSB = distance >> 7 & 0x7f ;
        
        Firmata.write(START_SYSEX);
        Firmata.write(SONAR_DATA) ;
        Firmata.write(sonarPinNumbers[currentSonar]) ;
        Firmata.write(sonarLSB) ;
        Firmata.write(sonarMSB) ;
        Firmata.write(END_SYSEX);

      }
    }

    if ( dhtLoopCounter++ > dhtNumLoops)
    {
      if (numActiveDHTs)
      {
        int rv = readDhtSensor(nextDHT);

        uint8_t current_pin = DHT_PinNumbers[nextDHT] ;
        uint8_t current_type = DHT_TYPE[nextDHT] ;
        dhtLoopCounter = 0 ;
        currentDHT = nextDHT ;
        if ( nextDHT++ >= numActiveDHTs - 1)
        {
          nextDHT = 0 ;
        }

        if (rv == DHTLIB_OK) {
          // TEST CHECKSUM
          uint8_t sum = _bits[0] + _bits[1] + _bits[2] + _bits[3];
          if (_bits[4] != sum)
          {
            rv = -1;
          }
        }
        // send the message back with an error status
        Firmata.write(START_SYSEX);
        Firmata.write(DHT_DATA) ;
        Firmata.write(current_pin) ;
        Firmata.write(current_type) ;
        for (uint8_t i = 0; i < sizeof(_bits) - 1; ++i) {
          Firmata.write(_bits[i] );
         // Firmata.write(_bits[i] ;
        }
        Firmata.write(abs(rv));
        Firmata.write(0);
        Firmata.write(END_SYSEX);
      }
    }

    if ( pm25LoopCounter++ > pm25NumLoops)
    {
      if(PM25_started)
      {
//        pm25LoopCounter = 0 ; 
        if(pms.read(pms_data))
        {
          byte pm25LSB, pm25MSB;
          byte pm10LSB, pm10MSB;
//          firstPM25 = 1;
          pm25LSB = pms_data.PM_AE_UG_2_5 & 0x7f;
          pm25MSB = pms_data.PM_AE_UG_2_5 >> 7 & 0x7f;
          pm10LSB = pms_data.PM_AE_UG_10_0 & 0x7f;
          pm10MSB = pms_data.PM_AE_UG_10_0 >> 7 & 0x7f;
          Firmata.write(START_SYSEX);
          Firmata.write(PM25_DATA);
          Firmata.write(pm25LSB);
          Firmata.write(pm25MSB);
          Firmata.write(pm10LSB);
          Firmata.write(pm10MSB);
          Firmata.write(0); //No problem
          Firmata.write(END_SYSEX);
        }
//        else if(firstPM25 == 0)
//        {
//          pms.wakeUp();
//        }
      }
    }
//      if (PM25_started)
//      {
//        pm25LoopCounter = 0 ;
//        if(pms.read(pms_data))
//        {
//          pm25LSB = pms_data.PM_AE_UG_2_5 & 0x7f;
//          pm25MSB = pms_data.PM_AE_UG_2_5 >> 7 & 0x7f;
//          pm10LSB = pms_data.PM_AE_UG_10_0 & 0x7f;
//          pm10MSB = pms_data.PM_AE_UG_10_0 >> 7 & 0x7f;
//          Firmata.write(START_SYSEX);
//          Firmata.write(PM25_DATA);
//          Firmata.write(pm25LSB);
//          Firmata.write(pm25MSB);
//          Firmata.write(pm10LSB);
//          Firmata.write(pm10MSB);
//          Firmata.write(0); //No problem
//          Firmata.write(END_SYSEX);
////          printData((char*)"LSB:", pm25LSB);
////          printData((char*)"MSB:", pm25MSB);
////          printData((char*)"LSB:", pm25LSB);
////          printData((char*)"MSB:", pm25MSB);
//        }
//        else
//        {
          //Send back message with error, where pm25 = 999 and pm10 = 888
//          Firmata.write(START_SYSEX);
//          Firmata.write(PM25_DATA);
//          Firmata.write(103);
//          Firmata.write(7);
//          Firmata.write(120);
//          Firmata.write(6);
//          Firmata.write(1); //Error
//          Firmata.write(END_SYSEX);
//          pms.wakeUp();
//        }
//      }
//      else
//      {
//        Firmata.sendString("PM25_DATA Error! Sensor not started yet!");
//      }
//    }

//    if ( IRLoopCounter++ > IRNumLoops)
//    {
//      if (IR_started)
//      {
//        IRLoopCounter = 0;
//        if (IR_interruptFlag)
//        {
//          noInterrupts();
//        }
//        else
//        {
//          interrupts();
//        }
//        if (IrReceiver.decode()) {
//          byte IR_LSB = IrReceiver.decodedIRData.command & 0x7f;
//          byte IR_MSB = IrReceiver.decodedIRData.command >> 7 & 0x7f;
//          byte IR_ADDRESS_LSB = IrReceiver.decodedIRData.address & 0x7f;
//          byte IR_ADDRESS_MSB = IrReceiver.decodedIRData.address >> 7 & 0x7f;
//          Firmata.write(START_SYSEX);
//          Firmata.write(IR_DATA);
//          Firmata.write(IR_ADDRESS_LSB);
//          Firmata.write(IR_ADDRESS_MSB);
//          Firmata.write(IR_LSB);
//          Firmata.write(IR_MSB);
//          Firmata.write(END_SYSEX);
//          Firmata.sendString("sent");
//          IrReceiver.resume();
//      }
//    }
          
    /* ANALOGREAD - do all analogReads() at the configured sampling interval */
    for (pin = 0; pin < TOTAL_PINS; pin++) {
      if (IS_PIN_ANALOG(pin) && Firmata.getPinMode(pin) == PIN_MODE_ANALOG) {
        analogPin = PIN_TO_ANALOG(pin);
        if (analogInputsToReport & (1 << analogPin)) {
          Firmata.sendAnalog(analogPin, analogRead(analogPin));
        }
      }
    }
    // report i2c data for all device with read continuous mode enabled
    if (queryIndex > -1) {
      for (byte i = 0; i < queryIndex + 1; i++) {
        readAndReportData(query[i].addr, query[i].reg, query[i].bytes, query[i].stopTX);
      }
    }

#if defined(__AVR__)
    if ( keepAliveInterval ) {
      currentMillis = millis();
      if (currentMillis - previousKeepAliveMillis > keepAliveInterval * 1000) {
        systemResetCallback();
        wdt_enable(WDTO_15MS);
        // systemResetCallback();
        while (1)
          ;
      }
    }
#endif
  }

#ifdef FIRMATA_SERIAL_FEATURE
  serialFeature.update();
#endif
}

void printData(char * id,  long data)
{
  char myArray[64] ;

  String myString = String(data);
  myString.toCharArray(myArray, 64) ;
  Firmata.sendString(id) ;
  Firmata.sendString(myArray);
}

// The following function i staken from https://github.com/RobTillaart/Arduino/tree/master/libraries/DHTNEW
// Thanks to the original authors
int readDhtSensor(int index)
{
  // INIT BUFFERVAR TO RECEIVE DATA
  uint8_t mask = 128;
  uint8_t idx = 0;

  // EMPTY BUFFER
  //  memset(_bits, 0, sizeof(_bits));
  for (uint8_t i = 0; i < 5; i++) _bits[i] = 0;
  uint8_t pin = DHT_PinNumbers[index] ;
  uint8_t wakeupDelay = DHT_WakeUpDelay[index] ;

  // REQUEST SAMPLE
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
  delay(wakeupDelay);
  pinMode(pin, INPUT);
  delayMicroseconds(40);

  // GET ACKNOWLEDGE or TIMEOUT
  uint16_t loopCnt = DHTLIB_TIMEOUT;
  while (digitalRead(pin) == LOW)
  {
    if (--loopCnt == 0) return DHTLIB_ERROR_TIMEOUT;
  }

  loopCnt = DHTLIB_TIMEOUT;
  while (digitalRead(pin) == HIGH)
  {
    if (--loopCnt == 0) return DHTLIB_ERROR_TIMEOUT;
  }

  // READ THE OUTPUT - 40 BITS => 5 BYTES
  for (uint8_t i = 40; i != 0; i--)
  {
    loopCnt = DHTLIB_TIMEOUT;
    while (digitalRead(pin) == LOW)
    {
      if (--loopCnt == 0) return DHTLIB_ERROR_TIMEOUT;
    }

    uint32_t t = micros();

    loopCnt = DHTLIB_TIMEOUT;
    while (digitalRead(pin) == HIGH)
    {
      if (--loopCnt == 0) return DHTLIB_ERROR_TIMEOUT;
    }

    if ((micros() - t) > 40)
    {
      _bits[idx] |= mask;
    }
    mask >>= 1;
    if (mask == 0)   // next byte?
    {
      mask = 128;
      idx++;
    }
  }
  return DHTLIB_OK;
}

void handleReceivedTinyIRData(uint16_t aAddress, uint8_t aCommand, bool isRepeat)
{
  byte IR_ADDRESS_LSB = aAddress & 0x7f;
  byte IR_ADDRESS_MSB = aAddress >> 7 & 0x7f;
  byte IR_LSB = aCommand & 0x7f;
  byte IR_MSB = aCommand >> 7 & 0x7f;
  Firmata.write(START_SYSEX);
  Firmata.write(IR_DATA);
  Firmata.write(IR_ADDRESS_LSB);
  Firmata.write(IR_ADDRESS_MSB);
  Firmata.write(IR_LSB);
  Firmata.write(IR_MSB);
  Firmata.write(END_SYSEX);
}
