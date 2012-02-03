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

#ifdef RANDR
#include "randrstr.h"
#endif

#include <SDL/SDL_video.h>
#include <SDL/SDL_opengles.h>
#include "esFunc.h"

static int screen_width = -1, screen_height = -1;
static int effective_screen_height = -1;

// Default keyboard size when nothing specified with -vkb.
static int keyboard_type = 0;

// Whether or not the keyboard is presently shown.
static int keyboard_active = 0;

// (Maximum) number of fingers pressed during a mouse event
static int num_fingers_down = 0;

typedef struct
{
  char * name;
  int portrait_offset;
  int landscape_offset;
} kbd_t;

// Keyboard sizes from
// https://developer.palm.com/distribution/viewtopic.php?p=83407#p83407
static kbd_t kbd_types[] =
{
  { "off",   0,   0},
  { "xs",  243, 243},
  { "s",   291, 291},
  { "m",   340, 340},
  { "l",   393, 393},
};

static const int MAX_KEYBOARD_TYPE = sizeof(kbd_types) / sizeof(kbd_types[0]);

// Distance, in pixels, required to distinguish a tap from a drag.
// This is non-zero due to noise from human 'error' and perhaps the screen also
static const int DRAG_THRESHOLD = 10;

// Values from https://developer.palm.com/content/api/reference/pdk/pdl/pdl-get-hardware-id.html
// HARDWARE_TOUCHPAD_GO isn't mentioned in the PDL_GetHardwareID spec, but here:
// https://developer.palm.com/content/api/reference/pdk/pdl-data-types.html
typedef enum {
HARDWARE_UNKNOWN     =  -1,
HARDWARE_PRE         = 101,
HARDWARE_PRE_PLUS    = 102,
HARDWARE_PIXI        = 201,
HARDWARE_VEER        = 301,
HARDWARE_PRE_2       = 401,
HARDWARE_PRE_3       = 501,
HARDWARE_TOUCHPAD    = 601,
HARDWARE_TOUCHPAD_GO = 701
} Hardware_t;

static int UseUnicode = 0;

#define HP_BT_LEFT 18
#define HP_BT_UP 19
#define HP_BT_RIGHT 20
#define HP_BT_DOWN 21

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
extern int  PDL_GetHardwareID(void);

 /* the action button below the screen */
#define PDL_ORIENTATION_0 0
/* the action button to the left of the screen */
#define PDL_ORIENTATION_90 1
/* the action button above the screen */
#define PDL_ORIENTATION_180 2
/* the action button to the right of the screen */
#define PDL_ORIENTATION_270 3

// DISMISS keyboard event, introduced in 3.0.2
// ( https://developer.palm.com/content/api/release-notes/3.0.2-652.html )
#define PDLK_GESTURE_DISMISS_KEYBOARD 24
// Experimentally determined on a device running 3.0.4
#define PDLK_VKB_EURO 8364

//#define DEBUG_GL
//#define DEBUG

