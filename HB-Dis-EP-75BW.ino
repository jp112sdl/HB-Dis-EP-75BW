//- -----------------------------------------------------------------------------------------------------------------------
// AskSin++
// 2016-10-31 papa Creative Commons - http://creativecommons.org/licenses/by-nc-sa/3.0/de/
// 2019-07-16 jp112sdl Creative Commons - http://creativecommons.org/licenses/by-nc-sa/3.0/de/
//- -----------------------------------------------------------------------------------------------------------------------

// use Arduino IDE Board Setting: STANDARD Layout

// define this to read the device id, serial and device type from bootloader section
// #define USE_OTA_BOOTLOADER
// #define USE_HW_SERIAL
// #define NDEBUG
// #define NDISPLAY

#define BATTERY_MODE

#ifdef BATTERY_MODE
#define USE_WOR
#endif





//////////////////// DISPLAY DEFINITIONS /////////////////////////////////////
#include <GxEPD.h>
#include <GxGDEW075T8/GxGDEW075T8.h>        // 7.5" b/w
#include <GxIO/GxIO_SPI/GxIO_SPI.h>
#include <GxIO/GxIO.h>

#define GxRST_PIN  14
#define GxBUSY_PIN 11
#define GxDC_PIN   12
#define GxCS_PIN   17

GxIO_Class io(SPI, GxCS_PIN, GxDC_PIN, GxRST_PIN);
GxEPD_Class display(io, GxRST_PIN, GxBUSY_PIN);

#include <Adafruit_GFX.h>
#include <U8g2_for_Adafruit_GFX.h>
U8G2_FOR_ADAFRUIT_GFX u8g2Fonts;
//////////////////////////////////////////////////////////////////////////////

#define EI_NOTEXTERNAL
#include <EnableInterrupt.h>

#include <AskSinPP.h>
#include <LowPower.h>

#include <Register.h>
#include <Remote.h>
#include <MultiChannelDevice.h>

#define CC1101_CS_PIN       4
#define CC1101_GDO0_PIN     2
#define CC1101_SCK_PIN      7
#define CC1101_MOSI_PIN     5
#define CC1101_MISO_PIN     6
#define CONFIG_BUTTON_PIN  15
#define LED_PIN_1          10
#define LED_PIN_2          13
#define BTN1_PIN           A7
#define BTN2_PIN           A6
#define BTN3_PIN           A5
#define BTN4_PIN           A4
#define BTN5_PIN           A3
#define BTN6_PIN           A2
#define BTN7_PIN           A1
#define BTN8_PIN           A0

#define TEXT_LENGTH        16
#define DISPLAY_LINES      36
#define COLUMN_WIDTH       212
#define ICON_ROWS          6
#define TEXT_ROWS          (ICON_ROWS * 2)
#define ICON_HEIGHT        56
#define ICON_WIDTH         ICON_HEIGHT
#define ICON_MARGIN        4
#define ICON_COL_WIDTH     ICON_WIDTH+ICON_MARGIN
#define TEXT_COL_WIDTH     150
#define PADDING            3
#define LINE_HEIGHT        (ICON_HEIGHT + (2 * ICON_MARGIN)) / 2

#define FONT_REGULAR       u8g2_font_helvR14_tf
#define FONT_BOLD          u8g2_font_helvB14_tf
#define FONT_HEIGHT        14

#define DISPLAY_ROTATE      0 // 0 = 0° , 1 = 90°, 2 = 180°, 3 = 270°

#define PEERS_PER_CHANNEL   8
#define NUM_CHANNELS        9
#define DEF_LOWBAT_VOLTAGE  24
#define DEF_CRITBAT_VOLTAGE 22

#define MSG_START_KEY       0x02
#define MSG_TEXT_KEY_NORMAL 0x11
#define MSG_TEXT_KEY_BOLD   0x12
#define MSG_ICON_KEY        0x18
#define MSG_CLR_KEY         0xFE
#define MSG_MIN_LENGTH         3
#define MSG_BUFFER_LENGTH    288

#include "Icons.h"

// all library classes are placed in the namespace 'as'
using namespace as;

const struct DeviceInfo PROGMEM devinfo = {
  {0xf3, 0x47, 0x00},          // Device ID
  "JPDISEP750",                // Device Serial
#ifdef BATTERY_MODE
  {0xf3, 0x47},                // Device Model
#else
  {0xf3, 0x57},                // Device Model
#endif
  0x10,                        // Firmware Version
  as::DeviceType::Remote,      // Device Type
  {0x01, 0x01}                 // Info Bytes
};


