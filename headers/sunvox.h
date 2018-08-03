/*
   SunVox modular synthesizer
   Copyright (c) 2008 - 2018, Alexander Zolotov <nightradio@gmail.com>, WarmPlace.ru
*/

#ifndef __SUNVOX_H__
#define __SUNVOX_H__

#include <stdio.h>
#include <stdint.h>

/*
   Constants, data types and macros
*/

#define NOTECMD_NOTE_OFF	128
#define NOTECMD_ALL_NOTES_OFF	129 /* notes of all synths off */
#define NOTECMD_CLEAN_SYNTHS	130 /* stop and clean all synths */
#define NOTECMD_STOP		131
#define NOTECMD_PLAY		132

typedef struct
{
    uint8_t	note;           /* NN: 0 - nothing; 1..127 - note num; 128 - note off; 129, 130... - see NOTECMD_xxx defines */
    uint8_t	vel;            /* VV: Velocity 1..129; 0 - default */
    uint8_t	module;         /* MM: 0 - nothing; 1..255 - module number + 1 */
    uint8_t	zero;		/* ...future use... */
    uint16_t	ctl;            /* 0xCCEE: CC: 1..127 - controller number + 1; EE - effect */
    uint16_t	ctl_val;        /* 0xXXYY: value of controller or effect */
} sunvox_note;

#define SV_INIT_FLAG_NO_DEBUG_OUTPUT 		( 1 << 0 )
#define SV_INIT_FLAG_USER_AUDIO_CALLBACK 	( 1 << 1 ) /* Interaction with sound card is on the user side */
#define SV_INIT_FLAG_AUDIO_INT16 		( 1 << 2 )
#define SV_INIT_FLAG_AUDIO_FLOAT32 		( 1 << 3 )
#define SV_INIT_FLAG_ONE_THREAD			( 1 << 4 ) /* Audio callback and song modification functions are in single thread */

#define SV_MODULE_FLAG_EXISTS 1
#define SV_MODULE_FLAG_EFFECT 2
#define SV_MODULE_INPUTS_OFF 16
#define SV_MODULE_INPUTS_MASK ( 255 << SV_MODULE_INPUTS_OFF )
#define SV_MODULE_OUTPUTS_OFF ( 16 + 8 )
#define SV_MODULE_OUTPUTS_MASK ( 255 << SV_MODULE_OUTPUTS_OFF )

#define SV_STYPE_INT16 0
#define SV_STYPE_INT32 1
#define SV_STYPE_FLOAT32 2
#define SV_STYPE_FLOAT64 3

#define SV_GET_MODULE_XY( in_xy, out_x, out_y ) out_x = in_xy & 0xFFFF; if( out_x & 0x8000 ) out_x -= 0x10000; out_y = ( in_xy >> 16 ) & 0xFFFF; if( out_y & 0x8000 ) out_y -= 0x10000;

#if defined(_WIN32) || defined(_WIN32_WCE) || defined(__WIN32__) || defined(_WIN64)
    #define WIN
    #define LIBNAME "sunvox.dll"
    typedef LPCTSTR LIBNAME_STR_TYPE;
#else
    typedef const char* LIBNAME_STR_TYPE;
#endif
#if defined(__APPLE__) 
    #define OSX
    #define LIBNAME "sunvox.dylib"
#endif
#if defined(__linux__) || defined(linux)
    #define LINUX
    #define LIBNAME "./sunvox.so"
#endif
#if defined(OSX) || defined(LINUX)
    #define UNIX
#endif

#ifdef WIN
    #ifdef __GNUC__
	#define SUNVOX_FN_ATTR __attribute__((stdcall))
    #else
	#define SUNVOX_FN_ATTR __stdcall
    #endif
#endif
#ifndef SUNVOX_FN_ATTR
    #define SUNVOX_FN_ATTR /**/
#endif

#ifdef SUNVOX_STATIC_LIB

