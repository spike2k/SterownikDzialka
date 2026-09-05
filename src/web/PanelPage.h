#pragma once

#include <pgmspace.h>

static const char PanelHtml[] PROGMEM = R"HTML(
<!doctype html>
<html lang="pl">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>EMS Działka</title>
<style>
:root{--bg:#070b12;--card:#101826;--line:#2a3b53;--sun:#f5c542;--go:#3ee0a0;--stop:#ff5d6c;--ice:#6cb6ff;--warn:#ff9f43;--muted:#8ea2bb;--ink:#f3f8ff}
*{box-sizing:border-box}body{margin:0;background:radial-gradient(1200px 500px at 10% -10%,#16324d 0%,var(--bg) 42%);color:var(--ink);font-family:Inter,Segoe UI,system-ui,sans-serif}
.wrap{max-width:1180px;margin:auto;padding:18px 16px 40px}button,input,select{font:inherit}
header{display:flex;justify-content:space-between;align-items:flex-start;gap:12px;margin-bottom:16px}
h1{margin:0;font-size:clamp(1.35rem,3vw,2rem);letter-spacing:-.03em}h2{margin:0 0 12px;font-size:.78rem;letter-spacing:.14em;text-transform:uppercase;color:var(--muted)}
.sub{color:var(--muted);font-size:.92rem}.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center}
.badge{border:1px solid var(--line);border-radius:999px;padding:6px 11px;color:var(--muted);background:#0c1420}
.iconbtn{width:42px;height:42px;border-radius:12px;border:1px solid var(--line);background:#142235;color:var(--ink);display:grid;place-items:center;cursor:pointer}
.iconbtn.on{border-color:var(--ice);color:var(--ice)}
.strip{display:grid;grid-template-columns:repeat(5,minmax(0,1fr));gap:10px;margin-bottom:14px}
.st{background:var(--card);border:1px solid var(--line);border-radius:16px;padding:12px 10px;text-align:center;min-height:92px}
.st svg{width:28px;height:28px;margin-bottom:4px}.st b{display:block;font-size:.78rem}.st small{color:var(--muted);display:block;margin-top:3px;font-size:.75rem;line-height:1.25}
.st.ok{border-color:#1f6a4a;background:linear-gradient(180deg,#10291f,var(--card))}
.st.ok small{color:var(--go)}.st.bad{border-color:#7a2a36;background:linear-gradient(180deg,#31141a,var(--card))}
.st.bad small{color:var(--stop)}.st svg{color:var(--muted)}.st.ok svg{color:var(--go)}.st.bad svg{color:var(--stop)}
.hero{display:grid;grid-template-columns:minmax(260px,.95fr) minmax(0,1.35fr);gap:12px}
.card{background:linear-gradient(165deg,#152033,var(--card));border:1px solid var(--line);border-radius:20px;padding:16px;box-shadow:0 16px 40px #0005}
.batbox{display:flex;gap:18px;align-items:center;flex-wrap:wrap}
.bat{width:78px;height:148px;position:relative;flex:none}
.bat-cap{width:28px;height:10px;background:#9ec0e0;border-radius:4px 4px 0 0;margin:0 auto}
.bat-body{height:138px;border:4px solid #9ec0e0;border-radius:14px;overflow:hidden;position:relative;background:#0a121c}
.bat-fill{position:absolute;left:0;right:0;bottom:0;height:0;transition:height .5s,background .3s}
.soc{font-size:clamp(3.4rem,9vw,5.8rem);font-weight:800;line-height:.9;letter-spacing:-.05em;font-variant-numeric:tabular-nums}
.dir{display:inline-flex;align-items:center;gap:8px;margin-top:10px;padding:8px 12px;border-radius:999px;font-weight:800;letter-spacing:.04em}
.dir svg{width:18px;height:18px}.dir.chg{background:#123d2c;color:var(--go)}.dir.dch{background:#3d141b;color:var(--stop)}
.dir.idle{background:#18283a;color:var(--ice)}.dir.off{background:#2a1c14;color:var(--warn)}
.meta{display:flex;gap:14px;flex-wrap:wrap;margin-top:12px;color:var(--muted);font-variant-numeric:tabular-nums}
.meta b{color:var(--ink);font-size:1.05rem}
.metrics{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px}
.metric .k{color:var(--muted);font-size:.78rem;text-transform:uppercase;letter-spacing:.08em}
.metric .live{display:flex;align-items:baseline;gap:6px;margin-top:6px}
.metric .num{font-size:clamp(1.7rem,4vw,2.4rem);font-weight:800;font-variant-numeric:tabular-nums;letter-spacing:-.03em}
.metric .u{color:var(--muted);font-weight:700}.metric.sun .num{color:var(--sun)}.metric.load .num{color:var(--ice)}
.metric.pvuse .num{color:#9be7c2}.metric.batuse .num{color:var(--stop)}
.ghost{display:none;align-items:center;gap:8px;margin-top:10px;color:var(--stop);font-weight:700}
.ghost svg{width:22px;height:22px;flex:none}.down .live{display:none}.down .ghost{display:flex}
.section{margin-top:16px}
.drift{display:flex;justify-content:space-between;gap:12px;flex-wrap:wrap;align-items:center;margin-bottom:12px}
.pill{padding:8px 12px;border-radius:12px;font-weight:800;border:1px solid var(--line)}
.pill.ok{color:var(--go);background:#123d2c}.pill.warn{color:var(--warn);background:#3a2712}.pill.alarm{color:var(--stop);background:#3d141b}
.cells{display:grid;grid-template-columns:repeat(auto-fit,minmax(92px,1fr));gap:8px}
.cell{background:#0b1420;border:1px solid var(--line);border-radius:12px;padding:10px;text-align:center}
.cell b{display:block;color:var(--ice);font-size:1.05rem}.cell.hot{border-color:var(--stop);background:#2a1216}.cell.hot b{color:var(--stop)}
.cell.warn{border-color:var(--warn)}.empty{padding:22px;color:var(--stop);display:flex;gap:10px;align-items:center;font-weight:700}
.relay{display:flex;justify-content:space-between;align-items:center;gap:8px}
.relay small{color:var(--muted)}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:10px}
.loadrows{display:flex;flex-direction:column;gap:8px;margin-bottom:14px}
.loadrow{display:grid;grid-template-columns:28px minmax(110px,1.4fr) 78px minmax(110px,1fr) 78px 88px;gap:8px;align-items:end}
.loadrow .idx{color:var(--muted);font-weight:700;padding-bottom:10px}
button.act{border:1px solid var(--line);background:#172a42;color:white;padding:10px 14px;border-radius:11px;cursor:pointer}
button.act.on{background:#146c43;border-color:var(--go)}button.act:disabled{opacity:.45;cursor:not-allowed}
.fail{color:var(--stop);font-weight:700}
.hidden{display:none !important}
form .g{display:grid;grid-template-columns:repeat(auto-fit,minmax(160px,1fr));gap:10px;margin-bottom:14px}
label{display:flex;flex-direction:column;gap:6px;color:var(--muted);font-size:.82rem}
input,select{background:#0b1420;border:1px solid var(--line);border-radius:10px;color:var(--ink);padding:10px}
.help{color:var(--muted);font-size:.85rem;line-height:1.45;margin:0 0 12px}
.toast{position:fixed;bottom:16px;right:16px;background:#123d2c;color:var(--go);padding:12px 14px;border-radius:12px;display:none}
.toast.err{background:#3d141b;color:var(--stop)}
@media(max-width:820px){.strip{grid-template-columns:repeat(2,1fr)}.hero,.metrics{grid-template-columns:1fr}.loadrow{grid-template-columns:1fr 1fr}}
</style>
</head>
<body>
<svg xmlns="http://www.w3.org/2000/svg" style="display:none">
<symbol id="i-bat" viewBox="0 0 24 24"><rect x="7" y="5" width="10" height="16" rx="2" fill="none" stroke="currentColor" stroke-width="2"/><rect x="10" y="2" width="4" height="3" rx="1" fill="currentColor"/><path d="M12 9v8M9 13h6" stroke="currentColor" stroke-width="2" fill="none"/></symbol>
<symbol id="i-inv" viewBox="0 0 24 24"><rect x="3" y="6" width="18" height="12" rx="2" fill="none" stroke="currentColor" stroke-width="2"/><path d="M7 13h2.5l1.5-3 2 6 1.5-3H17" fill="none" stroke="currentColor" stroke-width="2"/></symbol>
<symbol id="i-pylon" viewBox="0 0 24 24"><path d="M5 19h14M8 19V8l4-4 4 4v11M8 12h8" fill="none" stroke="currentColor" stroke-width="2"/></symbol>
<symbol id="i-wifi" viewBox="0 0 24 24"><path d="M5 12a10 10 0 0 1 14 0M8.5 15a6 6 0 0 1 7 0" fill="none" stroke="currentColor" stroke-width="2"/><circle cx="12" cy="18.5" r="1.4" fill="currentColor"/></symbol>
<symbol id="i-mqtt" viewBox="0 0 24 24"><path d="M7 8V6a5 5 0 0 1 10 0v2M6 8h12v10H6z" fill="none" stroke="currentColor" stroke-width="2"/></symbol>
<symbol id="i-sun" viewBox="0 0 24 24"><circle cx="12" cy="12" r="4" fill="none" stroke="currentColor" stroke-width="2"/><path d="M12 2v3M12 19v3M2 12h3M19 12h3M4.9 4.9l2.1 2.1M17 17l2.1 2.1M19.1 4.9 17 7M7 17l-2.1 2.1" stroke="currentColor" stroke-width="2"/></symbol>
<symbol id="i-home" viewBox="0 0 24 24"><path d="M4 11 12 4l8 7v9H4z" fill="none" stroke="currentColor" stroke-width="2"/></symbol>
<symbol id="i-warn" viewBox="0 0 24 24"><path d="M12 3 22 20H2z" fill="none" stroke="currentColor" stroke-width="2"/><path d="M12 9v6M12 17.5v.5" stroke="currentColor" stroke-width="2"/></symbol>
<symbol id="i-up" viewBox="0 0 24 24"><path d="M12 19V6M6 11l6-6 6 6" fill="none" stroke="currentColor" stroke-width="2.4"/></symbol>
<symbol id="i-dn" viewBox="0 0 24 24"><path d="M12 5v13M6 13l6 6 6-6" fill="none" stroke="currentColor" stroke-width="2.4"/></symbol>
<symbol id="i-gear" viewBox="0 0 24 24"><circle cx="12" cy="12" r="3" fill="none" stroke="currentColor" stroke-width="2"/><path d="M12 3v3M12 18v3M3 12h3M18 12h3M5.6 5.6l2.1 2.1M16.3 16.3l2.1 2.1M18.4 5.6l-2.1 2.1M7.7 16.3l-2.1 2.1" stroke="currentColor" stroke-width="2"/></symbol>
</svg>
<main class="wrap">
<header>
  <div>
    <h1>EMS Działka</h1>
    <div class="sub" id="ip">łączenie…</div>
  </div>
  <div class="row">
    <span class="badge" id="mode">AUTO</span>
    <button class="iconbtn" id="cfgBtn" title="Konfiguracja" onclick="showCfg(true)"><svg width="22" height="22"><use href="#i-gear"/></svg></button>
  </div>
</header>

<section id="dash">
  <section class="strip">
    <article class="st bad" id="st-jk"><svg><use href="#i-bat"/></svg><b>JK BMS</b><small>Błąd połączenia z BMS</small></article>
    <article class="st bad" id="st-inv"><svg><use href="#i-inv"/></svg><b>Falownik</b><small>Błąd połączenia z falownikiem</small></article>
    <article class="st bad" id="st-pylon"><svg><use href="#i-pylon"/></svg><b>Pylontech</b><small>Emulacja nieaktywna</small></article>
    <article class="st bad" id="st-wifi"><svg><use href="#i-wifi"/></svg><b>Wi-Fi</b><small>Brak sieci</small></article>
    <article class="st bad" id="st-mqtt"><svg><use href="#i-mqtt"/></svg><b>MQTT</b><small>Brak brokera</small></article>
  </section>

  <section class="hero">
    <article class="card">
      <h2>Bateria</h2>
      <div class="batbox">
        <div class="bat"><div class="bat-cap"></div><div class="bat-body"><div class="bat-fill" id="batFill"></div></div></div>
        <div>
          <div class="soc" id="soc">—</div>
          <div class="dir off" id="dir"><svg><use href="#i-warn"/></svg><span>Brak danych BMS</span></div>
          <div class="meta">
            <span>Napięcie<br><b id="batv">—</b></span>
            <span>Prąd<br><b id="bata">—</b></span>
            <span>Moc<br><b id="batw">—</b></span>
          </div>
        </div>
      </div>
    </article>
    <article class="card">
      <h2>Przepływ energii</h2>
      <div class="metrics">
        <div class="metric sun card" id="m-pv" style="padding:12px">
          <div class="k">Moc paneli</div>
          <div class="live"><span class="num" id="pv">—</span><span class="u">W</span></div>
          <div class="ghost"><svg><use href="#i-warn"/></svg><b>Błąd połączenia z falownikiem</b></div>
        </div>
        <div class="metric load card" id="m-load" style="padding:12px">
          <div class="k">Pobór odbiorników</div>
          <div class="live"><span class="num" id="load">—</span><span class="u">W</span></div>
          <div class="ghost"><svg><use href="#i-warn"/></svg><b>Błąd połączenia z falownikiem</b></div>
        </div>
        <div class="metric pvuse card" id="m-sun" style="padding:12px">
          <div class="k">Pobór ze słońca</div>
          <div class="live"><span class="num" id="fromSun">—</span><span class="u">W</span></div>
          <div class="ghost"><svg><use href="#i-warn"/></svg><b>Błąd połączenia z falownikiem</b></div>
        </div>
        <div class="metric batuse card" id="m-bat" style="padding:12px">
          <div class="k">Pobór z baterii</div>
          <div class="live"><span class="num" id="fromBat">—</span><span class="u">W</span></div>
          <div class="ghost"><svg><use href="#i-warn"/></svg><b>Błąd połączenia z BMS</b></div>
        </div>
      </div>
    </article>
  </section>

  <section class="section card">
    <div class="drift">
      <h2 style="margin:0">Napięcia cel</h2>
      <div class="pill" id="driftPill">Dryf —</div>
    </div>
    <div class="row" style="margin-bottom:10px;color:var(--muted)" id="minmax">min — · max —</div>
    <div class="cells" id="cells"></div>
  </section>

  <section class="section card">
    <h2>Tryb sterowania</h2>
    <div class="row">
      <button class="act" onclick="mode('auto')">Auto</button>
      <button class="act" onclick="mode('manual')">Manual</button>
      <span class="fail" id="failsafe"></span>
    </div>
  </section>

  <section class="section">
    <h2>Odbiorniki</h2>
    <div class="grid" id="relays"></div>
  </section>

  <section class="section hidden" id="inputsWrap">
    <h2>Wejścia stanu</h2>
    <div class="grid" id="inputs"></div>
  </section>
</section>

<section id="cfg" class="hidden">
  <article class="card">
    <div class="row" style="justify-content:space-between;margin-bottom:12px">
      <h2 style="margin:0">Konfiguracja</h2>
      <button class="act" onclick="showCfg(false)">Dashboard</button>
    </div>
    <p class="help">SSID i hasło zapisują się w pamięci ESP. Puste SSID włącza punkt dostępowy <b>SterownikDzialka-Setup</b> (192.168.4.1). Zmiana pinów UART (BMS / falownik / Pylontech) wymaga restartu.</p>
    <form id="cfgForm" onsubmit="return saveCfg(event)">
      <h2>Wi-Fi</h2>
      <div class="g">
        <label>SSID<input id="wifiSsid" name="wifiSsid" maxlength="32" autocomplete="off"></label>
        <label>Hasło<input id="wifiPassword" name="wifiPassword" type="password" maxlength="64" placeholder="bez zmian, jeśli puste"></label>
      </div>
      <h2>MQTT (opcjonalnie)</h2>
      <div class="g">
        <label>Host<input id="mqttHost" name="mqttHost" maxlength="64"></label>
        <label>Port<input id="mqttPort" name="mqttPort" type="number" min="1" max="65535"></label>
        <label>Użytkownik<input id="mqttUser" name="mqttUser" maxlength="32"></label>
        <label>Hasło<input id="mqttPassword" name="mqttPassword" type="password" maxlength="64" placeholder="bez zmian, jeśli puste"></label>
      </div>
      <h2>Odbiorniki</h2>
      <p class="help">GPIO ≥ 0 = sterowanie lokalne. Puste GPIO i identyfikator MQTT (np. <b>fontanna</b>) = satelita na <code>ems/sterownik-dzialka/load/{id}/set</code>. Oba puste = slot wyłączony. Priorytet 1 dostaje nadwyżkę pierwszy. Auto pomija kanał z mocą 0 W. Domyślne GPIO 1–4: 16, 17, 18, 19.</p>
      <div class="loadrows" id="loadRows"></div>
      <div class="g">
        <label>Logika GPIO<select id="relayActiveLow" name="relayActiveLow"><option value="1">aktywny stan niski</option><option value="0">aktywny stan wysoki</option></select></label>
        <label>Rezerwa (W)<input id="surplusReserveW" name="surplusReserveW" type="number" min="0" max="5000"></label>
        <label>Histereza (W)<input id="loadHysteresisW" name="loadHysteresisW" type="number" min="0" max="2000"></label>
        <label>Min. czas (ms)<input id="loadMinToggleMs" name="loadMinToggleMs" type="number" min="0" max="60000"></label>
      </div>
      <h2>Wejścia stanu</h2>
      <p class="help">Na razie bez podłączenia — GPIO zostaw -1. Wejścia 4, 13, 14, 15 mają wewnętrzny pull-up.</p>
      <div class="g">
        <label>Nazwa 1<input id="statusName0" name="statusName0" maxlength="20"></label>
        <label>GPIO 1<input id="status0" name="status0" type="number" min="-1" max="39"></label>
        <label>Nazwa 2<input id="statusName1" name="statusName1" maxlength="20"></label>
        <label>GPIO 2<input id="status1" name="status1" type="number" min="-1" max="39"></label>
        <label>Nazwa 3<input id="statusName2" name="statusName2" maxlength="20"></label>
        <label>GPIO 3<input id="status2" name="status2" type="number" min="-1" max="39"></label>
        <label>Nazwa 4<input id="statusName3" name="statusName3" maxlength="20"></label>
        <label>GPIO 4<input id="status3" name="status3" type="number" min="-1" max="39"></label>
        <label>Logika<select id="statusActiveLow" name="statusActiveLow"><option value="1">aktywny stan niski</option><option value="0">aktywny stan wysoki</option></select></label>
      </div>
      <h2>Komunikacja z baterią i falownikiem</h2>
      <div class="g">
        <label>MAC JK-BMS (opcjonalny)<input id="jkBmsMac" name="jkBmsMac" maxlength="17" placeholder="auto lub aa:bb:cc:dd:ee:ff"></label>
        <label>Falownik RX<input id="anenjiRx" name="anenjiRx" type="number" min="-1" max="39"></label>
        <label>Falownik TX<input id="anenjiTx" name="anenjiTx" type="number" min="-1" max="39"></label>
        <label>Pylon RX<input id="pylonRx" name="pylonRx" type="number" min="-1" max="39"></label>
        <label>Pylon TX<input id="pylonTx" name="pylonTx" type="number" min="-1" max="39"></label>
      </div>
      <h2>Debug komunikacji (konsola USB)</h2>
      <p class="help">ESP podłączone przez USB, monitor 115200. JK pokazuje skan BLE, services i notify HEX; falownik ramki RS232. Komendy: <b>debug jk</b>, <b>debug inv</b>, <b>debug pylon</b>, <b>debug off</b>, <b>help</b>.</p>
      <div class="g">
        <label>JK BMS<input id="debugJk" type="checkbox"></label>
        <label>Falownik<input id="debugAnenji" type="checkbox"></label>
        <label>Pylontech<input id="debugPylon" type="checkbox"></label>
      </div>
      <h2>Alarm dryfu cel</h2>
      <div class="g">
        <label>Próg alarmu (mV)<input id="cellDriftAlarmMv" name="cellDriftAlarmMv" type="number" min="5" max="500"></label>
      </div>
      <div class="row">
        <button class="act on" type="submit">Zapisz</button>
        <button class="act" type="button" onclick="fillDefaults()">Przywróć piny domyślne</button>
      </div>
    </form>
  </article>
</section>
</main>
<div class="toast" id="toast"></div>
<script>
const q=id=>document.getElementById(id);
let defaults=null;
function toast(msg,err){const t=q('toast');t.textContent=msg;t.className='toast'+(err?' err':'');t.style.display='block';setTimeout(()=>t.style.display='none',3200)}
function showCfg(on){q('dash').classList.toggle('hidden',on);q('cfg').classList.toggle('hidden',!on);q('cfgBtn').classList.toggle('on',on);if(on)loadCfg()}
function st(id,ok,good,bad){const el=q(id);el.className='st '+(ok?'ok':'bad');el.querySelector('small').textContent=ok?good:bad}
function setMetric(id,online){q(id).classList.toggle('down',!online)}
async function call(url){await fetch(url,{method:'POST'});await refresh()}
async function mode(v){await call('/api/mode?value='+v)}
async function relay(i,on){await call('/api/relay?id='+i+'&state='+(on?1:0))}
function fill(id,v){q(id).value=v}
function esc(s){return String(s||'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]))}
function nm(list,i,fb){const s=list&&list[i];return s&&s.length?s:fb}
function ensureLoadRows(){
  const box=q('loadRows');
  if(!box||box.dataset.ready)return;
  box.innerHTML=Array.from({length:10},(_,i)=>`<div class="loadrow"><span class="idx">${i+1}</span>
    <label>Nazwa<input id="loadName${i}" name="loadName${i}" maxlength="20"></label>
    <label>GPIO<input id="loadPin${i}" name="loadPin${i}" type="number" min="-1" max="39"></label>
    <label>MQTT<input id="loadMqtt${i}" name="loadMqtt${i}" maxlength="20" placeholder="np. fontanna" pattern="[a-zA-Z0-9_-]*"></label>
    <label>Priorytet<input id="loadPrio${i}" name="loadPrio${i}" type="number" min="1" max="10"></label>
    <label>Moc W<input id="loadPower${i}" name="loadPower${i}" type="number" min="0" max="30000"></label>
  </div>`).join('');
  box.dataset.ready='1';
}
function fillDefaults(){if(!defaults)return;ensureLoadRows();
(defaults.loadPins||[]).forEach((v,i)=>fill('loadPin'+i,v));
for(let i=0;i<10;i++){fill('loadPrio'+i,i+1);fill('loadPower'+i,0);fill('loadMqtt'+i,'')}
['status0','status1','status2','status3'].forEach((id,i)=>fill(id,defaults.statusPins[i]));
fill('jkBmsMac',defaults.jkBmsMac||'');
fill('anenjiRx',defaults.anenjiRx);fill('anenjiTx',defaults.anenjiTx);
fill('pylonRx',defaults.pylonRx);fill('pylonTx',defaults.pylonTx);
fill('relayActiveLow',defaults.relayActiveLow?1:0);fill('statusActiveLow',defaults.statusActiveLow?1:0);
fill('cellDriftAlarmMv',defaults.cellDriftAlarmMv);
fill('surplusReserveW',defaults.surplusReserveW);fill('loadHysteresisW',defaults.loadHysteresisW);fill('loadMinToggleMs',defaults.loadMinToggleMs)}
async function loadCfg(){
  const s=await(await fetch('/api/settings')).json();defaults=s.defaults;ensureLoadRows();
  fill('wifiSsid',s.wifiSsid);q('wifiPassword').value='';q('wifiPassword').placeholder=s.wifiHasPassword?'•••••••• · zostaw puste, by nie zmieniać':'hasło sieci';
  fill('mqttHost',s.mqttHost);fill('mqttPort',s.mqttPort);fill('mqttUser',s.mqttUser);q('mqttPassword').value='';
  (s.loads||[]).forEach((ch,i)=>{fill('loadName'+i,ch.name||'');fill('loadPin'+i,ch.pin);fill('loadMqtt'+i,ch.mqttKey||'');fill('loadPrio'+i,ch.priority);fill('loadPower'+i,ch.powerW)});
  s.statusPins.forEach((v,i)=>fill('status'+i,v));
  (s.statusNames||[]).forEach((v,i)=>fill('statusName'+i,v));
  q('debugJk').checked=!!s.debugJk;q('debugAnenji').checked=!!s.debugAnenji;q('debugPylon').checked=!!s.debugPylon;
  fill('relayActiveLow',s.relayActiveLow?1:0);fill('statusActiveLow',s.statusActiveLow?1:0);
  fill('jkBmsMac',s.jkBmsMac||'');fill('anenjiRx',s.anenjiRx);fill('anenjiTx',s.anenjiTx);
  fill('pylonRx',s.pylonRx);fill('pylonTx',s.pylonTx);fill('cellDriftAlarmMv',s.cellDriftAlarmMv);
  fill('surplusReserveW',s.surplusReserveW);fill('loadHysteresisW',s.loadHysteresisW);fill('loadMinToggleMs',s.loadMinToggleMs);
}
async function saveCfg(ev){
  ev.preventDefault();
  const body=new URLSearchParams(new FormData(q('cfgForm')));
  body.set('debugJk',q('debugJk').checked?'1':'0');
  body.set('debugAnenji',q('debugAnenji').checked?'1':'0');
  body.set('debugPylon',q('debugPylon').checked?'1':'0');
  const r=await fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
  const j=await r.json();
  if(!r.ok){toast(j.error||'Błąd zapisu',true);return}
  toast(j.reboot?'Zapisano. Restart ESP…':'Zapisano.');
  if(j.reboot){await fetch('/api/reboot',{method:'POST'});return}
  showCfg(false);refresh();
}
async function refresh(){
  try{
    const s=await(await fetch('/api/state')).json();
    q('mode').textContent=s.mode.toUpperCase();
    q('ip').textContent=s.ip+(s.ap?' · AP '+s.apSsid:'');
    q('failsafe').textContent=s.healthy?'':'Fail-safe: brak świeżej telemetrii — odbiorniki OFF';
    st('st-jk',s.jk,'Połączono','Błąd połączenia z BMS');
    st('st-inv',s.anenji,'Połączono','Błąd połączenia z falownikiem');
    st('st-pylon',s.pylon,'Ramka aktywna','Emulacja nieaktywna');
    st('st-wifi',s.wifi,s.ip,'Brak sieci');
    st('st-mqtt',s.mqtt,'Broker OK',s.mqttHost?'Błąd połączenia z MQTT':'MQTT nieustawione');
    const fill=q('batFill');
    if(s.jk){
      q('soc').textContent=s.soc.toFixed(0)+'%';
      q('batv').textContent=s.batteryV.toFixed(2)+' V';
      q('bata').textContent=(s.batteryA>=0?'+':'')+s.batteryA.toFixed(1)+' A';
      q('batw').textContent=(s.batteryW>=0?'+':'')+Math.round(s.batteryW)+' W';
      fill.style.height=Math.max(0,Math.min(100,s.soc))+'%';
      const dir=q('dir');
      if(s.charge==='charge'){dir.className='dir chg';dir.innerHTML='<svg><use href="#i-up"/></svg><span>ŁADOWANIE</span>';fill.style.background='linear-gradient(#6ff3c0,#1ea86a)'}
      else if(s.charge==='discharge'){dir.className='dir dch';dir.innerHTML='<svg><use href="#i-dn"/></svg><span>ROZŁADOWANIE</span>';fill.style.background='linear-gradient(#ff8a96,#d13b4c)'}
      else {dir.className='dir idle';dir.innerHTML='<span>BEZCZYNNA</span>';fill.style.background='linear-gradient(#8cc7ff,#2f78c4)'}
      if(s.soc<20) fill.style.background='linear-gradient(#ff8a96,#d13b4c)';
    } else {
      q('soc').textContent='—';q('batv').textContent='—';q('bata').textContent='—';q('batw').textContent='—';
      fill.style.height='0%';
      q('dir').className='dir off';q('dir').innerHTML='<svg><use href="#i-warn"/></svg><span>Błąd połączenia z BMS</span>';
    }
    setMetric('m-pv',s.anenji);setMetric('m-load',s.anenji);setMetric('m-sun',s.anenji);setMetric('m-bat',s.jk);
    if(s.anenji){q('pv').textContent=Math.round(s.pvW);q('load').textContent=Math.round(s.loadW);q('fromSun').textContent=Math.round(s.solarCoverW)}
    if(s.jk) q('fromBat').textContent=Math.round(s.batteryCoverW);
    if(!s.jk||!s.cells.length){
      q('cells').innerHTML='<div class="empty"><svg width="22" height="22"><use href="#i-warn"/></svg>Błąd połączenia z BMS — brak napięć cel</div>';
      q('driftPill').className='pill warn';q('driftPill').textContent='Dryf —';
      q('minmax').textContent='min — · max —';
    } else {
      const alarm=s.cellAlarm, warn=s.cellDriftMv>=s.cellDriftAlarmMv*0.6;
      q('driftPill').className='pill '+(alarm?'alarm':warn?'warn':'ok');
      q('driftPill').textContent=(alarm?'ALARM ':'')+'Dryf '+s.cellDriftMv.toFixed(0)+' mV';
      q('minmax').textContent='min '+s.cellMinV.toFixed(3)+' V · max '+s.cellMaxV.toFixed(3)+' V · próg '+s.cellDriftAlarmMv+' mV';
      q('cells').innerHTML=s.cells.map((v,i)=>{
        const hot=alarm&&(Math.abs(v-s.cellMinV)<0.0005||Math.abs(v-s.cellMaxV)<0.0005);
        const mid=warn&&!hot;
        return `<div class="cell${hot?' hot':mid?' warn':''}">C${i+1}<b>${v.toFixed(3)} V</b></div>`;
      }).join('');
    }
    const loads=(s.loads||[]).filter(x=>x.pin>=0||(x.mqttKey&&x.mqttKey.length));
    q('relays').innerHTML=loads.map(x=>{
      const via=x.pin>=0?('GPIO '+x.pin):( 'MQTT '+x.mqttKey);
      const extra='P'+x.priority+' · '+x.powerW+' W · '+via;
      return `<div class="card relay"><span>${esc(x.name||('Odbiornik '+(x.id+1)))}<br><small>${esc(extra)}</small></span><button class="act ${x.on?'on':''}" onclick="relay(${x.id},${!x.on})" ${s.mode!=='manual'?'disabled':''}>${x.on?'ON':'OFF'}</button></div>`;
    }).join('');
    const ins=s.inputs.filter(x=>x.pin>=0);
    q('inputsWrap').classList.toggle('hidden',!ins.length);
    q('inputs').innerHTML=s.inputs.map((x,i)=>x.pin<0?'':`<div class="card relay"><span>${esc(nm(s.inputNames,i,'Wejście '+(i+1)))}<br><small style="color:var(--muted)">GPIO ${x.pin}</small></span><b style="color:${x.on?'var(--go)':'var(--muted)'}">${x.on?'ON':'OFF'}</b></div>`).join('');
  }catch(e){q('ip').textContent='brak połączenia ze sterownikiem'}
}
refresh();setInterval(refresh,2000);
</script>
</body></html>
)HTML";
