#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>

#include "lib/freeOnExit/freeOnExit.h"
#include "lib/pca9685/pca9685.h"
#include "lib/config/config_arg.h"
#include "lib/signalHandler/signalHandler.h"

#include "lib/Xbox360-wireless/cXbox360.h"

const uint8_t _main_backTower = 14;
const uint8_t _main_dir = 15;
// if en1 < en2 front dir esle back dir
const uint8_t _main_engine1 = 12;
const uint8_t _main_engine2 = 13;

#pragma GCC diagnostic ignored "-Wunused-parameter"
void stopFunction ( void * arg )
{
	exit ( 0 );
}
#pragma GCC diagnostic pop

int main ( int argc,  char * argv[] )
{
	int pca9685 = 0;
	int joystick = 0;

	int16_t dir = 300;
	int16_t dirT = 300;

	Xbox360Controller pad = { 0 };
	int cmd = 0;
	uint32_t i = 0;
	uint8_t buffer [ 256 ] = { 0 };
	FILE *f = NULL;

	char *paramCam = NULL;
	char *paramDir = NULL;
	char *paramSpeed = NULL;

	int16_t * pCam;
	int16_t * pDir;
	int16_t * pSpeed;

	signalHandling sig = {
		.flag = { 
			.Int = 1
		},
		.Int = {
			.func = stopFunction,
			.arg = NULL
		}
	};

	struct
	{
		uint8_t help:1;
	}flags = { 0 };

	param_el param[] = {
		{ "--help", "-h", 0x01, cT ( bool ), &flags, "this window" },
		{ "--dir", "-d", 1, cT ( str ), paramDir, "direction joystick position on keypad ( left/right )" },
		{ "--speed", "-s", 1, cT ( str ), paramSpeed, "speed joystick position on keypad ( left/right )" },
		{ "--cam", "-c", 1, cT ( str ), paramCam, "camera joystick position on keypad ( right/left )" },
		{ NULL, NULL, 0, 0, NULL, NULL }
	};

	signalHandlerInit ( &sig );

	if ( initFreeOnExit ( ) )
	{
		return ( __LINE__ );
	}

	// defaut key pad config
	pDir = &pad.X1;
	pSpeed = &pad.Y1;
	pCam = &pad.X2;

	if ( readParamArgs ( argc, argv, param ) )
	{ // failure case
	}
	
	if ( flags.help )
	{// configFile read successfully
		helpParamArgs ( param );
		return ( 0 );
	}

	if ( !strcmp ( paramDir, "right" ) )
	{
		pDir = &pad.X2;
	}

	if ( !strcmp ( paramSpeed, "right" ) )
	{
		pSpeed = &pad.Y2;
	}

	if ( !strcmp ( paramCam, "left" ) )
	{
		pCam = &pad.X1;
	}

	// open PWM module over i2c 
	if ( openPCA9685 ( "/dev/i2c-0", 0x40, &pca9685 ) &&
		openPCA9685 ( "/dev/i2c-1", 0x40, &pca9685 ) )
	{
		return ( __LINE__ );
	}

	if ( setCloseOnExit ( pca9685 ) )
	{
		close ( pca9685 );
		return ( __LINE__ );
	}

	// open joystick
	if ( access ( "/dev/input/js0", F_OK ) )
	{ // file doesn't exist
		if ( !fork ( ) )
		{
			f = popen ( "lsusb | grep 045e | cut -d' ' -f6", "r" );
			if ( !f )
			{
				exit ( 0 );
			}
			fscanf ( f, "%s", buffer );
			fclose ( f );

			execlp ( "xboxdrv",
				"xboxdrv",
				"--detach-kernel-driver",
				"--device-by-id",
				buffer,
				"-v",
				"--type",
				"xbox360-wireless",
				NULL );
			//should never occured
			return ( __LINE__ );
		}
		else
		{
			i = 10;
			while ( access ( "/dev/input/js0", F_OK ) )
			{ // wait 10s max
				sleep ( 1 );
				if ( --i )
				{
					exit ( 0 );
				}
			}
		}
	}

	joystick = open ( "/dev/input/js0", O_RDONLY | O_NONBLOCK );
	if ( !joystick )
	{
		return ( __LINE__ );
	}

	// manage keypad
	getStatus360 ( joystick, &pad, true );

	while ( getStatus360 ( joystick, &pad, false ) )
	{
		if ( (*pDir) > 4000 )
		{
			dirT = 300 + ( (*pDir) - 4000 ) / 280;
		}
		else if ( (*pDir) < -4000 )
		{
			dirT = 300 + ( (*pDir) + 4000 ) / 280;
		}
		else
		{
			dirT = 300;
		}

		if ( ( (*pSpeed) > -8000 ) &&
			( (*pSpeed) < 8000 ) )
		{
			setPCA9685PWM ( _main_engine1, 0, 0, pca9685 );
			setPCA9685PWM ( _main_engine2, 0, 0, pca9685 );
		}
		else if ( (*pSpeed) > 0 )
		{
			setPCA9685PWM ( _main_engine1, 0, 4000, pca9685 );
			setPCA9685PWM ( _main_engine2, 0, 3000 - 30 * (*pSpeed) / 330, pca9685 );
		}
		else
		{
			setPCA9685PWM ( _main_engine1, 0, 3000 + 30 * (*pSpeed) / 330, pca9685 );
			setPCA9685PWM ( _main_engine2, 0, 4000, pca9685 );
		}

		if ( dirT > dir )
		{
			dir+=10;
		}
		else if ( dirT < dir )
		{
			dir-=10;
		}

		setPCA9685PWM ( _main_dir, 0, dir, pca9685 );
		
		usleep ( 50000 );
	}

	return ( 0 );
}