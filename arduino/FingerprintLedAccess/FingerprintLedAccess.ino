#include <Adafruit_Fingerprint.h>
#include <EEPROM.h>
#include <SoftwareSerial.h>
#include <stdint.h>

constexpr uint8_t FP_RX_PIN = 2;
constexpr uint8_t FP_TX_PIN = 3;
constexpr uint8_t RELAY_PIN = 8;
constexpr uint8_t RED_LED_PIN = 9;
// Verify the trigger polarity with HardwareSelfTest before normal operation.
constexpr bool RELAY_ACTIVE_LOW = true;
constexpr uint8_t RELAY_ACTIVE_LEVEL = RELAY_ACTIVE_LOW ? LOW : HIGH;
constexpr uint8_t RELAY_INACTIVE_LEVEL = RELAY_ACTIVE_LOW ? HIGH : LOW;
constexpr uint32_t SENSOR_BAUD_RATE = 57600UL;
constexpr uint32_t HOST_BAUD_RATE = 9600UL;

constexpr unsigned long ACCESS_GRANTED_DURATION_MS = 5000UL;
constexpr unsigned long ACCESS_DENIED_DURATION_MS = 2000UL;
constexpr unsigned long LOCKOUT_DURATION_MS = 120000UL;
constexpr unsigned long SENSOR_POLL_INTERVAL_MS = 75UL;
constexpr unsigned long REMOVAL_POLL_INTERVAL_MS = 100UL;

constexpr uint8_t MAX_FAILED_ATTEMPTS = 4;
constexpr uint32_t BOOT_RECORD_MAGIC = 0x46534153UL;
constexpr int EEPROM_BOOT_RECORD_ADDRESS = 0;
constexpr char DEVICE_ID[] = "FSAS-LED-01";

enum class SystemState : uint8_t {
  IDLE,
  ACCESS_GRANTED,
  ACCESS_DENIED,
  LOCKOUT,
  SENSOR_ERROR
};

enum class EventType : uint8_t {
  SYSTEM_READY,
  ACCESS,
  LOCKOUT_START,
  LOCKOUT_END,
  SYSTEM_ERROR
};

enum class AuthenticationResult : uint8_t {
  SUCCESS,
  FAILED,
  ERROR,
  LOCKOUT,
  READY
};

enum class AccessStatus : uint8_t {
  GRANTED,
  DENIED,
  LOCKED,
  IDLE
};

enum class IndicatorState : uint8_t {
  GREEN,
  RED,
  OFF
};

enum class UserName : uint8_t {
  ADMINISTRATOR,
  TECHNICIAN,
  UNKNOWN,
  SYSTEM
};

enum class EventReason : uint8_t {
  NONE,
  FINGERPRINT_NOT_MATCHED,
  UNAPPROVED_TEMPLATE_ID,
  FOUR_FAILED_ATTEMPTS,
  TIME_EXPIRED,
  SENSOR_INITIALIZATION_FAILED,
  SENSOR_COMMUNICATION_ERROR,
  IMAGE_CAPTURE_ERROR,
  FEATURE_EXTRACTION_ERROR,
  SENSOR_PROCESSING_ERROR
};

struct BootRecord {
  uint32_t magic;
  uint32_t bootId;
};

SoftwareSerial fingerprintSerial(FP_RX_PIN, FP_TX_PIN);
Adafruit_Fingerprint fingerprintSensor(&fingerprintSerial);

SystemState systemState = SystemState::IDLE;
uint32_t currentBootId = 0;
uint32_t nextEventSequence = 1;
uint8_t consecutiveFailedAttempts = 0;
unsigned long stateStartedAtMs = 0;
unsigned long lastSensorPollAtMs = 0;
unsigned long lastRemovalPollAtMs = 0;
bool fingerRemovalRequired = false;

