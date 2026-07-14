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

// ===================== WiFi (STA - noi WiFi nha) =====================
// SUA ten/mat khau WiFi nha ban o day:
#define WIFI_SSID "THANH TUNG L1"       // <-- SUA ten WiFi
#define WIFI_PASS "0348903226"  // <-- SUA mat khau WiFi

// Hub (server Node) - IP may chay server + cong WS THUONG cho xe (= PORT+2 = 3002)
#define HUB_HOST "192.168.1.164"   // <-- SUA IP may chay server.js
#define HUB_PORT 3002

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
void saveNetConfig();
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
#define MUX_S3  33
#define LINE_THRESHOLD 2000   // nguong den/trang (analog 0..4095)
// Mat cam bien con tot (false = bo qua). C1 hong -> mask.
bool SENSOR_OK[8] = { false, true, true, true, true, true, true, true };

// ===================== PWM (LEDC) =====================
#define PWM_FREQ 5000   // 5 kHz cho motor
#define PWM_RES  8      // 8-bit -> duty 0..255
#define MA_CH    2      // LEDC channel Motor A (ENA)
#define MB_CH    3      // LEDC channel Motor B (ENB)
#define BUZZER_CH 1     // LEDC channel Buzzer (phat tone)

// ===================== Thong so hieu chuan =====================
// Sua sau khi do thuc te
float WHEEL_DIAMETER_MM = 70.0;   // duong kinh banh (mm)
int   ENCODER_PPR       = 370;    // so xung / vong
float WHEEL_BASE_MM     = 170.0;  // khoang cach tam 2 banh sau (mm)
float CASTER_DIST_MM    = 100.0;  // khoang cach truc banh sau -> banh tu do (mm)

// Goc servo (do)
int SERVO_HOLD = 0;    // goc GIU hang
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

// --- Odometry (toa do tuong doi) ---
float poseX = 0, poseY = 0, poseTheta = 0;   // mm, mm, rad
long  lastOdoL = 0, lastOdoR = 0;

// --- Re chuan bang PID (quay tai cho theo goc) ---
float KpT = 200.0, KdT = 35.0;    // he so PID re (autotune: err<1deg, settled ~0.6s)
float TURN_TOL_DEG = 1.5;         // sai so chap nhan (do)
int   TURN_MIN = 160;             // PWM toi thieu de thang ma sat khi quay
int   TURN_MAX = 255;             // PWM toi da khi quay (full PWM de du luc pha ma sat khi chinh)
bool  turnVerbose = false;        // true = xuat trace step-response (cho autotune)
bool  INVERT_TURN = false;        // dao chieu actuation vong re (sua bang serial: TI)

// --- Dieu phoi: WebSocket client toi hub + bo thuc thi route ---
WebSocketsClient wsClient;
bool   hubOK = false;             // dang ket noi hub?
String route[48];                 // chuoi buoc F/L/R/B/DROP/HOME
int    routeN = 0, routeIdx = 0;  // so buoc + buoc dang chay
String routeTarget = "";          // ma o dang giao (C1..C9), "" = khong
bool   running = false;           // dang chay 1 don giao?
int    stepPhase = 0;             // 0 = re/khoi dong doan, 1 = dang tien
float  segStartX = 0, segStartY = 0;  // moc odometry dau doan
bool   leftStart = false;         // da roi giao diem xuat phat cua doan chua
bool   cargo = true;              // con hang tren xe?
int    lineCount = 0;             // so mat thay den o lan doc gan nhat
const float MIN_EDGE = 120;       // mm toi thieu 1 doan truoc khi cho ket thuc
const float MAX_EDGE = 750;       // mm toi da 1 doan (chan runaway neu miss giao diem)
const int   INTERSECT_N = 5;      // >= so mat thay den => coi la giao diem (nga tu)

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
  digitalWrite(MUX_S3, (ch >> 3) & 0x01);
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

// ===================== Odometry =====================
// Cap nhat toa do (x,y,theta) tu so xung encoder. Goi lien tuc trong loop.
void updateOdometry() {
  long l = encL, r = encR;
  long dL = l - lastOdoL;
  long dR = r - lastOdoR;
  lastOdoL = l; lastOdoR = r;
  if (dL == 0 && dR == 0) return;

  float mmPerPulse = (PI * WHEEL_DIAMETER_MM) / ENCODER_PPR;
  float sL = dL * mmPerPulse;
  float sR = dR * mmPerPulse;
  float dS = (sL + sR) * 0.5f;                 // quang duong tam xe
  float dTheta = (sR - sL) / WHEEL_BASE_MM;    // thay doi huong (rad)

  // tich phan vi tri (dung huong giua doan de chinh xac hon)
  poseX += dS * cosf(poseTheta + dTheta * 0.5f);
  poseY += dS * sinf(poseTheta + dTheta * 0.5f);
  poseTheta += dTheta;
}

