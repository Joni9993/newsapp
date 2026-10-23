"""Local UI fixture server for development only. Never part of the firmware."""
from http.server import ThreadingHTTPServer,SimpleHTTPRequestHandler
from pathlib import Path
import json,re,time
root=Path(__file__).resolve().parents[1]
catalog=[{'id':a,'name':b,'interval':int(c),'lastSuccess':0,'loading':False,'error':''} for a,b,c in re.findall(r'\{"([^"]+)",\s*"([^"]+)",\s*(\d+)\}',(root/'main/modules.c').read_text(encoding='utf-8'))]
cfg=dict(region=1,mode=0,theme=0,duration=20,brightness=80,night_brightness=12,night_start=22,night_end=6,city='',ssid='',modules=[dict(id=m['id'],enabled=m['id'] in ['news','weather','clock']) for m in catalog],calendars=[dict(name='',provider='google',configured=False) for _ in range(3)])
push=dict(enabled=True,privacy=0,token='0123456789abcdef0123456789abcdef')
class Handler(SimpleHTTPRequestHandler):
    def __init__(self,*args,**kw):super().__init__(*args,directory=str(root/'main'),**kw)
    def log_message(self,*args):pass
    def send_json(self,j,status=200,cookie=False):
        b=json.dumps(j).encode();self.send_response(status);self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(b)))
        if cookie:self.send_header('Set-Cookie','preview=1; HttpOnly; SameSite=Strict; Path=/')
        self.end_headers();self.wfile.write(b)
    def do_GET(self):
        if not self.path.startswith('/api/'):return super().do_GET()
        if 'preview=1' not in self.headers.get('Cookie',''):return self.send_json({'error':'Vorschau: mit Code 123456 koppeln'},401)
        if self.path.endswith('/notify/config'):return self.send_json(push)
        if self.path.endswith('/config'):return self.send_json(cfg)
        if self.path.endswith('/modules'):return self.send_json(catalog)
        if self.path.endswith('/status'):return self.send_json(dict(connected=False,setup=True,connecting=False,ip='192.168.4.1',error='',paused=False,heap=160000,minHeap=150000,uptime=130,timeValid=False,calendars=[dict(slot=i,error='',checking=False,updated=0) for i in range(3)]))
        self.send_json({'error':'Unknown'},404)
    def do_POST(self):
        j=json.loads(self.rfile.read(int(self.headers.get('Content-Length','0'))) or '{}')
        if self.path.endswith('/pair/request'):return self.send_json({'ok':True})
        if self.path.endswith('/pair'):return self.send_json({'ok':True} if j.get('code')=='123456' else {'error':'Code falsch'},200 if j.get('code')=='123456' else 401,cookie=j.get('code')=='123456')
        if self.path.endswith('/notify/config'):
            push.update({k:v for k,v in j.items() if k in('enabled','privacy')})
            if j.get('regenerate'):push['token']='%032x'%int(time.time()*1e6)
            return self.send_json({'ok':True})
        if self.path.endswith('/notify/test'):return self.send_json({'ok':True})
        if self.path.endswith('/config'):
            for i,c in enumerate(j.get('calendars',[])):c['configured']=False if c.get('remove') else bool(c.pop('url',None)) or cfg['calendars'][i]['configured']
            cfg.update(j)
        if self.path.endswith('/scan'):return self.send_json([dict(ssid='Mein Zuhause',rssi=-45,open=False,channel=6),dict(ssid='Gastnetz',rssi=-63,open=False,channel=11),dict(ssid='Gastnetz',rssi=-80,open=False,channel=1),dict(ssid='Nachbar',rssi=-84,open=True,channel=1)])
        self.send_json({'ok':True})
if __name__=='__main__':
    print('UI fixture: http://127.0.0.1:8765 — pairing code 123456',flush=True)
    ThreadingHTTPServer(('127.0.0.1',8765),Handler).serve_forever()
