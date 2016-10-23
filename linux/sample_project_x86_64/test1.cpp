//
// * Loading SunVox song from file.
// * Sending SunVox events.
// * Playing SunVox song.
//
  
#include <stdio.h>
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
	
	printf( "Loading SunVox song from file...\n" );
	if( sv_load( 0, "test.sunvox" ) == 0 )
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
	    printf( "Line counter: %f Module 7 -> %s = %d\n", 
		(float)sv_get_current_line2( 0 ) / 32, 
		sv_get_module_ctl_name( 0, 7, 1 ), //Get controller name
		sv_get_module_ctl_value( 0, 7, 1, 0 ) //Get controller value
	    );
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
    
    return 0;
}