#ifdef __cplusplus
extern "C" {
#endif

/*
   Functions
   (use the functions with the label "USE LOCK/UNLOCK" within the sv_lock_slot() / sv_unlock_slot() block only!)
*/

/*
   sv_init(), sv_deinit() - global sound system init/deinit
   Parameters:
     config - string with additional configuration in the following format: "option_name=value|option_name=value";
              example: "buffer=1024|audiodriver=alsa|audiodevice=hw:0,0";
              use null if you agree to the automatic configuration;
     freq - sample rate (Hz); min - 44100;
     channels - only 2 supported now;
     flags - mix of the SV_INIT_FLAG_xxx flags.
*/
int sv_init( const char* config, int freq, int channels, unsigned int flags ) SUNVOX_FN_ATTR;
int sv_deinit( void ) SUNVOX_FN_ATTR;

/*
   sv_update_input() - 
   handle input ON/OFF requests to enable/disable input ports of the sound card
   (for example, after the Input module creation).
   Call it from the main thread only, where the SunVox sound stream is not locked.
*/
int sv_update_input( void ) SUNVOX_FN_ATTR;

/*
   sv_audio_callback() - get the next piece of SunVox audio from the Output module.
   With sv_audio_callback() you can ignore the built-in SunVox sound output mechanism and use some other sound system.
   SV_INIT_FLAG_USER_AUDIO_CALLBACK flag in sv_init() mus be set.
   Parameters:
     buf - destination buffer of type signed short (if SV_INIT_FLAG_AUDIO_INT16 used in sv_init())
           or float (if SV_INIT_FLAG_AUDIO_FLOAT32 used in sv_init());
           stereo data will be interleaved in this buffer: LRLR... ; where the LR is the one frame (Left+Right channels);
     frames - number of frames in destination buffer;
     latency - audio latency (in frames);
     out_time - buffer output time (in system ticks);
   Return values: 0 - silence (buffer filled with zeroes); 1 - some signal.
   Example:
     user_out_time = ... ; //output time in user time space (NOT SunVox time space!)
     user_cur_time = ... ; //current time (user time space)
     user_ticks_per_second = ... ; //ticks per second (user time space)
     user_latency = user_out_time - use_cur_time; //latency in user time space
     unsigned int sunvox_latency = ( user_latency * sv_get_ticks_per_second() ) / user_ticks_per_second; //latency in SunVox time space
     unsigned int latency_frames = ( user_latency * sample_rate_Hz ) / user_ticks_per_second; //latency in frames
     sv_audio_callback( buf, frames, latency_frames, sv_get_ticks() + sunvox_latency );
*/
int sv_audio_callback( void* buf, int frames, int latency, unsigned int out_time ) SUNVOX_FN_ATTR;

/*
   sv_audio_callback2() - send some data to the Input module and receive the filtered data from the Output module.
   It's the same as sv_audio_callback() but you also can specify the input buffer.
   Parameters:
     ...
     in_type - input buffer type: 0 - signed short (16bit integer); 1 - float (32bit floating point);
     in_channels - number of input channels;
     in_buf - input buffer; stereo data will be interleaved in this buffer: LRLR... ; where the LR is the one frame (Left+Right channels);
*/
int sv_audio_callback2( void* buf, int frames, int latency, unsigned int out_time, int in_type, int in_channels, void* in_buf ) SUNVOX_FN_ATTR;

/*
   sv_open_slot(), sv_close_slot(), sv_lock_slot(), sv_unlock_slot() - 
   open/close/lock/unlock sound slot for SunVox.
   You can use several slots simultaneously (each slot with its own SunVox engine)
*/
int sv_open_slot( int slot ) SUNVOX_FN_ATTR;
int sv_close_slot( int slot ) SUNVOX_FN_ATTR;
int sv_lock_slot( int slot ) SUNVOX_FN_ATTR;
int sv_unlock_slot( int slot ) SUNVOX_FN_ATTR;

/*
   sv_load(), sv_load_from_memory() - 
   load SunVox project from the file or from the memory block.
*/
int sv_load( int slot, const char* name ) SUNVOX_FN_ATTR;
int sv_load_from_memory( int slot, void* data, unsigned int data_size ) SUNVOX_FN_ATTR;

/*
*/
int sv_play( int slot ) SUNVOX_FN_ATTR;
int sv_play_from_beginning( int slot ) SUNVOX_FN_ATTR;
int sv_stop( int slot ) SUNVOX_FN_ATTR;

/*
   sv_set_autostop()
   autostop values: 0 - disable autostop; 1 - enable autostop.
   When disabled, song is playing infinitely in the loop.
*/
int sv_set_autostop( int slot, int autostop ) SUNVOX_FN_ATTR;

/* 
   sv_end_of_song() return values: 0 - song is playing now; 1 - stopped. 
*/
int sv_end_of_song( int slot ) SUNVOX_FN_ATTR;

/*
*/
int sv_rewind( int slot, int line_num ) SUNVOX_FN_ATTR;

/* 
   sv_volume() - set volume from 0 (min) to 256 (max 100%) 
*/
int sv_volume( int slot, int vol ) SUNVOX_FN_ATTR;

/*
   sv_send_event() - send some event (note ON, note OFF, controller change, etc.)
   Parameters:
     slot;
     track_num - track number within the pattern;
     note: 0 - nothing; 1..127 - note num; 128 - note off; 129, 130... - see NOTECMD_xxx defines;
     vel: velocity 1..129; 0 - default;
     module: 0 - nothing; 1..255 - module number + 1;
     ctl: 0xCCEE. CC - number of a controller (1..255). EE - effect;
     ctl_val: value of controller or effect.
*/
int sv_send_event( int slot, int track_num, int note, int vel, int module, int ctl, int ctl_val ) SUNVOX_FN_ATTR;

/*
*/
int sv_get_current_line( int slot ) SUNVOX_FN_ATTR; /* Get current line number */
int sv_get_current_line2( int slot ) SUNVOX_FN_ATTR; /* Get current line number in fixed point format 27.5 */
int sv_get_current_signal_level( int slot, int channel ) SUNVOX_FN_ATTR; /* From 0 to 255 */
const char* sv_get_song_name( int slot ) SUNVOX_FN_ATTR;
int sv_get_song_bpm( int slot ) SUNVOX_FN_ATTR;
int sv_get_song_tpl( int slot ) SUNVOX_FN_ATTR;

/* 
   sv_get_song_length_frames(), sv_get_song_length_lines() -
   get the project length.
   Frame is one discrete of the sound. Sample rate 44100 Hz means, that you hear 44100 frames per second. 
*/
unsigned int sv_get_song_length_frames( int slot ) SUNVOX_FN_ATTR;
unsigned int sv_get_song_length_lines( int slot ) SUNVOX_FN_ATTR;

/*
   sv_new_module() - create a new module;
   sv_remove_module() - remove selected module;
   sv_connect_module() - connect the source to the destination;
   sv_disconnect_module() - disconnect the source from the destination;
   sv_load_module() - load a module or sample; supported file formats: sunsynth, xi, wav, aiff;
                      return value: new module number or negative value in case of some error;
   sv_load_module_from_memory() - load a module or sample from the memory block;
   sv_sampler_load() - load a sample to already created Sampler; to replace the whole sampler - set sample_slot to -1;
   sv_sampler_load_from_memory() - load a sample from the memory block;
*/
int sv_new_module( int slot, const char* type, const char* name, int x, int y, int z ) SUNVOX_FN_ATTR; /* USE LOCK/UNLOCK! */
int sv_remove_module( int slot, int mod_num ) SUNVOX_FN_ATTR; /* USE LOCK/UNLOCK! */
int sv_connect_module( int slot, int source, int destination ) SUNVOX_FN_ATTR; /* USE LOCK/UNLOCK! */
int sv_disconnect_module( int slot, int source, int destination ) SUNVOX_FN_ATTR; /* USE LOCK/UNLOCK! */
int sv_load_module( int slot, const char* file_name, int x, int y, int z ) SUNVOX_FN_ATTR;
int sv_load_module_from_memory( int slot, void* data, unsigned int data_size, int x, int y, int z ) SUNVOX_FN_ATTR;
int sv_sampler_load( int slot, int sampler_module, const char* file_name, int sample_slot ) SUNVOX_FN_ATTR;
int sv_sampler_load_from_memory( int slot, int sampler_module, void* data, unsigned int data_size, int sample_slot ) SUNVOX_FN_ATTR;

/*
*/
int sv_get_number_of_modules( int slot ) SUNVOX_FN_ATTR;
unsigned int sv_get_module_flags( int slot, int mod_num ) SUNVOX_FN_ATTR; /* SV_MODULE_FLAG_xxx */

/*
   sv_get_module_inputs(), sv_get_module_outputs() - 
   get pointers to the int[] arrays with the input/output links.
   Number of inputs = ( module_flags & SV_MODULE_INPUTS_MASK ) >> SV_MODULE_INPUTS_OFF.
   Number of outputs = ( module_flags & SV_MODULE_OUTPUTS_MASK ) >> SV_MODULE_OUTPUTS_OFF.
*/
int* sv_get_module_inputs( int slot, int mod_num ) SUNVOX_FN_ATTR;
int* sv_get_module_outputs( int slot, int mod_num ) SUNVOX_FN_ATTR;

/*
*/
const char* sv_get_module_name( int slot, int mod_num ) SUNVOX_FN_ATTR;

/*
   sv_get_module_xy() - get module XY coordinates packed in a single uint32 value:
   ( x & 0xFFFF ) | ( ( y & 0xFFFF ) << 16 ).
   Normal working area: 0x0 ... 1024x1024
   Center: 512x512
   Use SV_GET_MODULE_XY() macro to unpack X and Y.
*/
unsigned int sv_get_module_xy( int slot, int mod_num ) SUNVOX_FN_ATTR;

/*
   sv_get_module_color() - get module color in the following format: 0xBBGGRR
*/
int sv_get_module_color( int slot, int mod_num ) SUNVOX_FN_ATTR;

/* 
   sv_get_module_scope2() return value = received number of samples (may be less or equal to samples_to_read). 
*/
unsigned int sv_get_module_scope2( int slot, int mod_num, int channel, signed short* dest_buf, unsigned int samples_to_read ) SUNVOX_FN_ATTR;

/*
*/
int sv_get_number_of_module_ctls( int slot, int mod_num ) SUNVOX_FN_ATTR;
const char* sv_get_module_ctl_name( int slot, int mod_num, int ctl_num ) SUNVOX_FN_ATTR;
int sv_get_module_ctl_value( int slot, int mod_num, int ctl_num, int scaled ) SUNVOX_FN_ATTR;
int sv_get_number_of_patterns( int slot ) SUNVOX_FN_ATTR;
int sv_get_pattern_x( int slot, int pat_num ) SUNVOX_FN_ATTR;
int sv_get_pattern_y( int slot, int pat_num ) SUNVOX_FN_ATTR;
int sv_get_pattern_tracks( int slot, int pat_num ) SUNVOX_FN_ATTR;
int sv_get_pattern_lines( int slot, int pat_num ) SUNVOX_FN_ATTR;

/*
   sv_get_pattern_data() - get the pattern buffer (for reading and writing)
   containing notes (events) in the following order:
     line 0: note for track 0, note for track 1, ... note for track X;
     line 1: note for track 0, note for track 1, ... note for track X;
     ...
     line X: ...
   Example:
     int pat_tracks = sv_get_pattern_tracks( slot, pat_num ); //number of tracks
     sunvox_note* data = sv_get_pattern_data( slot, pat_num ); //get the buffer with all the pattern events (notes)
     sunvox_note* n = &data[ line_number * pat_tracks + track_number ];
     ... and then do someting with note n ...
*/
sunvox_note* sv_get_pattern_data( int slot, int pat_num ) SUNVOX_FN_ATTR;

/*
*/
int sv_pattern_mute( int slot, int pat_num, int mute ) SUNVOX_FN_ATTR; /* USE LOCK/UNLOCK! */

/*
   SunVox engine uses its own time space, measured in system ticks (don't confuse it with the project ticks);
   required when calculating the out_time parameter in the sv_audio_callback().
   Use sv_get_ticks() to get current tick counter (from 0 to 0xFFFFFFFF).
   Use sv_get_ticks_per_second() to get the number of SunVox ticks per second.
*/
unsigned int sv_get_ticks( void ) SUNVOX_FN_ATTR;
unsigned int sv_get_ticks_per_second( void ) SUNVOX_FN_ATTR;

/*
   sv_get_log() - get the latest messages from the log
   Parameters:
     size - max number of bytes to read.
   Return value: pointer to the null-terminated string with the latest log messages.
*/
const char* sv_get_log( int size ) SUNVOX_FN_ATTR;

/*
   DEPRECATED FUNCTIONS
*/
int sv_get_sample_type( void ) SUNVOX_FN_ATTR; /* Get internal sample type of the SunVox engine. Return value: one of the SV_STYPE_xxx defines. Use it to get the scope buffer type from get_module_scope() function. */
void* sv_get_module_scope( int slot, int mod_num, int channel, int* buffer_offset, int* buffer_size ) SUNVOX_FN_ATTR; /* Use sv_get_module_scope2() */

#ifdef __cplusplus
} /* ...extern "C" */
#endif

