// Receptor de robô de combate: ESP32-C3 SuperMini + 2 ESCs duais KingModel (sinal PWM RC 1000-2000 us).
// O ESP cria um Wi-Fi próprio e serve uma página de controle (joystick + arma) para o celular.
#include <WiFi.h>
#include <WebServer.h>

// ---------- Configuração ----------
const char *AP_SSID = "RoboCombate";
const char *AP_PASS = "12345678";  // mínimo 8 caracteres

// ESC 1 (locomoção): canal 1 / canal 2. ESC 2 (arma): canal 1.
const uint8_t PIN_DRIVE_CH1 = 0;
const uint8_t PIN_DRIVE_CH2 = 1;
const uint8_t PIN_WEAPON = 3;

// true: o ESP faz a mistura (ESC em modo independente: CH1 = motor esquerdo, CH2 = motor direito).
// false: ESC em modo misto: CH1 = aceleração, CH2 = direção.
const bool MIX_IN_ESP = true;
const bool INVERT_LEFT = false;
const bool INVERT_RIGHT = false;

const uint16_t US_MIN = 1000;
const uint16_t US_NEUTRAL = 1500;
const uint16_t US_MAX = 2000;

const uint32_t FAILSAFE_MS = 300;       // sem comando por este tempo => tudo neutro
const uint16_t WEAPON_RAMP_US_S = 1000; // aceleração máxima da arma (us por segundo)
const uint8_t DEADZONE_PCT = 5;

// ---------- PWM (50 Hz, 14 bits) ----------
const uint32_t PWM_FREQ = 50;
const uint8_t PWM_BITS = 14;
const uint8_t PINS[] = {PIN_DRIVE_CH1, PIN_DRIVE_CH2, PIN_WEAPON};
const uint8_t NUM_PINS = sizeof(PINS);

void pwmBegin(uint8_t idx) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(PINS[idx], PWM_FREQ, PWM_BITS);
#else
  ledcSetup(idx, PWM_FREQ, PWM_BITS);
  ledcAttachPin(PINS[idx], idx);
#endif
}

void pwmWriteUs(uint8_t idx, int us) {
  us = constrain(us, US_MIN, US_MAX);
  uint32_t duty = (uint32_t)us * ((1UL << PWM_BITS) - 1) / (1000000UL / PWM_FREQ);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(PINS[idx], duty);
#else
  ledcWrite(idx, duty);
#endif
}

// ---------- Estado ----------
WebServer server(80);

volatile int cmdX = 0;       // -100..100 (direita +)
volatile int cmdY = 0;       // -100..100 (frente +)
volatile int cmdWeapon = 0;  // 0..100
volatile bool cmdArmed = false;
volatile uint32_t lastCmdMs = 0;
bool hasCmd = false;

float weaponUs = US_NEUTRAL;
uint32_t lastUpdateMs = 0;

// ---------- Página ----------
const char PAGE[] PROGMEM = R"HTML(
<!DOCTYPE html><html lang="pt-br"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title>Robô</title>
<style>
*{box-sizing:border-box;-webkit-user-select:none;user-select:none;-webkit-tap-highlight-color:transparent}
body{margin:0;background:#111;color:#eee;font-family:sans-serif;display:flex;flex-direction:column;align-items:center;gap:16px;padding:12px;touch-action:none}
#st{font-size:14px;padding:4px 10px;border-radius:10px;background:#a00}
#st.ok{background:#080}
#joy{width:240px;height:240px;border-radius:50%;background:#222;border:2px solid #555;position:relative;touch-action:none}
#knob{width:80px;height:80px;border-radius:50%;background:#4af;position:absolute;left:80px;top:80px}
#arm{width:240px;padding:16px;font-size:20px;border:0;border-radius:10px;background:#555;color:#fff}
#arm.on{background:#d22}
#w{width:90%;max-width:400px;height:40px}
</style></head><body>
<div id="st">desconectado</div>
<div id="joy"><div id="knob"></div></div>
<button id="arm">ARMA DESARMADA</button>
<div>Arma: <span id="wv">0</span>%</div>
<input id="w" type="range" min="0" max="100" value="0">
<script>
const joy=document.getElementById('joy'),knob=document.getElementById('knob'),
arm=document.getElementById('arm'),w=document.getElementById('w'),wv=document.getElementById('wv'),st=document.getElementById('st');
let x=0,y=0,armed=false,busy=false,R=120,KR=40;
function setKnob(px,py){knob.style.left=(R-KR+px)+'px';knob.style.top=(R-KR+py)+'px';}
function move(e){
  const r=joy.getBoundingClientRect();
  let dx=e.clientX-(r.left+R),dy=e.clientY-(r.top+R);
  const max=R-KR,d=Math.hypot(dx,dy);
  if(d>max){dx*=max/d;dy*=max/d;}
  setKnob(dx,dy);x=dx/max;y=-dy/max;
}
joy.addEventListener('pointerdown',e=>{joy.setPointerCapture(e.pointerId);move(e);});
joy.addEventListener('pointermove',e=>{if(e.buttons||e.pressure>0)move(e);});
function release(){x=0;y=0;setKnob(0,0);}
joy.addEventListener('pointerup',release);
joy.addEventListener('pointercancel',release);
arm.onclick=()=>{armed=!armed;arm.classList.toggle('on',armed);arm.textContent=armed?'ARMA ARMADA':'ARMA DESARMADA';if(!armed){w.value=0;wv.textContent=0;}};
w.oninput=()=>{wv.textContent=w.value;};
setInterval(async()=>{
  if(busy)return;busy=true;
  const q='x='+Math.round(x*100)+'&y='+Math.round(y*100)+'&w='+(armed?w.value:0)+'&a='+(armed?1:0);
  try{await fetch('/c?'+q,{cache:'no-store'});st.textContent='conectado';st.className='ok';}
  catch(e){st.textContent='desconectado';st.className='';}
  busy=false;
},50);
</script></body></html>
)HTML";

