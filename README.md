# TCI Monitor

A lightweight diagnostic tool for the **TCI** (Transceiver Control Interface)
WebSocket protocol used by AetherSDR, ExpertSDR2, SunSDR, and similar SDR
applications.

TCI Monitor connects to a TCI server, lets you see every raw message the
server sends, and pulls structured information out of common events (VFO,
mode, spots).  Useful when you're debugging a TCI client, exploring what
events a server emits in a given workflow, or just curious about the wire
protocol.

## Features

- Connect to any TCI server over WebSocket — host + port, defaults to
  `127.0.0.1:50001`
- Real-time scrolling log of every message received, with timestamps
- Parsed views in the side panel:
  - **Current VFO / mode** — updated as the radio is tuned
  - **Spot table** — every `spot:` event the server emits, with callsign,
    frequency, mode, and source (DX cluster / parks / RBN / etc.)
- Save the raw log to a file for offline analysis
- Filter / search the message stream
- Auto-reconnect with backoff if the connection drops

## Build

Requires Qt 6.2+ with the WebSockets module, CMake 3.20+, and a C++17
compiler.

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

On Windows: `.\build.bat` does the configure + build in one shot.

## Why standalone?

Originally written to investigate what AetherSDR sends when an operator
clicks a DX-cluster spot on the panadapter — useful information that no
documentation seemed to cover.  Spinning it out as its own tool means it
stays available whenever a TCI question comes up, separate from any
particular logger or controller it might inform.

## License

MIT.  See [LICENSE](LICENSE).

73 de G0JKN / W3.
