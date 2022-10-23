#include "Arduino.h"
#include <arduino-timer.h>

#include "OneButton.h"

namespace settings
{
  constexpr bool debug = false;

  namespace serial
  {
    constexpr unsigned long baud = 115200;
  }

  namespace pins
  {
    constexpr int fan = 5;          // fan power mosfet switch/pwm - using this as
                                    // speed control, however it is only on above ~70% duty
    constexpr int mistSwitch = 7;   // mist solenoid power mosfet
    constexpr int buttonOne = 9;    // pushbutton closest to the connector
    constexpr int buttonTwo = 11;   // pushbutton in middle
    constexpr int buttonThree = 12; // pushbutton farthest from the connector
  }

  namespace delays
  {
    constexpr unsigned long timeout = 2 * 60 * 60 * 1000; // if no buttons are pressed for this long, then fan and
                                                          // mist will be turned off //(and sleep not implemented)
  }

  namespace pwm
  {
    constexpr uint32_t precision = 8;
    constexpr uint32_t frequency = 25000;

    namespace channel
    {
      constexpr int fan = 1;
      constexpr int mist = 2;
    }
  }
}

struct CurrentValue
{
  bool mistState = 0; // Current relay state
};
CurrentValue currentValue;

void setMistState(bool state) { currentValue.mistState = state; }
bool getMistState() { return currentValue.mistState; }

auto timer = timer_create_default(); // create a timer with default settings
Timer<>::Task mistForDurationRepeatingTask;
Timer<>::Task timeoutTimerTask;

OneButton buttonOne = OneButton(settings::pins::buttonOne, // Input pin for the button
                                true,                      // Button is active LOW
                                true                       // Enable internal pull-up resistor
);

OneButton buttonTwo = OneButton(settings::pins::buttonTwo, // Input pin for the button
                                true,                      // Button is active LOW
                                true                       // Enable internal pull-up resistor
);

OneButton buttonThree = OneButton(settings::pins::buttonThree, // Input pin for the button
                                  true,                        // Button is active LOW
                                  true                         // Enable internal pull-up resistor
);

uint32_t calculateMaxDutyFromPrecision(int precision)
{
  uint32_t maxDuty = (pow(2, precision) - 1);
  return maxDuty;
}

uint32_t calculateDutyFromPercent(int percent)
{
  uint32_t duty =
      (percent / 100.0) * calculateMaxDutyFromPrecision(settings::pwm::precision);
  return duty;
}

void setPwmPercent(uint32_t pwmChannel, int percent)
{
  if (settings::debug) Serial.printf("Channel %d PWM %d\n", pwmChannel, percent);
  ledcWrite(pwmChannel, calculateDutyFromPercent(percent));
}

void setFanSpeedPercent(int percent)
{
  setPwmPercent(settings::pwm::channel::fan, percent);
}

void writeMistState(bool state = currentValue.mistState)
{
  if ((state && !getMistState()) || (!state && getMistState()))
  {
    digitalWrite(settings::pins::mistSwitch, state);
    setMistState(state);
  }
}

void mistOn()
{
  if (settings::debug) Serial.println("Turning mist ON");
  writeMistState(1);
}

void cancelMistForDurationRepeatingTask()
{
  if (settings::debug) Serial.println("Repeating mist task CANCELLED");
  timer.cancel(mistForDurationRepeatingTask);
}

bool mistOnFromTimer(void *)
{
  mistOn();
  return true; 
}

void mistOff()
{
  if (settings::debug) Serial.println("Turning mist OFF");
  writeMistState(0);
}
bool mistOffFromTimer(void *)
{
  mistOff();
  return true; 
}

void toggleMistState()
{
  if (settings::debug) Serial.println("Toggling mist pin state");
  writeMistState(!currentValue.mistState);
}

void mistForDuration(size_t duration)
{
  if (settings::debug) Serial.printf("Turning mist ON for %d seconds\n", (duration / 1000));
  mistOn();
  timer.in(duration, mistOffFromTimer); //(delay, function_to_call)
}

bool mistForDurationFromTimer(void *opaque)
{
  if (buttonOne.isLongPressed())
  {
    if (settings::debug) Serial.println("mistForDurationFromTimer:  ");
    if (settings::debug) Serial.println("  Task triggered, but currently misting while button is held,");
    if (settings::debug) Serial.println("  so this task will be skipped");
  }
  else
  {
    size_t duration = (size_t)opaque;
    mistForDuration(duration);
  }
  return true; 
}

