#include <SPI.h>
#include <SD.h>
#include <math.h>

const int CS = 4;
File midi;

const uint32_t DEFAULT_TEMPO = 500000;
uint32_t tempo = DEFAULT_TEMPO;
uint16_t division;
uint8_t lastStatus = 0;

const uint8_t NUM_STRINGS   = 4;
const uint8_t NUM_FINGERS   = 4;
const uint8_t NUM_SOLENOIDS = NUM_STRINGS * NUM_FINGERS;

// Pulse duration for latching solenoid engage/release (ms) — tune to your solenoid
const uint16_t LATCH_PULSE_MS = 30;

// Open string MIDI notes — standard cello tuning
const uint8_t OPEN_STRING_NOTES[NUM_STRINGS] = {
  36,  // C2
  43,  // G2
  50,  // D3
  57   // A3
};

// Semitone offsets per finger above open string
const uint8_t FINGER_OFFSETS[NUM_FINGERS] = {
  1,   // index
  3,   // middle
  5,   // ring
  7    // pinky
};

// DRV8833 IN1/IN2 pin pairs per solenoid [string][finger]
// IN1=HIGH IN2=LOW  → plunger extends (finger presses string)
// IN1=LOW  IN2=HIGH → plunger retracts (finger lifts)
// IN1=LOW  IN2=LOW  → coast/idle
const uint8_t IN1_PINS[NUM_STRINGS][NUM_FINGERS] = {
  { 22, 24, 26, 28 },  // String 0 (C2)
  { 30, 32, 34, 36 },  // String 1 (G2)
  { 38, 40, 42, 44 },  // String 2 (D3)
  { 46, 48, 50, 52 }   // String 3 (A3)
};

const uint8_t IN2_PINS[NUM_STRINGS][NUM_FINGERS] = {
  { 23, 25, 27, 29 },  // String 0 (C2)
  { 31, 33, 35, 37 },  // String 1 (G2)
  { 39, 41, 43, 45 },  // String 2 (D3)
  { 47, 49, 51, 53 }   // String 3 (A3)
};

// noteToSolenoidIdx[midiNote] = flat solenoid index (string * NUM_FINGERS + finger), or -1
int noteToSolenoidIdx[128];

// Which finger is currently latched on each string (-1 = none)
int8_t activeFingerOnString[NUM_STRINGS];

void buildNoteMap();
void playNote(uint8_t midiNote);
void releaseNote(uint8_t midiNote);
void pressFingerLatching(uint8_t str, uint8_t finger);
void releaseFingerLatching(uint8_t str, uint8_t finger);
void releaseAllFingers();
uint32_t deltaTime();
String noteName(uint8_t midiNote);

void setup() {
  Serial.begin(9600);

  // Initialise all DRV8833 pins LOW (coast/idle)
  for (uint8_t s = 0; s < NUM_STRINGS; s++) {
    for (uint8_t f = 0; f < NUM_FINGERS; f++) {
      pinMode(IN1_PINS[s][f], OUTPUT);
      pinMode(IN2_PINS[s][f], OUTPUT);
      digitalWrite(IN1_PINS[s][f], LOW);
      digitalWrite(IN2_PINS[s][f], LOW);
    }
    activeFingerOnString[s] = -1;
  }

  releaseAllFingers();
  buildNoteMap();

  SD.begin(CS);

  //INPUT MIDI FILE NAME
  midi = SD.open("CELLO.mid", FILE_READ);

  char header[4];
  midi.readBytes(&header[0], 4);
  if (strncmp(header, "MThd", 4) != 0) {
    Serial.println("Not a MIDI file!");
  }

  midi.seek(12);
  division = (midi.read() << 8) | midi.read();
  Serial.print("Ticks per quarter note: ");
  Serial.println(division);
  tempo = DEFAULT_TEMPO;

  midi.seek(14 + 8);

  Serial.print("CELLO.mid size: ");
  Serial.print(midi.size());
  Serial.println(" bytes");

  Serial.println("Robotic Cello ready.");
}

