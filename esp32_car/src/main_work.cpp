/*
 * MAIN WORK - FIRMWARE RELEASE XE DÒ LINE ESP32
 *
 * Bản mã nguồn rút gọn để trình bày trong báo cáo.
 * Chỉ giữ các chức năng vận hành chính: đọc cảm biến, điều khiển động cơ,
 * PID bám line, encoder/gyro, điều phối lộ trình, WiFi, WebSocket, OTA và NVS.
 * Đã loại bỏ: menu Serial, in telemetry debug, web local, lệnh test và hiệu chuẩn.
 * Thông tin WiFi/IP bên dưới là giá trị minh họa để bảo mật khi đưa vào báo cáo.
 */

#include <Arduino.h>
#include <ESP32Servo.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <Wire.h>
#include <WiFiUdp.h>

// ===================== WiFi (STA - noi WiFi nha) =====================
// SUA ten/mat khau WiFi nha ban o day:
#define WIFI_SSID "TEN_WIFI"
#define WIFI_PASS "MAT_KHAU_WIFI"

// Hub (server Node) - IP may chay server + cong WS THUONG cho xe (= PORT+2 = 3002)
// Chi la MAC DINH du phong: xe TU TIM hub qua UDP broadcast (khong can sua tay).
#define HUB_HOST "192.168.1.100"
#define HUB_PORT 3002
#define DISC_PORT 3003             // cong UDP discovery cua server (= PORT+3)

bool wifiOK = false;
Preferences prefs;

// ===== Cấu hình mạng runtime, lưu trong NVS; mặc định lấy từ hằng số phía trên =====
String cfgSsid = WIFI_SSID;   // ten WiFi
String cfgPass = WIFI_PASS;   // mat khau WiFi
String cfgHub  = HUB_HOST;    // IP laptop chay server.js

// Khai báo trước các hàm dịch vụ
void setupWiFi();
void startHub();
void startOTA();
void onWifiUp();
void saveNetConfig();
bool computeLineError();
void hubLog(const String& s);

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
int   lineRaw[8];                  // gia tri analog 8 mat (C1..C8)
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

// --- Re theo GOC do bang GYRO (khong phu thuoc pin) ---
// Ban open-loop cu (PWM + THOI GIAN co dinh) bi loi: pin day hon -> cung PWM nhung banh
// quay nhanh hon -> cung thoi gian nhung GOC LON HON -> vot, mat line. Thoi gian khong
// "biet" xe da quay bao nhieu do.
// FIX TRIET DE: quay o PWM co dinh, DOC GOC THAT tu gyro (poseTheta) lien tuc, dung NGAY
// khi dat du goc (tru TURN_LEAD_DEG de bu quan tinh khi cat dien). Do dung theo goc THAT
// nen KHONG phu thuoc dien ap pin. KHONG servo/sua lui -> KHONG lac (quay 1 lan roi thoi).
// Khong co gyro (mpuOK=false) -> fallback ve open-loop thoi gian (ms/do).
int   TURN_OPEN_PWM   = 200;      // PWM khi re (thap hon 1 chut cho bot quan tinh; du thang ma sat)
// LEAD = dung SOM bao nhieu do de bu quan tinh (xe con truot them sau khi cat dien).
// TACH RIENG trai/phai: 2 ben khong doi xung (ma sat/motor) nen vot lo khac nhau - do thuc te
// trai vot ~0.7 do, phai vot ~1.4-2.9 do. Hieu chuan bang script tune-turn.js (re +90/-90).
// Do tu LOG ROUTE THAT (dang tin hon tune quay tai cho): luong truot them sau khi cat dien
// = lead + vot, trung binh ~10.8 do (trai) va ~10.4 do (phai), gan nhu KHONG doi giua goc
// 90 va 180 -> lead hang so la mo hinh dung.
float TURN_LEAD_DEG_L = 10.8;     // ben TRAI  (goc > 0, CCW)
float TURN_LEAD_DEG_R = 10.4;     // ben PHAI  (goc < 0, CW)
int   TURN_TIMEOUT_MS = 4000;     // gioi han an toan: qua thoi gian nay chua dat goc -> dung
// Fallback open-loop (chi dung khi KHONG co gyro): ms = OFFSET + |goc|*PERDEG, rieng trai/phai.
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
float  curNodeX = 0, curNodeY = 0; // toa do NODE xe toi sau buoc nay (server gui) -> RE-ANCHOR
bool   curNodeValid = false;      // co toa do node de snap odometry khong
float  curNodeTh = 0;             // huong doan di vao node (rad) -> snap poseTheta
bool   curNodeThValid = false;    // co huong de snap khong (DROP: khong)
String routeTarget = "";          // ma o dang giao (C1..C9), "" = khong
bool   running = false;           // dang trong 1 chuyen giao?
int    stepPhase = 0;             // 0 = re/khoi dong doan, 1 = tien bam line, 2 = bo canh tam nga tu
float  segStartX = 0, segStartY = 0;  // moc odometry dau doan
bool   leftStart = false;         // da roi giao diem xuat phat cua doan chua
bool   cargo = true;              // con hang tren xe?
int    lineCount = 0;             // so mat thay den o lan doc gan nhat
const float MIN_EDGE = 120;       // mm toi thieu 1 doan truoc khi cho ket thuc
const float MAX_EDGE = 750;       // mm toi da 1 doan (chan runaway neu miss giao diem)
int    INTERSECT_N = 3;           // >= so mat thay den => coi la giao diem (nga tu).
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
int    REACQUIRE_MAX_MM = 220;    // bo toi da bao nhieu mm de tim; qua -> dung an toan (noi rong de bot MAT LINE o nhanh)
float  reacqStartMM = 0;