/* ... SUNVOX_STATIC_LIB */
#else
/* DYNAMIC LIBRARY (DLL, SO, etc.) ... */

typedef int (SUNVOX_FN_ATTR *tsv_audio_callback)( void* buf, int frames, int latency, unsigned int out_time );
typedef int (SUNVOX_FN_ATTR *tsv_audio_callback2)( void* buf, int frames, int latency, unsigned int out_time, int in_type, int in_channels, void* in_buf );
typedef int (SUNVOX_FN_ATTR *tsv_open_slot)( int slot );
typedef int (SUNVOX_FN_ATTR *tsv_close_slot)( int slot );
typedef int (SUNVOX_FN_ATTR *tsv_lock_slot)( int slot );
typedef int (SUNVOX_FN_ATTR *tsv_unlock_slot)( int slot );
typedef int (SUNVOX_FN_ATTR *tsv_init)( const char* config, int freq, int channels, unsigned int flags );
typedef int (SUNVOX_FN_ATTR *tsv_deinit)( void );
typedef int (SUNVOX_FN_ATTR *tsv_update_input)( void );
typedef int (SUNVOX_FN_ATTR *tsv_get_sample_type)( void );
typedef int (SUNVOX_FN_ATTR *tsv_load)( int slot, const char* name );
typedef int (SUNVOX_FN_ATTR *tsv_load_from_memory)( int slot, void* data, unsigned int data_size );
typedef int (SUNVOX_FN_ATTR *tsv_play)( int slot );
typedef int (SUNVOX_FN_ATTR *tsv_play_from_beginning)( int slot );
typedef int (SUNVOX_FN_ATTR *tsv_stop)( int slot );
typedef int (SUNVOX_FN_ATTR *tsv_set_autostop)( int slot, int autostop );
typedef int (SUNVOX_FN_ATTR *tsv_end_of_song)( int slot );
typedef int (SUNVOX_FN_ATTR *tsv_rewind)( int slot, int t );
typedef int (SUNVOX_FN_ATTR *tsv_volume)( int slot, int vol );
typedef int (SUNVOX_FN_ATTR *tsv_send_event)( int slot, int track_num, int note, int vel, int module, int ctl, int ctl_val );
typedef int (SUNVOX_FN_ATTR *tsv_get_current_line)( int slot );
typedef int (SUNVOX_FN_ATTR *tsv_get_current_line2)( int slot );
typedef int (SUNVOX_FN_ATTR *tsv_get_current_signal_level)( int slot, int channel );
typedef const char* (SUNVOX_FN_ATTR *tsv_get_song_name)( int slot );
typedef int (SUNVOX_FN_ATTR *tsv_get_song_bpm)( int slot );
typedef int (SUNVOX_FN_ATTR *tsv_get_song_tpl)( int slot );
typedef unsigned int (SUNVOX_FN_ATTR *tsv_get_song_length_frames)( int slot );
typedef unsigned int (SUNVOX_FN_ATTR *tsv_get_song_length_lines)( int slot );
typedef int (SUNVOX_FN_ATTR *tsv_new_module)( int slot, const char* type, const char* name, int x, int y, int z );
typedef int (SUNVOX_FN_ATTR *tsv_remove_module)( int slot, int mod_num );
typedef int (SUNVOX_FN_ATTR *tsv_connect_module)( int slot, int source, int destination );
typedef int (SUNVOX_FN_ATTR *tsv_disconnect_module)( int slot, int source, int destination );
typedef int (SUNVOX_FN_ATTR *tsv_load_module)( int slot, const char* file_name, int x, int y, int z );
typedef int (SUNVOX_FN_ATTR *tsv_load_module_from_memory)( int slot, void* data, unsigned int data_size, int x, int y, int z );
typedef int (SUNVOX_FN_ATTR *tsv_sampler_load)( int slot, int sampler_module, const char* file_name, int sample_slot );
typedef int (SUNVOX_FN_ATTR *tsv_sampler_load_from_memory)( int slot, int sampler_module, void* data, unsigned int data_size, int sample_slot );
typedef int (SUNVOX_FN_ATTR *tsv_get_number_of_modules)( int slot );
typedef unsigned int (SUNVOX_FN_ATTR *tsv_get_module_flags)( int slot, int mod_num );
typedef int* (SUNVOX_FN_ATTR *tsv_get_module_inputs)( int slot, int mod_num );
typedef int* (SUNVOX_FN_ATTR *tsv_get_module_outputs)( int slot, int mod_num );
typedef const char* (SUNVOX_FN_ATTR *tsv_get_module_name)( int slot, int mod_num );
typedef unsigned int (SUNVOX_FN_ATTR *tsv_get_module_xy)( int slot, int mod_num );
typedef int (SUNVOX_FN_ATTR *tsv_get_module_color)( int slot, int mod_num );
typedef void* (SUNVOX_FN_ATTR *tsv_get_module_scope)( int slot, int mod_num, int channel, int* buffer_offset, int* buffer_size );
typedef unsigned int (SUNVOX_FN_ATTR *tsv_get_module_scope2)( int slot, int mod_num, int channel, signed short* dest_buf, unsigned int samples_to_read );
typedef int (SUNVOX_FN_ATTR *tsv_get_number_of_module_ctls)( int slot, int mod_num );
typedef const char* (SUNVOX_FN_ATTR *tsv_get_module_ctl_name)( int slot, int mod_num, int ctl_num );
typedef int (SUNVOX_FN_ATTR *tsv_get_module_ctl_value)( int slot, int mod_num, int ctl_num, int scaled );
typedef int (SUNVOX_FN_ATTR *tsv_get_number_of_patterns)( int slot );
typedef int (SUNVOX_FN_ATTR *tsv_get_pattern_x)( int slot, int pat_num );
typedef int (SUNVOX_FN_ATTR *tsv_get_pattern_y)( int slot, int pat_num );
typedef int (SUNVOX_FN_ATTR *tsv_get_pattern_tracks)( int slot, int pat_num );
typedef int (SUNVOX_FN_ATTR *tsv_get_pattern_lines)( int slot, int pat_num );
typedef sunvox_note* (SUNVOX_FN_ATTR *tsv_get_pattern_data)( int slot, int pat_num );
typedef int (SUNVOX_FN_ATTR *tsv_pattern_mute)( int slot, int pat_num, int mute );
typedef unsigned int (SUNVOX_FN_ATTR *tsv_get_ticks)( void );
typedef unsigned int (SUNVOX_FN_ATTR *tsv_get_ticks_per_second)( void );
typedef const char* (SUNVOX_FN_ATTR *tsv_get_log)( int size );