#ifdef DEBUG_GL
static void checkError(void)
{
    char * sdl_error;

    /* Check for error conditions. */
    GLenum gl_error = glGetError( );

    if( gl_error != GL_NO_ERROR ) {
        fprintf( stderr, "X sdlgl: OpenGL error: %x\n", gl_error );
        while(1);
        exit( 1 );
    }

    sdl_error = SDL_GetError( );

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
void configureForHardware(void);
int handleSpecialKeys(SDLKey key, int def);
void mouseEvent(int x, int y, int state);

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

// 32bit RGBA
int redMask   = 0x000000ff;
int greenMask = 0x0000ff00;
int blueMask  = 0x00ff0000;

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
    Rotation randr;
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

  configureForHardware();

  if (UseUnicode)
    SDL_EnableUNICODE( SDL_ENABLE );

  dprintf("Calling SDL_SetVideoMode...\n");
  s = SDL_SetVideoMode( screen->width, screen->height, screen->fb.depth,
      SDL_OPENGLES | SDL_FULLSCREEN );
  dprintf("SetVideoMode: %p\n", s );
  if( s == NULL )
    return FALSE;

  dprintf( "Set %dx%d/%dbpp mode\n", s->w, s->h, s->format->BitsPerPixel );

  // Sanity check the dimensions
  if ( s->w <= 0 || s->h <= 0 )
    return FALSE;

  // Figure out our current orientation
  detectOrientation();

  // Don't show the keyboard for devices with physical keyboards
  pdkVersion = PDL_GetPDKVersion();
  dprintf("PDK version: %d\n", pdkVersion);
  if (pdkVersion <= 300) {
    keyboard_type = 0;
    deviceOrientation = 0;
  }

  // Update our state using the detected orientation
  if (!updateOrientation(s->w, s->h))
    return FALSE;

  dprintf("keyboard_type: %d\n", keyboard_type);

  // Show keyboard as indicated by keyboard_type
  keyboard_active = keyboard_type != 0;
  PDL_SetKeyboardState(keyboard_active);

  //Create buffer for rendering into
  sdlGLESDriver->buffer = malloc(screen_width * screen_height * 32 / 8);
  sdlGLESDriver->width = screen_width;
  sdlGLESDriver->height = screen_height;

  screen->width = screen_width;
  screen->height = effective_screen_height;
  screen->fb.depth = 24;
  screen->fb.visuals=(1<<TrueColor);
  screen->fb.redMask=redMask;
  screen->fb.greenMask=greenMask;
  screen->fb.blueMask=blueMask;
  screen->fb.bitsPerPixel= 32;
  screen->rate=60;
  screen->driver=sdlGLESDriver;
  screen->fb.byteStride=(screen_width*32)/8;
  screen->fb.pixelStride=screen_width;
  screen->fb.frameBuffer=(CARD8 *)sdlGLESDriver->buffer;
  SDL_WM_SetCaption("Freedesktop.org X server (SDLGLES)", NULL);

  sdlGLESDriver->randr = screen->randr;

  GL_Init();
  GL_InitTexture( sdlGLESDriver );

  return TRUE;
}

void sdlShadowUpdate (ScreenPtr pScreen, shadowBufPtr pBuf)
{
  KdScreenPriv(pScreen);
  KdScreenInfo *screen = pScreenPriv->screen;
  struct SdlGLESDriver *sdlDriver=screen->driver;

  RegionPtr		damage = shadowDamage(pBuf);
  BoxPtr		pExtents = REGION_EXTENTS(pScreen, damage);

  // Find what was changed, and update that to the screen
  UpdateRect_t U;
  U.x1 = pExtents->x1;
  U.x2 = pExtents->x2;
  U.y1 = pExtents->y1;
  U.y2 = pExtents->y2;

  GL_Render( sdlDriver, U );
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
	//dprintf("Shadow window()\n");
	//return (void *)((CARD8 *)sdlDriver->screen->pixels + row * (*size) + offset);
}


static Bool sdlCreateRes(ScreenPtr pScreen)
{
	KdScreenPriv(pScreen);
	KdScreenInfo *screen = pScreenPriv->screen;
	KdShadowFbAlloc(screen, 0);
	KdShadowSet(pScreen, RR_Rotate_0, sdlShadowUpdate, sdlShadowWindow);
	return TRUE;
}


#ifdef RANDR
static Bool sdlRandRGetInfo(ScreenPtr pScreen, Rotation *rotations)
{
  KdScreenPriv(pScreen);
  KdScreenInfo *screen = pScreenPriv->screen;
  struct SdlGLESDriver *scrpriv = screen->driver;
  RRScreenSizePtr pSize;
  Rotation randr;
  int n;

  // TODO: RR_Rotate_All|RR_Reflect_All;
  //       For now, we don't suport any rotations/reflections
  *rotations = RR_Rotate_0;

  for (n = 0; n < pScreen->numDepths; n++)
    if (pScreen->allowedDepths[n].numVids)
      break;
  if (n == pScreen->numDepths)
    return FALSE;

  pSize = RRRegisterSize (pScreen,
      screen->width,
      screen->height,
      screen->width_mm,
      screen->height_mm);

  randr = KdSubRotation (scrpriv->randr, screen->randr);

  RRSetCurrentConfig (pScreen, randr, 0, pSize);

  return TRUE;
}