// ===================== Encoder ISR (quadrature) =====================
/**
 * Chức năng: Ngắt encoder bánh trái; cập nhật số xung và chiều quay từ kênh B.
 */
void IRAM_ATTR isrEncL() {
  // doc kenh B de biet chieu
  if (digitalRead(ENC_L_B)) encL++; else encL--;
}
/**
 * Chức năng: Ngắt encoder bánh phải; cập nhật số xung và chiều quay từ kênh B.
 */
void IRAM_ATTR isrEncR() {
  if (digitalRead(ENC_R_B)) encR++; else encR--;
}

// ===================== MUX 74HC4067 =====================
// Doc 1 kenh (0..15)
/**
 * Chức năng: Chọn một kênh của MUX 74HC4067 và lấy trung bình bốn mẫu ADC để giảm nhiễu.
 */
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
/**
 * Chức năng: Đọc liên tiếp tám mắt cảm biến dò line vào mảng lineRaw.
 */
void readLine() {
  for (int i = 0; i < 8; i++) lineRaw[i] = readMuxChannel(i);
}

// In gia tri 8 mat + dang nhi phan theo nguong

// ===================== Motor =====================
// chieu: +1 thuan, -1 nghich, 0 dung
// Bu vung chet: neu co lenh chay (dir!=0) ma PWM < nguong khoi dong -> nang len MOTOR_MIN_PWM
/**
 * Chức năng: Bù vùng chết động cơ bằng cách nâng PWM quá thấp lên ngưỡng khởi động tối thiểu.
 */
static inline int applyMinPwm(int dir, int spd) {
  if (dir != 0 && spd > 0 && spd < MOTOR_MIN_PWM) return MOTOR_MIN_PWM;
  return spd;
}
/**
 * Chức năng: Điều khiển chiều và tốc độ PWM của động cơ A.
 */
void setMotorA(int dir, int spd) {
  if (INVERT_A) dir = -dir;
  spd = applyMinPwm(dir, spd);
  digitalWrite(MA_IN1, dir > 0);
  digitalWrite(MA_IN2, dir < 0);
  ledcWrite(MA_CH, dir == 0 ? 0 : spd);
}
/**
 * Chức năng: Điều khiển chiều và tốc độ PWM của động cơ B.
 */
void setMotorB(int dir, int spd) {
  if (INVERT_B) dir = -dir;
  spd = applyMinPwm(dir, spd);
  digitalWrite(MB_IN3, dir > 0);
  digitalWrite(MB_IN4, dir < 0);
  ledcWrite(MB_CH, dir == 0 ? 0 : spd);
}
/**
 * Chức năng: Dừng đồng thời cả hai động cơ.
 */
