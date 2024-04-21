/***********************************************************************************
  Snes9x - Portable Super Nintendo Entertainment System (TM) emulator.

  See CREDITS file to find the copyright owners of this file.

  SDL Input/Audio/Video code (many lines of code come from snes9x & drnoksnes)
  The code conversion to SDL2 was done by https://github.com/darkxex/
  (c) Copyright 2011         Makoto Sugano (makoto.sugano@gmail.com)

  Snes9x homepage: http://www.snes9x.com/

  Permission to use, copy, modify and/or distribute Snes9x in both binary
  and source form, for non-commercial purposes, is hereby granted without
  fee, providing that this license information and copyright notice appear
  with all copies and any derived work.

  This software is provided 'as-is', without any express or implied
  warranty. In no event shall the authors be held liable for any damages
  arising from the use of this software or it's derivatives.

  Snes9x is freeware for PERSONAL USE only. Commercial users should
  seek permission of the copyright holders first. Commercial use includes,
  but is not limited to, charging money for Snes9x or software derived from
  Snes9x, including Snes9x or derivatives in commercial game bundles, and/or
  using Snes9x as a promotion for your commercial product.

  The copyright holders request that bug fixes and improvements to the code
  should be forwarded to them so everyone can benefit from the modifications
  in future versions.

  Super NES and Super Nintendo Entertainment System are trademarks of
  Nintendo Co., Limited and its subsidiary companies.
 ***********************************************************************************/

#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>
#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif

#include "snes9x.h"
#include "memmap.h"
#include "ppu.h"
#include "controls.h"
#include "movie.h"
#include "conffile.h"
#include "blit.h"
#include "display.h"
#include "iostream"
#include "sdl_snes9x.h"

//markus

#include "logo.h"
//


struct GUIData	GUI;

//markus
#ifdef __MORPHOS__
#define SDL_FULLSCREEN SDL_WINDOW_FULLSCREEN
#else
#define SDL_FULLSCREEN SDL_WINDOW_FULLSCREEN_DESKTOP
#endif
uint32 videoFlags =  SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
SDL_Rect sdl_sizescreen;
char* dropped_filedir = NULL;                  // Pointer for directory of dropped file
//

typedef	void (* Blitter) (uint8 *, int, uint8 *, int, int, int);

#ifdef __linux
// Select seems to be broken in 2.x.x kernels - if a signal interrupts a
// select system call with a zero timeout, the select call is restarted but
// with an infinite timeout! The call will block until data arrives on the
// selected fd(s).
//
// The workaround is to stop the X library calling select in the first
// place! Replace XPending - which polls for data from the X server using
// select - with an ioctl call to poll for data and then only call the blocking
// XNextEvent if data is waiting.
#define SELECT_BROKEN_FOR_SIGNALS
#endif



static void SetupImage (void);
static void TakedownImage (void);

void S9xExtraDisplayUsage (void)
{
#ifndef SDL_DROP
#ifdef AOS4QEMU
	S9xMessage(S9X_INFO, S9X_USAGE, "-fullscreen                     fullscreen mode (without scaling)");
#else
	S9xMessage(S9X_INFO, S9X_USAGE, "-fullscreen                     fullscreen mode");
#endif
	S9xMessage(S9X_INFO, S9X_USAGE, "");
	S9xMessage(S9X_INFO, S9X_USAGE, "-v1                             Video mode: Blocky (default)");
	S9xMessage(S9X_INFO, S9X_USAGE, "-v2                             Video mode: TV");
	S9xMessage(S9X_INFO, S9X_USAGE, "-v3                             Video mode: Smooth");
	S9xMessage(S9X_INFO, S9X_USAGE, "-v4                             Video mode: SuperEagle");
	S9xMessage(S9X_INFO, S9X_USAGE, "-v5                             Video mode: 2xSaI");
	S9xMessage(S9X_INFO, S9X_USAGE, "-v6                             Video mode: Super2xSaI");
	S9xMessage(S9X_INFO, S9X_USAGE, "-v7                             Video mode: EPX");
	S9xMessage(S9X_INFO, S9X_USAGE, "-v8                             Video mode: hq2x");
	S9xMessage(S9X_INFO, S9X_USAGE, "");
#endif
}