static Bool sdlRandRInit(ScreenPtr pScreen)
{
  rrScrPrivPtr    pScrPriv;

  if (!RRScreenInit (pScreen))
    return FALSE;

  pScrPriv = rrGetScrPriv(pScreen);
  pScrPriv->rrGetInfo = sdlRandRGetInfo;

  // TODO: For now, we don't support setting anything.
  //pScrPriv->rrSetConfig = sdlRandRSetConfig;
  return TRUE;
}
#endif


static Bool sdlFinishInitScreen(ScreenPtr pScreen)
{
  if (!shadowSetup (pScreen))
    return FALSE;


#ifdef RANDR
  if (!sdlRandRInit (pScreen))
    return FALSE;
#endif

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
  ErrorF("\nXsdl Device Usage (webOS):\n");
  ErrorF("-vkb vkb_type    What type of virtual keyboard to use (TP only).  Defaults to 'xs'.\n");
  ErrorF("                 Valid values for -vkb:  off,xs,s,m,l\n");
  ErrorF("\n");
}

int ddxProcessArgument(int argc, char **argv, int i)
{
  int j;
  if (!strcmp(argv[i], "-vkb"))
  {
    keyboard_type = -1;

    if (i + 1 < argc)
    {
      for(j = 0; j < MAX_KEYBOARD_TYPE; ++j)
      {
        if (!strcmp(argv[i+1], kbd_types[j].name))
        {
          keyboard_type = j;
          return 2;
        }
      }
    }

    // If invalid type, or not enough arguments...
    if (keyboard_type == -1)
    {
      UseMsg();
      exit(1);
    }
  }

	return KdProcessArgument(argc, argv, i);
}

