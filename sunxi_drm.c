/*
 * Copyright (c) 2013-2015 Jens Kuske <jenskuske@gmail.com>
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

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <libdrm/drm_fourcc.h>

struct sunxi_drm_private
{
	struct sunxi_disp pub;

	int fd;
    int ctrl_fd;
	int video_layer;
	int osd_layer;
};

static void sunxi_disp_close(struct sunxi_disp *sunxi_disp);
static int sunxi_disp_set_video_layer(struct sunxi_disp *sunxi_disp, int x, int y, int width, int height, output_surface_ctx_t *surface);
static void sunxi_disp_close_video_layer(struct sunxi_disp *sunxi_disp);
static int sunxi_disp_set_osd_layer(struct sunxi_disp *sunxi_disp, int x, int y, int width, int height, output_surface_ctx_t *surface);
static void sunxi_disp_close_osd_layer(struct sunxi_disp *sunxi_disp);

struct sunxi_disp *sunxi_drm_open(int osd_enabled)
{
    #define DRM_PATH "/dev/dri/card0"
    #define DRM_CTL_PATH "/dev/dri/controlD64"
//     #define DRM_CTL_PATH "/dev/dri/renderD128"

    struct sunxi_drm_private *disp = calloc(1, sizeof(*disp));

    int drm_fd = open(DRM_PATH, O_RDWR);
    if (drm_fd <= 0) {
        printf("Could not open %s", DRM_PATH);
        return 0;
    }
    int drm_ctl_fd = open(DRM_CTL_PATH, O_RDWR);
    if (drm_ctl_fd <= 0) {
        printf("Could not open %s", DRM_CTL_PATH);
        return 0;
    }

    drmModeResPtr r;
    drmModePlaneResPtr pr;

    int crtc_x = 0, crtc_y = 0, crtc_w = 0, crtc_h = 0;
    int old_fb = 0;
    int ret = -1;
    int i, j;
    int plane_id = 0;
    int crtc = 0;

//     Window win;

    /**
     * enable all planes
     */
    drmSetClientCap(drm_fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
#ifdef DRM_CLIENT_CAP_ATOMIC
    drmSetClientCap(drm_fd, DRM_CLIENT_CAP_ATOMIC, 1);
#endif

    /**
     * get drm res for crtc
     */
    r = drmModeGetResources(drm_fd);
    if (!r || !r->count_crtcs)
        printf("DAVE :( %d\n", r);
        goto err_res;

    /**
     * find the last available crtc
     **/
    for (i = r->count_crtcs; i && !crtc; i --)
    {
        drmModeCrtcPtr c = drmModeGetCrtc(drm_fd, r->crtcs[i - 1]);
        if (c && c->mode_valid)
        {
            crtc = i;
            crtc_x = c->x;
            crtc_y = c->y;
            crtc_w = c->width;
            crtc_h = c->height;
        }
        drmModeFreeCrtc(c);
    }

    /**
     * get plane res for plane
     */
    pr = drmModeGetPlaneResources(drm_fd);
    if (!pr || !pr->count_planes)
        goto err_plane_res;

    /**
     * find available plane
     */
    for (i = 0; i < pr->count_planes; i++)
    {
        drmModePlanePtr p = drmModeGetPlane(drm_fd, pr->planes[i]);
        if (p && p->possible_crtcs == crtc)
            for (j = 0; j < p->count_formats && !plane_id; j++)
                if (p->formats[j] == DRM_FORMAT_NV12)
                {
                    plane_id = pr->planes[i];
//                     old_fb = p->fb_id;
                }
        drmModeFreePlane(p);
    }

    /**
     * failed to get crtc or plane
     */
    if (!crtc || ! plane_id)
        goto err_overlay;

//     if (!fullscreen)
//     {
//         /**
//          * get window's x y w h
//          */
//         XTranslateCoordinates(dev->display,
//                 dev->drawable,
//                 RootWindow(dev->display, dev->screen),
//                 0, 0, &crtc_x, &crtc_y, &win);
//
//         XTranslateCoordinates(dev->display,
//                 dev->drawable,
//                 RootWindow(dev->display, dev->screen),
//                 clip_w, clip_h, &crtc_w, &crtc_h, &win);
//     }

//     ret = drmModeSetPlane(drm_ctl_fd, plane_id,
//             r->crtcs[crtc - 1], fb_id, 0,
//             crtc_x, crtc_y, crtc_w, crtc_h,
//             0, 0, (src_w ? src_w : crtc_w) << 16,
//             (src_h ? src_h : crtc_h) << 16);

//     if (dev->saved_fb < 0)
//     {
//         dev->saved_fb = old_fb;
//         printf ("store fb:%d", old_fb);
//     } else {
//         drmModeRmFB(dev->drm_ctl_fd, old_fb);
//     }

    disp->pub.close = sunxi_disp_close;
	disp->pub.set_video_layer = sunxi_disp_set_video_layer;
	disp->pub.close_video_layer = sunxi_disp_close_video_layer;
	disp->pub.set_osd_layer = sunxi_disp_set_osd_layer;
	disp->pub.close_osd_layer = sunxi_disp_close_osd_layer;

	return (struct sunxi_disp *)disp;


err_overlay:
    printf("err1");
    drmModeFreePlaneResources(pr);
err_plane_res:
    printf("err2");
    drmModeFreeResources(r);
err_res:
    printf("err3.1\n");
    return 0;
}

static void sunxi_disp_close(struct sunxi_disp *sunxi_disp)
{

}

static int sunxi_disp_set_video_layer(struct sunxi_disp *sunxi_disp, int x, int y, int width, int height, output_surface_ctx_t *surface)
{
// 	struct sunxi_disp_private *disp = (struct sunxi_disp_private *)sunxi_disp;
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
// 	case VDP_YCBCR_FORMAT_YV12:
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
}

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
