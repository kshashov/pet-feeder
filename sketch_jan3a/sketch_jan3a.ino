#include <Arduino.h>  
#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include <Preferences.h>
#include <ThreeWire.h> //  https://docs.sunfounder.com/projects/umsk/en/latest/03_esp32/esp32_lesson16_ds1302.html
#include <RtcDS1302.h>
#include <Stepper.h>

#define LED_B 34

// stepper
#define stepperIN1  23  
#define stepperIN2  19  
#define stepperIN3  4  
#define stepperIN4  15                  

Stepper stepper(2048, stepperIN1, stepperIN2, stepperIN3, stepperIN4);


// display
LiquidCrystal_I2C lcd(0x27,16,2);

// Chip specific
#define encoder0PinA  27                   // Rotary encoder gpio pin
#define encoder0PinB  26                   // Rotary encoder gpio pin
#define encoder0Press 25    
#define BUTTONPRESSEDSTATE 0               // rotary encoder gpio pin logic level when the button is pressed (usually 0)
#define DEBOUNCEDELAY 20      
const int itemTrigger = 2;                 // rotary encoder - counts per tick (varies between encoders usually 1 or 2)

// flash
const uint32_t NVM_Offset = 0x290000;      // Offset Value For NVS Partition

// rtc time clock
#define SCLK 18  // CLK
#define IO 17    // DAT
#define CE 16    // RST
ThreeWire myWire(IO, SCLK, CE);
RtcDS1302<ThreeWire> Rtc(myWire);

struct rotaryEncoders {
  volatile int encoder0Pos = 0;             // current value selected with rotary encoder (updated by interrupt routine)
  volatile bool encoderPrevA;               // used to debounced rotary encoder
  volatile bool encoderPrevB;               // used to debounced rotary encoder
  uint32_t reLastButtonChange = 0;          // last time state of button changed (for debouncing)
  bool encoderPrevButton = 0;               // used to debounce button
  int reButtonDebounced = 0;                // debounced current button state (1 when pressed)
  const bool reButtonPressedState = BUTTONPRESSEDSTATE;  // the logic level when the button is pressed
  const uint32_t reDebounceDelay = DEBOUNCEDELAY;        // button debounce delay setting
  bool reButtonPressed = 0;                 // flag set when the button is pressed (it has to be manually reset)
};
rotaryEncoders rotaryEncoder;

Preferences preferences;

// App specific
const int menuTimeout = 15;  
const int feedingTriggerMinutes = 5;

enum menuModes {
  off,                                  // display is off
  main,                                 // a menu is active
  value,                                // 'enter a value' none blocking is active
  message,                              // displaying a message
  blocking                              // a blocking procedure is in progress (see enter value)
};
menuModes menuMode = off; 

struct oledMenu {
  String items[10];
  int count;
  int selectedItem;
  int openedItem;
  uint32_t lastMenuActivity = 0;
};
oledMenu menu;


// Stored data data
uint32_t  time1 = 0;
uint32_t  time2 = 0;
uint32_t  time3 = 0;

bool time1Triggered = false;
bool time2Triggered = false;
bool time3Triggered = false;

uint32_t  portion = 1;

// the setup function runs once when you press reset or power the board
void setup() {
  // Serial monitor
  Serial.begin(115200); 
  while (!Serial); delay(50);       // start serial comms
  Serial.println("\n\n\nStarting menu\n");

  // init stepper
  stepper.setSpeed(10);

  // init clock
  initClock();

  // init led
  // pinMode(LED_B, OUTPUT);

  // init lcd
  lcd.init();
  lcd.clear();         
  lcd.backlight();      // Make sure backlight is on

  // init encoder
  pinMode(encoder0Press, INPUT_PULLUP);
  pinMode(encoder0PinA, INPUT);
  pinMode(encoder0PinB, INPUT);
  rotaryEncoder.encoder0Pos = 0;
  attachInterrupt(digitalPinToInterrupt(encoder0PinA), doEncoder, CHANGE);

  // load settings
  preferences.begin("feeder", false);
  time1 = preferences.getUInt("time1", 0);
  time2 = preferences.getUInt("time2", 0);
  time3 = preferences.getUInt("time3", 0);
  time1Triggered = preferences.getBool("time1Triggered", false);
  time2Triggered = preferences.getBool("time2Triggered", false);
  time3Triggered = preferences.getBool("time3Triggered", false);
  portion = preferences.getUInt("portion", 1);

  String items[5] = { "Feed Now", "Feed #1", "Feed #2", "Feed #3", "Amount" };
  initMenu(5, items);
  offMenu();                      // clear any previous menu
  resetFutureFeedTimers();
}

