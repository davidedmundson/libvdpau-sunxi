/*
 * Copyright (c) 2013-2015 Jens Kuske <jenskuske@gmail.com>
 * Copyright (c) 2018 David Edmundson <davidedmundson@kde.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <fcntl.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "vdpau_private.h"
#include "sunxi_disp.h"
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <memory.h>
 
#include  <math.h>
#include  <sys/time.h>
 
#include  <X11/Xlib.h>
#include  <X11/Xatom.h>
#include  <X11/Xutil.h>
 
#include  <GLES2/gl2.h>
#include  <EGL/egl.h>
 

struct sunxi_drm_private
{
	struct sunxi_disp pub;

	int fd;
    int ctrl_fd;
    int plane_id;
	int video_layer;
	int osd_layer;
};

void fatal(char *str)
{
	fprintf(stderr, "%s\n", str);
	exit(EXIT_FAILURE);
}

static void sunxi_disp_close(struct sunxi_disp *sunxi_disp);
static int sunxi_disp_set_video_layer(struct sunxi_disp *sunxi_disp, int x, int y, int width, int height, output_surface_ctx_t *surface);
static void sunxi_disp_close_video_layer(struct sunxi_disp *sunxi_disp);
static int sunxi_disp_set_osd_layer(struct sunxi_disp *sunxi_disp, int x, int y, int width, int height, output_surface_ctx_t *surface);
static void sunxi_disp_close_osd_layer(struct sunxi_disp *sunxi_disp);



const char vertex_src [] =
"                                        \
   attribute vec4        position;       \
   varying mediump vec2  pos;            \
   uniform vec4          offset;         \
                                         \
   void main()                           \
   {                                     \
      gl_Position = position + offset;   \
      pos = position.xy;                 \
   }                                     \
";
 
 
const char fragment_src [] =
"                                                      \
   varying mediump vec2    pos;                        \
   uniform mediump float   phase;                      \
                                                       \
   void  main()                                        \
   {                                                   \
      gl_FragColor  =  vec4( 1., 0.9, 0.7, 1.0 ) *     \
        cos( 30.*sqrt(pos.x*pos.x + 1.5*pos.y*pos.y)   \
             + atan(pos.y,pos.x) - phase );            \
   }                                                   \
";
//  some more formulas to play with...
//      cos( 20.*(pos.x*pos.x + pos.y*pos.y) - phase );
//      cos( 20.*sqrt(pos.x*pos.x + pos.y*pos.y) + atan(pos.y,pos.x) - phase );
//      cos( 30.*sqrt(pos.x*pos.x + 1.5*pos.y*pos.y - 1.8*pos.x*pos.y*pos.y)
//            + atan(pos.y,pos.x) - phase );
 
 
void
print_shader_info_log (
   GLuint  shader      // handle to the shader
)
{
   GLint  length;
 
   glGetShaderiv ( shader , GL_INFO_LOG_LENGTH , &length );
 
   if ( length ) {
      char* buffer = malloc(sizeof(char)* length);
      glGetShaderInfoLog ( shader , length , NULL , buffer );
//       cout << "shader info: " <<  buffer << flush;
      free(buffer);
 
      GLint success;
      glGetShaderiv( shader, GL_COMPILE_STATUS, &success );
      if ( success != GL_TRUE )   exit ( 1 );
   }
}
 
 
GLuint
load_shader (
   const char  *shader_source,
   GLenum       type
)
{
   GLuint  shader = glCreateShader( type );
 
   glShaderSource  ( shader , 1 , &shader_source , NULL );
   glCompileShader ( shader );
 
   print_shader_info_log ( shader );
 
   return shader;
}
 
 
Display    *x_display;
Window      win;
EGLDisplay  egl_display;
EGLContext  egl_context;
EGLSurface  egl_surface;
 
GLfloat
   norm_x    =  0.0,
   norm_y    =  0.0,
   offset_x  =  0.0,
   offset_y  =  0.0,
   p1_pos_x  =  0.0,
   p1_pos_y  =  0.0;
 
GLint
   phase_loc,
   offset_loc,
   position_loc;
 
 
int        update_pos = False;
 
const float vertexArray[] = {
   0.0,  0.5,  0.0,
  -0.5,  0.0,  0.0,
   0.0, -0.5,  0.0,
   0.5,  0.0,  0.0,
   0.0,  0.5,  0.0 
};
 
 
void  render()
{
   static float  phase = 0;
   static int    donesetup = 0;
 
   static XWindowAttributes gwa;
 
   //// draw
 
   if ( !donesetup ) {
      XWindowAttributes  gwa;
      XGetWindowAttributes ( x_display , win , &gwa );
      glViewport ( 0 , 0 , gwa.width , gwa.height );
      glClearColor ( 0.08 , 0.5 , 0.07 , 1.);    // background color
      donesetup = 1;
   }
   glClear ( GL_COLOR_BUFFER_BIT );
 
   glUniform1f ( phase_loc , phase );  // write the value of phase to the shaders phase
   phase  =  fmodf ( phase + 0.5f , 2.f * 3.141f );    // and update the local variable
 
   if ( update_pos ) {  // if the position of the texture has changed due to user action
      GLfloat old_offset_x  =  offset_x;
      GLfloat old_offset_y  =  offset_y;
 
      offset_x  =  norm_x - p1_pos_x;
      offset_y  =  norm_y - p1_pos_y;
 
      p1_pos_x  =  norm_x;
      p1_pos_y  =  norm_y;
 
      offset_x  +=  old_offset_x;
      offset_y  +=  old_offset_y;
 
      update_pos = False;
   }
 
   glUniform4f ( offset_loc  ,  offset_x , offset_y , 0.0 , 0.0 );
 
   glVertexAttribPointer ( position_loc, 3, GL_FLOAT, False, 0, vertexArray );
   glEnableVertexAttribArray ( position_loc );
   glDrawArrays ( GL_TRIANGLE_STRIP, 0, 5 );
 
   eglSwapBuffers ( egl_display, egl_surface );  // get the rendered buffer to the screen
}

struct sunxi_disp *sunxi_drm_open(int osd_enabled)
{
   ///////  the X11 part  //////////////////////////////////////////////////////////////////
   // in the first part the program opens a connection to the X11 window manager
   //
    struct sunxi_drm_private *disp = calloc(1, sizeof(*disp));
    disp->pub.close = sunxi_disp_close;
    disp->pub.set_video_layer = sunxi_disp_set_video_layer;

    
   x_display = XOpenDisplay ( NULL );   // open the standard display (the primary screen)
   if ( x_display == NULL ) {
      printf("cannot connect to X server\n");
      return 0;
   }
 
   Window root  =  DefaultRootWindow( x_display );   // get the root window (usually the whole screen)
 
   XSetWindowAttributes  swa;
   swa.event_mask  =  ExposureMask | PointerMotionMask | KeyPressMask;
 
   win  =  XCreateWindow (   // create a window with the provided parameters
              x_display, root,
              0, 0, 800, 480,   0,
              CopyFromParent, InputOutput,
              CopyFromParent, CWEventMask,
              &swa );
 
   XSetWindowAttributes  xattr;
   Atom  atom;
   int   one = 1;
 
   xattr.override_redirect = False;
   XChangeWindowAttributes ( x_display, win, CWOverrideRedirect, &xattr );
 
//    atom = XInternAtom ( x_display, "_NET_WM_STATE_FULLSCREEN", True );
//    XChangeProperty (
//       x_display, win,
//       XInternAtom ( x_display, "_NET_WM_STATE", True ),
//       XA_ATOM,  32,  PropModeReplace,
//       (unsigned char*) &atom, d 1 );
 
//    XChangeProperty (
//       x_display, win,
//       XInternAtom ( x_display, "_HILDON_NON_COMPOSITED_WINDOW", False ),
//       XA_INTEGER,  32,  PropModeReplace,
//       (unsigned char*) &one,  1);
//  
   XWMHints hints;
   hints.input = True;
   hints.flags = InputHint;
   XSetWMHints(x_display, win, &hints);
 
   XMapWindow ( x_display , win );             // make the window visible on the screen
   XStoreName ( x_display , win , "Internal window" ); // give the window a name
 
   //// get identifiers for the provided atom name strings
   Atom wm_state   = XInternAtom ( x_display, "_NET_WM_STATE", False );
//    Atom fullscreen = XInternAtom ( x_display, "_NET_WM_STATE_FULLSCREEN", False );
  
   ///////  the egl part  //////////////////////////////////////////////////////////////////
   //  egl provides an interface to connect the graphics related functionality of openGL ES
   //  with the windowing interface and functionality of the native operation system (X11
   //  in our case.
 
   egl_display  =  eglGetDisplay( (EGLNativeDisplayType) x_display );
   if ( egl_display == EGL_NO_DISPLAY ) {
      printf("Got no EGL display.\n");
      return 0;
   }
 
   if ( !eglInitialize( egl_display, NULL, NULL ) ) {
       printf("Got no EGL init.\n");
      return 0;
   }
 
   EGLint attr[] = {       // some attributes to set up our egl-interface
      EGL_BUFFER_SIZE, 16,
      EGL_RENDERABLE_TYPE,
      EGL_OPENGL_ES2_BIT,
      EGL_NONE
   };
 
   EGLConfig  ecfg;
   EGLint     num_config;
   if ( !eglChooseConfig( egl_display, attr, &ecfg, 1, &num_config ) ) {
              printf("Got no EGL config %d.\n", eglGetError());
      return 0;
   }
 
   if ( num_config != 1 ) {
//       cerr << "Didn't get exactly one config, but " << num_config << endl;
      return 0;
   }
 
   egl_surface = eglCreateWindowSurface ( egl_display, ecfg, win, NULL );
   if ( egl_surface == EGL_NO_SURFACE ) {
        printf("Got no EGL surface %d.\n", eglGetError());
      return 0;
   }
 
   //// egl-contexts collect all state descriptions needed required for operation
   EGLint ctxattr[] = {
      EGL_CONTEXT_CLIENT_VERSION, 2,
      EGL_NONE
   };
   egl_context = eglCreateContext ( egl_display, ecfg, EGL_NO_CONTEXT, ctxattr );
   if ( egl_context == EGL_NO_CONTEXT ) {
        printf("no create context %d.\n", eglGetError());
      return 0;
   }
 
   //// associate the egl-context with the egl-surface
   eglMakeCurrent( egl_display, egl_surface, egl_surface, egl_context );
 
 
   ///////  the openGL part  ///////////////////////////////////////////////////////////////
 
   GLuint vertexShader   = load_shader ( vertex_src , GL_VERTEX_SHADER  );     // load vertex shader
   GLuint fragmentShader = load_shader ( fragment_src , GL_FRAGMENT_SHADER );  // load fragment shader
 
   GLuint shaderProgram  = glCreateProgram ();                 // create program object
   glAttachShader ( shaderProgram, vertexShader );             // and attach both...
   glAttachShader ( shaderProgram, fragmentShader );           // ... shaders to it
 
   glLinkProgram ( shaderProgram );    // link the program
   glUseProgram  ( shaderProgram );    // and select it for usage
 
   //// now get the locations (kind of handle) of the shaders variables
   position_loc  = glGetAttribLocation  ( shaderProgram , "position" );
   phase_loc     = glGetUniformLocation ( shaderProgram , "phase"    );
   offset_loc    = glGetUniformLocation ( shaderProgram , "offset"   );
   if ( position_loc < 0  ||  phase_loc < 0  ||  offset_loc < 0 ) {
        printf("Unable to get uniform location\n") ;
      return 0;
   }
 
    printf("done\n");
    return disp;
}


static void sunxi_disp_close(struct sunxi_disp *sunxi_disp)
{
   eglDestroyContext ( egl_display, egl_context );
   eglDestroySurface ( egl_display, egl_surface );
   eglTerminate      ( egl_display );
   XDestroyWindow    ( x_display, win );
   XCloseDisplay     ( x_display );

}

static int sunxi_disp_set_video_layer(struct sunxi_disp *sunxi_disp, int x, int y, int width, int height, output_surface_ctx_t *surface)
{
/*    
    data[0] = cedrus_mem_get_pointer(surface->yuv->data);
    sizes[0] = surface->vs->luma_size;
    data[1] = cedrus_mem_get_pointer(surface->yuv->data) + sizes[0];
    sizes[1] = surface->vs->chroma_size / 2;
    data[2] = cedrus_mem_get_pointer(surface->yuv->data) + sizes[0] + sizes[1];
    sizes[2] = surface->vs->chroma_size / 2;*/
        printf("frame\n");
        render();
    return 0;
}


    
//
// 	switch (surface->vs->source_format) {
// 	case VDP_YCBCR_FORMAT_YUYV:
// 		disp->video_info.fb.mode = DISP_MOD_INTERLEAVED;
// 		disp->video_info.fb.format = DISP_FORMAT_YUV422;
// 		disp->video_info.fb.seq = DISP_SEQ_YUYV;
// 		break;
// 	case VDP_YCBCR_FORMAT_UYVY:
// 		disp->video_info.fb.mode = DISP_MOD_INTERLEAVED;
// 		disp->video_info.fb.format = DISP_FORMAT_YUV422;
// 		disp->video_info.fb.seq = DISP_SEQ_UYVY;
// 		break;
// 	case VDP_YCBCR_FORMAT_NV12:
// 		disp->video_info.fb.mode = DISP_MOD_NON_MB_UV_COMBINED;
// 		disp->video_info.fb.format = DISP_FORMAT_YUV420;
// 		disp->video_info.fb.seq = DISP_SEQ_UVUV;
// 		break;
// 	case VDP_YCBCR_FORMAT_YV12: <--star warsis this one
// 		disp->video_info.fb.mode = DISP_MOD_NON_MB_PLANAR;
// 		disp->video_info.fb.format = DISP_FORMAT_YUV420;
// 		disp->video_info.fb.seq = DISP_SEQ_UVUV;
// 		break;
// 	default:
// 	case INTERNAL_YCBCR_FORMAT:
// 		disp->video_info.fb.mode = DISP_MOD_MB_UV_COMBINED;
// 		disp->video_info.fb.format = DISP_FORMAT_YUV420;
// 		disp->video_info.fb.seq = DISP_SEQ_UVUV;
// 		break;
// 	}
//
// 	disp->video_info.fb.addr[0] = cedrus_mem_get_phys_addr(surface->yuv->data);
// 	disp->video_info.fb.addr[1] = cedrus_mem_get_phys_addr(surface->yuv->data) + surface->vs->luma_size;
// 	disp->video_info.fb.addr[2] = cedrus_mem_get_phys_addr(surface->yuv->data) + surface->vs->luma_size + surface->vs->chroma_size / 2;
//
// 	disp->video_info.fb.size.width = surface->vs->width;
// 	disp->video_info.fb.size.height = surface->vs->height;
// 	disp->video_info.src_win.x = surface->video_src_rect.x0;
// 	disp->video_info.src_win.y = surface->video_src_rect.y0;
// 	disp->video_info.src_win.width = surface->video_src_rect.x1 - surface->video_src_rect.x0;
// 	disp->video_info.src_win.height = surface->video_src_rect.y1 - surface->video_src_rect.y0;
// 	disp->video_info.scn_win.x = x + surface->video_dst_rect.x0;
// 	disp->video_info.scn_win.y = y + surface->video_dst_rect.y0;
// 	disp->video_info.scn_win.width = surface->video_dst_rect.x1 - surface->video_dst_rect.x0;
// 	disp->video_info.scn_win.height = surface->video_dst_rect.y1 - surface->video_dst_rect.y0;
//
// 	if (disp->video_info.scn_win.y < 0)
// 	{
// 		int scn_clip = -(disp->video_info.scn_win.y);
// 		int src_clip = scn_clip * disp->video_info.src_win.height / disp->video_info.scn_win.height;
// 		disp->video_info.src_win.y += src_clip;
// 		disp->video_info.src_win.height -= src_clip;
// 		disp->video_info.scn_win.y = 0;
// 		disp->video_info.scn_win.height -= scn_clip;
// 	}
//
// 	uint32_t args[4] = { 0, disp->video_layer, (unsigned long)(&disp->video_info), 0 };
// 	ioctl(disp->fd, DISP_CMD_LAYER_SET_PARA, args);
//
// 	ioctl(disp->fd, DISP_CMD_LAYER_OPEN, args);
//
// 	// Note: might be more reliable (but slower and problematic when there
// 	// are driver issues and the GET functions return wrong values) to query the
// 	// old values instead of relying on our internal csc_change.
// 	// Since the driver calculates a matrix out of these values after each
// 	// set doing this unconditionally is costly.
// 	if (surface->csc_change)
// 	{
// 		ioctl(disp->fd, DISP_CMD_LAYER_ENHANCE_OFF, args);
// 		args[2] = 0xff * surface->brightness + 0x20;
// 		ioctl(disp->fd, DISP_CMD_LAYER_SET_BRIGHT, args);
// 		args[2] = 0x20 * surface->contrast;
// 		ioctl(disp->fd, DISP_CMD_LAYER_SET_CONTRAST, args);
// 		args[2] = 0x20 * surface->saturation;
// 		ioctl(disp->fd, DISP_CMD_LAYER_SET_SATURATION, args);
// 		// hue scale is randomly chosen, no idea how it maps exactly
// 		args[2] = (32 / 3.14) * surface->hue + 0x20;
// 		ioctl(disp->fd, DISP_CMD_LAYER_SET_HUE, args);
// 		ioctl(disp->fd, DISP_CMD_LAYER_ENHANCE_ON, args);
// 		surface->csc_change = 0;
// 	}
//
// 	return 0;
// }
//
// static void sunxi_disp_close_video_layer(struct sunxi_disp *sunxi_disp)
// {
// 	struct sunxi_disp_private *disp = (struct sunxi_disp_private *)sunxi_disp;
//
// 	uint32_t args[4] = { 0, disp->video_layer, 0, 0 };
// 	ioctl(disp->fd, DISP_CMD_LAYER_CLOSE, args);
// }