void stopMotors() { setMotorA(0, 0); setMotorB(0, 0); }

// Dieu khien motor theo van toc co dau (-255..255)
/**
 * Chức năng: Nhận vận tốc có dấu và chuyển thành chiều quay/PWM cho động cơ A.
 */
void driveA(int v) { v = constrain(v, -255, 255); setMotorA(v > 0 ? 1 : (v < 0 ? -1 : 0), abs(v)); }
/**
 * Chức năng: Nhận vận tốc có dấu và chuyển thành chiều quay/PWM cho động cơ B.
 */
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

/**
 * Chức năng: Ghi một thanh ghi cấu hình vào cảm biến MPU6050 qua I2C.
 */
void mpuWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MPU_ADDR); Wire.write(reg); Wire.write(val); Wire.endTransmission();
}
/**
 * Chức năng: Đọc giá trị thô vận tốc góc quanh trục Z của MPU6050.
 */
int16_t mpuGyroZraw() {
  Wire.beginTransmission(MPU_ADDR); Wire.write(0x47);        // GYRO_ZOUT_H
  if (Wire.endTransmission(false) != 0) return 0;
  if (Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)2) != 2) return 0;
  return (int16_t)((Wire.read() << 8) | Wire.read());
}
// Do troi tinh gyro - XE PHAI DUNG YEN khi goi
/**
 * Chức năng: Tính độ lệch tĩnh của gyro khi xe đứng yên để giảm trôi góc.
 */
void gyroCalibrate(int n = 500) {
  if (!mpuOK) { hubLog("[gyro] chua co MPU6050"); return; }
  double sum = 0;
  for (int i = 0; i < n; i++) { sum += mpuGyroZraw(); delay(2); }
  gyroBiasZ = (float)(sum / n);
  gyroDelta = 0; lastGyroUs = micros();
  hubLog("[gyro] bias Z = " + String(gyroBiasZ, 1) + " LSB (" +
         String(gyroBiasZ / GYRO_LSB_PER_DPS, 2) + " do/s)");
}
/**
 * Chức năng: Khởi tạo I2C, kiểm tra MPU6050 và cấu hình dải đo cùng bộ lọc số.
 */
void mpuInit() {
  Wire.begin(MPU_SDA, MPU_SCL, 400000);
  Wire.beginTransmission(MPU_ADDR); Wire.write(0x75);        // WHO_AM_I
  uint8_t who = 0;
  if (Wire.endTransmission(false) == 0 && Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)1) == 1)
    who = Wire.read();
  mpuOK = (who != 0x00 && who != 0xFF);
  if (!mpuOK) { return; }
  mpuWrite(0x6B, 0x80); delay(100);        // reset
  mpuWrite(0x6B, 0x01); delay(10);         // wake, clock = gyro X (on dinh hon)
  mpuWrite(0x1A, 0x03);                    // DLPF ~44Hz (loc rung motor)
  mpuWrite(0x1B, 0x10);                    // gyro +-1000 do/s (quay tai cho nhanh de vuot +-250 -> bao hoa/clip)
  delay(50);
  lastGyroUs = micros();
}
// Quet bus I2C: liet ke dia chi tim thay. Neu thay MPU ma chua init -> init lai.

// Toc do goc hien tai (do/s) tu gyro. 0 neu khong co MPU.
float gyroRateDps() {
  if (!mpuOK) return 0;
  return GYRO_SIGN * (mpuGyroZraw() - gyroBiasZ) / GYRO_LSB_PER_DPS;
}

// Lay mau gyro va cong don goc. Tu gioi han ~250Hz.
/**
 * Chức năng: Lấy mẫu gyro theo chu kỳ và tích phân vận tốc góc thành góc quay.
 */
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
/**
 * Chức năng: Cập nhật tọa độ x, y và hướng xe bằng encoder kết hợp gyro.
 */
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

/**
 * Chức năng: Đưa bộ đếm encoder và trạng thái odometry về gốc.
 */
