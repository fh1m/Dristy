/*
 * Dristy.h — Arduino/ESP-IDF library for the Dristy Vision Co-Processor
 *
 * Works on: Arduino, ESP32, ESP32-S3, STM32, Teensy, any board with HardwareSerial.
 *
 * Quick start:
 *   #include <Dristy.h>
 *
 *   Dristy cam(Serial1);  // UART connected to Dristy Gravity port
 *
 *   void setup() {
 *       Serial1.begin(115200);
 *       cam.begin();
 *       cam.setMode(DRISTY_MODE_DETECT_TRACK);
 *   }
 *
 *   void loop() {
 *       if (cam.readTarget()) {
 *           int16_t yaw_error   = cam.target.errorX;   // [-1000, +1000]
 *           int16_t pitch_error = cam.target.errorY;
 *           uint16_t range      = cam.target.rangeMm;
 *
 *           // Feed directly into PID controller
 *           pid_yaw.update(yaw_error);
 *           pid_pitch.update(pitch_error);
 *       }
 *   }
 *
 * For ESP32-S3 SPI bridge to flight controller:
 *   1. Read vision from Dristy on UART
 *   2. Read IMU on I2C
 *   3. Time-align using timestamp_ms
 *   4. Send fused state to FC on SPI
 *
 * Memory: ~200 bytes RAM, ~2 KB flash.
 */

#ifndef DRISTY_H
#define DRISTY_H

#include <Arduino.h>

/* Vision modes */
#define DRISTY_MODE_DETECT          0x00
#define DRISTY_MODE_DETECT_CUSTOM   0x01
#define DRISTY_MODE_CLASSIFY        0x02
#define DRISTY_MODE_FACE_DETECT     0x03
#define DRISTY_MODE_COLOUR_TRACK    0x10
#define DRISTY_MODE_LINE_FOLLOW     0x11
#define DRISTY_MODE_APRILTAG        0x12
#define DRISTY_MODE_QR_CODE         0x13
#define DRISTY_MODE_OPTICAL_FLOW    0x14
#define DRISTY_MODE_ARUCO           0x16
#define DRISTY_MODE_MOTION_DETECT   0x17
#define DRISTY_MODE_DETECT_TRACK    0x20
#define DRISTY_MODE_DETECT_TAG      0x21
#define DRISTY_MODE_LANDING_TARGET  0x22
#define DRISTY_MODE_DETECT_ARUCO    0x23
#define DRISTY_MODE_DETECT_MOTION   0x24

/* Target types */
#define DRISTY_TARGET_NONE      0
#define DRISTY_TARGET_DETECTION 1
#define DRISTY_TARGET_TRACK     2
#define DRISTY_TARGET_TAG       3
#define DRISTY_TARGET_BLOB      4
#define DRISTY_TARGET_LINE      5

/* DLP command IDs */
#define DLP_CMD_SET_MODE        0x40
#define DLP_CMD_GET_TRACKS      0x42
#define DLP_CMD_GET_TAGS        0x44
#define DLP_CMD_SET_THRESHOLD   0x47
#define DLP_CMD_CTRL_TARGET     0x62
#define DLP_CMD_SET_TARGET_ID   0x64
#define DLP_CMD_SET_TARGET_CLS  0x65
#define DLP_CMD_GET_RESULT      0x67
#define DLP_CMD_IDENTIFY        0x4F

/* Max tracks/tags receivable */
#define DRISTY_MAX_TRACKS   15
#define DRISTY_MAX_TAGS     8

/* Primary target — read with readTarget() */
struct DristyTarget {
    uint32_t timestampMs;
    int16_t  errorX;        /* [-1000, +1000] → yaw PID */
    int16_t  errorY;        /* [-1000, +1000] → pitch PID */
    uint16_t size;           /* [0, 2000] → approach PID */
    int16_t  errorRateX;    /* d(error)/dt norm/second */
    int16_t  errorRateY;
    uint16_t rangeMm;       /* 3D range in mm (0 = unknown) */
    uint16_t trackId;
    uint8_t  targetType;
    uint8_t  confidence;    /* 0-100% */

    bool valid() const { return targetType != DRISTY_TARGET_NONE; }
    bool centred() const { return abs(errorX) < 100 && abs(errorY) < 100; }
};

/* Compact track */
struct DristyTrack {
    uint16_t id;
    int16_t  cx, cy;        /* normalised centre */
    int16_t  velX, velY;    /* normalised velocity */
    uint8_t  cls;
    uint8_t  confidence;
};

