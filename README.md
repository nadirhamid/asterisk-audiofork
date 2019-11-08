## what is app_audiofork

This is an asterisk module based on asterisk mix monitor that allows you to send the raw PCM or "audio/pcm" packets to a set websocket server. 
The module also lets you customize options an audio fork can use like applying volume gains or configuring beeps at set intervals.

# how to install

this is not officially a builtin asterisk module so you will have to drop files into the asterisk codebase. 
please use the following steps to install the module:

1. copy "app_audiofork.c" to "asterisk/apps/app_audiofork.c"
2. cd into your asterisk source tree
3. refresh the menuselect options
```
rm -rf ./menuselect.makeopts
```
4. re run menuselect
```
make menuselect
```
5. install asterisk
```
make
make install
```

# configuring in dial plans

here is an example of how to use "AudioFork()"

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
* although you can use any of your preference


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


# roadmap

below is a list of updates planned for the module:

```
- add asterisk manager support
  - stopping live AudioForks thru AMI
  - starting new AudioFork based on channel prefix
  - applying volume gain to AudioFork
  - muting AudioFork
- store responses pushed from websocket server into channel var
- add support for SSL connections (maybe)
```

# contact info

for any queries / more info please connect me directly:
```
Nadir Hamid <matrix.nad@gmail.com>
```

thank you