typedef struct {
  String Text       = "";
  bool   Bold       = false;
  bool   Center     = false;
} DisplayLine;
DisplayLine DisplayLines[DISPLAY_LINES];

typedef struct {
  uint8_t Num      = 0xff;
  bool    Right    = false;
} IconColumn;
IconColumn IconColumns[DISPLAY_LINES / 2];

String List1Texts[(NUM_CHANNELS - 1) * 4];

bool runSetup          = true;

/**
   Configure the used hardware
*/
typedef AvrSPI<CC1101_CS_PIN, CC1101_MOSI_PIN, CC1101_MISO_PIN, CC1101_SCK_PIN> SPIType;
typedef Radio<SPIType, CC1101_GDO0_PIN> RadioType;
typedef StatusLed<LED_PIN_1> LedType;

#ifdef BATTERY_MODE
typedef AskSin<LedType, BatterySensor, RadioType> BaseHal;
#else
typedef AskSin<LedType, NoBattery, RadioType> BaseHal;
#endif

class Hal: public BaseHal {
  public:
    void init(const HMID& id) {
      BaseHal::init(id);
      radio.initReg(CC1101_FREQ2, 0x21);
      radio.initReg(CC1101_FREQ1, 0x66);
      radio.initReg(CC1101_FREQ0, 0x0A);
#ifdef BATTERY_MODE
      battery.init(seconds2ticks(60UL * 60 * 21), sysclock); //battery measure once an day
      battery.low(DEF_LOWBAT_VOLTAGE);
      battery.critical(DEF_CRITBAT_VOLTAGE);
      activity.stayAwake(seconds2ticks(15));
#endif
    }

    bool runready () {
      return sysclock.runready() || BaseHal::runready();
    }
} hal;


void initDisplay();
void updateDisplay();
void emptyBatteryDisplay();
class ePaperType : public Alarm {
    class ePaperWorkingLedType : public StatusLed<LED_PIN_2>  {
      private:
        bool enabled;
      public:
        ePaperWorkingLedType () : enabled(true) {}
        virtual ~ePaperWorkingLedType () {}
        void Enabled(bool e) {
          enabled = e;
        }
        bool Enabled() {
          return enabled;
        }
    } workingLed;
  private:
    bool                 mUpdateDisplay, shInitDisplay, inverted, waiting, showgrid;
    uint16_t             clFG, clBG;
  public:
    ePaperType () :  Alarm(0), mUpdateDisplay(false), shInitDisplay(false), inverted(false), waiting(false), showgrid(false), clFG(GxEPD_BLACK), clBG(GxEPD_WHITE)  {}
    virtual ~ePaperType () {}

    uint16_t ForegroundColor() {
      return clFG;
    }

    void ForegroundColor(uint16_t c) {
      clFG = c;
    }

    uint16_t BackgroundColor() {
      return clBG;
    }

    void BackgroundColor(uint16_t c) {
      clBG = c;
    }

    bool showGrid() {
      return showgrid;
    }

    void showGrid(bool sg) {
      showgrid = sg;
    }

    bool Inverted() {
      return inverted;
    }

    void Inverted(bool i) {
      inverted = i;
    }

    bool showInitDisplay() {
      return shInitDisplay;
    }

    void showInitDisplay(bool s) {
      shInitDisplay = s;
    }

    bool mustUpdateDisplay() {
      return mUpdateDisplay;
    }

    void mustUpdateDisplay(bool m) {
      if (m == true && workingLed.Enabled() == true) workingLed.set(LedStates::pairing);
      mUpdateDisplay = m;
    }

    void init() {
      u8g2Fonts.begin(display);
      display.setRotation(DISPLAY_ROTATE);
      u8g2Fonts.setFontMode(1);
      u8g2Fonts.setFontDirection(0);
      workingLed.init();
    }

    void setWorkingLedEnabled(bool en) {
      workingLed.Enabled(en);
    }

    void setDisplayColors() {
      u8g2Fonts.setForegroundColor(ForegroundColor());
      u8g2Fonts.setBackgroundColor(BackgroundColor());
    }

    void isWaiting(bool w) {
      waiting = w;
      DPRINT("wait:"); DDECLN(waiting);
    }

    bool isWaiting() {
      return waiting;
    }

