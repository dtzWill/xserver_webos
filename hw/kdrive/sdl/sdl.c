/*
 * Copyright © 2004 PillowElephantBadgerBankPond 
 
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of PillowElephantBadgerBankPond not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  PillowElephantBadgerBankPond makes no
 * representations about the suitability of this software for any purpose.  It
 * is provided "as is" without express or implied warranty.
 *
 * PillowElephantBadgerBankPond DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL PillowElephantBadgerBankPond BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 * It's really not my fault - see it was the elephants!!
 * 	- jaymz
 *
 */
#ifdef HAVE_CONFIG_H
#include "kdrive-config.h"
#endif
#include "kdrive.h"
#include <SDL/SDL.h>
#include <X11/keysym.h>

#include <SDL/SDL_video.h>
#include <SDL/SDL_opengles.h>
#include "esFunc.h"

#define WIDTH 320
#define HEIGHT 480

//#define DEBUG_GL

#ifdef DEBUG_GL
static void checkError()
{
    /* Check for error conditions. */
    GLenum gl_error = glGetError( );

    if( gl_error != GL_NO_ERROR ) {
        fprintf( stderr, "X sdlgl: OpenGL error: %x\n", gl_error );
        while(1);
        exit( 1 );
    }

    char * sdl_error = SDL_GetError( );

    if( sdl_error[0] != '\0' ) {
        fprintf(stderr, "X sdlgl: SDL error '%s'\n", sdl_error);
        while(1);
        exit( 2 );
    }
}
#else
#define checkError()
#endif

struct SdlGLESDriver;

void GL_Init(void);
void GL_InitTexture( struct SdlGLESDriver * driver );
void GL_Render( struct SdlGLESDriver * driver );

/*-----------------------------------------------------------------------------
 *  GL variables
 *-----------------------------------------------------------------------------*/
GLuint texture = 0;

// Handle to a program object
GLuint programObject;

// Attribute locations
GLint  positionLoc;
GLint  texCoordLoc;

// Sampler location
GLint samplerLoc;

//We're doing one-to-one texture to screen anyway
int gl_filter = GL_NEAREST;

//Landscape, keyboard on left.
float land_l_vertexCoords[] =
{
    -1, -1,
    1, -1,
    -1, 1,
    1, 1
};

//Landscape, keyboard on right.
float land_r_vertexCoords[] =
{
    1, 1,
    -1, 1,
    1, -1,
    -1, -1
};
//Portrait
float portrait_vertexCoords[] =
{
    -1, 1,
    -1, -1,
    1, 1,
    1, -1
};

//Pick an orientation
//Have to update few other places to reflect changing this, fwiw
float * vertexCoords = NULL;


float texCoords[] =
{
    0.0, 0.0,
    0.0, 1.0,
    1.0, 0.0,
    1.0, 1.0
};

GLushort indices[] = { 0, 1, 2, 1, 2, 3 };

//We're using 24 bitdepth, each color has it's own byte
//Note that the screen still is 32bit, but we use 24bit so we can upload
//as a texture more easily (no reason to introduce alpha)
int redMask = 0x0000ff;
int greenMask = 0x00ff00;
int blueMask = 0xff0000;

static void xsdlFini(void);
static Bool sdlScreenInit(KdScreenInfo *screen);
static Bool sdlFinishInitScreen(ScreenPtr pScreen);
static Bool sdlCreateRes(ScreenPtr pScreen);

static void sdlKeyboardFini(KdKeyboardInfo *ki);
static Status sdlKeyboardInit(KdKeyboardInfo *ki);
static Status sdlKeyboardEnable(KdKeyboardInfo *pi);
static void sdlKeyboardDisable(KdKeyboardInfo *pi);

static Status sdlMouseInit(KdPointerInfo *pi);
static void sdlMouseFini(KdPointerInfo *pi);
static Status sdlMouseEnable(KdPointerInfo *pi);
static void sdlMouseDisable(KdPointerInfo *pi);

void *sdlShadowWindow (ScreenPtr pScreen, CARD32 row, CARD32 offset, int mode, CARD32 *size, void *closure);
void sdlShadowUpdate (ScreenPtr pScreen, shadowBufPtr pBuf);

void sdlTimer(void);

KdKeyboardInfo *sdlKeyboard = NULL;
KdPointerInfo *sdlPointer = NULL;

KdKeyboardDriver sdlKeyboardDriver = {
    .name = "keyboard",
    .Init = sdlKeyboardInit,
    .Fini = sdlKeyboardFini,
    .Enable = sdlKeyboardEnable,
    .Disable = sdlKeyboardDisable
};

