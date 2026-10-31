"""Production libcurl, actual HTTP, hostile desktop proxy, and timeout behavior."""
import os,subprocess,sys,threading,time
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
class Handler(BaseHTTPRequestHandler):
 def log_message(self,*args):pass
 def do_POST(self):
  payload=self.rfile.read(int(self.headers.get('Content-Length',0)))
  valid=self.headers.get('Authorization')=='Bearer probe-token' and self.headers.get('Content-Type')=='application/json' and payload==b'{"probe":true}'
  status=401 if self.path=='/unauthorized' else (200 if valid else 400)
  if self.path=='/slow':time.sleep(.3)
  body=b'{}'
  try:
   self.send_response(status);self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
  except (BrokenPipeError,ConnectionResetError):pass
server=ThreadingHTTPServer(('127.0.0.1',0),Handler);threading.Thread(target=server.serve_forever,daemon=True).start()
env=os.environ.copy()
for key in ['HTTP_PROXY','HTTPS_PROXY','ALL_PROXY','http_proxy','https_proxy','all_proxy']:env[key]='http://127.0.0.1:1'
env.update(NO_PROXY='',no_proxy='')
try:
 for path,timeout,expected in [('/ok',1000,'1 200'),('/unauthorized',1000,'1 401'),('/slow',50,'0 -1')]:
  result=subprocess.run([sys.argv[1],f'http://127.0.0.1:{server.server_port}'+path,str(timeout)],env=env,text=True,capture_output=True,timeout=3,check=True)
  assert result.stdout.strip()==expected,(path,result.stdout,result.stderr)
 print('Actual libcurl: direct connection bypasses hostile proxy; HTTP 401 and client timeout distinguished')
finally:server.shutdown();server.server_close()
