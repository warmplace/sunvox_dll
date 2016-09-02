//
// * Loading SunVox song from file.
// * Using SunVox audio callback.
// * Exporting the audio to the file.
//
  
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dlfcn.h>
#include <signal.h>

#define SUNVOX_MAIN
#include "../../headers/sunvox.h"

int g_sv_sampling_rate = 44100; //Hz
int g_sv_channels_num = 2; //1 - mono; 2 - stereo
int g_sv_buffer_size = 1024; //Audio buffer size (number of frames)

int keep_running = 1;
void int_handler( int param ) 
{
    keep_running = 0;
}

int main()
{
    signal( SIGINT, int_handler );
    
    if( sv_load_dll() )
	return 1;
    
    int ver = sv_init( 
	0, 
	g_sv_sampling_rate, 
	g_sv_channels_num, 
	SV_INIT_FLAG_USER_AUDIO_CALLBACK | SV_INIT_FLAG_AUDIO_INT16 | SV_INIT_FLAG_ONE_THREAD
    );
    if( ver >= 0 )
    {
	int major = ( ver >> 16 ) & 255;
	int minor1 = ( ver >> 8 ) & 255;
	int minor2 = ( ver ) & 255;
	printf( "SunVox lib version: %d.%d.%d\n", major, minor1, minor2 );
	
	sv_open_slot( 0 );
	
	printf( "Loading SunVox song from file...\n" );
	if( sv_load( 0, "test.sunvox" ) == 0 )
	    printf( "Loaded.\n" );
	else
	    printf( "Load error.\n" );
	sv_volume( 0, 256 );
	
	sv_play_from_beginning( 0 );
	
	//Saving the audio stream to the file:
	//(audio stream is 16bit stereo interleaved (LRLRLRLR...))
	FILE* f = fopen( "audio_stream.raw", "wb" );
	if( f )
	{
	    signed short* buf = (signed short*)malloc( g_sv_buffer_size * g_sv_channels_num * sizeof( signed short ) ); //Audio buffer
	    int song_len_frames = sv_get_song_length_frames( 0 );
	    int cur_frame = 0;
	    while( keep_running && cur_frame < song_len_frames )
	    {
		//Get the next piece of audio:
		int frames_num = g_sv_buffer_size;
		if( cur_frame + frames_num > song_len_frames )
		    frames_num = song_len_frames - cur_frame;
		sv_audio_callback( buf, frames_num, 0, 0 );
		cur_frame += frames_num;
		
		//Save this data to the file:
		fwrite( buf, 1, frames_num * g_sv_channels_num * sizeof( signed short ), f );
		
		//Print some info:
		printf( "Playing position: %d %%\n", (int)( ( (float)cur_frame / (float)song_len_frames ) * 100 ) );
	    }
	    fclose( f );
	    free( buf );
	}
	else
	{
	    printf( "Can't open the file\n" );
	}
	
	sv_stop( 0 );
	
	sv_close_slot( 0 );
	
	sv_deinit();
    }
    else 
    {
	printf( "sv_init() error %d\n", ver );
    }
    
    sv_unload_dll();
    
    return 0;
}