void resetOdometry() {
  encL = 0; encR = 0; lastOdoL = 0; lastOdoR = 0;
  poseX = poseY = poseTheta = 0;
  gyroDelta = 0; lastGyroUs = micros();   // bo goc gyro con ton dong
}

// In toa do hien tai

// ===================== Tinh sai so line =====================
// Tra ve TRUE neu thay line. Cap nhat lineError (am=line lech trai, duong=phai).
// Den = gia tri THAP -> mat thay line khi raw < nguong.
/**
 * Chức năng: Tính sai số vị trí line từ các cảm biến còn hoạt động và phát hiện mất line.
 */
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
/**
 * Chức năng: Tính hiệu chỉnh PID và phân phối tốc độ cho hai bánh để bám line.
 */
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
/**
 * Chức năng: Thực hiện một chu kỳ bám line; khi mất line sẽ quay tìm theo sai số gần nhất.
 */
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

// ===================== Re theo GOC (gyro feedback) =====================
// Quay tai cho mot goc tuong doi (deg). Duong = quay trai (CCW), am = phai.
// CACH LAM (khong phu thuoc pin): chay PWM co dinh, doc GOC THAT (poseTheta, hop nhat gyro)
// lien tuc, DUNG NGAY khi da quay du (|goc| >= |deg| - LEAD). LEAD (rieng trai/phai) bu phan
// xe con truot them sau khi cat dien. KHONG servo/sua lui -> quay 1 lan roi thoi, khong lac.
// Neu KHONG co gyro -> fallback open-loop theo thoi gian (ms/do) nhu truoc.
/**
 * Chức năng: Quay xe một góc tương đối; ưu tiên phản hồi gyro và dùng thời gian khi không có gyro.
 */
void turnRelative(float deg) {
  if (fabs(deg) < 0.5f) return;
  float startTheta = poseTheta;
  int sgn = INVERT_TURN ? -1 : 1;
  int dir = (deg >= 0) ? 1 : -1;
  float lead = (deg >= 0) ? TURN_LEAD_DEG_L : TURN_LEAD_DEG_R;   // vot lo khac nhau 2 ben
  unsigned long t0 = millis();
  bool timedOut = false;

  driveA(sgn * dir * TURN_OPEN_PWM); driveB(-sgn * dir * TURN_OPEN_PWM);  // quay tai cho: 2 banh nguoc chieu

  if (mpuOK) {
    // --- Dung theo GOC THAT (khong phu thuoc pin) ---
    float targetAbs = fabs(deg) - lead;
    if (targetAbs < fabs(deg) * 0.5f) targetAbs = fabs(deg) * 0.5f;   // goc nho: it nhat quay nua goc
    while (true) {
      updateOdometry();
      float progAbs = fabs(poseTheta - startTheta) * 180.0f / PI;      // goc DA quay (do)
      if (progAbs >= targetAbs) break;
      if (millis() - t0 > (unsigned long)TURN_TIMEOUT_MS) { timedOut = true; break; }
      delay(2);
    }
  } else {
    // --- Fallback: khong co gyro -> chay theo thoi gian (ms/do), rieng trai/phai ---
    float offset = (deg >= 0) ? TURN_MS_OFFSET_L  : TURN_MS_OFFSET_R;
    float perDeg = (deg >= 0) ? TURN_MS_PER_DEG_L : TURN_MS_PER_DEG_R;
    unsigned long ms = (unsigned long)(offset + fabs(deg) * perDeg + 0.5f);
    while (millis() - t0 < ms) { updateOdometry(); delay(2); }
  }
  stopMotors();

  // Cho dung han + do lai goc that de ghi log (xe con truot 1 chut sau khi cat dien)
  delay(120);
  updateOdometry();
  float achievedDeg = (poseTheta - startTheta) * 180.0f / PI;
  char res[170];
  snprintf(res, sizeof(res),
    "[turnres] tgt=%.1f dat=%.2f err=%.2f vot=%.2f lead=%.2f ms=%lu (%s, pwm=%d)",
    deg, achievedDeg, deg - achievedDeg,
    fabs(achievedDeg) - fabs(deg),          // vot lo (>0 = quay QUA goc, <0 = thieu goc)
    lead, millis() - t0,
    mpuOK ? (timedOut ? "gyro-TIMEOUT" : "gyro") : "thoi-gian", TURN_OPEN_PWM);
  hubLog(res);
}

