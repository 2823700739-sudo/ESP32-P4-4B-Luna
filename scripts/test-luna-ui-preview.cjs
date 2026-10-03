// Uses an externally supplied Playwright install. Nothing is npm-installed here.
const { chromium } = require('playwright');
const assert = require('node:assert/strict');
const path = require('node:path');
const fs = require('node:fs');

async function main() {
  const base = process.env.LUNA_PREVIEW_URL || 'http://127.0.0.1:8773/design/preview/';
  const executablePath = process.env.LUNA_PREVIEW_BROWSER;
  const browser = await chromium.launch({ headless:true, ...(executablePath ? {executablePath} : {}) });
  const errors = [], requests = [];
  const context = await browser.newContext({ viewport:{width:1280,height:930}, timezoneId:'Asia/Shanghai', reducedMotion:'reduce' });
  const page = await context.newPage();
  const output = path.resolve(__dirname,'../docs/design/preview/review');
  fs.mkdirSync(output,{recursive:true});
  let checks = 0;
  const check = (condition,label) => { assert.ok(condition,label); checks++; console.log(`PASS ${label}`); };
  page.on('pageerror', error => errors.push(error.message));
  page.on('request', request => requests.push(request.url()));
  try {
    await page.goto(base); await page.locator('#digital-hm').waitFor();
    check(await page.locator('#card').getAttribute('data-card') === 'clock','boot clock card');
    check((await page.locator('#digital-hm').textContent()).match(/^\d\d:\d\d$/),'local demo clock is populated');
    check(!await page.locator('#idle-screen').isVisible(),'standby clock hidden on boot');
    check(await page.locator('.card-kicker').evaluate(el=>getComputedStyle(el).fontSize)==='20px','clock corner caption enlarged');
    check(await page.locator('.nav-item').first().evaluate(el=>el.offsetWidth===64&&el.offsetHeight===58&&getComputedStyle(el).fontSize==='16px'),'menu buttons and labels enlarged');
    check(await page.locator('#device-footer').evaluate(el=>el.offsetTop===639),'bottom menu row moved up eight pixels');
    check(await page.locator('.nav-item span').evaluateAll(items=>items.every(el=>{
      const text=el.getBoundingClientRect(),button=el.parentElement.getBoundingClientRect(),screen=document.getElementById('display').getBoundingClientRect();
      return getComputedStyle(el).lineHeight==='24px'&&text.top>=button.top&&text.bottom<=button.bottom-2&&text.bottom<=screen.bottom;
    })),'all menu label line boxes fit buttons with bottom margin');
    const sprite = await page.evaluate(async () => {
      const image = new Image(); image.src='./assets/luna-mooncat-atlas.png'; await image.decode();
      return {width:image.naturalWidth,height:image.naturalHeight};
    });
    check(sprite.width / sprite.height === 2,'sprite atlas loads as 4x2 ratio');
    await page.locator('#display').screenshot({path:path.join(output,'clock.png')});
    await page.getByRole('button',{name:'音乐卡',exact:true}).click();
    const play = page.locator('[data-action=toggle]');
    check(await play.getAttribute('aria-label') === '播放','music starts paused');
    await play.click(); await play.click();
    check(await play.getAttribute('aria-label') === '播放','two rapid toggles return to paused');
    await page.locator('[data-action=volume-open]').click();
    check(await page.locator('#volume-popup').isVisible(),'volume popup opens');
    check(await page.locator('.music-bottom button').count() === 1,'one right-hand volume control only');
    check((await page.locator('.music-bottom').textContent()).trim() === '', 'no bottom playback/status labels');
    await page.locator('#volume-popup [data-action=mute]').click();
    check(await page.locator('[data-action=volume-open] use').getAttribute('href') === '#i-mute','muted main volume icon');
    check(await page.locator('#volume-popup').isVisible(),'mute keeps vertical popup open');
    await play.click();
    check(!await page.locator('#volume-popup').isVisible(),'outside play click closes volume');
    check(await play.getAttribute('aria-label') === '暂停','same outside click also plays');
    await page.locator('[data-action=volume-open]').click();
    const slider = page.locator('#volume-slider');
    await slider.fill('70'); await slider.dispatchEvent('input');
    check(await page.locator('#volume-value').textContent() === '70%','volume updates while popup stays open');
    await page.locator('#card .card-heading').click();
    check(!await page.locator('#volume-popup').isVisible(),'outside blank area closes popup');
    await page.locator('#display').screenshot({path:path.join(output,'music.png')});
    await page.getByRole('button',{name:'额度卡',exact:true}).click();
    check(await page.getByRole('progressbar').count() === 2,'two independent quota bars');
    check(await page.locator('.quota-info').count() === 0,'quota hints removed');
    check(!(await page.locator('.card-bottom').textContent()).includes('工程来源'),'quota footer source removed');
    check(await page.locator('.quota-section .progress').first().evaluate(el=>parseFloat(getComputedStyle(el).height)) === 22,'thicker quota bars');
    check(await page.getByRole('progressbar',{name:'5 小时剩余额度'}).getAttribute('aria-valuenow') === '72','remaining quota semantic');
    await page.locator('#display').screenshot({path:path.join(output,'codex.png')});
    await page.selectOption('#scenario','partial');
    check(await page.getByRole('progressbar',{name:'周剩余额度'}).getAttribute('aria-valuenow') === null,'unknown quota is not zero');
    check(await page.getByRole('progressbar',{name:'5 小时剩余额度'}).getAttribute('aria-valuenow') === '72','one missing quota does not hide the other');
    await page.getByRole('button',{name:'电脑卡',exact:true}).click();
    check((await page.locator('#card').textContent()).includes('温度不可用'),'unsupported temperatures are explicit');
    await page.locator('#display').screenshot({path:path.join(output,'partial.png')});
    await page.selectOption('#scenario','normal');
    await page.locator('#display').screenshot({path:path.join(output,'computer.png')});
    await page.getByRole('button',{name:'天气卡',exact:true}).click();
    check(await page.locator('.weather-detail').evaluateAll(items=>items.every(el=>{
      const label=el.querySelector('span'),value=el.querySelector('strong');
      return getComputedStyle(el).textAlign==='center'&&getComputedStyle(label).fontSize==='20px'&&getComputedStyle(value).fontSize==='28px'&&el.scrollHeight<=el.clientHeight;
    })),'all weather panels have larger centered labels and values without overflow');
    await page.selectOption('#scenario','ble-off');
    check((await page.locator('#card').textContent()).includes('Wi-Fi 独立联网'),'BLE offline still has independent weather');
    await page.locator('#display').screenshot({path:path.join(output,'weather.png')});
    await page.getByRole('button',{name:'音乐卡',exact:true}).click();
    check(await play.isDisabled(),'BLE offline disables media controls');
    await page.selectOption('#scenario','wifi-off'); check(!await play.isDisabled(),'Wi-Fi offline does not disable BLE controls');
    await page.selectOption('#scenario','stale'); check(await play.isDisabled(),'stale data disables controls');
    await page.selectOption('#scenario','empty');
    await page.getByRole('button',{name:'时钟卡',exact:true}).click();
    check(await page.locator('#digital-hm').textContent() === '--:--','invalid time shows placeholder');
    await page.selectOption('#scenario','long');
    for (const name of ['音乐卡','额度卡','电脑卡','天气卡']) {
      await page.getByRole('button',{name,exact:true}).click();
      check(await page.locator('#card').evaluate(el => el.scrollWidth <= el.clientWidth),'no horizontal overflow for '+name);
    }
    await page.getByRole('button',{name:'额度卡',exact:true}).click();
    await page.locator('#display').screenshot({path:path.join(output,'long.png')});
    for (const width of [320,720,1280]) {
      await page.setViewportSize({width,height:950});
      check(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth),`responsive page at ${width}px`);
    }
    await page.setViewportSize({width:1280,height:930});
    await page.selectOption('#scenario','normal');
    await page.getByRole('button',{name:'时钟卡',exact:true}).click();
    const box = await page.locator('#display').boundingBox();
    const x=box.x+500*box.width/720,y=box.y+450*box.width/720;
    await page.mouse.move(x,y); await page.mouse.down(); await page.mouse.move(x-130*box.width/720,y,{steps:5}); await page.mouse.up();
    check(await page.locator('#card').getAttribute('data-card') === 'music','horizontal pointer swipe navigates');
    // Virtual time verifies automatic idle, wake-only touch and manual entry.
    const timed = await context.newPage(); timed.on('pageerror',e=>errors.push(e.message)); timed.on('request',r=>requests.push(r.url()));
    await timed.clock.install(); await timed.goto(base); await timed.locator('#digital-hm').waitFor();
    await timed.getByRole('button',{name:'音乐卡',exact:true}).click();
    await timed.clock.fastForward(179000);
    check(!await timed.locator('#idle-screen').isVisible(),'not idle before three minutes');
    await timed.clock.fastForward(2000);
    check(await timed.locator('#idle-screen').isVisible(),'three-minute automatic analog clock');
    check(await timed.locator('#sleeping-pet').isVisible(),'sleeping cat visible in automatic clock');
    check(await timed.locator('#idle-message').textContent()==='慢慢来，你已经在向前了。','standby has one warm sentence');
    check(await timed.locator('#idle-source,.idle-hint').count()===0,'two technical standby captions removed');
    const firstZ=timed.locator('.sleep-zzz span').first();
    const firstZStyle=await firstZ.getAttribute('style');await timed.clock.fastForward(400);
    check(await firstZ.getAttribute('style')!==firstZStyle,'sleeping ZZZ rises and fades over time');
    check(await timed.locator('.clock-numbers text').evaluateAll(items=>items.every(el=>{
      const a=el.getBBox();return [a.x,a.x+a.width].every(x=>[a.y,a.y+a.height].every(y=>Math.hypot(x-220,y-220)<181));
    })),'clock number bounds stay inside major tick tips');
    await timed.locator('#idle-screen').click({position:{x:360,y:325}});
    check(!await timed.locator('#idle-screen').isVisible(),'touch wakes to previous card');
    check(await timed.locator('[data-action=toggle]').getAttribute('aria-label') === '播放','wake did not leak a playback action');
    await timed.clock.fastForward(400);
    await timed.locator('[data-action=toggle]').click();
    check(await timed.locator('[data-action=toggle]').getAttribute('aria-label') === '暂停','next deliberate click controls music');
    for(const position of [{x:400,y:550},{x:50,y:610},{x:680,y:600},{x:120,y:50},{x:540,y:60}]) {
      await timed.locator('#display').click({position});
      check(!await timed.locator('#idle-screen').isVisible(),`non-top-blank tap ${position.x},${position.y} does not enter clock`);
    }
    await timed.locator('#display').click({position:{x:400,y:120}});
    check(await timed.locator('#idle-screen').isVisible(),'blank tap enters manual clock');
    check(await timed.locator('#sleeping-pet').evaluate(el=>el.offsetLeft===624&&el.offsetTop===621&&el.offsetWidth===72&&el.offsetLeft+el.offsetWidth<=720&&el.offsetTop+el.offsetHeight<=720),'complete sleeping cat fits screen in manual clock');
    await timed.locator('#display').screenshot({path:path.join(output,'idle.png')});
    await timed.locator('#idle-screen').click({position:{x:360,y:325}});
    await timed.clock.fastForward(400);
    await timed.selectOption('#scenario','wifi-off');
    check(await timed.locator('#card').getAttribute('data-card') === 'music','background update preserves selected card');
    const animated = await browser.newPage({viewport:{width:1280,height:930}, reducedMotion:'no-preference'});
    animated.on('pageerror',e=>errors.push(e.message)); animated.on('request',r=>requests.push(r.url()));
    await animated.clock.install(); await animated.goto(base); await animated.locator('#digital-hm').waitFor();
    const petLeft = await animated.locator('#pet').evaluate(el=>el.style.left);
    await animated.clock.fastForward(1000);
    check(await animated.locator('#pet').evaluate(el=>el.style.left) !== petLeft,'pet roams with motion enabled');
    const petBox = await animated.locator('#pet').boundingBox();
    await animated.mouse.click(petBox.x+petBox.width/2,petBox.y+petBox.height/2);
    await animated.clock.fastForward(40);
    check(await animated.locator('#pet-bubble').isVisible(),'pet responds to a tap');
    check(await animated.locator('#pet-bubble').evaluate(el=>{
      const text=document.createRange(); text.selectNodeContents(el);
      const a=el.getBoundingClientRect(),b=text.getBoundingClientRect(),screen=document.getElementById('display').getBoundingClientRect();
      return b.left>=a.left&&b.right<=a.right&&b.top>=a.top&&b.bottom<=a.bottom&&a.left>=screen.left&&a.right<=screen.right;
    }),'pet greeting text fully fits bubble and screen');
    await animated.clock.fastForward(1700);
    check(!await animated.locator('#pet-bubble').isVisible(),'pet tap response is bounded');
    await animated.getByRole('button',{name:'音乐卡',exact:true}).click();
    check(await animated.locator('.record').evaluate(el=>getComputedStyle(el).animationPlayState) === 'paused','record stops while paused');
    await animated.locator('[data-action=toggle]').click();
    check(await animated.locator('.record').evaluate(el=>getComputedStyle(el).animationPlayState) === 'running','record rotates only while playing');
    check(await animated.locator('#card').evaluate(el=>{
      const frames=el.getAnimations().flatMap(a=>a.effect.getKeyframes());
      return frames.every(f=>f.opacity===undefined && !(f.transform||'').includes('scale'));
    }),'card transition avoids whole-card opacity and scaling');
    await animated.locator('[data-action=toggle]').click();
    check(await animated.locator('.record').evaluate(el=>getComputedStyle(el).animationPlayState) === 'paused','record stops again after pause');
    check(errors.length === 0,'no browser runtime errors');
    check(requests.every(url=>new URL(url).origin === new URL(base).origin),'no external or Agent requests');
    console.log(`Browser checks: ${checks} passed; screenshots: ${output}`);
  } finally { await browser.close(); }
}
main().catch(error=>{console.error(error);process.exitCode=1;});
