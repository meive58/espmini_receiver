// Receptor de robô de combate: ESP32-C3 SuperMini + 2 ESCs duais KingModel (sinal PWM RC 1000-2000 us).
// O ESP cria um Wi-Fi próprio e serve uma página de controle (joystick + arma) para o celular.
#include <WiFi.h>
#include <WebServer.h>

// ---------- Configuração ----------
const char *AP_SSID = "RoboCombate";
const char *AP_PASS = "12345678";  // mínimo 8 caracteres

// ESC 1 (locomoção): canal 1 / canal 2. ESC 2 (arma): canal 1 / canal 2 (motores em paralelo).
const uint8_t PIN_DRIVE_CH1 = 0;
const uint8_t PIN_DRIVE_CH2 = 1;
const uint8_t PIN_WEAPON = 3;
const uint8_t PIN_WEAPON2 = 4;
const bool INVERT_WEAPON2 = false;  // true se o motor 2 da arma estiver soldado ao contrário

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
const uint8_t PINS[] = {PIN_DRIVE_CH1, PIN_DRIVE_CH2, PIN_WEAPON, PIN_WEAPON2};
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
html,body{height:100%;margin:0}
body{background:#111;color:#eee;font-family:sans-serif;display:grid;grid-template-columns:1fr auto 1fr;align-items:center;padding:8px 24px;touch-action:none;--s:min(68vh,240px)}
.side{display:flex;align-items:center;justify-content:center}
.mid{display:flex;flex-direction:column;align-items:center;gap:14px}
#st{font-size:14px;padding:4px 10px;border-radius:10px;background:#a00}
#st.ok{background:#080}
.stick{position:relative;border-radius:50%;background:#222;border:2px solid #555;touch-action:none}
.stick .knob{position:absolute;left:50%;top:50%;width:33%;aspect-ratio:1;border-radius:50%;background:#4af;transform:translate(-50%,-50%)}
#joy{width:var(--s);height:var(--s)}
#thr{width:calc(var(--s)*0.42);height:var(--s);border-radius:40px;background:#1b1b1b}
#thr .knob{width:86%;height:56px;aspect-ratio:auto;border-radius:12px;background:#f80}
.hint{display:none;position:fixed;top:4px;left:0;right:0;text-align:center;font-size:13px;color:#fa0;pointer-events:none}
@media (orientation:portrait){.hint{display:block}}
</style></head><body>
<div class="side"><div id="joy" class="stick"><div class="knob"></div></div></div>
<div class="mid">
  <div id="st">desconectado</div>
  <div>Arma: <span id="wv">0</span>%</div>
</div>
<div class="side"><div id="thr" class="stick"><div class="knob"></div></div></div>
<div class="hint">Gire o celular para o modo horizontal</div>
<script>
const $=id=>document.getElementById(id);
function stick(id,vertical){
  const base=$(id),k=base.firstElementChild,s={x:0,y:0,on:false};
  const place=(dx,dy)=>{k.style.transform=`translate(calc(-50% + ${dx}px),calc(-50% + ${dy}px))`;};
  const lim=()=>({mx:(base.clientWidth-k.offsetWidth)/2,my:(base.clientHeight-k.offsetHeight)/2});
  function move(e){
    const r=base.getBoundingClientRect(),{mx,my}=lim();
    let dx=vertical?0:e.clientX-(r.left+r.width/2),dy=e.clientY-(r.top+r.height/2);
    if(vertical){dy=Math.max(-my,Math.min(my,dy));s.y=-dy/my;}
    else{const m=Math.min(mx,my),d=Math.hypot(dx,dy);if(d>m){dx*=m/d;dy*=m/d;}s.x=dx/m;s.y=-dy/m;}
    place(dx,dy);
  }
  base.addEventListener('pointerdown',e=>{s.on=true;base.setPointerCapture(e.pointerId);move(e);});
  base.addEventListener('pointermove',e=>{if(s.on)move(e);});
  const up=()=>{s.on=false;if(!vertical){s.x=s.y=0;place(0,0);}};
  base.addEventListener('pointerup',up);
  base.addEventListener('pointercancel',up);
  if(vertical){place(0,lim().my);s.y=-1;}
  return s;
}
const L=stick('joy',false),T=stick('thr',true);
let busy=false;
setInterval(async()=>{
  if(busy)return;busy=true;
  const w=Math.round((T.y+1)/2*100);
  $('wv').textContent=w;
  const q='x='+Math.round(L.x*100)+'&y='+Math.round(L.y*100)+'&w='+w;
  try{await fetch('/c?'+q,{cache:'no-store'});$('st').textContent='conectado';$('st').className='ok';}
  catch(e){$('st').textContent='desconectado';$('st').className='';}
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
  pwmWriteUs(3, US_NEUTRAL);
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

  // Arma: só para frente (1500..2000), rampa na subida e parada imediata ao reduzir.
  float target = pctToUs(cmdWeapon);
  if (target <= weaponUs) {
    weaponUs = target;
  } else {
    weaponUs = min(target, weaponUs + WEAPON_RAMP_US_S * dt);
  }
  pwmWriteUs(2, (int)weaponUs);
  pwmWriteUs(3, INVERT_WEAPON2 ? 2 * US_NEUTRAL - (int)weaponUs : (int)weaponUs);
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