void initMenu(int _noOfElements, String *_list) {
  menu.count = _noOfElements;    // set the number of items in this menu
  for (int i=1; i <= _noOfElements; i++) {
    menu.items[i] = _list[i-1];        // set the menu items
  }
}

// the loop function runs over and over again forever
void loop() {
  readEncoder();     // update rotary encoder button status (if pressed activate default menu)
  renderMenu();         // update or action the oled menu
  feedIfTime();       // feed if time near presets [time, time+5min]
}

void initClock() {
  Rtc.Begin();
  RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__);
  // if (!Rtc.IsDateTimeValid()) {
    // Common Causes:
    //    1) first time you ran and the device wasn't running yet
    //    2) the battery on the device is low or even missing
    Serial.println("RTC lost confidence in the DateTime!");
    Rtc.SetDateTime(compiled);
  // }
  // RtcDateTime now = Rtc.GetDateTime();
  // if (now < compiled) {
  //   Serial.println("RTC is older than compile time!  (Updating DateTime)");
  //   Rtc.SetDateTime(compiled);
  // } else if (now > compiled) {
  //   Serial.println("RTC is newer than compile time. (this is expected)");
  // } else if (now == compiled) {
  //   Serial.println("RTC is the same as compile time! (not expected but all is fine)");
  // }
}

void readEncoder() {
  bool tReading = digitalRead(encoder0Press);        // read current button state
  if (tReading != rotaryEncoder.encoderPrevButton) rotaryEncoder.reLastButtonChange = millis();     // if it has changed reset timer
  if ((unsigned long)(millis() - rotaryEncoder.reLastButtonChange) > rotaryEncoder.reDebounceDelay ) {  // if button state is stable
    if (rotaryEncoder.encoderPrevButton == rotaryEncoder.reButtonPressedState) {
      if (rotaryEncoder.reButtonDebounced == 0) {    // if the button has been pressed
        rotaryEncoder.reButtonPressed = 1;           // flag set when the button has been pressed
        if (menuMode == off) {
          delay(1000);
          // if the display is off start the default menu
          menuMode = main;                  // enable menu mode   
          rotaryEncoder.encoderPrevButton = tReading;
          return;       
        }
      }
      rotaryEncoder.reButtonDebounced = 1;           // debounced button status  (1 when pressed)
    } else {
      rotaryEncoder.reButtonDebounced = 0;
    }
  }
  rotaryEncoder.encoderPrevButton = tReading;            // update last state read
}

void offMenu() {
  // reset all menu variables / flags
  menuMode = off;
  menu.selectedItem = 1;
  menu.lastMenuActivity = millis();   // log time
  menu.openedItem = 0;

  rotaryEncoder.encoder0Pos = 0;
  rotaryEncoder.reButtonPressed = 0;
  rotaryEncoder.reButtonDebounced = 0;

  // clear oled display
  lcd.clear();
  lcd.noBacklight();
}

void renderMenu() {

  if (menuMode == off) return;    // if menu system is turned off do nothing more

  // if no recent activity then turn oled off
  if ( (unsigned long)(millis() - menu.lastMenuActivity) > (menuTimeout * 1000) ) {
    offMenu();
    return;
  }

  switch (menuMode) {
    // if there is an active menu
    case main:
      showMenu();
      // menuActions();
      break;

    // if there is an active none blocking 'enter value'
    case value:
      // serviceValue(0);
      // if (rotaryEncoder.reButtonPressed) {                        // if the button has been pressed
        // menuValues();                                             // a value has been entered so action it
        break;
      // }

    // if a message is being displayed
    case message:
      // if (rotaryEncoder.reButtonPressed == 1) defaultMenu();    // if button has been pressed return to default menu
      break;
  }
}

