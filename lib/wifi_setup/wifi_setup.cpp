#include "wifi_setup.h"

#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_wifi.h>

#include "epd.h"
#include "ui_config_screen.h"

namespace wifi_setup {

namespace {

// 设备热点名（用户指定）；IP 与配网二维码内容一致
constexpr const char* kApSsid = "DayIJoy-心选日";
const IPAddress kApIp(192, 168, 8, 1);
const IPAddress kApMask(255, 255, 255, 0);
const IPAddress kApGw(192, 168, 8, 1);
constexpr byte kDnsPort = 53;
constexpr uint8_t kApChannel = 6;
constexpr uint8_t kApMaxConn = 4;
constexpr uint32_t kScanCacheMs = 30000;

// NVS：命名空间与键沿用硬件方案 5.1
constexpr const char* kNvsNamespace = "jnr";
constexpr const char* kNvsSsid = "wifiSsid";
constexpr const char* kNvsPass = "wifiPass";

WebServer server(80);
DNSServer dns;
Preferences prefs;

String g_scanJson;
uint32_t g_scanTs = 0;
bool g_apReady = false;

// 配网页（单页 HTML，内嵌 PROGMEM，断网也能打开）——沿用示例配网页风格
const char kIndexHtml[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>设备配网</title>
<style>
  *{box-sizing:border-box}
  body{margin:0;font-family:-apple-system,BlinkMacSystemFont,"PingFang SC","Microsoft YaHei",sans-serif;background:#f4f6fa;color:#222}
  .wrap{max-width:480px;margin:0 auto;padding:24px 18px}
  h1{font-size:22px;margin:0 0 4px}
  .sub{color:#888;font-size:13px;margin-bottom:18px}
  .card{background:#fff;border-radius:12px;padding:16px;box-shadow:0 2px 12px rgba(0,0,0,.06);margin-bottom:14px}
  label{font-size:13px;color:#555;display:block;margin-bottom:6px}
  input{width:100%;padding:11px 12px;border:1px solid #dcdfe6;border-radius:8px;font-size:15px;outline:none;background:#fff}
  input:focus{border-color:#3b82f6}
  button{width:100%;padding:12px;background:#3b82f6;color:#fff;border:0;border-radius:8px;font-size:16px;font-weight:500;margin-top:8px;cursor:pointer}
  button:active{background:#2563eb}
  button.ghost{background:#fff;color:#3b82f6;border:1px solid #3b82f6;margin-top:8px}
  .row{display:flex;gap:8px;align-items:center;justify-content:space-between}
  .list{max-height:240px;overflow-y:auto;border:1px solid #eef0f4;border-radius:8px;margin-top:8px}
  .item{padding:10px 12px;border-bottom:1px solid #f0f2f6;display:flex;justify-content:space-between;align-items:center;cursor:pointer}
  .item:last-child{border-bottom:0}
  .item.sel{background:#eaf2ff}
  .rssi{font-size:12px;color:#888}
  .tip{font-size:12px;color:#888;margin-top:6px}
  .ok{color:#16a34a}
  .err{color:#dc2626}
  .footer{text-align:center;color:#aaa;font-size:12px;margin-top:14px}
</style>
</head>
<body>
<div class="wrap">
  <h1>设备配网</h1>
  <div class="sub">选择 WiFi 并输入密码，提交后设备会自动连接</div>
  <div class="card">
    <div class="row"><label style="margin:0">附近的 WiFi</label><button class="ghost" id="btnScan" style="margin:0;width:auto;padding:8px 14px">刷新</button></div>
    <div class="list" id="list"><div class="item">点击「刷新」扫描附近 WiFi</div></div>
  </div>
  <div class="card">
    <label>WiFi 名称 (SSID)</label>
    <input id="ssid" autocomplete="off" placeholder="点击上方列表自动填入">
    <label style="margin-top:12px">密码</label>
    <input id="pass" type="password" autocomplete="off" placeholder="留空表示开放网络">
    <button id="btnSave">保存并连接</button>
    <div class="tip" id="tip"></div>
  </div>
  <div class="card"><button class="ghost" id="btnReset">忘记网络 / 重置</button></div>
  <div class="footer">DayIJoy · 心选日 · 设备配网</div>
</div>
<script>
const $=id=>document.getElementById(id);
const list=$('list'),tip=$('tip');
function bars(r){if(r>=-55)return'●●●●';if(r>=-65)return'●●●○';if(r>=-75)return'●●○○';if(r>=-85)return'●○○○';return'○○○○';}
async function scan(){
  list.innerHTML='<div class="item">扫描中...</div>';
  try{
    const j=await(await fetch('/scan')).json();
    if(!j.length){list.innerHTML='<div class="item">未发现可用 WiFi</div>';return;}
    list.innerHTML='';
    j.forEach(n=>{
      const div=document.createElement('div');div.className='item';
      div.innerHTML=`<span>${n.ssid||'(隐藏网络)'} ${n.enc?'🔒':''}</span><span class="rssi">${bars(n.rssi)} ${n.rssi}dBm</span>`;
      div.onclick=()=>{document.querySelectorAll('.item').forEach(i=>i.classList.remove('sel'));div.classList.add('sel');$('ssid').value=n.ssid;$('pass').focus();};
      list.appendChild(div);
    });
  }catch(e){list.innerHTML='<div class="item err">扫描失败，请重试</div>';}
}
async function save(){
  const ssid=$('ssid').value.trim();
  if(!ssid){tip.textContent='请输入或选择 WiFi 名称';tip.className='tip err';return;}
  tip.textContent='正在保存并连接，请稍候...';tip.className='tip';$('btnSave').disabled=true;
  try{
    const fd=new FormData();fd.append('ssid',ssid);fd.append('pass',$('pass').value);
    const t=await(await fetch('/save',{method:'POST',body:fd})).text();
    tip.textContent=t||'已提交，设备即将重启...';tip.className='tip ok';
  }catch(e){tip.textContent='提交失败：'+e;tip.className='tip err';$('btnSave').disabled=false;}
}
async function reset(){
  if(!confirm('确定清空已保存的 WiFi 配置吗？'))return;
  await fetch('/reset',{method:'POST'});
  tip.textContent='已重置，设备即将重启...';tip.className='tip ok';
}
$('btnScan').onclick=scan;$('btnSave').onclick=save;$('btnReset').onclick=reset;
</script>
</body>
</html>
)HTML";

void handleRoot() { server.send_P(200, "text/html; charset=utf-8", kIndexHtml); }

// Captive Portal 探测 URL：让 iOS / Android 自动弹出配网页
void handleCaptiveOk() {
  server.sendHeader("Cache-Control", "no-cache");
  server.send(204, "text/plain", "");
}

void handleCaptivePortal() {
  server.sendHeader("Location", "http://" + kApIp.toString() + "/", true);
  server.send(302, "text/plain", "");
}

void refreshScanCache(bool force = false) {
  if (!force && g_scanJson.length() > 0 && (millis() - g_scanTs) < kScanCacheMs) return;

  // 扫描需要 AP+STA；纯 AP 模式下 scanNetworks 会导致热点掉线/搜不到
  WiFi.mode(WIFI_AP_STA);
  int n = WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/false);
  String json = "[";
  for (int i = 0; i < n; ++i) {
    if (i) json += ',';
    String s = WiFi.SSID(i);
    s.replace("\\", "\\\\");
    s.replace("\"", "\\\"");
    json += "{\"ssid\":\"" + s + "\",\"rssi\":" + WiFi.RSSI(i) +
            ",\"enc\":" + (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "false" : "true") + "}";
  }
  json += "]";
  WiFi.scanDelete();
  g_scanJson = json;
  g_scanTs = millis();
  Serial.printf("[wifi_setup] WiFi 扫描完成 n=%d\n", n);
}

void handleScan() {
  refreshScanCache(false);
  server.send(200, "application/json", g_scanJson);
}

void handleSave() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  if (ssid.length() == 0) {
    server.send(400, "text/plain; charset=utf-8", "SSID 不能为空");
    return;
  }
  prefs.putString(kNvsSsid, ssid);
  prefs.putString(kNvsPass, pass);
  server.send(200, "text/plain; charset=utf-8",
              "已保存：" + ssid + "，设备即将重启并尝试连接...");
  Serial.printf("[wifi_setup] 已保存 SSID=%s，1.5s 后重启\n", ssid.c_str());
  delay(1500);
  ESP.restart();
}

void handleReset() {
  prefs.remove(kNvsSsid);
  prefs.remove(kNvsPass);
  server.send(200, "text/plain; charset=utf-8", "已清空配置，设备即将重启...");
  delay(1000);
  ESP.restart();
}

void handleNotFound() {
  handleCaptivePortal();
}

void setupApRadio() {
  WiFi.persistent(false);
  WiFi.setSleep(false);
  esp_wifi_set_ps(WIFI_PS_NONE);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);

  // AP+STA：热点常开的同时允许扫描附近路由器（配网页 /scan 依赖此模式）
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(kApIp, kApGw, kApMask);
  if (!WiFi.softAP(kApSsid, nullptr, kApChannel, /*ssid_hidden=*/0, kApMaxConn)) {
    Serial.println("[wifi_setup] softAP 启动失败，重试...");
    delay(500);
    WiFi.softAP(kApSsid, nullptr, kApChannel, 0, kApMaxConn);
  }
  delay(300);
  g_apReady = true;
  Serial.printf("[wifi_setup] AP 就绪 SSID=%s IP=%s CH=%u\n",
                kApSsid, WiFi.softAPIP().toString().c_str(), kApChannel);
}

void setupHttp() {
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(kDnsPort, "*", kApIp);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/scan", HTTP_GET, handleScan);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/reset", HTTP_POST, handleReset);
  // iOS / Android / Windows Captive Portal 探测
  server.on("/generate_204", HTTP_GET, handleCaptiveOk);
  server.on("/gen_204", HTTP_GET, handleCaptiveOk);
  server.on("/hotspot-detect.html", HTTP_GET, handleCaptivePortal);
  server.on("/library/test/success.html", HTTP_GET, handleCaptivePortal);
  server.on("/connecttest.txt", HTTP_GET, handleCaptivePortal);
  server.on("/ncsi.txt", HTTP_GET, handleCaptivePortal);
  server.onNotFound(handleNotFound);
  server.begin();
}

}  // namespace

void showConfigScreen() {
  epd::clear(epd::WHITE);
  epd::drawImageRle(kCfgScreenRle, kCfgScreenRleLen);

  // 运行时把本机真实 MAC 画到设计稿预留位置
  String mac = WiFi.macAddress();  // 形如 AA:BB:CC:DD:EE:FF
  epd::drawText(CFG_MAC_X, CFG_MAC_Y, mac.c_str(), CFG_MAC_COLOR, CFG_MAC_SCALE);

  Serial.printf("[wifi_setup] 刷出配网画面 MAC=%s\n", mac.c_str());
  if (!epd::flush()) Serial.println("[wifi_setup] 刷屏超时");
}

void startAP() {
  prefs.begin(kNvsNamespace, false);

  // 先启动热点与 HTTP，再刷屏（刷屏阻塞 12-20s，期间手机应能搜到并连上 AP）
  setupApRadio();
  setupHttp();

  showConfigScreen();
}

void loop() {
  if (!g_apReady) return;
  dns.processNextRequest();
  server.handleClient();
}

}  // namespace wifi_setup
