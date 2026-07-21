/*
 * FIRMWARE TEST / HIEU CHUAN  -  Xe do line ESP32
 * Test: 2 Motor DC (L298N) + 2 Encoder + Servo tha hang + Buzzer
 *
 * Dieu khien qua Serial Monitor (115200 baud, gui kem Newline).
 * Go lenh roi Enter. Go "?" de xem menu.
 *
 * Chua bat WiFi - chi de test phan cung va lay so lieu hieu chuan
 * (duong kinh banh, PPR encoder, goc servo giu/tha).
 */

#include <Arduino.h>
#include <ESP32Servo.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <Wire.h>
#include <WiFiUdp.h>

// ===================== WiFi (STA - noi WiFi nha) =====================
// SUA ten/mat khau WiFi nha ban o day:
#define WIFI_SSID "THANH TUNG L1"       // <-- SUA ten WiFi
#define WIFI_PASS "0348903226"  // <-- SUA mat khau WiFi

// Hub (server Node) - IP may chay server + cong WS THUONG cho xe (= PORT+2 = 3002)
// Chi la MAC DINH du phong: xe TU TIM hub qua UDP broadcast (khong can sua tay).
#define HUB_HOST "192.168.1.32"
#define HUB_PORT 3002
#define DISC_PORT 3003             // cong UDP discovery cua server (= PORT+3)

WebServer server(80);
bool wifiOK = false;
Preferences prefs;

// ===== Cau hinh mang runtime (sua qua Serial, luu NVS). Mac dinh = #define tren =====
String cfgSsid = WIFI_SSID;   // ten WiFi
String cfgPass = WIFI_PASS;   // mat khau WiFi
String cfgHub  = HUB_HOST;    // IP laptop chay server.js

// Forward declaration (dinh nghia o cuoi file, dung trong handleCommand)
void setupWiFi();
void startHub();
void startOTA();
void onWifiUp();
void saveNetConfig();
bool computeLineError();
void hubLog(const String& s);   // in Serial + day log len hub (web doc duoc)

// ===================== Che do build =====================
// -DDEBUG_MODE=1 (env:debug)  -> in log, telemetry dinh ky
// -DDEBUG_MODE=0 (env:release)-> tat log de chay nhanh/gon
#ifndef DEBUG_MODE
#define DEBUG_MODE 1          // mac dinh: debug
#endif
#if DEBUG_MODE
  #define DBG(...)   Serial.printf(__VA_ARGS__)
  #define DBGLN(x)   Serial.println(x)
#else
  #define DBG(...)   do{}while(0)
  #define DBGLN(x)   do{}while(0)
#endif

// ===================== CHAN (theo PLAN.md) =====================
// Motor A (banh TRAI)
#define MA_IN1 18
#define MA_IN2 19
#define MA_EN  21
// Motor B (banh PHAI)
#define MB_IN3 22
#define MB_IN4 4
#define MB_EN  16
// Encoder trai (A/B)  -- da hoan doi: 2 encoder bi dao nhau tren xe
#define ENC_L_A 15
#define ENC_L_B 17
// Encoder phai (A/B)  -- dao A/B de sua chieu dem
#define ENC_R_A 14
#define ENC_R_B 13
// Servo tha hang
#define SERVO_PIN 5
// Buzzer
#define BUZZER 23
// MUX 74HC4067 (8 mat do line)
#define MUX_SIG 36
#define MUX_S0  27
#define MUX_S1  26
#define MUX_S2  25
// MUX_S3: chi dung 8 kenh (CH0..CH7) -> S3 luon = 0 -> NOI THANG XUONG GND tren board.
// Nho vay GPIO33 duoc giai phong lam SCL cho MPU6050.
// MPU6050 (I2C) - do gyro Z de hop nhat voi odometry (chinh xac hon khi banh truot)
#define MPU_SDA  32
#define MPU_SCL  33
#define MPU_ADDR 0x68
#define LINE_THRESHOLD 2000   // nguong den/trang (analog 0..4095)
// Mat cam bien con tot (false = bo qua). C1 hong -> mask.
bool SENSOR_OK[8] = { false, true, true, true, true, true, true, true };

// ===================== PWM (LEDC) =====================
#define PWM_FREQ 5000   // 5 kHz cho motor
#define PWM_RES  8      // 8-bit -> duty 0..255
// LEDC: timer = (channel/2) % 4  -> 2 channel lien tiep DUNG CHUNG 1 timer!
// ESP32Servo tu cap phat tu channel 0 (timer 0) va KHONG biet cac ledcSetup() goi truc tiep.
//   servo   = ch0        -> timer 0
//   motor   = ch2, ch3   -> timer 1
//   buzzer  = ch4        -> timer 2  (PHAI khac timer servo!)
// Truoc day buzzer = ch1 -> timer 0 = TRUNG timer servo: moi lan beep, ledcSetup doi
// timer 0 sang tan so beep (8-bit) -> servo mat xung 50Hz -> GIAT/QUAY LOAN.
#define MA_CH    2      // LEDC channel Motor A (ENA)
#define MB_CH    3      // LEDC channel Motor B (ENB)
#define BUZZER_CH 4     // LEDC channel Buzzer (timer 2 - tach khoi servo/motor)

// ===================== Thong so hieu chuan =====================
// Sua sau khi do thuc te. GIA TRI DA HIEU CHUAN (bake tu thong so chay tot tren web).
float WHEEL_DIAMETER_MM = 74.0;   // duong kinh banh HIEU DUNG (mm) - hieu chuan thuc te = 74.0
int   ENCODER_PPR       = 370;    // so xung / vong
float WHEEL_BASE_MM     = 170.0;  // khoang cach tam 2 banh sau (mm)
float CASTER_DIST_MM    = 100.0;  // khoang cach truc banh sau -> banh tu do (mm)

// Goc servo (do)
int SERVO_HOLD = 180;  // goc GIU hang
int SERVO_DROP = 90;   // goc THA hang

// Dao chieu motor neu banh quay nguoc (doi true/false roi flash lai)
bool INVERT_A = true;   // banh TRAI (L)
bool INVERT_B = true;   // banh PHAI (R)

// ===================== Bien toan cuc =====================
Servo servo;
int   motorSpeed = 210;            // toc do hien tai 0..255 (mac dinh 210)
int   MOTOR_MIN_PWM = 120;         // san PWM: lenh khac 0 nhung < nguong -> nang len (motor keu ma khong quay = duoi nguong khoi dong)
volatile long encL = 0;            // dem xung encoder trai
volatile long encR = 0;            // dem xung encoder phai
bool  streamLine = false;          // true = in lien tuc gia tri 8 mat
int   lineRaw[8];                  // gia tri analog 8 mat (C1..C8)
int   lineMin[8], lineMax[8];      // min/max moi mat khi cali
int   lineThresh[8];               // nguong rieng moi mat
bool  lineCalibrated = false;      // da cali chua

// --- Bam line PID ---
bool  lineFollow = false;          // dang bam line?
float Kp = 25.0, Ki = 0.0, Kd = 15.0;
float lineError = 0, lastError = 0, errIntegral = 0;
int   baseSpeed = 200;             // toc do co ban khi bam line (tren nguong khoi dong)
bool  lineLost = false;            // mat line?
float LINE_TRIM = 0.5;             // tam that cua mang cam bien (don vi = khoang cach 1 mat).
                                   // C1 mask -> dung C2..C8 -> tam hinh hoc = +0.5.
                                   // Dat lai tu dong: de line dung cho muon bam roi gui {"cmd":"linezero"}

// --- Odometry (toa do tuong doi) ---
float poseX = 0, poseY = 0, poseTheta = 0;   // mm, mm, rad
long  lastOdoL = 0, lastOdoR = 0;

// --- Re CO DINH (open-loop: PWM + thoi gian, KHONG PID/gyro-feedback) ---
// Ban PID cu (dua vao gyro+encoder) hay bi LAC/TIMEOUT do 2 ben trai/phai khong that su
// doi xung (ma sat/motor khac nhau). Doi sang re CO DINH: chay 1 PWM co dinh trong thoi
// gian re -> don gian, KHONG lac, KHONG timeout.
// QUAN TRONG: quan he thoi_gian -> goc_thuc KHONG ti le thuan tu 0 - co "thoi gian chet"
// khoi dong (thang ma sat tinh) truoc khi banh thuc su lan. Do tu nhieu lan Test re (gyro
// rat on dinh qua nhieu lan do doc lap): goc(do) = GYRO_DPS_RATE*ms - TURN_MS_OFFSET*GYRO_DPS_RATE,
// tuc ms = TURN_MS_OFFSET + |goc| * TURN_MS_PER_DEG (offset + tuyen tinh, KHONG phai ms=goc*const).
int   TURN_OPEN_PWM   = 220;      // PWM khi re (co dinh, du manh de thang ma sat ca 2 chieu)
// 2 ben TRAI/PHAI khong hoan toan doi xung (ma sat, motor) -> tach rieng he so moi ben,
// deu tinh ms = OFFSET_x + |goc| * PERDEG_x. Hieu chuan rieng bang "Test re" +90/-90.
float TURN_MS_OFFSET_L = 65.0, TURN_MS_PER_DEG_L = 4.57;   // ben TRAI (goc > 0, CCW)
float TURN_MS_OFFSET_R = 65.0, TURN_MS_PER_DEG_R = 4.57;   // ben PHAI (goc < 0, CW)
bool  INVERT_TURN = false;        // dao chieu actuation vong re (sua bang serial: TI)