void showMenu() {
  bool isSelected = false;
  // rotary encoder
  if (rotaryEncoder.encoder0Pos >= itemTrigger) {
    rotaryEncoder.encoder0Pos -= itemTrigger;
    menu.selectedItem++;
    menu.lastMenuActivity = millis();   // log time
    isSelected = true;
  }
  if (rotaryEncoder.encoder0Pos <= -itemTrigger) {
    rotaryEncoder.encoder0Pos += itemTrigger;
    menu.selectedItem--;
    menu.lastMenuActivity = millis();   // log time
    isSelected = true;
  }

  // make cursor be cycled by list count
  if (menu.selectedItem < 1) {
    menu.selectedItem = menu.count;
  }

  if (menu.selectedItem > menu.count) {
    menu.selectedItem = 1;
  }
 
  bool isOpened = false;
  if (rotaryEncoder.reButtonDebounced == 1) {
    menu.openedItem = menu.selectedItem;     // flag that the item has been selected
    rotaryEncoder.reButtonPressed = 0;
    rotaryEncoder.reButtonDebounced = 0;
    menu.lastMenuActivity = millis();   // log time
    isOpened = true;
  }

  showMenuItems(isOpened);
}

void showMenuItems(bool somethingIsOpened) {
 // show menu at top
  lcd.backlight();
  lcd.setCursor(0,0);

  String item = menu.items[menu.selectedItem];
  if (menu.selectedItem == 1) {
    // Add time for manual feeding

  }
  if (menu.openedItem == menu.selectedItem) {
    item = "[" + item + "]";
  }

  printLn("> " + item + "     ");

  if (somethingIsOpened) {
    onOpened();
  } else {
    onSelected();
  }
}

void onSelected() {
  lcd.setCursor(0,1);
  int selected = menu.selectedItem;
  if (selected == 1) {
    // printLn("Feed Now"); 
    printLn(getStringDateTime());
  } else if (selected == 2) {
    if (time1 == 0) {
      printLn("OFF");
    } else {
      printTime(time1);
    }
  } else if (selected == 3) {
    if (time2 == 0) {
      printLn("OFF");
    } else {
      printTime(time2);
    }
  } else if (selected == 4) {
    if (time3 == 0) {
      printLn("OFF");
    } else {
      printTime(time3);
    }
  } else if (selected == 5) {
    // portion size
    printLn(portion); 
  } 
}

void onOpened() {
  lcd.setCursor(0,1);
  // show opened menu item at bottom
  int opened = menu.openedItem;
  if (opened == 1) {
    printLn("YUMMY");
    feed();
  } else if (opened == 2) {
    time1 = enterFeedingTime(time1);
    preferences.putUInt("time1", time1);
  } else if (opened == 3) {
    time2 = enterFeedingTime(time2); 
    preferences.putUInt("time2", time2);
  } else if (opened == 4) {
    time3 = enterFeedingTime(time3); 
    preferences.putUInt("time3", time3);
  } else if (opened == 5) {
    // Enter portion size
    portion = enterAmount(portion);
    preferences.putUInt("portion", portion);
  } 

  menu.openedItem = 0; 
  menuMode = main;
}


int enterAmount(int current) {
  menuMode = blocking;
  menu.lastMenuActivity = millis();   // log time of last activity (for timeout)
  
  current = enterInt(current, 1, 9, 1, 0);

  return current;        
}

int enterFeedingTime(int current) {
  menuMode = blocking;
  menu.lastMenuActivity = millis();  

  current = enterTime(current, 1, 0);
  
  return current;
}