/* Compact tag */
struct DristyTag {
    uint16_t tagId;
    int16_t  cx, cy;
    uint16_t rangeMm;
    int16_t  yawX10;
    int16_t  pitchX10;
    uint8_t  family;
    uint8_t  hamming;

    bool hasPose() const { return rangeMm > 0; }
};

class Dristy {
public:
    /* Results — populated by read methods */
    DristyTarget target;
    DristyTrack  tracks[DRISTY_MAX_TRACKS];
    uint8_t      trackCount;
    DristyTag    tags[DRISTY_MAX_TAGS];
    uint8_t      tagCount;

    explicit Dristy(Stream &serial) : _serial(serial) {}

    /* Initialise and identify */
    bool begin(uint32_t timeoutMs = 2000);

    /* Mode control */
    bool setMode(uint8_t mode);

    /* Read primary target (fastest — 20 bytes over wire) */
    bool readTarget(uint32_t timeoutMs = 100);

    /* Read all tracks (returns count) */
    uint8_t readTracks(uint32_t timeoutMs = 100);

    /* Read all tags (returns count) */
    uint8_t readTags(uint32_t timeoutMs = 100);

    /* Target lock */
    bool lockTarget(uint16_t trackId);
    bool unlockTarget() { return lockTarget(0); }

    /* Filter by class */
    bool filterClass(uint8_t classId);

    /* Set confidence threshold (0-100%) */
    bool setConfidence(uint8_t percent);

    /* Check if device is alive (call periodically) */
    bool ping();

private:
    Stream &_serial;
    uint8_t _rxBuf[260];

    bool _sendCommand(uint8_t cmd, const uint8_t *data = nullptr, uint8_t len = 0);
    int  _readResponse(uint8_t *cmdOut, uint8_t *dataOut, uint8_t maxLen, uint32_t timeoutMs);
};

/* ── Implementation (header-only for Arduino simplicity) ─────────────── */

inline bool Dristy::begin(uint32_t timeoutMs) {
    return ping();
}

inline bool Dristy::_sendCommand(uint8_t cmd, const uint8_t *data, uint8_t len) {
    uint8_t pkt[260];
    pkt[0] = 0x55;
    pkt[1] = 0xAA;
    pkt[2] = 0x11;   /* addr */
    pkt[3] = len;
    pkt[4] = cmd;
    if (data && len > 0)
        memcpy(pkt + 5, data, len);
    uint8_t sum = 0;
    for (uint8_t i = 2; i < 5 + len; i++) sum += pkt[i];
    pkt[5 + len] = sum;
    _serial.write(pkt, 6 + len);
    return true;
}

inline int Dristy::_readResponse(uint8_t *cmdOut, uint8_t *dataOut,
                                  uint8_t maxLen, uint32_t timeoutMs) {
    uint32_t start = millis();
    uint16_t idx = 0;

    while (millis() - start < timeoutMs) {
        while (_serial.available()) {
            _rxBuf[idx++] = _serial.read();
            if (idx >= sizeof(_rxBuf)) idx = 0;

            /* Look for complete packet */
            if (idx >= 6) {
                for (uint16_t s = 0; s <= idx - 6; s++) {
                    if (_rxBuf[s] == 0x55 && _rxBuf[s+1] == 0xAA) {
                        uint8_t dLen = _rxBuf[s+3];
                        uint16_t total = s + 5 + dLen + 1;
                        if (total <= idx) {
                            *cmdOut = _rxBuf[s+4];
                            uint8_t copyLen = dLen > maxLen ? maxLen : dLen;
                            memcpy(dataOut, _rxBuf + s + 5, copyLen);
                            return copyLen;
                        }
                    }
                }
            }
        }
        delay(1);
    }
    return -1; /* timeout */
}

inline bool Dristy::ping() {
    _sendCommand(DLP_CMD_IDENTIFY);
    uint8_t cmd, data[32];
    return _readResponse(&cmd, data, sizeof(data), 500) > 0;
}

inline bool Dristy::setMode(uint8_t mode) {
    _sendCommand(DLP_CMD_SET_MODE, &mode, 1);
    uint8_t cmd, data[16];
    return _readResponse(&cmd, data, sizeof(data), 200) >= 0;
}