uint32_t loadAndIncrementBootId();
void configureOutputsSafely();
void setIndicators(IndicatorState indicator);
void printEventType(EventType value);
void printAuthenticationResult(AuthenticationResult value);
void printAccessStatus(AccessStatus value);
void printIndicatorState(IndicatorState value);
void printUserName(UserName value);
void printEventReason(EventReason value);
void emitEvent(
    EventType eventType,
    int16_t templateId,
    UserName userName,
    uint16_t confidence,
    AuthenticationResult authenticationResult,
    AccessStatus accessStatus,
    EventReason reason,
    uint8_t failedCount,
    unsigned long latencyMs,
    IndicatorState indicatorState);
bool isApprovedTemplate(uint16_t templateId);
UserName userForTemplate(uint16_t templateId);
EventReason imageErrorReason(uint8_t sensorResult);
EventReason processingErrorReason(uint8_t sensorResult);
void enterGrantedState(uint16_t templateId, uint16_t confidence, unsigned long latencyMs);
void recordDeniedAttempt(
    int16_t templateId,
    uint16_t confidence,
    EventReason reason,
    unsigned long latencyMs);
void enterTransientSensorError(EventReason reason, unsigned long latencyMs);
void endLockout();
void pollForFingerRemoval(unsigned long nowMs);
void pollFingerprintSensor();

void setup() {
  configureOutputsSafely();

  Serial.begin(HOST_BAUD_RATE);
  delay(100);

  currentBootId = loadAndIncrementBootId();
  fingerprintSensor.begin(SENSOR_BAUD_RATE);

  if (!fingerprintSensor.verifyPassword()) {
    systemState = SystemState::SENSOR_ERROR;
    setIndicators(IndicatorState::RED);
    emitEvent(
        EventType::SYSTEM_ERROR,
        -1,
        UserName::SYSTEM,
        0,
        AuthenticationResult::ERROR,
        AccessStatus::LOCKED,
        EventReason::SENSOR_INITIALIZATION_FAILED,
        0,
        0,
        IndicatorState::RED);
    return;
  }

  systemState = SystemState::IDLE;
  setIndicators(IndicatorState::OFF);
  emitEvent(
      EventType::SYSTEM_READY,
      -1,
      UserName::SYSTEM,
      0,
      AuthenticationResult::READY,
      AccessStatus::IDLE,
      EventReason::NONE,
      0,
      0,
      IndicatorState::OFF);
}

void loop() {
  const unsigned long nowMs = millis();

  if (systemState == SystemState::SENSOR_ERROR) {
    return;
  }

  if (systemState == SystemState::LOCKOUT) {
    if (nowMs - stateStartedAtMs >= LOCKOUT_DURATION_MS) {
      endLockout();
    }
    return;
  }

  if (systemState == SystemState::ACCESS_GRANTED) {
    if (nowMs - stateStartedAtMs >= ACCESS_GRANTED_DURATION_MS) {
      setIndicators(IndicatorState::OFF);
      systemState = SystemState::IDLE;
    }
    return;
  }

  if (systemState == SystemState::ACCESS_DENIED) {
    if (nowMs - stateStartedAtMs >= ACCESS_DENIED_DURATION_MS) {
      setIndicators(IndicatorState::OFF);
      systemState = SystemState::IDLE;
    }
    return;
  }

  if (fingerRemovalRequired) {
    pollForFingerRemoval(nowMs);
    return;
  }

  if (nowMs - lastSensorPollAtMs >= SENSOR_POLL_INTERVAL_MS) {
    lastSensorPollAtMs = nowMs;
    pollFingerprintSensor();
  }
}

uint32_t loadAndIncrementBootId() {
  BootRecord record;
  EEPROM.get(EEPROM_BOOT_RECORD_ADDRESS, record);

  if (record.magic != BOOT_RECORD_MAGIC ||
      record.bootId == 0 ||
      record.bootId == UINT32_MAX) {
    record.magic = BOOT_RECORD_MAGIC;
    record.bootId = 1;
  } else {
    ++record.bootId;
  }

  EEPROM.put(EEPROM_BOOT_RECORD_ADDRESS, record);
  return record.bootId;
}

void configureOutputsSafely() {
  // Preload the inactive levels before enabling the output drivers. This avoids
  // a software-created relay pulse when an active-low module is used.
  digitalWrite(RELAY_PIN, RELAY_INACTIVE_LEVEL);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RED_LED_PIN, LOW);
  pinMode(RED_LED_PIN, OUTPUT);
}