int enterTime(int current, int cursorV, int cursorH) {
  const int low = 0;
  const int high = 1435;
  const int step = 5; 
  uint32_t tTime;
  delay(500);
  rotaryEncoder.reButtonPressed = 0;
  do {
    // rotary encoder
    if (rotaryEncoder.encoder0Pos >= itemTrigger) {
      rotaryEncoder.encoder0Pos -= itemTrigger;
      current+= step;
      menu.lastMenuActivity = millis();   // log time
    }
    if (rotaryEncoder.encoder0Pos <= -itemTrigger) {
      rotaryEncoder.encoder0Pos += itemTrigger;
      current-= step;
      menu.lastMenuActivity = millis();   // log time
    }
    if (current < low) {
      current = high;
      menu.lastMenuActivity = millis();   // log time
    }
    if (current > high) {
      current = low;
      menu.lastMenuActivity = millis();   // log time
    }

    lcd.setCursor(cursorH,cursorV);

    if (current == 0) {
      printLn("OFF");
    } else {
      printTime(current);
    }

    readEncoder();        // check status of button
    tTime = (unsigned long)(millis() - menu.lastMenuActivity);      // time since last activity
  } while ((rotaryEncoder.reButtonPressed == 0) && tTime < (menuTimeout * 1000));        // if in blocking mode repeat until button is pressed or timeout
  rotaryEncoder.reButtonPressed = 0;
  rotaryEncoder.reButtonDebounced = 0;

  delay(500);
  
  return current;   
}

int enterInt(int current, int low, int high, int cursorV, int cursorH) {
  const int step = 1; 
  uint32_t tTime;
  delay(500);
  rotaryEncoder.reButtonPressed = 0;
  do {
    // rotary encoder
    if (rotaryEncoder.encoder0Pos >= itemTrigger) {
      rotaryEncoder.encoder0Pos -= itemTrigger;
      current+= step;
      menu.lastMenuActivity = millis();   // log time
    }
    if (rotaryEncoder.encoder0Pos <= -itemTrigger) {
      rotaryEncoder.encoder0Pos += itemTrigger;
      current-= step;
      menu.lastMenuActivity = millis();   // log time
    }
    if (current < low) {
      current = low;
      menu.lastMenuActivity = millis();   // log time
    }
    if (current > high) {
      current = high;
      menu.lastMenuActivity = millis();   // log time
    }

    lcd.setCursor(cursorH,cursorV);
    printLn(current);

    readEncoder();        // check status of button
    tTime = (unsigned long)(millis() - menu.lastMenuActivity);      // time since last activity

  } while ((rotaryEncoder.reButtonPressed == 0) && tTime < (menuTimeout * 1000));        // if in blocking mode repeat until button is pressed or timeout
  rotaryEncoder.reButtonPressed = 0;
  rotaryEncoder.reButtonDebounced = 0;

  delay(500);
  
  return current;   
}

// ----------------------------------------------------------------
//                     -interrupt for rotary encoder
// ----------------------------------------------------------------
// rotary encoder interrupt routine to update position counter when turned
//     interrupt info: https://www.gammon.com.au/forum/bbshowpost.php?id=11488

void doEncoder() {

  bool pinA = digitalRead(encoder0PinA);
  bool pinB = digitalRead(encoder0PinB);

  if ( (rotaryEncoder.encoderPrevA == pinA && rotaryEncoder.encoderPrevB == pinB) ) return;  // no change since last time (i.e. reject bounce)

  // same direction (alternating between 0,1 and 1,0 in one direction or 1,1 and 0,0 in the other direction)
  if (rotaryEncoder.encoderPrevA == 1 && rotaryEncoder.encoderPrevB == 0 && pinA == 0 && pinB == 1) rotaryEncoder.encoder0Pos -= 1;
    else if (rotaryEncoder.encoderPrevA == 0 && rotaryEncoder.encoderPrevB == 1 && pinA == 1 && pinB == 0) rotaryEncoder.encoder0Pos -= 1;
    else if (rotaryEncoder.encoderPrevA == 0 && rotaryEncoder.encoderPrevB == 0 && pinA == 1 && pinB == 1) rotaryEncoder.encoder0Pos += 1;
    else if (rotaryEncoder.encoderPrevA == 1 && rotaryEncoder.encoderPrevB == 1 && pinA == 0 && pinB == 0) rotaryEncoder.encoder0Pos += 1;

  // change of direction
    else if (rotaryEncoder.encoderPrevA == 1 && rotaryEncoder.encoderPrevB == 0 && pinA == 0 && pinB == 0) rotaryEncoder.encoder0Pos += 1;
    else if (rotaryEncoder.encoderPrevA == 0 && rotaryEncoder.encoderPrevB == 1 && pinA == 1 && pinB == 1) rotaryEncoder.encoder0Pos += 1;
    else if (rotaryEncoder.encoderPrevA == 0 && rotaryEncoder.encoderPrevB == 0 && pinA == 1 && pinB == 0) rotaryEncoder.encoder0Pos -= 1;
    else if (rotaryEncoder.encoderPrevA == 1 && rotaryEncoder.encoderPrevB == 1 && pinA == 0 && pinB == 1) rotaryEncoder.encoder0Pos -= 1;

  //else if (serialDebug) Serial.println("Error: invalid rotary encoder pin state - prev=" + String(rotaryEncoder.encoderPrevA) + ","
  //                                      + String(rotaryEncoder.encoderPrevB) + " new=" + String(pinA) + "," + String(pinB));

  // update previous readings
  rotaryEncoder.encoderPrevA = pinA;
  rotaryEncoder.encoderPrevB = pinB;
}

