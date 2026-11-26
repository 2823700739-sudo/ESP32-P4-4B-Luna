// Render 16:9 publication art from the original, code-native Luna website preview.
// Requires an existing Playwright installation via NODE_PATH; installs nothing.
const { chromium } = require('playwright');
const fs = require('node:fs');
const path = require('node:path');
const http = require('node:http');
const crypto = require('node:crypto');
const docs = path.resolve(__dirname, '../docs');
const output = path.resolve(process.env.LUNA_HUB_IMAGE_DIR || path.join(docs, 'images/hub-16x9'));
const scenes = [
  ['00-cover', '繁星与像素猫的\n桌面伙伴', '五张卡片，一只小猫。\n把时间、音乐和电脑状态，\n留在抬眼可见的地方。', '时钟 / 音乐 / 天气 / 额度 / 电脑', null],
  ['01-clock', '抬眼，\n就能看见时间。', '日期与时间清晰呈现。\n连接 Windows 后自动校时，\n当前采用北京时间。', '01 / 时钟', null],
  ['02-music', '音乐控制，\n留在桌面。', '查看歌名、歌手与播放状态。\n轻触播放、暂停、上一首和下一首。', '02 / 音乐', '音乐卡'],
  ['03-volume', '音量和静音，\n触手可及。', '点击音量按钮展开竖向滑条。\n调节 Windows 系统音量，\n点击顶部按钮切换静音。', '02 / 系统音量', '音乐卡'],
  ['04-weather', '天气小卡，\n多一点温度。', '首次向导填写 Wi-Fi 和城市。\n保存后由设备独立更新天气，\n无需电脑转发天气数据。', '03 / 天气 · 新版配置候选', '天气卡'],
  ['05-codex', '工作状态，\n一眼可见。', '查看 Codex 剩余额度，\n以及前台 VS Code 工程名。\n额度功能需要已有 Codex 环境。', '04 / 额度与工程', '额度卡'],
  ['06-computer', '电脑状态，\n不用反复切窗。', '查看 CPU、GPU、内存与显存。\n通过 Windows 系统接口采集，\n无需额外监控驱动。', '05 / 电脑状态', '电脑卡'],
  ['07-standby', '安静陪伴，\n也会回应你。', '三分钟无操作进入指针时钟。\n顶部空白也可手动切换，\n轻触屏幕即可返回。', '待机 / 像素猫', '电脑卡'],
];