// --- Dieu phoi TUNG BUOC: server gui 1 buoc, xe lam xong bao lai, server gui buoc ke ---
// Khac ban cu (server gui CA chuoi route[], xe tu chay het): gio xe chi giu 1 buoc dang chay
// (curAction), lam xong -> gui {type:stepdone} + dung cho buoc tiep. Server la "bo nao" quyet
// dinh buoc ke dua tren vi tri (odometry) xe bao ve.
WebSocketsClient wsClient;
bool   hubOK = false;             // dang ket noi hub?
String curAction = "";            // buoc dang chay: F/L/R/B/DROP/HOME. "" = idle, dang cho server
float  curDist = 0;               // khoang cach (mm) ky vong cho buoc 'F' hien tai (0 = khong biet)
int    curSeq = -1;               // chi so buoc (server gui), gui kem trong stepdone
String routeTarget = "";          // ma o dang giao (C1..C9), "" = khong
bool   running = false;           // dang trong 1 chuyen giao?
int    stepPhase = 0;             // 0 = re/khoi dong doan, 1 = tien bam line, 2 = bo canh tam nga tu
float  segStartX = 0, segStartY = 0;  // moc odometry dau doan
bool   leftStart = false;         // da roi giao diem xuat phat cua doan chua
bool   cargo = true;              // con hang tren xe?
int    lineCount = 0;             // so mat thay den o lan doc gan nhat
const float MIN_EDGE = 120;       // mm toi thieu 1 doan truoc khi cho ket thuc
const float MAX_EDGE = 750;       // mm toi da 1 doan (chan runaway neu miss giao diem)
int    INTERSECT_N = 4;           // >= so mat thay den => coi la giao diem (nga tu).
                                  // Do thuc te: giao diem CHU T (nhanh vao o) chi cho cnt=4
                                  // (stub lech 1 ben, khong doi xung nhu giao diem chu thap)
                                  // -> nguong 5 (cu) KHONG BAO GIO nhan ra giao diem chu T,
                                  // xe di thang qua toi khi cham MAX_EDGE moi dung an toan.
int    CENTER_OFFSET_MM = 145;    // sau khi thay nga tu, bo them de canh TRUC BANH vao tam
                                  // = k/c thanh cam bien -> truc banh sau (DA DO: 145mm)
float  centerStartMM = 0;         // moc quang duong khi bat dau bo canh tam
// Sau khi RE xong, cam bien (truoc truc 145mm) thuong CHUA nam tren nhanh moi.
// Thay vi XOAY TAI CHO de do (se quet trung nhanh CU -> chay nguoc), ta BO THANG CHAM
// de dua cam bien len nhanh moi. Khong thay trong REACQUIRE_MAX -> dung an toan.
int    REACQUIRE_SPEED  = 130;    // toc do bo (cham) khi canh tam / tim line
int    REACQUIRE_MAX_MM = 150;    // bo toi da bao nhieu mm de tim; qua -> dung an toan
float  reacqStartMM = 0;

// ===================== Encoder ISR (quadrature) =====================
void IRAM_ATTR isrEncL() {
  // doc kenh B de biet chieu
  if (digitalRead(ENC_L_B)) encL++; else encL--;
}
void IRAM_ATTR isrEncR() {
  if (digitalRead(ENC_R_B)) encR++; else encR--;
}

// ===================== MUX 74HC4067 =====================
// Doc 1 kenh (0..15)
int readMuxChannel(int ch) {
  digitalWrite(MUX_S0, ch & 0x01);
  digitalWrite(MUX_S1, (ch >> 1) & 0x01);
  digitalWrite(MUX_S2, (ch >> 2) & 0x01);
  // S3 da noi GND (chi doc CH0..CH7) -> khong can dieu khien
  delayMicroseconds(60);          // cho MUX + ADC on dinh (giam crosstalk)
  analogRead(MUX_SIG);            // mau bo (xa dien tich kenh truoc)
  long sum = 0;
  for (int k = 0; k < 4; k++) sum += analogRead(MUX_SIG);  // trung binh 4 mau
  return (int)(sum / 4);
}

// Doc 8 mat line (CH0..CH7 = C1..C8, trai -> phai) vao lineRaw[]
void readLine() {
  for (int i = 0; i < 8; i++) lineRaw[i] = readMuxChannel(i);
}

// In gia tri 8 mat + dang nhi phan theo nguong
void printLine() {
  readLine();
  Serial.print("RAW: ");
  for (int i = 0; i < 8; i++) { Serial.printf("%4d ", lineRaw[i]); }
  Serial.print(" | LINE(den=1): ");
  for (int i = 0; i < 8; i++) {
    if (!SENSOR_OK[i]) { Serial.print("x"); continue; }   // mat bi mask
    int th = lineCalibrated ? lineThresh[i] : LINE_THRESHOLD;
    Serial.print(lineRaw[i] < th ? "1" : "0");   // den = gia tri thap = 1
  }
  Serial.println();
}

// ===================== Motor =====================
// chieu: +1 thuan, -1 nghich, 0 dung
// Bu vung chet: neu co lenh chay (dir!=0) ma PWM < nguong khoi dong -> nang len MOTOR_MIN_PWM
static inline int applyMinPwm(int dir, int spd) {
  if (dir != 0 && spd > 0 && spd < MOTOR_MIN_PWM) return MOTOR_MIN_PWM;
  return spd;
}
void setMotorA(int dir, int spd) {
  if (INVERT_A) dir = -dir;
  spd = applyMinPwm(dir, spd);
  digitalWrite(MA_IN1, dir > 0);
  digitalWrite(MA_IN2, dir < 0);
  ledcWrite(MA_CH, dir == 0 ? 0 : spd);
}
void setMotorB(int dir, int spd) {
  if (INVERT_B) dir = -dir;
  spd = applyMinPwm(dir, spd);
  digitalWrite(MB_IN3, dir > 0);
  digitalWrite(MB_IN4, dir < 0);
  ledcWrite(MB_CH, dir == 0 ? 0 : spd);
}
void stopMotors() { setMotorA(0, 0); setMotorB(0, 0); }

// Dieu khien motor theo van toc co dau (-255..255)
void driveA(int v) { v = constrain(v, -255, 255); setMotorA(v > 0 ? 1 : (v < 0 ? -1 : 0), abs(v)); }
void driveB(int v) { v = constrain(v, -255, 255); setMotorB(v > 0 ? 1 : (v < 0 ? -1 : 0), abs(v)); }

// ===================== MPU6050 (gyro Z) =====================
// Encoder bi truot banh khi quay -> goc sai. Gyro do truc tiep van toc goc,
// hop nhat 2 nguon (complementary) cho poseTheta chinh xac hon.
bool  mpuOK = false;              // tim thay MPU6050?
float gyroBiasZ = 0;              // troi tinh (LSB) - do khi xe dung yen
float GYRO_W = 0.98;              // trong so tin GYRO khi hop nhat (0=chi encoder, 1=chi gyro)
int   GYRO_SIGN = 1;              // dao dau neu module gan nguoc chieu
float gyroDelta = 0;              // goc gyro tich luy (rad), odometry se tieu thu
unsigned long lastGyroUs = 0;
const float GYRO_LSB_PER_DPS = 32.8f;    // thang do +-1000 do/s (khop voi mpuWrite(0x1B,0x10))

void mpuWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MPU_ADDR); Wire.write(reg); Wire.write(val); Wire.endTransmission();
}
int16_t mpuGyroZraw() {
  Wire.beginTransmission(MPU_ADDR); Wire.write(0x47);        // GYRO_ZOUT_H
  if (Wire.endTransmission(false) != 0) return 0;
  if (Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)2) != 2) return 0;
  return (int16_t)((Wire.read() << 8) | Wire.read());
}
// Do troi tinh gyro - XE PHAI DUNG YEN khi goi
void gyroCalibrate(int n = 500) {
  if (!mpuOK) { hubLog("[gyro] chua co MPU6050"); return; }
  double sum = 0;
  for (int i = 0; i < n; i++) { sum += mpuGyroZraw(); delay(2); }
  gyroBiasZ = (float)(sum / n);
  gyroDelta = 0; lastGyroUs = micros();
  hubLog("[gyro] bias Z = " + String(gyroBiasZ, 1) + " LSB (" +
         String(gyroBiasZ / GYRO_LSB_PER_DPS, 2) + " do/s)");
}
void mpuInit() {
  Wire.begin(MPU_SDA, MPU_SCL, 400000);
  Wire.beginTransmission(MPU_ADDR); Wire.write(0x75);        // WHO_AM_I
  uint8_t who = 0;
  if (Wire.endTransmission(false) == 0 && Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)1) == 1)
    who = Wire.read();
  mpuOK = (who != 0x00 && who != 0xFF);
  if (!mpuOK) { Serial.printf(">> MPU6050 KHONG THAY (SDA=%d SCL=%d) - dung encoder don thuan\n", MPU_SDA, MPU_SCL); return; }
  mpuWrite(0x6B, 0x80); delay(100);        // reset
  mpuWrite(0x6B, 0x01); delay(10);         // wake, clock = gyro X (on dinh hon)
  mpuWrite(0x1A, 0x03);                    // DLPF ~44Hz (loc rung motor)
  mpuWrite(0x1B, 0x10);                    // gyro +-1000 do/s (quay tai cho nhanh de vuot +-250 -> bao hoa/clip)
  delay(50);
  Serial.printf(">> MPU6050 OK (WHO_AM_I=0x%02X, SDA=%d SCL=%d)\n", who, MPU_SDA, MPU_SCL);
  lastGyroUs = micros();
}
// Quet bus I2C: liet ke dia chi tim thay. Neu thay MPU ma chua init -> init lai.
void i2cScan() {
  // --- Chan doan muc dien ap 2 chan bus (truoc khi quet) ---
  // Bus I2C ranh phai o muc CAO (nho tro treo). Neu doc duoc THAP = chan bi
  // noi xuong GND / thiet bi giu bus -> khong bao gio giao tiep duoc.
  Wire.end();
  pinMode(MPU_SDA, INPUT_PULLUP);
  pinMode(MPU_SCL, INPUT_PULLUP);
  delay(5);
  int sdaLv = digitalRead(MPU_SDA), sclLv = digitalRead(MPU_SCL);
  hubLog("[i2c] Muc bus khi ranh: SDA(32)=" + String(sdaLv ? "CAO" : "THAP <-- LOI") +
         "  SCL(33)=" + String(sclLv ? "CAO" : "THAP <-- LOI") + "  (dung: ca hai CAO)");
  if (!sclLv || !sdaLv)
    hubLog("[i2c] Chan bi ghim xuong GND. Neu SCL(33) THAP: GO HAN day GPIO33 -> MUX S3 (S3 chi noi GND)");
  Wire.begin(MPU_SDA, MPU_SCL, 100000);   // 100kHz cho on dinh khi quet
  delay(5);

  String found = "";
  int n = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { found += " 0x" + String(a, HEX); n++; }
    delay(2);
  }
  if (n == 0) {
    hubLog("[i2c] KHONG thay thiet bi nao. Kiem tra: SDA=32, SCL=33, VCC=3.3V, GND chung, AD0=GND");
    return;
  }
  hubLog("[i2c] Thay " + String(n) + " thiet bi:" + found + "   (MPU6050 = 0x68)");
  if (!mpuOK) { hubLog("[i2c] Thu khoi tao lai MPU6050..."); mpuInit(); if (mpuOK) gyroCalibrate(); }
}