void S9xParseDisplayArg (char **argv, int &i, int argc)
{
	if (!strncasecmp(argv[i], "-fullscreen", 11))
        {
                GUI.fullscreen = TRUE;
                printf ("Entering fullscreen mode (without scaling).\n");
        }
        else
	if (!strncasecmp(argv[i], "-v", 2))
	{
		switch (argv[i][2])
		{
			case '1':	GUI.video_mode = VIDEOMODE_BLOCKY;		break;
			case '2':	GUI.video_mode = VIDEOMODE_TV;			break;
			case '3':	GUI.video_mode = VIDEOMODE_SMOOTH;		break;
			case '4':	GUI.video_mode = VIDEOMODE_SUPEREAGLE;	break;
			case '5':	GUI.video_mode = VIDEOMODE_2XSAI;		break;
			case '6':	GUI.video_mode = VIDEOMODE_SUPER2XSAI;	break;
			case '7':	GUI.video_mode = VIDEOMODE_EPX;			break;
			case '8':	GUI.video_mode = VIDEOMODE_HQ2X;		break;
		}
	}
	else
		S9xUsage();
}

const char * S9xParseDisplayConfig (ConfigFile &conf, int pass)
{
	if (pass != 1)
		return ("Unix/SDL");

	if (conf.Exists("Unix/SDL::VideoMode"))
	{
		GUI.video_mode = conf.GetUInt("Unix/SDL::VideoMode", VIDEOMODE_BLOCKY);
		if (GUI.video_mode < 1 || GUI.video_mode > 8)
			GUI.video_mode = VIDEOMODE_BLOCKY;
	}
	else
		GUI.video_mode = VIDEOMODE_BLOCKY;

	return ("Unix/SDL");
}

static void FatalError (const char *str)
{
#ifndef SDL_DROP
	fprintf(stderr, "%s\n", str);
#endif
	S9xExit();
}

