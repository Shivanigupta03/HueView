// =====================================================================
// ESP32-CAM SoftAP color stream + DFPlayer Mini
// - Live MJPEG on :81, dashboard/stats on :80
// - Volume slider on AP dashboard
// - Physical push button on GPIO 13 speaks current color
//
// SD play indexes (ear-tested, updated Sep 25):
// 1 Pink | 2 Device Started | 3 Red | 4 Green | 5 Black | 6 Blue |
// 7 Green | 8 Orange | 9 Yellow | 10 Unknown | 11 White
//
// DFPlayer wiring (AI Thinker ESP32-CAM):
// VCC → 5 V | GND → GND
// RX → GPIO 14 (1 kΩ resistor) | TX → GPIO 15
// SPK1/SPK2 → speaker
//
// Speak button:
// one side → GPIO 13, other side → GND (internal pull-up)
//
// Library: DFRobotDFPlayerMini
// =====================================================================


#include <esp_camera.h>
#include <WiFi.h>
#include <WiFiServer.h>
#include <esp_http_server.h>
#include <esp_wifi.h>
#include <HardwareSerial.h>
#include <DFRobotDFPlayerMini.h>


// =====================================================================
// SoftAP credentials
// =====================================================================
const char* AP_SSID = "ESP32-CAM-AP";
const char* AP_PASS = "12345678";


// =====================================================================
// Camera pins — AI Thinker ESP32-CAM
// =====================================================================
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22


// =====================================================================
// DFPlayer Mini — UART2 on free GPIO pins
// =====================================================================
#define DF_TX_PIN 14 // ESP32 TX → DFPlayer RX (via 1 kΩ)
#define DF_RX_PIN 15 // ESP32 RX ← DFPlayer TX
#define DF_VOLUME 20 // default 0–30
#define SPEAK_BTN_PIN 13 // push button to GND (INPUT_PULLUP)
#define VOL_UP_PIN 2 // volume + button to GND (INPUT_PULLUP)
#define VOL_DOWN_PIN 12 // volume - button to GND (INPUT_PULLUP)
#define VOL_STEP 2 // how much each press changes volume


HardwareSerial dfSerial(2);
DFRobotDFPlayerMini dfPlayer;
bool dfReady = false;
volatile int gVolume = DF_VOLUME;


// =====================================================================
// Port 80 → esp_http_server (UI, /stats, /contrast, /volume)
// Port 81 → plain WiFiServer (MJPEG stream, runs in loop())
// =====================================================================
httpd_handle_t uiServer = NULL;
WiFiServer streamServer(81);


// =====================================================================
// Shared globals written by loop(), read by /stats
// =====================================================================
portMUX_TYPE gMux = portMUX_INITIALIZER_UNLOCKED;
volatile uint8_t gR=0, gG=0, gB=0;
volatile uint16_t gH=0;
volatile uint8_t gS=0, gV=0;
volatile uint32_t gFps=0;
volatile int gContrast=0;
char gColorName[16] = "Scanning...";


// Detected color → DFPlayer track number (ear-tested, updated Sep 25)
// Track 2 = "Device is started" — played only on boot, not here.
int colorTrack(const char* name) {
 if (strcmp(name, "Pink") == 0) return 1;
 if (strcmp(name, "Red") == 0) return 3;
 if (strcmp(name, "Green") == 0) return 4; // track 4 = Green
 if (strcmp(name, "Black") == 0) return 5;
 if (strcmp(name, "Blue") == 0) return 6;
 if (strcmp(name, "Orange") == 0) return 8;
 if (strcmp(name, "Yellow") == 0) return 9;
 if (strcmp(name, "White") == 0) return 11;
 // Cyan, Purple, Gray, Lime → Unknown

 return 10;
}


void playDetectedColor() {
if (!dfReady) {
  Serial.println("[DF] not ready");
  return;
}
int track = colorTrack(gColorName);
dfPlayer.play(track);
Serial.printf("[DF] play track %d (color=%s)\n", track, gColorName);
}