async function main() {
  fs.mkdirSync(output, { recursive:true });
  const mime = {'.html':'text/html; charset=utf-8','.css':'text/css','.mjs':'text/javascript','.js':'text/javascript','.svg':'image/svg+xml','.png':'image/png'};
  const server = http.createServer((req,res) => {
    try {
      const pathname = decodeURIComponent(new URL(req.url, 'http://localhost').pathname);
      const file = path.resolve(docs, '.' + pathname + (pathname.endsWith('/') ? 'index.html' : ''));
      if (!file.startsWith(docs + path.sep) || !fs.statSync(file).isFile()) { res.writeHead(404).end(); return; }
      res.setHeader('Content-Type', mime[path.extname(file)] || 'application/octet-stream');
      fs.createReadStream(file).pipe(res);
    } catch { res.writeHead(404).end(); }
  });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  let browser;
  const results = [];
  try {
    browser = await chromium.launch({headless:true, ...(process.env.LUNA_PREVIEW_BROWSER ? {executablePath:process.env.LUNA_PREVIEW_BROWSER} : {})});
    const page = await browser.newPage({viewport:{width:1920,height:1080}, deviceScaleFactor:1, timezoneId:'Asia/Shanghai', reducedMotion:'reduce'});
    const errors = [];
    page.on('pageerror', e=>errors.push(e.message));
    await page.clock.setFixedTime(new Date('2026-10-08T20:08:00+08:00'));
    await page.goto(`http://127.0.0.1:${server.address().port}/design/preview/`);
    await page.locator('#digital-hm').waitFor();
    await page.evaluate(()=>document.fonts.ready);
    await page.selectOption('#scenario','normal');
    await page.addStyleTag({content:`
      html, body {width:1920px!important;height:1080px!important;overflow:hidden!important;}
      body {background:radial-gradient(ellipse at 65% 45%,#243150 0%,#111c30 43%,#0b1321 90%)!important;}
      body::before {content:'';position:absolute;inset:0;background:url('./assets/luna-starfield.svg') center/1080px;opacity:.25;pointer-events:none;}
      .preview-shell {display:block!important;position:absolute!important;left:940px!important;top:180px!important;width:720px!important;height:720px!important;min-height:0!important;max-width:none!important;margin:0!important;padding:0!important;}
      .studio,.stage-caption {display:none!important;}
      .stage,.screen-wrap {width:720px!important;height:720px!important;}
      .screen-wrap {border-radius:32px!important;box-shadow:0 40px 100px #0008,0 0 0 2px #52627a55,0 0 0 12px #111c2c!important;}
      .display {transform:scale(1)!important;}
      #hub-copy {position:absolute;left:260px;top:212px;width:610px;color:#eef3fb;font-family:'Microsoft YaHei','Segoe UI',sans-serif;}
      #hub-brand {font-family:'Segoe UI',sans-serif;font-size:30px;letter-spacing:9px;color:#c4b5ff;}
      #hub-title {font-size:66px;font-weight:500;line-height:1.35;letter-spacing:1px;margin:38px 0 28px;white-space:pre-line;}
      #hub-description {font-size:26px;line-height:1.95;color:#a7b6cc;white-space:pre-line;margin:0;}
      #hub-badge {display:inline-block;margin-top:38px;padding:12px 20px;border:1px solid #7185a144;border-radius:24px;color:#d2c7ff;font-size:19px;}
      #hub-footer {position:absolute;left:260px;right:260px;bottom:62px;display:flex;justify-content:space-between;align-items:center;color:#74849c;font:18px 'Microsoft YaHei','Segoe UI',sans-serif;}
    `});
    await page.evaluate(()=>{
      const box=document.createElement('section'); box.id='hub-copy';
      for(const [tag,id] of [['div','hub-brand'],['h1','hub-title'],['p','hub-description'],['div','hub-badge']]) { const e=document.createElement(tag);e.id=id;box.append(e); }
      box.firstChild.textContent='LUNA';document.body.append(box);
      const footer=document.createElement('footer');footer.id='hub-footer';
      for(const text of ['ESP32-P4-WIFI6-Touch-LCD-4B  ·  720 × 720','网站预览 · 示例数据 · 非实机照片']) {const e=document.createElement('span');e.textContent=text;footer.append(e);}
      document.body.append(footer);
    });
    for (const [name,title,description,badge,button] of scenes) {
      if (button) await page.getByRole('button',{name:button,exact:true}).click();
      if(name==='03-volume') await page.locator('[data-action=volume-open]').click();
      if(name==='07-standby') {
        await page.locator('#display').click({position:{x:360,y:100}});
        await page.locator('#idle-screen').waitFor({state:'visible'});
      }
      await page.evaluate(({title,description,badge})=>{
        document.getElementById('hub-title').textContent=title;
        document.getElementById('hub-description').textContent=description;
        document.getElementById('hub-badge').textContent=badge;
      },{title,description,badge});
      const bounds=await page.locator('#display').boundingBox();
      if(bounds.x!==940 || bounds.y!==180 || bounds.width!==720 || bounds.height!==720) throw new Error('Screen layout changed; refusing cropped export.');
      const file=path.join(output,name+'.png');
      await page.screenshot({path:file,animations:'disabled'});
      const data=fs.readFileSync(file);
      if(data.readUInt32BE(16)!==1920 || data.readUInt32BE(20)!==1080) throw new Error('Unexpected PNG dimensions');
      results.push({file:path.basename(file),width:1920,height:1080,bytes:data.length,sha256:crypto.createHash('sha256').update(data).digest('hex')});
      console.log('Rendered '+path.basename(file)+' 1920x1080');
    }
    if(errors.length) throw new Error(errors.join('\n'));
    fs.writeFileSync(path.join(output,'manifest.json'),JSON.stringify({source:'docs/design/preview/',kind:'website preview, demo data; not hardware photos',uiChanged:false,images:results},null,2)+'\n');
  } finally {
    if(browser) await browser.close();
    await new Promise(resolve=>server.close(resolve));
  }
}
main().catch(e=>{console.error(e);process.exitCode=1;});
