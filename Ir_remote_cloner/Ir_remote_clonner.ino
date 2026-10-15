
#include <Arduino.h>
#include <Wire.h>

// Must be defined before including IRremote, otherwise the library picks
// its own buffer size and may use too much RAM on the ATmega32U4.
#define RAW_BUFFER_LENGTH 200
#include <IRremote.hpp>

#define IR_RX_PIN 12
#define IR_TX_PIN 6
#define BTN_LEARN 10
#define BTN_SEND 8

#define EEPROM_ADDR 0x50
#define EEPROM_SIZE 1024
#define EEPROM_PAGE 16
#define MAGIC 0xA7
#define MAX_TIMINGS 200
// Frames shorter than this are noise or incomplete, not a real key press.
#define MIN_TIMINGS 10

uint16_t timings[MAX_TIMINGS];
uint16_t timingCount = 0;
bool hasCommand = false;

bool learning = false;
bool lastLearn = HIGH;
bool lastSend = HIGH;

// ---------------------------------------------------------------------------
// EEPROM (24LC08B)
// ---------------------------------------------------------------------------

// Returns the I2C address for a memory address: the 24LC08B is split
// into 4 blocks of 256 bytes, each answering at 0x50..0x53.
uint8_t eepromDevice(uint16_t addr) {
  return (uint8_t)(EEPROM_ADDR | ((addr >> 8) & 0x03));
}

// Waits until the chip finishes its internal write cycle (ACK polling).
// The chip does not acknowledge its address while it is busy writing.
bool eepromWaitReady(uint8_t dev) {
  unsigned long start = millis();

  while (millis() - start < 20) {
    Wire.beginTransmission(dev);
    if (Wire.endTransmission() == 0) return true;
  }

  return false;
}

// Writes a buffer in 16-byte pages, never crossing a page boundary.
bool eepromWriteBlock(uint16_t addr, const uint8_t *data, uint16_t len) {
  if (addr + len > EEPROM_SIZE) return false;

  while (len > 0) {
    uint8_t chunk = EEPROM_PAGE - (addr % EEPROM_PAGE);
    if (chunk > len) chunk = len;

    uint8_t dev = eepromDevice(addr);

    Wire.beginTransmission(dev);
    Wire.write((uint8_t)(addr & 0xFF));
    Wire.write(data, chunk);

    if (Wire.endTransmission() != 0) return false;
    if (!eepromWaitReady(dev)) return false;

    addr += chunk;
    data += chunk;
    len -= chunk;
  }

  return true;
}

// Writes a single byte.
bool eepromWriteByte(uint16_t addr, uint8_t value) {
  return eepromWriteBlock(addr, &value, 1);
}

// Reads a single byte.
bool eepromReadByte(uint16_t addr, uint8_t &value) {
  if (addr >= EEPROM_SIZE) return false;

  uint8_t dev = eepromDevice(addr);

  Wire.beginTransmission(dev);
  Wire.write((uint8_t)(addr & 0xFF));

  if (Wire.endTransmission(false) != 0) return false;

  if (Wire.requestFrom(dev, (uint8_t)1) != 1) return false;

  value = Wire.read();
  return true;
}

// Write/read test on the last byte (outside the command area).
// The original value is restored afterwards.
bool testEEPROM() {
  const uint16_t testAddr = 1023;
  uint8_t original, result;

  if (!eepromReadByte(testAddr, original)) return false;

  uint8_t testValue = original ^ 0x5A;

  if (!eepromWriteByte(testAddr, testValue)) return false;
  if (!eepromReadByte(testAddr, result)) return false;

  eepromWriteByte(testAddr, original);
  return result == testValue;
}

// ---------------------------------------------------------------------------
// Stored command
// Layout: [0] MAGIC, [1..2] timingCount, [3..] timings (little-endian).
// ---------------------------------------------------------------------------

// Saves the current timings. MAGIC is cleared first and written last,
// so an interrupted save never leaves a half-written command marked valid.
bool saveCommand() {
  if (!eepromWriteByte(0, 0)) return false;

  if (!eepromWriteBlock(1, (const uint8_t *)&timingCount, 2)) return false;
  if (!eepromWriteBlock(3, (const uint8_t *)timings, timingCount * 2)) return false;

  return eepromWriteByte(0, MAGIC);
}

