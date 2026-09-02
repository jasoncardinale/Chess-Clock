#include <ezButton.h>
#include "TM1637Display.h"

// I/O pins
#define DISPLAY_1_CLK 2
#define DISPLAY_1_DIO 3
#define DISPLAY_2_CLK 4
#define DISPLAY_2_DIO 5

#define SWITCH_1_PIN 6
#define SWITCH_2_PIN 7

#define LED_PIN 8

// 4-digit LED TM1637 displays
TM1637Display display_1(DISPLAY_1_CLK, DISPLAY_1_DIO);
TM1637Display display_2(DISPLAY_2_CLK, DISPLAY_2_DIO);

// Limit switches
ezButton button_1(SWITCH_1_PIN);
ezButton button_2(SWITCH_2_PIN);

const unsigned long HOUR = 3600000UL;
const unsigned long MINUTE = 60000UL;
const unsigned long SECOND = 1000UL;
const unsigned long LONG_PRESS_MS = 500UL;

// How long to show each value when flashing between base time and increment during idle mode
const unsigned long IDLE_FLASH_MS = 1500UL;

struct TimeControl {
  unsigned long base_ms;
  unsigned long increment_ms;
};

const TimeControl time_controls[] = {
  { MINUTE * 1, 0 },
  { MINUTE * 1, SECOND },
  { MINUTE * 3, 0 },
  { MINUTE * 3, SECOND * 2 },
  { MINUTE * 5, 0 },
  { MINUTE * 5, SECOND * 3 },
  { MINUTE * 10, 0 },
  { MINUTE * 10, SECOND * 5 },
  { MINUTE * 15, SECOND * 10 },
  { MINUTE * 30, 0 },
  { MINUTE * 30, SECOND * 20 },
  { HOUR, SECOND * 30 },
};
const int TIME_CONTROLS_COUNT = sizeof(time_controls) / sizeof(time_controls[0]);

unsigned long turn_start_time = 0;
unsigned long player_1_remaining = 0;
unsigned long player_2_remaining = 0;
unsigned long player_1_press_start = 0;
unsigned long player_2_press_start = 0;

// Turn states:
//   0 = idle (game not started)
//   1 = player 1's turn
//   2 = player 2's turn
//   3 = game over (someone flagged)
int turn = 0;
int time_control_index = 0;

// Track last value sent to each display so we only refresh when it changes
// -1 forces an update on the first call
int last_value_1 = -1;
int last_value_2 = -1;

void displayTime(TM1637Display &display, long time_in_ms, int &last_value) {
  if (time_in_ms < 0) {
    time_in_ms = 0;
  }

  int value;
  uint8_t colon = 0b01000000;

  if ((unsigned long)time_in_ms < MINUTE) {
    // Under a minute: show SS:HH (seconds and hundredths)
    int seconds = time_in_ms / 1000;
    int hundredths = (time_in_ms % 1000) / 10;
    value = seconds * 100 + hundredths;
  } else {
    // A minute or more: show MM:SS
    int minutes = time_in_ms / MINUTE;
    int seconds = (time_in_ms % MINUTE) / 1000;
    value = minutes * 100 + seconds;
  }

  if (value != last_value) {
    display.showNumberDecEx(value, colon, true, 4, 0);
    last_value = value;
  }
}

void resetClock() {
  turn_start_time = 0;
  player_1_remaining = time_controls[time_control_index].base_ms;
  player_2_remaining = time_controls[time_control_index].base_ms;
  turn = 0;
}

void gameOver() {
  turn = 3;

  for (int i = 0; i < 10; i++) {
    digitalWrite(LED_PIN, LOW);
    delay(250);
    digitalWrite(LED_PIN, HIGH);
    delay(250);
  }
}