void feedIfTime() {
  // Reset triggered timers for new day
  RtcDateTime dt = getTime();
  int currentTime = dt.Hour() * 60 + dt.Minute();

  if (time1 != 0) { 
    if (0 <= (currentTime - time1) <= feedingTriggerMinutes) {
      if (!time1Triggered) {
        feed();
        time1Triggered = true;
        preferences.putBool("time1Triggered", time1Triggered);
      }
    }
  }

  if (time2 != 0) {
    if (0 <= (currentTime - time2) <= feedingTriggerMinutes) {
      if (!time2Triggered) {
        feed();
        time2Triggered = true;
        preferences.putBool("time2Triggered", time2Triggered);
      }
    }
  }

  if (time3 != 0) { 
    if (0 <= (currentTime - time3) <= feedingTriggerMinutes) {
      if (!time3Triggered) {
        feed();
        time3Triggered = true;
        preferences.putBool("time3Triggered", time3Triggered);
      }
    }
  }
  
}

void resetFutureFeedTimers() {
  RtcDateTime dt = getTime();
  int currentTime = dt.Hour()*60+dt.Minute();
  if ((currentTime < time1) && time1Triggered) {
    time1Triggered = false;
    preferences.putBool("time1Triggered", time1Triggered);
  }
  if ((currentTime < time2) && time2Triggered) {
    time2Triggered = false;
    preferences.putBool("time2Triggered", time2Triggered);
  }
  if ((currentTime < time3) && time3Triggered) {
    time3Triggered = false;
    preferences.putBool("time3Triggered", time3Triggered);
  }
  // TODO time2 time3
}

void feed() {
  byte mult = 1;
  for (int i = 0; i < portion; i++) {
    delay(10);
    stepper.step(mult*256);
    stepper.step(-mult*1024);
    delay(10);

    stepper.step(mult*256);
    stepper.step(-mult*1024);
    delay(10);
  }

  // digitalWrite(LED_B, HIGH);  // turn the LED on (HIGH is the voltage level)
  // delay(3000);                      // wait for a second
  // digitalWrite(LED_B, LOW);   // turn the LED off by making the voltage LOW
  // delay(500);                      // wait for a second
}

void printTime(int time) {
  int hours = time / 60;
  int minutes = time % 60;
  printLn(formatTime(hours, minutes));
}

String formatTime(int hours, int minutes) {
  String total = "";
  
  if (hours < 10) {
    total += "0";
  }
  
  total += String(hours);
  total += ":";

  if (minutes < 10) {
    total += "0";
  }
  total += String(minutes);
  return total;
}

void printLn(int num) {
  printLn(String(num));
}

void printLn(String text) {
  int oldSize = text.length();
  if (oldSize < 16) {
    for (int i = 0; i < 16 - oldSize; i++) {
      text += " ";
    }
  }
  lcd.print(text);
}

// obsolete
String getStringDateTime() {
  RtcDateTime dt = getTime();
  char datestring[16];

  snprintf_P(datestring,
             countof(datestring),
             PSTR("%02u/%02u %02u:%02u:%02u"),
             dt.Day(),
             dt.Month(),
             dt.Hour(),
             dt.Minute(),
             dt.Second()
             );
  return String(datestring);
  // printLn(datestring);
}

RtcDateTime getTime() {
  return Rtc.GetDateTime();
}