// Loads the stored command into RAM. Returns false if none is stored.
bool loadCommand() {
  uint8_t value;

  if (!eepromReadByte(0, value) || value != MAGIC) return false;

  uint8_t lo, hi;

  if (!eepromReadByte(1, lo)) return false;
  if (!eepromReadByte(2, hi)) return false;

  uint16_t count = lo | ((uint16_t)hi << 8);

  if (count == 0 || count > MAX_TIMINGS) return false;

  for (uint16_t i = 0; i < count; i++) {
    uint16_t addr = 3 + i * 2;

    if (!eepromReadByte(addr, lo)) return false;
    if (!eepromReadByte(addr + 1, hi)) return false;

    timings[i] = lo | ((uint16_t)hi << 8);
  }

  timingCount = count;
  return true;
}

// ---------------------------------------------------------------------------
// IR
// ---------------------------------------------------------------------------

// Copies the received frame into timings[] (in microseconds), compensating
// for the receiver reporting marks too long and spaces too short.
void copyReceivedFrame(uint16_t count) {
  timingCount = count;

  for (uint16_t i = 0; i < timingCount; i++) {
    uint16_t t = (uint16_t)IrReceiver.irparams.rawbuf[i + 1] * MICROS_PER_TICK;

    if (i % 2 == 0) {
      if (t > MARK_EXCESS_MICROS) t -= MARK_EXCESS_MICROS;
    } else {
      t += MARK_EXCESS_MICROS;
    }

    timings[i] = t;
  }
}

// Handles a received frame while in learn mode.
void handleLearnFrame() {
  uint16_t count = IrReceiver.irparams.rawlen - 1;
  uint8_t flags = IrReceiver.decodedIRData.flags;

  // Noise and repeat frames are ignored silently; keep waiting for a key.
  if (IrReceiver.irparams.rawlen < 2 || count < MIN_TIMINGS ||
      (flags & IRDATA_FLAGS_IS_REPEAT)) {
    return;
  }

  if (count > MAX_TIMINGS || (flags & IRDATA_FLAGS_WAS_OVERFLOW)) {
    Serial.println(F("Semnal prea lung, nu poate fi salvat."));
    return;
  }

  copyReceivedFrame(count);
  hasCommand = saveCommand();

  if (!hasCommand) {
    Serial.println(F("EROARE: comanda nu a putut fi salvata in EEPROM."));
    return;
  }

  Serial.print(F("Comanda salvata: "));
  Serial.print(getProtocolString(IrReceiver.decodedIRData.protocol));

  if (IrReceiver.decodedIRData.protocol != UNKNOWN) {
    Serial.print(F(" adresa=0x"));
    Serial.print(IrReceiver.decodedIRData.address, HEX);
    Serial.print(F(" comanda=0x"));
    Serial.print(IrReceiver.decodedIRData.command, HEX);
  }

  Serial.println();
  learning = false;
}

// Sends the stored command through the IR LED. The receiver is paused
// so it does not pick up our own transmission.
void sendCommand() {
  if (!hasCommand) {
    Serial.println(F("Nu exista comanda memorata."));
    return;
  }

  IrReceiver.stop();
  IrSender.sendRaw(timings, timingCount, 38);
  IrReceiver.start();

  Serial.println(F("Comanda trimisa."));
}

// Returns true once per press (falling edge with 30 ms debounce).
bool buttonPressed(uint8_t pin, bool &last) {
  bool now = digitalRead(pin);
  bool pressed = false;

  if (last == HIGH && now == LOW) {
    delay(30);
    pressed = digitalRead(pin) == LOW;
  }

  last = now;
  return pressed;
}

// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(1500);

  pinMode(BTN_LEARN, INPUT_PULLUP);
  pinMode(BTN_SEND, INPUT_PULLUP);

  Wire.begin();
  Wire.setClock(100000);

  IrReceiver.begin(IR_RX_PIN, ENABLE_LED_FEEDBACK);
  IrSender.begin(IR_TX_PIN);

  Serial.println(F("=== TELECOMANDA IR ==="));
  Serial.println(testEEPROM() ? F("EEPROM OK") : F("EROARE EEPROM: verifica conexiunile I2C"));

  hasCommand = loadCommand();
  Serial.println(hasCommand ? F("Comanda memorata gasita.") : F("Nu exista comanda memorata."));
  Serial.println(F("LEARN = D10 | SEND = D8"));
}

void loop() {
  if (buttonPressed(BTN_LEARN, lastLearn)) {
    // Drop any stale frame captured while we were not learning.
    IrReceiver.resume();
    learning = true;
    Serial.println(F("LEARN: apasa o tasta pe telecomanda..."));
  }

  if (IrReceiver.decode()) {
    if (learning) handleLearnFrame();
    IrReceiver.resume();
  }

  if (buttonPressed(BTN_SEND, lastSend)) {
    Serial.println(F("SEND"));
    sendCommand();
  }
}