    void setRefreshAlarm (uint32_t t) {
      isWaiting(true);
      sysclock.cancel(*this);
      Alarm::set(millis2ticks(t));
      sysclock.add(*this);
    }
    virtual void trigger (__attribute__((unused)) AlarmClock& clock) {
      isWaiting(false);
      if (this->mustUpdateDisplay()) {
        this->mustUpdateDisplay(false);
#ifndef NDISPLAY
        if (workingLed.Enabled() == true) {
          workingLed.set(LedStates::nothing);
          workingLed.ledOn();
        }
        setDisplayColors();
        if (this->showInitDisplay() == true) {
          this->showInitDisplay(false);
          display.drawPaged(initDisplay);
        } else {
          display.drawPaged(updateDisplay);
        }

        workingLed.ledOff();
#else
        DPRINTLN("UPDATEDISPLAY!");
#endif
      }
    }
} ePaper;

DEFREGISTER(Reg0, MASTERID_REGS, DREG_TRANSMITTRYMAX, DREG_LEDMODE, DREG_LOWBATLIMIT, 0x06, 0x07, 0x34, 0x35, 0x36, 0x90)
class DispList0 : public RegList0<Reg0> {
  public:
    DispList0(uint16_t addr) : RegList0<Reg0>(addr) {}

    bool displayInvertingHb(bool v) const {
      return this->writeRegister(0x06, 0x01, 0, v);
    }
    bool displayInvertingHb() const {
      return this->readRegister(0x06, 0x01, 0, false);
    }

    uint8_t displayRefreshWaitTime () const {
      return this->readRegister(0x07, 0);
    }
    bool displayRefreshWaitTime (uint8_t value) const {
      return this->writeRegister(0x07, value);
    }

    uint8_t powerUpMode () const {
      return this->readRegister(0x34, 0x03, 0);
    }
    bool powerUpMode (uint8_t value) const {
      return this->writeRegister(0x34, 0x03, 0, value);
    }

    uint8_t powerUpKey () const {
      return this->readRegister(0x34, 0x0f, 2);
    }
    bool powerUpKey (uint8_t value) const {
      return this->writeRegister(0x34, 0x0f, 2, value);
    }

    uint8_t critBatLimit () const {
      return this->readRegister(0x35, 0);
    }
    bool critBatLimit (uint8_t value) const {
      return this->writeRegister(0x35, value);
    }

    bool showGrid (uint8_t value) const {
      return this->writeRegister(0x90, 0x01, 0, value & 0xff);
    }
    bool showGrid () const {
      return this->readRegister(0x90, 0x01, 0, false);
    }

    void defaults () {
      clear();
      displayInvertingHb(false);
      ledMode(1);
      transmitDevTryMax(2);
      displayRefreshWaitTime(50);
      powerUpMode(0);
      powerUpKey(0);
      showGrid(false);
#ifdef BATTERY_MODE
      lowBatLimit(DEF_LOWBAT_VOLTAGE);
      critBatLimit(DEF_CRITBAT_VOLTAGE);
#endif
    }
};

DEFREGISTER(DispReg1)
class DispList1 : public RegList1<DispReg1> {
  public:
    DispList1 (uint16_t addr) : RegList1<DispReg1>(addr) {}
    void defaults () {
      clear();
    }
};

DEFREGISTER(RemReg1, CREG_AES_ACTIVE, CREG_LONGPRESSTIME , CREG_DOUBLEPRESSTIME, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f, 0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75)
class RemList1 : public RegList1<RemReg1> {
  public:
    RemList1 (uint16_t addr) : RegList1<RemReg1>(addr) {}

    bool TEXT1 (uint8_t value[TEXT_LENGTH]) const {
      for (int i = 0; i < TEXT_LENGTH; i++) {
        this->writeRegister(0x36 + i, value[i] & 0xff);
      }
      return true;
    }
    String TEXT1 () const {
      String a = "";
      for (int i = 0; i < TEXT_LENGTH; i++) {
        byte b = this->readRegister(0x36 + i, 0x20);
        if (b == 0x00) b = 0x20;
        a += char(b);
      }
      return a;
    }

    bool TEXT2 (uint8_t value[TEXT_LENGTH]) const {
      for (int i = 0; i < TEXT_LENGTH; i++) {
        this->writeRegister(0x46 + i, value[i] & 0xff);
      }
      return true;
    }
    String TEXT2 () const {
      String a = "";
      for (int i = 0; i < TEXT_LENGTH; i++) {
        byte b = this->readRegister(0x46 + i, 0x20);
        if (b == 0x00) b = 0x20;
        a += char(b);
      }
      return a;
    }