inline bool Dristy::readTarget(uint32_t timeoutMs) {
    _sendCommand(DLP_CMD_CTRL_TARGET);
    uint8_t cmd;
    uint8_t data[64];
    int len = _readResponse(&cmd, data, sizeof(data), timeoutMs);

    /* Find the 20-byte target struct in response.
     * Skip any control output header if present. */
    const uint8_t *p = data;
    if (len >= 25 && data[0] == 0xD5) {
        p = data + 4;
        len -= 4;
    }
    if (len < 20) {
        target.targetType = DRISTY_TARGET_NONE;
        return false;
    }

    memcpy(&target.timestampMs, p, 4);
    memcpy(&target.errorX, p + 4, 2);
    memcpy(&target.errorY, p + 6, 2);
    memcpy(&target.size, p + 8, 2);
    memcpy(&target.errorRateX, p + 10, 2);
    memcpy(&target.errorRateY, p + 12, 2);
    memcpy(&target.rangeMm, p + 14, 2);
    memcpy(&target.trackId, p + 16, 2);
    target.targetType = p[18];
    target.confidence = p[19];

    return target.valid();
}

inline uint8_t Dristy::readTracks(uint32_t timeoutMs) {
    _sendCommand(DLP_CMD_GET_TRACKS);
    uint8_t cmd;
    uint8_t data[250];
    int len = _readResponse(&cmd, data, sizeof(data), timeoutMs);
    if (len < 0) { trackCount = 0; return 0; }

    trackCount = 0;
    for (int off = 0; off + 16 <= len && trackCount < DRISTY_MAX_TRACKS; off += 16) {
        DristyTrack &t = tracks[trackCount];
        memcpy(&t.id, data + off, 2);
        memcpy(&t.cx, data + off + 2, 2);
        memcpy(&t.cy, data + off + 4, 2);
        int16_t w, h, vx, vy;
        uint16_t age;
        memcpy(&w, data + off + 6, 2);
        memcpy(&h, data + off + 8, 2);
        memcpy(&vx, data + off + 10, 2);
        memcpy(&vy, data + off + 12, 2);
        memcpy(&age, data + off + 14, 2);
        (void)w;
        (void)h;
        (void)age;
        t.velX = vx;
        t.velY = vy;
        t.cls = 0;
        t.confidence = 0;
        trackCount++;
    }
    return trackCount;
}

inline uint8_t Dristy::readTags(uint32_t timeoutMs) {
    _sendCommand(DLP_CMD_GET_TAGS);
    uint8_t cmd;
    uint8_t data[250];
    int len = _readResponse(&cmd, data, sizeof(data), timeoutMs);
    if (len < 0) { tagCount = 0; return 0; }

    tagCount = 0;
    for (int off = 0; off + 18 <= len && tagCount < DRISTY_MAX_TAGS; off += 18) {
        DristyTag &t = tags[tagCount];
        uint16_t fam;
        uint16_t ham;
        memcpy(&t.tagId, data + off, 2);
        memcpy(&fam, data + off + 2, 2);
        memcpy(&t.cx, data + off + 4, 2);
        memcpy(&t.cy, data + off + 6, 2);
        memcpy(&ham, data + off + 8, 2);
        int16_t tx, ty, tz, yaw;
        memcpy(&tx, data + off + 10, 2);
        memcpy(&ty, data + off + 12, 2);
        memcpy(&tz, data + off + 14, 2);
        memcpy(&yaw, data + off + 16, 2);
        t.family = (uint8_t)(fam & 0xFFU);
        t.hamming = (uint8_t)(ham & 0xFFU);
        t.yawX10 = yaw;
        t.pitchX10 = 0;
        t.rangeMm = (uint16_t)(tz > 0 ? tz : 0);
        (void)tx;
        (void)ty;
        tagCount++;
    }
    return tagCount;
}

inline bool Dristy::lockTarget(uint16_t trackId) {
    uint8_t d[2] = { (uint8_t)(trackId & 0xFF), (uint8_t)(trackId >> 8) };
    _sendCommand(DLP_CMD_SET_TARGET_ID, d, 2);
    uint8_t cmd, data[4];
    return _readResponse(&cmd, data, sizeof(data), 200) >= 0;
}

inline bool Dristy::filterClass(uint8_t classId) {
    _sendCommand(DLP_CMD_SET_TARGET_CLS, &classId, 1);
    uint8_t cmd, data[4];
    return _readResponse(&cmd, data, sizeof(data), 200) >= 0;
}

inline bool Dristy::setConfidence(uint8_t percent) {
    uint16_t thresh = (uint16_t)percent * 100;
    uint8_t d[2] = { (uint8_t)(thresh & 0xFF), (uint8_t)(thresh >> 8) };
    _sendCommand(DLP_CMD_SET_THRESHOLD, d, 2);
    uint8_t cmd, data[16];
    return _readResponse(&cmd, data, sizeof(data), 200) >= 0;
}

#endif /* DRISTY_H */
