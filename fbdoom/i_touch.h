//
// Copyright(C) 2026 wmDOOM contributors
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//      Touchscreen input and on-screen controller UI for Sony Walkman.
//

#ifndef __I_TOUCH__
#define __I_TOUCH__

#include <linux/fb.h>

void I_InitTouch(int screen_w, int screen_h);
void I_GetTouchEvent(void);
void I_DrawTouchOverlay(unsigned char *screen_base, int line_length,
                        int fb_w, int fb_h, int bpp,
                        const struct fb_var_screeninfo *vinfo, int buf_idx);

#endif
