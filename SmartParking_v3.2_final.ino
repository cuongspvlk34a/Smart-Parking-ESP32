/**
 * ================================================================
 *  SMART PARKING SYSTEM  v3.2
 *  ESP32  |  2 slot  |  WiFi Dashboard  |  Telegram  |  Billing  |  Emergency
 * ================================================================
 *
 *  PHẦN CỨNG:
 *    GPIO 18  → Servo cổng VÀO   (signal)
 *    GPIO 19  → Servo cổng RA    (signal)
 *    GPIO 32  → IR cổng VÀO      (OUT)
 *    GPIO  4  → IR cổng RA       (OUT)
 *    GPIO 14  → IR Slot 1        (OUT)
 *    GPIO 27  → IR Slot 2        (OUT)
 *    GPIO 15  → Buzzer           (+)
 *    GPIO 21  → LCD SDA (I2C)
 *    GPIO 22  → LCD SCL (I2C)
 *    GPIO 13  → Nút Emergency    (một đầu GPIO 13, đầu kia GND)
 *
 *  THƯ VIỆN CẦN CÀI:
 *    - ESP32Servo
 *    - LiquidCrystal I2C  (Frank de Brabander)
 *    - ESPAsyncWebServer  (Me-No-Dev, cài từ GitHub ZIP)
 *    - AsyncTCP           (Me-No-Dev, cài từ GitHub ZIP)
 * ================================================================
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ESPAsyncWebServer.h>
#include "time.h"

// ================================================================
//  CONFIG  ← chỉnh tại đây trước khi nạp
// ================================================================
#define WIFI_SSID       "TOTOLINK_A3"
#define WIFI_PASS       "Anhcuong140296"
#define AP_SSID         "SmartParking"
#define AP_PASS         "parking123"

#define BOT_TOKEN       "8572713614:AAHU1vvbOAP20WztSNH7_dopKbcjBzCDImw"
#define CHAT_ID         "6633882197"

// Biểu phí (VND)
#define FREE_MINUTES    15
#define RATE_PER_BLOCK  2000
#define MIN_FEE         3000
#define BLOCK_MINUTES   30

// ================================================================
//  PINS & TIMING
// ================================================================
#define PIN_SERVO_IN    18
#define PIN_SERVO_OUT   19
#define PIN_IR_IN       32
#define PIN_IR_OUT       4
#define PIN_IR_S1       14
#define PIN_IR_S2       27
#define PIN_BUZZER      15
#define PIN_EMERGENCY   13   // Nút khẩn cấp: GPIO 13 → nút → GND

#define MAX_SLOTS         2
#define SERVO_CLOSED      0
#define SERVO_OPEN       90
#define SERVO_STEP        5
#define SERVO_STEP_MS    15
#define GATE_TRIGGER_MS 800
#define GATE_HOLD_MS   3000
#define DEBOUNCE_MS      50
#define MSG_DISPLAY_MS 2500

const int SLOT_PINS[MAX_SLOTS] = { PIN_IR_S1, PIN_IR_S2 };

// ================================================================
//  OBJECTS
// ================================================================
LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo             servoIn, servoOut;
AsyncWebServer    webServer(80);

// ================================================================
//  GATE STATE MACHINE
// ================================================================
enum GateState { GATE_CLOSED, GATE_TRIGGERED, GATE_OPENING, GATE_OPEN, GATE_CLOSING };

struct Gate {
  Servo*        servo;
  GateState     state;
  int           pos;
  unsigned long triggerTime;
  unsigned long openTime;
  unsigned long stepTime;
  bool          sensorRaw;
  bool          sensorStable;
  unsigned long debounceTime;
};

Gate gateIn  = { &servoIn,  GATE_CLOSED, 0, 0,0,0, false,false,0 };
Gate gateOut = { &servoOut, GATE_CLOSED, 0, 0,0,0, false,false,0 };

// ================================================================
//  BUZZER — non-blocking
// ================================================================
struct BuzzerCtrl { bool on; int total; int done; unsigned long dur; bool hi; unsigned long last; };
BuzzerCtrl buz = {};

void beepAsync(unsigned long d, int n) {
  buz = { true, n, 0, d, true, millis() };
  digitalWrite(PIN_BUZZER, HIGH);
}

void updateBuzzer() {
  if (!buz.on) return;
  if (millis() - buz.last < buz.dur) return;
  buz.last = millis();
  buz.hi   = !buz.hi;
  digitalWrite(PIN_BUZZER, buz.hi ? HIGH : LOW);
  if (!buz.hi && ++buz.done >= buz.total) {
    buz.on = false;
    digitalWrite(PIN_BUZZER, LOW);
  }
}

// ================================================================
//  BILLING
//  [FIX 1] Thêm rawRead + debounceTs để debounce slot IR
// ================================================================
struct SlotBilling {
  bool          wasOccupied;
  uint32_t      entrySec;
  bool          active;
  bool          rawRead;
  unsigned long debounceTs;
};

SlotBilling billing[MAX_SLOTS] = {};
bool        slotOccupied[MAX_SLOTS] = {};
int         carCount = 0;

uint32_t nowSec() {
  time_t t; time(&t);
  return (t > 1700000000UL) ? (uint32_t)t : millis() / 1000;
}

uint32_t calcFee(uint32_t durSec) {
  uint32_t m = max(1UL, durSec / 60);
  if (m <= FREE_MINUTES) return 0;
  uint32_t blocks = (m - FREE_MINUTES + BLOCK_MINUTES - 1) / BLOCK_MINUTES;
  return max((uint32_t)MIN_FEE, blocks * (uint32_t)RATE_PER_BLOCK);
}

String fmtDuration(uint32_t s) {
  uint32_t h = s/3600, m=(s%3600)/60, sec=s%60;
  if (h) return String(h)+"h"+String(m)+"m";
  if (m) return String(m)+"m"+String(sec)+"s";
  return String(sec)+"s";
}

String fmtFee(uint32_t fee) {
  if (fee == 0) return "MIEN PHI";
  if (fee >= 10000) return String(fee/1000)+","+String((fee%1000)/100)+"00d";
  return String(fee/1000)+",000d";
}

// ================================================================
//  TIME
// ================================================================
String getTimeStr() {
  struct tm t;
  if (!getLocalTime(&t)) return "--:--";
  char buf[12]; strftime(buf, sizeof(buf), "%H:%M:%S", &t);
  return String(buf);
}

String getDateTimeStr() {
  struct tm t;
  if (!getLocalTime(&t)) return "N/A";
  char buf[24]; strftime(buf, sizeof(buf), "%d/%m %H:%M:%S", &t);
  return String(buf);
}

// ================================================================
//  EVENT LOG  (8 sự kiện gần nhất)
// ================================================================
struct LogEvent { String msg; String time; String type; };
LogEvent  evLog[8];
int       evHead = 0;

void addEvent(const String& msg, const String& type = "sys") {
  evLog[evHead % 8] = { msg, getTimeStr(), type };
  evHead++;
  Serial.printf("[EVENT %s] %s %s\n", type.c_str(), getTimeStr().c_str(), msg.c_str());
}

// ================================================================
//  [FIX 2] escapeJson — tránh JSON broken
// ================================================================
String escapeJson(const String& s) {
  String out = "";
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s[i];
    if      (c == '"')  out += "\\\"";
    else if (c == '\\') out += "\\\\";
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else                out += c;
  }
  return out;
}

// ================================================================
//  [FIX 3] urlEncode — đúng chuẩn RFC 3986
// ================================================================
String urlEncode(const String& s) {
  String enc = "";
  const char* safe =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789-_.~";
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s[i];
    if (strchr(safe, c)) {
      enc += c;
    } else {
      char buf[4];
      snprintf(buf, sizeof(buf), "%%%02X", (unsigned char)c);
      enc += buf;
    }
  }
  return enc;
}

// ================================================================
//  TELEGRAM — FreeRTOS task trên Core 0
// ================================================================
SemaphoreHandle_t tgMutex;
String            tgMsg   = "";
bool              tgReady = false;

void telegramTask(void*) {
  for (;;) {
    if (tgReady && WiFi.status() == WL_CONNECTED) {
      xSemaphoreTake(tgMutex, portMAX_DELAY);
      String msg = tgMsg;
      tgReady    = false;
      xSemaphoreGive(tgMutex);

      WiFiClientSecure client;
      client.setInsecure();
      client.setTimeout(8000);

      if (client.connect("api.telegram.org", 443)) {
        String req = "GET /bot" + String(BOT_TOKEN) +
                     "/sendMessage?chat_id=" + String(CHAT_ID) +
                     "&text=" + urlEncode(msg) + " HTTP/1.1\r\n" +
                     "Host: api.telegram.org\r\n" +
                     "Connection: close\r\n\r\n";
        client.print(req);
        unsigned long t0 = millis();
        while (client.connected() && millis()-t0 < 5000) {
          while (client.available()) client.read();
        }
        client.stop();
        Serial.println("[TG] Sent OK");
      } else {
        Serial.println("[TG] Connect failed");
      }
    }
    vTaskDelay(pdMS_TO_TICKS(300));
  }
}

void sendTelegram(const String& msg) {
  if (WiFi.status() != WL_CONNECTED) return;
  xSemaphoreTake(tgMutex, portMAX_DELAY);
  tgMsg   = msg;
  tgReady = true;
  xSemaphoreGive(tgMutex);
}

// ================================================================
//  LCD
// ================================================================
bool          lcdDirty  = true;
unsigned long msgExpiry = 0;

void showMsg(const char* r0, const char* r1 = "                ") {
  lcd.clear();
  lcd.setCursor(0,0); lcd.print(r0);
  lcd.setCursor(0,1); lcd.print(r1);
  msgExpiry = millis() + MSG_DISPLAY_MS;
  lcdDirty  = true;
}

void refreshLCD() {
  if (millis() < msgExpiry) return;
  if (!lcdDirty) return;
  lcdDirty = false;
  lcd.setCursor(0,0);
  lcd.print("S1:"); lcd.print(slotOccupied[0] ? "FULL" : "FREE");
  lcd.print(" S2:"); lcd.print(slotOccupied[1] ? "FULL" : "FREE");
  lcd.setCursor(0,1);
  lcd.print("Cars:"); lcd.print(carCount); lcd.print("/"); lcd.print(MAX_SLOTS);
  lcd.print("  "); lcd.print(carCount >= MAX_SLOTS ? "[FULL]" : "[OPEN]");
}

// ================================================================
//  DEBOUNCE
// ================================================================
bool debounceRead(int pin, bool& raw, unsigned long& dt, bool& stable) {
  bool r = (digitalRead(pin) == LOW);
  if (r != raw) { raw = r; dt = millis(); }
  if (millis()-dt > DEBOUNCE_MS && r != stable) { stable = r; return true; }
  return false;
}

// ================================================================
//  GATE STATE MACHINE
// ================================================================
void updateGate(Gate& g) {
  unsigned long now = millis();
  switch (g.state) {
    case GATE_CLOSED: break;
    case GATE_TRIGGERED:
      if (now - g.triggerTime >= GATE_TRIGGER_MS)
        { g.state = GATE_OPENING; g.stepTime = now; }
      break;
    case GATE_OPENING:
      if (now - g.stepTime >= SERVO_STEP_MS) {
        g.stepTime = now; g.pos += SERVO_STEP;
        if (g.pos >= SERVO_OPEN) {
          g.pos = SERVO_OPEN; g.servo->write(SERVO_OPEN);
          g.state = GATE_OPEN; g.openTime = now;
        } else g.servo->write(g.pos);
      }
      break;
    case GATE_OPEN:
      if (g.sensorStable) g.openTime = now;
      else if (now - g.openTime >= GATE_HOLD_MS)
        { g.state = GATE_CLOSING; g.stepTime = now; }
      break;
    case GATE_CLOSING:
      if (now - g.stepTime >= SERVO_STEP_MS) {
        g.stepTime = now; g.pos -= SERVO_STEP;
        if (g.pos <= SERVO_CLOSED) {
          g.pos = SERVO_CLOSED; g.servo->write(SERVO_CLOSED);
          g.state = GATE_CLOSED;
        } else g.servo->write(g.pos);
      }
      break;
  }
}

void triggerGate(Gate& g) {
  if (g.state == GATE_CLOSED)
    { g.state = GATE_TRIGGERED; g.triggerTime = millis(); }
}

// ================================================================
//  SLOT MONITORING — [FIX 1] debounce slot IR
// ================================================================
void updateSlots() {
  bool prev[MAX_SLOTS];
  for (int i = 0; i < MAX_SLOTS; i++) prev[i] = slotOccupied[i];

  bool changed = false;
  for (int i = 0; i < MAX_SLOTS; i++) {
    bool r = (digitalRead(SLOT_PINS[i]) == LOW);
    if (r != billing[i].rawRead) {
      billing[i].rawRead    = r;
      billing[i].debounceTs = millis();
    }
    if (millis() - billing[i].debounceTs < DEBOUNCE_MS) continue;

    slotOccupied[i] = r;
    if (slotOccupied[i] != prev[i]) changed = true;
  }
  if (!changed) return;

  carCount = 0;
  for (int i = 0; i < MAX_SLOTS; i++) if (slotOccupied[i]) carCount++;
  lcdDirty = true;

  for (int i = 0; i < MAX_SLOTS; i++) {
    if (slotOccupied[i] && !prev[i]) {
      billing[i].entrySec = nowSec();
      billing[i].active   = true;
      addEvent("Xe vao S" + String(i+1), "in");
      sendTelegram(
        "[SMART PARKING]\n"
        "Xe vao: Slot " + String(i+1) + "\n"
        "Gio: " + getTimeStr() + "\n"
        "Bai: " + String(carCount) + "/" + String(MAX_SLOTS) + " cho"
      );
    } else if (!slotOccupied[i] && prev[i]) {
      billing[i].active = false;
      uint32_t dur  = nowSec() - billing[i].entrySec;
      uint32_t fee  = calcFee(dur);
      String durStr = fmtDuration(dur);
      String feeStr = fmtFee(fee);
      char r0[17], r1[17];
      snprintf(r0, sizeof(r0), "S%d Phi: %s", i+1, feeStr.c_str());
      snprintf(r1, sizeof(r1), "T/g: %-12s", durStr.c_str());
      showMsg(r0, r1);
      beepAsync(150, 2);
      addEvent("Xe ra S" + String(i+1) + " | " + durStr + " | " + feeStr, "out");
      sendTelegram(
        "[SMART PARKING]\n"
        "Xe ra: Slot " + String(i+1) + "\n"
        "Thoi gian: " + durStr + "\n"
        "Phi: " + feeStr + "\n"
        "Bai: " + String(carCount) + "/" + String(MAX_SLOTS) + " cho\n"
        "Gio: " + getTimeStr()
      );
    }
  }
}

// ================================================================
//  EMERGENCY MODE
//  Khai báo SAU lcdDirty, msgExpiry, addEvent, sendTelegram
//  vì C++ cần thấy biến/hàm trước khi dùng
// ================================================================
bool          emergencyActive = false;
bool          btnRaw          = false;
bool          btnStable       = false;
unsigned long btnDebounceTs   = 0;
bool          btnPrev         = false;
unsigned long lcdEmergencyTs  = 0;

void updateEmergency() {
  // Debounce nút
  bool r = (digitalRead(PIN_EMERGENCY) == LOW);
  if (r != btnRaw) { btnRaw = r; btnDebounceTs = millis(); }
  if (millis() - btnDebounceTs > 50 && btnStable != btnRaw) {
    btnStable = btnRaw;
  }

  // Phát hiện cạnh lên (nhả nút) = 1 lần nhấn hoàn chỉnh
  bool pressed = (btnPrev && !btnStable);
  btnPrev = btnStable;

  if (pressed) {
    emergencyActive = !emergencyActive;

    if (emergencyActive) {
      // BẬT EMERGENCY
      servoIn.write(SERVO_OPEN);
      servoOut.write(SERVO_OPEN);
      gateIn.pos  = SERVO_OPEN; gateIn.state  = GATE_OPEN;
      gateOut.pos = SERVO_OPEN; gateOut.state = GATE_OPEN;
      gateIn.openTime  = millis() + 86400000UL;
      gateOut.openTime = millis() + 86400000UL;

      lcdDirty      = false;
      lcdEmergencyTs = 0;

      addEvent("!!! KHAN CAP BAT !!!", "full");
      sendTelegram(
        "[SMART PARKING]\n"
        "!!! CHE DO KHAN CAP !!!\n"
        "CA 2 CONG DA MO THONG\n"
        "Xe ra vao tu do\n"
        "Gio: " + getTimeStr()
      );
      Serial.println("[EMERGENCY] BAT");

    } else {
      // TẮT EMERGENCY
      gateIn.state  = GATE_CLOSING; gateIn.stepTime  = millis();
      gateOut.state = GATE_CLOSING; gateOut.stepTime = millis();

      lcdDirty  = true;
      msgExpiry = 0;

      addEvent("Emergency tat - ve binh thuong", "sys");
      sendTelegram(
        "[SMART PARKING]\n"
        "Che do khan cap da TAT\n"
        "He thong tro ve binh thuong\n"
        "Gio: " + getTimeStr()
      );
      Serial.println("[EMERGENCY] TAT");
    }
  }

  // Khi đang emergency: nhấp nháy LCD mỗi 600ms
  if (emergencyActive && millis() - lcdEmergencyTs >= 600) {
    lcdEmergencyTs = millis();
    static bool flip = false;
    flip = !flip;
    lcd.clear();
    if (flip) {
      lcd.setCursor(0,0); lcd.print("!!! KHAN  CAP !!!");
      lcd.setCursor(0,1); lcd.print("  2 CONG MO THONG");
    } else {
      lcd.setCursor(0,0); lcd.print(" THOAT HIEM NGAY ");
      lcd.setCursor(0,1); lcd.print("  XE RA TU DO!!!");
    }
  }

  // Ép gate không tự đóng khi đang emergency
  if (emergencyActive) {
    gateIn.openTime  = millis() + 86400000UL;
    gateOut.openTime = millis() + 86400000UL;
  }
}

// ================================================================
//  WEB DASHBOARD HTML
// ================================================================
const char DASHBOARD_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="vi">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1">
<title>Smart Parking</title>
<style>
@import url('https://fonts.googleapis.com/css2?family=IBM+Plex+Mono:wght@400;600&family=IBM+Plex+Sans:wght@400;500;600&display=swap');
:root{--bg:#0f0f0f;--surface:#1a1a1a;--border:#2a2a2a;--text:#e8e8e8;--muted:#666;--green:#22c55e;--amber:#f59e0b;--red:#ef4444;--blue:#3b82f6}
*{box-sizing:border-box;margin:0;padding:0}
body{background:var(--bg);color:var(--text);font-family:'IBM Plex Sans',sans-serif;min-height:100vh;padding:20px 16px 40px}
.header{display:flex;align-items:flex-end;justify-content:space-between;margin-bottom:24px;padding-bottom:16px;border-bottom:1px solid var(--border)}
.header-left h1{font-family:'IBM Plex Mono',monospace;font-size:20px;font-weight:600;letter-spacing:-0.5px}
.header-left p{font-size:12px;color:var(--muted);margin-top:3px;font-family:'IBM Plex Mono',monospace}
.live-dot{display:flex;align-items:center;gap:6px;font-size:11px;color:var(--muted);font-family:'IBM Plex Mono',monospace}
.dot{width:7px;height:7px;border-radius:50%;background:var(--green);animation:blink 2s infinite}
@keyframes blink{0%,100%{opacity:1}50%{opacity:.2}}
.stats{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-bottom:16px}
.stat{background:var(--surface);border:1px solid var(--border);border-radius:10px;padding:14px 16px}
.stat-label{font-size:10px;color:var(--muted);text-transform:uppercase;letter-spacing:.8px;font-family:'IBM Plex Mono',monospace;margin-bottom:6px}
.stat-value{font-family:'IBM Plex Mono',monospace;font-size:26px;font-weight:600}
.stat-value.open{color:var(--green)}.stat-value.full{color:var(--red)}.stat-value.emg{color:#ff6b00}
.slots{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-bottom:16px}
.slot{background:var(--surface);border:1px solid var(--border);border-radius:10px;padding:16px;transition:border-color .3s}
.slot.occupied{border-color:var(--amber)}.slot.free{border-color:var(--green)}.slot.emergency{border-color:#ff6b00}
.slot-id{font-size:10px;font-family:'IBM Plex Mono',monospace;color:var(--muted);margin-bottom:8px;letter-spacing:.8px}
.slot-state{font-family:'IBM Plex Mono',monospace;font-size:16px;font-weight:600;margin-bottom:6px}
.slot.occupied .slot-state{color:var(--amber)}.slot.free .slot-state{color:var(--green)}.slot.emergency .slot-state{color:#ff6b00}
.slot-fee{font-size:12px;color:var(--muted);font-family:'IBM Plex Mono',monospace}
.slot-fee span{color:var(--blue)}
.events-header{font-size:11px;color:var(--muted);text-transform:uppercase;letter-spacing:.8px;font-family:'IBM Plex Mono',monospace;margin-bottom:10px}
.events{background:var(--surface);border:1px solid var(--border);border-radius:10px;overflow:hidden}
.ev-row{display:flex;align-items:flex-start;gap:10px;padding:11px 14px;border-bottom:1px solid var(--border);font-size:13px}
.ev-row:last-child{border-bottom:none}
.ev-pip{width:6px;height:6px;border-radius:50%;margin-top:4px;flex-shrink:0}
.pip-in{background:var(--green)}.pip-out{background:var(--amber)}.pip-full{background:var(--red)}.pip-sys{background:var(--blue)}
.ev-time{font-family:'IBM Plex Mono',monospace;font-size:11px;color:var(--muted);min-width:56px;margin-top:1px}
.ev-msg{color:var(--text);line-height:1.4}
.empty-state{padding:24px;text-align:center;color:var(--muted);font-size:13px}
.emg-banner{background:#ff6b00;color:#000;font-family:'IBM Plex Mono',monospace;font-weight:600;font-size:14px;text-align:center;padding:10px;border-radius:8px;margin-bottom:16px;display:none}
</style>
</head>
<body>
<div class="emg-banner" id="emgBanner">🚨 CHẾ ĐỘ KHẨN CẤP — CẢ 2 CỔNG MỞ THÔNG 🚨</div>
<div class="header">
  <div class="header-left">
    <h1>SMART PARKING</h1>
    <p id="clock">--:--:--</p>
  </div>
  <div class="live-dot"><div class="dot"></div><span>LIVE</span></div>
</div>
<div class="stats">
  <div class="stat"><div class="stat-label">Trong bai</div><div class="stat-value" id="carCount">-/-</div></div>
  <div class="stat"><div class="stat-label">Trang thai</div><div class="stat-value" id="lotStatus">-</div></div>
</div>
<div class="slots" id="slotGrid"></div>
<div class="events-header">Su kien gan day</div>
<div class="events" id="eventList"><div class="empty-state">Chua co su kien</div></div>
<script>
function fmt(v){if(!v||v===0)return'MIEN PHI';if(v>=10000)return(v/1000|0)+','+(v%1000<100?'0':'')+((v%1000/100)|0)+'00d';return(v/1000|0)+',000d';}
function tick(){document.getElementById('clock').textContent=new Date().toLocaleTimeString('vi-VN');}
setInterval(tick,1000);tick();
async function refresh(){
  try{
    const d=await(await fetch('/api/status')).json();
    document.getElementById('carCount').textContent=d.cars+'/'+d.maxSlots;
    const sb=document.getElementById('lotStatus');
    const emgBanner=document.getElementById('emgBanner');
    if(d.emergency){
      sb.textContent='KHAN CAP';sb.className='stat-value emg';
      emgBanner.style.display='block';
    } else if(d.cars>=d.maxSlots){
      sb.textContent='DAY';sb.className='stat-value full';
      emgBanner.style.display='none';
    } else {
      sb.textContent='CO CHO';sb.className='stat-value open';
      emgBanner.style.display='none';
    }
    const sg=document.getElementById('slotGrid');sg.innerHTML='';
    d.slots.forEach((s,i)=>{
      const el=document.createElement('div');
      const cls=d.emergency?'emergency':(s.occupied?'occupied':'free');
      el.className='slot '+cls;
      el.innerHTML=`<div class="slot-id">SLOT 0${i+1}</div>`
        +`<div class="slot-state">${d.emergency?'MO THONG':(s.occupied?'CO XE':'TRONG')}</div>`
        +`<div class="slot-fee">${s.occupied&&!d.emergency?`Phi: <span>${fmt(s.runningFee)}</span>`:'San sang'}</div>`;
      sg.appendChild(el);
    });
    const el=document.getElementById('eventList');
    if(!d.events||!d.events.length){el.innerHTML='<div class="empty-state">Chua co su kien</div>';return;}
    el.innerHTML='';
    [...d.events].reverse().forEach(ev=>{
      const row=document.createElement('div');row.className='ev-row';
      const pip={'in':'pip-in','out':'pip-out','full':'pip-full','sys':'pip-sys'}[ev.type]||'pip-sys';
      row.innerHTML=`<div class="ev-pip ${pip}"></div><div class="ev-time">${ev.time}</div><div class="ev-msg">${ev.msg}</div>`;
      el.appendChild(row);
    });
  }catch(e){console.error(e);}
}
refresh();setInterval(refresh,2500);
</script>
</body></html>
)rawliteral";

// ================================================================
//  JSON API  /api/status
// ================================================================
void setupWebServer() {
  webServer.on("/", HTTP_GET, [](AsyncWebServerRequest* r) {
    r->send_P(200, "text/html", DASHBOARD_HTML);
  });

  webServer.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* r) {
    String j = "{";
    j += "\"cars\":"     + String(carCount)   + ",";
    j += "\"maxSlots\":" + String(MAX_SLOTS)  + ",";
    j += "\"emergency\":" + String(emergencyActive ? "true" : "false") + ",";
    j += "\"slots\":[";
    for (int i = 0; i < MAX_SLOTS; i++) {
      uint32_t runFee = (billing[i].active)
                        ? calcFee(nowSec() - billing[i].entrySec) : 0;
      j += "{\"occupied\":" + String(slotOccupied[i] ? "true" : "false");
      j += ",\"runningFee\":" + String(runFee) + "}";
      if (i < MAX_SLOTS-1) j += ",";
    }
    j += "],\"events\":[";
    int cnt = min(evHead, 8);
    for (int i = 0; i < cnt; i++) {
      int idx = (evHead - cnt + i) % 8;
      j += "{\"msg\":\""  + escapeJson(evLog[idx].msg)  + "\",";
      j += "\"time\":\"" + escapeJson(evLog[idx].time) + "\",";
      j += "\"type\":\"" + escapeJson(evLog[idx].type) + "\"}";
      if (i < cnt-1) j += ",";
    }
    j += "]}";
    r->send(200, "application/json", j);
  });

  webServer.onNotFound([](AsyncWebServerRequest* r) {
    r->send(404, "text/plain", "Not found");
  });

  webServer.begin();
  Serial.println("[WEB] Server started on port 80");
}

// ================================================================
//  SETUP
// ================================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n[INIT] Smart Parking v3.2");

  lcd.init(); lcd.backlight();
  lcd.setCursor(1,0); lcd.print("SMART PARKING");
  lcd.setCursor(3,1); lcd.print("v3.2 Boot...");

  pinMode(PIN_BUZZER,    OUTPUT); digitalWrite(PIN_BUZZER, LOW);
  pinMode(PIN_IR_IN,     INPUT_PULLUP);
  pinMode(PIN_IR_OUT,    INPUT_PULLUP);
  pinMode(PIN_EMERGENCY, INPUT_PULLUP);
  for (int i = 0; i < MAX_SLOTS; i++) pinMode(SLOT_PINS[i], INPUT_PULLUP);

  ESP32PWM::allocateTimer(0); ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2); ESP32PWM::allocateTimer(3);
  servoIn.setPeriodHertz(50);
  servoIn.attach(PIN_SERVO_IN, 500, 2400);
  servoIn.write(SERVO_CLOSED);
  servoOut.setPeriodHertz(50);
  servoOut.attach(PIN_SERVO_OUT, 500, 2400);
  servoOut.write(SERVO_CLOSED);

  lcd.clear();
  lcd.setCursor(0,0); lcd.print("WiFi: "); lcd.print(AP_SSID);
  lcd.setCursor(0,1); lcd.print("Connecting STA..");

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.printf("[WIFI] AP: %s @ %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 24) {
    delay(500); tries++; Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WIFI] STA: %s\n", WiFi.localIP().toString().c_str());
    configTime(8 * 3600, 0, "pool.ntp.org", "time.nist.gov");
    struct tm t; int ntpTry = 0;
    while (!getLocalTime(&t) && ntpTry < 10) { delay(300); ntpTry++; }
    if (ntpTry < 10) Serial.printf("[NTP] %s\n", getDateTimeStr().c_str());
    else             Serial.println("[NTP] Sync timeout - using millis()");
    lcd.clear();
    lcd.setCursor(0,0); lcd.print("WiFi OK");
    lcd.setCursor(0,1); lcd.print(WiFi.localIP());
  } else {
    Serial.println("[WIFI] STA failed - AP only");
    lcd.clear();
    lcd.setCursor(0,0); lcd.print("AP only mode");
    lcd.setCursor(0,1); lcd.print(WiFi.softAPIP());
  }
  delay(1200);

  tgMutex = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(telegramTask, "TG", 8192, NULL, 1, NULL, 0);

  setupWebServer();

  // Khởi tạo billing + debounce slot
  for (int i = 0; i < MAX_SLOTS; i++) {
    billing[i]            = {};
    billing[i].rawRead    = (digitalRead(SLOT_PINS[i]) == LOW);
    billing[i].debounceTs = millis();
  }

  updateSlots();
  lcdDirty = true;
  refreshLCD();

  addEvent("He thong khoi dong", "sys");
  sendTelegram(
    "[SMART PARKING]\n"
    "He thong da khoi dong\n"
    "Dashboard: 192.168.4.1\n"
    "Gio: " + getDateTimeStr()
  );

  Serial.println("[INIT] Ready.");
  beepAsync(120, 2);
}

// ================================================================
//  MAIN LOOP — Core 1, không có blocking delay()
// ================================================================
void loop() {
  updateEmergency();   // ưu tiên cao nhất

  updateSlots();
  updateBuzzer();
  updateGate(gateIn);
  updateGate(gateOut);
  refreshLCD();

  // Khi đang emergency: bỏ qua toàn bộ logic IR cổng
  if (emergencyActive) return;

  // ---- Cổng VÀO ----
  if (debounceRead(PIN_IR_IN,
                   gateIn.sensorRaw,
                   gateIn.debounceTime,
                   gateIn.sensorStable)) {
    if (gateIn.sensorStable) {
      if (carCount >= MAX_SLOTS) {
        showMsg(" ** BAI DAY **", " Khong the vao!");
        beepAsync(300, 3);
        addEvent("Tu choi vao: bai day", "full");
        sendTelegram(
          "[SMART PARKING]\n"
          "BAI DAY - Tu choi xe vao\n"
          "Gio: " + getTimeStr()
        );
      } else {
        triggerGate(gateIn);
        showMsg(" Xe dang vao...", " Moi dau vao cho");
        beepAsync(120, 1);
      }
    } else {
      lcdDirty = true;
    }
  }

  // ---- Cổng RA ----
  if (debounceRead(PIN_IR_OUT,
                   gateOut.sensorRaw,
                   gateOut.debounceTime,
                   gateOut.sensorStable)) {
    if (gateOut.sensorStable) {
      triggerGate(gateOut);
      showMsg("  Xe dang ra...","    Cam on!");
      beepAsync(120, 1);
    } else {
      lcdDirty = true;
    }
  }
}

/*
 * ================================================================
 *  HƯỚNG DẪN NÚT EMERGENCY (GPIO 13)
 * ================================================================
 *  Kết nối: GPIO 13 --- [NÚT NHẤN] --- GND
 *  (Không cần điện trở - ESP32 có pull-up nội bộ)
 *  Nhấn lần 1: BẬT khẩn cấp - 2 cổng mở, LCD nhấp nháy, Telegram alert
 *  Nhấn lần 2: TẮT khẩn cấp - 2 cổng đóng lại, về bình thường
 *
 *  HƯỚNG DẪN TELEGRAM BOT
 * ================================================================
 *  1. @BotFather -> /newbot -> lấy BOT_TOKEN
 *  2. @userinfobot -> /start -> lấy CHAT_ID
 *  3. Nhắn 1 tin cho bot trước khi deploy
 *
 *  HƯỚNG DẪN DASHBOARD
 * ================================================================
 *  WiFi: SmartParking | Pass: parking123
 *  Browser: http://192.168.4.1
 * ================================================================
 */
