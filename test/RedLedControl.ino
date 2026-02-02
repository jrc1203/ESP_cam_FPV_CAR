// RedLedControl.ino
// Controls GPIO33 red LED per joystick commands with different blink rates

#define RED_LED_PIN 33

// Joystick motion states
enum JoystickState { NONE, FORWARD, BACKWARD, LEFT, RIGHT };
JoystickState currentState = NONE;

// Blink timing
unsigned long lastToggle = 0;
bool ledOn = false;

// Call from main loop with current joystick states
// forward = true if joystick forward pressed, same for others
void updateRedLed(bool forward, bool backward, bool left, bool right) {
  if (forward) {
    currentState = FORWARD;
    digitalWrite(RED_LED_PIN, LOW);  // ON (inverted logic)
    return;
  }
  else if (backward) currentState = BACKWARD;
  else if (left) currentState = LEFT;
  else if (right) currentState = RIGHT;
  else currentState = NONE;

  if (currentState == NONE) {
    digitalWrite(RED_LED_PIN, HIGH); // OFF
    ledOn = false;
    return;
  }

  unsigned long now = millis();
  unsigned long interval = 0;
  switch (currentState) {
    case BACKWARD: interval = 200; break;
    case LEFT:     interval = 600; break;
    case RIGHT:    interval = 1000; break;
    default:       interval = 0;   break;
  }

  if (interval > 0 && now - lastToggle >= interval) {
    ledOn = !ledOn;
    digitalWrite(RED_LED_PIN, ledOn ? LOW : HIGH); // invert for ON/OFF
    lastToggle = now;
  }
}

void redLedSetup() {
  pinMode(RED_LED_PIN, OUTPUT);
  digitalWrite(RED_LED_PIN, HIGH); // start OFF
}
