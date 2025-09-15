 
#ifndef _H_LINUX_CARROT_
#define _H_LINUX_CARROT_ 

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xos.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <sys/time.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <fstream>
#include <vector>
#include <cctype>

#include "renderer.h"
#include "strings.h"
#include "document.h"
#include "imgui.h"

struct WindowData {
    Display* display;
    Window window;
    GC gc;
    XImage* backBuffer;
    unsigned int* pixels;
    int screen;
    int width;
    int height;
    bool closeWindow;
};


#endif