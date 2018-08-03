//
// * Loading SunVox song from the memory
// * Sending Note ON/OFF events to the module
// * Playing
//
  
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dlfcn.h>
#include <signal.h>

#define SUNVOX_MAIN
#include "../../headers/sunvox.h"

int keep_running = 1;
void int_handler( int param )
{
    keep_running = 0;
}

int main()
{
    signal( SIGINT, int_handler );
    
    void* sunvox_song = malloc( 32000 );
    unsigned int sunvox_song_size;
    FILE* f = fopen( "test.sunvox", "rb" );
    sunvox_song_size = fread( sunvox_song, 1, 32000, f );
    fclose( f );

    if( sv_load_dll() )
	return 1;
    
    int ver = sv_init( 0, 44100, 2, 0 );
    if( ver >= 0 )
    {
	int major = ( ver >> 16 ) & 255;
	int minor1 = ( ver >> 8 ) & 255;
	int minor2 = ( ver ) & 255;
	printf( "SunVox lib version: %d.%d.%d\n", major, minor1, minor2 );
	
	sv_open_slot( 0 );
	
	printf( "Loading SunVox song from memory...\n" );
	if( sv_load_from_memory( 0, sunvox_song, sunvox_song_size ) == 0 )
	    printf( "Loaded.\n" );
	else
	    printf( "Load error.\n" );
	sv_volume( 0, 256 );
	
	//Send two events (Note ON):
	sv_send_event( 0, 0, 64, 128, 7, 0, 0 );
	sleep( 1 );
	sv_send_event( 0, 0, 64, 128, 7, 0, 0 );
	sleep( 1 );
	
	sv_play_from_beginning( 0 );
	
	while( keep_running )
	{
	    printf( "Line counter: %d\n", sv_get_current_line( 0 ) );
	    sleep( 1 );
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
    
    free( sunvox_song );
    
    return 0;
}