    bool TEXT3 (uint8_t value[TEXT_LENGTH]) const {
      for (int i = 0; i < TEXT_LENGTH; i++) {
        this->writeRegister(0x56 + i, value[i] & 0xff);
      }
      return true;
    }
    String TEXT3 () const {
      String a = "";
      for (int i = 0; i < TEXT_LENGTH; i++) {
        byte b = this->readRegister(0x56 + i, 0x20);
        if (b == 0x00) b = 0x20;
        a += char(b);
      }
      return a;
    }

    bool TEXT4 (uint8_t value[TEXT_LENGTH]) const {
      for (int i = 0; i < TEXT_LENGTH; i++) {
        this->writeRegister(0x66 + i, value[i] & 0xff);
      }
      return true;
    }
    String TEXT4 () const {
      String a = "";
      for (int i = 0; i < TEXT_LENGTH; i++) {
        byte b = this->readRegister(0x66 + i, 0x20);
        if (b == 0x00) b = 0x20;
        a += char(b);
      }
      return a;
    }

    void defaults () {
      clear();
      //aesActive(false);
      longPressTime(1);
      doublePressTime(0);
      uint8_t initValues[TEXT_LENGTH];
      memset(initValues, 0x00, TEXT_LENGTH);
      TEXT1(initValues);
      TEXT2(initValues);
      TEXT3(initValues);
      TEXT4(initValues);
    }
};

class RemChannel : public RemoteChannel<Hal, PEERS_PER_CHANNEL, DispList0, RemList1>  {
  public:
    RemChannel () : RemoteChannel() {}
    virtual ~RemChannel () {}
    void configChanged() {
      uint16_t _longpressTime = 300 + (this->getList1().longPressTime() * 100);
      setLongPressTime(millis2ticks(_longpressTime));

      List1Texts[(number() - 1)  * 4] = this->getList1().TEXT1();
      List1Texts[((number() - 1) * 4) + 1] = this->getList1().TEXT2();
      List1Texts[((number() - 1) * 4) + 2] = this->getList1().TEXT3();
      List1Texts[((number() - 1) * 4) + 3] = this->getList1().TEXT4();

      DPRINT(number() < 10 ? "0" : ""); DDEC(number()); DPRINT(F(" - TEXT1 = ")); DPRINT(this->getList1().TEXT1()); DPRINT(F(" - TEXT2 = ")); DPRINT(this->getList1().TEXT2()); DPRINT(F(" - TEXT3 = ")); DPRINT(this->getList1().TEXT3()); DPRINT(F(" - TEXT4 = ")); DPRINTLN(this->getList1().TEXT4());

      //if (!runSetup) ePaper.mustUpdateDisplay(true);
    }

    uint8_t flags () const {
      return hal.battery.low() ? 0x80 : 0x00;
    }
};

class DispChannel : public RemoteChannel<Hal, PEERS_PER_CHANNEL, DispList0, DispList1>  {
  private:
    uint8_t       msgBufferIdx;
    uint8_t       msgBuffer[MSG_BUFFER_LENGTH];
  public:
    DispChannel () : RemoteChannel(), msgBufferIdx(0) {}
    virtual ~DispChannel () {}

    void configChanged() { }

    uint8_t resetMessageBuffer() {
      //DPRINTLN("reset msgBuffer");
      msgBufferIdx = 0;
      memset(msgBuffer, 0, sizeof(msgBuffer));
      return msgBufferIdx;
    }