// Doc va bao cao 8 mat line (test cam bien tu xa)
void reportLine() {
  readLine();
  String s = "[line] RAW:";
  for (int i = 0; i < 8; i++) s += " " + String(lineRaw[i]);
  s += " | den=1:";
  int cnt = 0;
  for (int i = 0; i < 8; i++) {
    if (!SENSOR_OK[i]) { s += "x"; continue; }
    int th = lineCalibrated ? lineThresh[i] : LINE_THRESHOLD;
    bool black = lineRaw[i] < th;
    s += black ? "1" : "0";
    if (black) cnt++;
  }
  s += " cnt=" + String(cnt) + (lineCalibrated ? " (da cali)" : " (CHUA cali)");
  // Sai so sau khi tru trim: 0 = line dung tam. Am = line lech trai, duong = lech phai.
  if (computeLineError()) s += "  err=" + String(lineError, 2) + " (trim=" + String(LINE_TRIM, 2) + ")";
  else                    s += "  MAT LINE";
  hubLog(s);
}

// Dat TAM LINE tu dong: de xe dung cho muon bam (line duoi vi tri chuan) roi goi ham nay.
// Sai so tho hien tai se thanh moc 0 => het be lech 1 ben.
void lineSetZero() {
  float save = LINE_TRIM;
  LINE_TRIM = 0;                     // do sai so THO (chua tru trim)
  if (!computeLineError()) {
    LINE_TRIM = save;
    hubLog("[linezero] KHONG thay line - dat xe len vach roi thu lai");
    return;
  }
  LINE_TRIM = lineError;             // lay chinh no lam tam
  hubLog("[linezero] Da dat tam line: trim=" + String(LINE_TRIM, 2) +
         " (truoc=" + String(save, 2) + "). Gio line o vi tri nay = err 0.");
}

// Toc do goc hien tai (do/s) tu gyro. 0 neu khong co MPU.
float gyroRateDps() {
  if (!mpuOK) return 0;
  return GYRO_SIGN * (mpuGyroZraw() - gyroBiasZ) / GYRO_LSB_PER_DPS;
}

// Lay mau gyro va cong don goc. Tu gioi han ~250Hz.
void updateGyro() {
  if (!mpuOK) return;
  unsigned long now = micros();
  float dt = (now - lastGyroUs) * 1e-6f;
  if (dt < 0.004f) return;                 // toi da ~250Hz
  lastGyroUs = now;
  if (dt > 0.2f) return;                   // nhip qua dai -> bo (tranh nhay goc)
  float dps = (mpuGyroZraw() - gyroBiasZ) / GYRO_LSB_PER_DPS;
  gyroDelta += GYRO_SIGN * dps * dt * PI / 180.0f;   // rad
}

// ===================== Odometry =====================
// Cap nhat toa do (x,y,theta): quang duong tu encoder, GOC hop nhat encoder + gyro.
void updateOdometry() {
  updateGyro();                                // lay mau gyro (tu gioi han nhip)
  long l = encL, r = encR;
  long dL = l - lastOdoL;
  long dR = r - lastOdoR;
  lastOdoL = l; lastOdoR = r;
  float dThetaGyro = gyroDelta; gyroDelta = 0;  // tieu thu goc gyro da cong don
  if (dL == 0 && dR == 0 && fabsf(dThetaGyro) < 1e-6f) return;

  float mmPerPulse = (PI * WHEEL_DIAMETER_MM) / ENCODER_PPR;
  float sL = dL * mmPerPulse;
  float sR = dR * mmPerPulse;
  float dS = (sL + sR) * 0.5f;                 // quang duong tam xe
  float dThetaEnc = (sR - sL) / WHEEL_BASE_MM; // goc theo encoder (sai khi truot banh)
  // Hop nhat: tin gyro nhieu hon (khong bi truot), encoder chong troi dai han
  float dTheta = mpuOK ? (GYRO_W * dThetaGyro + (1.0f - GYRO_W) * dThetaEnc) : dThetaEnc;

  // tich phan vi tri (dung huong giua doan de chinh xac hon)
  poseX += dS * cosf(poseTheta + dTheta * 0.5f);
  poseY += dS * sinf(poseTheta + dTheta * 0.5f);
  poseTheta += dTheta;
}

void resetOdometry() {
  encL = 0; encR = 0; lastOdoL = 0; lastOdoR = 0;
  poseX = poseY = poseTheta = 0;
  gyroDelta = 0; lastGyroUs = micros();   // bo goc gyro con ton dong
}

// In toa do hien tai
void printPose() {
  Serial.printf("[pose] x=%.1fmm  y=%.1fmm  theta=%.1f deg  (encL=%ld encR=%ld)\n",
                poseX, poseY, poseTheta * 180.0f / PI, encL, encR);
}

// ===================== Tinh sai so line =====================
// Tra ve TRUE neu thay line. Cap nhat lineError (am=line lech trai, duong=phai).
// Den = gia tri THAP -> mat thay line khi raw < nguong.
bool computeLineError() {
  readLine();
  // trong so vi tri C1..C8: -3.5 .. +3.5 (giua C4-C5 = 0)
  static const float W[8] = {-3.5,-2.5,-1.5,-0.5, 0.5, 1.5, 2.5, 3.5};
  float sum = 0; int cnt = 0;
  for (int i = 0; i < 8; i++) {
    if (!SENSOR_OK[i]) continue;                   // bo qua mat hong
    int th = lineCalibrated ? lineThresh[i] : LINE_THRESHOLD;
    if (lineRaw[i] < th) { sum += W[i]; cnt++; }   // mat thay vach den
  }
  lineCount = cnt;
  if (cnt == 0) { lineLost = true; return false; } // mat line
  lineLost = false;
  // Tru LINE_TRIM de dua TAM THAT ve 0. Can thiet vi C1 bi mask -> mang dung duoc
  // la C2..C8, tam hinh hoc cua no o +0.5 chu khong phai 0; cong them sai lech
  // co khi gan thanh cam bien. Khong tru -> xe luon be lech 1 ben.
  lineError = sum / cnt - LINE_TRIM;
  return true;
}

// Chi phan PID + xuat dong co (gia dinh computeLineError() vua tra TRUE).
void lineDrivePID() {
  errIntegral += lineError;
  errIntegral = constrain(errIntegral, -50, 50);
  float d = lineError - lastError;
  float correction = Kp * lineError + Ki * errIntegral + Kd * d;
  lastError = lineError;
  int left  = baseSpeed - (int)correction;   // line lech phai (error>0) -> phai nhanh hon, xe re phai
  int right = baseSpeed + (int)correction;
  driveA(left); driveB(right);
}

// ===================== Vong dieu khien bam line (che do THU CONG 'g') =====================
void lineFollowStep() {
  if (!computeLineError()) {
    // Mat line: quay tai cho theo huong lech cuoi cung de tim lai.
    // Dung TURN_OPEN_PWM (khong phai baseSpeed): quay tai cho doi ma sat tinh lon hon
    // chay thang nhieu. Neu baseSpeed bi chinh thap (de bam line em) se khong du
    // luc xoay -> banh truot/ket tai cho nhung encoder van dem -> odom troi sai.
    int dir = (lastError >= 0) ? 1 : -1;
    int spd = max(baseSpeed, TURN_OPEN_PWM);
    driveA(-dir * spd); driveB(dir * spd);
    return;
  }
  lineDrivePID();
}

// ===================== Re CO DINH (open-loop) =====================
// Quay tai cho mot goc tuong doi (deg). Duong = quay trai (CCW), am = phai.
// KHONG dung PID/phan hoi: chay PWM TURN_OPEN_PWM co dinh trong thoi gian
// = OFFSET_x + |deg| * PERDEG_x (ms) roi dung, voi he so RIENG cho trai/phai (xem khai
// bao bien). Odometry (gyro+encoder) van duoc cap nhat trong luc quay NHUNG CHI DE GHI
// LOG/telemetry - khong dung de dieu chinh toc do/thoi gian. Hieu chuan bang "Test re"
// tren web: so dat=... voi tgt=..., neu dat<tgt tang PERDEG ben do (hoac OFFSET neu sai
// deu ca goc nho), dat>tgt thi giam.
void turnRelative(float deg) {
  if (fabs(deg) < 0.5f) return;
  float startTheta = poseTheta;
  int sgn = INVERT_TURN ? -1 : 1;
  int dir = (deg >= 0) ? 1 : -1;
  float offset = (deg >= 0) ? TURN_MS_OFFSET_L  : TURN_MS_OFFSET_R;
  float perDeg = (deg >= 0) ? TURN_MS_PER_DEG_L : TURN_MS_PER_DEG_R;
  unsigned long ms = (unsigned long)(offset + fabs(deg) * perDeg + 0.5f);

  driveA(sgn * dir * TURN_OPEN_PWM); driveB(-sgn * dir * TURN_OPEN_PWM);  // quay tai cho: 2 banh nguoc chieu
  unsigned long t0 = millis();
  while (millis() - t0 < ms) { updateOdometry(); delay(2); }
  stopMotors();

  float achievedDeg = (poseTheta - startTheta) * 180.0f / PI;
  float errDeg = deg - achievedDeg;
  char res[140];
  snprintf(res, sizeof(res),
    "[turnres] tgt=%.1f dat=%.2f err=%.2f ms=%lu (co dinh, pwm=%d)",
    deg, achievedDeg, errDeg, ms, TURN_OPEN_PWM);
  hubLog(res);
}