KdPointerDriver sdlMouseDriver = {
    .name = "mouse",
    .Init = sdlMouseInit,
    .Fini = sdlMouseFini,
    .Enable = sdlMouseEnable,
    .Disable = sdlMouseDisable
};


KdCardFuncs sdlFuncs = {
    .scrinit = sdlScreenInit,	/* scrinit */
    .finishInitScreen = sdlFinishInitScreen, /* finishInitScreen */
    .createRes = sdlCreateRes,	/* createRes */
};

int mouseState=0;

struct SdlGLESDriver
{
    char * buffer;
    int width;
    int height;
};



static Bool sdlScreenInit(KdScreenInfo *screen)
{
	struct SdlGLESDriver *sdlGLESDriver=calloc(1, sizeof(struct SdlGLESDriver));
    SDL_Surface * s = NULL;

#ifdef DEBUG
	printf("sdlScreenInit()\n");
#endif
    //Set resolution if not specified
	if (!screen->width || !screen->height)
	{
		screen->width = WIDTH;
		screen->height = HEIGHT;
	}

    //Check that this is a valid resolution and determine orientation
    if ( screen->width == WIDTH && screen->height == HEIGHT )
    {
        //Portrait orientation
        vertexCoords = portrait_vertexCoords;
    }
    else if ( screen->height == WIDTH && screen->width == HEIGHT )
    {
        //Keyboard on the right
        vertexCoords = land_r_vertexCoords;
    }
    else
    {
        //If this doesn't match either orientation, exit.
        //We don't support scaling/panning presently.
        return FALSE;
    }

	if (!screen->fb[0].depth)
		screen->fb[0].depth = 32;
#ifdef DEBUG
	printf("Attempting for %dx%d/%dbpp mode\n", screen->width, screen->height, screen->fb[0].depth);
#endif
	if ( SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) )
    {
        return FALSE;
    }
	s = SDL_SetVideoMode( WIDTH, HEIGHT, screen->fb[0].depth,
            SDL_OPENGLES | SDL_FULLSCREEN );
    fprintf( stderr, "SetVideoMode: %p\n", s );
	if( s == NULL )
		return FALSE;
#ifdef DEBUG
	printf("Set %dx%d/%dbpp mode\n", s->w, s->h, s->format->BitsPerPixel);
#endif

    //Create buffer for rendering into
    sdlGLESDriver->buffer = malloc( WIDTH*HEIGHT*24 / 8 );
    sdlGLESDriver->width = screen->width;
    sdlGLESDriver->height = screen->height;

	screen->fb[0].depth= 24;
	screen->fb[0].visuals=(1<<TrueColor);
	screen->fb[0].redMask=redMask;
	screen->fb[0].greenMask=greenMask;
	screen->fb[0].blueMask=blueMask;
	screen->fb[0].bitsPerPixel= 24;
	screen->rate=60;
	screen->memory_base=(CARD8 *)sdlGLESDriver->buffer;
	screen->memory_size=0;
	screen->off_screen_base=0;
	screen->driver=sdlGLESDriver;
	screen->fb[0].byteStride=(screen->width*24)/8;
	screen->fb[0].pixelStride=screen->width;
	screen->fb[0].frameBuffer=(CARD8 *)sdlGLESDriver->buffer;
	SDL_WM_SetCaption("Freedesktop.org X server (SDLGLES)", NULL);

    GL_Init();
    GL_InitTexture( sdlGLESDriver );

	return TRUE;
}

void sdlShadowUpdate (ScreenPtr pScreen, shadowBufPtr pBuf)
{
	KdScreenPriv(pScreen);
	KdScreenInfo *screen = pScreenPriv->screen;
	struct SdlGLESDriver *sdlDriver=screen->driver;
	
    GL_Render( sdlDriver );
}


void *sdlShadowWindow (ScreenPtr pScreen, CARD32 row, CARD32 offset, int mode, CARD32 *size, void *closure)
{
    fprintf( stderr, "No one calls this, right?!\n" );
    exit( -1 );
    return NULL;
	//KdScreenPriv(pScreen);
	//KdScreenInfo *screen = pScreenPriv->screen;
	//struct SdlDriver *sdlDriver=screen->driver;
	//*size=(sdlDriver->screen->w*sdlDriver->screen->format->BitsPerPixel)/8;
#ifdef DEBUG
	//printf("Shadow window()\n");
#endif
	//return (void *)((CARD8 *)sdlDriver->screen->pixels + row * (*size) + offset);
}


static Bool sdlCreateRes(ScreenPtr pScreen)
{
	KdScreenPriv(pScreen);
	KdScreenInfo *screen = pScreenPriv->screen;
	KdShadowFbAlloc(screen, 0, FALSE);
	KdShadowSet(pScreen, RR_Rotate_0, sdlShadowUpdate, sdlShadowWindow);
	return TRUE;
}