static int sunxi_disp_set_osd_layer(struct sunxi_disp *sunxi_disp, int x, int y, int width, int height, output_surface_ctx_t *surface)
{/*
	struct sunxi_disp_private *disp = (struct sunxi_disp_private *)sunxi_disp;

	switch (surface->rgba.format)
	{
	case VDP_RGBA_FORMAT_R8G8B8A8:
		disp->osd_info.fb.br_swap = 1;
		break;
	case VDP_RGBA_FORMAT_B8G8R8A8:
	default:
		disp->osd_info.fb.br_swap = 0;
		break;
	}

	disp->osd_info.fb.addr[0] = cedrus_mem_get_phys_addr(surface->rgba.data);
	disp->osd_info.fb.size.width = surface->rgba.width;
	disp->osd_info.fb.size.height = surface->rgba.height;
	disp->osd_info.src_win.x = surface->rgba.dirty.x0;
	disp->osd_info.src_win.y = surface->rgba.dirty.y0;
	disp->osd_info.src_win.width = surface->rgba.dirty.x1 - surface->rgba.dirty.x0;
	disp->osd_info.src_win.height = surface->rgba.dirty.y1 - surface->rgba.dirty.y0;
	disp->osd_info.scn_win.x = x + surface->rgba.dirty.x0;
	disp->osd_info.scn_win.y = y + surface->rgba.dirty.y0;
	disp->osd_info.scn_win.width = min_nz(width, surface->rgba.dirty.x1) - surface->rgba.dirty.x0;
	disp->osd_info.scn_win.height = min_nz(height, surface->rgba.dirty.y1) - surface->rgba.dirty.y0;

	uint32_t args[4] = { 0, disp->osd_layer, (unsigned long)(&disp->osd_info), 0 };
	ioctl(disp->fd, DISP_CMD_LAYER_SET_PARA, args);

	ioctl(disp->fd, DISP_CMD_LAYER_OPEN, args);

	return 0;*/
}

static void sunxi_disp_close_osd_layer(struct sunxi_disp *sunxi_disp)
{
// 	struct sunxi_disp_private *disp = (struct sunxi_disp_private *)sunxi_disp;
//
// 	uint32_t args[4] = { 0, disp->osd_layer, 0, 0 };
// 	ioctl(disp->fd, DISP_CMD_LAYER_CLOSE, args);
}