// ===================== Tien ich =====================
// Phat tone tan so freq (Hz) trong ms mili-giay
void buzzerTone(int freq, int ms) {
  ledcSetup(BUZZER_CH, freq, 8);
  ledcAttachPin(BUZZER, BUZZER_CH);
  ledcWrite(BUZZER_CH, 128);   // 50% duty
  delay(ms);
  ledcWrite(BUZZER_CH, 0);
}
void beep(int ms) { buzzerTone(2000, ms); }

// ===================== Cali line =====================
void saveCalibration();   // dinh nghia o duoi (NVS) - forward declare
// Quy trinh: dem nguoc 5s (tick) -> tone bat dau -> quet thanh cam bien
// qua vach den va nen trang trong 5s -> tone ket thuc -> tinh nguong rieng.
void calibrateLine() {
  Serial.println(F("\n== CALI LINE: chuan bi, bat dau sau 5 giay... =="));
  for (int s = 5; s >= 1; s--) {           // dem nguoc, moi giay 1 tick ngan
    Serial.printf("  %d...\n", s);
    buzzerTone(1000, 60);
    delay(940);
  }
  buzzerTone(1800, 400);                    // TONE BAT DAU (cao, dai)
  hubLog(">> QUET thanh cam bien qua VACH va NEN NGAY BAY GIO (5s)!");

  for (int i = 0; i < 8; i++) { lineMin[i] = 4095; lineMax[i] = 0; }
  unsigned long end = millis() + 5000;
  while (millis() < end) {
    readLine();
    for (int i = 0; i < 8; i++) {
      if (lineRaw[i] < lineMin[i]) lineMin[i] = lineRaw[i];
      if (lineRaw[i] > lineMax[i]) lineMax[i] = lineRaw[i];
    }
    delay(4);
  }
  for (int i = 0; i < 8; i++) lineThresh[i] = (lineMin[i] + lineMax[i]) / 2;
  lineCalibrated = true;

  buzzerTone(2600, 150); delay(80); buzzerTone(2600, 250);  // TONE KET THUC (2 beep)
  String rep = "[calib] XONG. thresh:";
  for (int i = 0; i < 8; i++) rep += " C" + String(i + 1) + "=" + String(lineThresh[i]);
  hubLog(rep);
  saveCalibration();   // luu vao NVS de tu nap lan sau
}

// ===================== NVS: luu/nap calibration =====================
void saveCalibration() {
  prefs.begin("line", false);
  prefs.putBytes("thresh", lineThresh, sizeof(lineThresh));
  prefs.putBool("done", true);
  prefs.end();
  Serial.println(F(">> Da luu calibration vao NVS (tu nap lan sau)"));
}
bool loadCalibration() {
  prefs.begin("line", true);
  bool done = prefs.getBool("done", false);
  if (done) prefs.getBytes("thresh", lineThresh, sizeof(lineThresh));
  prefs.end();
  if (done) {
    lineCalibrated = true;
    Serial.print(F(">> Da nap calibration tu NVS: "));
    for (int i = 0; i < 8; i++) Serial.printf("%d ", lineThresh[i]);
    Serial.println();
  }
  return done;
}

float pulsesToMM(long pulses) {
  float circ = PI * WHEEL_DIAMETER_MM;       // chu vi
  return (float)pulses / ENCODER_PPR * circ; // quang duong
}

void printMenu() {
  Serial.println(F("\n===== MENU TEST / HIEU CHUAN ====="));
  Serial.println(F("--- MOTOR (2 banh) ---"));
  Serial.println(F("  f = ca 2 banh tien     b = ca 2 banh lui"));
  Serial.println(F("  l = quay trai          r = quay phai"));
  Serial.println(F("  1 = chi banh TRAI tien 2 = chi banh PHAI tien"));
  Serial.println(F("  x = dung               + / - = tang/giam toc"));
  Serial.println(F("  v<so> = dat toc do (vd v200)"));
  Serial.println(F("--- ENCODER ---"));
  Serial.println(F("  e = reset bo dem       p = in xung + quang duong"));
  Serial.println(F("--- SERVO ---"));
  Serial.println(F("  o = THA hang           h = GIU hang"));
  Serial.println(F("  s<goc> = dat goc servo (vd s45)"));
  Serial.println(F("  H<goc> = dat goc GIU   D<goc> = dat goc THA"));
  Serial.println(F("--- DO LINE (MUX) ---"));
  Serial.println(F("  m = in 8 mat lien tuc  M = dung in"));
  Serial.println(F("  n = in 8 mat 1 lan"));
  Serial.println(F("  c = CALI line (5s dem nguoc + tone, roi quet cam bien)"));
  Serial.println(F("--- BAM LINE (PID) + ODOMETRY ---"));
  Serial.println(F("  g = bat dau bam line   x = dung"));
  Serial.println(F("  B<so> = toc do co ban  (vd B130)"));
  Serial.println(F("  kp<so> kd<so> ki<so> = chinh he so PID (vd kp25)"));
  Serial.println(F("  q = in vi tri (x,y,theta)   e = reset odometry"));
  Serial.println(F("--- RE (CO DINH: PWM + thoi gian) ---"));
  Serial.println(F("  T<goc> = re tai cho (vd T90, T-90)"));
  Serial.println(F("  Tm<so> = PWM re co dinh (chung 2 ben)"));
  Serial.println(F("  TsL<so> TsR<so> = ms/do rieng trai/phai   ToL<so> ToR<so> = ms chet rieng"));
  Serial.println(F("  TI = dao chieu actuation vong re"));
  Serial.println(F("--- CAU HINH MANG (WiFi + IP laptop) ---"));
  Serial.println(F("  Wn<ten> = ten WiFi     Wp<mk> = mat khau"));
  Serial.println(F("  Wh<ip>  = IP laptop    Ws = luu + ket noi lai"));
  Serial.println(F("  W       = xem cau hinh hien tai"));
  Serial.println(F("--- KHAC ---"));
  Serial.println(F("  z = buzzer beep        ? = menu nay"));
  Serial.println(F("==================================\n"));
}

