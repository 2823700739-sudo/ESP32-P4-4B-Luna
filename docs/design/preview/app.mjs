import { CARDS, PreviewModel, petMotion, percent, clockAngles } from './model.mjs';

const root = document.getElementById('display');
const card = document.getElementById('card');
const popup = document.getElementById('volume-popup');
const pet = document.getElementById('pet');
const sprite = document.getElementById('pet-sprite');
const bubble = document.getElementById('pet-bubble');
const model = new PreviewModel(performance.now());
let connectionDemo='auto';
const idleScreen = document.getElementById('idle-screen');
document.getElementById('clock-ticks').innerHTML=Array.from({length:60},(_,i)=>{
  const a=i*Math.PI/30,r=i%5?194:183;
  return `<line x1="${220+r*Math.sin(a)}" y1="${220-r*Math.cos(a)}" x2="${220+202*Math.sin(a)}" y2="${220-202*Math.cos(a)}" stroke="${i%5?'#596b85':'#b8a2ff'}" stroke-width="${i%5?2:4}"/>`;
}).join('');
const LABELS = { clock:'时钟', music:'音乐', weather:'天气', codex:'额度', computer:'电脑', focus:'专注' };
const ICONS = { clock:'clock', music:'play', weather:'weather', codex:'code', computer:'computer', focus:'moon' };
const tracks = ['在月光下漫步', '像素宇宙', '晚安，Luna'];
const icon = (name, cls = '') => `<svg class="${cls}" aria-hidden="true"><use href="#i-${name}"/></svg>`;
const esc = text => String(text).replace(/[&<>"']/g, char => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[char]));
const pct = value => percent(value) === null ? '--' : `${Math.round(percent(value))}%`;
const notes = d => d.stale ? '上次数据 · 示例' : '示例数据';

function header(title, glyph, detail) {
  return `<div class="card-header"><div class="card-heading">${icon(glyph)}${title}</div><span class="card-kicker">${detail}</span></div>`;
}
function progress(value, name) {
  const p = percent(value);
  return `<div class="progress" role="progressbar" aria-label="${name}" ${p === null ? 'aria-valuetext="不可用"' : `aria-valuemin="0" aria-valuemax="100" aria-valuenow="${p}"`}><div class="progress-fill" style="width:${p ?? 0}%;${p === null ? 'visibility:hidden' : ''}"></div></div>`;
}
function bottom(left, right, stale = false, cls = '') {
  return `<div class="card-bottom ${cls}"><span class="${stale ? 'stale-note' : ''}">${left}</span>${right ? `<span>${right}</span>` : ''}</div>`;
}
function clockCard(d) {
  return header('TIME', 'clock', '慢一点，也很好') + `<div class="clock-orbit"></div><div class="clock-content"><div class="clock-greeting" id="greeting">此刻，是你的时间</div><div class="digital-time"><span id="digital-hm">--:--</span><small id="digital-ss">--</small></div><div class="clock-date" id="clock-date"></div>${icon('moon','clock-moon')}<div class="clock-source"><span class="source-dot"></span><span id="clock-source"></span></div></div>` + bottom('LOCAL CLOCK', d.timeValid ? '保电时，本地继续走时' : '等待有效时间');
}
function musicCard(d) {
  const title = d.music.available ? (model.scenario === 'long' ? d.music.title : tracks[model.trackIndex]) : '等待音乐';
  const deck=`<div class="record-deck" data-playing="${model.musicPlaying}"><div class="record ${model.musicPlaying?'playing':''}">${icon('moon')}</div><svg class="tonearm" viewBox="0 0 220 250" aria-hidden="true"><g class="arm"><polyline points="183,76 171,130 149,154" fill="none" stroke="#d3dced" stroke-width="4"/><line x1="156" y1="146" x2="146" y2="157" stroke="#ffbc82" stroke-width="9"/></g><circle cx="183" cy="76" r="9" fill="#101521" stroke="#50617c" stroke-width="2"/><circle cx="183" cy="76" r="3" fill="#ffbc82"/></svg></div>`;
  return header('MUSIC', 'play', notes(d)) + `<div class="music-main">${deck}<div class="track-text"><div class="track-caption">LUNA / FIXED PLAYER</div><h2 class="track-title" title="${esc(title)}">${esc(title)}</h2><div class="track-artist" title="${esc(d.music.artist)}">${d.music.available ? esc(d.music.artist) : '连接电脑，打开音乐播放器'}</div></div></div><div class="music-controls"><button class="media-button" data-action="previous" aria-label="上一首" ${model.canControl ? '' : 'disabled'}>${icon('prev')}</button><button class="media-button play-button" data-action="toggle" aria-label="${model.playing ? '暂停' : '播放'}" ${model.canControl ? '' : 'disabled'}>${icon(model.playing ? 'pause' : 'play', model.playing ? '' : 'filled-play')}</button><button class="media-button" data-action="next" aria-label="下一首" ${model.canControl ? '' : 'disabled'}>${icon('next')}</button></div><div class="card-bottom music-bottom"><button class="volume-button" data-action="volume-open" aria-label="展开系统音量" ${model.canControl ? '' : 'disabled'}>${icon(model.muted ? 'mute' : 'volume')}</button></div>`;
}
function weatherCard(d) {
  const w = d.weather;
  return header('WEATHER', 'weather', esc(w.city)) + `<div class="weather-main"><div class="weather-visual" aria-hidden="true">${w.available ? '<div class="sun"></div><div class="cloud"></div>' : '<span class="weather-unknown">?</span>'}</div><div class="weather-reading"><div class="temperature">${w.available ? w.temperature : '--'}<sup>°C</sup></div><div class="condition">${w.available ? w.condition : '等待天气'}</div></div></div><div class="weather-details"><div class="weather-detail"><span>体感</span><strong>${w.available ? `${w.feels}°` : '--'}</strong></div><div class="weather-detail"><span>湿度</span><strong>${w.available ? `${w.humidity}%` : '--'}</strong></div><div class="weather-detail"><span>风速</span><strong>${w.available ? `${w.wind} m/s` : '--'}</strong></div></div>` + bottom(!w.available ? '尚无天气数据' : w.cached ? `缓存 · 上次更新 ${w.updated}` : `Wi-Fi 独立联网 · 更新 ${w.updated}`, 'OPEN-METEO / 示例', w.cached);
}
function quotaCard(d) {
  const q = d.quota;
  return header('CODEX', 'code', notes(d)) + `<section class="quota-section"><div class="quota-label"><span>5 小时剩余</span><strong>${pct(q.primary)}</strong></div>${progress(q.primary,'5 小时剩余额度')}</section><section class="quota-section weekly"><div class="quota-label"><span>周额度剩余</span><strong>${pct(q.weekly)}</strong></div>${progress(q.weekly,'周剩余额度')}</section><div class="project">${icon('code')}<div><div class="project-label">当前 VS Code 项目${d.project.stale ? ' · 上次信息' : ''}</div><div class="project-name" title="${esc(d.project.name)}">${d.project.available ? esc(d.project.name) : '尚未识别工程'}</div></div></div>` + bottom(`额度更新 ${q.updated} · 示例`, '', d.stale);
}
function gauge(value, label, cls = '') {
  const p = percent(value);
  return `<div class="gauge ${cls}" role="img" aria-label="${label}使用率 ${p === null ? '不可用' : `${p}%`}"><svg viewBox="0 0 160 160"><circle class="gauge-track" cx="80" cy="80" r="66" pathLength="100" stroke-dasharray="75 100"/><circle class="gauge-value" cx="80" cy="80" r="66" pathLength="100" stroke-dasharray="${(p ?? 0)*.75} 100" style="${p === null ? 'visibility:hidden' : ''}"/></svg><div class="gauge-number">${p === null ? '--' : Math.round(p)}<small>${p === null ? '' : '%'}</small><span class="gauge-label">${label}</span></div></div>`;
}
function memoryRow(name, value, capacity, cls = '') {
  return `<div class="memory-row ${cls}"><div class="memory-label"><span>${name}</span><span class="memory-capacity">${value === null ? '不可用' : capacity}</span><strong>${value === null ? '--' : pct(value)}</strong></div>${progress(value,`${name}使用率`)}</div>`;
}
function computerCard(d) {
  const c = d.computer;
  return header('COMPUTER', 'computer', notes(d)) + `<div class="computer-name" title="${esc(c.name)}">${esc(c.name)}</div><div class="gauges">${gauge(c.cpu,'CPU')}${gauge(c.gpu,'GPU','gpu')}</div><div class="memory-section">${memoryRow('内存',c.ram,c.ramLabel)}${memoryRow('显存',c.vram,c.vramLabel,'gpu')}</div>` + bottom(c.available ? '2 秒采样' : '等待电脑状态', '示例 · 非本机读数', d.stale);
}
function focusCard(){
  return header('FOCUS','moon','')+`<div id="focus-status">准备专注</div><div class="focus-digital"><div id="focus-hint">剩余专注时间</div><div id="focus-time" role="timer" aria-live="off">25:00</div><div class="focus-track"><div id="focus-progress"></div></div></div><div class="focus-presets">${[25,45,60].map(minutes=>`<button data-action="focus-${minutes}" data-minutes="${minutes}" aria-pressed="false">${minutes} 分钟</button>`).join('')}<button id="focus-custom" data-action="focus-custom">自定义</button></div><div class="focus-controls"><button id="focus-toggle" data-action="focus-start">开始</button><button data-action="focus-end">结束</button></div>`;
}
const renderers = { clock:clockCard, music:musicCard, weather:weatherCard, codex:quotaCard, computer:computerCard, focus:focusCard };

const nav = document.getElementById('card-nav');
nav.innerHTML = CARDS.map(key => `<button class="nav-item" data-card="${key}" aria-label="${LABELS[key]}卡" aria-current="${key === model.card}">${icon(ICONS[key])}<span>${LABELS[key]}</span></button>`).join('');
function renderCard(direction = 0) {
  const d = model.data;
  const deck=model.card==='music' && card.dataset.card==='music'?card.querySelector('.record-deck'):null;
  card.dataset.card = model.card;
  card.classList.toggle('data-stale', d.stale && ['music','codex','computer'].includes(model.card));
  if(deck){
    /* Keep the deck connected: detaching it cancels CSS transitions and
       restarts the record even when the same DOM node is reinserted. */
    const fresh=document.createElement('template');fresh.innerHTML=renderers[model.card](d);
    for(const selector of ['.card-header','.track-text','.music-controls','.music-bottom'])
      card.querySelector(selector).replaceWith(fresh.content.querySelector(selector));
    deck.dataset.playing=String(model.musicPlaying);
    deck.querySelector('.record').classList.toggle('playing',model.musicPlaying);
  }else card.innerHTML = renderers[model.card](d);
  if (direction) {
    card.style.setProperty('--slide', `${direction > 0 ? 26 : -26}px`);
    card.classList.remove('card-animate'); void card.offsetWidth; card.classList.add('card-animate');
  }
  for (const item of nav.children) item.setAttribute('aria-current', item.dataset.card === model.card ? 'true' : 'false');
  syncChrome(); tickClock();paintFocus(performance.now());
}
function syncChrome() {
  const d = model.data;
  idleScreen.hidden=!model.idle;root.classList.toggle('idle',model.idle);
  popup.hidden = !model.volumeOpen;
  const connection = document.getElementById('connection');
  connection.classList.toggle('offline', !d.pc);
  const phase={pairing:'连接中',recovery:'等待连接',securing:'连接中',ready:'已配对',waiting:'等待连接',error:'连接异常'}[connectionDemo];
  connection.querySelector('span').textContent = phase?`蓝牙 · 示例${phase}`:d.pc ? '蓝牙 · 示例已连接' : '蓝牙 · 示例未连接';
  document.getElementById('volume-value').textContent = `${model.volume}%`;
  document.getElementById('volume-slider').value = model.volume;
  const mute = popup.querySelector('[data-action="mute"]');
  mute.setAttribute('aria-label',model.muted ? '取消静音' : '静音');
  mute.disabled = !model.canControl;
  mute.querySelector('use').setAttribute('href',model.muted ? '#i-mute' : '#i-volume');
  document.getElementById('mode-label').textContent = `${LABELS[model.card]}卡`;
}
function sourceText(d) {
  return !d.timeValid ? '尚未校时 · 不显示假时间' : d.timeSource === 'BLE' ? 'BLE 优先校时 · 示例' : d.timeSource === 'NTP' ? 'Wi-Fi NTP 备用校时 · 示例' : '离线 · 依靠本地继续走时';
}
function tickClock() {
  const d = model.data;
  const date = new Date();
  const fmt = n => String(n).padStart(2,'0');
  const day = `${date.getFullYear()} 年 ${date.getMonth()+1} 月 ${date.getDate()} 日 · ${['星期日','星期一','星期二','星期三','星期四','星期五','星期六'][date.getDay()]}`;
  if(model.idle) {
    document.getElementById('sleeping-sprite').style.backgroundPosition=`${Math.floor(performance.now()/1000)%2?-144:-216}px -72px`;
    document.getElementById('idle-date').textContent=d.timeValid?day:'等待有效时间';
    const angles=clockAngles(date);
    for(const key of ['hour','minute','second']) {
      const hand=document.getElementById(`hand-${key}`);hand.style.visibility=d.timeValid?'':'hidden';
      hand.setAttribute('transform',`rotate(${angles[key]} 220 220)`);
    }
  }
  if (model.card === 'clock') {
    document.getElementById('digital-hm').textContent = d.timeValid ? `${fmt(date.getHours())}:${fmt(date.getMinutes())}` : '--:--';
    document.getElementById('digital-ss').textContent = d.timeValid ? fmt(date.getSeconds()) : '--';
    document.getElementById('clock-date').textContent = d.timeValid ? day : '等待有效时间';
    document.getElementById('clock-source').textContent = sourceText(d);
    document.getElementById('greeting').textContent = date.getHours() >= 18 ? '夜色温柔，慢慢来' : '此刻，是你的时间';
  }
}
function navigate(delta) { model.navigate(delta); renderCard(delta); }
let swipe = null;
let petPress = null;
let wakePointer=null,suppressClickUntil=0;
root.addEventListener('pointerdown', event => {
  if(model.focusEditing||model.connectionModal)return;
  if(model.touch(performance.now())==='wake') {
    wakePointer=event.pointerId;suppressClickUntil=performance.now()+1000;syncChrome();
    event.preventDefault();event.stopImmediatePropagation();return;
  }
  const dismissed=model.volumeOpen&&!popup.contains(event.target);
  if (!popup.contains(event.target)) { model.volumeOpen = false; syncChrome(); }
  if (pet.contains(event.target)) {
    petPress = event.pointerId;
    model.dragging = true;
    pet.setPointerCapture(event.pointerId);
    return;
  }
  if (event.target instanceof Element && event.target.closest('button,input,select,.volume-popup')) return;
  const content=event.target instanceof Element&&event.target.closest('h2,svg,.card-heading,.card-kicker,.track-text,.clock-content,.weather-reading,.weather-details,.quota-section,.project,.gauges,.memory-section,.computer-name,.card-bottom');
  const bounds=root.getBoundingClientRect();const localY=(event.clientY-bounds.top)*720/bounds.width;
  const topBlank=localY>=0&&localY<135&&!event.target.closest('.brand,.connection');
  swipe = { x:event.clientX, y:event.clientY, id:event.pointerId, blank:topBlank&&!dismissed&&!content };
  model.beginDrag(performance.now());
  root.setPointerCapture(event.pointerId);
}, true);
root.addEventListener('pointermove', event => {
  if(swipe?.id===event.pointerId){
    const scale=root.getBoundingClientRect().width/720;
    if(Math.abs(event.clientX-swipe.x)>12*scale||Math.abs(event.clientY-swipe.y)>12*scale)swipe.moved=true;
  }
},true);
root.addEventListener('pointerup', event => {
  if(wakePointer===event.pointerId) { wakePointer=null;suppressClickUntil=performance.now()+300;model.endDrag(performance.now());event.stopImmediatePropagation();return; }
  if (swipe?.id === event.pointerId) {
    const dx = event.clientX - swipe.x, dy = event.clientY - swipe.y;
    const scale = root.getBoundingClientRect().width / 720;
    if (Math.abs(dx) > 55 * scale && Math.abs(dx) > Math.abs(dy) * 1.3) navigate(dx < 0 ? 1 : -1);
    else if(swipe.blank&&!swipe.moved&&Math.abs(dx)<12*scale&&Math.abs(dy)<12*scale){model.enterIdle();syncChrome();tickClock();}
    swipe = null;
  }
  model.endDrag(performance.now());
}, true);
root.addEventListener('pointercancel', () => { swipe = null; petPress = null; model.endDrag(performance.now()); });
// Releasing a slider outside the screen must clear the active gesture.
window.addEventListener('pointerup', () => { petPress = null; if (model.dragging) model.endDrag(performance.now()); },true);
window.addEventListener('pointercancel', () => { petPress = null; if (model.dragging) model.endDrag(performance.now()); },true);
pet.addEventListener('lostpointercapture', () => { petPress = null; });
root.addEventListener('click', event => {
  if(model.connectionModal){
    event.preventDefault();event.stopImmediatePropagation();
    const action=event.target.closest('[data-ble-action]')?.dataset.bleAction;
    if(action)setConnectionDemo(action==='accept'?'ready':'waiting',action==='recover'?'旧配对已移除，请在电脑重新添加 Luna':'');
    return;
  }
  if(model.focusEditing){handleFocusEditor(event);return;}
  if(performance.now()<suppressClickUntil||model.idle){event.preventDefault();event.stopImmediatePropagation();return;}
  model.touch(performance.now());
  const button = event.target instanceof Element ? event.target.closest('button') : null;
  if (!button || button.disabled) return;
  if(button.dataset.action==='focus-custom'){openFocusEditor();return;}
  if (button.dataset.action) { model.command(button.dataset.action,performance.now()); renderCard(); }
  else if (button.dataset.card) { model.chooseCard(button.dataset.card); renderCard(1); }
  else if (button.id === 'next-card') navigate(1);
  else if (button.id === 'previous-card') navigate(-1);
  else if (button.id === 'pet') { const now=performance.now(); model.command('pet',now); syncChrome(); paintPet(now,0); }
}, true);
root.addEventListener('keydown', event => {
  if(model.connectionModal){
    if(event.key==='Tab')return;
    if(event.key==='Escape'){event.preventDefault();setConnectionDemo('waiting');return;}
    if(event.key==='Enter'||event.key===' ')return;
    event.preventDefault();event.stopImmediatePropagation();return;
  }
  if(model.focusEditing){focusEditorKey(event);return;}
  if (!['ArrowLeft','ArrowRight','Enter',' '].includes(event.key) || event.target.closest('input,select')) return;
  if(model.touch(performance.now())==='wake'){event.preventDefault();syncChrome();return;}
  if (event.key === 'ArrowLeft' || event.key === 'ArrowRight') { event.preventDefault(); navigate(event.key === 'ArrowLeft' ? -1 : 1); }
}, true);
const slider = document.getElementById('volume-slider');
slider.addEventListener('pointerdown', () => { model.dragging = true; });
slider.addEventListener('input', () => {
  model.touch(performance.now()); model.command('volume-set',Number(slider.value));
  document.getElementById('volume-value').textContent = `${model.volume}%`;
  // Preserve the same input node while dragging; do not rebuild its popup.
  renderCard();
});
document.getElementById('scenario').addEventListener('change', event => { model.setScenario(event.target.value); model.volumeOpen = false; renderCard(); });
let lastFrame = performance.now(), petActivityMs = 0, lastSecond = -1, lastSleepFrame = -1;
const reducedMotion = matchMedia('(prefers-reduced-motion: reduce)');
function animate() {
  // Use the same monotonic clock as interaction timing, not the RAF argument.
  const now = performance.now();
  const dt = Math.max(0,Math.min(100,now-lastFrame)); lastFrame = now;
  const wasIdle=model.idle;model.tick(now);if(wasIdle!==model.idle)syncChrome();
  paintFocus(now);
  if (Math.floor(now/1000) !== lastSecond) {
    lastSecond = Math.floor(now/1000); tickClock();
  }
  if(!model.idle)paintPet(now,dt);
  else if(Math.floor(now/200)!==lastSleepFrame){lastSleepFrame=Math.floor(now/200);paintSleepZzz(now);}
  requestAnimationFrame(animate);
}
function paintSleepZzz(now) {
  document.querySelectorAll('.sleep-zzz span').forEach((letter,i)=>{
    const phase=Math.floor(((now+i*800)%2400)/200);
    const alpha=phase<3?phase*85:phase>8?(11-phase)*85:255;
    letter.style.transform=`translateY(${-phase}px)`;letter.style.opacity=String(alpha/255);
  });
}
function paintPet(now,dt) {
  const responding = now < model.petResponseUntil;
  if (!responding && petPress === null && !reducedMotion.matches) petActivityMs += dt;
  const motion = petMotion(petActivityMs);
  const resting = motion.resting;
  pet.dataset.motion = responding ? 'response' : petPress !== null ? 'pressed' : reducedMotion.matches || resting ? 'rest' : 'walk';
  const position = reducedMotion.matches ? {x:590,y:596,left:false} : motion.position;
  pet.style.left = `${position.x}px`; pet.style.top = `${position.y}px`;
  bubble.hidden = model.focusEditing||model.connectionModal||(!responding&&!model.focus.notice);
  bubble.style.width=model.focus.notice?'104px':'64px';
  bubble.textContent=model.focus.notice?'休息一下':'喵～';
  if(responding||model.focus.notice) {
    const bx=Math.max(8,Math.min(720-8-bubble.offsetWidth,position.x+6));
    bubble.style.left=`${bx-position.x}px`;
  }
  const frame = responding ? 4+(Math.floor(now/350)%2) : reducedMotion.matches || resting ? (Math.floor(now/900)%7 === 0 ? 1 : 0) : 2+(Math.floor(now/220)%2);
  sprite.style.backgroundPosition = `${-(frame%4)*72}px ${-Math.floor(frame/4)*72}px`;
  sprite.style.transform = position.left && frame < 4 ? 'scaleX(-1)' : '';
}
function paintFocus(now){
  const f=model.focus,remaining=f.remaining(now),busy=['running','paused'].includes(f.state);
  const marker=document.querySelector('[data-card="focus"].nav-item');
  const badge=f.notice?'done':busy?f.state:'idle';if(marker.dataset.timer!==badge)marker.dataset.timer=badge;
  document.getElementById('focus-notice').hidden=!(model.idle&&f.notice);
  if(model.card!=='focus'||model.idle)return;
  const setText=(id,text)=>{const node=document.getElementById(id);if(node.textContent!==text)node.textContent=text;};
  setText('focus-time',`${String(Math.floor(remaining/60)).padStart(2,'0')}:${String(remaining%60).padStart(2,'0')}`);
  setText('focus-status',{idle:'准备专注',running:'专注中',paused:'已暂停',done:'专注完成'}[f.state]);
  setText('focus-hint','剩余专注时间');
  const progress=document.getElementById('focus-progress');progress.style.width=`${100*remaining/(f.minutes*60)}%`;
  for(const preset of document.querySelectorAll('.focus-presets [data-minutes]')){
    preset.disabled=busy;preset.setAttribute('aria-pressed',String(Number(preset.dataset.minutes)===f.minutes));
  }
  const custom=document.getElementById('focus-custom');custom.disabled=busy;
  const isCustom=![25,45,60].includes(f.minutes);custom.setAttribute('aria-pressed',String(isCustom));
  custom.textContent=isCustom?`自定义 · ${f.minutes} 分`:'自定义';
  const start=document.getElementById('focus-toggle');start.dataset.action=f.state==='running'?'focus-pause':'focus-start';
  start.textContent={idle:'开始',running:'暂停',paused:'继续',done:'再来一轮'}[f.state];
  document.querySelector('[data-action="focus-end"]').disabled=f.state==='idle';
}
const focusEditor=document.getElementById('focus-editor'),focusInput=document.getElementById('focus-minutes');
const bleEditor=document.getElementById('ble-editor');
let focusReplace=true;
document.querySelector('.focus-key-grid').innerHTML=['1','2','3','4','5','6','7','8','9','清空','0','退格'].map(key=>`<button data-focus-key="${key}" aria-label="${key}">${key==='退格'?'⌫':key}</button>`).join('');
function syncFocusEditor(){
  focusEditor.hidden=!model.focusEditing;
  for(const child of root.children)if(child!==focusEditor&&child!==bleEditor)child.inert=model.focusEditing||model.connectionModal;
  focusEditor.inert=model.connectionModal;bleEditor.inert=model.focusEditing;
}
function setConnectionDemo(value,feedback=''){
  connectionDemo=value;document.getElementById('connection-demo').value=value;
  const pair=value==='pairing',recover=value==='recovery';
  model.setConnectionModal(pair||recover,performance.now());
  bleEditor.hidden=!model.connectionModal;
  bleEditor.classList.toggle('recovery',recover);
  document.querySelector('#ble-heading span').textContent=pair?'蓝牙配对':'重新配对';
  document.getElementById('ble-detail').innerText=pair?'请核对电脑与 Luna 上的六位数字\n一致后，在两端分别确认':'电脑可能已移除了 Luna，\n但设备仍保留此前的配对。\n仅在需要重新配对时继续。';
  document.getElementById('ble-note').innerText=pair?'请在 28 秒内确认\n数字不一致，请拒绝本次配对':'只移除此电脑的旧配对，其他配对不受影响\n随后在 Windows 中重新添加 Luna';
  const notice=document.getElementById('ble-feedback');
  notice.textContent=feedback||(value==='error'?'蓝牙连接未完成，请稍后重试':'');notice.hidden=!notice.textContent;
  swipe=null;petPress=null;syncFocusEditor();syncChrome();
  if(model.connectionModal)bleEditor.querySelector(`[data-ble-action="${pair?'accept':'recover'}"]`).focus({preventScroll:true});
}
document.getElementById('connection-demo').addEventListener('change',event=>setConnectionDemo(event.target.value));
function openFocusEditor(){
  if(!model.openFocusEditor())return;
  focusInput.value=String(model.focus.minutes);focusReplace=true;
  document.getElementById('focus-editor-error').textContent='';swipe=null;petPress=null;model.dragging=false;
  syncFocusEditor();
}
function editFocusKey(key){
  if(key==='清空')focusInput.value='';
  else if(key==='退格')focusInput.value=focusInput.value.slice(0,-1);
  else if(/^\d$/.test(key))focusInput.value=((focusReplace?'':focusInput.value)+key).slice(0,3);
  focusReplace=false;document.getElementById('focus-editor-error').textContent='';
}
function confirmFocusMinutes(){
  if(!model.setFocusMinutes(Number(focusInput.value),performance.now())){document.getElementById('focus-editor-error').textContent='请输入 1-999 分钟';return;}
  syncFocusEditor();renderCard();
}
function handleFocusEditor(event){
  const button=event.target.closest('button');event.stopImmediatePropagation();
  if(!button)return;
  if(button.hasAttribute('data-focus-key'))editFocusKey(button.dataset.focusKey);
  else if(button.hasAttribute('data-focus-cancel')){model.closeFocusEditor(performance.now());syncFocusEditor();}
  else if(button.hasAttribute('data-focus-confirm'))confirmFocusMinutes();
}
function focusEditorKey(event){
  if(event.key==='Tab')return;
  event.preventDefault();event.stopImmediatePropagation();
  if(/^\d$/.test(event.key))editFocusKey(event.key);
  else if(event.key==='Backspace')editFocusKey('退格');
  else if(event.key==='Escape'){model.closeFocusEditor(performance.now());syncFocusEditor();}
  else if(event.key==='Enter')confirmFocusMinutes();
}
new ResizeObserver(entries => { root.style.transform = `scale(${entries[0].contentRect.width / 720})`; }).observe(root.parentElement);
renderCard(); paintPet(performance.now(),0); requestAnimationFrame(animate);
