#include <Adafruit_Fingerprint.h>
#include <SoftwareSerial.h>

constexpr uint8_t FP_RX_PIN = 2;
constexpr uint8_t FP_TX_PIN = 3;
constexpr uint8_t RELAY_PIN = 8;
constexpr uint8_t RED_LED_PIN = 9;
// Verify the trigger polarity with HardwareSelfTest before enrollment.
constexpr bool RELAY_ACTIVE_LOW = true;
constexpr uint8_t RELAY_ACTIVE_LEVEL = RELAY_ACTIVE_LOW ? LOW : HIGH;
constexpr uint8_t RELAY_INACTIVE_LEVEL = RELAY_ACTIVE_LOW ? HIGH : LOW;

constexpr unsigned long ENROLLMENT_FAILURE_LED_MS = 2000UL;
constexpr uint32_t SENSOR_BAUD_RATE = 57600UL;
constexpr uint32_t HOST_BAUD_RATE = 9600UL;

SoftwareSerial fingerprintSerial(FP_RX_PIN, FP_TX_PIN);
Adafruit_Fingerprint fingerprintSensor(&fingerprintSerial);

char inputBuffer[8];
uint8_t inputLength = 0;
bool inputOverflow = false;

void configureOutputsSafely();
void setEnrollmentOutputsSafe();
void pulseEnrollmentFailure();
void printPrompt();
void consumeSerialInput();
void handleEnrollmentLine();
bool captureAndConvert(uint8_t bufferNumber);
bool fingerprintBelongsToAnotherTemplate(uint8_t templateId);
bool waitForFingerRemoval();
bool enrollTemplate(uint8_t templateId);
void printSensorError(const __FlashStringHelper *stage, uint8_t code);

void setup() {
  configureOutputsSafely();

  Serial.begin(HOST_BAUD_RATE);
  delay(100);
  fingerprintSensor.begin(SENSOR_BAUD_RATE);

  Serial.println(F("Secure AS608 enrollment mode"));
  Serial.println(F("Only template 1 (Administrator) and 2 (Technician) are allowed."));
  Serial.println(F("Normal access authentication is disabled in this sketch."));

  if (!fingerprintSensor.verifyPassword()) {
    Serial.println(F("FATAL: AS608 communication failed."));
    Serial.println(F("Check power, common ground, and crossed TX/RX wiring."));
    digitalWrite(RELAY_PIN, RELAY_INACTIVE_LEVEL);
    digitalWrite(RED_LED_PIN, HIGH);
    while (true) {
      delay(1000);
    }
  }

  Serial.println(F("AS608 communication: OK"));
  printPrompt();
}

void loop() {
  consumeSerialInput();
}

void configureOutputsSafely() {
  // Enrollment must never command simulated access. Preload the inactive
  // relay level before enabling its output driver to avoid a startup pulse.
  digitalWrite(RELAY_PIN, RELAY_INACTIVE_LEVEL);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RED_LED_PIN, LOW);
  pinMode(RED_LED_PIN, OUTPUT);
}

void setEnrollmentOutputsSafe() {
  digitalWrite(RELAY_PIN, RELAY_INACTIVE_LEVEL);
  digitalWrite(RED_LED_PIN, LOW);
}

void pulseEnrollmentFailure() {
  digitalWrite(RELAY_PIN, RELAY_INACTIVE_LEVEL);
  digitalWrite(RED_LED_PIN, HIGH);
  delay(ENROLLMENT_FAILURE_LED_MS);
  digitalWrite(RED_LED_PIN, LOW);
}

void printPrompt() {
  setEnrollmentOutputsSafe();
  Serial.println();
  Serial.println(F("Enter 1 or 2, then send a newline:"));
  Serial.println(F("  1 = Administrator"));
  Serial.println(F("  2 = Technician"));
}

void consumeSerialInput() {
  while (Serial.available() > 0) {
    const char incoming = static_cast<char>(Serial.read());

    if (incoming == '\r') {
      continue;
    }

    if (incoming == '\n') {
      handleEnrollmentLine();
      inputLength = 0;
      inputOverflow = false;
      printPrompt();
      continue;
    }

    if (inputLength < sizeof(inputBuffer) - 1) {
      inputBuffer[inputLength++] = incoming;
    } else {
      inputOverflow = true;
    }
  }
}