    bool process (const ActionCommandMsg& msg) {
      static bool getText = false;
      static uint8_t lineNumber = 0;

      String Text = "";
      for (int i = 0; i < msg.len(); i++) {
        if (msg.value(i) == MSG_START_KEY) {
          lineNumber = resetMessageBuffer();
        }

        if (msgBufferIdx < MSG_BUFFER_LENGTH) {
          msgBuffer[msgBufferIdx] = msg.value(i);
          msgBufferIdx++;
        } else {
          lineNumber = resetMessageBuffer();
        }
      }

      if (
        msg.eot(AS_ACTION_COMMAND_EOT) &&
        msgBufferIdx >= MSG_MIN_LENGTH &&
        msgBuffer[0] == MSG_START_KEY
      ) {
        DPRINT("RECV: ");
        for (int i = 0; i < msgBufferIdx; i++) {
          DHEX(msgBuffer[i]); DPRINT(" ");

          if (msgBuffer[i] == AS_ACTION_COMMAND_EOT || msgBuffer[i] == MSG_TEXT_KEY_NORMAL || msgBuffer[i] == MSG_TEXT_KEY_BOLD || msgBuffer[i] == MSG_ICON_KEY) {
            if (Text != "") DisplayLines[lineNumber].Text = Text;
            //DPRINT("EOL DETECTED. currentLine = ");DDECLN(currentLine);
            Text = "";
            getText = false;
          }

          if (getText == true) {
            if ((msgBuffer[i] >= 0x20 && msgBuffer[i] < 0x80) || msgBuffer[i] == 0xb0 ) {
              char c = msgBuffer[i];
              Text += c;
            } else if (msgBuffer[i] >= 0x80 && msgBuffer[i] < 0x80 + 0x20) {
              uint8_t textNum = msgBuffer[i] - 0x80;
              String fixText = List1Texts[textNum];
              fixText.trim();
              Text += fixText;
              //DPRINTLN(""); DPRINT("USE PRECONF TEXT NUMBER "); DDEC(textNum); DPRINT(" = "); DPRINTLN(List1Texts[textNum]);
            }
          }

          if (msgBuffer[i] == MSG_TEXT_KEY_NORMAL || msgBuffer[i] == MSG_TEXT_KEY_BOLD) {
            bool bold = msgBuffer[i] == MSG_TEXT_KEY_BOLD;
            i++;
            bool center = false;
            if (msgBuffer[i] & 0x40) {
              msgBuffer[i] -= 0x40;
              center = true;
            }
            lineNumber = msgBuffer[i] - 0x80;
            DisplayLines[lineNumber].Center = center;
            DisplayLines[lineNumber].Bold = bold;

            if (msgBuffer[i + 1] == MSG_CLR_KEY) {
              DisplayLines[lineNumber].Text = "";
            } else {
              getText = true;
            }
          }

          if (msgBuffer[i] ==  MSG_ICON_KEY) {
            getText = false;
            i++;
            if (msgBuffer[i] >= 0x80) {
              bool right = false;
              if (msgBuffer[i] & 0x40) {
                msgBuffer[i] -= 0x40;
                right = true;
              }
              uint8_t iconPos = msgBuffer[i] - 0x80;
              i++;
              if (msgBuffer[i] == MSG_CLR_KEY) {
                IconColumns[iconPos].Num = 0xFF;
                IconColumns[iconPos].Right = false;
              } else {
                IconColumns[iconPos].Num = msgBuffer[i] - 0x80;
                IconColumns[iconPos].Right = right;
              }
            }
          }

        }

        DPRINTLN("");

        static uint8_t lastmsgcnt = 0xff;
        if (lastmsgcnt != msg.count()) {
          /*for (int i = 0; i < DISPLAY_LINES; i++) {
            DPRINT("LINE ");
            DDEC(i + 1);
            DPRINT(" BOLD = ");
            DDEC(DisplayLines[i].Bold);
            DPRINT(" CENT = ");
            DDEC(DisplayLines[i].Center);
            DPRINT(" TEXT = ");
            DPRINTLN(DisplayLines[i].Text);
            }*/
          /*for (int i = 0; i < DISPLAY_LINES / 2; i++) {
            DPRINT(" ICON ");
            DDEC(i + 1);
            DPRINT(": ");
            DDEC(IconColumns[i].Num);
            DPRINT(", RIGHT = ");
            DDECLN(IconColumns[i].Right);
            }*/
        }
        lastmsgcnt = msg.count();
        ePaper.mustUpdateDisplay(true);
      }
      return true;
    }

    bool process (const Message& msg) {
      return process(msg);
    }

    bool process (const RemoteEventMsg& msg) {
      return process(msg);
    }

    uint8_t flags () const {
      return hal.battery.low() ? 0x80 : 0x00;
    }
};

class DisplayDevice : public ChannelDevice<Hal, VirtBaseChannel<Hal, DispList0>, NUM_CHANNELS, DispList0> {
  public:
    VirtChannel<Hal, RemChannel,  DispList0> c[NUM_CHANNELS - 1];
    VirtChannel<Hal, DispChannel, DispList0> d;
  public:
    typedef ChannelDevice<Hal, VirtBaseChannel<Hal, DispList0>, NUM_CHANNELS, DispList0> DeviceType;
    DisplayDevice (const DeviceInfo& info, uint16_t addr) : DeviceType(info, addr) {
      for (uint8_t i = 0; i < NUM_CHANNELS - 1; i++) {
        DeviceType::registerChannel(c[i], i + 1);
      }
      DeviceType::registerChannel(d, NUM_CHANNELS);
    }
    virtual ~DisplayDevice () {}