// Debounced speak button (active LOW). Call from loop + stream loop.
// Latches gColorName at the instant the button goes DOWN so that
// continuous frame sampling cannot change the value mid-press.
void checkSpeakButton() {
 static bool lastState = HIGH;
 static unsigned long lastEdgeMs = 0;
 static unsigned long lastSpeakMs = 0;
 static char latchedColor[16] = "";


 bool state = digitalRead(SPEAK_BTN_PIN);
 unsigned long now = millis();


 // Falling edge — latch color immediately
 if (lastState == HIGH && state == LOW) {
   lastEdgeMs = now;
    strncpy(latchedColor, gColorName, sizeof(latchedColor) - 1);
 }


 // Confirm after debounce window (40 ms) and honour 3 s cooldown
 if (state == LOW &&
      (now - lastEdgeMs) >= 40 &&
      (now - lastSpeakMs) >= 3000) {
   lastSpeakMs = now;
   if (!dfReady) {
      Serial.println("[BTN] DF not ready");
    } else {
      int track = colorTrack(latchedColor);
      dfPlayer.play(track);
      Serial.printf("[BTN] pressed → color=%s track=%d\n", latchedColor, track);
    }
 }


 lastState = state;
}
// Volume up/down buttons — debounced, 300 ms repeat rate when held
void checkVolumeButtons() {
 static unsigned long lastUpMs = 0;
 static unsigned long lastDownMs = 0;
 unsigned long now = millis();


 // Volume UP (GPIO 2)
 if (digitalRead(VOL_UP_PIN) == LOW && (now - lastUpMs) >= 300) {
   lastUpMs = now;
   int v = constrain((int)gVolume + VOL_STEP, 0, 30);
   gVolume = v;
   if (dfReady) dfPlayer.volume(v);
   Serial.printf("[VOL+] %d/30\n", v);
 }


 // Volume DOWN (GPIO 12)
 if (digitalRead(VOL_DOWN_PIN) == LOW && (now - lastDownMs) >= 300) {
   lastDownMs = now;
   int v = constrain((int)gVolume - VOL_STEP, 0, 30);
   gVolume = v;
   if (dfReady) dfPlayer.volume(v);
   Serial.printf("[VOL-] %d/30\n", v);
 }
}


// =====================================================================
// YUV422 → RGB
// =====================================================================
 static inline void yuv2rgb(uint8_t y, uint8_t u, uint8_t v, uint8_t &r,
                                                           uint8_t &g, uint8_t &b) {
int C = y-16, D = u-128, E = v-128;
r = (uint8_t)constrain((298*C + 409*E + 128) >> 8, 0, 255); g =
(uint8_t)constrain((298*C - 100*D - 208*E + 128) >> 8, 0, 255); b =
(uint8_t)constrain((298*C + 516*D + 128) >> 8, 0, 255); }



// =====================================================================
// RGB → HSV (H 0-360, S/V 0-100)
// =====================================================================
static void rgb2hsv(uint8_t r, uint8_t g, uint8_t b,
                      uint16_t &h, uint8_t &s, uint8_t &v) {
float rf=r/255.f, gf=g/255.f, bf=b/255.f;
float mx=max(rf,max(gf,bf)), mn=min(rf,min(gf,bf)), d=mx-mn;
v = (uint8_t)(mx*100);
if (mx<0.001f) { s=h=0; return; }
s = (uint8_t)(d/mx*100);
float hf=0;
if (d>0.001f) {
  if (mx==rf) hf = 60.f*fmodf((gf-bf)/d, 6.f);
  else if (mx==gf) hf = 60.f*((bf-rf)/d + 2.f);
  else hf = 60.f*((rf-gf)/d + 4.f);

  if (hf<0) hf+=360.f;
}
h = (uint16_t)hf;
}


// =====================================================================
// HSV-based color name
// =====================================================================
static const char* colorNameFromHSV(uint16_t h, uint8_t s, uint8_t v) {
if (v < 15) return "Black";
if (s < 20 && v > 75) return "White";
if (s < 20) return "Gray";
if (s > 35 && v > 25) {
  if ((h <= 10) || (h >= 350)) return "Red";
  if (h >= 11 && h <= 25) return "Orange";
  if (h >= 26 && h <= 65) return "Yellow";
  if (h >= 66 && h <= 155) return "Green";
  if (h >= 156 && h <= 195) return "Cyan";
  if (h >= 196 && h <= 255) return "Blue";
  if (h >= 256 && h <= 290) return "Purple";
  if (h >= 291 && h <= 349) return "Pink";
}
return "Unknown";
}