void setIndicators(IndicatorState indicator) {
  // The green LED is a low-voltage load on the relay's COM/NO contacts. GREEN
  // therefore means "relay commanded active", not a direct Arduino LED pin.
  digitalWrite(
      RELAY_PIN,
      indicator == IndicatorState::GREEN
          ? RELAY_ACTIVE_LEVEL
          : RELAY_INACTIVE_LEVEL);
  digitalWrite(
      RED_LED_PIN,
      indicator == IndicatorState::RED ? HIGH : LOW);
}

void printEventType(EventType value) {
  switch (value) {
    case EventType::SYSTEM_READY:
      Serial.print(F("SYSTEM_READY"));
      break;
    case EventType::ACCESS:
      Serial.print(F("ACCESS"));
      break;
    case EventType::LOCKOUT_START:
      Serial.print(F("LOCKOUT_START"));
      break;
    case EventType::LOCKOUT_END:
      Serial.print(F("LOCKOUT_END"));
      break;
    case EventType::SYSTEM_ERROR:
      Serial.print(F("SYSTEM_ERROR"));
      break;
  }
}

void printAuthenticationResult(AuthenticationResult value) {
  switch (value) {
    case AuthenticationResult::SUCCESS:
      Serial.print(F("SUCCESS"));
      break;
    case AuthenticationResult::FAILED:
      Serial.print(F("FAILED"));
      break;
    case AuthenticationResult::ERROR:
      Serial.print(F("ERROR"));
      break;
    case AuthenticationResult::LOCKOUT:
      Serial.print(F("LOCKOUT"));
      break;
    case AuthenticationResult::READY:
      Serial.print(F("READY"));
      break;
  }
}

void printAccessStatus(AccessStatus value) {
  switch (value) {
    case AccessStatus::GRANTED:
      Serial.print(F("GRANTED"));
      break;
    case AccessStatus::DENIED:
      Serial.print(F("DENIED"));
      break;
    case AccessStatus::LOCKED:
      Serial.print(F("LOCKED"));
      break;
    case AccessStatus::IDLE:
      Serial.print(F("IDLE"));
      break;
  }
}

void printIndicatorState(IndicatorState value) {
  switch (value) {
    case IndicatorState::GREEN:
      Serial.print(F("GREEN"));
      break;
    case IndicatorState::RED:
      Serial.print(F("RED"));
      break;
    case IndicatorState::OFF:
      Serial.print(F("OFF"));
      break;
  }
}

void printUserName(UserName value) {
  switch (value) {
    case UserName::ADMINISTRATOR:
      Serial.print(F("ADMINISTRATOR"));
      break;
    case UserName::TECHNICIAN:
      Serial.print(F("TECHNICIAN"));
      break;
    case UserName::UNKNOWN:
      Serial.print(F("UNKNOWN"));
      break;
    case UserName::SYSTEM:
      Serial.print(F("SYSTEM"));
      break;
  }
}

void printEventReason(EventReason value) {
  switch (value) {
    case EventReason::NONE:
      Serial.print(F("NONE"));
      break;
    case EventReason::FINGERPRINT_NOT_MATCHED:
      Serial.print(F("FINGERPRINT_NOT_MATCHED"));
      break;
    case EventReason::UNAPPROVED_TEMPLATE_ID:
      Serial.print(F("UNAPPROVED_TEMPLATE_ID"));
      break;
    case EventReason::FOUR_FAILED_ATTEMPTS:
      Serial.print(F("FOUR_FAILED_ATTEMPTS"));
      break;
    case EventReason::TIME_EXPIRED:
      Serial.print(F("TIME_EXPIRED"));
      break;
    case EventReason::SENSOR_INITIALIZATION_FAILED:
      Serial.print(F("SENSOR_INITIALIZATION_FAILED"));
      break;
    case EventReason::SENSOR_COMMUNICATION_ERROR:
      Serial.print(F("SENSOR_COMMUNICATION_ERROR"));
      break;
    case EventReason::IMAGE_CAPTURE_ERROR:
      Serial.print(F("IMAGE_CAPTURE_ERROR"));
      break;
    case EventReason::FEATURE_EXTRACTION_ERROR:
      Serial.print(F("FEATURE_EXTRACTION_ERROR"));
      break;
    case EventReason::SENSOR_PROCESSING_ERROR:
      Serial.print(F("SENSOR_PROCESSING_ERROR"));
      break;
  }
}

