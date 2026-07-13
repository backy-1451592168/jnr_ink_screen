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

// 设备热点名；IP 与配网二维码内容一致
constexpr const char* kApSsid = "DayIJoy-心选日";
const IPAddress kApIp(192, 168, 8, 1);
const IPAddress kApMask(255, 255, 255, 0);
const IPAddress kApGw(192, 168, 8, 1);
constexpr byte kDnsPort = 53;
constexpr uint8_t kApChannel = 6;
constexpr uint8_t kApMaxConn = 4;
constexpr uint32_t kScanCacheMs = 30000;
constexpr uint32_t kConnectTimeoutMs = 20000;

// NVS：命名空间与键沿用硬件方案 5.1
constexpr const char* kNvsNamespace = "jnr";
constexpr const char* kNvsSsid = "wifiSsid";
constexpr const char* kNvsPass = "wifiPass";
constexpr const char* kNvsApiBase = "apiBase";
constexpr const char* kNvsApiBaseBak = "apiBaseBak";
constexpr const char* kNvsSyncHour = "syncHour";
constexpr const char* kNvsStaIp = "staIp";
constexpr const char* kNvsStaGw = "staGw";
constexpr const char* kNvsStaMask = "staMask";
constexpr const char* kNvsStaDns = "staDns";
constexpr uint8_t kDefaultSyncHour = 0;

WebServer server(80);
DNSServer dns;

String g_scanJson;
uint32_t g_scanTs = 0;
bool g_apReady = false;
bool g_httpReady = false;
bool g_dnsOn = false;

