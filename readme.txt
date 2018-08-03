[ About ]

SunVox is a powerful modular synthesizer and pattern-based sequencer (tracker): http://www.warmplace.ru/soft/sunvox
SunVox Library is the main part of the SunVox engine without a graphical interface.
Using this library, you can do the following:
 * load and play several SunVox/XM/MOD music files simultaneously;
 * play interactive/generative/microtonal music;
 * play synths, apply effects;
 * load samples (WAV,AIFF,XI), synths and effects created by other users (some of these modules are distributed along with the SunVox);
 * change any project parameters (synth controllers, pattern notes, etc.).

[ Structure ]

android - SunVox library for Android + sample projects;
headers - SunVox C/C++/Pixilang/FPC header files; API description is in the sunvox.h file;
ios - SunVox library for iOS + sample projects;
js - SunVox library for JavaScript+WebAssembly:
  js/index.html - advanced JS SunVox player with generative demo songs;
  js/index_basic.html - the simplest example of using the library in HTML page;
  js/lib - the library;
  js/lib/sunvox_lib_loader.js - definition of all SunVox functions from sunvox.h, but slightly modified for use in JS code;
linux - SunVox library for Linux + sample projects (!!most detailed examples!!);
macos - SunVox library for macOS + sample projects;
pixilang - example of using SunVox library in Pixilang application;
resources - SunVox songs and modules for the sample projects;
windows - SunVox library for Windows + sample projects;

[ Donation ]

You can make a donation to support my work:
http://www.warmplace.ru/donate

[ License ]

You can freely use the SunVox library in your own products (even commercial ones).
The following text must be included in the documentation and/or other materials provided with your products:

Powered by:
 * SunVox modular synthesizer
   Copyright (c) 2008 - 2018, Alexander Zolotov <nightradio@gmail.com>, WarmPlace.ru
 * Ogg Vorbis 'Tremor' integer playback codec
   Copyright (c) 2002, Xiph.org Foundation