void sdlTimer(void)
{
  // Mouse state
  // Are we treating this as a 'drag' event?
  static int clickDrag = 0;
  // The location of the first finger down for a mouse event
  static int startX = 0, startY = 0;
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

        // Ignore motion events with more than 1 finger down
        if (num_fingers_down != 1)
          break;

        // Also, only consider motion from the first finger
        // (TODO: When can this occur?)
        if (event.motion.which != 0)
          break;

        // Drag handling:
        {
          int dragDiffX = event.motion.x-startX;
          int dragDiffY = event.motion.y-startY;

          // We're in the 'drag' state if we've exceeded the DRAG_THRESHOLD
          if (!clickDrag &&
              (dragDiffX*dragDiffX) + (dragDiffY*dragDiffY) > DRAG_THRESHOLD*DRAG_THRESHOLD) {
            // We've just transitioned from 'waiting to see what happens'
            // post-mousedown state into 'drag' mode.
            // Accordingly, generated the delayed mousedown event
            // This is important to have drag include original coordinates!
            mouseEvent(startX, startY, mouseState);
            clickDrag = 1;
          }

          // Regardless or the above, if we're now in drag mode, move mouse
          // to its new location using the existing mouseState.
          if (clickDrag)
            mouseEvent(event.motion.x, event.motion.y, mouseState);
        }
        break;

      //Button down only sets the mouseState but does not trigger mouse down action.
      case SDL_MOUSEBUTTONDOWN:

        // Ignore additional buttons down once dragging
        if (clickDrag) break;

        num_fingers_down = event.button.which + 1;

        switch(num_fingers_down)
        {
        /*Left click */
          case 1:
            // Log location of mousedown, for determining when the finger has been
            // moved far enough to count as a drag event (versus tap)
            startX = event.button.x;
            startY = event.button.y;

            mouseState = 1;
            break;
        /*Right click*/
          case 2:
            mouseState = 4;
            break;
        /* Toggle Virtual Keyboard */
           case 3:
            keyboard_active = !keyboard_active;
            PDL_SetKeyboardState(keyboard_active);

            // Switch to 'no fingers down' state
            // to avoid firing mouse-related events from this
            num_fingers_down = 0;
            break;
          default:
            break;
        }

        break;

      // If not a drag event, emulates a tap (mousedown/mouseup)
      case SDL_MOUSEBUTTONUP:
        // Only fire for finger up based on num_fingers_down value
        // (Only consider the 'up' event for the last finger pressed down,
        // and ignore up events from previous fingers)
        if(event.button.which != num_fingers_down - 1)
          break;

        // Generate mouse-down event at original location if not in drag mode
        if (!clickDrag)
          mouseEvent(startX, startY, mouseState);

        //Set mousestate to 0 which means no buttons down (aka release all)
        mouseState = 0;
        KdEnqueuePointerEvent(sdlPointer, mouseState|KD_MOUSE_DELTA, 0, 0, 0);

        // Reset mouse state, we're done
        num_fingers_down = 0;
        clickDrag = 0;
        break;
      case SDL_KEYDOWN:
      case SDL_KEYUP:

        if (UseUnicode)
        {
          // On touchpad, send the raw unicode value.
          keyToPass = event.key.keysym.unicode;

          // Two notes.
          // 1)I have no idea why 'tab' ever mapped to unicode '1'.
          // 2)This should be unnecessary now since we check keysym.sym in
          //   handleSpecialKeys, however until I verify this new code still
          //   works on a 3.0.2 device I'm leaving this be.  The loss is that
          //   users cannot enter 'Start of Heading' characters, which I
          //   think is an acceptable risk :).
          if (keyToPass == 1) // TAB
            keyToPass = 255;

          keyToPass = handleSpecialKeys(event.key.keysym.sym, keyToPass);
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
        break;
      case SDL_ACTIVEEVENT:
        // Anytime the app gets focus, make sure we re-set the keyboard state.
        // Hopefully this fixes issues caused by device sleeping causing keyboard
        // to disappear.
        if (keyboard_type != 0) {
          PDL_SetKeyboardState( 1 );
        }

        // Noisy for now, but should help debugging JIC this doesn't fix it.
        fprintf(stderr, "Xsdl: Active event!\n");
        break;
      default:
        break;
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
        "  vec4 color = texture2D( s_texture, v_texCoord );  \n"
        "  gl_FragColor.rgb = color.rgb;                     \n"
        "  gl_FragColor.a = 1.0;                             \n"
        "}                                                   \n";

    // setup 2D gl environment
    checkError();
    glClearColor( 0.0f, 0.0f, 0.0f, 1.0f );//black background
    checkError();

    glDisable(GL_STENCIL_TEST);
    checkError();
    glDisable(GL_DEPTH_TEST);
    checkError();
    glDepthFunc( GL_ALWAYS );
    checkError();
    glDisable(GL_CULL_FACE);
    checkError();
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_BLEND);
    checkError();

    // Load the shaders and get a linked program object
    programObject = esLoadProgram ( ( char *)vShaderStr, (char *)fShaderStr );
    checkError();
    assert(programObject && "load program failed!");

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

    glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, driver->width, driver->height,
            0, GL_RGBA, GL_UNSIGNED_BYTE, NULL );
    checkError();
}

void GL_Render( struct SdlGLESDriver * driver, UpdateRect_t U )
{
    char * buf = NULL;

    dprintf( "UPDATE: x1: %ld, x2: %ld, y1: %ld, y2: %ld\n",
        U.x1, U.x2, U.y1, U.y2 );

    //Draw the buffer to the screen
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
#ifdef BLIT_FULL_TEXTURE
    glTexSubImage2D( GL_TEXTURE_2D,0,
            0,0, driver->width, driver->height,
            GL_RGB,GL_UNSIGNED_BYTE,driver->buffer );
#else
    // Only update the modified *lines*
    // We'd only send the modified rect, but that requires repacking.
    // Which is
    // a)more complicated O:)
    // b)potentially wasteful in terms of CPU/memory copying
    // c)I'm unclear on what the lifetime of the temporary packed data would be

    // Informally, just sending updated lines is already much faster.
    buf = driver->buffer + U.y1 * driver->width * 4;
    glTexSubImage2D( GL_TEXTURE_2D, 0,
            0, U.y1, driver->width, U.y2 - U.y1,
            GL_RGBA, GL_UNSIGNED_BYTE, buf );
#endif
    checkError();

    glDrawElements( GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices );
    checkError();

    //Push to screen
    SDL_GL_SwapBuffers();
    checkError();

    return;
}

