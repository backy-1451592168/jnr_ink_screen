#include "wifi_setup.h"

#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <time.h>

#include "epd.h"
#include "frame_store.h"
#include "ui_config_screen.h"
#include "ui_lan_upload_screen.h"
#include "ui_upload_page.h"

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

// 局域网传图
bool g_lanUploadEnabled = false;
bool g_uploadAddrShown = false;
bool g_lanBusy = false;
volatile bool g_lanCancel = false;
uint8_t* g_uploadBuf = nullptr;
size_t g_uploadPos = 0;
bool g_uploadOverflow = false;
bool g_lanApplyPending = false;

uint32_t crc32Buf(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) {
      crc = (crc & 1) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
    }
  }
  return crc ^ 0xFFFFFFFFu;
}

// ---------- HTML：配网页 ----------
const char kIndexHtml[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1">
<meta name="format-detection" content="telephone=no">
<title>设备配网</title>
<style>
  *{box-sizing:border-box}
  body{margin:0;font-family:-apple-system,BlinkMacSystemFont,"PingFang SC","Microsoft YaHei",sans-serif;background:#f4f6fa;color:#222}
  .wrap{max-width:480px;margin:0 auto;padding:24px 18px}
  h1{font-size:22px;margin:0 0 4px}
  .sub{color:#888;font-size:13px;margin-bottom:18px}
  .card{background:#fff;border-radius:12px;padding:16px;box-shadow:0 2px 12px rgba(0,0,0,.06);margin-bottom:14px}
  label{font-size:13px;color:#555;display:block;margin-bottom:6px}
  input[type="text"],input[type="password"],input[type="search"]{width:100%;padding:11px 12px;border:1px solid #dcdfe6;border-radius:8px;font-size:16px;outline:none;background:#fff;-webkit-user-select:text!important;user-select:text!important;-webkit-appearance:none;appearance:none;touch-action:manipulation}
  input:focus{border-color:#3b82f6}
  #ssid{background:#f5f7fa;color:#333;cursor:default;-webkit-user-select:none;user-select:none}
  .pass-wrap{position:relative}
  .pass-wrap input{padding-right:48px}
  .eye{position:absolute;right:4px;top:50%;transform:translateY(-50%);width:40px;height:40px;border:0;background:transparent;padding:0;margin:0;cursor:pointer;display:flex;align-items:center;justify-content:center}
  .eye svg{width:22px;height:22px;display:block;fill:#2c2c2c}
  .eye .ico-on{display:none}
  .eye.is-show .ico-on{display:block}
  .eye.is-show .ico-off{display:none}
  button{width:100%;padding:12px;background:#3b82f6;color:#fff;border:0;border-radius:8px;font-size:16px;font-weight:500;margin-top:8px;cursor:pointer}
  button:active{background:#2563eb}
  button:disabled{opacity:.6}
  button.ghost{background:#fff;color:#3b82f6;border:1px solid #3b82f6;margin-top:8px}
  .row{display:flex;gap:8px;align-items:center;justify-content:space-between}
  .list{max-height:240px;overflow-y:auto;border:1px solid #eef0f4;border-radius:8px;margin-top:8px;-webkit-overflow-scrolling:touch}
  .item{padding:10px 12px;border-bottom:1px solid #f0f2f6;display:flex;justify-content:space-between;align-items:center;cursor:pointer}
  .item:last-child{border-bottom:0}
  .item.sel{background:#eaf2ff}
  .rssi{font-size:12px;color:#888;flex-shrink:0;margin-left:8px}
  .tip{font-size:12px;color:#888;margin-top:6px;line-height:1.5}
  .tip.ok{color:#16a34a}
  .tip.err,.item.err{color:#dc2626}
  .tip.warn{color:#b45309}
  .footer{text-align:center;color:#aaa;font-size:12px;margin-top:14px}
  .mask{position:fixed;inset:0;background:rgba(0,0,0,.45);display:flex;align-items:center;justify-content:center;z-index:99}
  .mask[hidden]{display:none}
  .mask-box{background:#fff;border-radius:12px;padding:28px 24px;max-width:280px;text-align:center;font-size:15px;line-height:1.5;box-shadow:0 8px 24px rgba(0,0,0,.18)}
  .spin{width:28px;height:28px;margin:0 auto 14px;border:3px solid #e5e7eb;border-top-color:#3b82f6;border-radius:50%;animation:spin 0.8s linear infinite}
  @keyframes spin{to{transform:rotate(360deg)}}
  #setupPanel[hidden],#okPanel[hidden]{display:none!important}
  .ok-title{font-size:18px;font-weight:600;color:#16a34a;margin:0 0 8px}
  .ok-link{display:block;margin:10px 0;padding:10px 12px;background:#f8fafc;border:1px solid #e2e8f0;border-radius:8px;color:#2563eb;word-break:break-all;font-size:14px;text-decoration:none}
</style>
</head>
<body>
<div class="wrap">
  <h1>设备配网</h1>
  <div class="sub" id="sub">点选家里 WiFi，再输入密码。名称不可手改，进页会自动扫描。</div>

  <div id="setupPanel">
    <div class="card">
      <div class="row"><label style="margin:0">附近的 WiFi</label><button type="button" class="ghost" id="btnScan" style="margin:0;width:auto;padding:8px 14px">刷新</button></div>
      <div class="list" id="list"><div class="item">正在扫描附近 WiFi...</div></div>
    </div>
    <div class="card">
      <form id="wifiForm" action="/save" method="POST" autocomplete="off">
        <label for="ssid">WiFi 名称 (SSID)</label>
        <input id="ssid" type="text" name="ssid" readonly tabindex="-1" autocomplete="off" placeholder="请从上方列表点选">
        <label for="pass" style="margin-top:12px">密码</label>
        <div class="pass-wrap">
          <input id="pass" type="password" name="pass" inputmode="text" enterkeyhint="done" autocomplete="current-password" autocapitalize="off" autocorrect="off" spellcheck="false" readonly placeholder="留空表示开放网络">
          <button type="button" class="eye" id="btnEye" aria-label="显示密码">
            <svg class="ico-on" viewBox="0 0 1024 1024" xmlns="http://www.w3.org/2000/svg" aria-hidden="true"><path d="M512 298.666667c-162.133333 0-285.866667 68.266667-375.466667 213.333333 89.6 145.066667 213.333333 213.333333 375.466667 213.333333s285.866667-68.266667 375.466667-213.333333c-89.6-145.066667-213.333333-213.333333-375.466667-213.333333z m0 469.333333c-183.466667 0-328.533333-85.333333-426.666667-256 98.133333-170.666667 243.2-256 426.666667-256s328.533333 85.333333 426.666667 256c-98.133333 170.666667-243.2 256-426.666667 256z m0-170.666667c46.933333 0 85.333333-38.4 85.333333-85.333333s-38.4-85.333333-85.333333-85.333333-85.333333 38.4-85.333333 85.333333 38.4 85.333333 85.333333 85.333333z m0 42.666667c-72.533333 0-128-55.466667-128-128s55.466667-128 128-128 128 55.466667 128 128-55.466667 128-128 128z"></path></svg>
            <svg class="ico-off" viewBox="0 0 1024 1024" xmlns="http://www.w3.org/2000/svg" aria-hidden="true"><path d="M332.8 729.6l34.133333-34.133333c42.666667 12.8 93.866667 21.333333 145.066667 21.333333 162.133333 0 285.866667-68.266667 375.466667-213.333333-46.933333-72.533333-102.4-128-166.4-162.133334l29.866666-29.866666c72.533333 42.666667 132.266667 106.666667 183.466667 192-98.133333 170.666667-243.2 256-426.666667 256-59.733333 4.266667-119.466667-8.533333-174.933333-29.866667z m-115.2-64c-51.2-38.4-93.866667-93.866667-132.266667-157.866667 98.133333-170.666667 243.2-256 426.666667-256 38.4 0 76.8 4.266667 110.933333 12.8l-34.133333 34.133334c-25.6-4.266667-46.933333-4.266667-76.8-4.266667-162.133333 0-285.866667 68.266667-375.466667 213.333333 34.133333 51.2 72.533333 93.866667 115.2 128l-34.133333 29.866667z m230.4-46.933333l29.866667-29.866667c8.533333 4.266667 21.333333 4.266667 29.866666 4.266667 46.933333 0 85.333333-38.4 85.333334-85.333334 0-12.8 0-21.333333-4.266667-29.866666l29.866667-29.866667c12.8 17.066667 17.066667 38.4 17.066666 64 0 72.533333-55.466667 128-128 128-17.066667-4.266667-38.4-12.8-59.733333-21.333333zM384 499.2c4.266667-68.266667 55.466667-119.466667 123.733333-123.733333 0 4.266667-123.733333 123.733333-123.733333 123.733333zM733.866667 213.333333l29.866666 29.866667-512 512-34.133333-29.866667L733.866667 213.333333z"></path></svg>
          </button>
        </div>
        <button type="submit" id="btnSave">保存并连接</button>
      </form>
      <div class="tip" id="tip"></div>
    </div>
    <div class="card"><button type="button" class="ghost" id="btnReset">忘记网络 / 重置</button></div>
  </div>

  <div id="okPanel" hidden>
    <div class="card">
      <div class="ok-title">相框已连上家里 WiFi</div>
      <div class="tip ok" id="okMsg" style="margin:0"></div>
      <div class="tip warn" style="margin-top:12px">请立刻把手机 WiFi 切回原来的家里网络（离开「DayIJoy-心选日」热点）。</div>
      <div class="tip warn">切网后本页可能即将关闭或无法刷新，属正常现象。请先复制下方地址，再用浏览器打开继续设置。</div>
      <a class="ok-link" id="adminLink" href="#" target="_blank" rel="noopener"></a>
      <button type="button" id="btnCopy">复制管理页地址</button>
      <button type="button" id="btnOpenLan">我已切回家里 WiFi，打开管理页</button>
      <button type="button" class="ghost" id="btnStayAp">仍连热点，继续设置</button>
      <div class="tip" id="okTip"></div>
    </div>
  </div>

  <div class="footer">DayIJoy · 心选日 · 设备配网</div>
</div>
<div class="mask" id="mask" hidden>
  <div class="mask-box"><div class="spin"></div><div id="maskText">请等待 WiFi 连接中...</div></div>
</div>
<script>
const $=id=>document.getElementById(id);
const list=$('list'),tip=$('tip'),mask=$('mask');
let adminUrl='';
function bars(r){if(r>=-55)return'●●●●';if(r>=-65)return'●●●○';if(r>=-75)return'●●○○';if(r>=-85)return'●○○○';return'○○○○';}
function setLoading(on,text){
  mask.hidden=!on;
  if(text)$('maskText').textContent=text;
  $('btnSave').disabled=!!on;
  $('btnScan').disabled=!!on;
}
function unlockPass(){
  const p=$('pass');
  p.removeAttribute('readonly');
}
$('pass').addEventListener('touchstart',unlockPass,{passive:true});
$('pass').addEventListener('focus',unlockPass);
async function scan(){
  list.innerHTML='<div class="item">扫描中...</div>';
  try{
    const j=await(await fetch('/scan')).json();
    if(!j.length){list.innerHTML='<div class="item">未发现可用 WiFi，请点「刷新」重试</div>';return;}
    list.innerHTML='';
    j.forEach(n=>{
      if(!n.ssid)return;
      const div=document.createElement('div');div.className='item';
      const name=document.createElement('span');name.textContent=n.ssid+(n.enc?' 🔒':'');
      const rssi=document.createElement('span');rssi.className='rssi';rssi.textContent=bars(n.rssi)+' '+n.rssi+'dBm';
      div.appendChild(name);div.appendChild(rssi);
      div.onclick=()=>{
        document.querySelectorAll('#list .item').forEach(i=>i.classList.remove('sel'));
        div.classList.add('sel');
        $('ssid').value=n.ssid;
        unlockPass();
        $('pass').focus();
      };
      list.appendChild(div);
    });
    if(!list.children.length){list.innerHTML='<div class="item">未发现可用 WiFi，请点「刷新」重试</div>';}
  }catch(e){list.innerHTML='<div class="item err">扫描失败，请点「刷新」重试</div>';}
}
function showConnected(j){
  adminUrl=j.adminUrl||(j.ip?('http://'+j.ip+'/device'):'');
  $('setupPanel').hidden=true;
  $('okPanel').hidden=false;
  $('sub').textContent='配网成功，请按提示切回家里 WiFi';
  $('okMsg').textContent='已连接：'+(j.ssid||'')+'，IP='+(j.ip||'-');
  const a=$('adminLink');
  a.href=adminUrl||'#';
  a.textContent=adminUrl||'(无局域网地址)';
}
async function save(ev){
  if(ev)ev.preventDefault();
  unlockPass();
  const ssid=$('ssid').value.trim();
  if(!ssid){tip.textContent='请先在上方列表点选 WiFi';tip.className='tip err';return;}
  tip.textContent='';tip.className='tip';
  setLoading(true,'请等待 WiFi 连接中...');
  try{
    const fd=new FormData();fd.append('ssid',ssid);fd.append('pass',$('pass').value);
    const r=await fetch('/save',{method:'POST',body:fd});
    const ct=r.headers.get('content-type')||'';
    let j=null,t='';
    if(ct.indexOf('json')>=0){j=await r.json();}else{t=await r.text();}
    setLoading(false);
    if(!r.ok){tip.textContent=(j&&j.message)||t||'连接失败';tip.className='tip err';return;}
    if(!j){
      const m=t.match(/IP=([\d.]+)/);
      j={ssid:ssid,ip:m?m[1]:'',adminUrl:m?('http://'+m[1]+'/device'):''};
    }
    showConnected(j);
  }catch(e){setLoading(false);tip.textContent='提交失败，请确认仍连着设备热点后重试';tip.className='tip err';}
}
async function reset(){
  if(!confirm('确定清空已保存的 WiFi 配置吗？'))return;
  try{
    await fetch('/reset',{method:'POST'});
    tip.textContent='已重置，设备即将重启...';tip.className='tip ok';
  }catch(e){tip.textContent='重置失败：'+e;tip.className='tip err';}
}
async function copyUrl(){
  if(!adminUrl){$('okTip').textContent='暂无管理页地址';$('okTip').className='tip err';return;}
  try{
    if(navigator.clipboard&&navigator.clipboard.writeText){await navigator.clipboard.writeText(adminUrl);}
    else{const ta=document.createElement('textarea');ta.value=adminUrl;document.body.appendChild(ta);ta.select();document.execCommand('copy');document.body.removeChild(ta);}
    $('okTip').textContent='已复制：'+adminUrl;$('okTip').className='tip ok';
  }catch(e){$('okTip').textContent='复制失败，请长按上方蓝色地址手动复制';$('okTip').className='tip err';}
}
$('btnEye').onclick=()=>{
  unlockPass();
  const p=$('pass');const show=p.type==='password';
  p.type=show?'text':'password';
  $('btnEye').classList.toggle('is-show',show);
  $('btnEye').setAttribute('aria-label',show?'隐藏密码':'显示密码');
};
$('wifiForm').onsubmit=save;
$('btnScan').onclick=scan;$('btnReset').onclick=reset;
$('btnCopy').onclick=copyUrl;
$('btnOpenLan').onclick=()=>{if(adminUrl)location.href=adminUrl;};
$('btnStayAp').onclick=()=>{location.href='/device';};
scan();
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
  .banner{display:none;margin-bottom:14px;padding:12px;background:#fffbeb;border:1px solid #fcd34d;border-radius:10px;font-size:13px;line-height:1.55;color:#92400e}
  .banner.show{display:block}
</style>
</head>
<body>
<div class="wrap">
  <h1>设备信息</h1>
  <div class="sub">查看本机网络信息，并设置小程序服务地址</div>
  <div class="banner" id="apBanner">你仍连着设备热点。设置完后请点「完成并重启」，再把手机 WiFi 切回家里网络；热点断开后本页会失联，属正常。</div>
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
      <div class="tip ok" style="margin:0 0 8px">设备即将重启，热点会关闭，本页可能马上失联。请先把手机切回家里 WiFi，再用下方地址打开管理页：</div>
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
function alreadyOnAdmin(url){
  if(!url)return false;
  try{
    const u=new URL(url,location.href);
    return u.hostname===location.hostname && location.hostname!=='192.168.8.1';
  }catch(e){return false;}
}
function showAdminLink(url){
  adminUrl=url||'';
  if(alreadyOnAdmin(adminUrl))return;
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
if(location.hostname==='192.168.8.1'){$('apBanner').classList.add('show');}
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

void handleCaptivePortal() {
  // 一律跳配网页：勿对 generate_204 回 204，否则 Android 会以为已联网并关掉强制门户，输入框失效
  server.sendHeader("Location", "http://" + kApIp.toString() + "/", true);
  server.send(302, "text/plain", "");
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
  String ip = WiFi.localIP().toString();
  Serial.printf("[wifi_setup] STA 已连接 IP=%s\n", ip.c_str());
  // JSON 回包：前端提示切回家里 WiFi；先回包再刷屏（全刷 12–20s）
  String body = "{\"ok\":true,\"ssid\":\"" + jsonEscape(ssid) + "\",\"ip\":\"" +
                jsonEscape(ip) + "\",\"adminUrl\":\"http://" + ip + "/device\"}";
  server.send(200, "application/json; charset=utf-8", body);
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
  json += "\"syncHour\":" + String(readSyncHourFromNvs()) + ",";
  json += "\"screenWidth\":" + String(frame_store::screenWidth()) + ",";
  json += "\"screenHeight\":" + String(frame_store::screenHeight());
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

void handleUploadGet() {
  if (!g_lanUploadEnabled) {
    server.send(404, "text/plain; charset=utf-8", "请先双击模式键切到局域网传图（青灯）");
    return;
  }
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html; charset=utf-8", kUploadPage);
}

void handleUploadWrite() {
  HTTPUpload& u = server.upload();
  if (u.status == UPLOAD_FILE_START) {
    g_uploadPos = 0;
    g_uploadOverflow = false;
    g_lanCancel = false;
    const size_t need = epd::frameBytes();
    if (!g_uploadBuf) {
      g_uploadBuf = (uint8_t*)ps_malloc(need);
    }
    if (!g_uploadBuf) {
      Serial.println("[wifi_setup] upload PSRAM 分配失败");
      g_uploadOverflow = true;
    }
    g_lanBusy = true;
    Serial.printf("[wifi_setup] upload start name=%s\n", u.filename.c_str());
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (g_lanCancel) {
      g_uploadOverflow = true;
      return;
    }
    if (!g_uploadBuf || g_uploadOverflow) return;
    if (g_uploadPos + u.currentSize > epd::frameBytes()) {
      g_uploadOverflow = true;
      return;
    }
    memcpy(g_uploadBuf + g_uploadPos, u.buf, u.currentSize);
    g_uploadPos += u.currentSize;
  } else if (u.status == UPLOAD_FILE_END) {
    Serial.printf("[wifi_setup] upload end bytes=%u\n", (unsigned)g_uploadPos);
  }
}

void handleUploadPost() {
  if (!g_lanUploadEnabled) {
    g_lanBusy = false;
    server.send(403, "text/plain; charset=utf-8", "局域网传图未启用");
    return;
  }
  if (g_lanCancel) {
    g_lanBusy = false;
    server.send(499, "text/plain; charset=utf-8", "已取消");
    return;
  }
  if (g_uploadOverflow || !g_uploadBuf || g_uploadPos != epd::frameBytes()) {
    g_lanBusy = false;
    server.send(400, "text/plain; charset=utf-8",
                "帧长度须为 192000 字节（480x800 或 800x480 六色 packE6）");
    return;
  }

  // 上传页用 query 传 screenWidth/screenHeight（multipart 附加字段在 ESP 上不可靠）
  int w = frame_store::screenWidth();
  int h = frame_store::screenHeight();
  if (server.hasArg("screenWidth") && server.hasArg("screenHeight")) {
    w = server.arg("screenWidth").toInt();
    h = server.arg("screenHeight").toInt();
  }
  if (!frame_store::setScreenSize(w, h)) {
    g_lanBusy = false;
    server.send(400, "text/plain; charset=utf-8",
                "不支持的分辨率（仅 480x800 或 800x480）");
    return;
  }

  g_lanApplyPending = true;
  // 先回包，避免刷屏 10–20s 导致浏览器超时；主循环 pollLanUploadApply 真正刷屏
  server.send(200, "text/plain; charset=utf-8", "上传成功，正在刷屏");
}

void handleRootSta() {
  if (g_lanUploadEnabled) {
    server.sendHeader("Location", "/upload", true);
  } else {
    server.sendHeader("Location", "/device", true);
  }
  server.send(302, "text/plain", "");
}

void handleNotFound() {
  if (g_apReady) {
    handleCaptivePortal();
    return;
  }
  if (g_lanUploadEnabled) {
    server.sendHeader("Location", "/upload", true);
  } else {
    server.sendHeader("Location", "/device", true);
  }
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
  // Captive Portal 探测：全部跳配网页，避免系统误判「已联网」关掉门户
  server.on("/generate_204", HTTP_GET, handleCaptivePortal);
  server.on("/gen_204", HTTP_GET, handleCaptivePortal);
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
  server.on("/", HTTP_GET, handleRootSta);
  server.on("/upload", HTTP_GET, handleUploadGet);
  server.on("/upload", HTTP_POST, handleUploadPost, handleUploadWrite);
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
  // 配网 RLE 点阵固定竖屏 480×800，临时切逻辑分辨率（不改 NVS）
  epd::setLogicalSize(epd::kPortraitW, epd::kPortraitH);
  epd::clear(epd::WHITE);
  epd::drawImageRle(kCfgScreenRle, kCfgScreenRleLen);

  String mac = WiFi.macAddress();
  epd::drawText(CFG_MAC_X, CFG_MAC_Y, mac.c_str(), CFG_MAC_COLOR, CFG_MAC_SCALE);

  Serial.printf("[wifi_setup] 开始刷配网画面 MAC=%s（全刷约 12–20s）\n", mac.c_str());
  bool ok = flushWithCooldown("配网画面");
  Serial.printf("[wifi_setup] 配网画面刷屏结束 ok=%d\n", (int)ok);
}

void showReadyScreen() {
  // 就绪 ASCII 排版按竖屏坐标写的；临时竖屏刷，避免横屏模式下 y>480 被裁切
  epd::setLogicalSize(epd::kPortraitW, epd::kPortraitH);
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
  // 恢复用户屏参，供后续 sync / 局域网帧按正确方向刷
  frame_store::applyStoredScreenSize();
}

void showSyncFailScreen() {
  epd::setLogicalSize(epd::kPortraitW, epd::kPortraitH);
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
  frame_store::applyStoredScreenSize();
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

void setLanUploadEnabled(bool enabled) {
  g_lanUploadEnabled = enabled;
  if (!enabled) {
    g_uploadAddrShown = false;
    g_lanApplyPending = false;
  }
  Serial.printf("[wifi_setup] lanUpload=%d\n", (int)enabled);
}

bool lanUploadEnabled() { return g_lanUploadEnabled; }

bool uploadAddressVisible() { return g_uploadAddrShown; }

bool showUploadAddressScreen() {
  // 横屏静态中文 RLE + 运行时叠画完整 URL（避免 24px 点阵放大缺笔）
  epd::setLogicalSize(epd::kLandscapeW, epd::kLandscapeH);
  epd::clear(epd::YELLOW);
  epd::drawImageRle(kLanUploadScreenRle, kLanUploadScreenRleLen);

  String ip = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : String("--");
  String url = "http://" + ip + "/upload";
  epd::drawText(LAN_URL_X, LAN_URL_Y, url.c_str(), LAN_URL_COLOR, LAN_URL_SCALE);

  bool ok = flushWithCooldown("传图地址");
  frame_store::applyStoredScreenSize();
  g_uploadAddrShown = true;
  Serial.printf("[wifi_setup] 传图地址屏 %s ok=%d (横屏RLE)\n", url.c_str(), (int)ok);
  return ok;
}

bool toggleUploadAddressScreen() {
  if (g_uploadAddrShown) {
    g_uploadAddrShown = false;
    return false;  // 调用方恢复 /last.bin
  }
  return showUploadAddressScreen();
}

bool lanBusy() { return g_lanBusy || g_lanApplyPending; }

void requestLanCancel() { g_lanCancel = true; }

int pollLanUploadApply(LanActivityHook hook) {
  if (!g_lanApplyPending || !g_uploadBuf) return 0;
  g_lanApplyPending = false;
  g_lanBusy = true;
  g_lanCancel = false;

  const size_t len = epd::frameBytes();
  if (hook) hook();
  bool ok = epd::drawImageRaw(g_uploadBuf, len);
  if (ok && !g_lanCancel) {
    if (hook) {
      epd::setBusyPollHook(hook);
    }
    ok = flushWithCooldown("局域网传图");
    epd::setBusyPollHook(nullptr);
  }
  if (ok && !g_lanCancel) {
    uint32_t crc = crc32Buf(g_uploadBuf, len);
    frame_store::saveLastFrame(g_uploadBuf, len, crc);
    // 置 0，切回小程序模式长按 sync 时可重新拉正式帧
    frame_store::setContentVersion(0);
    uint32_t now = (uint32_t)time(nullptr);
    if (now < 1700000000) now = millis() / 1000;
    frame_store::setLastRefreshTs(now);
    g_uploadAddrShown = false;
    Serial.printf("[wifi_setup] 局域网帧已刷屏 crc=%08x\n", crc);
    g_lanBusy = false;
    return 1;
  }
  Serial.println("[wifi_setup] 局域网刷屏失败或取消");
  g_lanBusy = false;
  return -1;
}

void loop() {
  if (!g_httpReady) return;
  if (g_dnsOn) dns.processNextRequest();
  server.handleClient();
}

bool httpActive() { return g_httpReady; }

}  // namespace wifi_setup