// ===================== Xu ly lenh =====================
void handleCommand(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;
  char c = cmd.charAt(0);

  switch (c) {
    // ----- Motor -----
    case 'f': setMotorA(+1, motorSpeed); setMotorB(+1, motorSpeed);
              Serial.println("Ca 2 banh TIEN"); break;
    case 'b': setMotorA(-1, motorSpeed); setMotorB(-1, motorSpeed);
              Serial.println("Ca 2 banh LUI"); break;
    case 'l': setMotorA(-1, motorSpeed); setMotorB(+1, motorSpeed);
              Serial.println("Quay TRAI"); break;
    case 'r': setMotorA(+1, motorSpeed); setMotorB(-1, motorSpeed);
              Serial.println("Quay PHAI"); break;
    case '1': setMotorA(+1, motorSpeed); setMotorB(0, 0);
              Serial.println("Chi banh TRAI tien"); break;
    case '2': setMotorA(0, 0); setMotorB(+1, motorSpeed);
              Serial.println("Chi banh PHAI tien"); break;
    case 'x': lineFollow = false; stopMotors(); Serial.println("DUNG"); break;
    case '+': motorSpeed = min(255, motorSpeed + 10);
              Serial.printf("Toc do = %d\n", motorSpeed); break;
    case '-': motorSpeed = max(0, motorSpeed - 10);
              Serial.printf("Toc do = %d\n", motorSpeed); break;
    case 'v': motorSpeed = constrain(cmd.substring(1).toInt(), 0, 255);
              Serial.printf("Toc do = %d\n", motorSpeed); break;

    // ----- Encoder / Odometry -----
    case 'e': resetOdometry(); Serial.println("Reset encoder + odometry"); break;
    case 'q': printPose(); break;
    case 'p': {
      long l = encL, r = encR;
      Serial.printf("Encoder  L=%ld (%.1f mm)  R=%ld (%.1f mm)\n",
                    l, pulsesToMM(l), r, pulsesToMM(r));
      break;
    }

    // ----- Servo -----
    case 'o': servo.write(SERVO_DROP); Serial.printf("THA hang (goc %d)\n", SERVO_DROP); break;
    case 'h': servo.write(SERVO_HOLD); Serial.printf("GIU hang (goc %d)\n", SERVO_HOLD); break;
    case 's': { int a = constrain(cmd.substring(1).toInt(), 0, 180);
                servo.write(a); Serial.printf("Servo -> %d do\n", a); break; }
    case 'H': SERVO_HOLD = constrain(cmd.substring(1).toInt(), 0, 180);
              Serial.printf("Goc GIU = %d\n", SERVO_HOLD); break;
    case 'D': SERVO_DROP = constrain(cmd.substring(1).toInt(), 0, 180);
              Serial.printf("Goc THA = %d\n", SERVO_DROP); break;

    // ----- Tu test motor -----
    case 't': {
      Serial.println(">> TEST: A tien 1.5s");
      setMotorA(+1, 200); setMotorB(0,0); delay(1500);
      Serial.println(">> TEST: A lui 1.5s");
      setMotorA(-1, 200); delay(1500); stopMotors(); delay(500);
      Serial.println(">> TEST: B tien 1.5s");
      setMotorB(+1, 200); setMotorA(0,0); delay(1500);
      Serial.println(">> TEST: B lui 1.5s");
      setMotorB(-1, 200); delay(1500); stopMotors();
      Serial.println(">> TEST xong");
      break;
    }
    // ----- Re co dinh (open-loop) -----
    case 'T':
      if (cmd.length() > 1 && cmd.charAt(1) == 'I') { INVERT_TURN = !INVERT_TURN; Serial.printf("INVERT_TURN=%d\n", INVERT_TURN); }
      else if (cmd.length() > 1 && cmd.charAt(1) == 'm') { TURN_OPEN_PWM = cmd.substring(2).toInt(); Serial.printf("TURN_OPEN_PWM=%d\n", TURN_OPEN_PWM); }
      else if (cmd.length() > 2 && cmd.charAt(1) == 's' && cmd.charAt(2) == 'L') { TURN_MS_PER_DEG_L = cmd.substring(3).toFloat(); Serial.printf("TURN_MS_PER_DEG_L=%.2f\n", TURN_MS_PER_DEG_L); }
      else if (cmd.length() > 2 && cmd.charAt(1) == 's' && cmd.charAt(2) == 'R') { TURN_MS_PER_DEG_R = cmd.substring(3).toFloat(); Serial.printf("TURN_MS_PER_DEG_R=%.2f\n", TURN_MS_PER_DEG_R); }
      else if (cmd.length() > 2 && cmd.charAt(1) == 'o' && cmd.charAt(2) == 'L') { TURN_MS_OFFSET_L = cmd.substring(3).toFloat(); Serial.printf("TURN_MS_OFFSET_L=%.1f\n", TURN_MS_OFFSET_L); }
      else if (cmd.length() > 2 && cmd.charAt(1) == 'o' && cmd.charAt(2) == 'R') { TURN_MS_OFFSET_R = cmd.substring(3).toFloat(); Serial.printf("TURN_MS_OFFSET_R=%.1f\n", TURN_MS_OFFSET_R); }
      else {
        float deg = cmd.substring(1).toFloat();
        Serial.printf(">> Re %.0f do...\n", deg);
        turnRelative(deg);
      }
      break;

    case 'i': // dao chieu runtime: iA hoac iB
      if (cmd.indexOf('A') > 0 || cmd.indexOf('a') > 0) { INVERT_A = !INVERT_A; Serial.printf("INVERT_A=%d\n", INVERT_A); }
      else if (cmd.indexOf('B') > 0 || cmd.indexOf('b') > 0) { INVERT_B = !INVERT_B; Serial.printf("INVERT_B=%d\n", INVERT_B); }
      else Serial.println("Dung: iA hoac iB");
      break;

    // ----- Do line (MUX) -----
    case 'm': streamLine = true;  Serial.println("Stream 8 mat: ON"); break;
    case 'M': streamLine = false; Serial.println("Stream 8 mat: OFF"); break;
    case 'n': printLine(); break;
    case 'c': calibrateLine(); break;

    // ----- Bam line PID -----
    case 'g':
      lineFollow = true; lastError = 0; errIntegral = 0;
      Serial.println("BAM LINE: ON");
      break;
    case 'B': baseSpeed = constrain(cmd.substring(1).toInt(), 0, 255);
              Serial.printf("baseSpeed = %d\n", baseSpeed); break;
    case 'k':
      if (cmd.startsWith("kp")) { Kp = cmd.substring(2).toFloat(); Serial.printf("Kp=%.2f\n", Kp); }
      else if (cmd.startsWith("ki")) { Ki = cmd.substring(2).toFloat(); Serial.printf("Ki=%.2f\n", Ki); }
      else if (cmd.startsWith("kd")) { Kd = cmd.substring(2).toFloat(); Serial.printf("Kd=%.2f\n", Kd); }
      else Serial.println("Dung: kp.. / ki.. / kd..");
      break;

    // ----- Cau hinh mang (WiFi + IP hub) -----
    // Wn<ten>  = dat ten WiFi     Wp<mk> = dat mat khau
    // Wh<ip>   = dat IP laptop/hub Ws     = luu NVS + ket noi lai
    // W        = xem cau hinh hien tai
    case 'W': {
      if (cmd.length() == 1) {
        Serial.printf("SSID='%s'  PASS='%s'  HUB=%s:%d\n",
                      cfgSsid.c_str(), cfgPass.c_str(), cfgHub.c_str(), HUB_PORT);
        break;
      }
      char sub = cmd.charAt(1);
      String val = cmd.substring(2); val.trim();
      if (sub == 'n')      { cfgSsid = val; Serial.printf("SSID = '%s'\n", cfgSsid.c_str()); }
      else if (sub == 'p') { cfgPass = val; Serial.printf("PASS = '%s'\n", cfgPass.c_str()); }
      else if (sub == 'h') { cfgHub  = val; Serial.printf("HUB  = %s\n", cfgHub.c_str()); }
      else if (sub == 's') {
        saveNetConfig();
        Serial.println(F(">> Ket noi lai voi cau hinh moi..."));
        WiFi.disconnect(); wifiOK = false; hubOK = false;
        setupWiFi();          // -> onWifiUp() tu bat web + hub + OTA
      }
      else Serial.println(F("Dung: Wn<ten> / Wp<mk> / Wh<ip> / Ws (luu+ket noi) / W (xem)"));
      break;
    }

    // ----- Khac -----
    case 'z': beep(150); Serial.println("Beep"); break;
    case '?': printMenu(); break;
    default:  Serial.printf("Lenh khong hieu: '%s' (go ? de xem menu)\n", cmd.c_str());
  }
}