// ---------- HTML：配网页 ----------
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
  input[type="text"],input[type="password"]{width:100%;padding:11px 12px;border:1px solid #dcdfe6;border-radius:8px;font-size:16px;outline:none;background:#fff;-webkit-user-select:text;user-select:text;-webkit-appearance:none}
  input:focus{border-color:#3b82f6}
  .pass-wrap{position:relative}
  .pass-wrap input{padding-right:48px}
  .eye{position:absolute;right:4px;top:50%;transform:translateY(-50%);width:40px;height:40px;border:0;background:transparent;color:#666;font-size:13px;padding:0;margin:0;cursor:pointer}
  button{width:100%;padding:12px;background:#3b82f6;color:#fff;border:0;border-radius:8px;font-size:16px;font-weight:500;margin-top:8px;cursor:pointer}
  button:active{background:#2563eb}
  button:disabled{opacity:.6}
  button.ghost{background:#fff;color:#3b82f6;border:1px solid #3b82f6;margin-top:8px}
  .row{display:flex;gap:8px;align-items:center;justify-content:space-between}
  .list{max-height:240px;overflow-y:auto;border:1px solid #eef0f4;border-radius:8px;margin-top:8px}
  .item{padding:10px 12px;border-bottom:1px solid #f0f2f6;display:flex;justify-content:space-between;align-items:center;cursor:pointer}
  .item:last-child{border-bottom:0}
  .item.sel{background:#eaf2ff}
  .rssi{font-size:12px;color:#888}
  .tip{font-size:12px;color:#888;margin-top:6px}
  .tip.ok{color:#16a34a}
  .tip.err,.item.err{color:#dc2626}
  .footer{text-align:center;color:#aaa;font-size:12px;margin-top:14px}
  .mask{position:fixed;inset:0;background:rgba(0,0,0,.45);display:flex;align-items:center;justify-content:center;z-index:99}
  .mask[hidden]{display:none}
  .mask-box{background:#fff;border-radius:12px;padding:28px 24px;max-width:280px;text-align:center;font-size:15px;line-height:1.5;box-shadow:0 8px 24px rgba(0,0,0,.18)}
  .spin{width:28px;height:28px;margin:0 auto 14px;border:3px solid #e5e7eb;border-top-color:#3b82f6;border-radius:50%;animation:spin 0.8s linear infinite}
  @keyframes spin{to{transform:rotate(360deg)}}
</style>
</head>
<body>
<div class="wrap">
  <h1>设备配网</h1>
  <div class="sub">选择 WiFi 并输入密码，连接成功后进入设备信息页</div>
  <div class="card">
    <div class="row"><label style="margin:0">附近的 WiFi</label><button type="button" class="ghost" id="btnScan" style="margin:0;width:auto;padding:8px 14px">刷新</button></div>
    <div class="list" id="list"><div class="item">点击「刷新」扫描附近 WiFi</div></div>
  </div>
  <div class="card">
    <label for="ssid">WiFi 名称 (SSID)</label>
    <input id="ssid" type="text" name="ssid" inputmode="text" enterkeyhint="next" autocomplete="off" autocapitalize="off" autocorrect="off" spellcheck="false" placeholder="可手动输入，或点上方列表填入">
    <label for="pass" style="margin-top:12px">密码</label>
    <div class="pass-wrap">
      <input id="pass" type="password" name="pass" inputmode="text" enterkeyhint="done" autocomplete="off" autocapitalize="off" autocorrect="off" spellcheck="false" placeholder="留空表示开放网络">
      <button type="button" class="eye" id="btnEye" aria-label="显示或隐藏密码">显示</button>
    </div>
    <button type="button" id="btnSave">保存并连接</button>
    <div class="tip" id="tip"></div>
  </div>
  <div class="card"><button type="button" class="ghost" id="btnReset">忘记网络 / 重置</button></div>
  <div class="footer">DayIJoy · 心选日 · 设备配网</div>
</div>
<div class="mask" id="mask" hidden>
  <div class="mask-box"><div class="spin"></div>请等待 WiFi 连接中...</div>
</div>
<script>
const $=id=>document.getElementById(id);
const list=$('list'),tip=$('tip'),mask=$('mask');
function bars(r){if(r>=-55)return'●●●●';if(r>=-65)return'●●●○';if(r>=-75)return'●●○○';if(r>=-85)return'●○○○';return'○○○○';}
function setLoading(on){mask.hidden=!on;$('btnSave').disabled=!!on;$('btnScan').disabled=!!on;}
async function scan(){
  list.innerHTML='<div class="item">扫描中...</div>';
  try{
    const j=await(await fetch('/scan')).json();
    if(!j.length){list.innerHTML='<div class="item">未发现可用 WiFi</div>';return;}
    list.innerHTML='';
    j.forEach(n=>{
      const div=document.createElement('div');div.className='item';
      const name=document.createElement('span');name.textContent=(n.ssid||'(隐藏网络)')+(n.enc?' 🔒':'');
      const rssi=document.createElement('span');rssi.className='rssi';rssi.textContent=bars(n.rssi)+' '+n.rssi+'dBm';
      div.appendChild(name);div.appendChild(rssi);
      div.onclick=()=>{document.querySelectorAll('.item').forEach(i=>i.classList.remove('sel'));div.classList.add('sel');$('ssid').value=n.ssid||'';$('pass').focus();};
      list.appendChild(div);
    });
  }catch(e){list.innerHTML='<div class="item err">扫描失败，请重试</div>';}
}
async function save(){
  const ssid=$('ssid').value.trim();
  if(!ssid){tip.textContent='请输入或选择 WiFi 名称';tip.className='tip err';return;}
  tip.textContent='';tip.className='tip';
  setLoading(true);
  try{
    const fd=new FormData();fd.append('ssid',ssid);fd.append('pass',$('pass').value);
    const r=await fetch('/save',{method:'POST',body:fd});
    const t=await r.text();
    if(!r.ok){setLoading(false);tip.textContent=t||'连接失败';tip.className='tip err';return;}
    tip.textContent=t||'连接成功，正在跳转...';tip.className='tip ok';
    location.href='/device';
  }catch(e){setLoading(false);tip.textContent='提交失败：'+e;tip.className='tip err';}
}
async function reset(){
  if(!confirm('确定清空已保存的 WiFi 配置吗？'))return;
  await fetch('/reset',{method:'POST'});
  tip.textContent='已重置，设备即将重启...';tip.className='tip ok';
}
$('btnEye').onclick=()=>{
  const p=$('pass');const show=p.type==='password';
  p.type=show?'text':'password';
  $('btnEye').textContent=show?'隐藏':'显示';
};
$('btnScan').onclick=scan;$('btnSave').onclick=save;$('btnReset').onclick=reset;
</script>
</body>
</html>
)HTML";

// ---------- HTML：设备信息 / 服务地址 ----------
const char kDeviceHtml[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>设备信息</title>
<style>
  *{box-sizing:border-box}
  body{margin:0;font-family:-apple-system,BlinkMacSystemFont,"PingFang SC","Microsoft YaHei",sans-serif;background:#f4f6fa;color:#222}
  .wrap{max-width:480px;margin:0 auto;padding:24px 18px}
  h1{font-size:22px;margin:0 0 4px}
  .sub{color:#888;font-size:13px;margin-bottom:18px}
  .card{background:#fff;border-radius:12px;padding:16px;box-shadow:0 2px 12px rgba(0,0,0,.06);margin-bottom:14px}
  .row{display:flex;justify-content:space-between;gap:12px;padding:8px 0;border-bottom:1px solid #f0f2f6;font-size:14px}
  .row:last-child{border-bottom:0}
  .k{color:#888;flex-shrink:0}
  .v{word-break:break-all;text-align:right}
  label{font-size:13px;color:#555;display:block;margin-bottom:6px}
  input,select{width:100%;padding:11px 12px;border:1px solid #dcdfe6;border-radius:8px;font-size:14px;outline:none;background:#fff}
  input:focus,select:focus{border-color:#3b82f6}
  button{width:100%;padding:12px;background:#3b82f6;color:#fff;border:0;border-radius:8px;font-size:16px;font-weight:500;margin-top:8px;cursor:pointer}
  button:active{background:#2563eb}
  button.ghost{background:#fff;color:#3b82f6;border:1px solid #3b82f6}
  button.ok{background:#16a34a;color:#fff}
  .tip{font-size:12px;color:#888;margin-top:8px}
  .tip.ok{color:#16a34a}
  .tip.err{color:#dc2626}
  .footer{text-align:center;color:#aaa;font-size:12px;margin-top:14px}
  .done-box{display:none;margin-top:12px;padding:12px;background:#f0fdf4;border:1px solid #bbf7d0;border-radius:8px}
  .done-box.show{display:block}
  .done-box a{color:#2563eb;word-break:break-all;font-size:14px}
  .done-actions{display:flex;gap:8px;margin-top:10px}
  .done-actions button{margin-top:0}
</style>
</head>
<body>
<div class="wrap">
  <h1>设备信息</h1>
  <div class="sub">查看本机网络信息，并设置小程序服务地址</div>
  <div class="card" id="info">
    <div class="row"><span class="k">状态</span><span class="v">加载中...</span></div>
  </div>
  <div class="card">
    <label>主服务地址（API 根）</label>
    <input id="apiBase" autocomplete="off" placeholder="https://host:port/api">
    <label style="margin-top:12px">备用服务地址（可选）</label>
    <input id="apiBaseBak" autocomplete="off" placeholder="留空表示不启用备用">
    <label style="margin-top:12px">每日自动同步时间</label>
    <select id="syncHour"></select>
    <div class="tip">接口路径写死在固件；此处只配域名/端口等前缀。主地址留空用默认值。请求时先试主，失败再试备，下次仍先主。同步时间为本地整点，每天只自动拉取一次（拉图逻辑后续实现）。</div>
    <button type="button" id="btnSave">保存设置</button>
    <button type="button" class="ok" id="btnFinish">完成并重启</button>
    <div class="tip" id="tip"></div>
    <div class="done-box" id="doneBox">
      <div class="tip ok" style="margin:0 0 8px">设备即将重启。请先改连家里 WiFi，再用下方地址打开管理页：</div>
      <a id="adminLink" href="#" target="_blank" rel="noopener"></a>
      <div class="done-actions">
        <button type="button" class="ghost" id="btnCopy">复制地址</button>
        <button type="button" id="btnOpen">打开管理页</button>
      </div>
    </div>
  </div>
  <div class="footer">DayIJoy · 心选日 · 局域网管理</div>
</div>
<script>
const $=id=>document.getElementById(id);
const tip=$('tip');
let adminUrl='';
(function(){
  const sel=$('syncHour');
  for(let h=0;h<24;h++){
    const o=document.createElement('option');
    o.value=String(h);
    o.textContent=String(h).padStart(2,'0')+':00';
    sel.appendChild(o);
  }
})();
function esc(s){return String(s==null?'':s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));}
function apiForm(){
  const fd=new FormData();
  fd.append('apiBase',$('apiBase').value.trim());
  fd.append('apiBaseBak',$('apiBaseBak').value.trim());
  fd.append('syncHour',$('syncHour').value);
  return fd;
}
function showAdminLink(url){
  adminUrl=url||'';
  const a=$('adminLink');
  a.href=adminUrl||'#';
  a.textContent=adminUrl||'(无局域网 IP)';
  $('doneBox').classList.add('show');
}
async function load(){
  try{
    const j=await(await fetch('/info')).json();
    const hour=(j.syncHour==null?0:Number(j.syncHour));
    adminUrl=j.adminUrl&&j.adminUrl!=='-'?j.adminUrl:'';
    const rows=[
      ['WiFi',j.ssid||'-'],
      ['局域网 IP',j.ip||'-'],
      ['网关',j.gateway||'-'],
      ['子网掩码',j.mask||'-'],
      ['MAC',j.mac||'-'],
      ['管理页',adminUrl||'-'],
      ['主服务地址',j.apiBase||'-'],
      ['备用服务地址',j.apiBaseBak||'(未设置)'],
      ['每日同步',String(hour).padStart(2,'0')+':00'],
    ];
    $('info').innerHTML=rows.map(([k,v])=>{
      if(k==='管理页'&&adminUrl){
        return `<div class="row"><span class="k">${esc(k)}</span><span class="v"><a href="${esc(adminUrl)}">${esc(adminUrl)}</a></span></div>`;
      }
      return `<div class="row"><span class="k">${esc(k)}</span><span class="v">${esc(v)}</span></div>`;
    }).join('');
    $('apiBase').value=j.apiBase||'';
    $('apiBaseBak').value=j.apiBaseBak||'';
    $('syncHour').value=String(hour);
  }catch(e){
    $('info').innerHTML='<div class="row"><span class="k">错误</span><span class="v" style="color:#dc2626">加载失败</span></div>';
  }
}
async function saveApi(){
  tip.textContent='保存中...';tip.className='tip';
  try{
    const t=await(await fetch('/api-base',{method:'POST',body:apiForm()})).text();
    tip.textContent=t||'已保存';tip.className='tip ok';
    load();
  }catch(e){tip.textContent='保存失败：'+e;tip.className='tip err';}
}
async function finish(){
  tip.textContent='正在保存并重启...';tip.className='tip';
  $('btnFinish').disabled=true;
  try{
    if(!adminUrl){
      try{const j=await(await fetch('/info')).json();adminUrl=j.adminUrl&&j.adminUrl!=='-'?j.adminUrl:'';}catch(e){}
    }
    await fetch('/api-base',{method:'POST',body:apiForm()});
    await fetch('/finish',{method:'POST'});
    tip.textContent='设备即将重启';tip.className='tip ok';
    showAdminLink(adminUrl);
  }catch(e){tip.textContent='操作失败：'+e;tip.className='tip err';$('btnFinish').disabled=false;}
}
async function copyUrl(){
  if(!adminUrl){tip.textContent='暂无管理页地址';tip.className='tip err';return;}
  try{
    if(navigator.clipboard&&navigator.clipboard.writeText){
      await navigator.clipboard.writeText(adminUrl);
    }else{
      const ta=document.createElement('textarea');ta.value=adminUrl;document.body.appendChild(ta);ta.select();document.execCommand('copy');document.body.removeChild(ta);
    }
    tip.textContent='已复制：'+adminUrl;tip.className='tip ok';
  }catch(e){tip.textContent='复制失败，请长按链接手动复制';tip.className='tip err';}
}
$('btnSave').onclick=saveApi;$('btnFinish').onclick=finish;
$('btnCopy').onclick=copyUrl;
$('btnOpen').onclick=()=>{if(adminUrl)location.href=adminUrl;};
load();
</script>
</body>
</html>
)HTML";

String normalizeApiBase(String v, bool useDefaultIfEmpty) {
  v.trim();
  while (v.length() > 8 && v.endsWith("/")) v.remove(v.length() - 1);
  if (v.isEmpty() && useDefaultIfEmpty) v = kDefaultApiBase;
  return v;
}

String readApiBaseFromNvs() {
  Preferences p;
  p.begin(kNvsNamespace, true);
  String v = p.getString(kNvsApiBase, "");
  p.end();
  return normalizeApiBase(v, true);
}

String readApiBaseBakFromNvs() {
  Preferences p;
  p.begin(kNvsNamespace, true);
  String v = p.getString(kNvsApiBaseBak, "");
  p.end();
  return normalizeApiBase(v, false);
}

uint8_t readSyncHourFromNvs() {
  Preferences p;
  p.begin(kNvsNamespace, true);
  uint8_t h = p.getUChar(kNvsSyncHour, kDefaultSyncHour);
  p.end();
  return (h <= 23) ? h : kDefaultSyncHour;
}

// 校验并规范化用户输入；ok=false 时 err 为原因。空串表示清除该项。
bool parseApiBaseArg(const String& raw, String& out, String& err) {
  out = raw;
  out.trim();
  if (out.isEmpty()) return true;
  while (out.endsWith("/")) out.remove(out.length() - 1);
  if (!out.startsWith("http://") && !out.startsWith("https://")) {
    err = "地址须以 http:// 或 https:// 开头";
    return false;
  }
  return true;
}

// syncHour 须为 0–23 整点；非法则 err。
bool parseSyncHourArg(const String& raw, uint8_t& out, String& err) {
  if (raw.length() == 0) {
    err = "请选择每日同步时间";
    return false;
  }
  int v = raw.toInt();
  if (v < 0 || v > 23) {
    err = "同步时间须为 0–23 整点";
    return false;
  }
  // "09abc" 这类 toInt 会得到 9，再核对整串是否纯数字
  for (size_t i = 0; i < raw.length(); ++i) {
    if (raw[i] < '0' || raw[i] > '9') {
      err = "同步时间须为 0–23 整点";
      return false;
    }
  }
  out = static_cast<uint8_t>(v);
  return true;
}

void handleRoot() { server.send_P(200, "text/html; charset=utf-8", kIndexHtml); }

void handleDevicePage() { server.send_P(200, "text/html; charset=utf-8", kDeviceHtml); }

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

bool tryConnectSta(const String& ssid, const String& pass, bool preferStatic) {
  if (preferStatic) applySavedStaticIp();

  WiFi.begin(ssid.c_str(), pass.c_str());
  uint32_t start = millis();
  while (millis() - start < kConnectTimeoutMs) {
    if (WiFi.status() == WL_CONNECTED) return true;
    delay(200);
    yield();
  }

  // 静态 IP 失败时回退 DHCP 再试一次
  Preferences p;
  p.begin(kNvsNamespace, true);
  bool hadStatic = preferStatic && p.getString(kNvsStaIp, "").length() > 0;
  p.end();
  if (hadStatic) {
    Serial.println("[wifi_setup] 静态 IP 连接失败，回退 DHCP");
    WiFi.disconnect(false, false);
    delay(200);
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
    WiFi.begin(ssid.c_str(), pass.c_str());
    start = millis();
    while (millis() - start < kConnectTimeoutMs) {
      if (WiFi.status() == WL_CONNECTED) return true;
      delay(200);
      yield();
    }
  }
  return false;
}

void handleSave() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  if (ssid.length() == 0) {
    server.send(400, "text/plain; charset=utf-8", "SSID 不能为空");
    return;
  }

  {
    Preferences p;
    p.begin(kNvsNamespace, false);
    p.putString(kNvsSsid, ssid);
    p.putString(kNvsPass, pass);
    p.end();
  }
  Serial.printf("[wifi_setup] 已保存 SSID=%s，尝试 STA 连接（保持 AP）\n", ssid.c_str());

  // 保持 AP，切 AP+STA 连路由器
  WiFi.mode(WIFI_AP_STA);
  if (!tryConnectSta(ssid, pass, /*preferStatic=*/true)) {
    server.send(500, "text/plain; charset=utf-8",
                "无法连接该 WiFi，请检查名称/密码后重试");
    Serial.println("[wifi_setup] STA 连接失败");
    return;
  }

  saveCurrentStaIp();
  Serial.printf("[wifi_setup] STA 已连接 IP=%s\n", WiFi.localIP().toString().c_str());
  // 先回包再刷屏（全刷 12–20s，否则浏览器会超时）
  server.send(200, "text/plain; charset=utf-8",
              "已连接：" + ssid + "，IP=" + WiFi.localIP().toString());
  showReadyScreen();
}

void handleReset() {
  Preferences p;
  p.begin(kNvsNamespace, false);
  p.remove(kNvsSsid);
  p.remove(kNvsPass);
  p.end();
  // 保留 apiBase / syncHour / 静态 IP，避免重配 WiFi 时丢服务地址与同步时间；出厂重置另议
  server.send(200, "text/plain; charset=utf-8", "已清空 WiFi，设备即将重启...");
  delay(1000);
  ESP.restart();
}

String jsonEscape(const String& s) {
  String o;
  o.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    if (c == '\\' || c == '"') {
      o += '\\';
      o += c;
    } else if (c == '\n') {
      o += "\\n";
    } else {
      o += c;
    }
  }
  return o;
}

void handleInfo() {
  String ip = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : String("-");
  String admin = (ip == "-") ? String("-") : ("http://" + ip + "/device");
  String json = "{";
  json += "\"ssid\":\"" + jsonEscape(WiFi.SSID()) + "\",";
  json += "\"ip\":\"" + jsonEscape(ip) + "\",";
  json += "\"gateway\":\"" + jsonEscape(WiFi.gatewayIP().toString()) + "\",";
  json += "\"mask\":\"" + jsonEscape(WiFi.subnetMask().toString()) + "\",";
  json += "\"mac\":\"" + jsonEscape(WiFi.macAddress()) + "\",";
  json += "\"adminUrl\":\"" + jsonEscape(admin) + "\",";
  json += "\"apiBase\":\"" + jsonEscape(readApiBaseFromNvs()) + "\",";
  json += "\"apiBaseBak\":\"" + jsonEscape(readApiBaseBakFromNvs()) + "\",";
  json += "\"syncHour\":" + String(readSyncHourFromNvs());
  json += "}";
  server.send(200, "application/json", json);
}

void handleApiBase() {
  String primary, backup, err;
  uint8_t hour = kDefaultSyncHour;
  if (!parseApiBaseArg(server.arg("apiBase"), primary, err) ||
      !parseApiBaseArg(server.arg("apiBaseBak"), backup, err) ||
      !parseSyncHourArg(server.arg("syncHour"), hour, err)) {
    server.send(400, "text/plain; charset=utf-8", err);
    return;
  }

  {
    Preferences p;
    p.begin(kNvsNamespace, false);
    if (primary.isEmpty()) {
      p.remove(kNvsApiBase);
    } else {
      p.putString(kNvsApiBase, primary);
    }
    if (backup.isEmpty()) {
      p.remove(kNvsApiBaseBak);
    } else {
      p.putString(kNvsApiBaseBak, backup);
    }
    p.putUChar(kNvsSyncHour, hour);
    p.end();
  }

  char hourLabel[8];
  snprintf(hourLabel, sizeof(hourLabel), "%02u:00", hour);
  String msg = String("主：") + (primary.isEmpty() ? kDefaultApiBase : primary.c_str());
  msg += "；备：";
  msg += backup.isEmpty() ? "(未设置)" : backup;
  msg += "；同步：";
  msg += hourLabel;
  server.send(200, "text/plain; charset=utf-8", msg);
  Serial.printf("[wifi_setup] apiBase=%s apiBaseBak=%s syncHour=%u\n",
                primary.isEmpty() ? kDefaultApiBase : primary.c_str(),
                backup.isEmpty() ? "(none)" : backup.c_str(), hour);
}

void handleFinish() {
  if (WiFi.status() == WL_CONNECTED) saveCurrentStaIp();
  server.send(200, "text/plain; charset=utf-8", "即将重启...");
  Serial.printf("[wifi_setup] 用户完成配置，apiBase=%s，重启\n",
                readApiBaseFromNvs().c_str());
  delay(800);
  ESP.restart();
}

void handleNotFound() {
  if (g_apReady) {
    handleCaptivePortal();
    return;
  }
  // STA 管理页：未知路径转到 /device
  server.sendHeader("Location", "/device", true);
  server.send(302, "text/plain", "");
}

void setupApRadio() {
  WiFi.persistent(false);
  WiFi.setSleep(false);
  esp_wifi_set_ps(WIFI_PS_NONE);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);

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

void registerCommonRoutes() {
  server.on("/device", HTTP_GET, handleDevicePage);
  server.on("/info", HTTP_GET, handleInfo);
  server.on("/api-base", HTTP_POST, handleApiBase);
  server.on("/finish", HTTP_POST, handleFinish);
}

void setupHttpAp() {
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(kDnsPort, "*", kApIp);
  g_dnsOn = true;

  server.on("/", HTTP_GET, handleRoot);
  server.on("/scan", HTTP_GET, handleScan);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/reset", HTTP_POST, handleReset);
  registerCommonRoutes();
  // Captive Portal 探测
  server.on("/generate_204", HTTP_GET, handleCaptiveOk);
  server.on("/gen_204", HTTP_GET, handleCaptiveOk);
  server.on("/hotspot-detect.html", HTTP_GET, handleCaptivePortal);
  server.on("/library/test/success.html", HTTP_GET, handleCaptivePortal);
  server.on("/connecttest.txt", HTTP_GET, handleCaptivePortal);
  server.on("/ncsi.txt", HTTP_GET, handleCaptivePortal);
  server.onNotFound(handleNotFound);
  server.begin();
  g_httpReady = true;
}

void setupHttpSta() {
  registerCommonRoutes();
  server.on("/", HTTP_GET, []() {
    server.sendHeader("Location", "/device", true);
    server.send(302, "text/plain", "");
  });
  server.onNotFound(handleNotFound);
  server.begin();
  g_httpReady = true;
  Serial.printf("[wifi_setup] STA 管理页 http://%s/device\n",
                WiFi.localIP().toString().c_str());
}

}  // namespace

namespace {

// 全刷约 12–20s；连续两次 flush 过近时面板易白屏，强制冷却后再刷。
uint32_t g_lastFlushMs = 0;
constexpr uint32_t kFlushCooldownMs = 2000;

bool flushWithCooldown(const char* tag) {
  uint32_t now = millis();
  if (g_lastFlushMs != 0 && (now - g_lastFlushMs) < kFlushCooldownMs) {
    delay(kFlushCooldownMs - (now - g_lastFlushMs));
  }
  bool ok = epd::flush();
  if (!ok) {
    Serial.printf("[wifi_setup] %s 刷屏超时，重试\n", tag);
    delay(500);
    ok = epd::flush();
  }
  g_lastFlushMs = millis();
  if (!ok) Serial.printf("[wifi_setup] %s 刷屏失败\n", tag);
  return ok;
}

}  // namespace

void showConfigScreen() {
  epd::clear(epd::WHITE);
  epd::drawImageRle(kCfgScreenRle, kCfgScreenRleLen);

  String mac = WiFi.macAddress();
  epd::drawText(CFG_MAC_X, CFG_MAC_Y, mac.c_str(), CFG_MAC_COLOR, CFG_MAC_SCALE);

  Serial.printf("[wifi_setup] 刷出配网画面 MAC=%s\n", mac.c_str());
  flushWithCooldown("配网画面");
}

void showReadyScreen() {
  epd::clear(epd::WHITE);

  const char* title = "DayIJoy Ready";
  String ip = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : String("--");
  String mac = WiFi.macAddress();
  String url = "http://" + ip + "/device";

  // 竖屏简单排版（当前字库仅 ASCII）；用黑字保证对比度
  epd::drawText(40, 80, title, epd::BLACK, 2);
  epd::drawText(40, 160, "LAN IP:", epd::BLACK, 2);
  epd::drawText(40, 200, ip.c_str(), epd::BLACK, 2);
  epd::drawText(40, 280, "MAC:", epd::BLACK, 2);
  epd::drawText(40, 320, mac.c_str(), epd::BLACK, 2);
  epd::drawText(40, 400, "Open browser:", epd::BLACK, 2);
  epd::drawText(40, 440, url.c_str(), epd::BLACK, 1);
  epd::drawText(40, 520, "Set API base,", epd::BLACK, 2);
  epd::drawText(40, 560, "then Finish.", epd::BLACK, 2);

  Serial.printf("[wifi_setup] 就绪画面 IP=%s\n", ip.c_str());
  flushWithCooldown("就绪画面");
}

void showSyncFailScreen() {
  epd::clear(epd::WHITE);
  String base = apiBase();
  epd::drawText(40, 80, "Sync failed", epd::BLACK, 2);
  epd::drawText(40, 160, "Check apiBase:", epd::BLACK, 2);
  // 地址可能较长，分两行粗略截断
  if (base.length() <= 28) {
    epd::drawText(40, 220, base.c_str(), epd::BLACK, 1);
  } else {
    String a = base.substring(0, 28);
    String b = base.substring(28);
    epd::drawText(40, 220, a.c_str(), epd::BLACK, 1);
    epd::drawText(40, 260, b.c_str(), epd::BLACK, 1);
  }
  epd::drawText(40, 340, "Local: no /api", epd::BLACK, 2);
  epd::drawText(40, 400, "Long-press Action", epd::BLACK, 2);
  epd::drawText(40, 460, "to retry sync.", epd::BLACK, 2);
  Serial.printf("[wifi_setup] sync 失败提示 apiBase=%s\n", base.c_str());
  flushWithCooldown("sync失败提示");
}

void applySavedStaticIp() {
  Preferences p;
  if (!p.begin(kNvsNamespace, true)) return;
  String ipS = p.getString(kNvsStaIp, "");
  if (ipS.isEmpty()) {
    p.end();
    return;
  }

  IPAddress ip, gw, mask, dns1;
  if (!ip.fromString(ipS)) {
    p.end();
    return;
  }
  gw.fromString(p.getString(kNvsStaGw, ""));
  if (!mask.fromString(p.getString(kNvsStaMask, "255.255.255.0"))) {
    mask = IPAddress(255, 255, 255, 0);
  }
  if (!dns1.fromString(p.getString(kNvsStaDns, ""))) dns1 = gw;
  p.end();

  if (!WiFi.config(ip, gw, mask, dns1)) {
    Serial.println("[wifi_setup] WiFi.config 静态 IP 失败");
    return;
  }
  Serial.printf("[wifi_setup] 使用固定 IP %s gw=%s\n",
                ip.toString().c_str(), gw.toString().c_str());
}

void saveCurrentStaIp() {
  if (WiFi.status() != WL_CONNECTED) return;
  // 全局 prefs 可能尚未 begin（例如 main 里刚连上），统一用临时写入
  Preferences p;
  if (!p.begin(kNvsNamespace, false)) return;
  p.putString(kNvsStaIp, WiFi.localIP().toString());
  p.putString(kNvsStaGw, WiFi.gatewayIP().toString());
  p.putString(kNvsStaMask, WiFi.subnetMask().toString());
  p.putString(kNvsStaDns, WiFi.dnsIP().toString());
  p.end();
  Serial.printf("[wifi_setup] 已记住局域网 IP=%s\n", WiFi.localIP().toString().c_str());
}

String apiBase() {
  Preferences p;
  p.begin(kNvsNamespace, true);
  String v = p.getString(kNvsApiBase, "");
  p.end();
  return normalizeApiBase(v, true);
}

String apiBaseBackup() {
  Preferences p;
  p.begin(kNvsNamespace, true);
  String v = p.getString(kNvsApiBaseBak, "");
  p.end();
  return normalizeApiBase(v, false);
}

uint8_t syncHour() {
  Preferences p;
  p.begin(kNvsNamespace, true);
  uint8_t h = p.getUChar(kNvsSyncHour, kDefaultSyncHour);
  p.end();
  return (h <= 23) ? h : kDefaultSyncHour;
}

void startAP() {
  setupApRadio();
  setupHttpAp();
  showConfigScreen();
}

void startLocalAdmin() {
  g_apReady = false;
  g_dnsOn = false;
  WiFi.setSleep(false);
  setupHttpSta();
}

void loop() {
  if (!g_httpReady) return;
  if (g_dnsOn) dns.processNextRequest();
  server.handleClient();
}

bool httpActive() { return g_httpReady; }

}  // namespace wifi_setup
