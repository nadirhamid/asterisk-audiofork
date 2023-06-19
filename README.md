## What is app_audiofork

app_audiofork lets you integrate raw audio streams in your third party app by making minor adjustments to your asterisk dialplan. 

The asterisk app simply pushes any Asterisk audio to a web socket server.

For example:
```
ASTERISK -> AUDIO STREAM -> WS APP SERVER
```

The main purpose of this app is to provide a simple integration with the Asterisk audiohooks API -- allowing developers to integrate higher level programs for audio processing.

# How to install

You can use the Makefile to easily install app_audiofork. 

To get started, please run:

```
make
make install
```

## Load module

Afterwards, you will need to load the module.

You can simply run the following command:

asterisk -rx 'module load app_audiofork.so'

# Configuring in dial plans

Here is a simple example of how to use "AudioFork()"

```
exten => _X.,1,Answer()
exten => _X.,n,Verbose(starting audio fork)
exten => _X.,n,AudioFork(ws://localhost:8080/)
exten => _X.,n,Verbose(audio fork was started continuing call..)
exten => _X.,n,Playback(hello-world)
exten => _X.,n,Hangup()
```

# Configuring a Websocket server

In order to integrate the module, it is advised that you use a websocket server that is compliant with the latest standard. In other words, it should support both text and binary data frames.

Here is an example of a Node.js Websocket module:
[WebSocket nodejs server](https://github.com/websockets/ws)

# Example: simple integration

Below is an example that receives audio frames from the AudioFork app and stores them into a audio file.

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


# Converting raw audio to WAV

You can quickly convert any raw audio data to a another format, such as WAV, using the Sox command line tool.

For example:

```
sox -r 8000 -e signed-integer -b 16 audio.raw audio.wav
```

# Sending separate Websocket streams

In a production scenario, it is common to handle both legs of the call, incoming and outgoing. 

Below is an example including a WebSocket server that receives two connections and stores each stream contents in its own unique file.

Updated dialplan

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

Node.js server implementation

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

For a demo integration with Google Cloud speech APIs, please see: [Asterisk Transcribe Demo](https://github.com/nadirhamid/audiofork-transcribe-demo)

# TLS support

AudioFork currently supports secure websocket connections. In order to create a secure websocket connection, you must add the "T" option to the app options.

For example:

```
AudioFork(wss://example.org/in,D(out)T(on))
```

# Reconnecting closed sockets

It is also possible to setup basic backoff for reconnection. By default, Audiofork is configured to reconnect to the WS server, and after a preconfigured number of attempts it will close the connection. These parameters, however, can be adjusted.

To adjust the reconnection parameters, you can use the following parameters:

```
R(timeout_for_connection)
r(number of times to attempt reconnection)
```

For instance, the following example will set the reconnection timeout to 10 seconds and will attempt to reconnect five times.

```
AudioFork(wss://example.org/in,R(10)r(5))
```

# Project roadmap

At this time, AudioFork is largely incomplete and has many updates planned. 

The following updates are scheduled for the upcoming releases:

- Test and ensure module is fully compatible with Asterisk manager
- Store any WebSocket responses in a dialplan variable

# Contact info

For any queries, please contact me directly:
```
Nadir Hamid <matrix.nad@gmail.com>
```