void mistForDurationRepeating(size_t onDuration, size_t offDuration)
{
  if (settings::debug) Serial.printf(
      "Starting mist on/off repeating timer; on for %d seconds, off for %d "
      "seconds.",
      (onDuration / 1000), (offDuration / 1000));
  mistForDuration(
      onDuration); // timer.every waits for the off duration before first call,
                   // so we call the function once initially.
  mistForDurationRepeatingTask = timer.every((offDuration + onDuration), mistForDurationFromTimer,
                                             (void *)onDuration); // (interval, function_to_call, argument)
}

void fanOn()
{
  if (settings::debug) Serial.println("Turning fan ON");
  setPwmPercent(settings::pwm::channel::fan, 100);
}

void fanOff()
{
  if (settings::debug) Serial.println("Turning fan OFF");
  setPwmPercent(settings::pwm::channel::fan, 0);
}

void cancelAllTimerTasks()
{
  if (settings::debug) Serial.printf("Cancelling ALL running timer tasks!\n");
  timer.cancel();
}

void cancelAllTimerTasksAndTurnOffMistAndFan()
{
  cancelAllTimerTasks();
  mistOff();
  fanOff();
}

void implementTimeout()
{
  if (settings::debug) Serial.println("Timeout timer task has executed, doing timeout task now...");
  cancelAllTimerTasksAndTurnOffMistAndFan();
  // go to sleep? need to add (deep)sleep mode.
}

bool implementTimeoutFromTimer(void *)
{
  implementTimeout();
  return true;
}

void createTimeoutTimer()
{
  if (settings::debug) Serial.print("Timeout timer (re)set, timeout in (ms): ");
  if (settings::debug) Serial.println(settings::delays::timeout);
  timeoutTimerTask = timer.in(settings::delays::timeout, implementTimeoutFromTimer);
}

void resetTimeoutTimer()
{
  timer.cancel(timeoutTimerTask);
  createTimeoutTimer();
}

// This function will be called when the button1 was pressed 1 time (and no 2.
// button press followed).
void clickOne()
{
  resetTimeoutTimer();
  if (settings::debug) Serial.println("Button 1 click.");
  mistForDuration(1000);
}

// This function will be called when the button1 was pressed 2 times in a short
// timeframe.
void doubleclickOne()
{
  resetTimeoutTimer();
  if (settings::debug) Serial.println("Button 1 doubleclick.");
  // mist for 1 second every 30 seconds
  mistForDurationRepeating(1000, 30000);
}

// This function will be called once, when the button1 is pressed for a long
// time.
void longPressStartOne()
{
  resetTimeoutTimer();
  if (settings::debug) Serial.println("Button 1 longPress start");
}

// This function will be called often, while the button1 is pressed for a long
// time.
void longPressOne()
{
  resetTimeoutTimer();
  if (settings::debug) Serial.println("Button 1 longPress...");
  mistOn();
}

// This function will be called once, when the button1 is released after beeing
// pressed for a long time.
void longPressStopOne()
{
  resetTimeoutTimer();
  if (settings::debug) Serial.println("Button 1 longPress stop");
  mistOff();
}

// this function will be called when the button was pressed multiple times in a
// short timeframe.
void multiClickOne()
{
  resetTimeoutTimer();
  int n = buttonOne.getNumberClicks();
  if (settings::debug) Serial.printf("multiclick detected, n=%d. \n", n);
  if (n == 3)
  {
    mistForDurationRepeating(1000, 15000);
  }
  else if (n == 4)
  {
    mistForDurationRepeating(3000, 30000);
  }
  else if (n == 5)
  {
    mistForDurationRepeating(3000, 15000);
  }
  else
  {
  }
}

void clickTwo()
{
  resetTimeoutTimer();
  if (settings::debug) Serial.println("Button 2 click.");
  fanOn();
}

void doubleclickTwo()
{
  resetTimeoutTimer();
  if (settings::debug) Serial.println("Button 2 doubleclick.");
  fanOff();
}