void setup() {
  Serial.begin(9600);

  pinMode(SWITCH_1_PIN, INPUT_PULLUP);
  pinMode(SWITCH_2_PIN, INPUT_PULLUP);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  display_1.clear();
  display_2.clear();

  display_1.setBrightness(1);
  display_2.setBrightness(1);

  button_1.setDebounceTime(50);
  button_2.setDebounceTime(50);

  player_1_remaining = time_controls[time_control_index].base_ms;
  player_2_remaining = time_controls[time_control_index].base_ms;
}

void loop() {
  button_1.loop();
  button_2.loop();

  // Record when each button was pressed
  if (button_1.isPressed()) {
    player_1_press_start = millis();
  }
  if (button_2.isPressed()) {
    player_2_press_start = millis();
  }

  // Act on release, using the stored start time to decide short vs long
  if (button_1.isReleased()) {
    unsigned long held = millis() - player_1_press_start;
    if ((turn == 0 || turn == 3) && held >= LONG_PRESS_MS) {
      // Long press while idle or game over: cycle time control
      time_control_index = (time_control_index + 1) % TIME_CONTROLS_COUNT;
      resetClock();
    } else if (turn == 0) {
      // Short press while idle: start game, P2's turn first
      turn_start_time = millis();
      turn = 2;
    } else if (turn == 3) {
      // Short press while game over: reset to idle with same time control
      resetClock();
    } else if (turn == 1) {
      // End P1's turn
      unsigned long elapsed = millis() - turn_start_time;
      if (elapsed >= player_1_remaining) {
        // P1 flagged on the press itself
        player_1_remaining = 0;
        gameOver();
      } else {
        player_1_remaining += time_controls[time_control_index].increment_ms - elapsed;
        turn_start_time = millis();
        turn = 2;
      }
    }
  }

  if (button_2.isReleased()) {
    unsigned long held = millis() - player_2_press_start;
    if ((turn == 0 || turn == 3) && held >= LONG_PRESS_MS) {
      time_control_index = (time_control_index + 1) % TIME_CONTROLS_COUNT;
      resetClock();
    } else if (turn == 0) {
      turn_start_time = millis();
      turn = 1;
    } else if (turn == 3) {
      resetClock();
    } else if (turn == 2) {
      unsigned long elapsed = millis() - turn_start_time;
      if (elapsed >= player_2_remaining) {
        player_2_remaining = 0;
        gameOver();
      } else {
        player_2_remaining += time_controls[time_control_index].increment_ms - elapsed;
        turn_start_time = millis();
        turn = 1;
      }
    }
  }

  // Update displays
  if (turn == 0) {
    // Idle: flash between base time and increment
    // If increment is 0, just show the base time on both displays
    unsigned long inc = time_controls[time_control_index].increment_ms;
    unsigned long base = time_controls[time_control_index].base_ms;

    if (inc == 0) {
      displayTime(display_1, base, last_value_1);
      displayTime(display_2, base, last_value_2);
    } else {
      bool show_base = ((millis() / IDLE_FLASH_MS) % 2) == 0;
      unsigned long shown = show_base ? base : inc;
      displayTime(display_1, shown, last_value_1);
      displayTime(display_2, shown, last_value_2);
    }
  } else if (turn == 3) {
    // Game over: leave displays showing whatever they last showed
    // The flagged player's display will read 00:00
  } else {
    // Active turn
    unsigned long elapsed = millis() - turn_start_time;

    long time_1 = (long)player_1_remaining;
    long time_2 = (long)player_2_remaining;

    if (turn == 1) {
      time_1 -= (long)elapsed;
      if (time_1 <= 0) {
        time_1 = 0;
        player_1_remaining = 0;
        gameOver();
      }
    } else if (turn == 2) {
      time_2 -= (long)elapsed;
      if (time_2 <= 0) {
        time_2 = 0;
        player_2_remaining = 0;
        gameOver();
      }
    }

    displayTime(display_1, time_1, last_value_1);
    displayTime(display_2, time_2, last_value_2);
  }
}
