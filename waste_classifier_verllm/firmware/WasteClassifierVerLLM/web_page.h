#pragma once

const char PAGE[] PROGMEM = R"PAGE(<!doctype html>
<html lang="vi">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#e6efe4">
<title>Phân loại rác</title>
<style>
:root{--bg:#e6efe4;--ink:#17241c;--muted:#5c6b62;--card:#fffdf8;--line:#d5e2d6;--green:#0e6b52;--green-d:#0a5340;--shadow:0 12px 32px rgba(23,36,28,.08)}
*{box-sizing:border-box}
body{margin:0;min-height:100vh;color:var(--ink);background:radial-gradient(900px 320px at 50% -8%,#f8fbf6,transparent 55%),var(--bg);font:16px/1.45 "Segoe UI",system-ui,sans-serif}
.app{max-width:440px;margin:0 auto;padding:18px 16px 32px}
header{display:flex;align-items:flex-end;justify-content:space-between;gap:12px;margin-bottom:14px}
.kicker{margin:0;color:var(--green);font-size:12px;font-weight:700;letter-spacing:.08em;text-transform:uppercase}
h1{margin:2px 0 0;font-size:28px;letter-spacing:-.03em}
.modes{display:flex;gap:4px;margin:0 0 14px;padding:4px;background:var(--card);border:1px solid var(--line);border-radius:14px;box-shadow:var(--shadow)}
.modes button{flex:1;border:0;background:transparent;color:var(--muted);border-radius:10px;padding:10px;font:inherit;font-weight:700;cursor:pointer}
.modes button.on{background:var(--green);color:#fff}
.tools{display:flex;align-items:center;gap:8px}
#server{width:42px;height:42px;border-radius:999px;border:1px solid var(--line);background:var(--card);color:#b7c4bb;display:grid;place-items:center;box-shadow:var(--shadow)}
#server.on{color:#0e6b52;background:#e7f6ee;border-color:#b7e0cb}
#wifi-btn{display:flex;align-items:center;gap:8px;border:1px solid var(--line);background:var(--card);color:var(--ink);border-radius:999px;padding:10px 14px;font:inherit;font-weight:700;cursor:pointer;box-shadow:var(--shadow)}
#dot{width:9px;height:9px;border-radius:50%;background:#c45b4a;display:block}
#dot.on{background:#1f9d62}
.frame{position:relative;background:#101613;border-radius:22px;overflow:hidden;aspect-ratio:4/3;box-shadow:var(--shadow)}
img{width:100%;height:100%;object-fit:contain;display:block;background:#101613}
#badge{position:absolute;left:12px;top:12px;margin:0;background:rgba(255,253,248,.94);border-radius:999px;padding:6px 10px;font-size:13px;font-weight:700}
#status{margin:12px 2px 0;color:var(--muted);min-height:1.45em}
.bins{display:flex;gap:8px;margin:12px 0 0;padding:0;list-style:none}
.bins li{flex:1;text-align:center;background:var(--card);border:1px solid var(--line);border-radius:999px;padding:6px 4px;font-size:13px;font-weight:700}
#result{display:flex;gap:14px;align-items:center;margin-top:14px;padding:14px 14px 12px;background:var(--card);border:1px solid var(--line);border-left:6px solid var(--green);border-radius:18px;box-shadow:var(--shadow)}
#result[hidden]{display:none}
.mark{width:56px;height:56px;border-radius:16px;display:grid;place-items:center;color:#fff;font-weight:800;font-size:22px;flex:none}
#result[data-kind="plastic"]{border-left-color:#1d6fbf}#result[data-kind="plastic"] .mark{background:#1d6fbf}
#result[data-kind="metal"]{border-left-color:#c47b12}#result[data-kind="metal"] .mark{background:#c47b12}
#result[data-kind="paper_cardboard"]{border-left-color:#8a5a2b}#result[data-kind="paper_cardboard"] .mark{background:#8a5a2b}
.eyebrow{margin:0;color:var(--muted);font-size:13px}
#label{margin:0;font-size:30px;letter-spacing:-.03em}
.actions{display:flex;flex-direction:column;gap:4px;margin-top:14px}
.actions[hidden]{display:none}
button.primary{border:0;border-radius:16px;background:var(--green);color:#fff;padding:16px;font:inherit;font-size:18px;font-weight:700;cursor:pointer}
button.primary:active{background:var(--green-d)}
button.primary:disabled{background:#8aa399;cursor:default}
button.ghost{border:0;background:transparent;color:var(--green-d);font:inherit;font-weight:700;padding:10px;cursor:pointer}
button.stop{border:1px solid #e3b2ab;background:#fff;color:#9a3b2f;border-radius:16px;padding:14px;font:inherit;font-size:17px;font-weight:700;cursor:pointer}
.sheet{position:fixed;inset:0;background:rgba(18,28,22,.46);display:none;align-items:flex-end;justify-content:center;z-index:5}
.sheet.open{display:flex}
.panel{width:min(440px,100%);max-height:min(92vh,760px);overflow:auto;background:var(--card);border-radius:24px 24px 0 0;padding:16px 16px 28px}
.panel h2{margin:0 0 4px;font-size:22px}
.panel p{margin:8px 0;color:var(--muted)}
.top{display:flex;justify-content:space-between;align-items:center;gap:8px}
.close{border:0;background:#eef3ec;border-radius:999px;padding:8px 12px;font:inherit;font-weight:700;cursor:pointer}
label{display:block;margin:14px 0 6px;font-weight:700}
input,select{width:100%;border:1px solid #c9d5cb;border-radius:12px;padding:12px;font:inherit;background:#fff;color:var(--ink)}
.row{display:flex;gap:8px}
.row input{flex:1}
button.save,button.small{border:0;border-radius:12px;background:var(--green);color:#fff;font:inherit;font-weight:700;cursor:pointer}
button.save{width:100%;margin-top:14px;padding:14px;font-size:17px}
button.small{padding:0 14px}
ul{list-style:none;padding:0;margin:8px 0 0}
li button{width:100%;display:flex;justify-content:space-between;align-items:center;gap:10px;text-align:left;margin:0 0 8px;padding:12px;border-radius:14px;border:1px solid var(--line);background:#fff;font:inherit;cursor:pointer}
li b{font-weight:700}
li span{color:var(--muted);font-size:13px;white-space:nowrap}
</style>
</head>
<body>
<div class="app">
<header>
  <div><p class="kicker">Camera</p><h1>Phân loại rác</h1></div>
  <div class="tools">
    <span id="server" aria-label="Server"><svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" stroke-width="2"><rect x="3" y="3" width="18" height="7" rx="2"></rect><rect x="3" y="14" width="18" height="7" rx="2"></rect><path d="M7 6.5h.01M7 17.5h.01" stroke-linecap="round"></path></svg></span>
    <button id="wifi-btn" type="button"><i id="dot"></i>Wi-Fi</button>
  </div>
</header>
<div class="modes">
  <button id="mode-manual" class="on" type="button">Thủ công</button>
  <button id="mode-auto" type="button">Tự động</button>
</div>
<div class="frame"><img id="view" alt="Hình từ camera"><p id="badge" hidden>Ảnh vừa chụp</p></div>
<p id="status">Đang kết nối tới server</p>
<ul class="bins"><li>Nhựa</li><li>Kim loại</li><li>Giấy</li></ul>
<article id="result" hidden>
  <div class="mark" id="mark">?</div>
  <div style="flex:1">
    <p class="eyebrow">Cho vào ngăn</p>
    <h2 id="label">—</h2>
  </div>
</article>
<div id="manual-actions" class="actions">
  <button id="snap" class="primary" type="button" disabled>Chụp và phân loại</button>
  <button id="live" class="ghost" type="button" hidden>Xem hình trực tiếp</button>
</div>
<div id="auto-actions" class="actions" hidden>
  <button id="arm" class="primary" type="button" disabled>Sẵn sàng, bắt đầu</button>
  <button id="stop-auto" class="stop" type="button" hidden>Dừng</button>
</div>
</div>
<div id="sheet" class="sheet">
  <div class="panel">
    <div class="top"><h2>Cài đặt Wi-Fi</h2><button id="close" class="close" type="button">Đóng</button></div>
    <p id="wifi-state">Đang kiểm tra mạng...</p>
    <form method="POST" action="/wifi/save">
      <label for="ssid">Tên mạng</label>
      <input id="ssid" name="ssid" maxlength="32" required autocomplete="off" placeholder="Tên Wi-Fi 2.4 GHz">
      <label for="password">Mật khẩu</label>
      <div class="row">
        <input id="password" name="password" type="password" maxlength="63" autocomplete="off" placeholder="Để trống nếu mạng không mật khẩu">
        <button id="peek" class="small" type="button">Hiện</button>
      </div>
      <label for="channel">Kênh Wi-Fi</label>
      <select id="channel" name="channel">
        <option value="0">Tự động theo mạng (dùng cho hotspot điện thoại)</option>
        <option value="1">1</option><option value="2">2</option><option value="3">3</option>
        <option value="4">4</option><option value="5">5</option><option value="6">6</option>
        <option value="7">7</option><option value="8">8</option><option value="9">9</option>
        <option value="10">10</option><option value="11">11</option><option value="12">12</option>
        <option value="13">13</option>
      </select>
      <button class="save" type="submit">Lưu và kết nối</button>
    </form>
    <p>Máy chỉ vào được mạng 2.4 GHz. Chọn kênh cố định thì mạng phải ở đúng kênh đó, nếu không máy sẽ bỏ mạng và chỉ phát hotspot. Sau khi lưu, máy sẽ khởi động lại.</p>
    <h2>Mạng tìm thấy</h2>
    <ul id="nets"><li>Đang chờ...</li></ul>
  </div>
</div>
<script>
const NAMES={plastic:["Nhựa","N"],metal:["Kim loại","K"],paper_cardboard:["Giấy","G"]};
const view=document.getElementById("view"),status=document.getElementById("status"),result=document.getElementById("result");
const snap=document.getElementById("snap"),liveBtn=document.getElementById("live"),sheet=document.getElementById("sheet");
const badge=document.getElementById("badge"),arm=document.getElementById("arm"),stopAutoBtn=document.getElementById("stop-auto");
let live=true,hold=false,busy=false,scanning=false,mode="manual",autoToken=0,deviceReady=false,linkChecking=true;
let captureIntervalSeconds=30,seenAttempt=-1;
function tick(){if(live&&!hold)view.src="/capture?t="+Date.now();}
view.onload=function(){if(live&&!hold)setTimeout(tick,140);};
view.onerror=function(){setTimeout(tick,700);};
tick();
function sleep(ms){return new Promise(function(r){setTimeout(r,ms);});}
function showResult(data){
  const name=NAMES[data.label]||["Không rõ","?"];
  result.hidden=false;
  result.dataset.kind=NAMES[data.label]?data.label:"";
  document.getElementById("mark").textContent=name[1];
  document.getElementById("label").textContent=name[0];
}
async function classifyOnce(){
  hold=true;
  try{
    const r=await fetch("/classify",{method:"POST",cache:"no-store"});
    const attempt=parseInt(r.headers.get("X-Attempt-Id"),10);
    if(!isNaN(attempt))seenAttempt=attempt;
    if(!r.ok){
      const error=new Error(r.status===429?"Đang chờ hạn mức API. Thử lại sau "+(r.headers.get("Retry-After")||30)+" giây.":"Không phân loại được. Kiểm tra backend.");
      error.rateLimited=r.status===429;
      throw error;
    }
    return await r.json();
  }finally{hold=false;if(live)tick();}
}
async function waitSeconds(token,seconds,label){
  for(let s=seconds;s>=1;s--){
    if(token!==autoToken)return false;
    status.textContent=label(s);
    await sleep(1000);
  }
  return token===autoToken;
}
function stopAuto(message){
  autoToken=0;
  arm.hidden=false;
  stopAutoBtn.hidden=true;
  if(mode==="auto"&&deviceReady)status.textContent=message||"Đã dừng. Bấm sẵn sàng khi muốn bắt đầu lại.";
}
async function autoLoop(token){
  while(token===autoToken){
    status.textContent="Đang chụp...";
    let shotOk=false;
    try{showResult(await classifyOnce());shotOk=true;}
    catch(e){if(token!==autoToken)return;if(e.rateLimited){stopAuto(e.message);return;}}
    if(token!==autoToken)return;
    const gap=Math.max(30,captureIntervalSeconds);
    const waitLabel=shotOk?function(s){return "Lần chụp tiếp theo sau "+(s+3)+" giây.";}
      :function(s){return "Không chụp được. Lần chụp tiếp theo sau "+(s+3)+" giây.";}
    if(!(await waitSeconds(token,gap-3,waitLabel)))return;
    if(!(await waitSeconds(token,3,function(s){return "Sẵn sàng chụp lại sau "+s+" giây.";})))return;
  }
}
snap.onclick=async function(){
  if(busy||mode!=="manual")return;
  busy=true;live=false;snap.disabled=true;badge.hidden=true;
  status.textContent="Đang chụp...";
  try{
    const data=await classifyOnce();
    if(data.capture_id)view.src="/last.jpg?id="+data.capture_id+"&t="+Date.now();
    showResult(data);
    status.textContent="Đã xong.";
    badge.hidden=false;
    liveBtn.hidden=false;
    snap.textContent="Chụp tiếp";
  }catch(e){
    status.textContent="Không chụp được. Thử lại.";
    live=true;badge.hidden=true;tick();
  }finally{snap.disabled=false;busy=false;}
};
liveBtn.onclick=function(){live=true;badge.hidden=true;liveBtn.hidden=true;status.textContent="Đang xem camera.";tick();};
function setMode(next){
  if(next===mode)return;
  if(mode==="auto")stopAuto();
  mode=next;
  document.getElementById("manual-actions").hidden=mode!=="manual";
  document.getElementById("auto-actions").hidden=mode!=="auto";
  document.getElementById("mode-manual").classList.toggle("on",mode==="manual");
  document.getElementById("mode-auto").classList.toggle("on",mode==="auto");
  if(mode==="auto"){
    live=true;badge.hidden=true;liveBtn.hidden=true;tick();
  }
  if(linkChecking)status.textContent="Đang kết nối tới server";
  else if(mode==="auto"&&deviceReady)status.textContent="Đang xem camera. Bấm sẵn sàng khi muốn bắt đầu.";
  else if(deviceReady&&live)status.textContent="Đưa vật vào giữa khung, rồi chụp.";
}
document.getElementById("mode-manual").onclick=function(){setMode("manual");};
document.getElementById("mode-auto").onclick=function(){setMode("auto");};
arm.onclick=function(){
  if(arm.disabled||autoToken)return;
  autoToken=1;
  arm.hidden=true;
  stopAutoBtn.hidden=false;
  autoLoop(1);
};
stopAutoBtn.onclick=function(){stopAuto();};
async function refreshWifi(){
  try{
    const s=await(await fetch("/wifi/status",{cache:"no-store"})).json();
    document.getElementById("dot").classList.toggle("on",!!s.connected);
    const state=document.getElementById("wifi-state");
    const field=document.getElementById("ssid");
    if(!field.value)field.value=s.connected?s.ssid:(s.saved||"");
    if(typeof s.chan_setting==="number")document.getElementById("channel").value=String(s.chan_setting);
    if(s.connected)state.textContent="Đang dùng mạng "+s.ssid+" (kênh "+s.channel+"). Mở camera ở địa chỉ "+s.ip;
    else if(s.saved)state.textContent="Chưa vào được mạng "+s.saved+". Trang này vẫn mở qua hotspot WasteCamera.";
    else state.textContent="Chưa lưu mạng nhà. Bạn đang mở trang qua hotspot WasteCamera.";
  }catch(e){}
}
async function scan(){
  if(scanning)return;
  scanning=true;
  const list=document.getElementById("nets");
  list.textContent="";
  const waiting=document.createElement("li");
  waiting.textContent="Đang tìm mạng...";
  list.appendChild(waiting);
  try{
    const nets=await(await fetch("/wifi/networks",{cache:"no-store"})).json();
    const seen=new Map();
    nets.forEach(function(n){if(n.ssid&&(!seen.has(n.ssid)||n.rssi>seen.get(n.ssid).rssi))seen.set(n.ssid,n);});
    const rows=Array.from(seen.values()).sort(function(a,b){return b.rssi-a.rssi;});
    list.textContent="";
    if(!rows.length){const li=document.createElement("li");li.textContent="Không thấy mạng 2.4 GHz nào.";list.appendChild(li);return;}
    rows.forEach(function(n){
      const li=document.createElement("li"),b=document.createElement("button"),name=document.createElement("b"),meta=document.createElement("span");
      b.type="button";
      name.textContent=n.ssid;
      const level=n.rssi>=-60?"mạnh":n.rssi>=-75?"vừa":"yếu";
      meta.textContent=(n.open?"không mật khẩu":"có mật khẩu")+" · "+level+(n.ch?" · kênh "+n.ch:"");
      b.appendChild(name);b.appendChild(meta);
      b.onclick=function(){document.getElementById("ssid").value=n.ssid;document.getElementById("password").focus();};
      li.appendChild(b);list.appendChild(li);
    });
  }catch(e){list.textContent="";const li=document.createElement("li");li.textContent="Không tìm được mạng. Đóng rồi mở lại.";list.appendChild(li);}
  finally{scanning=false;}
}
function openSheet(){sheet.classList.add("open");document.body.style.overflow="hidden";refreshWifi();scan();}
function closeSheet(){sheet.classList.remove("open");document.body.style.overflow="";}
document.getElementById("wifi-btn").onclick=openSheet;
document.getElementById("close").onclick=closeSheet;
sheet.onclick=function(e){if(e.target===sheet)closeSheet();};
document.getElementById("peek").onclick=function(){
  const p=document.getElementById("password"),show=p.type==="password";
  p.type=show?"text":"password";
  this.textContent=show?"Ẩn":"Hiện";
};
// Show captures started outside this page (physical button, firmware auto mode).
async function syncExternal(h){
  if(typeof h.attempt_id!=="number")return;
  if(seenAttempt<0){seenAttempt=h.attempt_id;return;}
  if(busy||hold||h.attempt_id===seenAttempt)return;
  seenAttempt=h.attempt_id;
  const who=h.last_trigger==="button"?" bằng nút":"";
  if(!h.last_ok){if(!autoToken)status.textContent="Chụp"+who+" không được. Thử lại.";return;}
  try{
    const r=await fetch("/latest",{cache:"no-store"});
    if(r.status!==200||busy||hold)return;
    const data=await r.json();
    showResult(data);
    if(mode==="manual"){
      live=false;
      if(data.capture_id)view.src="/last.jpg?id="+data.capture_id+"&t="+Date.now();
      badge.hidden=false;liveBtn.hidden=false;snap.textContent="Chụp tiếp";
      status.textContent="Đã xong (chụp"+who+").";
    }
  }catch(e){}
}
function readyText(){return mode==="auto"?"Đang xem camera. Bấm sẵn sàng khi muốn bắt đầu.":"Đưa vật vào giữa khung, rồi chụp.";}
function applyHealth(h){
  captureIntervalSeconds=Math.max(30,Math.ceil((h.capture_interval_ms||30000)/1000));
  const wasChecking=linkChecking;
  linkChecking=!!h.api_checking;
  document.getElementById("server").classList.toggle("on",!!h.api_online);
  if(!h.camera_ready||!h.model_ready){
    autoToken=0;arm.hidden=false;stopAutoBtn.hidden=true;snap.disabled=true;arm.disabled=true;linkChecking=false;
    if(!h.camera_ready){live=false;status.textContent="Camera chưa hoạt động.";}
    else status.textContent="Máy chưa sẵn sàng để phân loại. Hãy tắt mở lại.";
    return;
  }
  deviceReady=true;
  if(linkChecking){status.textContent="Đang kết nối tới server";snap.disabled=true;arm.disabled=true;return;}
  if(!busy){snap.disabled=false;arm.disabled=false;}
  if(wasChecking&&!autoToken&&!busy)status.textContent=readyText();
  syncExternal(h);
}
function pollHealth(){
  fetch("/health",{cache:"no-store"}).then(function(r){return r.json();}).then(function(h){
    applyHealth(h);
    setTimeout(pollHealth,linkChecking?700:1000);
  }).catch(function(){setTimeout(pollHealth,1000);});
}
refreshWifi();
pollHealth();
if(location.pathname==="/wifi")openSheet();
</script>
</body>
</html>)PAGE";

const char SAVED_PAGE[] PROGMEM = R"PAGE(<!doctype html>
<html lang="vi">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Đã lưu Wi-Fi</title>
<style>
body{margin:0;background:#e6efe4;color:#17241c;font:16px/1.45 "Segoe UI",system-ui,sans-serif}
main{max-width:420px;margin:28px auto;background:#fffdf8;border-radius:22px;padding:22px 20px;box-shadow:0 12px 32px rgba(23,36,28,.08)}
h1{margin:0 0 8px;font-size:26px}
p{margin:8px 0;color:#5c6b62}
a{color:#0e6b52;font-weight:700}
</style>
</head>
<body>
<main>
<h1>Đã lưu Wi-Fi</h1>
<p>Máy đang khởi động lại. Nối lại hotspot WasteCamera, rồi mở 192.168.4.1. Trong phần Wi-Fi sẽ có địa chỉ mới của camera.</p>
<p><a href="/">Về trang camera</a></p>
</main>
</body>
</html>)PAGE";
