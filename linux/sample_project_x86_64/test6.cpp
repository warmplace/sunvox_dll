//
// * Using SunVox as a filter for some user-generated signal
//   (with export to WAV)
//
  
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dlfcn.h>
#include <signal.h>
#include <math.h>

#define SUNVOX_MAIN
#include "../../headers/sunvox.h"

int g_sv_sampling_rate = 44100; //Hz
int g_sv_channels_num = 2; //1 - mono; 2 - stereo
int g_sv_buffer_size = 1024 * 4; //Audio buffer size (number of frames)

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
	
	sv_volume( 0, 256 );
	sv_lock_slot( 0 );
	int mod1 = sv_new_module( 0, "Input", "Input", 96, 0, 0 );
	int mod2 = sv_new_module( 0, "Flanger", "Flanger", 64, 0, 0 );
	int mod3 = sv_new_module( 0, "Reverb", "Reverb", 32, 0, 0 );
	printf( "Input: %d\n", mod1 );
	printf( "Flanger: %d\n", mod2 );
	printf( "Reverb: %d\n", mod3 );
	sv_connect_module( 0, mod1, mod2 ); //Input -> Flanger
	sv_connect_module( 0, mod2, mod3 ); //Flanger -> Reverb
	sv_connect_module( 0, mod3, 0 ); //Reverb -> Output
	sv_unlock_slot( 0 );
	sv_update_input();
	
	//Saving the audio stream to the WAV file:
	//(audio format: 16bit stereo interleaved (LRLRLRLR...))
	FILE* f = fopen( "audio_stream2.wav", "wb" );
	if( f )
	{	
	    signed short* buf = (signed short*)malloc( g_sv_buffer_size * g_sv_channels_num * sizeof( signed short ) ); //Output audio buffer
	    signed short* in_buf = (signed short*)malloc( g_sv_buffer_size * g_sv_channels_num * sizeof( signed short ) ); //Input audio buffer
	    unsigned int out_frames = g_sv_sampling_rate * 8; //8 seconds
            unsigned int out_bytes = out_frames * sizeof( signed short ) * g_sv_channels_num;
	    unsigned int cur_frame = 0;
	    unsigned int val;
	    
	    //WAV header:
            fwrite( (void*)"RIFF", 1, 4, f );
            val = 4 + 24 + 8 + out_bytes; fwrite( &val, 4, 1, f );
            fwrite( (void*)"WAVE", 1, 4, f );
            
            //WAV FORMAT:
            fwrite( (void*)"fmt ", 1, 4, f );
            val = 16; fwrite( &val, 4, 1, f );
            val = 1; fwrite( &val, 2, 1, f ); //format
            val = g_sv_channels_num; fwrite( &val, 2, 1, f ); //channels
            val = g_sv_sampling_rate; fwrite( &val, 4, 1, f ); //frames per second
            val = g_sv_sampling_rate * g_sv_channels_num * sizeof( signed short ); fwrite( &val, 4, 1, f ); //bytes per second
            val = g_sv_channels_num * sizeof( signed short ); fwrite( &val, 2, 1, f ); //block align
    	    val = sizeof( signed short ) * 8; fwrite( &val, 2, 1, f ); //bits
                        
            //WAV DATA:
            fwrite( (void*)"data", 1, 4, f );
            fwrite( &out_bytes, 4, 1, f );
            int pos = 0;
	    while( keep_running && cur_frame < out_frames )
	    {
		int size = g_sv_buffer_size;
		if( cur_frame + size > out_frames )
		    size = out_frames - cur_frame;

		//Generate the input:
		signed short* in = in_buf;
		for( int i = 0; i < size; i++, in += g_sv_channels_num )
		{
		    int phase = cur_frame + i;
		    float a = sin( (float)phase / 4096.0F );
		    phase += a * 1024 * 8;
		    int v = ( phase & 511 ) - 256;
		    v *= 64 * a;
		    in[ 0 ] = v; //left channel
		    in[ 1 ] = v; //right channel
		}

		//Send it to SunVox and read the filtered output:
		sv_audio_callback2( 
		    buf, //output buffer
		    size, //output buffer length (frames)
		    0, //latency (frames)
		    sv_get_ticks(), //output time in system ticks
		    0, //input type: 0 - int16; 1 - float32
		    g_sv_channels_num, //input channels
		    in_buf //input buffer
		);
		
		cur_frame += size;
		
		//Save this data to the file:
		fwrite( buf, 1, size * g_sv_channels_num * sizeof( signed short ), f );
		
		//Print some info:
		int new_pos = (int)( ( (float)cur_frame / (float)out_frames ) * 100 );
		if( pos != new_pos )
		{
		    printf( "%d %%\n", pos );
		    pos = new_pos;
		}
	    }
	    fclose( f );
	    free( buf );
	    free( in_buf );
	}
	else
	{
	    printf( "Can't open the file\n" );
	}
	
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
