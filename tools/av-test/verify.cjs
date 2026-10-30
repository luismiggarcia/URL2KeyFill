// Offline checks for the generated standalone assets; no npm dependencies.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert');
const folder=path.resolve(__dirname,'../../assets/Diagnostics');
for(const file of fs.readdirSync(folder).filter(f=>f.endsWith('.html'))){
 const html=fs.readFileSync(path.join(folder,file),'utf8');
 assert(!/<(?:button|select|input|a)\b/i.test(html),`${file}: interactive UI`);
 assert(!/\b(?:src|href)\s*=/i.test(html),`${file}: external dependency`);
 const code=html.match(/<script>([\s\S]*?)<\/script>/)[1],labels=[];
 const drawing=new Proxy({},{get:(_,key)=>key==='fillText'?(s)=>labels.push(s):()=>{},set:()=>true});
 const canvas={getContext:()=>drawing};let callback,meterTone=false;
 class Audio{constructor(){this.sampleRate=48000;this.currentTime=0;this.state='running';this.destination={};}createBuffer(c,n){assert.equal(c,2);return{getChannelData:()=>new Float32Array(n)}}createChannelSplitter(){return{connect(){}}}createAnalyser(){return{fftSize:2048,getFloatTimeDomainData(data){for(let i=0;i<data.length;i++)data[i]=meterTone?10**(-18/20)*Math.sin(2*Math.PI*1000*i/48000):0}}}createBufferSource(){return{connect(){},start(){}}}async resume(){}}
 const env={document:{getElementById:()=>canvas},window:{AudioContext:Audio},performance:{now:()=>1000},innerWidth:1920,innerHeight:1080,devicePixelRatio:1,addEventListener(){},requestAnimationFrame:f=>callback=f,console};
 vm.runInNewContext(code,env);const api=env.window.avTest;
 assert.equal(api.duration,api.mode==='auto'?96:api.mode==='sweep'?16:8);
 let offset=0;for(const [index,s] of api.stages.entries()){
  assert.equal(api.stageAt(offset).index,index);assert.equal(api.stageAt(offset+s.duration-1e-6).index,index);
  for(let c=0;c<2;c++)for(let i=0;i<48000*s.duration;i+=101){const v=api.sample(i/48000,s.sound,c,s.duration);assert(Number.isFinite(v));assert(Math.abs(v)<=10**(-18/20)+1e-9);if(s.sound==='silence'||s.sound==='left'&&c===1||s.sound==='right'&&c===0)assert.equal(v,0);}
  const l=api.sample(.01025,s.sound,0,s.duration),r=api.sample(.01025,s.sound,1,s.duration);if(s.sound==='phase')assert.equal(l,-r);if(s.sound==='stereo')assert.equal(l,r);
  api.context.currentTime=.1+offset+.01;callback(1000+offset*1000);assert(labels.includes(s.name));assert(labels.some(l=>l.includes(s.sound==='silence'?'SILÊNCIO DIGITAL · intencional':'SINAL PREVISTO')));assert(labels.some(l=>l.includes('Fonte medida: L −∞ dBFS RMS / −∞ pico')));offset+=s.duration;
 }
 // Blocked autoplay must leave the visual generator operational and report its state.
 assert.equal(api.sourceLevels()[0].rms,-Infinity);assert.equal(api.sourceLevels()[1].peak,-Infinity);
 meterTone=true;for(const level of api.sourceLevels()){assert(Math.abs(level.peak+18)<.01);assert(Math.abs(level.rms+21.01)<.1);}meterTone=false;
 api.context.state='suspended';callback(200000);assert(labels.some(l=>l.includes('SUSPENDED')));
 console.log(`PASS ${file}: startup, stages, channel samples, peak limits, canvas, blocked autoplay`);
}
