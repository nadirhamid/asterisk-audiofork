## what is app_audiofork

app_audiofork lets you integrate raw audio streams in your third party app by making minor adjustments to your asterisk dialplan. 

the asterisk app works as a small "fork" between your dialplan and app logic. 

```
ASTERISK -> AUDIO STREAM -> WS APP SERVER
```


the main purpose of this app is to quickly offload audio streams to another script or app -- allowing implementors to add higher levels of audio processing to their dialplan.

# how to install

this is not officially a builtin asterisk module so you will have to drop files into the asterisk codebase. 
please use the following steps to install the module:

1. copy "app_audiofork.c" to "asterisk/apps/app_audiofork.c"
2. cd into your asterisk source tree
3. refresh the menuselect options
```
rm -f ./menuselect.makeopts
```
4. re run menuselect
```
make menuselect
```
* app_audiofork should be listed under "Applications" and selected by default.
5. install asterisk
```
make
make install
```
6. reload asterisk
```
asterisk -rx 'core reload'
```

# configuring in dial plans

here is a simple example of how to use "AudioFork()"

```
exten => _X.,1,Answer()
exten => _X.,n,Verbose(starting audio fork)
exten => _X.,n,AudioFork(ws://localhost:8080/)
exten => _X.,n,Verbose(audio fork was started continuing call..)
exten => _X.,n,Playback(hello-world)
exten => _X.,n,Hangup()
```

# configuring a websocket server

you will need to use a websocket server that supports receiving binary frames. 
below is one written in node.js that was also used during testing:

[WebSocket nodejs server](https://github.com/websockets/ws)
* NOTE: you will need to use version 7.2.0 or below as there is currently a client side masking issue in res_http_websocket.c


# example for receving on the server side

below is an example that receives audio frames from "AudioFork()" and stores them into a file.

```
const WebSocket = require('ws');

const wss = new WebSocket.Server({ port: 8080 });
var fs = require('fs');
var wstream = fs.createWriteStream('audio.raw');

wss.on('connection', function connection(ws) {
  console.log("got connection ");

  ws.on('message', function incoming(message) {
    console.log('received frame..');
    wstream.write(message);
  });
});
```


# converting raw audio to WAV

below is an example using sox to convert audio received into a format like WAV.

```
sox -r 8000 -e signed-integer -b 16 audio.raw audio.wav
```

# sending separate WS streams

below is an example of a dialplan that can send 2 separate streams to a websocket server. in this example the basic dialplan and the websocket server has been modified to accept separate URL paths so that we can save to two separate files depending on which direction of the call we are processing. 

updated dialplan

```
[main-out]
exten => _.,1,Verbose(call was placed..)
same => n,Answer()
same => n,AudioFork(ws://localhost:8080/out,D(out))
same => n,Dial(SIP/1001,60,gM(in))
same => n,Hangup()

[macro-in]
exten => _.,1,Verbose(macro-in called)
same => n,AudioFork(ws://localhost:8080/in,D(out))
```

node.js server implementation

```
const http = require('http');
const WebSocket = require('ws');
const url = require('url');
const fs = require('fs');

const server = http.createServer();
const wss1 = new WebSocket.Server({ noServer: true });
const wss2 = new WebSocket.Server({ noServer: true });
var outstream = fs.createWriteStream('out.raw');
var instream = fs.createWriteStream('in.raw');


wss1.on('connection', function connection(ws) {
  // ...
  console.log("got out connection ");

  ws.on('message', function incoming(message) {
    console.log('received out frame..');
    outstream.write(message);
  });
});

wss2.on('connection', function connection(ws) {
  // ...
  console.log("got in connection ");

  ws.on('message', function incoming(message) {
    console.log('received in frame..');
    instream.write(message);
  });

});

server.on('upgrade', function upgrade(request, socket, head) {
  const pathname = url.parse(request.url).pathname;

  if (pathname === '/out') {
    wss1.handleUpgrade(request, socket, head, function done(ws) {
      wss1.emit('connection', ws, request);
    });
  } else if (pathname === '/in') {
    wss2.handleUpgrade(request, socket, head, function done(ws) {
      wss2.emit('connection', ws, request);
    });
  } else {
    socket.destroy();
  }
});

server.listen(8080);
```

# Live transcription demo

for a demo integration with Google Cloud speech APIs, please see: [Asterisk Transcribe Demo](https://github.com/nadirhamid/audiofork-transcribe-demo)

# TLS support

AudioFork() currently supports secure websocket connections. in order to create a secure websocket connection, you must specify the "T" option in the "AudioFork()" app options.

for example:
```
AudioFork(wss://example.org/in,D(out)T(certfile,pvtfile,cipher,cafile,capath))
```

all paths to CA files should be absolute.

# project roadmap

below is a list of updates planned for the module:

- add asterisk manager support
  - stopping live AudioForks thru AMI
  - starting new AudioFork based on channel prefix
  - applying volume gain to AudioFork
  - muting AudioFork
- store responses pushed from websocket server into channel var

# contact info

for any queries / more info please contact me directly:
```
Nadir Hamid <matrix.nad@gmail.com>
```

thank you