void longPressStartTwo()
{
  resetTimeoutTimer();
  if (settings::debug) Serial.println("Button 2 longPress start");
}

void longPressTwo()
{
  resetTimeoutTimer();
  if (settings::debug) Serial.println("Button 2 longPress...");
}

void longPressStopTwo()
{
  resetTimeoutTimer();
  if (settings::debug) Serial.println("Button 2 longPress stop");
}

void multiClickTwo()
{
  resetTimeoutTimer();
  int n = buttonTwo.getNumberClicks();
  if (n == 3)
  {
    if (settings::debug) Serial.println("tripleClick detected.");
  }
  else if (n == 4)
  {
    if (settings::debug) Serial.println("quadrupleClick detected.");
  }
  else
  {
    if (settings::debug) Serial.print("multiClick(");
    if (settings::debug) Serial.print(n);
    if (settings::debug) Serial.println(") detected.");
  }
}

void clickThree()
{
  resetTimeoutTimer();
  if (settings::debug) Serial.println("Button 3 click.");
  cancelMistForDurationRepeatingTask();
}

void doubleclickThree()
{
  resetTimeoutTimer();
  if (settings::debug) Serial.println("Button 3 doubleclick.");
  cancelAllTimerTasksAndTurnOffMistAndFan();
}

void longPressStartThree()
{
  resetTimeoutTimer();
  if (settings::debug) Serial.println("Button 3 longPress start");
}

void longPressThree()
{
  resetTimeoutTimer();
  if (settings::debug) Serial.println("Button 3 longPress...");
}

void longPressStopThree()
{
  resetTimeoutTimer();
  if (settings::debug) Serial.println("Button 3 longPress stop");
}

void multiClickThree()
{
  resetTimeoutTimer();
  int n = buttonThree.getNumberClicks();
  if (n == 3)
  {
    if (settings::debug) Serial.println("tripleClick detected.");
  }
  else if (n == 4)
  {
    if (settings::debug) Serial.println("quadrupleClick detected.");
  }
  else
  {
    if (settings::debug) Serial.print("multiClick(");
    if (settings::debug) Serial.print(n);
    if (settings::debug) Serial.println(") detected.");
  }
}

void buttonTick()
{
  buttonOne.tick();
  buttonTwo.tick();
  buttonThree.tick();
}

bool buttonTickFromTimer(void *)
{
  buttonTick();
  return true;
}

void buttonSetup()
{
  if (settings::debug) Serial.println("Setting up buttons...");
  buttonOne.attachClick(clickOne);
  buttonOne.attachDoubleClick(doubleclickOne);
  buttonOne.attachLongPressStart(longPressStartOne);
  buttonOne.attachLongPressStop(longPressStopOne);
  buttonOne.attachDuringLongPress(longPressOne);
  buttonOne.attachMultiClick(multiClickOne);

  buttonTwo.attachClick(clickTwo);
  buttonTwo.attachDoubleClick(doubleclickTwo);
  buttonTwo.attachLongPressStart(longPressStartTwo);
  buttonTwo.attachLongPressStop(longPressStopTwo);
  buttonTwo.attachDuringLongPress(longPressTwo);
  buttonTwo.attachMultiClick(multiClickTwo);

  buttonThree.attachClick(clickThree);
  buttonThree.attachDoubleClick(doubleclickThree);
  buttonThree.attachLongPressStart(longPressStartThree);
  buttonThree.attachLongPressStop(longPressStopThree);
  buttonThree.attachDuringLongPress(longPressThree);
  buttonThree.attachMultiClick(multiClickThree);

  timer.every(0, buttonTickFromTimer);
  if (settings::debug) Serial.println("Buttons setup successfully");
}

void setup()
{
  if (settings::debug) Serial.begin(115200);

  if (settings::debug) Serial.println("Starting setup...");
  createTimeoutTimer();

  pinMode(settings::pins::mistSwitch, OUTPUT);

  ledcSetup(settings::pwm::channel::fan, settings::pwm::frequency, settings::pwm::precision);
  ledcAttachPin(settings::pins::fan, settings::pwm::channel::fan);

  if (settings::debug) Serial.println("Setting up buttons...");
  buttonSetup();
  if (settings::debug) Serial.println("Completed setup...");

  fanOn();
}

void loop()
{
  timer.tick();
}
