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
#include "lib/log/log.h"
#include "lib/Xbox360-wireless/cXbox360.h"

#include "utils.h"

typedef enum
{
	_main_backTower = 5,
	_main_frontTower = 4,
	_main_dir = 6,
	_main_engine1 = 0,
	_main_engine2 = 1,
}
PCA_PINMAP;

typedef enum
{
	dirCentre = 280, // 350
	dirMin = 210, // 170
	dirMax = 350, // 570
}
DIRECTION;

const uint8_t speedStep = 200;

#define convertSpeed(s) ({ (abs(s) > 8000)? s >> 4: 0; })

static void engine ( int s, int pca9685 )
{
	static int16_t speed = 0;
	if ( (s >> 4) < speed )
	{
		speed = max( convertSpeed ( s ), speed - speedStep );
	}
	else if ( (s >> 4) > speed )
	{
		speed = min( convertSpeed ( s ), speed + speedStep );
	}

	if ( abs ( speed ) > 100 )
	{
		setPCA9685PWM ( _main_engine1, 0, 2047 - speed, pca9685 );
		setPCA9685PWM ( _main_engine2, 0, 2048 + speed, pca9685 );
	}
	else
	{
		setPCA9685PWM ( _main_engine1, 0, 0, pca9685 );
		setPCA9685PWM ( _main_engine2, 0, 0, pca9685 );
	}
}

static void direction ( int d, int pca9685 )
{
	static uint16_t dir = dirCentre;

	if ( (d / 500) < dir )
	{
		dir = max( dirCentre + ( d / 500 ), dir - 10 );
	}
	else if ( (d / 500) > dir )
	{
		dir = min( dirCentre + ( d / 500 ), dir + 10 );
	}

	setPCA9685PWM ( _main_dir, 0, dir, pca9685 );
}

#pragma GCC diagnostic ignored "-Wunused-parameter"
void stopFunction ( void * arg )
{
	exit ( 0 );
}
#pragma GCC diagnostic pop

int main ( int argc,  char * argv[] )
{
	char *paramCam = NULL;
	char *paramDir = NULL;
	char *paramSpeed = NULL;

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
		uint8_t help:1,
			quiet:1,
			verbose:1,
			color:1,
			debug:1,
			term:1,
			file:1;
	}
	flags = { 0, 0, 1, 0, 0, 1, 0 };
	char logFileName[ 512 ] = "";

	int pca9685_addr = 0x50;
	char pca9685_i2c[ 512 ] = "/dev/i2c-1";

	param_el param[] = {
		{ "--help", "-h", 0x01, cT ( bool ), &flags, "this window" },
		{ "--quiet", "-q", 0x02, cT ( bool ), &flags, "quiet" },
		{ "--verbose", "-v", 0x04, cT ( bool ), &flags, "verbose" },
		{ "--color", "-c", 0x08, cT ( bool ), &flags, "color" },
		{ "--debug", "-d", 0x10, cT ( bool ), &flags, "debug" },
		{ "--term", "-lT", 0x20, cT ( bool ), &flags, "log on term" },
		{ "--file", "-lF", 0x40, cT ( bool ), &flags, "log in file" },
		{ "--logFileName", "-lFN", 1, cT ( str ), logFileName, "log file name" },
		{ "--pca9685_addr", "-pA", 1, cT ( int32_t ), &pca9685_addr, "pca9685 address" },
		{ "--pca9685_i2c", "-pI", 1, cT ( str ), pca9685_i2c, "pca9685 i2c bus name" },
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

	if ( readParamArgs ( argc, argv, param ) )
	{ // failure case
	}

	if ( flags.help )
	{// configFile read successfully
		helpParamArgs ( param );
		return ( 0 );
	}

	logSetVerbose ( flags.verbose );
	logSetDebug ( flags.debug );
	logSetColor ( flags.color );
	logSetQuiet ( flags.quiet );
	logSetOutput ( flags.term | !flags.file, flags.file );
	logSetFileName ( logFileName );

	// open PWM module over i2c
	int pca9685 = 0;
	if ( openPCA9685 ( pca9685_i2c, pca9685_addr, &pca9685 ) )
	{
		logVerbose ( "can't open i2c device\n" );
		return ( __LINE__ );
	}

	if ( setCloseOnExit ( pca9685 ) )
	{
		close ( pca9685 );
		logVerbose ( "error during the init of i2c bus\n" );
		return ( __LINE__ );
	}

	setPCA9685PWMFreq ( 50, pca9685 );

	// open joystick
	if ( access ( "/dev/input/js0", F_OK ) )
	{ // file doesn't exist
		if ( access ( "/dev/input/event0", F_OK ) )
		{ // if no event0 : xboxdrv not loaded
			if ( !fork ( ) )
			{
				FILE *f = popen ( "lsusb | grep 045e | cut -d' ' -f6", "r" );
				if ( !f )
				{
					logVerbose ( "can't find xbox adapter\n" );
					exit ( 0 );
				}

				uint8_t buffer [ 256 ] = { 0 };
				fscanf ( f, "%s", buffer );
				fclose ( f );

				logVerbose ( "start xboxdrv\n" );
				logVerbose ( " you should stop it manualy if you want\n" );

				execlp ( "xboxdrv",
					"xboxdrv",
					"--detach-kernel-driver",
					"--device-by-id",
					buffer,
					"-v",
					"--type",
					"xbox360-wireless",
					"--silent",
					NULL );
				//should never occured

				logVerbose ( "an error occured\n" );
				return ( __LINE__ );
			}
		}
		else
		{
			logVerbose ( "xboxdrv seem to be loaded, but no keyâd available\n" );
			logVerbose ( " - check if keypap is connected\n" );
			logVerbose ( " - check if joydev module loaded\n" );
			logVerbose ( "\e[3A" );
		}

		uint8_t loopCounter = 10;
		while ( access ( "/dev/input/js0", F_OK ) )
		{ // wait 10s max
			sleep ( 1 );
			if ( --loopCounter )
			{
				exit ( 0 );
			}
		}
	}

	int joystick = open ( "/dev/input/js0", O_RDONLY | O_NONBLOCK );
	if ( !joystick )
	{
		return ( __LINE__ );
	}

	// manage keypad
	Xbox360Controller pad = { 0 };

	// defaut key pad config
	int16_t * pDir = &pad.X1;
	int16_t * pSpeed = &pad.Y1;
	int16_t * pCam = &pad.X2;

	if ( paramDir &&
		!strcmp ( paramDir, "right" ) )
	{
		pDir = &pad.X2;
	}

	if ( paramSpeed &&
		!strcmp ( paramSpeed, "right" ) )
	{
		pSpeed = &pad.Y2;
	}

	if ( paramCam &&
		!strcmp ( paramCam, "left" ) )
	{
		pCam = &pad.X1;
	}

	getStatus360 ( joystick, &pad, true );

	while ( !getStatus360 ( joystick, &pad, false ) )
	{
		engine ( *pSpeed, pca9685 );
		direction ( *pDir, pca9685 );

		usleep ( 50000 );
	}

	return ( 0 );
}