// ---------- Handlers ----------
void handleRoot() { server.send_P(200, "text/html", PAGE); }

void handleControl() {
  cmdX = constrain(server.arg("x").toInt(), -100, 100);
  cmdY = constrain(server.arg("y").toInt(), -100, 100);
  cmdWeapon = constrain(server.arg("w").toInt(), 0, 100);
  cmdArmed = server.arg("a").toInt() == 1;
  lastCmdMs = millis();
  hasCmd = true;
  server.send(204);
}

// ---------- Saídas ----------
int pctToUs(float pct) { return US_NEUTRAL + (int)(pct * (US_MAX - US_NEUTRAL) / 100.0f); }

float deadzone(int v) { return abs(v) < DEADZONE_PCT ? 0 : v; }

void allNeutral() {
  pwmWriteUs(0, US_NEUTRAL);
  pwmWriteUs(1, US_NEUTRAL);
  pwmWriteUs(2, US_NEUTRAL);
  weaponUs = US_NEUTRAL;
}

void updateOutputs() {
  uint32_t now = millis();
  float dt = (now - lastUpdateMs) / 1000.0f;
  lastUpdateMs = now;

  if (!hasCmd || now - lastCmdMs > FAILSAFE_MS) {
    allNeutral();
    return;
  }

  float thr = deadzone(cmdY);
  float str = deadzone(cmdX);
  float ch1, ch2;
  if (MIX_IN_ESP) {
    float left = thr + str, right = thr - str;
    float m = max(fabsf(left), fabsf(right));
    if (m > 100) {
      left = left * 100 / m;
      right = right * 100 / m;
    }
    ch1 = INVERT_LEFT ? -left : left;
    ch2 = INVERT_RIGHT ? -right : right;
  } else {
    ch1 = thr;
    ch2 = str;
  }
  pwmWriteUs(0, pctToUs(ch1));
  pwmWriteUs(1, pctToUs(ch2));

  // Arma: só para frente (1500..2000), rampa na subida e parada imediata ao desarmar.
  float target = cmdArmed ? pctToUs(cmdWeapon) : US_NEUTRAL;
  if (target <= weaponUs) {
    weaponUs = target;
  } else {
    weaponUs = min(target, weaponUs + WEAPON_RAMP_US_S * dt);
  }
  pwmWriteUs(2, (int)weaponUs);
}

void setup() {
  for (uint8_t i = 0; i < NUM_PINS; i++) pwmBegin(i);
  allNeutral();  // ESCs precisam ver neutro ao ligar para armar

  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  WiFi.softAP(AP_SSID, AP_PASS, 1, 0, 2);
  // Alguns SuperMini só funcionam bem com potência reduzida:
  // WiFi.setTxPower(WIFI_POWER_8_5dBm);

  server.on("/", handleRoot);
  server.on("/c", handleControl);
  server.begin();
  lastUpdateMs = millis();
}

void loop() {
  server.handleClient();
  static uint32_t lastOut = 0;
  if (millis() - lastOut >= 20) {
    lastOut = millis();
    updateOutputs();
  }
}