// ===================== Tien ich =====================
// Phat tone tan so freq (Hz) trong ms mili-giay
/**
 * Chức năng: Phát âm báo bằng PWM với tần số và thời lượng cho trước.
 */
void buzzerTone(int freq, int ms) {
  ledcSetup(BUZZER_CH, freq, 8);
  ledcAttachPin(BUZZER, BUZZER_CH);
  ledcWrite(BUZZER_CH, 128);   // 50% duty
  delay(ms);
  ledcWrite(BUZZER_CH, 0);
}
/**
 * Chức năng: Phát một tiếng bíp ngắn ở tần số mặc định.
 */
void beep(int ms) { buzzerTone(2000, ms); }

// ===================== Cali line =====================
/**
 * Chức năng: Đọc ngưỡng cảm biến line đã hiệu chuẩn từ bộ nhớ NVS.
 */
bool loadCalibration() {
  prefs.begin("line", true);
  bool done = prefs.getBool("done", false);
  if (done) prefs.getBytes("thresh", lineThresh, sizeof(lineThresh));
  prefs.end();
  if (done) {
    lineCalibrated = true;
  }
  return done;
}

/**
 * Chức năng: Đổi số xung encoder thành quãng đường milimét.
 */
float pulsesToMM(long pulses) {
  float circ = PI * WHEEL_DIAMETER_MM;       // chu vi
  return (float)pulses / ENCODER_PPR * circ; // quang duong
}

// ===================== DIEU PHOI: route tu hub =====================
// Gui trang thai xe ve hub (khop {status,node,pos,cargo} ma server.js cho).
// Frame odometry TRUNG frame map (HOME=(0,0), nhin +x=0deg) nen gui thang pose.
/**
 * Chức năng: Gửi trạng thái, vị trí và tình trạng hàng hóa của xe về hub.
 */
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

// Gửi thông báo dạng {type:"clog",text:...} lên hub để giao diện hiển thị.
/**
 * Chức năng: Gửi thông báo vận hành dạng JSON về giao diện quản lý qua WebSocket.
 */
void hubLog(const String& s) {
  if (!hubOK) return;
  JsonDocument d;
  d["type"] = "clog";
  d["text"] = s;
  String out;
  serializeJson(d, out);
  wsClient.sendTXT(out);
}

// Lai tay F/B/L/R (huy route dang chay). 'S'/khac = dung.
/**
 * Chức năng: Điều khiển xe thủ công theo các hướng tiến, lùi, trái, phải hoặc dừng.
 */
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
/**
 * Chức năng: Dừng xe, điều khiển servo thả hàng rồi đưa servo về vị trí giữ.
 */
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
/**
 * Chức năng: Kết thúc một bước lộ trình, báo kết quả và hiệu chỉnh lại odometry tại node.
 */
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
  // RE-ANCHOR: buoc THANH CONG toi 1 node da biet -> snap odometry ve dung toa do + HUONG node,
  // xoa troi tich luy (xe bam line thuc te dung nhung odometry troi dan). Bao pose THO o tren
  // (server do troi that) roi moi snap. Chi snap khi ok (that bai -> xe khong o node).
  if (ok && curNodeValid) {
    poseX = curNodeX;
    poseY = curNodeY;
    if (curNodeThValid) poseTheta = curNodeTh;   // snap ca HUONG -> xoa troi huong tich luy
  }
  curAction = "";              // idle -> cho server gui buoc ke
  stepPhase = 0;
  sendTelemetry();
}

// Nap 1 buoc tu server va bat dau thuc thi. seq==0 -> chuyen moi (reset odometry tai HOME).
// nodeX/nodeY: toa do node xe se toi (server gui de re-anchor); nodeValid=false -> khong snap.
// nodeTh (rad): huong doan di vao node; nodeThValid=false -> khong snap huong.
/**
 * Chức năng: Nhận và khởi tạo trạng thái cho một bước lộ trình mới từ server.
 */
