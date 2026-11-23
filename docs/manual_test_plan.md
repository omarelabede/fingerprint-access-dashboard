# Manual test plan

## Rules for recording evidence

- Perform tests on the real prototype unless a row explicitly targets host
  software.
- Use a stopwatch or timestamped video for relay/LED-duration and
  lockout-duration checks.
- Preserve complete newline-terminated EVT lines when serial evidence is needed.
- Do not infer a biometric result from a synthetic software test.
- The Actual Result, Pass/Fail, Evidence Filename, and Notes fields below are
  intentionally empty. Complete them only after running the test.
- Power the board off before disconnecting or reconnecting sensor wires.

## Test cases

| Test ID | Purpose | Steps | Expected Result | Actual Result | Pass/Fail | Evidence Filename | Notes |
|---|---|---|---|---|---|---|---|
| MT-00 | Verify relay polarity and switched green circuit | Keep the electronic lock and 12 V adapter disconnected; upload HardwareSelfTest; follow its inactive, active, and release prompts; if necessary, power down and reverse `RELAY_ACTIVE_LOW` consistently in all three sketches before repeating. | The configured inactive level releases the relay and keeps green off; the active level switches the relay and green on through COM/NO; returning inactive releases it and turns green off. |  |  |  |  |
| MT-01 | Verify safe Arduino startup and reset | After MT-00 passes, upload FingerprintLedAccess; remove any finger; reset the Uno; observe the relay and both LEDs from reset until SYSTEM_READY. | No simulated unlock pulse is observed; relay and green remain inactive and red is off in idle after successful initialization. |  |  |  |  |
| MT-02 | Verify sensor initialization succeeds | Wire the AS608 as documented; reset the board; capture USB Serial at 9,600 baud. | Exactly one SYSTEM_READY event is emitted for the boot and normal authentication becomes available. |  |  |  |  |
| MT-03 | Verify initialization failure is fail-safe | Power off; disconnect one sensor communication lead; power on and capture Serial; restore wiring only after powering off again. | Relay remains inactive, green stays off, red stays continuously on, authentication is disabled, and one SYSTEM_ERROR event reports SENSOR_INITIALIZATION_FAILED. |  |  |  |  |
| MT-04 | Verify Administrator access indication | Enroll the intended Administrator at ID 1; run the main sketch; place that finger once; remove it; time the relay-controlled green LED. | One successful ACCESS event identifies ID 1 and ADMINISTRATOR; red stays off; relay and green are active for 5 seconds and then return inactive/off. |  |  |  |  |
| MT-05 | Verify Technician access indication | Enroll the intended Technician at ID 2; run the main sketch; place that finger once; remove it; time the relay-controlled green LED. | One successful ACCESS event identifies ID 2 and TECHNICIAN; red stays off; relay and green are active for 5 seconds and then return inactive/off. |  |  |  |  |
| MT-06 | Verify unknown-fingerprint denial | With fewer than three consecutive failures pending, present an unenrolled finger once; remove it; time the red LED. | One failed ACCESS event reports FINGERPRINT_NOT_MATCHED; relay and green stay inactive; red is on for 2 seconds and then turns off. |  |  |  |  |
| MT-07 | Verify relay/green never signal a failed attempt | Repeat one ordinary unknown-fingerprint attempt while filming the relay and both LEDs. | Relay remains inactive and green remains off for the entire failed attempt. |  |  |  |  |
| MT-08 | Verify red never signals successful access | Present either approved identity while filming the relay and both LEDs. | Red remains off while relay and green are active during the successful access state. |  |  |  |  |
| MT-09 | Verify one placement creates one attempt | Present an approved finger and keep it continuously on the sensor beyond the 5-second relay/green interval; keep it in place for at least 5 seconds more; then remove it; inspect Serial events. | Only one ACCESS event and one relay activation occur. No second attempt occurs until the sensor first observes no finger. |  |  |  |  |
| MT-10 | Verify the four-failure lockout threshold and duration | Reset or first complete a successful authentication; make four unknown-fingerprint attempts, fully removing the finger between attempts; start a stopwatch at LOCKOUT_START. | Failed counts progress 1, 2, 3, 4; the fourth denial is followed by LOCKOUT_START; relay/green remain inactive, lockout lasts 2 minutes, and no fingerprint is accepted during it. |  |  |  |  |
| MT-11 | Verify red remains on throughout lockout | During MT-10, record the relay and both LEDs continuously from LOCKOUT_START through LOCKOUT_END. | Relay and green remain inactive; red remains continuously on for the full active lockout and turns off when lockout ends. |  |  |  |  |
| MT-12 | Verify relay/green cannot activate during lockout | During an active lockout, briefly present ID 1 or ID 2 and remove it before expiry; inspect the relay, LEDs, and serial events. | Relay stays inactive, green stays off, and no successful ACCESS event is produced during lockout. |  |  |  |  |
| MT-13 | Verify authentication resumes after expiry | Wait for LOCKOUT_END; ensure no finger is present; then present an approved finger once. | LOCKOUT_END reports failed count 0 and indicator OFF with relay inactive; the later approved attempt activates relay/green normally. |  |  |  |  |
| MT-14 | Verify the Python logger receives success | Start the logger on the correct COM port; perform one approved authentication; inspect logger output. | The logger receives and parses one complete successful ACCESS event. |  |  |  |  |
| MT-15 | Verify the Python logger receives denial | With the logger running, present an unknown finger once; inspect logger output. | The logger receives and parses one complete failed ACCESS event. |  |  |  |  |
| MT-16 | Verify SQLite stores valid events | Run MT-14 and MT-15; query access_events for the captured device, boot, and sequence values. | Each valid event is stored once with all 15 protocol values, host receive time, and raw line. |  |  |  |  |
| MT-17 | Verify dashboard totals agree with storage | Record the dashboard totals; calculate the equivalent counts directly from SQLite using the same event filters. | Dashboard totals exactly match the corresponding SQLite counts. |  |  |  |  |
| MT-18 | Verify an exact duplicate is rejected | In an isolated test database, submit the same valid raw event twice with identical device_id, boot_id, and event_seq. | access_events contains one row for the event, not two. |  |  |  |  |
| MT-19 | Verify replay alert creation | Continue MT-18 and inspect security_alerts. | A REPLAY_SUSPECTED alert references the duplicated event key and raw line. |  |  |  |  |
| MT-20 | Verify local authentication is independent of the host | Stop the Python logger without resetting or rewiring Arduino; present an approved finger. | Arduino still makes the local decision and activates relay/green for 5 seconds; the offline host does not receive that event. |  |  |  |  |
| MT-21 | Verify the dashboard is monitoring-only | Inspect registered Flask routes and the rendered UI; attempt requests only against documented read-only routes; search for unlock, enrollment, serial-write, relay-control, and LED-control handlers. | No route, button, or handler can unlock anything, enroll a fingerprint, write Serial data, or directly control the relay or either LED. |  |  |  |  |

## Evidence handling

Use unique evidence names such as MT-04_video.mp4 or MT-16_query.txt only after
the corresponding trial is performed. A software unit-test pass is evidence for
software behavior only; it is not evidence of fingerprint false-acceptance rate,
false-rejection rate, liveness detection, physical relay/LED timing, or
mechanical access control.
