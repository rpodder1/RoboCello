// Single solenoid test — DRV8833 H-bridge
// Repeatedly engages and releases one solenoid so you can verify
// pulse duration, force, and wiring before running the full cello code.

// DRV8833 pins for the solenoid under test
const uint8_t IN1 = 22;  // change to whichever pins you have wired
const uint8_t IN2 = 23;

// Pulse duration (ms) — increase if the latch isn't catching, decrease if solenoid gets warm
const uint16_t LATCH_PULSE_MS = 30;

// Time to hold the engaged state before releasing (ms)
const uint16_t HOLD_MS = 1000;

// Time to hold the released state before engaging again (ms)
const uint16_t REST_MS = 1000;

void setup() {
  Serial.begin(9600);
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  Serial.println("Solenoid test ready.");
}

void loop() {
  // Engage — pulse IN1 HIGH to extend plunger and latch down
  Serial.println("ENGAGE");
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  delay(LATCH_PULSE_MS);
  digitalWrite(IN1, LOW);

  delay(HOLD_MS);

  // Release — pulse IN2 HIGH to retract plunger and unlatch
  Serial.println("RELEASE");
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  delay(LATCH_PULSE_MS);
  digitalWrite(IN2, LOW);

  delay(REST_MS);
}