void loop() {
  while (midi.available()) {
    uint32_t dTime   = deltaTime();
    uint32_t usTick  = tempo / division;
    uint32_t msDelay = (dTime * usTick) / 1000;
    delay(msDelay);

    long pos       = midi.position();
    uint8_t status = midi.read();

    // Running status
    if (status < 0x80) {
      midi.seek(pos);
      status = lastStatus;
    } else {
      lastStatus = status;
    }

    // Meta events
    if (status == 0xFF) {
      uint8_t type = midi.read();
      uint32_t len = deltaTime();
      if (type == 0x51 && len == 3) {
        tempo = ((uint32_t)midi.read() << 16)
              | ((uint32_t)midi.read() << 8)
              |  (uint32_t)midi.read();
        Serial.print("Tempo change: ");
        Serial.print(60000000 / tempo);
        Serial.println(" BPM");
      } else {
        midi.seek(midi.position() + len);
      }
      continue;
    }

    // SysEx events
    if (status == 0xF0 || status == 0xF7) {
      uint32_t len = deltaTime();
      midi.seek(midi.position() + len);
      continue;
    }

    // Note On
    if ((status & 0xF0) == 0x90) {
      uint8_t note     = midi.read();
      uint8_t velocity = midi.read();
      if (velocity > 0) playNote(note);
      else              releaseNote(note);
    }

    // Note Off
    else if ((status & 0xF0) == 0x80) {
      uint8_t note = midi.read();
      midi.read();
      releaseNote(note);
    }
  }
}

// Populates noteToSolenoidIdx lookup table
void buildNoteMap() {
  for (int n = 0; n < 128; n++) noteToSolenoidIdx[n] = -1;
  for (uint8_t s = 0; s < NUM_STRINGS; s++) {
    for (uint8_t f = 0; f < NUM_FINGERS; f++) {
      uint8_t midiNote = OPEN_STRING_NOTES[s] + FINGER_OFFSETS[f];
      if (midiNote < 128) noteToSolenoidIdx[midiNote] = s * NUM_FINGERS + f;
    }
  }
}

// Looks up string and finger for a MIDI note, releases any conflicting finger on that string, latches new finger down
void playNote(uint8_t midiNote) {
  int idx = noteToSolenoidIdx[midiNote];
  if (idx < 0) {
    Serial.print("Note ");
    Serial.print(noteName(midiNote));
    Serial.println(" not mapped.");
    return;
  }
  uint8_t s = idx / NUM_FINGERS;
  uint8_t f = idx % NUM_FINGERS;
  if (activeFingerOnString[s] >= 0 && activeFingerOnString[s] != (int8_t)f) {
    releaseFingerLatching(s, activeFingerOnString[s]);
  }
  pressFingerLatching(s, f);
  activeFingerOnString[s] = f;
  Serial.print("Note ON  ");
  Serial.print(noteName(midiNote));
  Serial.print(" -> String ");
  Serial.print(s);
  Serial.print(", Finger ");
  Serial.println(f);
}

// Releases the finger for this note if it is currently active
void releaseNote(uint8_t midiNote) {
  int idx = noteToSolenoidIdx[midiNote];
  if (idx < 0) return;
  uint8_t s = idx / NUM_FINGERS;
  uint8_t f = idx % NUM_FINGERS;
  if (activeFingerOnString[s] == (int8_t)f) {
    releaseFingerLatching(s, f);
    activeFingerOnString[s] = -1;
    Serial.print("Note OFF ");
    Serial.print(noteName(midiNote));
    Serial.print(" -> String ");
    Serial.print(s);
    Serial.print(", Finger ");
    Serial.println(f);
  }
}

// Pulses IN1 HIGH, IN2 LOW to extend plunger and latch finger down
void pressFingerLatching(uint8_t str, uint8_t finger) {
  digitalWrite(IN1_PINS[str][finger], HIGH);
  digitalWrite(IN2_PINS[str][finger], LOW);
  delay(LATCH_PULSE_MS);
  digitalWrite(IN1_PINS[str][finger], LOW);
}

// Pulses IN2 HIGH, IN1 LOW to retract plunger and release finger
void releaseFingerLatching(uint8_t str, uint8_t finger) {
  digitalWrite(IN1_PINS[str][finger], LOW);
  digitalWrite(IN2_PINS[str][finger], HIGH);
  delay(LATCH_PULSE_MS);
  digitalWrite(IN2_PINS[str][finger], LOW);
}

// Sends a release pulse to every solenoid — used on startup and shutdown
void releaseAllFingers() {
  for (uint8_t s = 0; s < NUM_STRINGS; s++) {
    for (uint8_t f = 0; f < NUM_FINGERS; f++) {
      releaseFingerLatching(s, f);
      delay(10);
    }
    activeFingerOnString[s] = -1;
  }
}

// Variable length quantity delta time decode — identical to glockenspiel
uint32_t deltaTime() {
  uint8_t bitPull;
  uint32_t value = 0;
  do {
    bitPull = midi.read();
    value   = (value << 7) | (bitPull & 0x7F);
  } while (bitPull & 0x80);
  return value;
}

// Returns note name string from MIDI note number
String noteName(uint8_t midiNote) {
  static const char* names[] = {
    "C", "C#", "D", "D#", "E", "F",
    "F#", "G", "G#", "A", "A#", "B"
  };
  int pitchClass = midiNote % 12;
  int octave     = (midiNote / 12) - 1;
  return String(names[pitchClass]) + String(octave);
}
