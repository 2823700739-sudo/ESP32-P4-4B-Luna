import test from 'node:test';
import assert from 'node:assert/strict';
import { CARDS, PreviewModel, FocusTimer, demoSnapshot, percent, petPosition, petMotion } from './model.mjs';

test('boot always starts at the clock card, not the last business card', () => {
  assert.equal(new PreviewModel().card, 'clock'); assert.equal(CARDS.length,6);
});
test('carousel wraps in both directions', () => {
  const m = new PreviewModel(); m.navigate(-1); assert.equal(m.card,'focus');
  m.navigate(1); assert.equal(m.card,'clock'); m.navigate(14); assert.equal(m.card,'weather');
});
test('two rapid toggles preserve the original paused state', () => {
  const m = new PreviewModel(); m.command('toggle'); m.command('toggle'); assert.equal(m.playing,false); assert.equal(m.actions,2);
});

test('focus preserves fractional time through pause and rejects an accidental new preset',()=>{
  const f=new FocusTimer();assert.equal(f.select(45),true);f.start(1000);
  assert.equal(f.start(2000),false);assert.equal(f.select(25),false);
  f.pause(2250);assert.equal(f.leftMs,2698750);assert.equal(f.remaining(9000000),2699);
  f.start(9000000);assert.equal(f.deadline,11698750);
  f.tick(11698750);assert.equal(f.state,'done');assert.equal(f.notice,true);
});
test('running focus blocks standby behind other cards and acknowledges completion once',()=>{
  const m=new PreviewModel();m.setScenario('offline');m.chooseCard('focus');
  assert.equal(m.command('focus-start',1000),true);const deadline=m.focus.deadline;
  m.chooseCard('music');m.tick(200000);assert.equal(m.idle,false);assert.equal(m.enterIdle(),false);
  m.tick(deadline);assert.equal(m.focus.state,'done');assert.equal(m.focus.notice,true);
  assert.equal(m.card,'music');assert.equal(m.actions,0);
  m.touch(deadline+1);m.chooseCard('focus');assert.equal(m.focus.notice,false);
  m.tick(deadline+1000);assert.equal(m.focus.notice,false);
});
test('focus uses a deadline after delayed ticks and cannot pause at zero',()=>{
  const f=new FocusTimer();f.start(1000);assert.equal(f.remaining(125500),1376);
  assert.equal(f.pause(1501000),false);assert.equal(f.state,'done');
  f.start(2000000);assert.equal(f.remaining(2000000),1500);
  f.end();assert.equal(f.state,'idle');assert.equal(f.notice,false);
});
test('focus reboot creates a fresh session and rejects unsupported presets',()=>{
  const f=new FocusTimer();assert.equal(f.select(1000),false);f.select(60);f.start(1000);
  const reboot=new FocusTimer();assert.equal(reboot.minutes,25);assert.equal(reboot.state,'idle');
});
test('custom editor locks navigation, media and standby until confirm or cancel',()=>{
  const m=new PreviewModel();m.chooseCard('focus');assert.equal(m.openFocusEditor(),true);
  assert.equal(m.navigate(1),false);m.chooseCard('music');assert.equal(m.card,'focus');
  assert.equal(m.enterIdle(),false);assert.equal(m.command('toggle',1),false);
  assert.equal(m.setFocusMinutes(0,10),false);assert.equal(m.focusEditing,true);
  assert.equal(m.setFocusMinutes(40,11),true);assert.equal(m.focus.minutes,40);assert.equal(m.focus.state,'idle');
  m.command('focus-start',100);assert.equal(m.openFocusEditor(),false);
  m.touch(200);m.command('focus-pause',200);assert.equal(m.enterIdle(),true);
  m.touch(300);m.command('focus-start',300);assert.equal(m.enterIdle(),false);
});
test('custom duration validates integer bounds and retains a long monotonic deadline',()=>{
  const f=new FocusTimer();for(const n of [0,1000,1.5,NaN,Infinity,'40'])assert.equal(f.select(n),false);
  for(const n of [1,40,65,999])assert.equal(f.select(n),true);
  f.start(1000);assert.equal(f.deadline,59941000);assert.equal(f.remaining(1000),59940);
  f.tick(f.deadline);assert.equal(f.notice,true);
});
test('two rapid toggles also preserve an original playing state', () => {
  const m = new PreviewModel(); m.playing=true; m.command('toggle'); m.command('toggle'); assert.equal(m.playing,true);
});
test('clicking play outside the volume popup closes it and still acts', () => {
  const m = new PreviewModel(); m.command('volume-open'); m.command('toggle'); assert.equal(m.volumeOpen,false); assert.equal(m.playing,true);
});
test('slider interaction keeps the popup open and clamps volume', () => {
  const m = new PreviewModel(); m.command('volume-open'); m.command('volume-set',120);
  assert.equal(m.volumeOpen,true); assert.equal(m.volume,100); m.command('volume-set',-5); assert.equal(m.volume,0);
});
test('non-numeric volume is rejected rather than becoming NaN', () => {
  const m = new PreviewModel(); assert.equal(m.command('volume-set',NaN),false); assert.equal(m.volume,42);
});
test('pet interaction closes volume and is independent of PC connection', () => {
  const m = new PreviewModel(); m.command('volume-open'); m.setScenario('offline'); m.command('pet',100);
  assert.equal(m.volumeOpen,false); assert.equal(m.petResponseUntil,1700); assert.equal(m.actions,0);
});
test('switching cards closes the volume popup', () => {
  const m = new PreviewModel(); m.command('volume-open'); m.navigate(1); assert.equal(m.volumeOpen,false);
});
test('offline, empty, and stale data all disable media commands', () => {
  for (const scenario of ['offline','empty','stale','ble-off']) { const m = new PreviewModel(); m.setScenario(scenario); assert.equal(m.command('next'),false); assert.equal(m.actions,0); }
});
test('Wi-Fi failure does not disable BLE music controls', () => {
  const m = new PreviewModel(); m.setScenario('wifi-off'); assert.equal(m.command('toggle'),true); assert.equal(m.data.weather.cached,true);
});