void S9xInitDisplay (int argc, char **argv)
{
	if (SDL_Init(SDL_INIT_VIDEO) != 0)
	{
		printf("Unable to initialize SDL: %s\n", SDL_GetError());
	}
  
	atexit(SDL_Quit);
	
	/*
	 * domaemon
	 *
	 * we just go along with RGB565 for now, nothing else..
	 */

	S9xBlitFilterInit();
	S9xBlit2xSaIFilterInit();
	S9xBlitHQ2xFilterInit();
	
	/*
	 * domaemon
	 *
	 * FIXME: The secreen size should be flexible
	 * FIXME: Check if the SDL screen is really in RGB565 mode. screen->fmt	
	 */	
    if (GUI.fullscreen == TRUE)
    {
	    videoFlags |= SDL_FULLSCREEN;
	    SDL_ShowCursor(SDL_FALSE);
	}

	sdl_sizescreen.w = (SNES_WIDTH*2);
	sdl_sizescreen.h = (SNES_HEIGHT_EXTENDED*2);
	sdl_sizescreen.y = 0;
	sdl_sizescreen.x =0;

	GUI.pixels = (uint32*)calloc(1, (sdl_sizescreen.w * sdl_sizescreen.h) * sizeof(uint32));
	
	GUI.sdl_window =  SDL_CreateWindow("Snes9x for SDL2", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, sdl_sizescreen.w,  sdl_sizescreen.h, videoFlags ); 
	GUI.sdl_renderer = SDL_CreateRenderer(GUI.sdl_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (!GUI.sdl_renderer) {
		GUI.sdl_renderer = SDL_CreateRenderer(GUI.sdl_window, -1, SDL_RENDERER_SOFTWARE | SDL_RENDERER_PRESENTVSYNC);
		printf("SDL use SOFTWARE renderer with VSYNC !\n");
	} else {
		printf("SDL use ACCELERATED renderer with VSYNC !\n");
	}

	// scaling hint - before texture creation
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    GUI.sdl_texture = SDL_CreateTexture(GUI.sdl_renderer, 
                               SDL_PIXELFORMAT_RGB565,
                               SDL_TEXTUREACCESS_STREAMING,
                               sdl_sizescreen.w, sdl_sizescreen.h);

	SDL_RenderSetLogicalSize(GUI.sdl_renderer, sdl_sizescreen.w, SNES_HEIGHT * 2);
	SDL_RenderClear(GUI.sdl_renderer);
	/*
	 * domaemon
	 *
	 * buffer allocation, quite important
	 */
	SetupImage();
}

/*
 * toggles fullscreen.
 */
void S9xSDL_toggleFullscreen(void)
{

	if (videoFlags & SDL_FULLSCREEN)
	{ 
	   videoFlags &= ~ SDL_FULLSCREEN;

	   SDL_SetWindowFullscreen(GUI.sdl_window, videoFlags);
	   SDL_ShowCursor(SDL_ENABLE);
    }
        else 
	{
	   videoFlags |= SDL_FULLSCREEN;
	   SDL_SetWindowFullscreen(GUI.sdl_window, videoFlags);
	   SDL_ShowCursor(SDL_DISABLE);
	}
#ifdef __MORPHOS__
	SDL_RenderClear(GUI.sdl_renderer);
#endif
}

static void TakedownImage (void)
{

	if (GUI.snes_buffer)
	{
		free(GUI.snes_buffer);
		GUI.snes_buffer = NULL;
	}

	S9xGraphicsDeinit();
}

void S9xDeinitDisplay (void)
{
	TakedownImage();
//markus
	free(GUI.pixels);
	GUI.pixels = NULL;
#ifdef __MORPHOS__
	if (GUI.sdl_texture)
		SDL_DestroyTexture(GUI.sdl_texture);
	
	if (GUI.sdl_renderer)
		SDL_DestroyRenderer(GUI.sdl_renderer);
#endif

	SDL_DestroyWindow(GUI.sdl_window);
	SDL_Quit();

	S9xBlitFilterDeinit();
	S9xBlit2xSaIFilterDeinit();
	S9xBlitHQ2xFilterDeinit();
}

static void SetupImage (void)
{
	TakedownImage();

	// domaemon: The whole unix code basically assumes output=(original * 2);
	// This way the code can handle the SNES filters, which does the 2X.
	GFX.Pitch = SNES_WIDTH * 2 * 2;
	GUI.snes_buffer = (uint8 *) calloc(GFX.Pitch * ((SNES_HEIGHT_EXTENDED + 4) * 2), 1);
	if (!GUI.snes_buffer)
		FatalError("Failed to allocate GUI.snes_buffer.");

	// domaemon: Add 2 lines before drawing.
	GFX.Screen = (uint16 *) (GUI.snes_buffer + (GFX.Pitch * 2 * 2));
	GUI.blit_screen = (uint8 *) GUI.pixels;
	GUI.blit_screen_pitch = SNES_WIDTH * 2 * 2; // window size =(*2); 2 byte pir pixel =(*2)

	S9xGraphicsInit();
}

void S9xPutImage (int width, int height)
{
	static int	prevWidth = 0, prevHeight = 0;
	Blitter		blitFn = NULL;

	if (GUI.video_mode == VIDEOMODE_BLOCKY || GUI.video_mode == VIDEOMODE_TV || GUI.video_mode == VIDEOMODE_SMOOTH)
		if ((width <= SNES_WIDTH) && ((prevWidth != width) || (prevHeight != height)))
			S9xBlitClearDelta();

	if (width <= SNES_WIDTH)
	{
		if (height > SNES_HEIGHT_EXTENDED)
		{
			blitFn = S9xBlitPixSimple2x1;
		}
		else
		{

			switch (GUI.video_mode)
			{
				case VIDEOMODE_BLOCKY:		blitFn = S9xBlitPixSimple2x2;		break;
				case VIDEOMODE_TV:			blitFn = S9xBlitPixTV2x2;			break;
				case VIDEOMODE_SMOOTH:		blitFn = S9xBlitPixSmooth2x2;		break;
				case VIDEOMODE_SUPEREAGLE:	blitFn = S9xBlitPixSuperEagle16;	break;
				case VIDEOMODE_2XSAI:		blitFn = S9xBlitPix2xSaI16;			break;
				case VIDEOMODE_SUPER2XSAI:	blitFn = S9xBlitPixSuper2xSaI16;	break;
				case VIDEOMODE_EPX:			blitFn = S9xBlitPixEPX16;			break;
				case VIDEOMODE_HQ2X:		blitFn = S9xBlitPixHQ2x16;			break;
			}
		}
	}
	else
	if (height <= SNES_HEIGHT_EXTENDED)
	{
		switch (GUI.video_mode)
		{
			default:					blitFn = S9xBlitPixSimple1x2;	break;
			case VIDEOMODE_TV:			blitFn = S9xBlitPixTV1x2;		break;
		}
	}
	else
	{
		blitFn = S9xBlitPixSimple1x1;
	}


	// domaemon: this is place where the rendering buffer size should be changed?
	blitFn((uint8 *) GFX.Screen, GFX.Pitch, GUI.blit_screen, GUI.blit_screen_pitch, width, height);

	// domaemon: does the height change on the fly?
	if (height < prevHeight)
	{
		int	p = GUI.blit_screen_pitch >> 2;
		for (int y = SNES_HEIGHT * 2; y < SNES_HEIGHT_EXTENDED * 2; y++)
		{
			uint32	*d = (uint32 *) (GUI.blit_screen + y * GUI.blit_screen_pitch);
			for (int x = 0; x < p; x++)
				*d++ = 0;
		}
	}

    SDL_UpdateTexture(GUI.sdl_texture, NULL, GUI.blit_screen, GUI.blit_screen_pitch);
	SDL_RenderCopy(GUI.sdl_renderer, GUI.sdl_texture, NULL, &sdl_sizescreen);
	SDL_RenderPresent(GUI.sdl_renderer);

	prevWidth  = width;
	prevHeight = height;
}

void S9xMessage (int type, int number, const char *message)
{
	const int	max = 36 * 3;
	static char	buffer[max + 1];

#ifndef SDL_DROP
	fprintf(stdout, "%s\n", message);
#endif
	strncpy(buffer, message, max + 1);
	buffer[max] = 0;
	S9xSetInfoString(buffer);
}

const char * S9xStringInput (const char *message)
{
	static char	buffer[256];

	printf("%s: ", message);
	fflush(stdout);

	if (fgets(buffer, sizeof(buffer) - 2, stdin))
		return (buffer);

	return (NULL);
}

void S9xSetTitle (const char *string)
{
	SDL_SetWindowTitle(GUI.sdl_window, string); 
}

void S9xSetPalette (void)
{
	return;
}
//markus
void S9xDropWindow (void)
{

    //extension
    const char    *extensions[] =
    {
            ".smc", ".SMC", ".fig", ".FIG", ".sfc", ".SFC",
            ".jma", ".JMA", ".zip", ".ZIP", ".gd3", ".GD3",
            ".swc", ".SWC", ".gz", ".GZ", ".bs", ".BS",
            NULL
    };

    char *ext;
    SDL_bool done,bingo;
    SDL_Window *window;
    SDL_Event event;                        // Declare event handle

    SDL_Init(SDL_INIT_VIDEO);               // SDL2 initialization

    window = SDL_CreateWindow(  // Create a window
        "Snes9x for SDL2 : usage, please drop the rom file on window",
	SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 640, 480, 0);
    // Check that the window was successfully made
    if (window == NULL) {
        // In the event that the window could not be made...
        SDL_Log("Could not create window: %s", SDL_GetError());
        SDL_Quit();
    }
	
    SDL_RWops *pixelsWop = SDL_RWFromConstMem((const unsigned char *)bin2c_lolosnes_bmp, sizeof(bin2c_lolosnes_bmp));
    SDL_Surface *image = SDL_LoadBMP_RW(pixelsWop, 1);

    SDL_Renderer * renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (renderer == NULL)
		renderer = SDL_CreateRenderer(GUI.sdl_window, -1, SDL_RENDERER_SOFTWARE | SDL_RENDERER_PRESENTVSYNC);

    SDL_Texture * texture = SDL_CreateTextureFromSurface(renderer, image);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);



    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);

    done = SDL_FALSE;
    bingo = SDL_FALSE;
    while (!done) {                         // Program loop
        while (!done && SDL_PollEvent(&event)) {
            switch (event.type) {
                case (SDL_QUIT): {          // In case of exit
                    done = SDL_TRUE;
                    break;
                }
				case (SDL_KEYDOWN):
					if(event.key.keysym.sym == SDLK_q) {
						S9xReportButton(SDLK_ESCAPE, true );
						done = SDL_TRUE;
					}
					if(event.key.keysym.sym == SDLK_ESCAPE) {
						S9xReportButton(SDLK_ESCAPE, true );
						done = SDL_TRUE;
					}	
 		    		break;

                case (SDL_DROPFILE): {      // In case if dropped file
					//check extension
					ext = strrchr(event.drop.file, '.');
					if (ext) 
					{
					  for (int i = 0; extensions[i]; i++)
					{
						if (strcmp(extensions[i],ext) == 0 )
							bingo = SDL_TRUE;
					}
					}
					if (!bingo)
					{
						SDL_ShowSimpleMessageBox(
                         SDL_MESSAGEBOX_ERROR,
                         "File dropped on window",
                         "!!!  FILE IS  not ROM !!!",
                         window);
					}
					else 
					{
						dropped_filedir = event.drop.file;
					 	done = SDL_TRUE;
					}

                 }
                    break;
            }
        }
        SDL_Delay(1);
    }

    // Close and destroy the window
    SDL_DestroyTexture(texture);
    SDL_FreeSurface(image);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);        

// Clean up
    SDL_Quit();                       
    if (!dropped_filedir)
	exit(0);
}
