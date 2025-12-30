/*
    sunvox_lib.cpp
    This file is part of the SunVox Library.
    Copyright (C) 2012 - 2025 Alexander Zolotov <nightradio@gmail.com>
    warmplace.ru
*/

#include "sundog.h"
#define SVH_INLINES
#include "sunvox_engine_helper.h"
#include "psynth/psynths_sampler.h"
#include "psynth/psynths_metamodule.h"
#include "psynth/psynths_vorbis_player.h"

//There are three types of functions provided by the SunVox library:
// 1) functions that don't require slot lock by the user: sv_load (and all other load fns), sv_send_event, etc.;
// 2) functions that can only be called from a locked slot (marked as "USE LOCK/UNLOCK!"): sv_new_module, sv_remove_module, sv_connect_module, etc.;
// 3) functions that require slot locking only when similar SunVox data is modified and read in different threads;
//    example:
//      thread 1: sv_lock_slot(0); sv_get_module_flags(0,mod1); sv_unlock_slot(0);
//      thread 2: sv_lock_slot(0); sv_remove_module(0,mod2); sv_unlock_slot(0);

#ifdef OS_WIN
    #define SUNVOX_EXPORT extern "C" __declspec(dllexport) __stdcall
#endif
#if defined(OS_APPLE) || defined(OS_LINUX) || defined(OS_EMSCRIPTEN)
    #define SUNVOX_EXPORT extern "C"
#endif
#ifndef SUNVOX_EXPORT
    #define SUNVOX_EXPORT
#endif

const char* g_app_config[] = { "1:/sunvox_dll_config.ini", "2:/sunvox_dll_config.ini", "0" };
const char* g_app_log = "3:/sunvox_dll_log.txt";
const char* g_app_name = "SunVox Library";
const char* g_app_name_short = "SunVox Library";

static sundog_sound* g_sound = NULL;
static sunvox_engine* g_sv[ SUNDOG_SOUND_SLOTS ];
static volatile int g_sv_locked[ SUNDOG_SOUND_SLOTS ];
static stime_ticks_t g_sv_evt_t[ SUNDOG_SOUND_SLOTS ];
static bool g_sv_evt_t_set[ SUNDOG_SOUND_SLOTS ];
static uint g_sv_flags;
static int g_sv_freq;
static int g_sv_channels;
static char* g_sv_log = NULL;
static bool g_sv_initialized;

#define SV_INIT_FLAG_NO_DEBUG_OUTPUT 		( 1 << 0 )
#define SV_INIT_FLAG_USER_AUDIO_CALLBACK 	( 1 << 1 )
#define SV_INIT_FLAG_AUDIO_INT16 		( 1 << 2 )
#define SV_INIT_FLAG_AUDIO_FLOAT32 		( 1 << 3 )
#define SV_INIT_FLAG_ONE_THREAD 		( 1 << 4 )

#define SV_TIME_MAP_SPEED       0
#define SV_TIME_MAP_FRAMECNT    1
#define SV_TIME_MAP_TYPE_MASK   3

#define SV_MODULE_FLAG_EXISTS		( 1 << 0 )
#define SV_MODULE_FLAG_GENERATOR        ( 1 << 1 ) /* Note input + Sound output */
#define SV_MODULE_FLAG_EFFECT           ( 1 << 2 ) /* Sound input + Sound output */
#define SV_MODULE_FLAG_MUTE 		( 1 << 3 )
#define SV_MODULE_FLAG_SOLO 		( 1 << 4 )
#define SV_MODULE_FLAG_BYPASS 		( 1 << 5 )
#define SV_MODULE_INPUTS_OFF 	16
#define SV_MODULE_INPUTS_MASK 	( 255 << SV_MODULE_INPUTS_OFF )
#define SV_MODULE_OUTPUTS_OFF 	( 16 + 8 )
#define SV_MODULE_OUTPUTS_MASK 	( 255 << SV_MODULE_OUTPUTS_OFF )

#ifdef OS_EMSCRIPTEN
    //For iOS-version of Safari only:
    //https://developer.apple.com/library/content/documentation/AudioVideo/Conceptual/Using_HTML5_Audio_Video/PlayingandSynthesizingSounds/PlayingandSynthesizingSounds.html
    #define DEFERRED_SOUND_STREAM_INIT
#endif

#ifdef OS_ANDROID
    #include <jni.h>
#endif

int app_global_init()
{
    int rv = 0;
    if( sunvox_global_init() ) rv = -1;
    return rv;
}

int app_global_deinit()
{
    int rv = 0;
    if( sunvox_global_deinit() ) rv = -1;
    return rv;
}