test('record and tonearm require current connected media, independent of command availability', () => {
  const m=new PreviewModel();m.playing=true;
  assert.equal(m.musicPlaying,true);
  for(const scenario of ['offline','ble-off','stale','empty']){
    m.setScenario(scenario);assert.equal(m.musicPlaying,false);
  }
  m.setScenario('wifi-off');assert.equal(m.musicPlaying,true);
  m.playing=false;assert.equal(m.musicPlaying,false);
});
test('BLE failure allows independent Wi-Fi weather and backup time', () => {
  const d=demoSnapshot('ble-off'); assert.equal(d.wifi,true); assert.equal(d.weather.cached,false); assert.equal(d.timeSource,'NTP');
});
test('offline warm device keeps a valid local clock, cold no-data does not', () => {
  assert.equal(demoSnapshot('offline').timeValid,true); assert.equal(demoSnapshot('empty').timeValid,false);
});
test('quota windows have independent null availability, not fake zero', () => {
  const d=demoSnapshot('partial'); assert.equal(d.quota.primary,72); assert.equal(d.quota.weekly,null); assert.equal(percent(d.quota.weekly),null);
});
test('unsupported sensors stay null, not zero', () => {
  const d=demoSnapshot('partial'); assert.equal(d.computer.gpu,null); assert.equal(d.computer.cpuTemp,null); assert.equal(d.computer.cpu,37);
});
test('project comes from the explicit demo VS Code source', () => {
  const d=demoSnapshot('normal'); assert.equal(d.project.source,'VS Code'); assert.ok(!d.project.name.includes('D:\\'));
});
test('three-minute automatic clock wakes without leaking an action', () => {
  const m=new PreviewModel(); m.chooseCard('music');
  m.tick(179999);assert.equal(m.idle,false);m.tick(180000);assert.equal(m.idle,true);
  assert.equal(m.command('toggle'),false);
  assert.equal(m.touch(180001),'wake'); assert.equal(m.card,'music');
  assert.equal(m.command('toggle'),true); assert.equal(m.playing,true);
  m.enterIdle();assert.equal(m.idle,true);assert.equal(m.touch(180002),'wake');
});

test('background data updates preserve the current card', () => {
  const m=new PreviewModel(); m.chooseCard('weather'); m.setScenario('partial');
  assert.equal(m.card,'weather'); assert.equal(m.lastTouch,0);
});

test('drag release clears gesture state without an inactivity deadline', () => {
  const m=new PreviewModel(); assert.equal(m.beginDrag(100),true);
  m.endDrag(3600000); assert.equal(m.dragging,false); assert.equal(m.lastTouch,3600000);
});

test('all pet hit boxes remain outside the card and within the 720 screen', () => {
  for (let t=0;t<12*6500;t+=33) {
    const {x,y}=petPosition(t); assert.ok(x>=0 && x+48<=720 && y>=0 && y+48<=720);
    const overlaps = x<655 && x+48>65 && y<585 && y+48>135; assert.equal(overlaps,false,`hit box at ${t}`);
  }
});
test('negative or invalid frame time cannot crash the pet path', () => {
  const origin=petPosition(0);
  for (const value of [-1,-1000,NaN,Infinity,undefined]) assert.deepEqual(petPosition(value),origin);
});
test('pet rests for 3 seconds without advancing its path', () => {
  const start=petMotion(13000);
  for (const t of [13000,14000,15000,15999]) {
    const motion=petMotion(t); assert.equal(motion.resting,true);
    assert.equal(motion.travelMs,13000); assert.deepEqual(motion.position,start.position);
  }
});
test('pet resumes from the rest position rather than skipping 3 seconds ahead', () => {
  const before=petMotion(15999),after=petMotion(16000),next=petMotion(16016);
  assert.deepEqual(after.position,before.position); assert.equal(after.resting,false);
  assert.equal(next.travelMs,13016); assert.ok(Math.hypot(next.position.x-before.position.x,next.position.y-before.position.y)<1.3);
});
test('all rest/resume boundaries remain continuous across multiple circuits', () => {
  for (let cycle=0;cycle<30;cycle++) {
    for (const offset of [13000,16000]) {
      const t=cycle*16000+offset;
      const a=petMotion(t-1).position,b=petMotion(t+1).position;
      assert.ok(Math.hypot(b.x-a.x,b.y-a.y)<.17,`cycle ${cycle}, boundary ${offset}`);
    }
  }
});
test('pet path speed never includes accumulated rest time', () => {
  let before=petMotion(0).position;
  for (let t=16;t<=160000;t+=16) {
    const after=petMotion(t).position;
    assert.ok(Math.hypot(after.x-before.x,after.y-before.y)<=1.3,`frame ${t}`);
    before=after;
  }
});
test('percent sanitization preserves unknowns and bounds available values', () => {
  for (const n of [null,undefined,NaN,Infinity,'72']) assert.equal(percent(n),null);
  assert.equal(percent(-2),0); assert.equal(percent(200),100); assert.equal(percent(72),72);
});
test('unknown preview scenarios cannot silently replace state', () => {
  const m=new PreviewModel(); m.setScenario('unexpected'); assert.equal(m.scenario,'normal');
});
