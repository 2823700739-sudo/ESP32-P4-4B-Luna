// Offline browser regression. Uses an existing Playwright installation; no radio/device access.
const { chromium } = require('playwright');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const html = fs.readFileSync(path.join(__dirname,'../firmware/luna-panel/main/web/setup.html'),'utf8');

async function main() {
  const browser = await chromium.launch({headless:true, executablePath:process.env.LUNA_PREVIEW_BROWSER});
  let checks=0;
  async function scenario(mode,run) {
    const context=await browser.newContext({viewport:{width:390,height:844},isMobile:true,hasTouch:true});
    const requests=[],errors=[];
    let scanReads=0,scanPosts=0;
    let state={setup_id:'previous',saved:true,state:2,error:0,wifi_error:0,online:true,clock_ready:true,weather_fresh:false,remaining_seconds:600};
    await context.route('**/*',async route=>{
      const request=route.request(),url=new URL(request.url());
      assert.equal(url.origin,'http://192.168.4.1');
      if(url.pathname==='/')return route.fulfill({contentType:'text/html',body:html});
      requests.push({path:url.pathname,body:request.postDataJSON(),headers:request.headers()});
      const json=value=>route.fulfill({contentType:'application/json',body:JSON.stringify(value)});
      if(url.pathname==='/api/session')return json({token:'test-token'});
      assert.equal(request.headers()['x-luna-token'],'test-token');
      if(url.pathname==='/api/status')return json(state);
      if(url.pathname==='/api/scan'){
        if(request.method()==='POST'){
          scanPosts++;scanReads=0;
          if(mode==='scan-busy'&&scanPosts===1)return route.fulfill({status:503,body:'Busy'});
          return json({accepted:true});
        }
        scanReads++;
        if(scanReads===1)return json({state:1,networks:[]});
        if(mode==='scan-fail')return json({state:3,error:-1,networks:[]});
        return json({state:2,networks:mode==='scan-empty'?[]:[
          {ssid:'家里 <img src=x onerror=alert(1)>',secured:true,rssi:-40},
          {ssid:'访客网络',secured:false,rssi:-80}]});
      }
      if(url.pathname==='/api/network')return json({accepted:true});
      if(url.pathname==='/api/cities') {
        if(mode==='city-down')return route.fulfill({status:503,body:'City search unavailable.'});
        return json({results:[{name:'同名城市',admin1:'甲省',country:'示例国',latitude:1,longitude:2},{name:'同名城市',admin1:'乙省',country:'示例国',latitude:3,longitude:4}]});
      }
      if(url.pathname==='/api/save'){
        if(mode==='rejected')return route.fulfill({status:400,body:'Invalid configuration.'});
        if(mode!=='uncertain')state={...state,setup_id:request.postDataJSON().setup_id,saved:true,state:2,weather_fresh:true};
        if(mode==='lost' || mode==='uncertain')return route.abort();
        return json({accepted:true});
      }
      if(url.pathname==='/api/close')return json({closing:true});
      throw Error('Unexpected request '+url.pathname);
    });
    try {
      const page=await context.newPage();page.on('pageerror',e=>errors.push(e.message));
      await page.goto('http://192.168.4.1');await page.waitForFunction(()=>!document.getElementById('connect').disabled);
      const fill=async()=>{await page.locator('#ssid').fill('Example network');await page.locator('#password').fill('example-only');await page.locator('#manual').evaluate(e=>e.open=true);await page.locator('#location').fill('示例城市');await page.locator('#latitude').fill('0');await page.locator('#longitude').fill('2');};
      await run({page,requests,fill});
      assert.deepEqual(errors,[]);
      assert.equal(await page.evaluate(()=>localStorage.length),0);
      assert(requests.every(r=>!r.path.includes('music')&&!r.path.includes('media')));
      checks++;
    } finally {await context.close();}
  }
  try {
    await scenario('scan',async({page,requests})=>{
      assert.equal(requests.filter(r=>r.path==='/api/scan').length,0);
      await page.locator('#password').fill('old-password');await page.locator('#scan').click();
      assert.equal(await page.locator('#connect').isDisabled(),true);
      await page.waitForFunction(()=>document.getElementById('networks').options.length===3);
      assert.equal(await page.locator('#networks').inputValue(),'');
      await page.locator('#networks').selectOption('0');
      assert.equal(await page.locator('#ssid').inputValue(),'家里 <img src=x onerror=alert(1)>');
      assert.equal(await page.locator('#password').inputValue(),'');
      assert.equal(await page.locator('img').count(),0);
      assert.equal(requests.filter(r=>r.path==='/api/network'||r.path==='/api/save').length,0);
      await page.locator('#networks').selectOption('1');
      assert.equal(await page.locator('#password').getAttribute('placeholder'),'此网络无需密码');
      assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
      if(process.env.LUNA_PORTAL_SCREENSHOT)await page.screenshot({path:process.env.LUNA_PORTAL_SCREENSHOT,fullPage:true});
      await page.locator('#networks').selectOption('0');await page.locator('#ssid').fill('家里的Wi-Fi');
      await page.locator('#password').fill('example-only');await page.locator('#connect').click();
      await page.waitForFunction(()=>!document.getElementById('connect').disabled);
      assert.equal(requests.find(r=>r.path==='/api/network').body.ssid,'家里的Wi-Fi');
    });
    for(const mode of ['scan-empty','scan-fail'])await scenario(mode,async({page,fill,requests})=>{
      await page.locator('#scan').click();await page.waitForFunction(()=>!document.getElementById('scan').disabled);
      assert.equal(await page.locator('#networksLabel').isVisible(),false);
      assert((await page.locator('#scanStatus').innerText()).includes(mode==='scan-empty'?'没有找到':'扫描未完成'));
      await fill();await page.locator('#connect').click();await page.waitForFunction(()=>!document.getElementById('connect').disabled);
      assert.equal(requests.filter(r=>r.path==='/api/network').length,1);
    });
    await scenario('scan-busy',async({page,requests})=>{
      await page.locator('#scan').click();await page.waitForFunction(()=>!document.getElementById('scan').disabled);
      assert((await page.locator('#scanStatus').innerText()).includes('设备忙'));
      await page.locator('#scan').click();await page.waitForFunction(()=>document.getElementById('networks').options.length===3);
      assert.equal(requests.filter(r=>r.path==='/api/scan'&&r.body!==null).length,2);
    });
    await scenario('normal',async({page,requests,fill})=>{
      assert(!((await page.locator('#status').innerText()).includes('设置已保存')));
      assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
      assert.equal(await page.locator('#choicesLabel').isVisible(),false);
      if(process.env.LUNA_PORTAL_SCREENSHOT)await page.screenshot({path:process.env.LUNA_PORTAL_SCREENSHOT.replace(/\.png$/,'-initial.png'),fullPage:true});
      await fill();await page.locator('#connect').click();await page.waitForFunction(()=>!document.getElementById('connect').disabled);
      await page.locator('#query').fill('同名城市');await page.locator('#find').click();
      await page.locator('#choices option').nth(2).waitFor({state:'attached'});
      assert.equal(await page.locator('#choices').inputValue(),'');
      await page.locator('#choices').selectOption('1');assert.equal(await page.locator('#latitude').inputValue(),'3');
      await page.locator('#save').click();await page.waitForFunction(()=>document.getElementById('status').textContent.includes('最新天气已就绪'));
      assert.equal(requests.filter(r=>r.path==='/api/save').length,1);assert.equal(await page.locator('#password').inputValue(),'');
    });
    await scenario('lost',async({page,requests,fill})=>{
      await fill();await page.locator('#save').click();await page.waitForFunction(()=>document.getElementById('status').textContent.includes('设置已保存'));
      await page.evaluate(()=>poll());assert.equal(requests.filter(r=>r.path==='/api/save').length,1);
    });
    await scenario('uncertain',async({page,requests,fill})=>{
      await fill();await page.locator('#save').click();await page.waitForFunction(()=>document.getElementById('notice').textContent.includes('保存未确认'));
      await page.evaluate(()=>poll());assert.equal(await page.locator('#save').isDisabled(),true);
      assert(!(await page.locator('#status').innerText()).includes('设置已保存'));assert.equal(requests.filter(r=>r.path==='/api/save').length,1);
    });
    await scenario('rejected',async({page,fill})=>{
      await fill();await page.locator('#save').click();await page.waitForFunction(()=>document.getElementById('notice').textContent.includes('输入格式'));
      assert.equal(await page.locator('#save').isDisabled(),false);
    });
    await scenario('city-down',async({page,requests,fill})=>{
      await fill();await page.locator('#query').fill('Example');await page.locator('#find').click();
      await page.waitForFunction(()=>document.getElementById('notice').textContent.includes('手动填写'));
      await page.locator('#save').click();await page.waitForFunction(()=>document.getElementById('status').textContent.includes('设置已保存'));
      assert.equal(requests.find(r=>r.path==='/api/save').body.latitude,0);
    });
    await scenario('normal',async({page,requests,fill})=>{
      await fill();await page.locator('#latitude').fill('91');await page.locator('#save').click();
      await page.waitForFunction(()=>document.getElementById('notice').textContent.includes('范围'));
      assert.equal(requests.filter(r=>r.path==='/api/save').length,0);
    });
    console.log(`${checks} offline phone-browser scenarios passed. Requests mocked; no hardware acceptance.`);
  } finally {await browser.close();}
}
main().catch(error=>{console.error(error);process.exitCode=1;});
