/*
Copyright 2005-2017 Jay Sorg

Permission to use, copy, modify, distribute, and sell this software and its
documentation for any purpose is hereby granted without fee, provided that
the above copyright notice appear in all copies and that both that
copyright notice and this permission notice appear in supporting
documentation.

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
OPEN GROUP BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Client connection to xrdp

*/

#ifndef _RDPCLIENTCON_H
#define _RDPCLIENTCON_H

#include <xorg-server.h>
#include <xorgVersion.h>
#include <xf86.h>

#include "xup_client_info.h"

/* Capability bits for the accel-assist helper (control batch message).
   Must match XH_CAPS_* in xrdp's xrdp_accel_assist.h. */
#define XH_CAPS_AVC444 (1 << 0)
#define XH_CAPS_AVC444_V2 (1 << 1)

/* XORGXRDP_TIMING=1: per-stage frame timing on this side of the pipe. */
struct rdp_timing
{
    int enabled;
    int count;
    int capture_total_ms;
    int capture_max_ms;
    int send_total_ms;
    int send_max_ms;
    int ack_total_ms;          /* send -> rect_id_ack, the lockstep gap */
    int ack_max_ms;
    /* xrdp's send-to-ack round trip, which unlike ack_total_ms excludes our
       own capture interval. */
    int crtt_total_ms;
    int crtt_max_ms;
    int crtt_count;
    int blocked;               /* callbacks that returned early on the gate */
    CARD32 sent_ms;
    /* Send time per frame, indexed by rect_id: with frames in flight an ack
       need not be for the latest one. */
#define RDP_SEND_TIME_SLOTS 64
    CARD32 send_time[RDP_SEND_TIME_SLOTS];
    int blit_total_ms;         /* the CopyArea loop */
    int sync_total_ms;         /* the 1x1 GetImage that drains the GPU */
    int blit_count;
    /* idle_total_ms: time from handing a frame off to the next capture.
       inflight_total: sum of rect_id - rect_id_ack at capture start.
       damage_starved: frames that ended with an empty dirtyRegion. */
    int idle_total_ms;
    int capture_count;          /* frames actually sent, vs count = acks */
    int idle_max_ms;
    int inflight_total;
    int damage_starved;
    /* The bounding-box collapse in rdpCapRect: how often, rects discarded,
       and waste (box area as a percentage of the dirty area; 100 = free).
       Invisible downstream, where a collapse looks like one large rect. */
    /* Every capture: rect count and monitor coverage, including single-rect
       frames the collapse counters do not see. */
    int dirty_frames;
    int dirty_rects_total;
    int dirty_rects_max;
    int dirty_area_total;       /* percent of the monitor, summed */
    int dirty_area_max;
    int dirty_full_frames;      /* captures covering 90% or more */
    int collapse_considered;    /* frames with more than one dirty rect */
    int collapse_fired;
    int collapse_rects_total;   /* pre-collapse rect count, when it fired */
    int collapse_rects_max;
    int collapse_waste_total;
    int collapse_waste_max;
};

/* used in rdpGlyphs.c */
struct font_cache
{
    int offset;
    int baseline;
    int width;
    int height;
    int crc;
    int stamp;
};

struct rdpup_os_bitmap
{
    int used;
    PixmapPtr pixmap;
    rdpPixmapPtr priv;
    int stamp;
};

enum shared_memory_status {
    SHM_UNINITIALIZED = 0,
    SHM_RESIZING,
    SHM_ACTIVE_PENDING,
    SHM_RFX_ACTIVE_PENDING,
    SHM_H264_ACTIVE_PENDING,
    SHM_ACTIVE,
    SHM_RFX_ACTIVE,
    SHM_H264_ACTIVE
};

/* one of these for each client */
struct _rdpClientCon
{
    rdpPtr dev;

    int sck;
    int sckControlListener;
    int sckControl;
    struct stream *out_s;
    struct stream *in_s;

    int connected; /* boolean. Set to False when I/O fails */
    int begin; /* boolean */
    int count;
    struct rdpup_os_bitmap *osBitmaps;
    int maxOsBitmaps;
    int osBitmapStamp;
    int osBitmapAllocSize;
    int osBitmapNumUsed;
    int doComposite;
    int doGlyphCache;
    int canDoPixToPix;
    int doMultimon;

    int rdp_bpp; /* client depth */
    int rdp_Bpp;
    int rdp_Bpp_mask;
    int rdp_width;
    int rdp_height;
    int rdp_format; /* XRDP_a8r8g8b8, XRDP_r5g6b5, ... */
    int cap_left;
    int cap_top;
    int cap_width;
    int cap_height;
    int cap_stride_bytes;

    int rdpIndex; /* current os target */

    int conNumber;

    /* rdpGlyphs.c */
    struct font_cache font_cache[12][256];
    int font_stamp;

    struct xup_client_info client_info;
    struct rdp_timing timing;

    uint8_t *shmemptr;
    int shmemfd;
    int shmem_bytes;
    int shmem_lineBytes;
    RegionPtr shmRegion;
    int rect_id;
    int rect_id_ack;
    enum shared_memory_status shmemstatus;

