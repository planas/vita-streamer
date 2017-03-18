#include <stdbool.h>

#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/jpegenc.h>
#include <psp2/io/fcntl.h>
#include <psp2/display.h>

#include <taihen.h>

#include "taishell/logger.h"
#include "taishell/utils.h"

#define NUM_THREADS 2
#define BUFFER_SIZE 768*1024

#define FB_WIDTH  960
#define FB_HEIGHT 544
#define FB_SIZE   FB_WIDTH*FB_HEIGHT*4

/* Kernel module syscall */
void streamerUpdate(uintptr_t addr, int size);

typedef struct thread_s {
  int idx;
  SceUID thuid;
  bool busy;
  void *jpeg_ctx;
  void *buffer;
  SceDisplayFrameBuf *fb;
} thread_t;

static thread_t thread_pool[NUM_THREADS];

static void *cdram_buffer;
static void *jpeg_buffer;

static int worker_thread(SceSize args, void *argp) {
  thread_t *thread = *(thread_t **)argp;
  int ret;

  ret = sceJpegEncoderCsc(
    thread->jpeg_ctx,
    thread->buffer,
    thread->fb->base,
    thread->fb->pitch,
    thread->fb->pixelformat
  );

  if(ret < 0) {
    LOG_E("sceJpegEncoderCsc: 0x%x", ret);
    goto exit;
  }

  ret = sceJpegEncoderEncode(thread->jpeg_ctx, thread->buffer);
  if(ret > 0) {
    streamerUpdate((uintptr_t)thread->buffer, ret);
  }

  exit:
    LOG_D("[%d] Exiting thread! 0x%x", thread->idx, ret);
    thread->busy = false;
    sceKernelExitThread(0);
    return 0;
}

SceInt64 ts;
void *last = NULL;

void fb_callback(SceDisplayFrameBuf *fb) {
  for(int i = 0; i < NUM_THREADS; i++) {
    thread_t *thread = &thread_pool[i];

    if(!thread->busy) {
      thread->fb = fb;
      thread->busy = true;
      sceKernelStartThread(thread->thuid, sizeof(thread), &thread);
      return;
    }
  }
  LOG_D("No workers available. Skipping frame");
}

static int init() {
  int ret;

  int jpeg_ctx_size = sceJpegEncoderGetContextSize();
  LOG_D("JPEG context required size: %ld", jpeg_ctx_size);

  jpeg_buffer = tshMemAlloc(
    SCE_KERNEL_MEMBLOCK_TYPE_USER_RW,
    ALIGN(jpeg_ctx_size*NUM_THREADS, 0xfff)
  );

  if(!jpeg_buffer) {
    goto err;
  }

  cdram_buffer = tshMemAlloc(
    SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,
    BUFFER_SIZE*NUM_THREADS
  );

  if(!cdram_buffer) {
    goto err1;
  }

  for(int i = 0; i < NUM_THREADS; i++) {
    thread_t *thread = &thread_pool[i];

    thread->idx = i;
    thread->thuid = sceKernelCreateThread("streamer_worker_thread", worker_thread, 0x10000100, 0x1000, 0, 0, NULL);
    LOG_D("Worker thread UID: 0x%08X", thread->thuid);

    thread->jpeg_ctx = jpeg_buffer + jpeg_ctx_size * i;
    thread->buffer   = cdram_buffer + BUFFER_SIZE * i;

    ret = sceJpegEncoderInit(
      thread->jpeg_ctx,
      FB_WIDTH, FB_HEIGHT,
      PIXELFORMAT_YCBCR420 | PIXELFORMAT_CSC_ARGB_YCBCR,
      thread->buffer, BUFFER_SIZE
    );
    LOG_D("sceJpegEncoderInit: 0x%X", ret);

    if(ret < 0)
      goto err2;
  }

  return 0;

  err2:
    tshMemFree(cdram_buffer);
  err1:
    tshMemFree(jpeg_buffer);
  err:
    return -1;
}

void _start() __attribute__ ((weak, alias ("module_start")));
int module_start(SceSize argc, const void *args) {
  LOG_D("VitaStream loaded");

  if(init() >= 0) {
    tshDisableAutoSuspend();
    tshRegisterFbCallback(fb_callback);
  }
  return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize argc, const void *args) {
  LOG_D("Stopping VitaStream");

  tshMemFree(jpeg_buffer);
  tshMemFree(cdram_buffer);
  tshEnableAutoSuspend();

  return SCE_KERNEL_STOP_SUCCESS;
}