SUNVOX_EXPORT int sv_deinit( void )
{
    if( !g_sv_initialized ) return -1;
    if( g_sound )
    {
	sundog_sound_deinit( g_sound );
	smem_free( g_sound );
	g_sound = NULL;
    }
    smem_free( g_sv_log );
    g_sv_log = NULL;
    sundog_global_deinit();
    g_sv_initialized = false;
    return 0;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_deinit( JNIEnv* je, jclass jc )
{
    return sv_deinit();
}
#endif

SUNVOX_EXPORT int sv_init( const char* config, int freq, int channels, uint flags )
{
    if( g_sv_initialized )
    {
	slog( "sv_init(): already initialized!\n" );
	return -1;
    }
    sundog_global_init();
    int rv = -1;
    while( 1 )
    {
#ifdef OS_MAEMO
	sconfig_set_int_value( APP_CFG_SND_BUF, sconfig_get_int_value( APP_CFG_SND_BUF, 2048, 0 ), 0 );
	sconfig_set_str_value( APP_CFG_SND_DRIVER, sconfig_get_str_value( APP_CFG_SND_DRIVER, "sdl", 0 ), 0 );
#endif
	sconfig_load_from_string( config, '|', NULL );
	for( int i = 0; i < SUNDOG_SOUND_SLOTS; i++ ) g_sv[ i ] = NULL;
	sound_buffer_type type = sound_buffer_int16;
	uint stream_flags = 0;
#ifdef OS_MACOS
	if( ( flags & SV_INIT_FLAG_USER_AUDIO_CALLBACK ) == 0 )
	    type = sound_buffer_float32;
#endif
#ifdef OS_EMSCRIPTEN
	flags |= SV_INIT_FLAG_ONE_THREAD;
#endif
	if( flags & SV_INIT_FLAG_AUDIO_INT16 )
	    type = sound_buffer_int16;
	if( flags & SV_INIT_FLAG_AUDIO_FLOAT32 )
	    type = sound_buffer_float32;
	if( flags & SV_INIT_FLAG_NO_DEBUG_OUTPUT )
	    slog_disable( 1, 1 );
	if( flags & SV_INIT_FLAG_ONE_THREAD )
	    stream_flags |= SUNDOG_SOUND_FLAG_ONE_THREAD;
	g_sound = SMEM_ZALLOC2( sundog_sound, 1 );
	if( flags & SV_INIT_FLAG_USER_AUDIO_CALLBACK )
	{
	    if( sundog_sound_init( g_sound, 0, type, freq, channels, stream_flags | SUNDOG_SOUND_FLAG_USER_CONTROLLED ) )
		break;
	}
	else
	{
#ifdef DEFERRED_SOUND_STREAM_INIT
	    stream_flags |= SUNDOG_SOUND_FLAG_DEFERRED_INIT;
#endif
	    if( sundog_sound_init( g_sound, 0, type, freq, channels, stream_flags ) )
		break;
	}
	g_sv_freq = freq;
	g_sv_channels = channels;
	g_sv_flags = flags;
	g_sv_initialized = true;
	rv = 0;
	break;
    }
    if( rv < 0 )
    {
	//Some error:
	sv_deinit();
	return -1;
    }
    return SUNVOX_ENGINE_VERSION >> 8;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_init( JNIEnv* je, jclass jc, jstring config, jint freq, jint channels, jint flags )
{
    jint rv = 0;
    const char* c_config = NULL;
    if( config ) c_config = je->GetStringUTFChars( config, 0 );
    rv = sv_init( c_config, freq, channels, flags );
    if( config ) je->ReleaseStringUTFChars( config, c_config );
    return rv;
}
#endif

SUNVOX_EXPORT int sv_get_sample_rate( void )
{
    return g_sound->freq;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_get_1sample_1rate( JNIEnv* je, jclass jc )
{
    return sv_get_sample_rate();
}
#endif

SUNVOX_EXPORT int sv_update_input( void )
{
    sundog_sound_handle_input_requests( g_sound );
    return 0;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_update_1input( JNIEnv* je, jclass jc )
{
    return sv_update_input();
}
#endif

SUNVOX_EXPORT int sv_audio_callback( void* buf, int frames, int latency, stime_ticks_t out_time )
{
    return user_controlled_sound_callback( g_sound, buf, frames, latency, out_time );
}
SUNVOX_EXPORT int sv_audio_callback2( void* buf, int frames, int latency, stime_ticks_t out_time, int in_type, int in_channels, void* in_buf )
{
    sound_buffer_type type = sound_buffer_int16;
    if( in_type == 1 ) type = sound_buffer_float32;
    return user_controlled_sound_callback( g_sound, buf, frames, latency, out_time, type, in_channels, in_buf );
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_audio_1callback( JNIEnv* je, jclass jc, jbyteArray buf, jint frames, jint latency, jint out_time )
{
    int rv;
    jbyte* c_buf = je->GetByteArrayElements( buf, NULL );
    rv = sv_audio_callback( c_buf, frames, latency, out_time );
    je->ReleaseByteArrayElements( buf, c_buf, 0 );
    return rv;
}
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_audio_1callback2( JNIEnv* je, jclass jc, jbyteArray buf, jint frames, jint latency, jint out_time, jint in_type, jint in_channels, jbyteArray in_buf )
{
    int rv;
    jbyte* c_buf = je->GetByteArrayElements( buf, NULL );
    jbyte* c_buf2 = je->GetByteArrayElements( in_buf, NULL );
    rv = sv_audio_callback2( c_buf, frames, latency, out_time, in_type, in_channels, c_buf2 );
    je->ReleaseByteArrayElements( buf, c_buf, 0 );
    je->ReleaseByteArrayElements( in_buf, c_buf2, 0 );
    return rv;
}
#endif

int render_piece_of_sound( sundog_sound* ss, int slot_num )
{
    int handled = 0;

    if( !ss ) return 0;

    sundog_sound_slot* slot = &ss->slots[ slot_num ];
    sunvox_engine* s = (sunvox_engine*)slot->user_data;

    if( !s ) return 0;
    if( !s->initialized ) return 0;

    sunvox_render_data rdata;
    SMEM_CLEAR_STRUCT( rdata );
    rdata.buffer_type = ss->out_type;
    rdata.buffer = slot->buffer;
    rdata.frames = slot->frames;
    rdata.channels = ss->out_channels;
    rdata.out_latency = ss->out_latency;
    rdata.out_latency2 = ss->out_latency2;
    rdata.out_time = slot->time;
    rdata.in_buffer = slot->in_buffer;
    rdata.in_type = ss->in_type;
    rdata.in_channels = ss->in_channels;

    handled = sunvox_render_piece_of_sound( &rdata, s );

    if( handled && rdata.silence )
        handled = 2;

    return handled;
}

int sv_sound_stream_control( sunvox_stream_command cmd, void* user_data, sunvox_engine* s )
{
    size_t v = (size_t)user_data;
    int slot_num = (int)v;
    int rv = 0;
    switch( cmd )
    {
	case SUNVOX_STREAM_LOCK:
	    g_sv_locked[ slot_num ]++;
	    if( !( g_sv_flags & SV_INIT_FLAG_ONE_THREAD ) )
		sundog_sound_lock( g_sound );
	    break;

	case SUNVOX_STREAM_UNLOCK:
	    if( !( g_sv_flags & SV_INIT_FLAG_ONE_THREAD ) )
		sundog_sound_unlock( g_sound );
	    g_sv_locked[ slot_num ]--;
	    break;

	case SUNVOX_STREAM_STOP:
	    sundog_sound_stop( g_sound, slot_num );
	    break;

	case SUNVOX_STREAM_PLAY:
	    sundog_sound_play( g_sound, slot_num );
	    break;

    	case SUNVOX_STREAM_ENABLE_INPUT:
            sundog_sound_input_request( g_sound, true );
            break;

    	case SUNVOX_STREAM_DISABLE_INPUT:
            sundog_sound_input_request( g_sound, false );
            break;

	case SUNVOX_STREAM_SYNC:
    	    sundog_sound_slot_sync( g_sound, slot_num, s->stream_control_par_sync );
    	    break;

	case SUNVOX_STREAM_SYNC_PLAY:
	    sundog_sound_sync_play( g_sound, slot_num, true );
	    break;

    	case SUNVOX_STREAM_IS_SUSPENDED:
            rv = sundog_sound_is_slot_suspended( g_sound, slot_num );
            break;

        default: break;
    }
    return rv;
}

static bool check_slot( int slot )
{
    if( (unsigned)slot >= (unsigned)SUNDOG_SOUND_SLOTS )
    {
	slog_enable( 1, 1 );
	slog( "Wrong slot number %d! Correct values: 0...%d\n", slot, SUNDOG_SOUND_SLOTS - 1 );
	return true;
    }
    if( !g_sv[ slot ] ) return true;
    return false;
}

static bool is_sv_locked( int slot, const char* fn_name )
{
    if( ( g_sv_flags & SV_INIT_FLAG_ONE_THREAD ) == 0 && g_sv_locked[ slot ] <= 0 )
    {
	slog_enable( 1, 1 );
        slog( "%s error: use it within sv_lock_slot() / sv_unlock_slot() block only!\n", fn_name );
        return false;
    }
    return true;
}

SUNVOX_EXPORT int sv_open_slot( int slot )
{
    if( (unsigned)slot >= (unsigned)SUNDOG_SOUND_SLOTS )
    {
	slog_enable( 1, 1 );
	slog( "Wrong slot number %d! Correct values: 0...%d\n", slot, SUNDOG_SOUND_SLOTS - 1 );
	return -1;
    }
    uint flags = 0;
    if( g_sv_flags & SV_INIT_FLAG_ONE_THREAD ) flags |= SUNVOX_FLAG_ONE_THREAD;
    g_sv[ slot ] = SMEM_ALLOC2( sunvox_engine, 1 );
    g_sv_locked[ slot ] = 0;
    sunvox_engine_init( 
	SUNVOX_FLAG_CREATE_PATTERN | SUNVOX_FLAG_CREATE_MODULES | SUNVOX_FLAG_MAIN | flags, 
	g_sound->freq,
	0, 0, sv_sound_stream_control, (void*)((size_t)slot), g_sv[ slot ] );
    sundog_sound_set_slot_callback( g_sound, slot, &render_piece_of_sound, g_sv[ slot ] );
    sundog_sound_play( g_sound, slot );
    return 0;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_open_1slot( JNIEnv* je, jclass jc, jint slot )
{
    return sv_open_slot( slot );
}
#endif

SUNVOX_EXPORT int sv_close_slot( int slot )
{
    if( check_slot( slot ) ) return -1;
    sundog_sound_remove_slot_callback( g_sound, slot );
    sunvox_engine_close( g_sv[ slot ] );
    smem_free( g_sv[ slot ] );
    g_sv[ slot ] = NULL;
    g_sv_locked[ slot ] = 0;
    return 0;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_close_1slot( JNIEnv* je, jclass jc, jint slot )
{
    return sv_close_slot( slot );
}
#endif

SUNVOX_EXPORT int sv_lock_slot( int slot )
{
    if( check_slot( slot ) ) return -1;
    SUNVOX_SOUND_STREAM_CONTROL( g_sv[ slot ], SUNVOX_STREAM_LOCK );
    return 0;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_lock_1slot( JNIEnv* je, jclass jc, jint slot )
{
    return sv_lock_slot( slot );
}
#endif

SUNVOX_EXPORT int sv_unlock_slot( int slot )
{
    if( check_slot( slot ) ) return -1;
    SUNVOX_SOUND_STREAM_CONTROL( g_sv[ slot ], SUNVOX_STREAM_UNLOCK );
    return 0;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_unlock_1slot( JNIEnv* je, jclass jc, jint slot )
{
    return sv_unlock_slot( slot );
}
#endif

SUNVOX_EXPORT int sv_load( int slot, const char* name )
{
    if( check_slot( slot ) ) return -1;
    int rv = sunvox_load_proj( name, 0, g_sv[ slot ] );
    if( rv == 0 ) sundog_sound_handle_input_requests( g_sound );
    return rv;
}
SUNVOX_EXPORT int sv_load_from_memory( int slot, void* data, uint data_size )
{
    if( check_slot( slot ) ) return -1;
    int rv = -1;
    sfs_file f = sfs_open_in_memory( data, data_size );
    if( f )
    {
	rv = sunvox_load_proj_from_fd( f, 0, g_sv[ slot ] );
	sfs_close( f );
    }
    if( rv == 0 ) sundog_sound_handle_input_requests( g_sound );
    return rv;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_load( JNIEnv* je, jclass jc, jint slot, jstring name )
{
    jint rv = 0;
    const char* c_name = NULL;
    if( name ) c_name = je->GetStringUTFChars( name, 0 );
    rv = sv_load( slot, c_name );
    if( name ) je->ReleaseStringUTFChars( name, c_name );
    return rv;
}
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_load_1from_1memory( JNIEnv* je, jclass jc, jint slot, jbyteArray data )
{
    jint rv = 0;
    uint data_size = (uint)je->GetArrayLength( data );
    jbyte* c_data = je->GetByteArrayElements( data, NULL );
    rv = sv_load_from_memory( slot, c_data, data_size );
    je->ReleaseByteArrayElements( data, c_data, 0 );
    return rv;
}
#endif

SUNVOX_EXPORT int sv_save( int slot, const char* name )
{
    if( check_slot( slot ) ) return -1;
    int rv = sunvox_save_proj( name, 0, g_sv[ slot ] );
    return rv;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_save( JNIEnv* je, jclass jc, jint slot, jstring name )
{
    jint rv = 0;
    const char* c_name = NULL;
    if( name ) c_name = je->GetStringUTFChars( name, 0 );
    rv = sv_save( slot, c_name );
    if( name ) je->ReleaseStringUTFChars( name, c_name );
    return rv;
}
#endif

SUNVOX_EXPORT void* sv_save_to_memory( int slot, size_t* size )
{
    if( check_slot( slot ) ) return NULL;
    int rv = 0;
    if( size ) *size = 0;
    void* out = NULL;
    sfs_file f = sfs_open_in_memory( SMEM_ALLOC( 16 ), 0 );
    if( f )
    {
	rv = sunvox_save_proj_to_fd( f, 0, g_sv[ slot ] );
	if( rv == 0 )
	{
	    *size = sfs_get_data_size( f );
	    if( *size )
	    {
		out = malloc( *size );
		smem_copy( out, sfs_get_data( f ), *size );
	    }
	}
	smem_free( sfs_get_data( f ) );
	sfs_close( f );
    }
    return out;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jbyteArray JNICALL Java_nightradio_sunvoxlib_SunVoxLib_save_1to_1memory( JNIEnv* je, jclass jc, jint slot )
{
    size_t size;
    void* out = sv_save_to_memory( slot, &size );
    if( !out ) return NULL;
    if( size == 0 )
    {
	free( out );
	return NULL;
    }
    jbyteArray rv = je->NewByteArray( size );
    je->SetByteArrayRegion( rv, 0, size, (const jbyte*)out );
    free( out );
    return rv;
}
#endif

SUNVOX_EXPORT int sv_play( int slot )
{
    if( check_slot( slot ) ) return -1;
#ifdef DEFERRED_SOUND_STREAM_INIT
    sundog_sound_init_deferred( g_sound );
#endif
    sunvox_play( 0, false, -1, g_sv[ slot ] );
    return 0;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_play( JNIEnv* je, jclass jc, jint slot )
{
    return sv_play( slot );
}
#endif

SUNVOX_EXPORT int sv_play_from_beginning( int slot )
{
    if( check_slot( slot ) ) return -1;
#ifdef DEFERRED_SOUND_STREAM_INIT
    sundog_sound_init_deferred( g_sound );
#endif
    sunvox_play( 0, true, -1, g_sv[ slot ] );
    return 0;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_play_1from_1beginning( JNIEnv* je, jclass jc, jint slot )
{
    return sv_play_from_beginning( slot );
}
#endif

SUNVOX_EXPORT int sv_stop( int slot )
{
    if( check_slot( slot ) ) return -1;
    sunvox_stop( g_sv[ slot ] );
    return 0;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_stop( JNIEnv* je, jclass jc, jint slot )
{
    return sv_stop( slot );
}
#endif

SUNVOX_EXPORT int sv_pause( int slot )
{
    if( check_slot( slot ) ) return -1;
    SUNVOX_SOUND_STREAM_CONTROL( g_sv[ slot ], SUNVOX_STREAM_STOP );
    return 0;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_pause( JNIEnv* je, jclass jc, jint slot )
{
    return sv_pause( slot );
}
#endif

SUNVOX_EXPORT int sv_resume( int slot )
{
    if( check_slot( slot ) ) return -1;
    SUNVOX_SOUND_STREAM_CONTROL( g_sv[ slot ], SUNVOX_STREAM_PLAY );
    return 0;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_resume( JNIEnv* je, jclass jc, jint slot )
{
    return sv_resume( slot );
}
#endif

SUNVOX_EXPORT int sv_sync_resume( int slot )
{
    if( check_slot( slot ) ) return -1;
    SUNVOX_SOUND_STREAM_CONTROL( g_sv[ slot ], SUNVOX_STREAM_SYNC_PLAY );
    return 0;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_sync_1resume( JNIEnv* je, jclass jc, jint slot )
{
    return sv_sync_resume( slot );
}
#endif

SUNVOX_EXPORT int sv_set_autostop( int slot, int autostop )
{
    if( check_slot( slot ) ) return -1;
    g_sv[ slot ]->stop_at_the_end_of_proj = autostop;
    return 0;
}
SUNVOX_EXPORT int sv_get_autostop( int slot )
{
    if( check_slot( slot ) ) return 0;
    return g_sv[ slot ]->stop_at_the_end_of_proj;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_set_1autostop( JNIEnv* je, jclass jc, jint slot, jint autostop )
{
    return sv_set_autostop( slot, autostop );
}
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_get_1autostop( JNIEnv* je, jclass jc, jint slot )
{
    return sv_get_autostop( slot );
}
#endif

SUNVOX_EXPORT int sv_end_of_song( int slot )
{
    if( check_slot( slot ) ) return 0;
    return (int)( g_sv[ slot ]->playing == 0 );
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_end_1of_1song( JNIEnv* je, jclass jc, jint slot )
{
    return sv_end_of_song( slot );
}
#endif

SUNVOX_EXPORT int sv_rewind( int slot, int line_num )
{
    if( check_slot( slot ) ) return -1;
    sunvox_rewind( line_num, -1, g_sv[ slot ] );
    return 0;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_rewind( JNIEnv* je, jclass jc, jint slot, jint line_num )
{
    return sv_rewind( slot, line_num );
}
#endif

SUNVOX_EXPORT int sv_volume( int slot, int vol )
{
    if( check_slot( slot ) ) return -1;
    int prev_vol = g_sv[ slot ]->net->global_volume;
    if( vol >= 0 ) g_sv[ slot ]->net->global_volume = vol;
    return prev_vol;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_volume( JNIEnv* je, jclass jc, jint slot, jint vol )
{
    return sv_volume( slot, vol );
}
#endif

SUNVOX_EXPORT int sv_set_event_t( int slot, int set, int t )
{
    if( check_slot( slot ) ) return -1;
    g_sv_evt_t_set[ slot ] = set;
    g_sv_evt_t[ slot ] = t;
    return 0;
}
SUNVOX_EXPORT int sv_send_event( int slot, int track_num, int note, int vel, int module, int ctl, int ctl_val )
{
    if( check_slot( slot ) ) return -1;
#ifdef DEFERRED_SOUND_STREAM_INIT
    sundog_sound_init_deferred( g_sound );
#endif
    stime_ticks_t t;
    if( g_sv_evt_t_set[ slot ] )
	t = g_sv_evt_t[ slot ];
    else
	t = stime_ticks();
    sunvox_engine* s = g_sv[ slot ];
    return svh_send_event( s, t, track_num, note, vel, module, ctl, ctl_val );
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_set_1event_1t( JNIEnv* je, jclass jc, jint slot, jint set, jint t )
{
    return sv_set_event_t( slot, set, t );
}
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_send_1event( JNIEnv* je, jclass jc, jint slot, jint track_num, jint note, jint vel, jint module, jint ctl, jint ctl_val )
{
    return sv_send_event( slot, track_num, note, vel, module, ctl, ctl_val );
}
#endif

SUNVOX_EXPORT int sv_get_current_line( int slot )
{
    if( check_slot( slot ) ) return 0;
    return sunvox_frames_get_value( SUNVOX_VF_CHAN_LINENUM, stime_ticks(), g_sv[ slot ] ) / 32;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_get_1current_1line( JNIEnv* je, jclass jc, jint slot )
{
    return sv_get_current_line( slot );
}
#endif

SUNVOX_EXPORT int sv_get_current_line2( int slot )
{
    if( check_slot( slot ) ) return 0;
    return sunvox_frames_get_value( SUNVOX_VF_CHAN_LINENUM, stime_ticks(), g_sv[ slot ] );
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_get_1current_1line2( JNIEnv* je, jclass jc, jint slot )
{
    return sv_get_current_line2( slot );
}
#endif

SUNVOX_EXPORT int sv_get_current_signal_level( int slot, int channel )
{
    if( check_slot( slot ) ) return 0;
    switch( channel )
    {
	case 0:
	    return sunvox_frames_get_value( SUNVOX_VF_CHAN_VOL0, stime_ticks(), g_sv[ slot ] );
	    break;
	case 1:
	    return sunvox_frames_get_value( SUNVOX_VF_CHAN_VOL1, stime_ticks(), g_sv[ slot ] );
	    break;
    }
    return 0;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_get_1current_1signal_1level( JNIEnv* je, jclass jc, jint slot, jint channel )
{
    return sv_get_current_signal_level( slot, channel );
}
#endif

SUNVOX_EXPORT const char* sv_get_song_name( int slot )
{
    if( check_slot( slot ) ) return NULL;
    return g_sv[ slot ]->proj_name;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jstring JNICALL Java_nightradio_sunvoxlib_SunVoxLib_get_1song_1name( JNIEnv* je, jclass jc, jint slot )
{
    return je->NewStringUTF( sv_get_song_name( slot ) );
}
#endif

SUNVOX_EXPORT int sv_set_song_name( int slot, const char* name )
{
    if( check_slot( slot ) ) return -1;
    sunvox_rename( g_sv[ slot ], name );
    return 0;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_set_1song_1name( JNIEnv* je, jclass jc, jint slot, jstring name )
{
    jint rv = -1;
    const char* c_name = NULL;
    if( name ) je->GetStringUTFChars( name, 0 );
    rv = sv_set_song_name( slot, c_name );
    if( name ) je->ReleaseStringUTFChars( name, c_name );
    return rv;
}
#endif

SUNVOX_EXPORT int sv_get_base_version( int slot )
{
    if( check_slot( slot ) ) return 0;
    return g_sv[ slot ]->base_version;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_get_1base_1version( JNIEnv* je, jclass jc, jint slot )
{
    return sv_get_base_version( slot );
}
#endif

SUNVOX_EXPORT int sv_get_song_bpm( int slot )
{
    if( check_slot( slot ) ) return 0;
    return g_sv[ slot ]->bpm;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_get_1song_1bpm( JNIEnv* je, jclass jc, jint slot )
{
    return sv_get_song_bpm( slot );
}
#endif

SUNVOX_EXPORT int sv_get_song_tpl( int slot )
{
    if( check_slot( slot ) ) return 0;
    return g_sv[ slot ]->speed;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_get_1song_1tpl( JNIEnv* je, jclass jc, jint slot )
{
    return sv_get_song_tpl( slot );
}
#endif

SUNVOX_EXPORT uint sv_get_song_length_frames( int slot )
{
    if( check_slot( slot ) ) return 0;
    return sunvox_get_proj_frames( 0, 0, g_sv[ slot ] );
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_get_1song_1length_1frames( JNIEnv* je, jclass jc, jint slot )
{
    return sv_get_song_length_frames( slot );
}
#endif

SUNVOX_EXPORT uint sv_get_song_length_lines( int slot )
{
    if( check_slot( slot ) ) return 0;
    return sunvox_get_proj_lines( g_sv[ slot ] );
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_get_1song_1length_1lines( JNIEnv* je, jclass jc, jint slot )
{
    return sv_get_song_length_lines( slot );
}
#endif

SUNVOX_EXPORT int sv_get_time_map( int slot, int start_line, int len, uint32_t* dest, int flags )
{
    int rv = -1;
    if( check_slot( slot ) ) return -1;
    if( len <= 0 ) return -1;
    if( !dest ) return -1;
    int map_type = flags & SV_TIME_MAP_TYPE_MASK;
    sunvox_time_map_item* map = SMEM_ALLOC2( sunvox_time_map_item, len );
    if( map )
    {
	uint32_t* frame_map = NULL;
	if( map_type == SV_TIME_MAP_FRAMECNT ) frame_map = dest;
        sunvox_get_time_map( map, frame_map, start_line, len, g_sv[ slot ] );
	if( map_type == SV_TIME_MAP_SPEED ) for( int i = 0; i < len; i++ ) dest[ i ] = map[ i ].bpm | ( map[ i ].tpl << 16 );
        rv = 0;
	smem_free( map );
    }
    return rv;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_get_1time_1map( JNIEnv* je, jclass jc, jint slot, jint start_line, jint len, jintArray dest, jint flags )
{
    int rv;
    jint* c_dest = je->GetIntArrayElements( dest, NULL );
    rv = sv_get_time_map( slot, start_line, len, (uint32_t*)c_dest, flags );
    je->ReleaseIntArrayElements( dest, c_dest, 0 );
    return rv;
}
#endif

SUNVOX_EXPORT int sv_new_module( int slot, const char* type, const char* name, int x, int y, int z )
{
    if( check_slot( slot ) ) return -1;
    if( !is_sv_locked( slot, __FUNCTION__ ) ) return -1;
    PS_RETTYPE (*mod_hnd)( PSYNTH_MODULE_HANDLER_PARAMETERS ) = get_module_handler_by_name( type, g_sv[ slot ] );
    if( mod_hnd == psynth_empty ) return -1;
    if( !name ) name = type;
    int rv = psynth_add_module(
	-1,
	mod_hnd,
	name,
	0,
	x, y, z,
	g_sv[ slot ]->bpm,
	g_sv[ slot ]->speed,
	g_sv[ slot ]->net );
    if( rv > 0 )
    {
        psynth_do_command( rv, PS_CMD_SETUP_FINISHED, g_sv[ slot ]->net );
    }
    return rv;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_new_1module( JNIEnv* je, jclass jc, jint slot, jstring type, jstring name, jint x, jint y, jint z )
{
    jint rv;
    const char* c_type = NULL;
    const char* c_name = NULL;
    if( type ) c_type = je->GetStringUTFChars( type, 0 );
    if( name ) c_name = je->GetStringUTFChars( name, 0 );
    rv = sv_new_module( slot, c_type, c_name, x, y, z );
    if( name ) je->ReleaseStringUTFChars( name, c_name );
    if( type ) je->ReleaseStringUTFChars( type, c_type );
    return rv;
}
#endif

SUNVOX_EXPORT int sv_remove_module( int slot, int mod_num )
{
    if( check_slot( slot ) ) return -1;
    if( !is_sv_locked( slot, __FUNCTION__ ) ) return -1;
    psynth_remove_module( mod_num, g_sv[ slot ]->net );
    return 0;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_remove_1module( JNIEnv* je, jclass jc, jint slot, jint mod_num )
{
    return sv_remove_module( slot, mod_num );
}
#endif

SUNVOX_EXPORT int sv_connect_module( int slot, int source, int destination )
{
    if( check_slot( slot ) ) return -1;
    if( !is_sv_locked( slot, __FUNCTION__ ) ) return -1;
    psynth_make_link( destination, source, g_sv[ slot ]->net );
    return 0;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_connect_1module( JNIEnv* je, jclass jc, jint slot, jint source, jint destination )
{
    return sv_connect_module( slot, source, destination );
}
#endif

SUNVOX_EXPORT int sv_disconnect_module( int slot, int source, int destination )
{
    if( check_slot( slot ) ) return -1;
    if( !is_sv_locked( slot, __FUNCTION__ ) ) return -1;
    psynth_remove_link( destination, source, g_sv[ slot ]->net );
    return 0;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_disconnect_1module( JNIEnv* je, jclass jc, jint slot, jint source, jint destination )
{
    return sv_connect_module( slot, source, destination );
}
#endif

SUNVOX_EXPORT const char* sv_get_module_type( int slot, int mod_num )
{
    if( check_slot( slot ) ) return NULL;
    const char* rv = "";
    psynth_module* m = psynth_get_module( mod_num, g_sv[ slot ]->net );
    if( m )
    {
	psynth_event mod_evt = {};
        mod_evt.command = PS_CMD_GET_NAME;
        rv = (const char*)m->handler( mod_num, &mod_evt, g_sv[ slot ]->net );
        if( !rv ) rv = "";
        if( mod_num == 0 ) rv = "Output";
    }
    return rv;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jstring JNICALL Java_nightradio_sunvoxlib_SunVoxLib_get_1module_1type( JNIEnv* je, jclass jc, jint slot, jint mod_num )
{
    return je->NewStringUTF( sv_get_module_type( slot, mod_num ) );
}
#endif

static int sv_load_module( int slot, const char* file_name, sfs_file f, int x, int y, int z )
{
    if( check_slot( slot ) ) return -1;
    int rv = -1;
    if( f )
    {
	rv = sunvox_load_module_from_fd( -1, x, y, z, f, 0, g_sv[ slot ] );
    }
    else
    {
	if( sfs_get_file_size( file_name ) == 0 ) return -1;
	rv = sunvox_load_module( -1, x, y, z, file_name, 0, g_sv[ slot ] );
    }
    if( rv < 0 )
    {
	rv = psynth_add_module(
            -1,
            get_module_handler_by_name( "Sampler", g_sv[ slot ] ),
            "Sampler",
            0, x, y, z,
            g_sv[ slot ]->bpm,
            g_sv[ slot ]->speed,
            g_sv[ slot ]->net );
        if( rv > 0 )
        {
            psynth_do_command( rv, PS_CMD_SETUP_FINISHED, g_sv[ slot ]->net );
            sfs_rewind( f );
            sampler_load( file_name, f, rv, g_sv[ slot ]->net, -1, 0 );
        }
    }
    return rv;
}
SUNVOX_EXPORT int sv_load_module( int slot, const char* file_name, int x, int y, int z )
{
    return sv_load_module( slot, file_name, 0, x, y, z );
}
SUNVOX_EXPORT int sv_load_module_from_memory( int slot, void* data, uint data_size, int x, int y, int z )
{
    if( check_slot( slot ) ) return -1;
    int rv = -1;
    sfs_file f = sfs_open_in_memory( data, data_size );
    if( f )
    {
	rv = sv_load_module( slot, 0, f, x, y, z );
	sfs_close( f );
    }
    return rv;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_load_1module( JNIEnv* je, jclass jc, jint slot, jstring file_name, jint x, jint y, jint z )
{
    jint rv;
    const char* c_file = NULL;
    if( file_name ) c_file = je->GetStringUTFChars( file_name, 0 );
    rv = sv_load_module( slot, c_file, x, y, z );
    if( file_name ) je->ReleaseStringUTFChars( file_name, c_file );
    return rv;
}
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_load_1module_1from_1memory( JNIEnv* je, jclass jc, jint slot, jbyteArray data, jint x, jint y, jint z )
{
    jint rv = 0;
    uint data_size = (uint)je->GetArrayLength( data );
    jbyte* c_data = je->GetByteArrayElements( data, NULL );
    rv = sv_load_module_from_memory( slot, c_data, data_size, x, y, z );
    je->ReleaseByteArrayElements( data, c_data, 0 );
    return rv;
}
#endif

const char* g_mod_load_types[] = { "Sampler", "MetaModule", "Vorbis player" };
static int sv_mod_load_check( int slot, int modtype, int mod_num )
{
    if( (unsigned)modtype > (unsigned)2 ) return -1;
    const char* modtype_str1 = sv_get_module_type( slot, mod_num );
    const char* modtype_str2 = g_mod_load_types[ modtype ];
    if( strcmp( modtype_str1, modtype_str2 ) )
    {
	slog( "Can't load data into the %s module. Expected type - %s\n", modtype_str1, modtype_str2 );
	return -1;
    }
    return 0;
}
static int sv_mod_load( int slot, int modtype, int mod_num, const char* file_name, int sample_slot )
{
    if( check_slot( slot ) ) return -1;
    if( sv_mod_load_check( slot, modtype, mod_num ) ) return -1;
    int rv = -1;
    switch( modtype )
    {
	case 0: rv = sampler_load( file_name, 0, mod_num, g_sv[ slot ]->net, sample_slot, 0 ); break;
	case 1: rv = metamodule_load( file_name, 0, mod_num, g_sv[ slot ]->net ); break;
	case 2: rv = vplayer_load_file( mod_num, file_name, 0, g_sv[ slot ]->net ); break;
    }
    return rv;
}
static int sv_mod_load_from_memory( int slot, int modtype, int mod_num, void* data, uint data_size, int sample_slot )
{
    if( check_slot( slot ) ) return -1;
    if( sv_mod_load_check( slot, modtype, mod_num ) ) return -1;
    int rv = -1;
    sfs_file f = sfs_open_in_memory( data, data_size );
    if( f )
    {
	switch( modtype )
	{
	    case 0: rv = sampler_load( NULL, f, mod_num, g_sv[ slot ]->net, sample_slot, 0 ); break;
	    case 1: rv = metamodule_load( NULL, f, mod_num, g_sv[ slot ]->net ); break;
	    case 2: rv = vplayer_load_file( mod_num, NULL, f, g_sv[ slot ]->net ); break;
	}
	sfs_close( f );
    }
    return rv;
}
SUNVOX_EXPORT int sv_sampler_load( int slot, int mod_num, const char* file_name, int sample_slot )
{
    return sv_mod_load( slot, 0, mod_num, file_name, sample_slot );
}
SUNVOX_EXPORT int sv_sampler_load_from_memory( int slot, int mod_num, void* data, uint data_size, int sample_slot )
{
    return sv_mod_load_from_memory( slot, 0, mod_num, data, data_size, sample_slot );
}
SUNVOX_EXPORT int sv_metamodule_load( int slot, int mod_num, const char* file_name )
{
    return sv_mod_load( slot, 1, mod_num, file_name, 0 );
}
SUNVOX_EXPORT int sv_metamodule_load_from_memory( int slot, int mod_num, void* data, uint data_size )
{
    return sv_mod_load_from_memory( slot, 1, mod_num, data, data_size, 0 );
}
SUNVOX_EXPORT int sv_vplayer_load( int slot, int mod_num, const char* file_name )
{
    return sv_mod_load( slot, 2, mod_num, file_name, 0 );
}
SUNVOX_EXPORT int sv_vplayer_load_from_memory( int slot, int mod_num, void* data, uint data_size )
{
    return sv_mod_load_from_memory( slot, 2, mod_num, data, data_size, 0 );
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_sampler_1load( JNIEnv* je, jclass jc, jint slot, jint mod_num, jstring file_name, jint sample_slot )
{
    jint rv;
    const char* c_file = NULL;
    if( file_name ) c_file = je->GetStringUTFChars( file_name, 0 );
    rv = sv_sampler_load( slot, mod_num, c_file, sample_slot );
    if( file_name ) je->ReleaseStringUTFChars( file_name, c_file );
    return rv;
}
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_sampler_1load_1from_1memory( JNIEnv* je, jclass jc, jint slot, jint mod_num, jbyteArray data, jint sample_slot )
{
    jint rv = 0;
    uint data_size = (uint)je->GetArrayLength( data );
    jbyte* c_data = je->GetByteArrayElements( data, NULL );
    rv = sv_sampler_load_from_memory( slot, mod_num, c_data, data_size, sample_slot );
    je->ReleaseByteArrayElements( data, c_data, 0 );
    return rv;
}
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_metamodule_1load( JNIEnv* je, jclass jc, jint slot, jint mod_num, jstring file_name )
{
    jint rv;
    const char* c_file = NULL;
    if( file_name ) c_file = je->GetStringUTFChars( file_name, 0 );
    rv = sv_metamodule_load( slot, mod_num, c_file );
    if( file_name ) je->ReleaseStringUTFChars( file_name, c_file );
    return rv;
}
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_metamodule_1load_1from_1memory( JNIEnv* je, jclass jc, jint slot, jint mod_num, jbyteArray data )
{
    jint rv = 0;
    uint data_size = (uint)je->GetArrayLength( data );
    jbyte* c_data = je->GetByteArrayElements( data, NULL );
    rv = sv_metamodule_load_from_memory( slot, mod_num, c_data, data_size );
    je->ReleaseByteArrayElements( data, c_data, 0 );
    return rv;
}
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_vplayer_1load( JNIEnv* je, jclass jc, jint slot, jint mod_num, jstring file_name )
{
    jint rv;
    const char* c_file = NULL;
    if( file_name ) c_file = je->GetStringUTFChars( file_name, 0 );
    rv = sv_vplayer_load( slot, mod_num, c_file );
    if( file_name ) je->ReleaseStringUTFChars( file_name, c_file );
    return rv;
}
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_vplayer_1load_1from_1memory( JNIEnv* je, jclass jc, jint slot, jint mod_num, jbyteArray data )
{
    jint rv = 0;
    uint data_size = (uint)je->GetArrayLength( data );
    jbyte* c_data = je->GetByteArrayElements( data, NULL );
    rv = sv_vplayer_load_from_memory( slot, mod_num, c_data, data_size );
    je->ReleaseByteArrayElements( data, c_data, 0 );
    return rv;
}
#endif

SUNVOX_EXPORT int sv_sampler_par( int slot, int mod_num, int sample_slot, int par, int par_val, int set )
{
    if( check_slot( slot ) ) return 0;
    const char* modtype_str = sv_get_module_type( slot, mod_num );
    if( strcmp( modtype_str, "Sampler" ) )
    {
	slog( "sv_sampler_par(): module %d is not a Sampler!\n", mod_num );
	return 0;
    }
    return sampler_par( g_sv[ slot ]->net, mod_num, sample_slot, par, par_val, set );
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_sampler_1par( JNIEnv* je, jclass jc, jint slot, jint mod_num, jint sample_slot, jint par, jint par_val, jint set )
{
    return sv_sampler_par( slot, mod_num, sample_slot, par, par_val, set );
}
#endif

SUNVOX_EXPORT uint sv_get_number_of_modules( int slot )
{
    if( check_slot( slot ) ) return 0;
    return g_sv[ slot ]->net->mods_num;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_get_1number_1of_1modules( JNIEnv* je, jclass jc, jint slot )
{
    return sv_get_number_of_modules( slot );
}
#endif

SUNVOX_EXPORT int sv_find_module( int slot, const char* name )
{
    if( check_slot( slot ) ) return -1;
    if( !name ) return -1;
    return psynth_get_module_by_name( name, g_sv[ slot ]->net );
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_find_1module( JNIEnv* je, jclass jc, jint slot, jstring name )
{
    jint rv = -1;
    const char* c_name = NULL;
    if( name ) c_name = je->GetStringUTFChars( name, 0 );
    rv = sv_find_module( slot, c_name );
    if( name ) je->ReleaseStringUTFChars( name, c_name );
    return rv;
}
#endif

SUNVOX_EXPORT uint32_t sv_get_module_flags( int slot, int mod_num )
{
    if( check_slot( slot ) ) return 0;
    uint32_t rv = 0;
    psynth_module* m = psynth_get_module( mod_num, g_sv[ slot ]->net );
    if( m )
    {
	rv |= SV_MODULE_FLAG_EXISTS;
	if( m->flags & PSYNTH_FLAG_GENERATOR ) rv |= SV_MODULE_FLAG_GENERATOR;
	if( m->flags & PSYNTH_FLAG_EFFECT ) rv |= SV_MODULE_FLAG_EFFECT;
	if( m->flags & PSYNTH_FLAG_MUTE ) rv |= SV_MODULE_FLAG_MUTE;
	if( m->flags & PSYNTH_FLAG_SOLO ) rv |= SV_MODULE_FLAG_SOLO;
	if( m->flags & PSYNTH_FLAG_BYPASS ) rv |= SV_MODULE_FLAG_BYPASS;
	rv |= m->input_links_num << SV_MODULE_INPUTS_OFF;
	rv |= m->output_links_num << SV_MODULE_OUTPUTS_OFF;
    }
    return rv;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_get_1module_1flags( JNIEnv* je, jclass jc, jint slot, jint mod_num )
{
    return sv_get_module_flags( slot, mod_num );
}
#endif

SUNVOX_EXPORT int* sv_get_module_inputs( int slot, int mod_num )
{
    if( check_slot( slot ) ) return NULL;
    int* rv = NULL;
    psynth_module* m = psynth_get_module( mod_num, g_sv[ slot ]->net );
    if( m )
    {
	rv = m->input_links;
    }
    return rv;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jintArray JNICALL Java_nightradio_sunvoxlib_SunVoxLib_get_1module_1inputs( JNIEnv* je, jclass jc, jint slot, jint mod_num )
{
    if( !g_sv[ slot ] ) return NULL;
    psynth_module* m = psynth_get_module( mod_num, g_sv[ slot ]->net );
    if( m )
    {
	if( m->input_links_num && m->input_links )
	{
	    jintArray array = je->NewIntArray( m->input_links_num );
	    je->SetIntArrayRegion( array, 0, m->input_links_num, m->input_links );
	    return array;
	}
    }
    return NULL;
}
#endif

SUNVOX_EXPORT int* sv_get_module_outputs( int slot, int mod_num )
{
    if( check_slot( slot ) ) return NULL;
    int* rv = NULL;
    psynth_module* m = psynth_get_module( mod_num, g_sv[ slot ]->net );
    if( m )
    {
	rv = m->output_links;
    }
    return rv;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jintArray JNICALL Java_nightradio_sunvoxlib_SunVoxLib_get_1module_1outputs( JNIEnv* je, jclass jc, jint slot, jint mod_num )
{
    if( !g_sv[ slot ] ) return NULL;
    psynth_module* m = psynth_get_module( mod_num, g_sv[ slot ]->net );
    if( m )
    {
        if( m->output_links_num && m->output_links )
        {
    	    jintArray array = je->NewIntArray( m->output_links_num );
	    je->SetIntArrayRegion( array, 0, m->output_links_num, m->output_links );
	    return array;
	}
    }
    return NULL;
}
#endif

SUNVOX_EXPORT const char* sv_get_module_name( int slot, int mod_num )
{
    if( check_slot( slot ) ) return NULL;
    const char* rv = "";
    psynth_module* m = psynth_get_module( mod_num, g_sv[ slot ]->net );
    if( m )
    {
        rv = (const char*)m->name;
    }
    return rv;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jstring JNICALL Java_nightradio_sunvoxlib_SunVoxLib_get_1module_1name( JNIEnv* je, jclass jc, jint slot, jint mod_num )
{
    return je->NewStringUTF( sv_get_module_name( slot, mod_num ) );
}
#endif

SUNVOX_EXPORT int sv_set_module_name( int slot, int mod_num, const char* name )
{
    if( check_slot( slot ) ) return -1;
    psynth_rename( mod_num, name, g_sv[ slot ]->net );
    return 0;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_set_1module_1name( JNIEnv* je, jclass jc, jint slot, jint mod_num, jstring name )
{
    jint rv = -1;
    const char* c_name = NULL;
    if( name ) c_name = je->GetStringUTFChars( name, 0 );
    rv = sv_set_module_name( slot, mod_num, c_name );
    if( name ) je->ReleaseStringUTFChars( name, c_name );
    return rv;
}
#endif

SUNVOX_EXPORT uint32_t sv_get_module_xy( int slot, int mod_num )
{
    if( check_slot( slot ) ) return 0;
    int rv = 0;
    psynth_module* m = psynth_get_module( mod_num, g_sv[ slot ]->net );
    if( m )
    {
        uint32_t x = m->x;
        uint32_t y = m->y;
        rv = ( x & 0xFFFF ) | ( ( y & 0xFFFF ) << 16 );
    }
    return rv;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_get_1module_1xy( JNIEnv* je, jclass jc, jint slot, jint mod_num )
{
    return sv_get_module_xy( slot, mod_num );
}
#endif

SUNVOX_EXPORT int sv_set_module_xy( int slot, int mod_num, int x, int y )
{
    if( check_slot( slot ) ) return -1;
    psynth_module* m = psynth_get_module( mod_num, g_sv[ slot ]->net );
    if( m )
    {
        m->x = x;
        m->y = y;
        return 0;
    }
    return -1;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_set_1module_1xy( JNIEnv* je, jclass jc, jint slot, jint mod_num, jint x, jint y )
{
    return sv_set_module_xy( slot, mod_num, x, y );
}
#endif

SUNVOX_EXPORT int sv_get_module_color( int slot, int mod_num )
{
    if( check_slot( slot ) ) return 0;
    int rv = 0;
    psynth_module* m = psynth_get_module( mod_num, g_sv[ slot ]->net );
    if( m )
    {
	rv = (int)m->color[ 0 ] | ( (int)m->color[ 1 ] << 8 ) | ( (int)m->color[ 2 ] << 16 );
    }
    return rv;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_get_1module_1color( JNIEnv* je, jclass jc, jint slot, jint mod_num )
{
    return sv_get_module_color( slot, mod_num );
}
#endif

SUNVOX_EXPORT int sv_set_module_color( int slot, int mod_num, int color )
{
    if( check_slot( slot ) ) return -1;
    psynth_module* m = psynth_get_module( mod_num, g_sv[ slot ]->net );
    if( m )
    {
	m->color[ 0 ] = color & 255;
	m->color[ 1 ] = ( color >> 8 ) & 255;
	m->color[ 2 ] = ( color >> 16 ) & 255;
	return 0;
    }
    return -1;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_set_1module_1color( JNIEnv* je, jclass jc, jint slot, jint mod_num, jint color )
{
    return sv_set_module_color( slot, mod_num, color );
}
#endif

SUNVOX_EXPORT uint32_t sv_get_module_finetune( int slot, int mod_num )
{
    if( check_slot( slot ) ) return 0;
    uint32_t rv = 0;
    psynth_module* m = psynth_get_module( mod_num, g_sv[ slot ]->net );
    if( m )
    {
	uint32_t x = m->finetune;
	uint32_t y = m->relative_note;
	rv = ( x & 0xFFFF ) | ( ( y & 0xFFFF ) << 16 );
    }
    return rv;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_get_1module_1finetune( JNIEnv* je, jclass jc, jint slot, jint mod_num )
{
    return sv_get_module_finetune( slot, mod_num );
}
#endif

SUNVOX_EXPORT uint32_t sv_set_module_finetune( int slot, int mod_num, int finetune )
{
    if( check_slot( slot ) ) return -1;
    psynth_module* m = psynth_get_module( mod_num, g_sv[ slot ]->net );
    if( m )
    {
	m->finetune = finetune;
	return 0;
    }
    return -1;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_set_1module_1finetune( JNIEnv* je, jclass jc, jint slot, jint mod_num, jint finetune )
{
    return sv_set_module_finetune( slot, mod_num, finetune );
}
#endif

SUNVOX_EXPORT uint32_t sv_set_module_relnote( int slot, int mod_num, int relative_note )
{
    if( check_slot( slot ) ) return -1;
    psynth_module* m = psynth_get_module( mod_num, g_sv[ slot ]->net );
    if( m )
    {
	m->relative_note = relative_note;
	return 0;
    }
    return -1;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_set_1module_1relnote( JNIEnv* je, jclass jc, jint slot, jint mod_num, jint relative_note )
{
    return sv_set_module_relnote( slot, mod_num, relative_note );
}
#endif

SUNVOX_EXPORT uint sv_get_module_scope2( int slot, int mod_num, int channel, int16_t* buf, uint samples_to_read )
{
    if( check_slot( slot ) ) return 0;
    uint rv = 0;
    if( (unsigned)mod_num < g_sv[ slot ]->net->mods_num )
    {
	psynth_module* m = &g_sv[ slot ]->net->mods[ mod_num ];
	if( m->flags & PSYNTH_FLAG_EXISTS )
	{
	    int size = 0;
	    int offset = 0;
	    stime_ticks_t t = stime_ticks();
	    void* scope = psynth_get_scope_buffer( channel, &offset, &size, mod_num, t, g_sv[ slot ]->net );
	    if( scope == 0 || size == 0 ) return 0;
	    size--; //make mask
	    rv = samples_to_read;
#ifdef PSYNTH_SCOPE_MODE_SLOW_HQ
	    offset = ( offset - samples_to_read ) & size;
#else
            //Buffer is not cyclic. We cet get only last g_sound->out_frames:
            if( g_sound->out_frames < samples_to_read )
        	rv = g_sound->out_frames;
#endif
	    for( uint i = 0; i < rv; i++ )
	    {
		PS_STYPE v = ((PS_STYPE*)scope)[ ( offset + i ) & size ];
		PS_STYPE_TO_INT16( buf[ i ], v );
	    }
	}
    }
    return rv;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_get_1module_1scope( JNIEnv* je, jclass jc, jint slot, jint mod_num, jint channel, jshortArray dest_buf, jint samples_to_read )
{
    size_t len = je->GetArrayLength( dest_buf );
    if( len == 0 ) return 0;
    if( len < samples_to_read ) samples_to_read = len;
    int16_t* c_buf = je->GetShortArrayElements( dest_buf, NULL );
    jint received = sv_get_module_scope2( slot, mod_num, channel, c_buf, samples_to_read );
    je->ReleaseShortArrayElements( dest_buf, c_buf, 0 );
    return received;
}
#endif

SUNVOX_EXPORT int sv_module_curve( int slot, int mod_num, int curve_num, float* data, int len, int w )
{
    if( check_slot( slot ) ) return 0;
    return psynth_curve( mod_num, curve_num, data, len, w, g_sv[ slot ]->net );
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_module_1curve( JNIEnv* je, jclass jc, jint slot, jint mod_num, jint curve_num, jfloatArray data, jint len, jint w )
{
    int rv;
    jfloat* c_data = je->GetFloatArrayElements( data, NULL );
    rv = sv_module_curve( slot, mod_num, curve_num, c_data, len, w );
    je->ReleaseFloatArrayElements( data, c_data, 0 );
    return rv;
}
#endif

SUNVOX_EXPORT int sv_get_number_of_module_ctls( int slot, int mod_num )
{
    if( check_slot( slot ) ) return 0;
    sunvox_engine* s = g_sv[ slot ];
    return svh_get_number_of_module_ctls( s, mod_num );
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_get_1number_1of_1module_1ctls( JNIEnv* je, jclass jc, jint slot, jint mod_num )
{
    return sv_get_number_of_module_ctls( slot, mod_num );
}
#endif

SUNVOX_EXPORT const char* sv_get_module_ctl_name( int slot, int mod_num, int ctl_num )
{
    if( check_slot( slot ) ) return NULL;
    sunvox_engine* s = g_sv[ slot ];
    return svh_get_module_ctl_name( s, mod_num, ctl_num );
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jstring JNICALL Java_nightradio_sunvoxlib_SunVoxLib_get_1module_1ctl_1name( JNIEnv* je, jclass jc, jint slot, jint mod_num, jint ctl_num )
{
    return je->NewStringUTF( sv_get_module_ctl_name( slot, mod_num, ctl_num ) );
}
#endif

SUNVOX_EXPORT int sv_get_module_ctl_value( int slot, int mod_num, int ctl_num, int scaled )
{
    if( check_slot( slot ) ) return 0;
    sunvox_engine* s = g_sv[ slot ];
    return svh_get_module_ctl_value( s, mod_num, ctl_num, scaled );
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_get_1module_1ctl_1value( JNIEnv* je, jclass jc, jint slot, jint mod_num, jint ctl_num, jint scaled )
{
    return sv_get_module_ctl_value( slot, mod_num, ctl_num, scaled );
}
#endif

SUNVOX_EXPORT int sv_set_module_ctl_value( int slot, int mod_num, int ctl_num, int val, int scaled )
{
    if( check_slot( slot ) ) return -1;
    stime_ticks_t t;
    if( g_sv_evt_t_set[ slot ] )
	t = g_sv_evt_t[ slot ];
    else
	t = stime_ticks();
    sunvox_engine* s = g_sv[ slot ];
    return svh_set_module_ctl_value( s, t, mod_num, ctl_num, val, scaled );
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_set_1module_1ctl_1value( JNIEnv* je, jclass jc, jint slot, jint mod_num, jint ctl_num, jint val, jint scaled )
{
    return sv_set_module_ctl_value( slot, mod_num, ctl_num, val, scaled );
}
#endif

SUNVOX_EXPORT int sv_get_module_ctl_min( int slot, int mod_num, int ctl_num, int scaled )
{
    if( check_slot( slot ) ) return 0;
    sunvox_engine* s = g_sv[ slot ];
    return svh_get_module_ctl_par( s, mod_num, ctl_num, scaled, 0 );
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_get_1module_1ctl_1min( JNIEnv* je, jclass jc, jint slot, jint mod_num, jint ctl_num, jint scaled )
{
    return sv_get_module_ctl_min( slot, mod_num, ctl_num, scaled );
}
#endif

SUNVOX_EXPORT int sv_get_module_ctl_max( int slot, int mod_num, int ctl_num, int scaled )
{
    if( check_slot( slot ) ) return 0;
    sunvox_engine* s = g_sv[ slot ];
    return svh_get_module_ctl_par( s, mod_num, ctl_num, scaled, 1 );
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_get_1module_1ctl_1max( JNIEnv* je, jclass jc, jint slot, jint mod_num, jint ctl_num, jint scaled )
{
    return sv_get_module_ctl_max( slot, mod_num, ctl_num, scaled );
}
#endif

SUNVOX_EXPORT int sv_get_module_ctl_offset( int slot, int mod_num, int ctl_num )
{
    if( check_slot( slot ) ) return 0;
    sunvox_engine* s = g_sv[ slot ];
    return svh_get_module_ctl_par( s, mod_num, ctl_num, 0, 2 );
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_get_1module_1ctl_1offset( JNIEnv* je, jclass jc, jint slot, jint mod_num, jint ctl_num )
{
    return sv_get_module_ctl_offset( slot, mod_num, ctl_num );
}
#endif

SUNVOX_EXPORT int sv_get_module_ctl_type( int slot, int mod_num, int ctl_num )
{
    if( check_slot( slot ) ) return 0;
    sunvox_engine* s = g_sv[ slot ];
    return svh_get_module_ctl_par( s, mod_num, ctl_num, 0, 3 );
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_get_1module_1ctl_1type( JNIEnv* je, jclass jc, jint slot, jint mod_num, jint ctl_num )
{
    return sv_get_module_ctl_type( slot, mod_num, ctl_num );
}
#endif

SUNVOX_EXPORT int sv_get_module_ctl_group( int slot, int mod_num, int ctl_num )
{
    if( check_slot( slot ) ) return 0;
    sunvox_engine* s = g_sv[ slot ];
    return svh_get_module_ctl_par( s, mod_num, ctl_num, 0, 4 );
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_get_1module_1ctl_1group( JNIEnv* je, jclass jc, jint slot, jint mod_num, jint ctl_num )
{
    return sv_get_module_ctl_group( slot, mod_num, ctl_num );
}
#endif

SUNVOX_EXPORT int sv_new_pattern( int slot, int clone, int x, int y, int tracks, int lines, int icon_seed, const char* name )
{
    if( check_slot( slot ) ) return -1;
    sunvox_engine* s = g_sv[ slot ];
    if( !is_sv_locked( slot, __FUNCTION__ ) ) return -1;
    int rv = -1;
    if( clone >= 0 )
	rv = sunvox_new_pattern_clone( clone, x, y, s );
    else
    {
	rv = sunvox_new_pattern( lines, tracks, x, y, icon_seed, s );
	sunvox_rename_pattern( rv, name, s );
    }
    return rv;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_new_1pattern( JNIEnv* je, jclass jc, jint slot, jint clone, jint x, jint y, jint tracks, jint lines, jint icon_seed, jstring name )
{
    jint rv = -1;
    const char* c_name = NULL;
    if( name ) c_name = je->GetStringUTFChars( name, 0 );
    rv = sv_new_pattern( slot, clone, x, y, tracks, lines, icon_seed, c_name );
    if( name ) je->ReleaseStringUTFChars( name, c_name );
    return rv;
}
#endif

SUNVOX_EXPORT int sv_remove_pattern( int slot, int pat_num )
{
    if( check_slot( slot ) ) return -1;
    sunvox_engine* s = g_sv[ slot ];
    if( !is_sv_locked( slot, __FUNCTION__ ) ) return -1;
    sunvox_remove_pattern( pat_num, s );
    return 0;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_remove_1pattern( JNIEnv* je, jclass jc, jint slot, jint pat_num )
{
    return sv_remove_pattern( slot, pat_num );
}
#endif

SUNVOX_EXPORT int sv_get_number_of_patterns( int slot )
{
    if( check_slot( slot ) ) return 0;
    return g_sv[ slot ]->pats_num;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_get_1number_1of_1patterns( JNIEnv* je, jclass jc, jint slot )
{
    return sv_get_number_of_patterns( slot );
}
#endif

SUNVOX_EXPORT int sv_find_pattern( int slot, const char* name )
{
    if( check_slot( slot ) ) return -1;
    if( !name ) return -1;
    return sunvox_get_pattern_num_by_name( name, g_sv[ slot ] );
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_find_1pattern( JNIEnv* je, jclass jc, jint slot, jstring name )
{
    jint rv = -1;
    const char* c_name = NULL;
    if( name ) c_name = je->GetStringUTFChars( name, 0 );
    rv = sv_find_pattern( slot, c_name );
    if( name ) je->ReleaseStringUTFChars( name, c_name );
    return rv;
}
#endif

SUNVOX_EXPORT int sv_get_pattern_x( int slot, int pat_num )
{
    if( check_slot( slot ) ) return 0;
    sunvox_engine* s = g_sv[ slot ];
    if( (unsigned)pat_num >= (unsigned)s->pats_num ) return 0;
    if( !s->pats[ pat_num ] ) return 0;
    return s->pats_info[ pat_num ].x;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_get_1pattern_1x( JNIEnv* je, jclass jc, jint slot, jint pat_num )
{
    return sv_get_pattern_x( slot, pat_num );
}
#endif

SUNVOX_EXPORT int sv_get_pattern_y( int slot, int pat_num )
{
    if( check_slot( slot ) ) return 0;
    sunvox_engine* s = g_sv[ slot ];
    if( (unsigned)pat_num >= (unsigned)s->pats_num ) return 0;
    if( !s->pats[ pat_num ] ) return 0;
    return s->pats_info[ pat_num ].y;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_get_1pattern_1y( JNIEnv* je, jclass jc, jint slot, jint pat_num )
{
    return sv_get_pattern_y( slot, pat_num );
}
#endif

SUNVOX_EXPORT int sv_set_pattern_xy( int slot, int pat_num, int x, int y )
{
    if( check_slot( slot ) ) return -1;
    sunvox_engine* s = g_sv[ slot ];
    if( (unsigned)pat_num >= (unsigned)s->pats_num ) return -1;
    if( !s->pats[ pat_num ] ) return -1;
    if( !is_sv_locked( slot, __FUNCTION__ ) ) return -1;
    s->pats_info[ pat_num ].x = x;
    s->pats_info[ pat_num ].y = y;
    return 0;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_set_1pattern_1xy( JNIEnv* je, jclass jc, jint slot, jint pat_num, jint x, jint y )
{
    return sv_set_pattern_xy( slot, pat_num, x, y );
}
#endif

SUNVOX_EXPORT int sv_get_pattern_tracks( int slot, int pat_num )
{
    if( check_slot( slot ) ) return 0;
    sunvox_engine* s = g_sv[ slot ];
    if( (unsigned)pat_num >= (unsigned)s->pats_num ) return 0;
    if( !s->pats[ pat_num ] ) return 0;
    return s->pats[ pat_num ]->data_xsize;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_get_1pattern_1tracks( JNIEnv* je, jclass jc, jint slot, jint pat_num )
{
    return sv_get_pattern_tracks( slot, pat_num );
}
#endif

SUNVOX_EXPORT int sv_get_pattern_lines( int slot, int pat_num )
{
    if( check_slot( slot ) ) return 0;
    sunvox_engine* s = g_sv[ slot ];
    if( (unsigned)pat_num >= (unsigned)s->pats_num ) return 0;
    if( !s->pats[ pat_num ] ) return 0;
    return s->pats[ pat_num ]->data_ysize;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_get_1pattern_1lines( JNIEnv* je, jclass jc, jint slot, jint pat_num )
{
    return sv_get_pattern_lines( slot, pat_num );
}
#endif

SUNVOX_EXPORT int sv_set_pattern_size( int slot, int pat_num, int tracks, int lines )
{
    if( check_slot( slot ) ) return -1;
    sunvox_engine* s = g_sv[ slot ];
    if( (unsigned)pat_num >= (unsigned)s->pats_num ) return -1;
    if( !s->pats[ pat_num ] ) return -1;
    if( !is_sv_locked( slot, __FUNCTION__ ) ) return -1;
    if( s->pats[ pat_num ]->data_xsize != tracks && tracks > 0 )
	sunvox_pattern_set_number_of_channels( pat_num, tracks, s );
    if( s->pats[ pat_num ]->data_ysize != lines && lines > 0 )
	sunvox_pattern_set_number_of_lines( pat_num, lines, 0, s );
    return 0;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_set_1pattern_1size( JNIEnv* je, jclass jc, jint slot, jint pat_num, jint tracks, jint lines )
{
    return sv_set_pattern_size( slot, pat_num, tracks, lines );
}
#endif

SUNVOX_EXPORT const char* sv_get_pattern_name( int slot, int pat_num )
{
    if( check_slot( slot ) ) return NULL;
    sunvox_engine* s = g_sv[ slot ];
    if( (unsigned)pat_num >= (unsigned)s->pats_num ) return NULL;
    if( !s->pats[ pat_num ] ) return NULL;
    return s->pats[ pat_num ]->name;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jstring JNICALL Java_nightradio_sunvoxlib_SunVoxLib_get_1pattern_1name( JNIEnv* je, jclass jc, jint slot, jint pat_num )
{
    return je->NewStringUTF( sv_get_pattern_name( slot, pat_num ) );
}
#endif

SUNVOX_EXPORT int sv_set_pattern_name( int slot, int pat_num, const char* name )
{
    if( check_slot( slot ) ) return -1;
    sunvox_engine* s = g_sv[ slot ];
    if( !is_sv_locked( slot, __FUNCTION__ ) ) return -1;
    sunvox_rename_pattern( pat_num, name, s );
    return 0;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_set_1pattern_1name( JNIEnv* je, jclass jc, jint slot, jint pat_num, jstring name )
{
    jint rv = -1;
    const char* c_name = NULL;
    if( name ) c_name = je->GetStringUTFChars( name, 0 );
    rv = sv_set_pattern_name( slot, pat_num, c_name );
    if( name ) je->ReleaseStringUTFChars( name, c_name );
    return rv;
}
#endif

SUNVOX_EXPORT sunvox_note* sv_get_pattern_data( int slot, int pat_num )
{
    if( check_slot( slot ) ) return NULL;
    sunvox_engine* s = g_sv[ slot ];
    if( (unsigned)pat_num >= (unsigned)s->pats_num ) return NULL;
    if( !s->pats[ pat_num ] ) return NULL;
    return s->pats[ pat_num ]->data;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jbyteArray JNICALL Java_nightradio_sunvoxlib_SunVoxLib_get_1pattern_1data( JNIEnv* je, jclass jc, jint slot, jint pat_num )
{
    if( check_slot( slot ) ) return NULL;
    sunvox_engine* s = g_sv[ slot ];
    if( !s ) return NULL;
    if( (unsigned)pat_num >= (unsigned)s->pats_num ) return NULL;
    if( !s->pats[ pat_num ] ) return NULL;
    size_t size = s->pats[ pat_num ]->data_xsize * s->pats[ pat_num ]->data_ysize * sizeof( sunvox_note );
    jbyteArray rv = je->NewByteArray( size );
    je->SetByteArrayRegion( rv, 0, size, (const jbyte*)s->pats[ pat_num ]->data );
    return rv;
}
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_set_1pattern_1data( JNIEnv* je, jclass jc, jint slot, jint pat_num, jbyteArray pat_data )
{
    if( check_slot( slot ) ) return -1;
    sunvox_engine* s = g_sv[ slot ];
    if( !s ) return -1;
    if( (unsigned)pat_num >= (unsigned)s->pats_num ) return -1;
    if( !s->pats[ pat_num ] ) return -1;
    size_t size = s->pats[ pat_num ]->data_xsize * s->pats[ pat_num ]->data_ysize * sizeof( sunvox_note );
    size_t data_size = je->GetArrayLength( pat_data );
    jbyte* c_data = je->GetByteArrayElements( pat_data, NULL );
    if( c_data )
    {
        if( data_size > size ) data_size = size;
	smem_copy( s->pats[ pat_num ]->data, c_data, data_size );
        je->ReleaseByteArrayElements( pat_data, c_data, 0 );
        return 0;
    }
    return -1;
}
#endif

SUNVOX_EXPORT int sv_set_pattern_event( int slot, int pat_num, int track, int line, int nn, int vv, int mm, int ccee, int xxyy )
{
    if( check_slot( slot ) ) return -1;
    sunvox_pattern* pat = sunvox_get_pattern( pat_num, g_sv[ slot ] );
    if( !pat ) return -2;
    if( (unsigned)track >= (unsigned)pat->channels ) return -3;
    if( (unsigned)line >= (unsigned)pat->lines ) return -4;
    sunvox_note* p = &pat->data[ line * pat->data_xsize + track ];
    if( nn >= 0 ) p->note = nn;
    if( vv >= 0 ) p->vel = vv;
    if( mm >= 0 ) p->mod = mm;
    if( ccee >= 0 ) p->ctl = ccee;
    if( xxyy >= 0 ) p->ctl_val = xxyy;
    return 0;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_set_1pattern_1event( JNIEnv* je, jclass jc, jint slot, jint pat_num, jint track, jint line, jint nn, jint vv, jint mm, jint ccee, jint xxyy )
{
    return sv_set_pattern_event( slot, pat_num, track, line, nn, vv, mm, ccee, xxyy );
}
#endif

SUNVOX_EXPORT int sv_get_pattern_event( int slot, int pat_num, int track, int line, int column )
{
    if( check_slot( slot ) ) return -1;
    sunvox_pattern* pat = sunvox_get_pattern( pat_num, g_sv[ slot ] );
    if( !pat ) return -2;
    if( (unsigned)track >= (unsigned)pat->channels ) return -3;
    if( (unsigned)line >= (unsigned)pat->lines ) return -4;
    sunvox_note* p = &pat->data[ line * pat->data_xsize + track ];
    int rv = -1;
    switch( column )
    {
	case 0: rv = p->note; break;
	case 1: rv = p->vel; break;
	case 2: rv = p->mod; break;
	case 3: rv = p->ctl; break;
	case 4: rv = p->ctl_val; break;
    }
    return rv;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_get_1pattern_1event( JNIEnv* je, jclass jc, jint slot, jint pat_num, jint track, jint line, jint column )
{
    return sv_get_pattern_event( slot, pat_num, track, line, column );
}
#endif

SUNVOX_EXPORT int sv_pattern_mute( int slot, int pat_num, int mute )
{
    if( check_slot( slot ) ) return -1;
    int prev_val = 0;
    sunvox_engine* s = g_sv[ slot ];
    if( (unsigned)pat_num >= (unsigned)s->pats_num ) return -1;
    if( !s->pats[ pat_num ] ) return -1;
    if( !is_sv_locked( slot, __FUNCTION__ ) ) return -1;
    if( s->pats_info[ pat_num ].flags & SUNVOX_PATTERN_INFO_FLAG_MUTE ) prev_val = 1;
    if( mute == 1 ) s->pats_info[ pat_num ].flags |= SUNVOX_PATTERN_INFO_FLAG_MUTE;
    if( mute == 0 ) s->pats_info[ pat_num ].flags &= ~SUNVOX_PATTERN_INFO_FLAG_MUTE;
    return prev_val;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_pattern_1mute( JNIEnv* je, jclass jc, jint slot, jint pat_num, jint mute )
{
    return sv_pattern_mute( slot, pat_num, mute );
}
#endif

SUNVOX_EXPORT uint sv_get_ticks( void )
{
    return stime_ticks();
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_get_1ticks( JNIEnv* je, jclass jc )
{
    return sv_get_ticks();
}
#endif

SUNVOX_EXPORT uint sv_get_ticks_per_second( void )
{
    return stime_ticks_per_second();
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jint JNICALL Java_nightradio_sunvoxlib_SunVoxLib_get_1ticks_1per_1second( JNIEnv* je, jclass jc )
{
    return sv_get_ticks_per_second();
}
#endif

SUNVOX_EXPORT const char* sv_get_log( int size )
{
    smem_free( g_sv_log );
    g_sv_log = slog_get_latest( nullptr, size );
    return g_sv_log;
}
#ifdef OS_ANDROID
SUNVOX_EXPORT JNIEXPORT jstring JNICALL Java_nightradio_sunvoxlib_SunVoxLib_get_1log( JNIEnv* je, jclass jc, jint size )
{
    const char* s = sv_get_log( size );
    if( s == 0 ) s = "";
    return je->NewStringUTF( s );
}
#endif
