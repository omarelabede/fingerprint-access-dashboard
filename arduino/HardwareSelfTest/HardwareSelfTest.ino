#include <Adafruit_Fingerprint.h>
#include <SoftwareSerial.h>

constexpr uint8_t FP_RX_PIN = 2;
constexpr uint8_t FP_TX_PIN = 3;
constexpr uint8_t RELAY_PIN = 8;
constexpr uint8_t RED_LED_PIN = 9;
// Change this single setting only after checking the actual relay module.
constexpr bool RELAY_ACTIVE_LOW = true;
constexpr uint8_t RELAY_ACTIVE_LEVEL = RELAY_ACTIVE_LOW ? LOW : HIGH;
constexpr uint8_t RELAY_INACTIVE_LEVEL = RELAY_ACTIVE_LOW ? HIGH : LOW;
constexpr uint32_t SENSOR_BAUD_RATE = 57600UL;
constexpr uint32_t HOST_BAUD_RATE = 9600UL;

SoftwareSerial fingerprintSerial(FP_RX_PIN, FP_TX_PIN);
Adafruit_Fingerprint fingerprintSensor(&fingerprintSerial);

void configureOutputsSafely();
void setOutputsSafe();
void clearPendingSerialInput();
bool askForConfirmation(const __FlashStringHelper *question);
bool testRelayAndGreenLed();
bool testRedLed();

void setup() {
  configureOutputsSafely();

  Serial.begin(HOST_BAUD_RATE);
  delay(1000);

  Serial.println(F("Fingerprint relay/LED prototype - hardware self-test"));
  Serial.println(F("This sketch does not enroll or authenticate fingerprints."));
  Serial.println(F("Use Serial Monitor at 9600 baud with any line ending."));
  Serial.print(F("Configured relay trigger: "));
  Serial.println(RELAY_ACTIVE_LOW ? F("ACTIVE LOW") : F("ACTIVE HIGH"));
  Serial.println(F("If the relay or green LED is active before requested, enter N,"));
  Serial.println(F("disconnect power, and reverse RELAY_ACTIVE_LOW."));
  Serial.println();

  fingerprintSensor.begin(SENSOR_BAUD_RATE);
  const bool sensorPassed = fingerprintSensor.verifyPassword();
  if (sensorPassed) {
    Serial.println(F("SENSOR_COMMUNICATION: PASS"));
  } else {
    Serial.println(F("SENSOR_COMMUNICATION: FAIL"));
    Serial.println(F("Check sensor voltage, common ground, and crossed TX/RX."));
  }

  const bool relayPassed = testRelayAndGreenLed();
  const bool redPassed = testRedLed();

  setOutputsSafe();
  Serial.println();
  Serial.println(F("Self-test summary"));
  Serial.print(F("  Sensor: "));
  Serial.println(sensorPassed ? F("PASS") : F("FAIL"));
  Serial.print(F("  Relay and green LED: "));
  Serial.println(relayPassed ? F("PASS") : F("FAIL"));
  Serial.print(F("  Red LED: "));
  Serial.println(redPassed ? F("PASS") : F("FAIL"));

  if (sensorPassed && relayPassed && redPassed) {
    Serial.println(F("OVERALL: PASS"));
  } else {
    Serial.println(F("OVERALL: FAIL"));
  }
  Serial.println(F("Relay is inactive and both LEDs are now OFF. Reset to repeat."));
}

void loop() {
  // The dedicated self-test runs once after reset.
}

void configureOutputsSafely() {
  // Preload inactive levels before enabling the output drivers. This avoids a
  // software-created relay pulse when an active-low module is used.
  digitalWrite(RELAY_PIN, RELAY_INACTIVE_LEVEL);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RED_LED_PIN, LOW);
  pinMode(RED_LED_PIN, OUTPUT);
}

void setOutputsSafe() {
  digitalWrite(RELAY_PIN, RELAY_INACTIVE_LEVEL);
  digitalWrite(RED_LED_PIN, LOW);
}

bool testRelayAndGreenLed() {
  setOutputsSafe();
  const bool inactiveBefore = askForConfirmation(
      F("Is the relay inactive and the relay-controlled green LED OFF? Enter Y or N."));

  digitalWrite(RELAY_PIN, RELAY_ACTIVE_LEVEL);
  const bool active = askForConfirmation(
      F("Did the relay switch and ONLY the green LED turn ON? Enter Y or N."));

  digitalWrite(RELAY_PIN, RELAY_INACTIVE_LEVEL);
  const bool inactiveAfter = askForConfirmation(
      F("Did the relay release and the green LED turn OFF? Enter Y or N."));

  setOutputsSafe();
  return inactiveBefore && active && inactiveAfter;
}

bool testRedLed() {
  setOutputsSafe();
  digitalWrite(RED_LED_PIN, HIGH);
  const bool passed = askForConfirmation(
      F("Is ONLY the directly controlled red LED ON? Enter Y or N."));
  setOutputsSafe();
  return passed;
}

bool askForConfirmation(const __FlashStringHelper *question) {
  clearPendingSerialInput();
  Serial.println();
  Serial.println(question);

  while (true) {
    if (Serial.available() == 0) {
      continue;
    }

    const char response = static_cast<char>(Serial.read());
    if (response == 'Y' || response == 'y') {
      Serial.println(F("Recorded: PASS"));
      return true;
    }
    if (response == 'N' || response == 'n') {
      Serial.println(F("Recorded: FAIL"));
      return false;
    }
  }
}

void clearPendingSerialInput() {
  while (Serial.available() > 0) {
    Serial.read();
  }
}