    RemChannel& remChannel (uint8_t num)  {
      return c[num - 1];
    }

    DispChannel& dispChannel ()  {
      return d;
    }

    bool process(Message& msg) {
      HMID devid;
      this->getDeviceID(devid);
      if (msg.to() == devid) {
        uint16_t rtime = this->getList0().displayRefreshWaitTime() * 100;
        ePaper.setRefreshAlarm(rtime);
      }
      return ChannelDevice::process(msg);
    }

    virtual void configChanged () {
      DPRINTLN(F("CONFIG LIST0 CHANGED"));

#ifdef BATTERY_MODE
      uint8_t lowbat = getList0().lowBatLimit();
      uint8_t critbat = getList0().critBatLimit();
      if ( lowbat > 0 ) battery().low(lowbat);
      if ( critbat > 0 ) battery().critical(critbat);
      DPRINT(F("lowBat          : ")); DDECLN(lowbat);
      DPRINT(F("critBat         : ")); DDECLN(critbat);
#endif

      uint8_t ledmode = this->getList0().ledMode();
      DPRINT(F("ledMode         : ")); DDECLN(ledmode);
      ePaper.setWorkingLedEnabled(ledmode);

      if (this->getList0().displayInvertingHb()) {
        ePaper.ForegroundColor(GxEPD_WHITE);
        ePaper.BackgroundColor(GxEPD_BLACK);
      } else {
        ePaper.ForegroundColor(GxEPD_BLACK);
        ePaper.BackgroundColor(GxEPD_WHITE);
      }
      bool invertChanged = (ePaper.Inverted() != this->getList0().displayInvertingHb());
      bool showGridChanged = (ePaper.showGrid() != this->getList0().showGrid());
      ePaper.Inverted(this->getList0().displayInvertingHb());
      ePaper.showGrid(this->getList0().showGrid());
      
      DPRINT(F("displayInverting: ")); DDECLN(this->getList0().displayInvertingHb());
      DPRINT(F("RefreshWaitTime : ")); DDECLN(this->getList0().displayRefreshWaitTime());
      DPRINT(F("PowerUpMode     : ")); DDECLN(this->getList0().powerUpMode());
      DPRINT(F("PowerUpKey      : ")); DDECLN(this->getList0().powerUpKey());
      DPRINT(F("ShowGrid        : ")); DDECLN(this->getList0().showGrid());

      if (this->getList0().masterid().valid() == false || runSetup == true) {
        if (this->getList0().powerUpMode() == 0) {
          ePaper.showInitDisplay(true);
          ePaper.mustUpdateDisplay(true);
          ePaper.setRefreshAlarm(20);
        }
      }

      if (!runSetup && (invertChanged || showGridChanged)) ePaper.mustUpdateDisplay(true);
    }
};
DisplayDevice sdev(devinfo, 0x20);
class ConfBtn : public ConfigButton<DisplayDevice>  {
  public:
    ConfBtn (DisplayDevice& i) : ConfigButton(i)  {}
    virtual ~ConfBtn () {}

    virtual void state (uint8_t s) {
      if ( s == ButtonType::longreleased ) {
        ePaper.mustUpdateDisplay(true);
        ePaper.setRefreshAlarm(20);
      }
      ConfigButton::state(s);
    }
};
ConfBtn cfgBtn(sdev);

void setup () {
  runSetup = true;
  for (int i = 0; i < DISPLAY_LINES; i++)
    DisplayLines[i].Text = "";

#ifndef NDISPLAY
  display.init(57600);
#endif

  DINIT(57600, ASKSIN_PLUS_PLUS_IDENTIFIER);
  sdev.init(hal);
  remoteChannelISR(sdev.remChannel(1), BTN1_PIN);
  remoteChannelISR(sdev.remChannel(2), BTN2_PIN);
  remoteChannelISR(sdev.remChannel(3), BTN3_PIN);
  remoteChannelISR(sdev.remChannel(4), BTN4_PIN);
  remoteChannelISR(sdev.remChannel(5), BTN5_PIN);
  remoteChannelISR(sdev.remChannel(6), BTN6_PIN);
  remoteChannelISR(sdev.remChannel(7), BTN7_PIN);
  remoteChannelISR(sdev.remChannel(8), BTN8_PIN);

  buttonISR(cfgBtn, CONFIG_BUTTON_PIN);
  sdev.initDone();
  DDEVINFO(sdev);

#ifndef NDISPLAY
  ePaper.init();
#endif

  uint8_t powerupmode = sdev.getList0().powerUpMode();
  uint8_t powerupkey  = sdev.getList0().powerUpKey();
  if (powerupmode > 0) {
    sdev.remChannel(powerupkey + 1).state(powerupmode == 1 ? Button::released : Button::longreleased);
    sdev.remChannel(powerupkey + 1).state(Button::none);
  } else {
    sdev.dispChannel().changed(true);
  }

  runSetup = false;
}