void detectOrientation(void)
{
  SDL_Joystick *joystick;
  Sint16 xAxis, yAxis, zAxis;

  // Read the current accellerometer values
  joystick = SDL_JoystickOpen(0);

  SDL_PumpEvents();
  xAxis = SDL_JoystickGetAxis(joystick, 0);
  yAxis = SDL_JoystickGetAxis(joystick, 1);
  zAxis = SDL_JoystickGetAxis(joystick, 2);
  dprintf("Sample orientation: %d %d %d\n", xAxis, yAxis, zAxis);
  SDL_JoystickClose(joystick);

  // Convert it into a device orientation using some heuristics
  if ((xAxis < -10000) && (yAxis > -25000) && (yAxis < 25000)) {
    deviceOrientation = 0;
  }
  else if ((yAxis > 10000) && (xAxis > -25000) && (xAxis < 25000)) {
    deviceOrientation = 90;
  }
  else if ((xAxis > 10000) && (yAxis > -25000) && (yAxis < 25000)) {
    deviceOrientation = 180;
  }
  else if ((yAxis < -10000) && (xAxis > -25000) && (xAxis < 25000)) {
    deviceOrientation = 270;
  }
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

  // Change _effective_ height to accomodate keyboard
  assert(keyboard_type >= 0 && keyboard_type < MAX_KEYBOARD_TYPE);
  if (deviceOrientation % 180)
    effective_screen_height -= kbd_types[keyboard_type].portrait_offset;
  else
    effective_screen_height -= kbd_types[keyboard_type].landscape_offset;

  return TRUE;
}

void configureForHardware(void)
{

  // Check if the hardware is a touchpad.
  // If so, switch to unicode encoding of keyboard events,
  // because (for example) we don't get SDL events for shift keys
  // and the like, so the keyboard we're presenting doesn't have
  // the usual (normal keys)x(modifiers) configuration.

  // This method should work on other devices as well for the most part,
  // however all 'special' efforts in the mappings such as support
  // for sticky modifiers, arrows, etc, _won't_ work, so for now
  // we use this method just for the touchpad.

  int pdkVersion = PDL_GetPDKVersion();
  // PDL_GetHardwareID only exists on PDK version >= 200
  if (pdkVersion >= 200) {
    Hardware_t H = PDL_GetHardwareID();
    if ((H == HARDWARE_TOUCHPAD) || (H == HARDWARE_TOUCHPAD_GO))
      UseUnicode = 1;
  }

  dprintf("UseUnicode: %d\n", 1);
}

int handleSpecialKeys(SDLKey key, int def)
{
  // Special-case misc keys:
  switch (key)
  {
    case SDLK_TAB:
      return 255;
    case PDLK_GESTURE_DISMISS_KEYBOARD:
    case SDLK_LCTRL:
    case SDLK_RCTRL:
      return 254;
    case SDLK_LALT:
    case SDLK_RALT:
      return 253;
    case HP_BT_LEFT:
    case SDLK_LEFT:
      return 252;
    case HP_BT_UP:
    case SDLK_UP:
      return 251;
    case HP_BT_RIGHT:
    case SDLK_RIGHT:
      return 250;
    case HP_BT_DOWN:
    case SDLK_DOWN:
      return 249;
    case PDLK_VKB_EURO:
      return 248;
    default:
      return def;
  }
}

void mouseEvent(int x, int y, int state) {
  switch (deviceOrientation) {
    case 0:
      KdEnqueuePointerEvent(sdlPointer, state,
          x, y, 0);
      break;
    case 90:
      KdEnqueuePointerEvent(sdlPointer, state,
          screen_width - y, x, 0);
      break;
    case 180:
      KdEnqueuePointerEvent(sdlPointer, state,
          screen_width - x, screen_height - y, 0);
      break;
    case 270:
      KdEnqueuePointerEvent(sdlPointer, state,
          y, screen_height - x, 0);
      break;
    default:
      // Do nothing
      break;
  }

}
