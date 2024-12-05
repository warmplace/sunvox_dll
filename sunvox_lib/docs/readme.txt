[ About ]

SunVox is a powerful modular synthesizer and pattern-based sequencer (tracker): https://warmplace.ru/soft/sunvox
SunVox Library allows you to use most of SunVox features in your own products:
 * load and play several SunVox/XM/MOD music files simultaneously;
 * play interactive/generative/microtonal music;
 * play synths, apply effects;
 * load samples (WAV,AIFF,XI,OGG,MP3,FLAC), synths and effects created by other users;
 * change the project parameters (synth controllers, pattern notes, etc.).

Read more: https://warmplace.ru/soft/sunvox/sunvox_lib.php

[ Structure ]

sunvox_lib
  android - SunVox library for Android + sample projects;
  docs - documentation and license files;
  examples - examples of using SunVox library;
  headers - SunVox C/C++ header file with API description;
  ios - SunVox library for iOS + sample projects;
  js - SunVox library for JavaScript+WebAssembly:
    js/index.html - advanced JS SunVox player with generative demo songs;
    js/index_basic.html - the simplest example of using the library in HTML page;
    js/lib - the library;
    js/lib/sunvox_lib_loader.js - definition of all SunVox functions from sunvox.h, but slightly modified for use in JS code;
    js/lib_lofi - the same but for slow devices (4.12 fixed-point audio engine);
  linux - SunVox library for Linux;
  macos - SunVox library for macOS;
  main - main CPP file of the library; ignore it if you don't need to rebuild the library;
  make - build scripts; use them if you need to rebuild the library for different platform or with different options;
  resources - SunVox songs and modules for the sample projects;
  windows - SunVox library for Windows + sample projects;

lib_* - auxiliary libraries that will be required if you rebuild the SunVox Library from sources.