void beginStep(const String& action, float dist, int seq, float nodeX, float nodeY, bool nodeValid,
               float nodeTh, bool nodeThValid) {
  if (seq == 0) resetOdometry();     // xe dang o HOME -> xoa troi tich luy
  curAction = action;
  curDist   = dist;
  curSeq    = seq;
  curNodeX = nodeX; curNodeY = nodeY; curNodeValid = nodeValid;
  curNodeTh = nodeTh; curNodeThValid = nodeThValid;
  stepPhase = 0; leftStart = false;
  lastError = 0; errIntegral = 0;
  running = true;
  hubLog("[step] " + String(seq) + ": " + action + (dist > 0 ? (" (" + String((int)dist) + "mm)") : ""));
}

// Thuc thi 1 nhip cua buoc HIEN TAI (goi trong loop khi running && curAction != "").
// Xong 1 buoc -> finishStep(...) (gui stepdone + idle cho buoc ke tu server).
// Moi buoc L/R/B = RE truoc (0/+90/-90/180) roi TIEN 1 canh toi giao diem/het line.
/**
 * Chức năng: Máy trạng thái thực thi bước rẽ, tìm line, chạy tới giao điểm và căn tâm xe.
 */
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
  // NHAN DIEN NGA TU LA CHINH: chay den khi CAM BIEN THAY vach ngang (cnt>=INTERSECT_N) hoac
  // HET line (den cuoi nhanh) thi dung -> vi tri chuan theo LINE that (line vat ly luon dung),
  // KE CA phai chay qua khoang cach du kien. Khoang cach chi dung 2 viec phu:
  //  - LOWER_MARGIN: khong bat nga tu SOM hon (bo qua giao diem giua doan F-gop / diem xuat phat)
  //  - SAFETY_MARGIN: chan chay lac vo han neu that su mat line (dung khi qua xa moi chiu thua)
  const float LOWER_MARGIN  = 130.0f;   // chi bat nga tu tu (detectTrav - margin) tro di
  const float SAFETY_MARGIN = 220.0f;   // cho phep chay qua detectTrav toi da bay nhieu de tim vach
  bool reached = false;
  if (curDist > 0) {
    float detectTrav = curDist - CENTER_OFFSET_MM;      // trav ky vong khi cam bien cham node
    float minGate = max(40.0f, detectTrav - LOWER_MARGIN);
    if (trav > minGate && leftStart && (lineCount >= INTERSECT_N || lineLost)) reached = true;  // THAY vach -> dung
    else if (trav >= detectTrav + SAFETY_MARGIN) reached = true;   // safety chong chay lac
  } else {
    if (trav > MIN_EDGE && leftStart && (lineCount >= INTERSECT_N || lineLost)) reached = true;
    if (trav > MAX_EDGE) reached = true;
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

// Su kien WebSocket toi hub.
/**
 * Chức năng: Xử lý kết nối WebSocket và các lệnh vận hành chính do hub gửi xuống.
 */
void onWsEvent(WStype_t type, uint8_t* payload, size_t len) {
  switch (type) {
    case WStype_CONNECTED:
      hubOK = true;
      wsClient.sendTXT("{\"role\":\"car\"}");
      hubLog("[fw] build " __DATE__ " " __TIME__);   // danh dau ban firmware dang chay (xac minh OTA)
      break;
    case WStype_DISCONNECTED:
      hubOK = false;
      break;
    case WStype_TEXT: {
      JsonDocument doc;
      if (deserializeJson(doc, payload, len)) return;   // loi parse -> bo
      const char* cmd = doc["cmd"];
      if (!cmd) return;                                 // hello/khac -> bo qua
      if      (!strcmp(cmd, "step"))   beginStep(String((const char*)(doc["action"] | "F")), doc["dist"] | 0.0f, doc["seq"] | 0,
                                                 doc["nodeX"] | 0.0f, doc["nodeY"] | 0.0f, !doc["nodeX"].isNull(),
                                                 ((float)(doc["nodeTh"] | 0.0f)) * PI / 180.0f, !doc["nodeTh"].isNull());
      else if (!strcmp(cmd, "stop"))   { running = false; curAction = ""; lineFollow = false; stopMotors(); routeTarget = ""; hubLog("[ws] STOP"); }
      else if (!strcmp(cmd, "manual")) doManual(String((const char*)(doc["dir"] | "S")));
      else if (!strcmp(cmd, "drop"))   doDrop();
      break;
    }
    default: break;
  }
}

// ===================== NVS: luu/nap cau hinh mang =====================
/**
 * Chức năng: Nạp SSID, mật khẩu WiFi và địa chỉ hub từ NVS.
 */
void loadNetConfig() {
  prefs.begin("net", true);
  cfgSsid = prefs.getString("ssid", WIFI_SSID);
  cfgPass = prefs.getString("pass", WIFI_PASS);
  cfgHub  = prefs.getString("hub",  HUB_HOST);
  prefs.end();
}
/**
 * Chức năng: Lưu cấu hình mạng hiện tại vào NVS.
 */
void saveNetConfig() {
  prefs.begin("net", false);
  prefs.putString("ssid", cfgSsid);
  prefs.putString("pass", cfgPass);
  prefs.putString("hub",  cfgHub);
  prefs.end();
}

/**
 * Chức năng: Khởi động dịch vụ cập nhật firmware qua WiFi và dừng động cơ khi bắt đầu nạp.
 */
void startOTA() {
  if (!wifiOK) return;
  ArduinoOTA.setHostname("esp32-car");
  // ArduinoOTA.setPassword("1234");   // mo neu muon dat mat khau OTA
  ArduinoOTA.onStart([]() { stopMotors(); });
  ArduinoOTA.begin();
}

bool hubStarted = false;          // da goi wsClient.begin() chua

/**
 * Chức năng: Khởi tạo WebSocket client và cơ chế tự kết nối lại với hub.
 */
void startHub() {
  if (!wifiOK) return;
  wsClient.begin(cfgHub.c_str(), HUB_PORT, "/");
  wsClient.onEvent(onWsEvent);
  wsClient.setReconnectInterval(3000);
  hubStarted = true;
}

// ===================== Tu tim hub (UDP broadcast) =====================
// DHCP hay doi IP laptop -> thay vi go tay 'Wh<ip>', xe hoi cA mang:
// broadcast "CAR_WHO?" -> server tra loi "HUB <ip> <port>" -> luu NVS + noi lai.
WiFiUDP disc;
bool discReady = false;

/**
 * Chức năng: Tự tìm địa chỉ hub trong mạng LAN bằng UDP broadcast.
 */
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
/**
 * Chức năng: Khởi động các dịch vụ cần thiết sau khi WiFi kết nối thành công.
 */
void onWifiUp() {
  wifiOK = true;
  beep(80);
  startHub();
  startOTA();
}

/**
 * Chức năng: Cấu hình ESP32 ở chế độ station và kết nối vào mạng WiFi đã lưu.
 */
void setupWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(cfgSsid.c_str(), cfgPass.c_str());
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) { delay(250); }
  if (WiFi.status() == WL_CONNECTED) onWifiUp();
}

// ===================== Setup =====================
/**
 * Chức năng: Khởi tạo chân I/O, PWM, encoder, servo, gyro, NVS và kết nối mạng.
 */
void setup() {

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
}

// ===================== Loop =====================
/**
 * Chức năng: Vòng lặp chính: duy trì mạng, cập nhật odometry, chạy lộ trình và gửi telemetry.
 */
void loop() {

  // WiFi co the len TRE hon timeout 10s luc boot (router cham), hoac tu noi lai sau khi rot.
  // Neu khong theo doi -> wifiOK ket false vinh vien: xe co IP, ping duoc, nhung
  // hub/OTA/web KHONG BAO GIO chay (da tung dinh loi nay).
  static unsigned long lastWifiChk = 0;
  if (!wifiOK && millis() - lastWifiChk > 2000) {
    lastWifiChk = millis();
    if (WiFi.status() == WL_CONNECTED) onWifiUp();
  }

  // Web server + WebSocket client toi hub + OTA
  if (wifiOK) {
    wsClient.loop();
    ArduinoOTA.handle();
  }

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

  // In dinh ky moi 500ms (chi o ban DEBUG)
}