// ===================== Web server =====================
const char PAGE_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html><html lang="vi"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 Car</title>
<style>
 body{font-family:system-ui,sans-serif;text-align:center;background:#111;color:#eee;margin:0;padding:20px}
 h1{font-size:1.4em}
 button{font-size:1.6em;padding:22px 0;width:80%;margin:12px 0;border:0;border-radius:14px;color:#fff;font-weight:700}
 .start{background:#1e9e54}.stop{background:#c0392b}
 #st{font-size:1.1em;margin-top:18px;line-height:1.7em}
 .on{color:#2ecc71}.off{color:#e74c3c}
</style></head><body>
<h1>🚗 ESP32 Car — Bám line</h1>
<button class="start" onclick="cmd('start')">▶ START</button>
<button class="stop" onclick="cmd('stop')">■ STOP</button>
<div id="st">...</div>
<script>
 function cmd(c){fetch('/'+c).then(()=>upd())}
 function upd(){fetch('/status').then(r=>r.json()).then(s=>{
   document.getElementById('st').innerHTML=
    'Trạng thái: <b class="'+(s.run?'on':'off')+'">'+(s.run?'ĐANG CHẠY':'DỪNG')+'</b><br>'+
    'err='+s.err.toFixed(2)+(s.lost?' ⚠️ MẤT LINE':'')+'<br>'+
    'x='+s.x+' y='+s.y+' θ='+s.th+'°';
 }).catch(()=>{})}
 setInterval(upd,400);upd();
</script></body></html>
)HTML";

void handleRoot()   { server.send_P(200, "text/html", PAGE_HTML); }
void handleStart()  { lineFollow = true; lastError = 0; errIntegral = 0;
                      Serial.println("[web] START bam line");
                      server.send(200, "text/plain", "started"); }
void handleStop()   { lineFollow = false; stopMotors();
                      Serial.println("[web] STOP");
                      server.send(200, "text/plain", "stopped"); }
void handleStatus() {
  char buf[160];
  snprintf(buf, sizeof(buf),
    "{\"run\":%d,\"err\":%.2f,\"lost\":%d,\"x\":%.0f,\"y\":%.0f,\"th\":%.0f}",
    lineFollow ? 1 : 0, lineError, lineLost ? 1 : 0,
    poseX, poseY, poseTheta * 180.0f / PI);
  server.send(200, "application/json", buf);
}

// ===================== DIEU PHOI: route tu hub =====================
// Gui trang thai xe ve hub (khop {status,node,pos,cargo} ma server.js cho).
// Frame odometry TRUNG frame map (HOME=(0,0), nhin +x=0deg) nen gui thang pose.
void sendTelemetry() {
  if (!hubOK) return;
  const char* st = running ? (cargo ? "moving" : "arrived") : "idle";
  char node[16];
  if (routeTarget.length()) snprintf(node, sizeof(node), "\"%s\"", routeTarget.c_str());
  else strcpy(node, "null");
  char buf[176];
  snprintf(buf, sizeof(buf),
    "{\"status\":\"%s\",\"node\":%s,\"pos\":{\"x\":%.0f,\"y\":%.0f,\"th\":%.0f},\"cargo\":%s}",
    st, node, poseX, poseY, poseTheta * 180.0f / PI, cargo ? "true" : "false");
  wsClient.sendTXT(buf);
}

// In log ra Serial VA day len hub duoi dang {type:"clog",text:...} de web hien thi.
void hubLog(const String& s) {
  Serial.println(s);
  if (!hubOK) return;
  JsonDocument d;
  d["type"] = "clog";
  d["text"] = s;
  String out;
  serializeJson(d, out);
  wsClient.sendTXT(out);
}

// Lai tay F/B/L/R (huy route dang chay). 'S'/khac = dung.
void doManual(const String& dir) {
  running = false; lineFollow = false; routeTarget = "";
  char d = dir.length() ? dir.charAt(0) : 'S';
  int s = motorSpeed;
  // !! PHAN CUNG: chan MA_* thuc te chay banh PHAI, MB_* chay banh TRAI (bi trao khi lap).
  // Da kiem chung: 'F'/'B' khong bi anh huong (2 banh cung chieu);
  // 'L' cu (A=-1,B=+1) lam xe quay PHAI -> phai doi cho L/R.
  // turnRelative va lineFollowStep KHONG sua: chung dung driveA/driveB doi xung nen
  // cai trao nay TU TRIET TIEU (da do: re +90 -> +89.8 do, dung chieu).
  if      (d == 'F') { setMotorA(+1, s); setMotorB(+1, s); }
  else if (d == 'B') { setMotorA(-1, s); setMotorB(-1, s); }
  else if (d == 'L') { setMotorA(+1, s); setMotorB(-1, s); }   // banh phai tien, banh trai lui
  else if (d == 'R') { setMotorA(-1, s); setMotorB(+1, s); }   // banh phai lui, banh trai tien
  else stopMotors();
}

// Tha hang: dung -> ha servo -> beep -> ve goc giu.
void doDrop() {
  stopMotors();
  servo.write(SERVO_DROP);
  cargo = false;
  beep(120);
  delay(400);                 // cho co cau tha xong
  servo.write(SERVO_HOLD);
  hubLog("[drop] Da tha hang");
  sendTelemetry();
}

// Bao 1 buoc da xong (ok/that bai) ve server + dung cho buoc ke.
// HOME (buoc cuoi 1 chuyen) hoac that bai -> ket thuc chuyen (running=false).
void finishStep(bool ok, const String& reason) {
  stopMotors();
  String act = curAction;
  if (act == "HOME" || !ok) { running = false; lineFollow = false; }
  if (act == "HOME") cargo = true;
  if (hubOK) {
    JsonDocument d;
    d["type"] = "stepdone";
    d["seq"] = curSeq;
    d["action"] = act;
    d["ok"] = ok;
    d["reason"] = reason;
    JsonObject p = d["pos"].to<JsonObject>();
    p["x"]  = (int)lroundf(poseX);
    p["y"]  = (int)lroundf(poseY);
    p["th"] = (int)lroundf(poseTheta * 180.0f / PI);
    d["lineCount"] = lineCount;
    String out; serializeJson(d, out);
    wsClient.sendTXT(out);
  }
  curAction = "";              // idle -> cho server gui buoc ke
  stepPhase = 0;
  sendTelemetry();
}

// Nap 1 buoc tu server va bat dau thuc thi. seq==0 -> chuyen moi (reset odometry tai HOME).
void beginStep(const String& action, float dist, int seq) {
  if (seq == 0) resetOdometry();     // xe dang o HOME -> xoa troi tich luy
  curAction = action;
  curDist   = dist;
  curSeq    = seq;
  stepPhase = 0; leftStart = false;
  lastError = 0; errIntegral = 0;
  running = true;
  hubLog("[step] " + String(seq) + ": " + action + (dist > 0 ? (" (" + String((int)dist) + "mm)") : ""));
}

// Thuc thi 1 nhip cua buoc HIEN TAI (goi trong loop khi running && curAction != "").
// Xong 1 buoc -> finishStep(...) (gui stepdone + idle cho buoc ke tu server).
// Moi buoc L/R/B = RE truoc (0/+90/-90/180) roi TIEN 1 canh toi giao diem/het line.
void stepExec() {
  String s = curAction;

  if (s == "DROP") { doDrop(); finishStep(true, "drop"); return; }
  if (s == "HOME") {
    // Nhanh HOME tren map chi noi 1 chieu (tu luoi di ra) -> xe LUON toi HOME voi
    // huong ~180 do (nguoc voi luc xuat phat, nhin +x=0 do). Xoay lai ve 0 do de
    // san sang cho chuyen giao ke tiep. Quay xong PHAI bo TIM LAI line (giong het
    // pha 3 cac buoc re khac) truoc khi ket thuc that su - khong lam vay, sai so
    // quay (~5-8 do) de cam bien dung LECH khoi line that, chuyen giao KE TIEP se
    // "khong thay line" ngay tu buoc dau tien (da xay ra thuc te).
    if (stepPhase == 0) {
      turnRelative(180);
      reacqStartMM = (pulsesToMM(encL) + pulsesToMM(encR)) * 0.5f;
      stepPhase = 3;
      return;
    }
    // stepPhase == 3: bo CAN CHINH lai line. Dung nguong >=2 mat (khong phai >=1 nhu
    // reacquire thuong): day la LAN KIEM TRA DAU TIEN ngay sau khi dung yen (0mm di
    // chuyen) - 1 mat le co the bao DUONG TINH GIA do nhieu/mat hong.
    computeLineError();
    if (lineCount >= 2 && !lineLost) { finishStep(true, "home"); return; }  // da can lai line -> xong
    float d = (pulsesToMM(encL) + pulsesToMM(encR)) * 0.5f - reacqStartMM;
    if (d > REACQUIRE_MAX_MM) {   // bo qua xa van khong thay -> thoi, cu ket thuc (tranh xe ket mai)
      finishStep(true, "home: khong can lai duoc line sau khi quay (kiem tra vi tri xe)");
      return;
    }
    int spd = max(REACQUIRE_SPEED, TURN_OPEN_PWM);
    driveA(spd); driveB(spd);
    return;
  }

  if (stepPhase == 0) {                       // --- Pha 0: re dau doan ---
    float deg = 0;
    if      (s == "L") deg = 90;
    else if (s == "R") deg = -90;
    else if (s == "B") deg = 180;
    if (deg != 0) turnRelative(deg);          // rE tai cho (co dinh, blocking)
    segStartX = poseX; segStartY = poseY; leftStart = false;
    lastError = 0; errIntegral = 0;
    reacqStartMM = (pulsesToMM(encL) + pulsesToMM(encR)) * 0.5f;
    stepPhase = 3;                             // -> BO TIM line moi (khong xoay do)
    return;
  }

  // --- Pha 3: sau khi re, BO THANG CHAM de dua cam bien len nhanh moi ---
  if (stepPhase == 3) {
    computeLineError();                        // cap nhat lineCount/lineLost
    if (lineCount >= 1 && !lineLost) {         // da bat duoc line moi
      // KHONG reset segStart o day: giu moc tu pha 0 (= vi tri NODE, truoc khi bo reacquire)
      // de trav trong pha 1 do dung khoang cach truc banh TU NODE -> khop voi curDist
      // (node->node). Reset o day se bo qua doan reacquire -> trav thieu -> gate sai.
      leftStart = false;
      lastError = 0; errIntegral = 0;
      stepPhase = 1;
      return;
    }
    float d = (pulsesToMM(encL) + pulsesToMM(encR)) * 0.5f - reacqStartMM;
    if (d > REACQUIRE_MAX_MM) {                // bo qua xa van khong thay -> DUNG AN TOAN
      finishStep(false, "MAT LINE sau khi re (bo " + String((int)d) + "mm khong thay). Kiem tra goc re / CENTER_OFFSET.");
      return;
    }
    // Dang dung yen (vua RE xong, van toc = 0) truoc khi bo: REACQUIRE_SPEED (130) co the
    // duoi nguong ma sat tinh khi khoi dong tu dung im hoan toan -> xe "e" tai cho, encoder
    // khong tang. Dung TURN_OPEN_PWM lam san toi thieu de dam bao THUC SU lan banh.
    int spd = max(REACQUIRE_SPEED, TURN_OPEN_PWM);
    driveA(spd); driveB(spd);                  // bo thang, KHONG xoay
    return;
  }

  // --- Pha 2: bo thang them de canh TRUC BANH vao tam nga tu -> xong buoc ---
  if (stepPhase == 2) {
    float d = (pulsesToMM(encL) + pulsesToMM(encR)) * 0.5f - centerStartMM;
    if (d < CENTER_OFFSET_MM) { driveA(baseSpeed); driveB(baseSpeed); }   // bo thang
    else { finishStep(true, "toi giao diem"); }                          // da canh tam -> xong buoc
    return;
  }

  // --- Pha 1: tien bam line den giao diem ke ---
  bool found = computeLineError();          // cap nhat lineCount/lineLost/lineError
  float trav = hypotf(poseX - segStartX, poseY - segStartY);
  if (!leftStart && lineCount <= 2 && !lineLost) leftStart = true;   // da roi nga tu cu
  // curDist = khoang cach NODE->NODE (truc banh di het 1 doan). Cam bien o TRUOC truc banh
  // CENTER_OFFSET_MM, nen no cham giao diem ke khi truc banh moi di duoc (curDist - offset).
  // Dat cong 2 phia diem nay 1 khoang dung sai GATE_SLACK: minGate chan nhan nham giao diem
  // giua doan (F gop), maxGate ep dung gan node du KHONG thay tin hieu giao diem (chong vot).
  // curDist==0 (route thu cong khong ghi mm) -> quay ve nguong co dinh cu.
  const float GATE_SLACK = 90.0f;
  float minGate, maxGate;
  if (curDist > 0) {
    float detectTrav = curDist - CENTER_OFFSET_MM;      // trav ky vong khi cam bien cham node
    minGate = max(40.0f, detectTrav - GATE_SLACK);
    maxGate = detectTrav + GATE_SLACK;
  } else {
    minGate = MIN_EDGE; maxGate = MAX_EDGE;
  }
  bool reached = false;
  if (trav > minGate) {
    if (leftStart && (lineCount >= INTERSECT_N || lineLost)) reached = true;  // toi nga tu / het line
    if (trav > maxGate) reached = true;                                       // an toan (chong vot)
  }
  if (reached) {                       // thay nga tu -> canh tam (khong re ngay = tranh re som)
    centerStartMM = (pulsesToMM(encL) + pulsesToMM(encR)) * 0.5f;
    stepPhase = 2;
    return;
  }
  if (found) lineDrivePID();           // bam line binh thuong
  else { driveA(REACQUIRE_SPEED); driveB(REACQUIRE_SPEED); }  // hut line giua doan -> bo thang, KHONG xoay
}

// ===================== Hieu chuan quang duong =====================
// Chay thang bam line tu giao diem hien tai -> giao diem ke (biet knownMM, mac dinh 475).
//
// ⚠️ DAT XE: THANH CAM BIEN phai nam NGAY TREN vach ngang cua nga tu xuat phat
//    (KHONG phai truc banh!). Ly do: ta dem quang duong tu luc bat dau (cam bien @ nga tu 1)
//    den luc CAM BIEN thay nga tu 2 -> dung bang 1 canh (475mm). Neu can truc banh vao
//    nga tu thi cam bien da o truoc 145mm -> do ra ~330mm -> goi y duong kinh sai.
void calibDistance(float knownMM) {
  running = false; lineFollow = false;
  resetOdometry();
  leftStart = false;
  hubLog("[caldist] Chay toi nga tu ke (biet " + String(knownMM, 0) +
         "mm). Luu y: dat THANH CAM BIEN tren nga tu xuat phat.");

  unsigned long t0 = millis();
  // Quang duong TIEN = trung binh CO DAU cua 2 banh.
  // (Dung fabs() la SAI: khi mat line xe xoay tai cho -> L am, R duong -> fabs cong don
  //  thanh "da di xa" du xe dung im -> so do rac.)
  auto travel = []() { return (pulsesToMM(encL) + pulsesToMM(encR)) * 0.5f; };
  const char* why = "TIMEOUT (khong thay nga tu ke)";   // ly do dung
  while (millis() - t0 < 8000) {                 // timeout 8s
    updateOdometry();
    lineFollowStep();                            // bam line 1 nhip
    float trav = travel();
    if (!leftStart && lineCount <= 2 && !lineLost) leftStart = true;   // da roi giao diem xuat phat
    if (trav > MIN_EDGE) {
      if (leftStart && lineCount >= INTERSECT_N) { why = "thay nga tu ke";       break; }
      if (leftStart && lineLost)                 { why = "MAT LINE (do khong tin)"; break; }
      if (trav > MAX_EDGE)                       { why = "qua MAX_EDGE (do khong tin)"; break; }
    }
    delay(5);
  }
  stopMotors();

  float measured = travel();
  bool  ok = (strcmp(why, "thay nga tu ke") == 0) && measured > MIN_EDGE;
  float suggest = (measured > 1) ? WHEEL_DIAMETER_MM * knownMM / measured : WHEEL_DIAMETER_MM;
  char buf[220];
  snprintf(buf, sizeof(buf),
    "[caldist] %s | biet=%.0fmm do=%.1fmm (encL=%ld encR=%ld) | WHEEL_D hien=%.2f -> goi y=%.2fmm%s",
    why, knownMM, measured, encL, encR, WHEEL_DIAMETER_MM, suggest,
    ok ? "" : "   << KHONG DUNG SO NAY, do lai!");
  hubLog(buf);
}

// Su kien WebSocket toi hub.
void onWsEvent(WStype_t type, uint8_t* payload, size_t len) {
  switch (type) {
    case WStype_CONNECTED:
      hubOK = true;
      wsClient.sendTXT("{\"role\":\"car\"}");
      Serial.println("[ws] Da ket noi hub, khai bao role=car");
      hubLog("[fw] build " __DATE__ " " __TIME__);   // danh dau ban firmware dang chay (xac minh OTA)
      break;
    case WStype_DISCONNECTED:
      hubOK = false;
      Serial.println("[ws] Mat ket noi hub");
      break;
    case WStype_TEXT: {
      JsonDocument doc;
      if (deserializeJson(doc, payload, len)) return;   // loi parse -> bo
      const char* cmd = doc["cmd"];
      if (!cmd) return;                                 // hello/khac -> bo qua
      if      (!strcmp(cmd, "step"))   beginStep(String((const char*)(doc["action"] | "F")), doc["dist"] | 0.0f, doc["seq"] | 0);
      else if (!strcmp(cmd, "stop"))   { running = false; curAction = ""; lineFollow = false; stopMotors(); routeTarget = ""; hubLog("[ws] STOP"); }
      else if (!strcmp(cmd, "manual")) doManual(String((const char*)(doc["dir"] | "S")));
      else if (!strcmp(cmd, "drop"))   doDrop();
      else if (!strcmp(cmd, "calib"))  { hubLog("[calib] Bat dau hieu chuan line..."); calibrateLine(); }
      else if (!strcmp(cmd, "tune")) {                  // chinh tham so runtime tu web
        if (!doc["base"].isNull())     baseSpeed     = constrain((int)doc["base"], 0, 255);
        if (!doc["min"].isNull())      MOTOR_MIN_PWM = constrain((int)doc["min"], 0, 255);
        if (!doc["kp"].isNull())       Kp = (float)doc["kp"];
        if (!doc["kd"].isNull())       Kd = (float)doc["kd"];
        if (!doc["openpwm"].isNull())    TURN_OPEN_PWM = constrain((int)doc["openpwm"], 0, 255);
        if (!doc["msperdegL"].isNull())  TURN_MS_PER_DEG_L = (float)doc["msperdegL"];
        if (!doc["msperdegR"].isNull())  TURN_MS_PER_DEG_R = (float)doc["msperdegR"];
        if (!doc["msoffsetL"].isNull())  TURN_MS_OFFSET_L = (float)doc["msoffsetL"];
        if (!doc["msoffsetR"].isNull())  TURN_MS_OFFSET_R = (float)doc["msoffsetR"];
        if (!doc["speed"].isNull())    motorSpeed = constrain((int)doc["speed"], 0, 255);
        if (!doc["wheeld"].isNull())   WHEEL_DIAMETER_MM = (float)doc["wheeld"];
        if (!doc["ppr"].isNull())      ENCODER_PPR = (int)doc["ppr"];
        if (!doc["wbase"].isNull())    WHEEL_BASE_MM = (float)doc["wbase"];
        if (!doc["center"].isNull())   CENTER_OFFSET_MM = constrain((int)doc["center"], 0, 300);
        if (!doc["reacqmax"].isNull()) REACQUIRE_MAX_MM = constrain((int)doc["reacqmax"], 20, 500);
        if (!doc["reacqspd"].isNull()) REACQUIRE_SPEED = constrain((int)doc["reacqspd"], 0, 255);
        if (!doc["intersectn"].isNull()) INTERSECT_N = constrain((int)doc["intersectn"], 2, 7);
        if (!doc["trim"].isNull())     LINE_TRIM = (float)doc["trim"];
        if (!doc["gyrow"].isNull())    GYRO_W = constrain((float)doc["gyrow"], 0.0f, 1.0f);
        if (!doc["gyrosign"].isNull()) GYRO_SIGN = ((int)doc["gyrosign"] >= 0) ? 1 : -1;
        hubLog("[tune] base=" + String(baseSpeed) + " min=" + String(MOTOR_MIN_PWM) +
               " kp=" + String(Kp, 1) + " kd=" + String(Kd, 1) +
               " openpwm=" + String(TURN_OPEN_PWM) +
               " msperdegL=" + String(TURN_MS_PER_DEG_L, 2) + " msperdegR=" + String(TURN_MS_PER_DEG_R, 2) +
               " msoffsetL=" + String(TURN_MS_OFFSET_L, 1) + " msoffsetR=" + String(TURN_MS_OFFSET_R, 1) +
               " speed=" + String(motorSpeed) +
               " wheeld=" + String(WHEEL_DIAMETER_MM, 2) + " ppr=" + String(ENCODER_PPR) +
               " wbase=" + String(WHEEL_BASE_MM, 1) + " center=" + String(CENTER_OFFSET_MM) +
               " intersectn=" + String(INTERSECT_N) +
               " gyro=" + String(mpuOK ? "OK" : "--") + " gyrow=" + String(GYRO_W, 2) +
               " gyrosign=" + String(GYRO_SIGN));
      }
      else if (!strcmp(cmd, "turn")) {                  // test 1 cu re (deg): +trai / -phai
        float deg = doc["deg"] | 90.0;
        hubLog("[turn] test re " + String(deg, 0) + " do...");
        turnRelative(deg);
      }
      else if (!strcmp(cmd, "enc")) {                   // doc encoder (hieu chuan chieu dem)
        if (!doc["reset"].isNull()) resetOdometry();
        hubLog("[enc] L=" + String(encL) + " R=" + String(encR) +
               " x=" + String(poseX, 0) + " y=" + String(poseY, 0) +
               " th=" + String(poseTheta * 180.0f / PI, 1) +
               " | gyro=" + String(mpuOK ? "OK" : "--") +
               (mpuOK ? (" wz=" + String((mpuGyroZraw() - gyroBiasZ) / GYRO_LSB_PER_DPS, 1) + "do/s") : ""));
      }
      else if (!strcmp(cmd, "caldist")) {               // hieu chuan quang duong (1 canh line)
        calibDistance(doc["known"] | 475.0);
      }
      else if (!strcmp(cmd, "gyrocal")) {               // do lai troi tinh gyro (xe phai dung yen)
        stopMotors();
        hubLog("[gyro] Do troi tinh - GIU XE DUNG YEN...");
        gyroCalibrate();
      }
      else if (!strcmp(cmd, "i2cscan"))  i2cScan();      // quet bus I2C (tim MPU6050)
      else if (!strcmp(cmd, "line"))     reportLine();   // doc 8 mat line
      else if (!strcmp(cmd, "linezero")) lineSetZero();  // dat tam line = vi tri hien tai
      else if (!strcmp(cmd, "servo")) {                 // test servo tay tu web (KHONG dong cargo/beep)
        if (!doc["angle"].isNull()) {
          int a = constrain((int)doc["angle"], 0, 180);
          servo.write(a);
          hubLog("[servo] -> " + String(a) + " do (test)");
        } else {
          const char* action = doc["action"] | "";
          if      (!strcmp(action, "drop")) { servo.write(SERVO_DROP); hubLog("[servo] THA (test, goc " + String(SERVO_DROP) + ")"); }
          else if (!strcmp(action, "hold")) { servo.write(SERVO_HOLD); hubLog("[servo] GIU (test, goc " + String(SERVO_HOLD) + ")"); }
        }
      }
      break;
    }
    default: break;
  }
}

// ===================== NVS: luu/nap cau hinh mang =====================
void loadNetConfig() {
  prefs.begin("net", true);
  cfgSsid = prefs.getString("ssid", WIFI_SSID);
  cfgPass = prefs.getString("pass", WIFI_PASS);
  cfgHub  = prefs.getString("hub",  HUB_HOST);
  prefs.end();
  Serial.printf(">> Cau hinh mang: SSID='%s'  HUB=%s\n", cfgSsid.c_str(), cfgHub.c_str());
}
void saveNetConfig() {
  prefs.begin("net", false);
  prefs.putString("ssid", cfgSsid);
  prefs.putString("pass", cfgPass);
  prefs.putString("hub",  cfgHub);
  prefs.end();
  Serial.println(F(">> Da luu cau hinh mang vao NVS (giu sau khi tat nguon)"));
}

void startOTA() {
  if (!wifiOK) return;
  ArduinoOTA.setHostname("esp32-car");
  // ArduinoOTA.setPassword("1234");   // mo neu muon dat mat khau OTA
  ArduinoOTA.onStart([]() { stopMotors(); Serial.println(F("[OTA] Bat dau nap firmware...")); });
  ArduinoOTA.onEnd([]()   { Serial.println(F("\n[OTA] Xong. Khoi dong lai.")); });
  ArduinoOTA.onProgress([](unsigned p, unsigned t) { Serial.printf("[OTA] %u%%\r", p * 100 / t); });
  ArduinoOTA.onError([](ota_error_t e) { Serial.printf("[OTA] Loi %u\n", e); });
  ArduinoOTA.begin();
  Serial.printf(">> OTA san sang: pio run -e ota -t upload  (IP %s)\n", WiFi.localIP().toString().c_str());
}

bool hubStarted = false;          // da goi wsClient.begin() chua

void startHub() {
  if (!wifiOK) return;
  wsClient.begin(cfgHub.c_str(), HUB_PORT, "/");
  wsClient.onEvent(onWsEvent);
  wsClient.setReconnectInterval(3000);
  hubStarted = true;
  Serial.printf(">> Ket noi hub  ws://%s:%d\n", cfgHub.c_str(), HUB_PORT);
}

// ===================== Tu tim hub (UDP broadcast) =====================
// DHCP hay doi IP laptop -> thay vi go tay 'Wh<ip>', xe hoi cA mang:
// broadcast "CAR_WHO?" -> server tra loi "HUB <ip> <port>" -> luu NVS + noi lai.
WiFiUDP disc;
bool discReady = false;

void discoverHub() {
  if (!wifiOK || hubOK) return;                 // da noi duoc hub thi thoi
  if (!discReady) { disc.begin(DISC_PORT); discReady = true; }

  // 1) Doc goi tra loi (neu co)
  int sz = disc.parsePacket();
  if (sz > 0) {
    char buf[64] = {0};
    int n = disc.read(buf, sizeof(buf) - 1);
    if (n > 0 && strncmp(buf, "HUB ", 4) == 0) {
      String ip = String(buf + 4);
      int sp = ip.indexOf(' ');
      if (sp > 0) ip = ip.substring(0, sp);
      ip.trim();
      if (ip.length()) {
        if (ip != cfgHub) {                        // hub doi IP -> luu & noi lai
          buzzerTone(2600, 60); delay(60); buzzerTone(3200, 90);   // 2 bip len giong = DA TIM THAY
          hubLog("[disc] Tim thay hub moi: " + ip + " (cu: " + cfgHub + ") -> luu & noi lai");
          cfgHub = ip;
          saveNetConfig();
          wsClient.disconnect();
          startHub();
        } else if (!hubStarted) {                  // dung IP, chi la chua khoi dong client
          buzzerTone(2600, 60); delay(60); buzzerTone(3200, 90);
          startHub();
        }
        // Neu da begin() dung IP roi -> KHONG goi lai. wsClient tu retry moi 3s.
        // (Goi begin() lap lai se cat ngang bat tay dang do -> KHONG BAO GIO noi duoc.)
      }
      return;
    }
  }

  // 2) Hoi lai moi 3s (kem 1 bip NGAN = dang tim hub)
  static unsigned long lastAsk = 0;
  if (millis() - lastAsk < 3000) return;
  lastAsk = millis();
  IPAddress bc = WiFi.localIP(); bc[3] = 255;   // broadcast trong subnet hien tai
  disc.beginPacket(bc, DISC_PORT);
  disc.print("CAR_WHO?");
  disc.endPacket();
  // bip ngan, tram = dang tim (thua dan: chi bip 3 lan dau roi moi ~15s 1 lan cho do on)
  static int askN = 0;
  askN++;
  if (askN <= 3 || askN % 5 == 0) buzzerTone(1500, 25);
}

// Bat toan bo dich vu mang khi WiFi da len. Goi 1 lan (duoc gac boi wifiOK).
void onWifiUp() {
  wifiOK = true;
  Serial.print(F(">> WiFi OK. Mo trinh duyet: http://"));
  Serial.println(WiFi.localIP());
  server.on("/", handleRoot);
  server.on("/start", handleStart);
  server.on("/stop", handleStop);
  server.on("/status", handleStatus);
  server.begin();
  beep(80);
  startHub();
  startOTA();
}

void setupWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(cfgSsid.c_str(), cfgPass.c_str());
  Serial.printf("Ket noi WiFi '%s' ...\n", cfgSsid.c_str());
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) { delay(250); Serial.print('.'); }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) onWifiUp();
  else Serial.println(F(">> WiFi chua len (qua 10s) - loop se tu bat dich vu khi WiFi len"));
}

