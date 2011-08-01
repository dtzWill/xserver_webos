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
#include "fbdev.h"

static int screen_width = -1, screen_height = -1;
static int effective_screen_height = -1;

int use_keyboard = 1;

#define PORTRAIT_KEYBOARD_OFFSET 250
#define LANDSCAPE_KEYBOARD_OFFSET 250

static const int TOUCHPAD = 1;

typedef struct
{
  long x1, x2, y1, y2;
} UpdateRect_t;

// #define BLIT_FULL_TEXTURE

//XXX: include <pdl.h> ?
extern void PDL_SetOrientation( int orientation );
extern void PDL_Init( char unused );
extern void PDL_SetKeyboardState(int visible);
extern int  PDL_GetPDKVersion(void);

 /* the action button below the screen */
#define PDL_ORIENTATION_0 0
/* the action button to the left of the screen */
#define PDL_ORIENTATION_90 1
/* the action button above the screen */
#define PDL_ORIENTATION_180 2
/* the action button to the right of the screen */
#define PDL_ORIENTATION_270 3

//#define DEBUG_GL
//#define DEBUG

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

#ifdef DEBUG
#define dprintf(fmt, args...) \
  do { fprintf(stderr, "Xsdl: " fmt, ##args); } while(0)
#else
#define dprintf(fmt, args...) \
  do { } while(0)
#endif

struct SdlGLESDriver;

void GL_Init(void);
void GL_InitTexture( struct SdlGLESDriver * driver );
void GL_Render( struct SdlGLESDriver * driver, UpdateRect_t U );

void detectOrientation(void);
Bool updateOrientation(int width, int height);

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

// Landscape, with home button on the right
float orientation_0_vertexCoords[] =
{
    -1, 1,
    -1, -1,
    1, 1,
    1, -1
};

// Landscape, with home button on the left
float orientation_180_vertexCoords[] =
{
    1, -1,
    1, 1,
    -1, -1,
    -1, 1
};

// Portrait, with home button at the bottom
float orientation_90_vertexCoords[] =
{
    -1, -1,
    1, -1,
    -1, 1,
    1, 1
};
// Portrait, with home button at the top
float orientation_270_vertexCoords[] =
{
    1, 1,
    -1, 1,
    1, -1,
    -1, -1
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

int deviceOrientation = 0; // 0, 90, 180, 270

GLushort indices[] = { 0, 1, 2, 1, 2, 3 };

//We're using 24 bitdepth, each color has it's own byte
//Note that the screen still is 32bit, but we use 24bit so we can upload
//as a texture more easily (no reason to introduce alpha)
int redMask = 0x0000ff;
int greenMask = 0x00ff00;
int blueMask = 0xff0000;

static void xsdlFini(void);
static Bool sdlScreenInit(KdScreenInfo *screen);

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

KdCardFuncs	sdlFuncs = {
    fbdevCardInit,	    /* cardinit */
    sdlScreenInit,	    /* scrinit */
    fbdevInitScreen,	    /* initScreen */
    fbdevFinishInitScreen,  /* finishInitScreen */
    fbdevCreateResources,   /* createRes */
    fbdevPreserve,	    /* preserve */
    fbdevEnable,	    /* enable */
    fbdevDPMS,		    /* dpms */
    fbdevDisable,	    /* disable */
    fbdevRestore,	    /* restore */
    fbdevScreenFini,	    /* scrfini */
    fbdevCardFini,	    /* cardfini */

    0,			    /* initCursor */
    0,			    /* enableCursor */
    0,			    /* disableCursor */
    0,			    /* finiCursor */
    0,			    /* recolorCursor */

    0,			    /* initAccel */
    0,			    /* enableAccel */
    0,			    /* disableAccel */
    0,			    /* finiAccel */

    fbdevGetColors,    	    /* getColors */
    fbdevPutColors,	    /* putColors */
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
  int pdkVersion;

  dprintf("sdlScreenInit()\n");

  // Override what the user said the resolution should be...
  screen->width = 0;
  screen->height = 0;

  if (!screen->fb.depth)
    screen->fb.depth = 32;
  dprintf("Attempting for %dx%d/%dbpp mode\n", screen->width, screen->height, screen->fb.depth);

  dprintf("Calling PDL_Init...\n");
  PDL_Init(0);

  dprintf("Calling SDL_Init...\n");
  if ( SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_JOYSTICK ) )
  {
    return FALSE;
  }

  if (TOUCHPAD)
    SDL_EnableUNICODE( SDL_ENABLE );

  dprintf("Calling SDL_SetVideoMode...\n");
  s = SDL_SetVideoMode( screen->width, screen->height, screen->fb.depth, SDL_ANYFORMAT );

  dprintf("SetVideoMode: %p\n", s );
  if( s == NULL )
    return FALSE;

  dprintf( "Set %dx%d/%dbpp mode\n", s->w, s->h, s->format->BitsPerPixel );

  // Sanity check the dimensions
  if ( s->w <= 0 || s->h <= 0 )
    return FALSE;

  // Force orientation, framebuffer doesn't
  // play nice with non-default orientations.
  // (They _work_, it's just much slower)
  deviceOrientation = 0;

  // Don't show the keyboard for devices with physical keyboards
  pdkVersion = PDL_GetPDKVersion();
  dprintf("PDK version: %d\n", pdkVersion);
  if (pdkVersion <= 300) {
    use_keyboard = 0;
    deviceOrientation = 0;
  }

  // Update our state using the detected orientation
  if (!updateOrientation(s->w, s->h))
    return FALSE;

  // Only call PDL_SetKeyboardState if we want the keyboard
  if (use_keyboard) {
    dprintf("PDL_SetKeyboardState %d\n", use_keyboard );
    PDL_SetKeyboardState( use_keyboard );
  }

  // Forward to fbdev's impl
  return fbdevScreenInit(screen);
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
  KdCardInfoAdd (&sdlFuncs, 0);
  dprintf("InitCard: %s\n", name);
}

void InitOutput(ScreenInfo *pScreenInfo, int argc, char **argv)
{
	KdInitOutput(pScreenInfo, argc, argv);
	dprintf("InitOutput()\n");
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

void
CloseInput (void)
{
}

void ddxUseMsg(void)
{
	KdUseMsg();
}

int ddxProcessArgument(int argc, char **argv, int i)
{
  fbdevDevicePath = "/dev/fb1";

	return KdProcessArgument(argc, argv, i);
}

void sdlTimer(void)
{
  static int buttonState=0;
  int keyToPass;
  SDL_Event event;
  SDL_ShowCursor(FALSE);

  /* get the mouse state */
  while ( SDL_PollEvent(&event) ) {

    // If we haven't initialized the screen yet, nothing to do here.
    // We still loop through the PollEvents (calling PumpEvents internally)
    // to keep things chugging along.
    if (screen_width <= 0 || screen_height <= 0)
      continue;

    switch (event.type) {
      case SDL_MOUSEMOTION:
        switch (deviceOrientation) {
          case 0:
            KdEnqueuePointerEvent(sdlPointer, mouseState,
                event.motion.x, event.motion.y, 0);
            break;
          case 90:
            KdEnqueuePointerEvent(sdlPointer, mouseState,
                screen_width - event.motion.y, event.motion.x, 0);
            break;
          case 180:
            KdEnqueuePointerEvent(sdlPointer, mouseState,
                screen_width - event.motion.x, screen_height - event.motion.y, 0);
            break;
          case 270:
            KdEnqueuePointerEvent(sdlPointer, mouseState,
                event.motion.y, screen_height - event.motion.x, 0);
            break;
          default:
            /* Do nothing */
            break;
        }
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

        if (TOUCHPAD)
        {
          // On touchpad, send the raw unicode value.
          keyToPass = event.key.keysym.unicode;

          // '1' is outside valid range of 8->255,
          // remap to 255.
          if (keyToPass == 1) // TAB
            keyToPass = 255;
        }
        else
        {
          //We want keycodes in SDL 0->127 and 255+, but X only wants 8-255.
          //so we map 255+ to 127+ by subtracting 127
          keyToPass = event.key.keysym.sym > 255 ? event.key.keysym.sym - 127 :
            event.key.keysym.sym;

        }
        dprintf("KEY(%d): %d -> %d\n",
            event.type == SDL_KEYDOWN,
            event.key.keysym.sym, keyToPass);

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
	dprintf("Calling SDL_Init()\n");
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

    // Yes, use this one.
    glUseProgram ( programObject );
    checkError();

    // Set sampler
    glUniform1i( samplerLoc, 0 );
    checkError();

}

Bool updateOrientation(int width, int height)
{
  switch (deviceOrientation) {
  case 0:
    dprintf("Orientation 0\n");
    screen_width  = width;
    screen_height = height;
    vertexCoords = orientation_0_vertexCoords;
    PDL_SetOrientation(PDL_ORIENTATION_0);
    break;
  case 90:
    dprintf("Orientation 90\n");
    screen_width  = height;
    screen_height = width;
    vertexCoords = orientation_90_vertexCoords;
    PDL_SetOrientation(PDL_ORIENTATION_90);
    break;
  case 180:
    dprintf("Orientation 180\n");
    screen_width  = width;
    screen_height = height;
    vertexCoords = orientation_180_vertexCoords;
    PDL_SetOrientation(PDL_ORIENTATION_180);
    break;
  case 270:
    dprintf("Orientation 270\n");
    screen_width  = height;
    screen_height = width;
    vertexCoords = orientation_270_vertexCoords;
    PDL_SetOrientation(PDL_ORIENTATION_270);
    break;
  default:
    fprintf( stderr, "Invalid deviceOrientation!\n" );
    return FALSE;
  }

  effective_screen_height = screen_height;

  if (use_keyboard)
  {
    // Change _effective_ height to accomodate keyboard
    if (deviceOrientation % 180)
      effective_screen_height -= PORTRAIT_KEYBOARD_OFFSET;
    else
      effective_screen_height -= LANDSCAPE_KEYBOARD_OFFSET;
  }

  return TRUE;
}
