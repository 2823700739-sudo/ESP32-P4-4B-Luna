// Reproducible format conversion of the approved preview assets, not new artwork.
// NODE_PATH supplies sharp; lv_font_conv 1.5.3 is installed only in ignored .tools.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const {spawnSync} = require('node:child_process');
const sharp = require('sharp');
const repo = path.resolve(__dirname, '..');
const dest = path.join(repo, 'firmware/luna-panel/main/assets');
const tool = path.join(repo, '.tools/lv-font-conv/node_modules/lv_font_conv');
const opentype = require(path.join(tool, 'node_modules/opentype.js'));
const fontPath = path.join(repo, '.tools/lv-font-conv/NotoSansCJKsc-Regular.otf');
const sha = data => crypto.createHash('sha256').update(data).digest('hex');
const expectedFont = '2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b';
if (sha(fs.readFileSync(fontPath)) !== expectedFont) throw Error('Font source hash differs');
fs.mkdirSync(dest, {recursive:true});
const font = opentype.loadSync(fontPath);
const cmap = Object.keys(font.tables.cmap.glyphIndexMap).map(Number);
const inRange = (v,a,b) => v >= a && v <= b;
// Every Han character mapped by this font, including its supplementary-plane Han.
const full = cmap.filter(v => inRange(v,32,255) || inRange(v,0x2000,0x206f) ||
  inRange(v,0x3000,0x30ff) || inRange(v,0x3100,0x312f) || inRange(v,0x3400,0x9fff) ||
  inRange(v,0xf900,0xfaff) || inRange(v,0xff00,0xffef) || inRange(v,0x20000,0x2ffff));
function ranges(values) {
  const sorted = [...new Set(values)].sort((a,b)=>a-b), result=[];
  for(let i=0;i<sorted.length;i++) {
    const first=sorted[i];let last=first;
    while(sorted[i+1]===last+1) last=sorted[++i];
    result.push(first===last ? String(first) : `${first}-${last}`);
  }
  return result.join(',');
}
const ui = fs.readFileSync(path.join(repo,'firmware/luna-panel/main/luna_preview_ui.c'),'utf8') +
  fs.readFileSync(path.join(repo,'firmware/luna-panel/main/luna_weather_parse.c'),'utf8');