// ===================== Setup =====================
void setup() {
  Serial.begin(115200);
  delay(300);

  // Motor pins
  pinMode(MA_IN1, OUTPUT); pinMode(MA_IN2, OUTPUT);
  pinMode(MB_IN3, OUTPUT); pinMode(MB_IN4, OUTPUT);
  ledcSetup(MA_CH, PWM_FREQ, PWM_RES); ledcAttachPin(MA_EN, MA_CH);
  ledcSetup(MB_CH, PWM_FREQ, PWM_RES); ledcAttachPin(MB_EN, MB_CH);
  stopMotors();

  // Buzzer
  pinMode(BUZZER, OUTPUT); digitalWrite(BUZZER, LOW);

  // MUX 74HC4067 (S3 noi GND tren board -> chi 3 chan chon kenh)
  pinMode(MUX_S0, OUTPUT); pinMode(MUX_S1, OUTPUT); pinMode(MUX_S2, OUTPUT);
  analogReadResolution(12);   // 0..4095

  // Encoder pins (input-only 13/14/15/17 deu co pull-up noi)
  pinMode(ENC_L_A, INPUT_PULLUP); pinMode(ENC_L_B, INPUT_PULLUP);
  pinMode(ENC_R_A, INPUT_PULLUP); pinMode(ENC_R_B, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ENC_L_A), isrEncL, RISING);
  attachInterrupt(digitalPinToInterrupt(ENC_R_A), isrEncR, RISING);

  // Servo
  servo.setPeriodHertz(50);
  servo.attach(SERVO_PIN, 500, 2400);
  servo.write(SERVO_HOLD);

  mpuInit();           // I2C + MPU6050 (gyro Z ho tro odometry)
  gyroCalibrate();     // do troi tinh - XE PHAI DUNG YEN luc khoi dong

  loadCalibration();   // nap calibration tu NVS (neu co)
  loadNetConfig();     // nap SSID / mat khau / IP hub tu NVS (neu co)
  setupWiFi();         // ket noi WiFi + bat web server
  startHub();          // WebSocket client toi hub (chi khi da co WiFi)
  startOTA();          // bat nap firmware qua WiFi (OTA)

  Serial.println(F("\n>> Firmware san sang (dieu phoi route tu hub)"));
  printMenu();
}