static Bool sdlFinishInitScreen(ScreenPtr pScreen)
{
	if (!shadowSetup (pScreen))
		return FALSE;
		
/*
#ifdef RANDR
	if (!sdlRandRInit (pScreen))
		return FALSE;
#endif
*/
	return TRUE;
}

static void sdlKeyboardFini(KdKeyboardInfo *ki)
{
    sdlKeyboard = NULL;
}

static Status sdlKeyboardInit(KdKeyboardInfo *ki)
{
    ki->minScanCode = 8;
    ki->maxScanCode = 255;

    sdlKeyboard = ki;

    return Success;
}

static Status sdlKeyboardEnable(KdKeyboardInfo *pi)
{
    //XXX: Is something supposed to happen here?
    return Success;
}

static void sdlKeyboardDisable(KdKeyboardInfo *pi)
{
    //XXX: Is something supposed to happen here?
}

static Status sdlMouseInit (KdPointerInfo *pi)
{
    sdlPointer = pi;
    return Success;
}

static void sdlMouseFini(KdPointerInfo *pi)
{
    sdlPointer = NULL;
}

static Status sdlMouseEnable (KdPointerInfo *pi)
{
    //XXX: Is something supposed to happen here?
    return Success;
}

static void sdlMouseDisable (KdPointerInfo *pi)
{
    //XXX: Is something supposed to happen here?
}

void InitCard(char *name)
{
	KdCardAttr attr;
        KdCardInfoAdd (&sdlFuncs, &attr, 0);
#ifdef DEBUG
	printf("InitCard: %s\n", name);
#endif
}

void InitOutput(ScreenInfo *pScreenInfo, int argc, char **argv)
{
	KdInitOutput(pScreenInfo, argc, argv);
#ifdef DEBUG
	printf("InitOutput()\n");
#endif
}

void InitInput(int argc, char **argv)
{
        KdPointerInfo *pi;
        KdKeyboardInfo *ki;

        KdAddKeyboardDriver(&sdlKeyboardDriver);
        KdAddPointerDriver(&sdlMouseDriver);
        
        ki = KdParseKeyboard("keyboard");
        KdAddKeyboard(ki);
        pi = KdParsePointer("mouse");
        KdAddPointer(pi);

        KdInitInput();
}

void ddxUseMsg(void)
{
	KdUseMsg();
}

int ddxProcessArgument(int argc, char **argv, int i)
{
	return KdProcessArgument(argc, argv, i);
}

void sdlTimer(void)
{
	static int buttonState=0;
    int keyToPass, newx, newy;
	SDL_Event event;
	SDL_ShowCursor(FALSE);
	/* get the mouse state */
	while ( SDL_PollEvent(&event) ) {
		switch (event.type) {
			case SDL_MOUSEMOTION:
                //Interpret mouse coordinates differently
                //depending on the screen orientation
                if ( vertexCoords == portrait_vertexCoords )
                {
                    newx = event.motion.x;
                    newy = event.motion.y;
                }
                else if ( vertexCoords == land_r_vertexCoords )
                {
                    newx = event.motion.y;
                    newy = WIDTH - event.motion.x;
                }
                else
                {
                    //Unexpected orientation
                    break;
                }
				KdEnqueuePointerEvent(sdlPointer, mouseState, newx, newy, 0);
				break;
			case SDL_MOUSEBUTTONDOWN:
				switch(event.button.button)
				{
					case 1:
						buttonState=KD_BUTTON_1;
						break;
					case 2:
						buttonState=KD_BUTTON_2;
						break;
					case 3:
						buttonState=KD_BUTTON_3;
						break;
				}
				mouseState|=buttonState;
				KdEnqueuePointerEvent(sdlPointer, mouseState|KD_MOUSE_DELTA, 0, 0, 0);
				break;
			case SDL_MOUSEBUTTONUP:
				switch(event.button.button)
				{
					case 1:
						buttonState=KD_BUTTON_1;
						break;
					case 2:
						buttonState=KD_BUTTON_2;
						break;
					case 3:
						buttonState=KD_BUTTON_3;
						break;
				}
				mouseState &= ~buttonState;
				KdEnqueuePointerEvent(sdlPointer, mouseState|KD_MOUSE_DELTA, 0, 0, 0);
				break;
			case SDL_KEYDOWN:
			case SDL_KEYUP:
                //We want keycodes in SDL 0->127 and 255+, but X only wants 8-255.
                //so we map 255+ to 127+ by subtracting 127
			    keyToPass = event.key.keysym.sym > 255 ? event.key.keysym.sym - 127 :
					event.key.keysym.sym;
				
			        KdEnqueueKeyboardEvent (sdlKeyboard, keyToPass,
					event.type==SDL_KEYUP);
				break;

			case SDL_QUIT:
				/* this should never happen */
				SDL_Quit();
		}
	}
}

