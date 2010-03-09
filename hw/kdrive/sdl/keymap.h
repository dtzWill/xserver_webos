/*
 * ===========================================================================
 *
 *       Filename:  keymap.h
 *
 *    Description:  Converts SDL keys to their X equivalent on the default
 *                  keymap.  Really I should just knuckle down and write
 *                  a proper keymap for sdl...
 *
 *        Version:  1.0
 *        Created:  03/08/2010 09:14:45 PM
 *
 *         Author:  Will Dietz (WD), w@wdtz.org
 *        Company:  dtzTech
 *
 * ===========================================================================
 */

#include <SDL/SDL.h>

#ifndef _SDL_KEYMAP_H_
#define _SDL_KEYMAP_H_

//Array for converting SDLK_* to alpha more easily
static int sdlToX_Alpha[] =
{
38 ,//= a A a A
56 ,//= b B b B
54 ,//= c C c C
40 ,//= d D d D
26 ,//= e E e E
41 ,//= f F f F
42 ,//= g G g G
43 ,//= h H h H
31 ,//= i I i I
44 ,//= j J j J
45 ,//= k K k K
46 ,//= l L l L
58 ,//= m M m M
57 ,//= n N n N
32 ,//= o O o O
33 ,//= p P p P
24 ,//= q Q q Q
27 ,//= r R r R
39 ,//= s S s S
28 ,//= t T t T
30 ,//= u U u U
55 ,//= v V v V
25 ,//= w W w W
53 ,//= x X x X
29 ,//= y Y y Y
52 ,//= z Z z Z
-1
};

//XXX
//XXX This should really be implemented with the HELP of xkb, not fighting it!
//XXX

static int sdlSymForKeyEvent( SDL_KeyboardEvent * event )
{
    //State:
    static int shift_down = 0;

    int sym = event->keysym.sym;
    int ret = 0;

    //If this is an alpha key...
    if ( sym >= SDLK_a && sym <= SDLK_z )
    {
        int offset = sym - SDLK_a;
        ret = sdlToX_Alpha[offset];
    }

    //XXX: Model shift and whatever other modifiers
    else if  ( sym == SDLK_LSHIFT || sym == SDLK_RSHIFT )
    {
        ret = 50;//LShift
    }

    else if ( sym == SDLK_BACKSPACE )
    {
        ret = 22;//backspace
    }

    //XXX: Model special characters
    else if ( sym == SDLK_RETURN )
    {
        ret = 36;
    }

    else if ( sym == SDLK_SPACE )
    {
        ret = 65;
    }

    else if ( sym == SDLK_PERIOD )
    {
        ret = 60;
    }

    else if ( sym == SDLK_COMMA )
    {
        ret = 59;
    }

    //XXX: Localization dependent?

    return ret;
}


#endif //_SDL_KEYMAP_H_
