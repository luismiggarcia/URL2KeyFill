#!/usr/bin/env python3
"""Serve the real native HTML with a mock bridge; never opens devices or saves preferences."""
from pathlib import Path
from http.server import BaseHTTPRequestHandler, HTTPServer
import argparse
ROOT=Path(__file__).resolve().parents[1]
BRIDGE='''<script>
const fixture={layers:[{id:'a',name:'New layer',url:'u2k://Diagnostics/AV_Test.html',visible:true,gain:1,left:true,right:true,mono:false,muted:false},{id:'b',name:'Lower Thirds',url:'u2k://Lower_Third/Animated_10_sec_cycle.html',visible:true,gain:1,left:true,right:true,mono:false,muted:false},{id:'c',name:'Transparência',url:'u2k://Transparency/Basic_Test.html',visible:true,gain:1,left:true,right:true,mono:false,muted:false}],layersVisible:true,device:'test',deviceName:'UltraStudio HD Mini',devices:[{id:'test',name:'UltraStudio HD Mini'}],mode:0,modeName:'1080p50',modes:[{id:0,name:'1080p50'}],pixel:'BGRA',pixels:['BGRA'],outputs:[{id:'Fill + Key',name:'Fill + Key'}],keying:true,width:1920,height:1080,browserSize:'match',timingProfile:'reliable',syncOffsetMs:0,audioGain:1,audioMono:false,audioMuted:false,audioLeft:true,audioRight:true,monitorOutput:false,monitoredLayers:[],output:{running:false,onAir:false},diagnostics:'UI controls fixture',message:'Isolated test bridge · no devices',examples:[]};
window.webkit={messageHandlers:{native:{postMessage(message){
 if(message.type==='layout'||message.type==='dragWindow')return;
 const log=document.getElementById('test-events');if(log)log.textContent=JSON.stringify(message);
 if(message.type==='layer'){const layer=fixture.layers.find(l=>l.id===message.id);if(layer)Object.assign(layer,Object.fromEntries(Object.entries(message).filter(([k])=>!['id','type'].includes(k))));}
 if(message.type==='audioGlobal')for(const key of ['gain','mono','muted','left','right'])if(key in message)fixture['audio'+key[0].toUpperCase()+key.slice(1)]=message[key];
 if(message.type==='audioMute')fixture.audioMuted=message.value;
 setTimeout(()=>{window.receiveState(fixture);paintFixture();},0);
}}}};
function paintFixture(){const amp=db=>10**(db/20)/Math.SQRT2;window.receiveAudioLevels({a:[amp(-18),amp(-9)],b:[0,0],c:[amp(-30),amp(-24)]},[amp(-12),amp(-3)]);}
window.addEventListener('DOMContentLoaded',()=>{window.receiveState(fixture);paintFixture();setInterval(paintFixture,50);});
</script>'''

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',type=int,default=8767)
    args=parser.parse_args()
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path.split('?')[0] not in ('/','/interface.html'):
                self.send_error(404);return
            html=(ROOT/'assets/interface.html').read_text().replace('<script>',BRIDGE+'<script>',1)
            tests=(ROOT/'tests/UIControls.js').read_text()
            html=html.replace('</body>', '<script>'+tests+'''
document.addEventListener('DOMContentLoaded',()=>{document.getElementById('run-controls-tests').onclick=async()=>{try{document.getElementById('test-result').textContent=await testAudioControls();}catch(error){document.getElementById('test-result').textContent='FAIL · '+error.message;}};});</script><button id="run-controls-tests" style="position:fixed;right:8px;bottom:8px;z-index:999">Testar controlos</button><output id="test-result" style="position:fixed;left:8px;right:8px;bottom:40px;color:#fff;background:#111;display:block;z-index:999"></output></body>''')
            html=html.replace('</body>','<output id="test-events" style="display:none"></output></body>')
            data=html.encode();self.send_response(200);self.send_header('Content-Type','text/html; charset=utf-8');self.send_header('Cache-Control','no-store');self.end_headers();self.wfile.write(data)
    print(f'Native interface fixture: http://127.0.0.1:{args.port}/interface.html',flush=True)
    HTTPServer(('127.0.0.1',args.port),Handler).serve_forever()
if __name__=='__main__':main()
