// Preview-only state. No BLE, media, sensors, or PC-agent calls are made here.
export const CARDS = Object.freeze(['clock', 'music', 'weather', 'codex', 'computer']);
export const SCENARIOS = Object.freeze(['normal', 'offline', 'empty', 'stale', 'partial', 'long', 'ble-off', 'wifi-off']);
export const IDLE_MS = 180000;
export function clockAngles(date) {
  return {hour:(date.getHours()%12)*30+date.getMinutes()*.5+date.getSeconds()/120,
    minute:date.getMinutes()*6+date.getSeconds()*.1,second:date.getSeconds()*6};
}

export function percent(value) {
  return typeof value === 'number' && Number.isFinite(value) ? Math.max(0, Math.min(100, value)) : null;
}

export function petPosition(elapsedMs) {
  // A first animation-frame timestamp can predate initialization. Never allow
  // negative/invalid elapsed time to become a negative/NaN array index.
  elapsedMs = Number.isFinite(elapsedMs) ? Math.max(0,elapsedMs) : 0;
  // Four rectangular rails outside (65,135,590,450); a 48x48 hit box stays clear.
  const rails = [[90, 85], [600, 85], [660, 85], [660, 150], [660, 530], [660, 596], [600, 596], [90, 596], [8, 596], [8, 530], [8, 150], [8, 85]];
  const index = Math.floor(elapsedMs / 6500) % rails.length;
  const t = (elapsedMs % 6500) / 6500;
  const a = rails[index], b = rails[(index + 1) % rails.length];
  // Axis-aligned outside corners do not enter the card rectangle.
  return { x: a[0] + (b[0] - a[0]) * t, y: a[1] + (b[1] - a[1]) * t, left: b[0] < a[0] };
}

export function petMotion(activeMs) {
  activeMs = Number.isFinite(activeMs) ? Math.max(0,activeMs) : 0;
  const walkingMs = 13000, cycleMs = 16000;
  const phaseMs = activeMs % cycleMs;
  // A 3-second rest advances the activity phase, but never the path distance.
  // Both sides of the cycle boundary therefore resolve to the same position.
  const travelMs = Math.floor(activeMs / cycleMs) * walkingMs + Math.min(phaseMs,walkingMs);
  return { resting:phaseMs >= walkingMs, travelMs, position:petPosition(travelMs) };
}

export function demoSnapshot(scenario = 'normal') {
  const empty = scenario === 'empty';
  const offline = scenario === 'offline';
  const stale = scenario === 'stale';
  const partial = scenario === 'partial';
  const long = scenario === 'long';
  const pc = !empty && !offline && scenario !== 'ble-off';
  const wifi = !offline && scenario !== 'wifi-off';
  return {
    pc, wifi, stale: stale || offline || scenario === 'ble-off',
    timeValid: !empty, timeSource: pc ? 'BLE' : (wifi ? 'NTP' : 'LOCAL'),
    music: { available: !empty, title: long ? '写给月亮的一封很长很长的信 · 桌面陪伴特别版' : '在月光下漫步', artist: long ? 'Luna Studio / 像素宇宙乐团与夜行旅人' : 'Luna Studio', },
    weather: { available: !empty, cached: !wifi || stale, temperature: 23, feels: 22, humidity: 64, wind: 2.1, city: long ? '上海 · 浦东新区 · 张江科技园' : '上海', condition: '多云', updated: '10:24' },
    quota: { primary: empty ? null : 72, weekly: empty || partial ? null : 41, updated: empty ? '--' : '10:24' },
    project: { available: !empty, name: long ? 'Luna桌面伴侣-蓝牙无线版-多工作区界面与资源验证工程' : 'ESP32-P4-4B-Luna', stale: stale || !pc, source: 'VS Code', updated: '10:24' },
    computer: { available: !empty, cpu: empty ? null : 37, gpu: empty || partial ? null : 24, cpuTemp: empty || partial ? null : 52, gpuTemp: empty || partial ? null : 46, ram: empty ? null : 43, vram: empty || partial ? null : 31, ramLabel: '13.8 / 32 GB', vramLabel: '2.5 / 8 GB', name: empty ? '等待电脑连接' : long ? 'Luna-Desktop · 示例主显卡名称过长时限定显示区域' : 'Luna-Desktop · 主 GPU' },
  };
}

export class PreviewModel {
  constructor(now = 0) {
    this.card = 'clock';
    this.scenario = 'normal';
    this.playing = false;
    this.trackIndex = 0;
    this.volume = 42;
    this.muted = false;
    this.volumeOpen = false;
    this.dragging = false;
    this.lastTouch = now;
    this.petResponseUntil = 0;
    this.actions = 0;
    this.idle = false;
  }
  get data() { return demoSnapshot(this.scenario); }
  get canControl() { const d = this.data; return d.pc && !d.stale && d.music.available; }
  setScenario(value) { if (SCENARIOS.includes(value)) this.scenario = value; }
  touch(now) {
    this.lastTouch = now;
    if (this.idle) { this.idle=false; return 'wake'; }
    return 'active';
  }
  enterIdle() { this.idle=true; this.volumeOpen=false; this.dragging=false; }
  tick(now) { if(!this.idle&&!this.dragging&&now-this.lastTouch>=IDLE_MS) this.enterIdle(); }
  navigate(delta) {
    const i = CARDS.indexOf(this.card);
    this.card = CARDS[(i + delta % CARDS.length + CARDS.length) % CARDS.length];
    this.volumeOpen = false;
    return true;
  }
  chooseCard(card) { if (CARDS.includes(card)) { this.card = card; this.volumeOpen = false; } }
  command(action, value) {
    if(this.idle) return false;
    // Outside clicks close the popup AND still execute the intended action.
    if (!['volume-set','mute'].includes(action)) this.volumeOpen = false;
    if (action === 'pet') { this.petResponseUntil = value + 1600; return true; }
    if (!this.canControl) return false;
    switch (action) {
      case 'toggle': this.playing = !this.playing; break;
      case 'next': this.trackIndex = (this.trackIndex + 1) % 3; break;
      case 'previous': this.trackIndex = (this.trackIndex + 2) % 3; break;
      case 'volume-open': this.volumeOpen = true; return true;
      case 'volume-set': if (percent(value) === null) return false; this.volume = percent(value); break;
      case 'mute': this.muted = !this.muted; break;
      default: return false;
    }
    this.actions++;
    return true;
  }
  beginDrag(now) { this.touch(now); this.dragging = true; return true; }
  endDrag(now) { this.dragging = false; this.lastTouch = now; }
}