static void sampleColors(camera_fb_t *fb) {
uint32_t tr=0,tg=0,tb=0,n=0;
for (int i=0; i+3<(int)fb->len; i+=16) {
  uint8_t r,g,b;
  yuv2rgb(fb->buf[i], fb->buf[i+1], fb->buf[i+3], r,g,b);
  tr+=r; tg+=g; tb+=b; n++;
}
if (!n) return;
uint8_t ar=tr/n, ag=tg/n, ab=tb/n;
uint16_t h; uint8_t s,v;
rgb2hsv(ar,ag,ab,h,s,v);
const char* name = colorNameFromHSV(h, s, v);
portENTER_CRITICAL(&gMux);
gR=ar; gG=ag; gB=ab; gH=h; gS=s; gV=v;
portEXIT_CRITICAL(&gMux);
strncpy(gColorName, name, sizeof(gColorName)-1);
}


// =====================================================================
// Dashboard HTML (stream on :81, click image to speak)
// =====================================================================
static const char INDEX_HTML[] PROGMEM = R"html(
<!DOCTYPE html><html>
<head>

<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32-CAM</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{background:#0b0b0b;color:#eee;font-family:system-ui,sans-serif;
display:flex;flex-direction:column;align-items:center;padding:14px;gap:12px}
h1{font-size:1.1rem;letter-spacing:.05em}
#feed{width:100%;max-width:480px;border:2px solid #333;border-radius:8px;
     background:#111;overflow:hidden;min-height:120px}
#feed img{width:100%;display:block;aspect-ratio:4/3;object-fit:contain;
          image-rendering:pixelated}
.panel{width:100%;max-width:480px;padding:13px;
      background:#161616;border:1px solid #252525;border-radius:8px;
      display:flex;flex-direction:column;gap:10px}
.row{display:flex;align-items:center;gap:10px}
.lbl{font-size:.72rem;color:#9ca3af;text-transform:uppercase
   ; letter-spacing:.06em;white-space:nowrap}
input[type=range]{flex:1;accent-color:#3b82f6}
#cval,#volval{font-weight:700;min-width:2rem;text-align:right}
.grid{display:grid;grid-template-columns:repeat(3,1fr);gap:7px}
.chip{background:#111;border:1px solid #252525;border-radius:6px;
padding:9px 4px;text-align:center}
.chip
b{display:block;font-size:1.3rem;font-weight:700;line-height:1.1} .chip
small{color:#6b7280;font-size:.6rem;text-transform:uppercase}
#swatch{height:34px;border-radius:6px;border:1px solid #333;
transition:background .15s}
#meta{font-size:.72rem;color:#555;text-align:center}
#color-badge{margin-top:4px;padding:14px 8px;border-radius:8px;
             text-align:center;border:1px solid #252525;
              background:#111;transition:background .3s}
#color-badge .clabel{font-size:.7rem;color:#9ca3af;text-transform:uppercase;
                       letter-spacing:.08em;margin-bottom:6px}
#color-badge .cname{font-size:2rem;font-weight:800;letter-spacing:.03em;
                                transition:color .3s}
#hint{font-size:.75rem;color:#6b7280;text-align:center;max-width:480px}
</style>
</head>
<body>
<h1>ESP32-CAM Live (AP)</h1>
<div id="feed">
<img id="camimg" alt="loading stream...">
</div>
<p id="hint">Press the physical button (GPIO 13) to speak the detected color</p>
<div class="panel">
<div class="row">
  <span class="lbl">Contrast</span>
  <input type="range" id="cs" min="0" max="100" value="50"
           oninput="setC(this.value)">

  <span id="cval">50</span>
