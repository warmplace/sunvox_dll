//
// * Creating the new module.
// * Loading the module from disk.
// * Connecting the module to the main Output.
// * Sending some events to this module.
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

	//Create Generator module:
	sv_lock_slot( 0 );
	int mod_num = sv_new_module( 0, "Generator", "Generator", 0, 0, 0 );
	sv_unlock_slot( 0 );
	if( mod_num >= 0 )
	{
	    printf( "New module created: %d\n", mod_num );
	    //Connect the new module to the Main Output:
	    sv_lock_slot( 0 );
	    sv_connect_module( 0, mod_num, 0 );
	    sv_unlock_slot( 0 );
	    //Send Note ON:
	    printf( "Note ON\n" );
	    sv_send_event( 0, 0, 64, 128, mod_num + 1, 0, 0 );
	    sleep( 1 );
	    //Send Note OFF:
	    printf( "Note OFF\n" );
	    sv_send_event( 0, 0, NOTECMD_NOTE_OFF, 128, mod_num + 1, 0, 0 );
	    sleep( 1 );
	}
	else
	{
	    printf( "Can't create the new module\n" );
	}

	//Load module and play it:
	int mod_num2 = sv_load_module( 0, "organ.sunsynth", 0, 0, 0 );
	if( mod_num2 >= 0 )
	{
	    printf( "Module loaded: %d\n", mod_num2 );
	    //Connect the new module to the Main Output:
	    sv_lock_slot( 0 );
	    sv_connect_module( 0, mod_num2, 0 );
	    sv_unlock_slot( 0 );
	    //Send Note ON:
	    printf( "Note ON\n" );
	    sv_send_event( 0, 0, 64, 128, mod_num2 + 1, 0, 0 );
	    sleep( 1 );
	    //Send Note OFF:
	    printf( "Note OFF\n" );
	    sv_send_event( 0, 0, NOTECMD_NOTE_OFF, 128, mod_num2 + 1, 0, 0 );
	    sleep( 1 );
	}
	else
	{
	    printf( "Can't load the module\n" );
	}

	while( keep_running )
	{
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