    /* Two capture buffers per monitor, alternated so a frame can be
       captured while the helper reads the previous one. rdpCapture copies
       only damage, but the full-frame AVC444 aux pass reads the whole
       texture, so accelAssistPending[mon][buf] holds the damage this buffer
       has missed; a capture copies it along with the current damage. The
       client is still told only the current damage. */
    PixmapPtr accelAssistPixmaps[16][2];
    RegionPtr accelAssistPending[16][2];
    int accelAssistBuf[16];
    int capture_depth;         /* XORGXRDP_CAPTURE_DEPTH, 1 or 2 */

    OsTimerPtr updateTimer;
    CARD32 lastUpdateTime; /* millisecond timestamp */
    int updateScheduled; /* boolean */
    int updateRetries;

    /* Minimum spacing between captures for this connection: seeded from
       client_info, then steered by adaptive pacing. Per connection, since
       the right value depends on the client. */
    CARD32 msFrameInterval;
    int pace_enabled;          /* XORGXRDP_ADAPTIVE_PACE */
    int pace_min_ms;           /* XORGXRDP_PACE_MIN_MS */
    int pace_max_ms;           /* XORGXRDP_PACE_MAX_MS */
    int pace_rtt_ms;           /* smoothed client rtt, the control signal */
    int pace_samples;          /* acks seen, until the average is warm */
    int pace_good_run;         /* consecutive acks the client kept up on */

    RegionPtr dirtyRegion;

    int num_rfx_crcs_alloc[16];
    uint64_t *rfx_crcs[16];
    int send_key_frame[16];

    /* true = skip drawing */
    int suppress_output;

    int use_accel_assist;
    int accel_assist_pid;

    struct _rdpClientCon *next;
    struct _rdpClientCon *prev;
};

extern _X_EXPORT int
rdpClientConBeginUpdate(rdpPtr dev, rdpClientCon *clientCon);
extern _X_EXPORT int
rdpClientConEndUpdate(rdpPtr dev, rdpClientCon *clientCon);
extern _X_EXPORT int
rdpClientConSetFgcolor(rdpPtr dev, rdpClientCon *clientCon, int fgcolor);
extern _X_EXPORT int
rdpClientConSetKeyboardIndicators(rdpPtr dev, rdpClientCon *clientCon,
                                  int led_flags);
extern _X_EXPORT int
rdpClientConFillRect(rdpPtr dev, rdpClientCon *clientCon,
                     short x, short y, int cx, int cy);
extern _X_EXPORT int
rdpClientConCheck(ScreenPtr pScreen);
extern _X_EXPORT int
rdpClientConInit(rdpPtr dev);
extern _X_EXPORT int
rdpClientConDeinit(rdpPtr dev);

extern _X_EXPORT int
rdpClientConDeleteOsSurface(rdpPtr dev, rdpClientCon *clientCon, int rdpindex);

extern _X_EXPORT int
rdpClientConRemoveOsBitmap(rdpPtr dev, rdpClientCon *clientCon, int rdpindex);

extern _X_EXPORT void
rdpClientConScheduleDeferredUpdate(rdpPtr dev);
extern _X_EXPORT int
rdpClientConCheckDirtyScreen(rdpPtr dev, rdpClientCon *clientCon);
extern _X_EXPORT int
rdpClientConAddDirtyScreenReg(rdpPtr dev, rdpClientCon *clientCon,
                              RegionPtr reg);
extern _X_EXPORT int
rdpClientConAddDirtyScreenBox(rdpPtr dev, rdpClientCon *clientCon,
                              BoxPtr box);
extern _X_EXPORT int
rdpClientConAddDirtyScreen(rdpPtr dev, rdpClientCon *clientCon,
                           int x, int y, int cx, int cy);
extern _X_EXPORT void
rdpClientConGetScreenImageRect(rdpPtr dev, rdpClientCon *clientCon,
                               struct image_data *id);
extern _X_EXPORT int
rdpClientConAddAllReg(rdpPtr dev, RegionPtr reg, DrawablePtr pDrawable);
extern _X_EXPORT int
rdpClientConAddAllBox(rdpPtr dev, BoxPtr box, DrawablePtr pDrawable);
extern _X_EXPORT int
rdpClientConSetCursorSystem(rdpPtr dev, rdpClientCon *clientCon,
                            int pointer_type);
extern _X_EXPORT int
rdpClientConMoveCursor(rdpPtr dev, rdpClientCon *clientCon, int x, int y);
extern _X_EXPORT int
rdpClientConSetCursor(rdpPtr dev, rdpClientCon *clientCon,
                      short x, short y, uint8_t *cur_data, uint8_t *cur_mask);
extern _X_EXPORT int
rdpClientConSetCursorEx(rdpPtr dev, rdpClientCon *clientCon,
                        short x, short y, uint8_t *cur_data,
                        uint8_t *cur_mask, int bpp);
extern _X_EXPORT int
rdpClientConSetCursorShmFd(rdpPtr dev, rdpClientCon *clientCon,
                           short x, short y,
                           uint8_t *cur_data, uint8_t *cur_mask, int bpp,
                           int width, int height);

#endif