</div>
<div class="row">
  <span class="lbl">Volume</span>
  <input type="range" id="vs" min="0" max="30" value="20"
           oninput="setVol(this.value)">
  <span id="volval">20</span>
</div>
<span class="lbl">RGB</span>
<div class="grid">
  <div class="chip"><b id="rv">0</b><small>R</small></div>
  <div class="chip"><b id="gv">0</b><small>G</small></div>
  <div class="chip"><b id="bv">0</b><small>B</small></div>
</div>
<div id="swatch"></div>
<span class="lbl">HSV</span>
<div class="grid">
  <div class="chip"><b id="hv">0</b><small>H°</small></div>
  <div class="chip"><b id="sv">0</b><small>S %</small></div>
  <div class="chip"><b id="vv">0</b><small>V %</small></div>
</div>
<div id="meta">connecting…</div>
</div>


<div id="color-badge" style="width:100%;max-width:480px;">
<div class="clabel">Detected Color</div>
<div class="cname" id="cname">—</div>
</div>
<script>
document.getElementById('camimg').src =
'http://' + location.hostname + ':81/stream';


async function setC(v){
document.getElementById('cval').textContent = v;
try{ await fetch('/contrast?v='+v); }catch(_){}
}


async function setVol(v){
document.getElementById('volval').textContent = v;
try{ await fetch('/volume?v='+v); }catch(_){}
}


async function poll(){
try{
  const d = await (await fetch('/stats')).json();
  document.getElementById('rv').textContent = d.r;
  document.getElementById('gv').textContent = d.g;
  document.getElementById('bv').textContent = d.b;
  document.getElementById('hv').textContent = d.h;
  document.getElementById('sv').textContent = d.s;
  document.getElementById('vv').textContent = d.v;
  document.getElementById('swatch').style.background =
     'rgb('+d.r+','+d.g+','+d.b+')';
  document.getElementById('meta').textContent =
     'QQVGA 160×120 · ~'+d.fps+' FPS · SoftAP' +
     (d.dfReady ? ' · Speaker OK' : ' · Speaker OFF');
  document.getElementById('cs').value = d.cui;
  document.getElementById('cval').textContent = d.cui;
  if (typeof d.vol === 'number') {
     document.getElementById('vs').value = d.vol;
     document.getElementById('volval').textContent = d.vol;
  }
  const colorMap = {
     Red:'#ef4444',Orange:'#f97316',Yellow:'#eab308',Green:'#22c55e',
  Cyan:'#06b6d4',Blue:'#3b82f6',Purple:'#a855f7',Pink:'#ec4899',
  White:'#f1f5f9',Gray:'#6b7280',Black:'#374151',Unknown:'#444' };
  const cn = d.color || '—';
  const cc = colorMap[cn] || '#888';
  document.getElementById('cname').textContent = cn;
  document.getElementById('cname').style.color = cc;
  document.getElementById('color-badge').style.borderColor = cc+'66';
} catch(e){
  document.getElementById('meta').textContent = 'retrying stats…';
}
}
setInterval(poll, 250);
poll();
</script>
</body></html>
)html";


// =====================================================================
// Port-80 handlers
// =====================================================================
static esp_err_t h_root(httpd_req_t *req){
httpd_resp_set_type(req,"text/html");
return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}


static esp_err_t h_stats(httpd_req_t *req){
uint8_t r,g,b,s,v; uint16_t h; uint32_t fps;
portENTER_CRITICAL(&gMux);
r=gR;g=gG;b=gB;h=gH;s=gS;v=gV;fps=gFps;
portEXIT_CRITICAL(&gMux);
int cui = constrain((gContrast+2)*25, 0, 100);
char buf[240];
snprintf(buf,sizeof(buf),
  "{\"r\":%u,\"g\":%u,\"b\":%u,\"h\":%u,\"s\":%u,\"v\":%u,"

"\"fps\":%u,\"cui\":%d,\"vol\":%d,\"color\":\"%s\",\"dfReady\":%s}",
r,g,b,h,s,v,fps,cui,gVolume,gColorName, dfReady ? "true" : "false");
httpd_resp_set_type(req,"application/json");
httpd_resp_set_hdr(req,"Cache-Control","no-store");
httpd_resp_set_hdr(req,"Access-Control-Allow-Origin","*");
return httpd_resp_send(req,buf,HTTPD_RESP_USE_STRLEN); }