#ifdef SUNVOX_MAIN
    #define SV_FN_DECL
    #define SV_FN_DECL2 =0
#else
    #define SV_FN_DECL extern
    #define SV_FN_DECL2
#endif

SV_FN_DECL tsv_audio_callback sv_audio_callback SV_FN_DECL2;
SV_FN_DECL tsv_audio_callback2 sv_audio_callback2 SV_FN_DECL2;
SV_FN_DECL tsv_open_slot sv_open_slot SV_FN_DECL2;
SV_FN_DECL tsv_close_slot sv_close_slot SV_FN_DECL2;
SV_FN_DECL tsv_lock_slot sv_lock_slot SV_FN_DECL2;
SV_FN_DECL tsv_unlock_slot sv_unlock_slot SV_FN_DECL2;
SV_FN_DECL tsv_init sv_init SV_FN_DECL2;
SV_FN_DECL tsv_deinit sv_deinit SV_FN_DECL2;
SV_FN_DECL tsv_update_input sv_update_input SV_FN_DECL2;
SV_FN_DECL tsv_get_sample_type sv_get_sample_type SV_FN_DECL2;
SV_FN_DECL tsv_load sv_load SV_FN_DECL2;
SV_FN_DECL tsv_load_from_memory sv_load_from_memory SV_FN_DECL2;
SV_FN_DECL tsv_play sv_play SV_FN_DECL2;
SV_FN_DECL tsv_play_from_beginning sv_play_from_beginning SV_FN_DECL2;
SV_FN_DECL tsv_stop sv_stop SV_FN_DECL2;
SV_FN_DECL tsv_set_autostop sv_set_autostop SV_FN_DECL2;
SV_FN_DECL tsv_end_of_song sv_end_of_song SV_FN_DECL2;
SV_FN_DECL tsv_rewind sv_rewind SV_FN_DECL2;
SV_FN_DECL tsv_volume sv_volume SV_FN_DECL2;
SV_FN_DECL tsv_send_event sv_send_event SV_FN_DECL2;
SV_FN_DECL tsv_get_current_line sv_get_current_line SV_FN_DECL2;
SV_FN_DECL tsv_get_current_line2 sv_get_current_line2 SV_FN_DECL2;
SV_FN_DECL tsv_get_current_signal_level sv_get_current_signal_level SV_FN_DECL2;
SV_FN_DECL tsv_get_song_name sv_get_song_name SV_FN_DECL2;
SV_FN_DECL tsv_get_song_bpm sv_get_song_bpm SV_FN_DECL2;
SV_FN_DECL tsv_get_song_tpl sv_get_song_tpl SV_FN_DECL2;
SV_FN_DECL tsv_get_song_length_frames sv_get_song_length_frames SV_FN_DECL2;
SV_FN_DECL tsv_get_song_length_lines sv_get_song_length_lines SV_FN_DECL2;
SV_FN_DECL tsv_new_module sv_new_module SV_FN_DECL2;
SV_FN_DECL tsv_remove_module sv_remove_module SV_FN_DECL2;
SV_FN_DECL tsv_connect_module sv_connect_module SV_FN_DECL2;
SV_FN_DECL tsv_disconnect_module sv_disconnect_module SV_FN_DECL2;
SV_FN_DECL tsv_load_module sv_load_module SV_FN_DECL2;
SV_FN_DECL tsv_load_module_from_memory sv_load_module_from_memory SV_FN_DECL2;
SV_FN_DECL tsv_sampler_load sv_sampler_load SV_FN_DECL2;
SV_FN_DECL tsv_sampler_load_from_memory sv_sampler_load_from_memory SV_FN_DECL2;
SV_FN_DECL tsv_get_number_of_modules sv_get_number_of_modules SV_FN_DECL2;
SV_FN_DECL tsv_get_module_flags sv_get_module_flags SV_FN_DECL2;
SV_FN_DECL tsv_get_module_inputs sv_get_module_inputs SV_FN_DECL2;
SV_FN_DECL tsv_get_module_outputs sv_get_module_outputs SV_FN_DECL2;
SV_FN_DECL tsv_get_module_name sv_get_module_name SV_FN_DECL2;
SV_FN_DECL tsv_get_module_xy sv_get_module_xy SV_FN_DECL2;
SV_FN_DECL tsv_get_module_color sv_get_module_color SV_FN_DECL2;
SV_FN_DECL tsv_get_module_scope sv_get_module_scope SV_FN_DECL2;
SV_FN_DECL tsv_get_module_scope2 sv_get_module_scope2 SV_FN_DECL2;
SV_FN_DECL tsv_get_number_of_module_ctls sv_get_number_of_module_ctls SV_FN_DECL2;
SV_FN_DECL tsv_get_module_ctl_name sv_get_module_ctl_name SV_FN_DECL2;
SV_FN_DECL tsv_get_module_ctl_value sv_get_module_ctl_value SV_FN_DECL2;
SV_FN_DECL tsv_get_number_of_patterns sv_get_number_of_patterns SV_FN_DECL2;
SV_FN_DECL tsv_get_pattern_x sv_get_pattern_x SV_FN_DECL2;
SV_FN_DECL tsv_get_pattern_y sv_get_pattern_y SV_FN_DECL2;
SV_FN_DECL tsv_get_pattern_tracks sv_get_pattern_tracks SV_FN_DECL2;
SV_FN_DECL tsv_get_pattern_lines sv_get_pattern_lines SV_FN_DECL2;
SV_FN_DECL tsv_get_pattern_data sv_get_pattern_data SV_FN_DECL2;
SV_FN_DECL tsv_pattern_mute sv_pattern_mute SV_FN_DECL2;
SV_FN_DECL tsv_get_ticks sv_get_ticks SV_FN_DECL2;
SV_FN_DECL tsv_get_ticks_per_second sv_get_ticks_per_second SV_FN_DECL2;
SV_FN_DECL tsv_get_log sv_get_log SV_FN_DECL2;