static int xsdlInit(void)
{
#ifdef DEBUG
	printf("Calling SDL_Init()\n");
#endif
    /*
     * Moved actual SDL_Init call to directly above SDL_SetVideoMode()
     */
    return TRUE;
}


static void xsdlFini(void)
{
	SDL_Quit();
}

KdOsFuncs sdlOsFuncs={
	.Init = xsdlInit,
	.Fini = xsdlFini,
	.pollEvents = sdlTimer,
};

void OsVendorInit (void)
{
    KdOsInit (&sdlOsFuncs);
}


/*-----------------------------------------------------------------------------
 *  GL stuff
 *-----------------------------------------------------------------------------*/
void GL_Init(void)
{
    static GLbyte vShaderStr[] =  
        "attribute vec4 a_position;   \n"
        "attribute vec2 a_texCoord;   \n"
        "varying vec2 v_texCoord;     \n"
        "void main()                  \n"
        "{                            \n"
        "   gl_Position = a_position; \n"
        "   v_texCoord = a_texCoord;  \n"
        "}                            \n";

    static GLbyte fShaderStr[] =  
        "precision mediump float;                            \n"
        "varying vec2 v_texCoord;                            \n"
        "uniform sampler2D s_texture;                        \n"
        "void main()                                         \n"
        "{                                                   \n"
        "  gl_FragColor = texture2D( s_texture, v_texCoord );\n"
        "}                                                   \n";

    // setup 2D gl environment
    checkError();
    glClearColor( 0.0f, 0.0f, 0.0f, 1.0f );//black background
    checkError();

    glDisable(GL_DEPTH_TEST);
    glDepthFunc( GL_ALWAYS );
    checkError();
    glDisable(GL_CULL_FACE);
    checkError();

    // Load the shaders and get a linked program object
    programObject = esLoadProgram ( ( char *)vShaderStr, (char *)fShaderStr );
    checkError();

    // Get the attribute locations
    positionLoc = glGetAttribLocation ( programObject, "a_position" );
    checkError();
    texCoordLoc = glGetAttribLocation ( programObject, "a_texCoord" );
    checkError();

    // Get the sampler location
    samplerLoc = glGetUniformLocation ( programObject, "s_texture" );
    checkError();
}

void GL_InitTexture( struct SdlGLESDriver * driver )
{
    int num;

    //delete it if we already have one
    if ( texture )
    {
        glDeleteTextures( 1, &texture );
        texture = 0;
    }

    glGenTextures(1, &texture);
    checkError();
    glBindTexture(GL_TEXTURE_2D, texture);
    checkError();
    
    //sanity check
    glGetIntegerv( GL_TEXTURE_BINDING_2D, &num );
    assert( num == texture );
    glGetIntegerv( GL_ACTIVE_TEXTURE, &num );
    assert( num == GL_TEXTURE0 );
    checkError();

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter );
    checkError();
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter );
    checkError();

    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
    checkError();
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
    checkError();

    glTexImage2D( GL_TEXTURE_2D, 0, GL_RGB, driver->width, driver->height,
            0, GL_RGB, GL_UNSIGNED_BYTE, NULL );
    checkError();
}

void GL_Render( struct SdlGLESDriver * driver )
{
    //Draw the buffer to the screen
    glClear( GL_COLOR_BUFFER_BIT );
    checkError();

    glUseProgram ( programObject );
    checkError();

    glVertexAttribPointer( positionLoc, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), vertexCoords );
    checkError();
    glVertexAttribPointer( texCoordLoc, 2, GL_FLOAT, GL_FALSE, 2*sizeof(GLfloat), texCoords );

    checkError();

    glEnableVertexAttribArray( positionLoc );
    checkError();
    glEnableVertexAttribArray( texCoordLoc );
    checkError();

    glBindTexture(GL_TEXTURE_2D, texture);
    checkError();

    //Upload buffer to texture
    glTexSubImage2D( GL_TEXTURE_2D,0,
            0,0, driver->width, driver->height,
            GL_RGB,GL_UNSIGNED_BYTE,driver->buffer );
    checkError();

    glUniform1i( samplerLoc, 0 );
    checkError();

    glDrawElements( GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices );
    checkError();

    //Push to screen
    SDL_GL_SwapBuffers();
    checkError();

    return;
}