void loop() {
  bool worked = hal.runready();
  bool poll = sdev.pollRadio();
  if ( worked == false && poll == false ) {
#ifdef BATTERY_MODE
    if (hal.battery.critical()) {
      display.drawPaged(emptyBatteryDisplay);
      hal.activity.sleepForever(hal);
    }
    if (ePaper.isWaiting()) {
      hal.activity.savePower<Idle<>>(hal);
    } else {
      hal.activity.savePower<Sleep<>>(hal);
    }
#else
    hal.activity.savePower<Idle<>>(hal);
#endif
  }
}

uint16_t centerPosition(const char * text) {
  return (display.width() / 2) - (u8g2Fonts.getUTF8Width(text) / 2);
}

String replaceText(String txt) {
  txt.replace("{", "ä");
  txt.replace("|", "ö");
  txt.replace("}", "ü");
  txt.replace("[", "Ä");
  txt.replace("#", "Ö");
  txt.replace("$", "Ü");
  txt.replace("~", "ß");
  txt.replace("'", "=");
  return txt;
}

void updateDisplay() {
  display.fillScreen(ePaper.BackgroundColor());

  for (uint8_t i = 0; i < DISPLAY_LINES; i++) {

    //Icons zeichnen
    if (i % 2 == 0) {
      uint8_t iconColumnIdx = i / 2;
      uint8_t icon_number = IconColumns[iconColumnIdx].Num;
      uint8_t col = iconColumnIdx / ICON_ROWS;
      uint8_t row = iconColumnIdx - (col * ICON_ROWS);
      uint16_t y = (row * (ICON_HEIGHT + (2 * ICON_MARGIN))) + ICON_MARGIN;
      if (icon_number != 255) {
        display.drawBitmap(
          Icons[icon_number],
          /*x=*/(IconColumns[iconColumnIdx].Right ? TEXT_COL_WIDTH : 0) + (col * COLUMN_WIDTH) + PADDING,
          /*y=*/y,
          /*w=*/ICON_WIDTH,
          /*h=*/ICON_HEIGHT,
          ePaper.ForegroundColor(),
          GxEPD::bm_normal
        );
      }

      if (ePaper.showGrid() == true) {
        if (row > 0)
          display.drawLine(0, y - (ICON_MARGIN / 2), display.width(), y - (ICON_MARGIN / 2), ePaper.ForegroundColor());

        if (col > 0)
          display.drawLine(col * COLUMN_WIDTH + 1, 0 , col * COLUMN_WIDTH + 1, display.height(), ePaper.ForegroundColor());
      }
    }

    //Text zeichnen
    String viewText = replaceText(DisplayLines[i].Text);
    u8g2Fonts.setFont(DisplayLines[i].Bold ? FONT_BOLD : FONT_REGULAR);
    uint8_t fh = (LINE_HEIGHT / 2) + ((LINE_HEIGHT - FONT_HEIGHT) / 2);
    uint8_t col = i / TEXT_ROWS;
    uint8_t row = i - (col * TEXT_ROWS);

    if (i % 2 == 0 && DisplayLines[i + 1].Text == "") {
      fh = (LINE_HEIGHT / 2) + ((ICON_HEIGHT - FONT_HEIGHT)) / 2;
    }

    uint8_t centerWidth = 0;
    if (DisplayLines[i].Center) {
      /*if (i % 2 == 1) {
        uint8_t prevTextWidth = u8g2Fonts.getUTF8Width(DisplayLines[i - 1].Text.c_str());
        uint8_t thisTextWidth = u8g2Fonts.getUTF8Width(viewText.c_str());
        int16_t textWidthDiff = prevTextWidth - thisTextWidth;
        centerWidth = textWidthDiff > 0 ? textWidthDiff / 2 : 0;
        if (IconColumns[i / 2].Right) centerWidth += (TEXT_COL_WIDTH - prevTextWidth);
        }*/
      uint8_t thisTextWidth = u8g2Fonts.getUTF8Width(viewText.c_str());
      int16_t textWidthDiff = TEXT_COL_WIDTH - thisTextWidth;
      centerWidth = textWidthDiff > 0 ? textWidthDiff / 2 : 0;
    } else {
      centerWidth = IconColumns[i / 2].Right ? TEXT_COL_WIDTH - u8g2Fonts.getUTF8Width(viewText.c_str()) : 0;
    }

    u8g2Fonts.setCursor(
      /*x=*/(col * COLUMN_WIDTH) + (IconColumns[i / 2].Right ? 0 : (ICON_COL_WIDTH)) + PADDING + centerWidth,
      /*y=*/(row * LINE_HEIGHT) + fh + (ICON_MARGIN * (i % 2 == 0 ? 1 : -1))
    );

    u8g2Fonts.print(viewText);
  }
}