void emitEvent(
    EventType eventType,
    int16_t templateId,
    UserName userName,
    uint16_t confidence,
    AuthenticationResult authenticationResult,
    AccessStatus accessStatus,
    EventReason reason,
    uint8_t failedCount,
    unsigned long latencyMs,
    IndicatorState indicatorState) {
  Serial.print(F("EVT|"));
  Serial.print(DEVICE_ID);
  Serial.print('|');
  Serial.print(currentBootId);
  Serial.print('|');
  Serial.print(nextEventSequence++);
  Serial.print('|');
  Serial.print(millis());
  Serial.print('|');
  printEventType(eventType);
  Serial.print('|');
  Serial.print(templateId);
  Serial.print('|');
  printUserName(userName);
  Serial.print('|');
  Serial.print(confidence);
  Serial.print('|');
  printAuthenticationResult(authenticationResult);
  Serial.print('|');
  printAccessStatus(accessStatus);
  Serial.print('|');
  printEventReason(reason);
  Serial.print('|');
  Serial.print(failedCount);
  Serial.print('|');
  Serial.print(latencyMs);
  Serial.print('|');
  printIndicatorState(indicatorState);
  Serial.println();
}

bool isApprovedTemplate(uint16_t templateId) {
  return templateId == 1 || templateId == 2;
}

UserName userForTemplate(uint16_t templateId) {
  if (templateId == 1) {
    return UserName::ADMINISTRATOR;
  }
  if (templateId == 2) {
    return UserName::TECHNICIAN;
  }
  return UserName::UNKNOWN;
}

EventReason imageErrorReason(uint8_t sensorResult) {
  if (sensorResult == FINGERPRINT_PACKETRECIEVEERR) {
    return EventReason::SENSOR_COMMUNICATION_ERROR;
  }
  if (sensorResult == FINGERPRINT_IMAGEFAIL) {
    return EventReason::IMAGE_CAPTURE_ERROR;
  }
  return EventReason::SENSOR_PROCESSING_ERROR;
}

EventReason processingErrorReason(uint8_t sensorResult) {
  if (sensorResult == FINGERPRINT_PACKETRECIEVEERR) {
    return EventReason::SENSOR_COMMUNICATION_ERROR;
  }
  if (sensorResult == FINGERPRINT_IMAGEMESS ||
      sensorResult == FINGERPRINT_FEATUREFAIL ||
      sensorResult == FINGERPRINT_INVALIDIMAGE) {
    return EventReason::FEATURE_EXTRACTION_ERROR;
  }
  return EventReason::SENSOR_PROCESSING_ERROR;
}

void enterGrantedState(
    uint16_t templateId,
    uint16_t confidence,
    unsigned long latencyMs) {
  consecutiveFailedAttempts = 0;
  setIndicators(IndicatorState::GREEN);
  systemState = SystemState::ACCESS_GRANTED;
  stateStartedAtMs = millis();

  emitEvent(
      EventType::ACCESS,
      static_cast<int16_t>(templateId),
      userForTemplate(templateId),
      confidence,
      AuthenticationResult::SUCCESS,
      AccessStatus::GRANTED,
      EventReason::NONE,
      0,
      latencyMs,
      IndicatorState::GREEN);
}

