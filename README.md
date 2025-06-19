SangeetAdda is a local network application where a server streams songs (preloaded audio files — WAV or decoded MP3) to multiple clients on the same LAN.
It feels like a shared “adda” where everyone listens to the same playlist — live.

🗂 Folder Structure
rust
Copy
Edit
SangeetAdda/  
├── README.md  
├── server/  
│   ├── main_server.c  
│   ├── audio_streamer.c  
│   ├── audio_streamer.h  
│   ├── control_handler.c  
│   ├── control_handler.h  
│   ├── playlist/  
│   │   ├── song1.wav  
│   │   ├── song2.wav  
│   │   └── ...  
│   └── Makefile  
├── client/  
│   ├── main_client.c  
│   ├── audio_player.c  
│   ├── audio_player.h  
│   ├── ui.c  // (optional)  
│   ├── ui.h  
│   └── Makefile  
└── docs/  
    └── design.md  (for your proposal writeup)  
🔑 Modules Breakdown
Server Modules:

main_server.c → entry point, handle connections

audio_streamer.c → load audio, send chunks

control_handler.c → handle pause/next/play commands

playlist/ → folder with WAV files

Client Modules:

main_client.c → entry point, connect to server

audio_player.c → receive & play audio

ui.c → show current song, status (optional)

🕒 Workflow for You
✅ Week 1 (June 20–27):

Set up basic server and client connection (TCP or UDP)

Load WAV file, send chunks

Play audio at client

One client only

✅ Week 2 (June 28–July 4 — Registration deadline):

Support multiple clients

Implement buffering & smooth playback

Add control commands (Play, Pause, Next)

Test on local LAN (multiple devices)

✅ Final Week (July 5–15):

Polish UI (song name display)

Test with 2–3 clients

Write README + prepare demo