void initDisplay() {
  display.fillScreen(ePaper.BackgroundColor());

  uint8_t serial[11];
  sdev.getDeviceSerial(serial);
  serial[10] = 0;

#define TOSTRING(x) #x
#define TOSTR(x) TOSTRING(x)

  const char * title        PROGMEM = "HB-Dis-EP-75BW";
  const char * asksinpp     PROGMEM = "AskSin++";
  const char * version      PROGMEM = "V " ASKSIN_PLUS_PLUS_VERSION;
  const char * compiledMsg  PROGMEM = "compiled on";
  const char * compiledDate PROGMEM = __DATE__ " " __TIME__;
  const char * pages        PROGMEM = "GxGDEW075T8_PAGES: " TOSTR(GxGDEW075T8_PAGES);
  const char * ser                  = (char*)serial;
  const char * nomaster1    PROGMEM = "- keine Zentrale -";
  const char * nomaster2    PROGMEM = "- angelernt -";

  u8g2Fonts.setFont(u8g2_font_helvB24_tf);
  u8g2Fonts.setCursor(centerPosition(title), 85);
  u8g2Fonts.print(title);

  u8g2Fonts.setCursor(centerPosition(asksinpp), 135);
  u8g2Fonts.print(asksinpp);

  u8g2Fonts.setCursor(centerPosition(version), 175);
  u8g2Fonts.print(version);

  u8g2Fonts.setFont(u8g2_font_helvB12_tf);
  u8g2Fonts.setCursor(centerPosition(compiledMsg), 200);
  u8g2Fonts.print(compiledMsg);
  u8g2Fonts.setCursor(centerPosition(compiledDate), 220);
  u8g2Fonts.print(compiledDate);

  u8g2Fonts.setFont(u8g2_font_helvR10_tf);
  u8g2Fonts.setCursor(centerPosition(pages), 240);
  u8g2Fonts.print(pages);

  u8g2Fonts.setFont(u8g2_font_helvB18_tf);
  u8g2Fonts.setCursor(centerPosition(ser), 285);
  u8g2Fonts.print(ser);

  if (sdev.getList0().masterid().valid() == false) {
    u8g2Fonts.setFont(u8g2_font_helvB18_tf);
    u8g2Fonts.setCursor(centerPosition(nomaster1), 325); u8g2Fonts.print(nomaster1);
    u8g2Fonts.setCursor(centerPosition(nomaster2), 351); u8g2Fonts.print(nomaster2);
  }

  display.drawRect(170, 103, 300, 145, ePaper.ForegroundColor());
}

void emptyBatteryDisplay() {
  display.fillScreen(ePaper.BackgroundColor());
  uint16_t fg = ePaper.ForegroundColor();

  uint8_t batt_x     = 205;
  uint8_t batt_y     = 122;
  uint8_t batt_w     = 230;
  uint8_t batt_h     = 140;
  uint8_t cap_width  = 40;
  uint8_t cap_height = 60;
  uint8_t line_w = 4;

  display.fillRect(batt_x+batt_w, batt_y+(batt_h / 2)-(cap_height/2), cap_width , cap_height, fg);

  for (uint8_t i = 0 ; i < line_w; i++) {
    display.drawRect(batt_x + i, batt_y + i, batt_w - i * 2, batt_h - i * 2, fg);
    display.drawLine(batt_x + i, batt_y + batt_h - 1, batt_x + batt_w - line_w + i, batt_y + 1, fg);
  }

  u8g2Fonts.setFont(u8g2_font_helvB18_tf);
  const char * batt_empty PROGMEM = "Batterie leer!";
  u8g2Fonts.setCursor(centerPosition(batt_empty), 335);
  u8g2Fonts.print(batt_empty);
}