static esp_err_t h_contrast(httpd_req_t *req){
char qs[24]={0}, val[8]={0};
httpd_req_get_url_query_str(req,qs,sizeof(qs));
httpd_query_key_value(qs,"v",val,sizeof(val));
int ui = constrain(atoi(val),0,100);
int c = constrain((ui/25)-2,-2,2);
gContrast = c;
sensor_t *s = esp_camera_sensor_get();
if (s) s->set_contrast(s,c);
Serial.printf("[contrast] slider=%d sensor=%d\n",ui,c);
return httpd_resp_sendstr(req,"ok");
}


static esp_err_t h_volume(httpd_req_t *req){
char qs[24]={0}, val[8]={0};
httpd_req_get_url_query_str(req,qs,sizeof(qs));
httpd_query_key_value(qs,"v",val,sizeof(val));
int vol = constrain(atoi(val), 0, 30);
gVolume = vol;
if (dfReady) dfPlayer.volume(vol);
Serial.printf("[volume] %d/30\n", vol);
return httpd_resp_sendstr(req,"ok");
}


static void startUIServer(){
httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
cfg.server_port = 80;
cfg.max_open_sockets = 5;
if (httpd_start(&uiServer, &cfg) != ESP_OK){
  Serial.println("UI server FAILED"); return;
}
httpd_uri_t u0 = {"/", HTTP_GET, h_root, NULL}; httpd_uri_t
u1 = {"/stats", HTTP_GET, h_stats, NULL}; httpd_uri_t u2 =
{"/contrast", HTTP_GET, h_contrast, NULL}; httpd_uri_t u3 =
{"/volume",             HTTP_GET,            h_volume,             NULL};
httpd_register_uri_handler(uiServer,&u0);
httpd_register_uri_handler(uiServer,&u1);
httpd_register_uri_handler(uiServer,&u2);
httpd_register_uri_handler(uiServer,&u3);
Serial.println("UI server ready → http://192.168.4.1");
}



// =====================================================================
// Serve one MJPEG client on the raw WiFiServer (port 81)
// =====================================================================
static void serveStream(WiFiClient client){
Serial.println("[stream] client connected");


client.print(
  "HTTP/1.1 200 OK\r\n"
  "Content-Type: multipart/x-mixed-replace;boundary=fb\r\n"
  "Cache-Control: no-cache\r\n"
  "Pragma: no-cache\r\n\r\n");


uint32_t frames=0, t0=millis();


while (client.connected()){
  checkSpeakButton();
  checkVolumeButtons();


  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb){ delay(5); continue; }



  sampleColors(fb);


  uint8_t *jpg=NULL; size_t jlen=0;
  bool ok = frame2jpg(fb, 50, &jpg, &jlen);
  esp_camera_fb_return(fb);
  if (!ok || !jpg){ continue; }


  char hdr[72];
  int hl = snprintf(hdr,sizeof(hdr),
     "\r\n--fb\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
     (unsigned)jlen);
  client.write((uint8_t*)hdr, hl);
  client.write(jpg, jlen);
  free(jpg);


  frames++;
  uint32_t dt = millis()-t0;
  if (dt >= 1000){
    portENTER_CRITICAL(&gMux);
     gFps = frames;
    portEXIT_CRITICAL(&gMux);
     frames=0; t0=millis();
  }
}
Serial.println("[stream] client disconnected");
portENTER_CRITICAL(&gMux);
gFps=0;
portEXIT_CRITICAL(&gMux);
}