void resetOdometry() {
  encL = 0; encR = 0; lastOdoL = 0; lastOdoR = 0;
  poseX = poseY = poseTheta = 0;
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
  lineError = sum / cnt;                            // vi tri trung binh cua line
  return true;
}

// ===================== Vong dieu khien bam line =====================
void lineFollowStep() {
  bool found = computeLineError();
  if (!found) {
    // Mat line: quay tai cho theo huong lech cuoi cung de tim lai
    int dir = (lastError >= 0) ? 1 : -1;
    driveA(-dir * baseSpeed); driveB(dir * baseSpeed);
    return;
  }
  // PID
  errIntegral += lineError;
  errIntegral = constrain(errIntegral, -50, 50);
  float d = lineError - lastError;
  float correction = Kp * lineError + Ki * errIntegral + Kd * d;
  lastError = lineError;

  int left  = baseSpeed - (int)correction;   // line lech phai (error>0) -> phai nhanh hon, xe re phai
  int right = baseSpeed + (int)correction;
  driveA(left); driveB(right);
}

// ===================== Re chuan bang PID =====================
// Quay tai cho mot goc tuong doi (deg). Duong = quay trai (CCW), am = phai.
// Dung poseTheta lam phan hoi, PID dieu khien toc do quay, co timeout.
void turnRelative(float deg) {
  float startTheta = poseTheta;
  float target = poseTheta + deg * PI / 180.0f;
  float tol = TURN_TOL_DEG * PI / 180.0f;
  float lastErr = 0;
  float maxOverDeg = 0;             // vot lo lon nhat (deg)
  float dirSign = (deg >= 0) ? 1.0f : -1.0f;
  int settle = 0;
  bool settled = false;
  unsigned long t0 = millis();

  // --- Bo nho ghi trace step-response (cho autotune) ---
  const int TRMAX = 500;
  static uint16_t trT[TRMAX];        // thoi gian (ms tu khi bat dau)
  static float    trA[TRMAX];        // goc da quay (deg, theo huong re)
  int trN = 0;

  while (millis() - t0 < 4000) {     // timeout 4s
    updateOdometry();
    float err = target - poseTheta;
    while (err >  PI) err -= 2 * PI;  // chuan hoa [-pi, pi]
    while (err < -PI) err += 2 * PI;

    // theo doi vot lo: tien do theo huong re vuot qua |deg|
    float progDeg = (poseTheta - startTheta) * 180.0f / PI * dirSign;
    float over = progDeg - fabs(deg);
    if (over > maxOverDeg) maxOverDeg = over;

    // ghi trace (goc tuyet doi theo huong re, de luon duong khi tien toi tgt)
    if (trN < TRMAX) { trT[trN] = (uint16_t)(millis() - t0); trA[trN] = progDeg; trN++; }

    if (fabs(err) < tol) {
      stopMotors();
      if (++settle > 8) { settled = true; break; }  // on dinh ~40ms
    } else {
      settle = 0;
      float d = err - lastErr;
      int spd = (int)(KpT * err + KdT * d);
      spd = constrain(spd, -TURN_MAX, TURN_MAX);   // chan toc do -> bot quan tinh/vot lo
      if (abs(spd) < TURN_MIN) spd = (spd >= 0) ? TURN_MIN : -TURN_MIN;
      int sgn = INVERT_TURN ? -1 : 1;
      driveA(sgn * spd); driveB(-sgn * spd);   // quay tai cho: 2 banh nguoc chieu
    }
    lastErr = err;
    delay(5);
  }
  stopMotors();
  float achievedDeg = (poseTheta - startTheta) * 180.0f / PI;
  float errDeg = deg - achievedDeg;
  unsigned long ms = millis() - t0;
  // Dong ket qua co cau truc cho script tu doc/hieu chuan (Serial + Log xe tren web):
  char res[160];
  snprintf(res, sizeof(res),
    "[turnres] tgt=%.1f dat=%.2f err=%.2f votlo=%.2f ms=%lu %s",
    deg, achievedDeg, errDeg, maxOverDeg, ms, settled ? "ON DINH" : "TIMEOUT");
  hubLog(res);

  // Xuat trace day du de host nhan dang he & tinh PID toi uu (1 lan test)
  if (turnVerbose) {
    Serial.printf("[trace_begin] tgt=%.1f kp=%.1f kd=%.1f n=%d\n", fabs(deg), KpT, KdT, trN);
    for (int i = 0; i < trN; i++) Serial.printf("%u,%.2f\n", trT[i], trA[i]);
    Serial.println("[trace_end]");
  }
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
  Serial.println(F("--- RE CHUAN (PID) ---"));
  Serial.println(F("  T<goc> = re tai cho (vd T90, T-90)"));
  Serial.println(F("  Tp<so> Td<so> = chinh he so PID re"));
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
    // ----- Re chuan PID -----
    case 'T':
      if (cmd.length() > 1 && cmd.charAt(1) == 'p') { KpT = cmd.substring(2).toFloat(); Serial.printf("KpT=%.1f\n", KpT); }
      else if (cmd.length() > 1 && cmd.charAt(1) == 'd') { KdT = cmd.substring(2).toFloat(); Serial.printf("KdT=%.1f\n", KdT); }
      else if (cmd.length() > 1 && cmd.charAt(1) == 'V') { turnVerbose = !turnVerbose; Serial.printf("turnVerbose=%d\n", turnVerbose); }
      else if (cmd.length() > 1 && cmd.charAt(1) == 'I') { INVERT_TURN = !INVERT_TURN; Serial.printf("INVERT_TURN=%d\n", INVERT_TURN); }
      else if (cmd.length() > 1 && cmd.charAt(1) == 'n') { TURN_MIN = cmd.substring(2).toInt(); Serial.printf("TURN_MIN=%d\n", TURN_MIN); }
      else if (cmd.length() > 1 && cmd.charAt(1) == 'x') { TURN_MAX = cmd.substring(2).toInt(); Serial.printf("TURN_MAX=%d\n", TURN_MAX); }
      else if (cmd.length() > 1 && cmd.charAt(1) == 'o') { TURN_TOL_DEG = cmd.substring(2).toFloat(); Serial.printf("TURN_TOL_DEG=%.1f\n", TURN_TOL_DEG); }
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
        setupWiFi(); startHub();
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
  if      (d == 'F') { setMotorA(+1, s); setMotorB(+1, s); }
  else if (d == 'B') { setMotorA(-1, s); setMotorB(-1, s); }
  else if (d == 'L') { setMotorA(-1, s); setMotorB(+1, s); }
  else if (d == 'R') { setMotorA(+1, s); setMotorB(-1, s); }
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

// Ket thuc don (da ve HOME): dung, san sang don ke tiep.
void finishRoute() {
  stopMotors(); lineFollow = false;
  running = false; stepPhase = 0;
  routeTarget = ""; cargo = true;
  hubLog("[route] xong -> ve HOME, san sang");
  sendTelemetry();
}

// Nap chuoi buoc tu hub va bat dau. Re-anchor odometry tai HOME.
void startRoute(JsonArrayConst steps, const char* target) {
  routeN = 0;
  for (JsonVariantConst v : steps) if (routeN < 48) route[routeN++] = String(v.as<const char*>());
  routeIdx = 0; stepPhase = 0; leftStart = false;
  routeTarget = target ? target : "";
  cargo = true;
  resetOdometry();            // xe dang o HOME -> xoa troi tich luy
  running = true;
  hubLog("[route] bat dau -> " + routeTarget + " (" + String(routeN) + " buoc)");
}

// Chay 1 nhip cua route (goi trong loop khi running).
// Moi buoc F/L/R/B = RE truoc (0/+90/-90/180) roi TIEN 1 canh toi giao diem/het line.
void routeStep() {
  if (routeIdx >= routeN) { finishRoute(); return; }
  String s = route[routeIdx];

  if (s == "DROP") { doDrop(); routeIdx++; return; }
  if (s == "HOME") { routeIdx++; return; }   // buoc cuoi -> finishRoute o nhip sau

  if (stepPhase == 0) {                       // --- Pha 0: re dau doan ---
    float deg = 0;
    if      (s == "L") deg = 90;
    else if (s == "R") deg = -90;
    else if (s == "B") deg = 180;
    if (deg != 0) turnRelative(deg);          // rE tai cho (PID, blocking)
    segStartX = poseX; segStartY = poseY; leftStart = false;
    lastError = 0; errIntegral = 0;
    stepPhase = 1;
    return;
  }

  // --- Pha 1: tien bam line den giao diem ke (hoac het line = tam o) ---
  lineFollowStep();
  float trav = hypotf(poseX - segStartX, poseY - segStartY);
  if (!leftStart && lineCount <= 2 && !lineLost) leftStart = true;   // da roi nga tu cu
  bool done = false;
  if (trav > MIN_EDGE) {
    if (leftStart && (lineCount >= INTERSECT_N || lineLost)) done = true;  // toi nga tu / het line
    if (trav > MAX_EDGE) done = true;                                       // an toan
  }
  if (done) { stopMotors(); stepPhase = 0; routeIdx++; }
}

// Su kien WebSocket toi hub.
void onWsEvent(WStype_t type, uint8_t* payload, size_t len) {
  switch (type) {
    case WStype_CONNECTED:
      hubOK = true;
      wsClient.sendTXT("{\"role\":\"car\"}");
      Serial.println("[ws] Da ket noi hub, khai bao role=car");
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
      if      (!strcmp(cmd, "route"))  startRoute(doc["steps"].as<JsonArrayConst>(), doc["target"] | "");
      else if (!strcmp(cmd, "stop"))   { running = false; lineFollow = false; stopMotors(); routeTarget = ""; hubLog("[ws] STOP"); }
      else if (!strcmp(cmd, "manual")) doManual(String((const char*)(doc["dir"] | "S")));
      else if (!strcmp(cmd, "drop"))   doDrop();
      else if (!strcmp(cmd, "calib"))  { hubLog("[calib] Bat dau hieu chuan line..."); calibrateLine(); }
      else if (!strcmp(cmd, "tune")) {                  // chinh tham so runtime tu web
        if (!doc["base"].isNull())     baseSpeed     = constrain((int)doc["base"], 0, 255);
        if (!doc["min"].isNull())      MOTOR_MIN_PWM = constrain((int)doc["min"], 0, 255);
        if (!doc["kp"].isNull())       Kp = (float)doc["kp"];
        if (!doc["kd"].isNull())       Kd = (float)doc["kd"];
        if (!doc["turnmin"].isNull())  TURN_MIN = constrain((int)doc["turnmin"], 0, 255);
        if (!doc["turnmax"].isNull())  TURN_MAX = constrain((int)doc["turnmax"], 0, 255);
        if (!doc["speed"].isNull())    motorSpeed = constrain((int)doc["speed"], 0, 255);
        if (!doc["kpt"].isNull())      KpT = (float)doc["kpt"];
        if (!doc["kdt"].isNull())      KdT = (float)doc["kdt"];
        if (!doc["turntol"].isNull())  TURN_TOL_DEG = (float)doc["turntol"];
        hubLog("[tune] base=" + String(baseSpeed) + " min=" + String(MOTOR_MIN_PWM) +
               " kp=" + String(Kp, 1) + " kd=" + String(Kd, 1) +
               " turnmin=" + String(TURN_MIN) + " turnmax=" + String(TURN_MAX) +
               " kpt=" + String(KpT, 1) + " kdt=" + String(KdT, 1) +
               " tol=" + String(TURN_TOL_DEG, 1) + " speed=" + String(motorSpeed));
      }
      else if (!strcmp(cmd, "turn")) {                  // test 1 cu re (deg): +trai / -phai
        float deg = doc["deg"] | 90.0;
        hubLog("[turn] test re " + String(deg, 0) + " do...");
        turnRelative(deg);
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

void startHub() {
  if (!wifiOK) return;
  wsClient.begin(cfgHub.c_str(), HUB_PORT, "/");
  wsClient.onEvent(onWsEvent);
  wsClient.setReconnectInterval(3000);
  Serial.printf(">> Ket noi hub  ws://%s:%d\n", cfgHub.c_str(), HUB_PORT);
}

void setupWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(cfgSsid.c_str(), cfgPass.c_str());
  Serial.printf("Ket noi WiFi '%s' ...\n", cfgSsid.c_str());
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) { delay(250); Serial.print('.'); }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    wifiOK = true;
    Serial.print(F(">> WiFi OK. Mo trinh duyet: http://"));
    Serial.println(WiFi.localIP());
    server.on("/", handleRoot);
    server.on("/start", handleStart);
    server.on("/stop", handleStop);
    server.on("/status", handleStatus);
    server.begin();
    beep(80);
  } else {
    Serial.println(F(">> WiFi FAIL - chay khong web (van dieu khien qua Serial)"));
  }
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

  // MUX 74HC4067
  pinMode(MUX_S0, OUTPUT); pinMode(MUX_S1, OUTPUT);
  pinMode(MUX_S2, OUTPUT); pinMode(MUX_S3, OUTPUT);
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

  // Web server + WebSocket client toi hub + OTA
  if (wifiOK) { server.handleClient(); wsClient.loop(); ArduinoOTA.handle(); }

  // Odometry: cap nhat lien tuc
  updateOdometry();

  // Dieu phoi: chay route tu hub; neu khong, cho phep bam line thu cong (menu 'g')
  static unsigned long lastPid = 0;
  if (running) {
    if (stepPhase == 0) routeStep();                                    // buoc re (blocking) chay ngay
    else if (millis() - lastPid >= 10) { lastPid = millis(); routeStep(); }  // tien PID ~10ms
  } else if (lineFollow && millis() - lastPid >= 10) {
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

