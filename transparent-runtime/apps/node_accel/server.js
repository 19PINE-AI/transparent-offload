const accel = require('./build/Release/accel.node');
const http = require('http');
http.createServer((req, res) => {
  if (req.url === '/sync')  { accel.accelSync(); res.end('OK'); }       // blocks event loop
  else                      { accel.accelAsync(() => res.end('OK')); }   // overlap via uv pool
}).listen(7780, () => console.log('node-accel on 7780'));