#ifdef SUNVOX_MAIN

#ifdef WIN
#define IMPORT( Handle, Type, Function, Store ) \
    { \
	Store = (Type)GetProcAddress( Handle, Function ); \
	if( Store == 0 ) { fn_not_found = Function; break; } \
    }
#define ERROR_MSG( msg ) MessageBoxA( 0, msg, "Error", MB_OK );
#endif

#ifdef UNIX
#define IMPORT( Handle, Type, Function, Store ) \
    { \
	Store = (Type)dlsym( Handle, Function ); \
	if( Store == 0 ) { fn_not_found = Function; break; } \
    }
#define ERROR_MSG( msg ) printf( "ERROR: %s\n", msg );
#endif

#ifdef UNIX
    void* g_sv_dll = 0;
#endif

#ifdef WIN
    HMODULE g_sv_dll = 0;
#endif

int sv_load_dll2( LIBNAME_STR_TYPE filename )
{
#ifdef WIN
    g_sv_dll = LoadLibrary( filename );
    if( g_sv_dll == 0 ) 
    {
        printf( "LoadLibrary() error %d\n", GetLastError() );
        ERROR_MSG( "can't load sunvox.dll" );
        return -1;
    }
#endif
#ifdef UNIX
    g_sv_dll = dlopen( filename, RTLD_NOW );
    if( g_sv_dll == 0 )
    {
	printf( "%s\n", dlerror() );
        return -1;
    }
#endif
    const char* fn_not_found = 0;
    while( 1 )
    {
	IMPORT( g_sv_dll, tsv_audio_callback, "sv_audio_callback", sv_audio_callback );
	IMPORT( g_sv_dll, tsv_audio_callback2, "sv_audio_callback2", sv_audio_callback2 );
	IMPORT( g_sv_dll, tsv_open_slot, "sv_open_slot", sv_open_slot );
	IMPORT( g_sv_dll, tsv_close_slot, "sv_close_slot", sv_close_slot );
	IMPORT( g_sv_dll, tsv_lock_slot, "sv_lock_slot", sv_lock_slot );
	IMPORT( g_sv_dll, tsv_unlock_slot, "sv_unlock_slot", sv_unlock_slot );
	IMPORT( g_sv_dll, tsv_init, "sv_init", sv_init );
	IMPORT( g_sv_dll, tsv_deinit, "sv_deinit", sv_deinit );
	IMPORT( g_sv_dll, tsv_update_input, "sv_update_input", sv_update_input );
	IMPORT( g_sv_dll, tsv_get_sample_type, "sv_get_sample_type", sv_get_sample_type );
	IMPORT( g_sv_dll, tsv_load, "sv_load", sv_load );
	IMPORT( g_sv_dll, tsv_load_from_memory, "sv_load_from_memory", sv_load_from_memory );
	IMPORT( g_sv_dll, tsv_play, "sv_play", sv_play );
	IMPORT( g_sv_dll, tsv_play_from_beginning, "sv_play_from_beginning", sv_play_from_beginning );
	IMPORT( g_sv_dll, tsv_stop, "sv_stop", sv_stop );
	IMPORT( g_sv_dll, tsv_set_autostop, "sv_set_autostop", sv_set_autostop );
	IMPORT( g_sv_dll, tsv_end_of_song, "sv_end_of_song", sv_end_of_song );
	IMPORT( g_sv_dll, tsv_rewind, "sv_rewind", sv_rewind );
	IMPORT( g_sv_dll, tsv_volume, "sv_volume", sv_volume );
	IMPORT( g_sv_dll, tsv_send_event, "sv_send_event", sv_send_event );
	IMPORT( g_sv_dll, tsv_get_current_line, "sv_get_current_line", sv_get_current_line );
	IMPORT( g_sv_dll, tsv_get_current_line2, "sv_get_current_line2", sv_get_current_line2 );
	IMPORT( g_sv_dll, tsv_get_current_signal_level, "sv_get_current_signal_level", sv_get_current_signal_level );
	IMPORT( g_sv_dll, tsv_get_song_name, "sv_get_song_name", sv_get_song_name );
	IMPORT( g_sv_dll, tsv_get_song_bpm, "sv_get_song_bpm", sv_get_song_bpm );
	IMPORT( g_sv_dll, tsv_get_song_tpl, "sv_get_song_tpl", sv_get_song_tpl );
	IMPORT( g_sv_dll, tsv_get_song_length_frames, "sv_get_song_length_frames", sv_get_song_length_frames );
	IMPORT( g_sv_dll, tsv_get_song_length_lines, "sv_get_song_length_lines", sv_get_song_length_lines );
	IMPORT( g_sv_dll, tsv_new_module, "sv_new_module", sv_new_module );
	IMPORT( g_sv_dll, tsv_remove_module, "sv_remove_module", sv_remove_module );
	IMPORT( g_sv_dll, tsv_connect_module, "sv_connect_module", sv_connect_module );
	IMPORT( g_sv_dll, tsv_disconnect_module, "sv_disconnect_module", sv_disconnect_module );
	IMPORT( g_sv_dll, tsv_load_module, "sv_load_module", sv_load_module );
	IMPORT( g_sv_dll, tsv_load_module_from_memory, "sv_load_module_from_memory", sv_load_module_from_memory );
	IMPORT( g_sv_dll, tsv_sampler_load, "sv_sampler_load", sv_sampler_load );
	IMPORT( g_sv_dll, tsv_sampler_load_from_memory, "sv_sampler_load_from_memory", sv_sampler_load_from_memory );
	IMPORT( g_sv_dll, tsv_get_number_of_modules, "sv_get_number_of_modules", sv_get_number_of_modules );
	IMPORT( g_sv_dll, tsv_get_module_flags, "sv_get_module_flags", sv_get_module_flags );
	IMPORT( g_sv_dll, tsv_get_module_inputs, "sv_get_module_inputs", sv_get_module_inputs );
	IMPORT( g_sv_dll, tsv_get_module_outputs, "sv_get_module_outputs", sv_get_module_outputs );
	IMPORT( g_sv_dll, tsv_get_module_name, "sv_get_module_name", sv_get_module_name );
	IMPORT( g_sv_dll, tsv_get_module_xy, "sv_get_module_xy", sv_get_module_xy );
	IMPORT( g_sv_dll, tsv_get_module_color, "sv_get_module_color", sv_get_module_color );
	IMPORT( g_sv_dll, tsv_get_module_scope, "sv_get_module_scope", sv_get_module_scope );
	IMPORT( g_sv_dll, tsv_get_module_scope2, "sv_get_module_scope2", sv_get_module_scope2 );
	IMPORT( g_sv_dll, tsv_get_number_of_module_ctls, "sv_get_number_of_module_ctls", sv_get_number_of_module_ctls );
	IMPORT( g_sv_dll, tsv_get_module_ctl_name, "sv_get_module_ctl_name", sv_get_module_ctl_name );
	IMPORT( g_sv_dll, tsv_get_module_ctl_value, "sv_get_module_ctl_value", sv_get_module_ctl_value );
	IMPORT( g_sv_dll, tsv_get_number_of_patterns, "sv_get_number_of_patterns", sv_get_number_of_patterns );
	IMPORT( g_sv_dll, tsv_get_pattern_x, "sv_get_pattern_x", sv_get_pattern_x );
	IMPORT( g_sv_dll, tsv_get_pattern_y, "sv_get_pattern_y", sv_get_pattern_y );
	IMPORT( g_sv_dll, tsv_get_pattern_tracks, "sv_get_pattern_tracks", sv_get_pattern_tracks );
	IMPORT( g_sv_dll, tsv_get_pattern_lines, "sv_get_pattern_lines", sv_get_pattern_lines );
	IMPORT( g_sv_dll, tsv_get_pattern_data, "sv_get_pattern_data", sv_get_pattern_data );
	IMPORT( g_sv_dll, tsv_pattern_mute, "sv_pattern_mute", sv_pattern_mute );
	IMPORT( g_sv_dll, tsv_get_ticks, "sv_get_ticks", sv_get_ticks );
	IMPORT( g_sv_dll, tsv_get_ticks_per_second, "sv_get_ticks_per_second", sv_get_ticks_per_second );
	IMPORT( g_sv_dll, tsv_get_log, "sv_get_log", sv_get_log );
	break;
    }
    if( fn_not_found )
    {
	char ts[ 256 ];
	sprintf( ts, "sunvox lib: %s() not found", fn_not_found );
	ERROR_MSG( ts );
	return -2;
    }
    
    return 0;
}

int sv_load_dll( void )
{
#ifdef WIN
    return sv_load_dll2( TEXT(LIBNAME) );
#else
    return sv_load_dll2( LIBNAME );
#endif
    return -1111;
}

int sv_unload_dll( void )
{
#ifdef UNIX
    if( g_sv_dll ) dlclose( g_sv_dll );
#endif
    return 0;
}

#else /* ... SUNVOX_MAIN */

int sv_load_dll2( LIBNAME_STR_TYPE filename );
int sv_load_dll( void );
int sv_unload_dll( void );

#endif /* ... not SUNVOX_MAIN */

#endif /* ... DYNAMIC LIBRARY */

#endif