// =====================================================================
void setup(){
Serial.begin(115200);
delay(400);
Serial.println("\n=== ESP32-CAM SoftAP + DFPlayer ===");


pinMode(SPEAK_BTN_PIN, INPUT_PULLUP);
pinMode(VOL_UP_PIN, INPUT_PULLUP);
pinMode(VOL_DOWN_PIN, INPUT_PULLUP);


// DFPlayer first (needs settle time)
dfSerial.begin(9600, SERIAL_8N1, DF_RX_PIN, DF_TX_PIN);
delay(1000);
if (dfPlayer.begin(dfSerial, false, true)) {
  dfPlayer.volume(gVolume);
  dfReady = true;
  Serial.printf("[DF] ready — volume %d/30\n", gVolume);
} else {
  Serial.println("[DF] not found — check wiring/SD card");
}


pinMode(PWDN_GPIO_NUM, OUTPUT);
digitalWrite(PWDN_GPIO_NUM,HIGH); delay(80);
digitalWrite(PWDN_GPIO_NUM,LOW); delay(80);


camera_config_t cfg;
cfg.ledc_channel=LEDC_CHANNEL_0; cfg.ledc_timer=LEDC_TIMER_0;
cfg.pin_d0=Y2_GPIO_NUM; cfg.pin_d1=Y3_GPIO_NUM;
cfg.pin_d2=Y4_GPIO_NUM; cfg.pin_d3=Y5_GPIO_NUM;
cfg.pin_d4=Y6_GPIO_NUM; cfg.pin_d5=Y7_GPIO_NUM;
cfg.pin_d6=Y8_GPIO_NUM; cfg.pin_d7=Y9_GPIO_NUM;
cfg.pin_xclk=XCLK_GPIO_NUM; cfg.pin_pclk=PCLK_GPIO_NUM;
cfg.pin_vsync=VSYNC_GPIO_NUM; cfg.pin_href=HREF_GPIO_NUM;
cfg.pin_sscb_sda=SIOD_GPIO_NUM; cfg.pin_sscb_scl=SIOC_GPIO_NUM;
cfg.pin_pwdn=PWDN_GPIO_NUM; cfg.pin_reset=RESET_GPIO_NUM;
cfg.xclk_freq_hz = 20000000;
cfg.pixel_format = PIXFORMAT_YUV422;
cfg.frame_size = FRAMESIZE_QQVGA;
cfg.grab_mode = CAMERA_GRAB_LATEST;
cfg.jpeg_quality = 50;
cfg.fb_count = psramFound() ? 2 : 1;
cfg.fb_location = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;


if (esp_camera_init(&cfg) != ESP_OK){
  Serial.println("Camera FAILED"); return;

}
sensor_t *s = esp_camera_sensor_get();
if (s){
  s->set_contrast(s,0);
  s->set_brightness(s,0);
  s->set_saturation(s,0);
  s->set_framesize(s,FRAMESIZE_QQVGA);
}
Serial.println("Camera OK — QQVGA 160×120");


WiFi.mode(WIFI_AP);
WiFi.softAP(AP_SSID, AP_PASS);
esp_wifi_set_ps(WIFI_PS_NONE);
Serial.printf("AP: %s / %s\n", AP_SSID, AP_PASS);
Serial.printf("Dashboard → http://%s\n", WiFi.softAPIP().toString().c_str());
Serial.printf("Stream → http://%s:81/stream\n", WiFi.softAPIP().toString().c_str());



camera_fb_t *fb = esp_camera_fb_get();
if (fb){ sampleColors(fb); esp_camera_fb_return(fb); }


startUIServer();
streamServer.begin();
Serial.println("Stream server (WiFiServer) ready on :81");
Serial.println("Speak button: GPIO 13 → GND (press to speak color)");


// Play startup sound LAST — after camera, WiFi, and servers are all
// ready so nothing disturbs the DFPlayer UART during playback.
if (dfReady) {
  delay(500); // let everything settle
  dfPlayer.play(2);
  Serial.println("[DF] startup sound → track 2 (Device is started)");
}
}


void loop(){
checkSpeakButton();
checkVolumeButtons();


WiFiClient client = streamServer.accept();
if (client){
  uint32_t deadline = millis() + 200;
  while (client.available() && millis() < deadline) client.read();
  serveStream(client);
  client.stop();
} else {
  camera_fb_t *fb = esp_camera_fb_get();
  if (fb){ sampleColors(fb); esp_camera_fb_return(fb); }
  delay(20);
}
}
