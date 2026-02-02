// Global joystick variables
int leftY = 0;
int rightX = 0;

#define WIFI_AP_MODE
// #define WIFI_STA_MODE
// #define WIFI_STA_AP_MODE

#include "esp_camera.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ElegantOTA.h>
#include <ArduinoJson.h>

// Pin definitions (AI Thinker)
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// Motor pins
#define IN1 16
#define IN2 0
#define IN3 1
#define IN4 3

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// WiFi/AP credentials
const char* ap_ssid = "MySpyCar1";
const char* ap_password = "123456789";

// Motor control
void setMotors(int left, int right)
{
  digitalWrite(IN1, left == 1 ? HIGH : LOW);
  digitalWrite(IN2, left == -1 ? HIGH : LOW);
  digitalWrite(IN3, right == 1 ? HIGH : LOW);
  digitalWrite(IN4, right == -1 ? HIGH : LOW);
}

// WebSocket message handler
void onWSMsg(AsyncWebSocket* server, AsyncWebSocketClient* client, AwsFrameInfo* info,
  uint8_t* data, size_t len)
{
  StaticJsonDocument<128> doc;
  DeserializationError err = deserializeJson(doc, data, len);
  if (!err) {
    leftY = doc["joyL"][1];
    rightX = doc["joyR"][0];
    int l = 0, r = 0;
    if (abs(leftY) > 32) l = r = leftY > 0 ? 1 : -1;
    if (abs(rightX) > 32) {
      l = rightX < 0 ? -1 : 1;
      r = rightX > 0 ? -1 : 1;
    }
    setMotors(l, r);
  }
}

// MJPEG stream handler
void handle_jpg_stream(AsyncWebServerRequest* request)
{
  AsyncWebServerResponse* response = request->beginChunkedResponse("multipart/x-mixed-replace; boundary=frame",
    [](uint8_t* buffer, size_t maxLen, size_t index) -> size_t {
      camera_fb_t* fb = esp_camera_fb_get();
      if (!fb) return 0;
      size_t len = 0;
      len += snprintf((char*)buffer, maxLen, "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", fb->len);
      if (maxLen - len >= fb->len) {
        memcpy(buffer + len, fb->buf, fb->len);
        len += fb->len;
      }
      else {
        esp_camera_fb_return(fb);
        return 0;
      }
      len += snprintf((char*)buffer + len, maxLen - len, "\r\n");
      esp_camera_fb_return(fb);
      return len;
    });
  request->send(response);
}

// Red LED on GPIO33 control from your external RedLedControl.ino interface
extern void redLedSetup();
extern void updateRedLed(bool forward, bool backward, bool left, bool right);

void setup()
{
  Serial.begin(115200);
  delay(500);

  redLedSetup();

  // Camera init
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  if (psramFound()) {
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  }
  else {
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }
  esp_camera_init(&config);

  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);

#if defined(WIFI_AP_MODE)
  WiFi.softAP(ap_ssid, ap_password);
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());
#elif defined(WIFI_STA_MODE)
  // Not used in this mode
#elif defined(WIFI_STA_AP_MODE)
  // Not used in this mode