void recordDeniedAttempt(
    int16_t templateId,
    uint16_t confidence,
    EventReason reason,
    unsigned long latencyMs) {
  if (consecutiveFailedAttempts < MAX_FAILED_ATTEMPTS) {
    ++consecutiveFailedAttempts;
  }

  setIndicators(IndicatorState::RED);
  emitEvent(
      EventType::ACCESS,
      templateId,
      UserName::UNKNOWN,
      confidence,
      AuthenticationResult::FAILED,
      AccessStatus::DENIED,
      reason,
      consecutiveFailedAttempts,
      latencyMs,
      IndicatorState::RED);

  stateStartedAtMs = millis();
  if (consecutiveFailedAttempts >= MAX_FAILED_ATTEMPTS) {
    systemState = SystemState::LOCKOUT;
    emitEvent(
        EventType::LOCKOUT_START,
        -1,
        UserName::UNKNOWN,
        0,
        AuthenticationResult::LOCKOUT,
        AccessStatus::LOCKED,
        EventReason::FOUR_FAILED_ATTEMPTS,
        consecutiveFailedAttempts,
        0,
        IndicatorState::RED);
  } else {
    systemState = SystemState::ACCESS_DENIED;
  }
}

void enterTransientSensorError(EventReason reason, unsigned long latencyMs) {
  setIndicators(IndicatorState::RED);
  systemState = SystemState::ACCESS_DENIED;
  stateStartedAtMs = millis();

  emitEvent(
      EventType::SYSTEM_ERROR,
      -1,
      UserName::SYSTEM,
      0,
      AuthenticationResult::ERROR,
      AccessStatus::DENIED,
      reason,
      consecutiveFailedAttempts,
      latencyMs,
      IndicatorState::RED);
}

void endLockout() {
  consecutiveFailedAttempts = 0;
  setIndicators(IndicatorState::OFF);
  systemState = SystemState::IDLE;
  fingerRemovalRequired = true;

  emitEvent(
      EventType::LOCKOUT_END,
      -1,
      UserName::SYSTEM,
      0,
      AuthenticationResult::READY,
      AccessStatus::IDLE,
      EventReason::TIME_EXPIRED,
      0,
      0,
      IndicatorState::OFF);
}

void pollForFingerRemoval(unsigned long nowMs) {
  if (nowMs - lastRemovalPollAtMs < REMOVAL_POLL_INTERVAL_MS) {
    return;
  }
  lastRemovalPollAtMs = nowMs;

  const unsigned long startedAtMs = millis();
  const uint8_t result = fingerprintSensor.getImage();
  if (result == FINGERPRINT_NOFINGER) {
    fingerRemovalRequired = false;
    return;
  }
  if (result != FINGERPRINT_OK) {
    enterTransientSensorError(
        imageErrorReason(result),
        millis() - startedAtMs);
  }
}

void pollFingerprintSensor() {
  const unsigned long attemptStartedAtMs = millis();
  uint8_t result = fingerprintSensor.getImage();

  if (result == FINGERPRINT_NOFINGER) {
    return;
  }
  if (result != FINGERPRINT_OK) {
    fingerRemovalRequired = true;
    enterTransientSensorError(
        imageErrorReason(result),
        millis() - attemptStartedAtMs);
    return;
  }

  fingerRemovalRequired = true;
  result = fingerprintSensor.image2Tz(1);
  if (result != FINGERPRINT_OK) {
    enterTransientSensorError(
        processingErrorReason(result),
        millis() - attemptStartedAtMs);
    return;
  }

  result = fingerprintSensor.fingerFastSearch();
  const unsigned long latencyMs = millis() - attemptStartedAtMs;

  if (result == FINGERPRINT_NOTFOUND) {
    recordDeniedAttempt(
        -1,
        0,
        EventReason::FINGERPRINT_NOT_MATCHED,
        latencyMs);
    return;
  }

  if (result != FINGERPRINT_OK) {
    enterTransientSensorError(
        processingErrorReason(result),
        latencyMs);
    return;
  }

  const uint16_t matchedTemplateId = fingerprintSensor.fingerID;
  const uint16_t confidence = fingerprintSensor.confidence;
  if (!isApprovedTemplate(matchedTemplateId)) {
    recordDeniedAttempt(
        static_cast<int16_t>(matchedTemplateId),
        confidence,
        EventReason::UNAPPROVED_TEMPLATE_ID,
        latencyMs);
    return;
  }

  enterGrantedState(matchedTemplateId, confidence, latencyMs);
}