// ===================== Loop =====================
unsigned long lastPrint = 0;
void loop() {
  // Doc lenh tu Serial
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    handleCommand(line);
  }

  // WiFi co the len TRE hon timeout 10s luc boot (router cham), hoac tu noi lai sau khi rot.
  // Neu khong theo doi -> wifiOK ket false vinh vien: xe co IP, ping duoc, nhung
  // hub/OTA/web KHONG BAO GIO chay (da tung dinh loi nay).
  static unsigned long lastWifiChk = 0;
  if (!wifiOK && millis() - lastWifiChk > 2000) {
    lastWifiChk = millis();
    if (WiFi.status() == WL_CONNECTED) onWifiUp();
  }

  // Web server + WebSocket client toi hub + OTA
  if (wifiOK) { server.handleClient(); wsClient.loop(); ArduinoOTA.handle(); }

  // Chua noi duoc hub -> tu di tim (DHCP doi IP laptop cung khong sao)
  if (wifiOK && !hubOK) discoverHub();

  // Odometry: cap nhat lien tuc
  updateOdometry();

  // Dieu phoi TUNG BUOC: chi chay khi dang co buoc (curAction!=""); xong 1 buoc thi
  // curAction="" -> xe DUNG CHO server gui buoc ke. Neu khong co route, cho phep bam
  // line thu cong (menu 'g').
  static unsigned long lastPid = 0;
  if (running && curAction.length()) {
    if (stepPhase == 0) stepExec();                                    // buoc re (blocking) chay ngay
    else if (millis() - lastPid >= 10) { lastPid = millis(); stepExec(); }  // tien PID ~10ms
  } else if (!running && lineFollow && millis() - lastPid >= 10) {
    lastPid = millis();
    lineFollowStep();
  }

  // Gui telemetry ve hub moi 200ms
  static unsigned long lastTel = 0;
  if (hubOK && millis() - lastTel >= 200) { lastTel = millis(); sendTelemetry(); }

  // Stream 8 mat do line moi 200ms
  static unsigned long lastLine = 0;
  if (streamLine && millis() - lastLine > 200) {
    lastLine = millis();
    printLine();
  }

  // In dinh ky moi 500ms (chi o ban DEBUG)
#if DEBUG_MODE
  if (millis() - lastPrint > 500) {
    lastPrint = millis();
    if (lineFollow) {
      Serial.printf("[line] err=%.2f %s | x=%.0f y=%.0f th=%.0fdeg\n",
                    lineError, lineLost ? "(MAT LINE)" : "",
                    poseX, poseY, poseTheta * 180.0f / PI);
    } else {
      static long lastL = 0, lastR = 0;
      if (encL != lastL || encR != lastR) {
        Serial.printf("[enc] L=%ld (%.1fmm)  R=%ld (%.1fmm)\n",
                      encL, pulsesToMM(encL), encR, pulsesToMM(encR));
        lastL = encL; lastR = encR;
      }
    }
  }
#endif
}