const small = [...ui].filter(c=>c.codePointAt(0)>127).map(c=>c.codePointAt(0));
const ascii = Array.from({length:95},(_,i)=>32+i);
const declarations=[];const manifest={fontSourceSha256:expectedFont,converter:'lv_font_conv@1.5.3',fonts:{},images:{}};
for(const [size,all] of [[16,true],[28,true],[10,false],[13,false],[18,false],[20,false],[86,false],[101,false]]) {
  const name=size>80?`luna_digits_${size}`:`luna_cjk_${size}`;
  const glyphs=size>80?[...'0123456789:-'].map(c=>c.codePointAt(0)):all ? full : [...new Set([...ascii, ...small])].filter(c=>cmap.includes(c));
  console.log(`Generating ${name}: ${glyphs.length} glyphs`,true);
  const output=path.join(dest,`${name}.c`);
  const result=spawnSync(process.execPath,[path.join(tool,'lv_font_conv.js'),'--font',fontPath,
    '--range',ranges(glyphs),'--size',String(size),'--bpp',all?'2':'4','--format','lvgl',
    '--no-kerning',...(!all?['--no-compress']:[]),'--lv-include','lvgl.h','--lv-font-name',name,'--output',output],{encoding:'utf8'});
  if(result.status!==0) throw Error(result.stderr || result.stdout || 'Font conversion failed');
  // Normalize source header paths, retain the generated bitmap/metrics verbatim.
  let source=fs.readFileSync(output,'utf8').replaceAll(fontPath,'NotoSansCJKsc-Regular.otf');
  // Match preview's 1.5em line boxes without changing glyph baseline/bitmap.
  // Rare tall glyphs retain their full bitmap; the card's outer clip is unchanged.
  if(all) {
    const height=Number(source.match(/\.line_height = (\d+)/)[1]);
    const baseline=Number(source.match(/\.base_line = (\d+)/)[1]);
    const normalized=size*1.5;
    source=source.replace(/\.line_height = \d+/,`.line_height = ${normalized}`)
      .replace(/\.base_line = \d+/,`.base_line = ${baseline-(height-normalized)}`);
  }
  if(all) source=source.replace('.get_glyph_bitmap = lv_font_get_bitmap_fmt_txt',
    '.get_glyph_bitmap = luna_font_bitmap_get');
  source='// Noto-derived Luna bitmap font. See OFL-NotoSansCJK.txt and README.md.\n'+
    (all?'#include "luna_font_decode.h"\n':'')+source;
  fs.writeFileSync(output,source);
  declarations.push(`LV_FONT_DECLARE(${name});`);
  manifest.fonts[name]={size,bpp:all?2:4,compressed:all,glyphCount:glyphs.length,codepoints:glyphs,sha256:sha(source)};
}
const arrays=[];
function bytes(data) {const rows=[];for(let i=0;i<data.length;i+=24) rows.push('    '+[...data.subarray(i,i+24)].map(v=>'0x'+v.toString(16).padStart(2,'0')).join(','));return rows.join(',\n');}
function image(name,w,h,data,cf,stride) {
  arrays.push(`static const uint8_t ${name}_data[] = {\n${bytes(data)}\n};\n`+
    `const lv_image_dsc_t ${name} = {.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=${cf},.w=${w},.h=${h},.stride=${stride}},.data_size=sizeof(${name}_data),.data=${name}_data};\n`);
  declarations.push(`extern const lv_image_dsc_t ${name};`);
  manifest.images[name]={width:w,height:h,bytes:data.length,sha256:sha(data)};
}
async function rgba(name,input,w,h) {
  const data=await sharp(input).resize(w,h,{kernel:'nearest'}).ensureAlpha().raw().toBuffer();
  for(let i=0;i<data.length;i+=4) [data[i],data[i+2]]=[data[i+2],data[i]]; // LVGL little-endian BGRA
  image(name,w,h,data,'LV_COLOR_FORMAT_ARGB8888',w*4);
}
async function main() {
  const html=fs.readFileSync(path.join(repo,'docs/design/preview/index.html'),'utf8');
  const symbols=[...html.matchAll(/<symbol id="i-([a-z-]+)" viewBox="0 0 24 24">([\s\S]*?)<\/symbol>/g)];
  for(const [,name,body] of symbols) {
    for(const size of name==='moon'?[24,30,33,135]:['play','pause'].includes(name)?[24,30]:[24]) {
      const filled=name==='play'&&size===30;
      const svg=`<svg xmlns="http://www.w3.org/2000/svg" width="${size}" height="${size}" viewBox="0 0 24 24" fill="${filled?'#ffffff':'none'}" stroke="${filled?'none':'#ffffff'}" stroke-width="${['prev','next','pause'].includes(name)?2:1.6}" stroke-linecap="round" stroke-linejoin="round">${body}</svg>`;
      await rgba(`luna_icon_${name.replaceAll('-','_')}_${size}`,Buffer.from(svg),size,size);
    }
  }
  // CSS weather cloud flattened to one image: no negative-y children to clip.
  await rgba('luna_cloud',Buffer.from('<svg xmlns="http://www.w3.org/2000/svg" width="160" height="105"><g fill="#b6d5f5"><circle cx="66.5" cy="40.5" r="40.5"/><circle cx="110.5" cy="44.5" r="28.5"/><rect x="0" y="39" width="160" height="66" rx="33"/></g></svg>'),160,105);
  await rgba('luna_sun',Buffer.from('<svg xmlns="http://www.w3.org/2000/svg" width="138" height="138"><circle cx="69" cy="69" r="69" fill="#ffe0aa" opacity=".02"/><circle cx="69" cy="69" r="54" fill="#ffe0aa" opacity=".03"/><circle cx="69" cy="69" r="41" fill="#ffe0aa"/></svg>'),138,138);
  for(const [kind,color] of [['warm','#e5dcff'],['cool','#d4e7ff']]) {
    await rgba(`luna_star_${kind}`,Buffer.from(`<svg xmlns="http://www.w3.org/2000/svg" width="14" height="14"><defs><radialGradient id="g"><stop stop-color="${color}"/><stop offset=".25" stop-color="${color}" stop-opacity=".8"/><stop offset=".4" stop-color="${color}" stop-opacity=".15"/><stop offset="1" stop-color="${color}" stop-opacity="0"/></radialGradient></defs><circle cx="7" cy="7" r="6" fill="url(#g)"/></svg>`),14,14);
  }
  const pet=await sharp(path.join(repo,'docs/design/preview/assets/luna-mooncat-atlas.png')).resize(288,144,{kernel:'nearest'}).png().toBuffer();
  for(let i=0;i<8;i++) {
    const data=await sharp(pet).extract({left:(i%4)*72,top:Math.floor(i/4)*72,width:72,height:72}).ensureAlpha().raw().toBuffer();
    for(let j=0;j<data.length;j+=4) [data[j],data[j+2]]=[data[j+2],data[j]];
    image(`luna_pet_${i}`,72,72,data,'LV_COLOR_FORMAT_ARGB8888',288);
    if(i<4) {
      const left=await sharp(pet).extract({left:(i%4)*72,top:Math.floor(i/4)*72,width:72,height:72}).flop().ensureAlpha().raw().toBuffer();
      for(let j=0;j<left.length;j+=4) [left[j],left[j+2]]=[left[j+2],left[j]];
      image(`luna_pet_left_${i}`,72,72,left,'LV_COLOR_FORMAT_ARGB8888',288);
    }
  }
  let stars=fs.readFileSync(path.join(repo,'docs/design/preview/assets/luna-starfield.svg'),'utf8');
  stars=stars.replace('<!-- Fixed, local starfield: shared screen backdrop, never a card decoration. -->',
    '<defs><radialGradient id="sky" cx="80%" cy="5%" r="85%"><stop stop-color="#253055" stop-opacity=".33"/><stop offset="1" stop-color="#101827" stop-opacity="0"/></radialGradient></defs><rect width="720" height="720" fill="#101827"/><rect width="720" height="720" fill="url(#sky)"/>');
  // I8 keeps a 720x720 backdrop to 519 KiB rather than a 2 MiB ARGB buffer.
  const palettePng=await sharp(Buffer.from(stars)).png({palette:true,colours:256,dither:0}).toBuffer();
  const raw=await sharp(palettePng).ensureAlpha().raw().toBuffer();const colors=new Map();const data=Buffer.alloc(1024+720*720);
  for(let i=0;i<720*720;i++) {
    const offset=i*4,key=raw.readUInt32LE(offset);
    if(!colors.has(key)) {
      const n=colors.size;if(n>=256)throw Error('Backdrop palette exceeds I8');colors.set(key,n);
      data[n*4]=raw[offset+2];data[n*4+1]=raw[offset+1];data[n*4+2]=raw[offset];data[n*4+3]=raw[offset+3];
    }
    data[1024+i]=colors.get(key);
  }
  image('luna_backdrop',720,720,data,'LV_COLOR_FORMAT_I8',720);
  fs.writeFileSync(path.join(dest,'luna_images.c'),'// Generated from approved local preview artwork.\n#include "lvgl.h"\n'+arrays.join('\n'));
  fs.writeFileSync(path.join(dest,'luna_ui_assets.h'),'#pragma once\n#include "lvgl.h"\n'+declarations.join('\n')+'\n');
  fs.writeFileSync(path.join(dest,'manifest.json'),JSON.stringify(manifest,null,2)+'\n');
  console.log('Generated preview artwork and complete source-mapped Han fonts');
}
main().catch(error=>{console.error(error);process.exitCode=1;});