#endif

  server.on("/stream", HTTP_GET, handle_jpg_stream);

  ws.onEvent([](AsyncWebSocket* server, AsyncWebSocketClient* client,
    AwsEventType type, void* arg, uint8_t* data, size_t len) {
      if (type == WS_EVT_DATA)
        onWSMsg(server, client, (AwsFrameInfo*)arg, data, len);
    });

  server.addHandler(&ws);

  ElegantOTA.begin(&server);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(200, "text/html", R"rawliteral(
<!DOCTYPE html>
<html>
  <head>
  <meta charset="UTF-8" />
  <title>ESP32-CAM Spy Car</title>
  <style>
    body { font-family: Arial; background: #111; color: #fff; text-align: center; }
    #video { width: 100%; max-width: 480px; border: 6px solid #333; margin-bottom: 10px; }
    .joysticks { display: flex; justify-content: space-around; margin-top: 30px; }
    .joystick-wrap { width: 120px; height: 120px; background: #222; border-radius: 16px; display:flex; flex-direction:column; align-items: center; padding: 10px; }
    canvas { background: #444; border-radius: 50%; cursor: pointer; }
    .label { margin: 5px 0 0 0; font-size: 15px; color: #aaa; }
  </style>
  </head>
  <body>
  <h2>ESP32-CAM Spy Car Control</h2>
  <img id="video" src="/stream" alt="video stream" />
  <div class="joysticks">
    <div class="joystick-wrap">
      <span class="label">Left: Fwd/Rev</span>
      <canvas id="joyL" width="100" height="100"></canvas>
    </div>
    <div class="joystick-wrap">
      <span class="label">Right: L/R Turn</span>
      <canvas id="joyR" width="100" height="100"></canvas>
    </div>
  </div>
  <script>
  const ws = new WebSocket(`ws://${window.location.hostname}/ws`);

  function sendJoystick(lx, ly, rx, ry) {
    ws.send(JSON.stringify({ "joyL": [lx, ly], "joyR": [rx, ry] }));
  }

  function setupJoystick(canvas, callback) {
    const ctx = canvas.getContext('2d');
    const radius = 45, knobRadius = 18;
    let dragging = false, value = [0, 0];

    function draw(x, y) {
      // Clear canvas
      ctx.clearRect(0, 0, canvas.width, canvas.height);
      
      // Draw outer circle (joystick base)
      ctx.beginPath();
      ctx.arc(50, 50, radius, 0, Math.PI * 2);
      ctx.strokeStyle = '#666';
      ctx.lineWidth = 2;
      ctx.stroke();
      
      // Fill outer circle with subtle gradient
      ctx.beginPath();
      ctx.arc(50, 50, radius, 0, Math.PI * 2);
      ctx.fillStyle = '#333';
      ctx.fill();
      
      // Draw center dot
      ctx.beginPath();
      ctx.arc(50, 50, 2, 0, Math.PI * 2);
      ctx.fillStyle = '#777';
      ctx.fill();
      
      // Draw knob (always visible, follows finger/mouse)
      ctx.beginPath();
      ctx.arc(50 + x, 50 + y, knobRadius, 0, Math.PI * 2);
      
      // Knob gradient effect
      const gradient = ctx.createRadialGradient(50 + x, 50 + y, 0, 50 + x, 50 + y, knobRadius);
      if (dragging) {
        gradient.addColorStop(0, '#00ffff');
        gradient.addColorStop(0.7, '#0099cc');
        gradient.addColorStop(1, '#006699');
      } else {
        gradient.addColorStop(0, '#00cccc');
        gradient.addColorStop(0.7, '#008899');
        gradient.addColorStop(1, '#005566');
      }
      
      ctx.fillStyle = gradient;
      ctx.fill();
      
      // Knob border
      ctx.strokeStyle = dragging ? '#ffffff' : '#aaaaaa';
      ctx.lineWidth = 2;
      ctx.stroke();
      
      // Add a small highlight to make it look more 3D
      ctx.beginPath();
      ctx.arc(50 + x - 6, 50 + y - 6, 4, 0, Math.PI * 2);
      ctx.fillStyle = 'rgba(255, 255, 255, 0.6)';
      ctx.fill();
    }

    function getXY(e) {
      const rect = canvas.getBoundingClientRect();
      let ex = (e.touches ? e.touches[0].clientX : e.clientX) - rect.left - 50;
      let ey = (e.touches ? e.touches[0].clientY : e.clientY) - rect.top - 50;
      const len = Math.sqrt(ex * ex + ey * ey);
      if (len > radius) {
        ex *= radius / len;
        ey *= radius / len;
      }
      return [Math.round(ex), Math.round(ey)];
    }

    function onDown(e) {
      dragging = true;
      canvas.style.cursor = 'grabbing';
      update(e);
      e.preventDefault();
    }

    function onUp(e) {
      dragging = false;
      canvas.style.cursor = 'pointer';
      value = [0, 0];
      draw(0, 0);
      callback(0, 0);
      e.preventDefault();
    }

    function update(e) {
      if (!dragging) return;
      e.preventDefault();
      value = getXY(e);
      draw(value[0], value[1]);
      callback(value[0], value[1]);
    }

    // Mouse events
    canvas.addEventListener('mousedown', onDown);
    canvas.addEventListener('mouseup', onUp);
    canvas.addEventListener('mouseleave', onUp);
    canvas.addEventListener('mousemove', update);
    
    // Touch events
    canvas.addEventListener('touchstart', onDown);
    canvas.addEventListener('touchend', onUp);
    canvas.addEventListener('touchcancel', onUp);
    canvas.addEventListener('touchmove', update);

    // Initial draw
    draw(0, 0);
  }

  let joyL = [0, 0],
      joyR = [0, 0];

  setupJoystick(document.getElementById('joyL'), (x, y) => { joyL = [x, y]; });
  setupJoystick(document.getElementById('joyR'), (x, y) => { joyR = [x, y]; });

  setInterval(() => sendJoystick(joyL[0], joyL[1], joyR[0], joyR[1]), 60);

  ws.onopen = () => console.log("WS Connected");
  ws.onclose = () => console.log("WS Disconnected");
  ws.onerror = e => console.log("WS error", e);
  ws.onmessage = e => console.log("WS message", e.data);
  </script>
  </body>
</html>
  )rawliteral");
});

server.begin();

}

void loop()
{
  bool forward = (leftY > 30);
  bool backward = (leftY < -30);
  bool leftDir = (rightX < -30);
  bool rightDir = (rightX > 30);

  updateRedLed(forward, backward, leftDir, rightDir);
}