void handleEnrollmentLine() {
  if (inputOverflow || inputLength != 1 ||
      (inputBuffer[0] != '1' && inputBuffer[0] != '2')) {
    Serial.println(F("REJECTED: only the exact template ID 1 or 2 is accepted."));
    pulseEnrollmentFailure();
    return;
  }

  const uint8_t templateId =
      inputBuffer[0] == '1' ? static_cast<uint8_t>(1) : static_cast<uint8_t>(2);
  Serial.print(F("Starting enrollment for template "));
  Serial.print(templateId);
  Serial.print(F(" ("));
  Serial.print(templateId == 1 ? F("Administrator") : F("Technician"));
  Serial.println(F(")."));

  if (enrollTemplate(templateId)) {
    setEnrollmentOutputsSafe();
    Serial.print(F("SUCCESS: template "));
    Serial.print(templateId);
    Serial.println(F(" was stored inside the AS608."));
    Serial.println(F("The relay and green access LED were not used during enrollment."));
  } else {
    Serial.println(F("FAILED: no template was stored by this attempt."));
    pulseEnrollmentFailure();
  }
}

bool captureAndConvert(uint8_t bufferNumber) {
  Serial.println(F("Place the required finger flat on the sensor."));

  uint8_t result = FINGERPRINT_NOFINGER;
  while (result == FINGERPRINT_NOFINGER) {
    result = fingerprintSensor.getImage();
    if (result == FINGERPRINT_NOFINGER) {
      delay(50);
    }
  }

  if (result != FINGERPRINT_OK) {
    printSensorError(F("capture"), result);
    return false;
  }

  result = fingerprintSensor.image2Tz(bufferNumber);
  if (result != FINGERPRINT_OK) {
    printSensorError(F("feature conversion"), result);
    return false;
  }

  Serial.println(F("Fingerprint sample captured."));
  return true;
}

bool waitForFingerRemoval() {
  Serial.println(F("Remove the finger completely."));

  while (true) {
    const uint8_t result = fingerprintSensor.getImage();
    if (result == FINGERPRINT_NOFINGER) {
      delay(250);
      return true;
    }
    if (result != FINGERPRINT_OK) {
      printSensorError(F("finger-removal check"), result);
      return false;
    }
    delay(50);
  }
}

bool fingerprintBelongsToAnotherTemplate(uint8_t templateId) {
  const uint8_t result = fingerprintSensor.fingerFastSearch();
  if (result == FINGERPRINT_NOTFOUND) {
    return false;
  }
  if (result != FINGERPRINT_OK) {
    printSensorError(F("duplicate-template check"), result);
    return true;
  }
  if (fingerprintSensor.fingerID == templateId) {
    Serial.println(F("This finger already belongs to the selected template; it may be refreshed."));
    return false;
  }

  Serial.print(F("REJECTED: this finger is already assigned to template "));
  Serial.print(fingerprintSensor.fingerID);
  Serial.println(F(". Use a different physical finger for the other identity."));
  return true;
}

bool enrollTemplate(uint8_t templateId) {
  setEnrollmentOutputsSafe();

  if (!captureAndConvert(1)) {
    return false;
  }
  if (fingerprintBelongsToAnotherTemplate(templateId)) {
    return false;
  }
  if (!waitForFingerRemoval()) {
    return false;
  }

  Serial.println(F("Place the SAME finger on the sensor a second time."));
  if (!captureAndConvert(2)) {
    return false;
  }

  uint8_t result = fingerprintSensor.createModel();
  if (result != FINGERPRINT_OK) {
    printSensorError(F("sample comparison/model creation"), result);
    return false;
  }

  result = fingerprintSensor.storeModel(templateId);
  if (result != FINGERPRINT_OK) {
    printSensorError(F("template storage"), result);
    return false;
  }

  return true;
}

void printSensorError(const __FlashStringHelper *stage, uint8_t code) {
  Serial.print(F("Sensor error during "));
  Serial.print(stage);
  Serial.print(F("; code="));
  Serial.println(code);
}
