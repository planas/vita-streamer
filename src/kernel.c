/*
Networking based on ftpvita by xerpi
see https://github.com/xerpi/libftpvita
*/

#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include <psp2kern/kernel/processmgr.h>
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/threadmgr.h>
#include <psp2kern/kernel/sysmem.h>
#include <psp2kern/kernel/cpu.h>
#include <psp2kern/net/net.h>

#include <taihen.h>

#include "taishell/logger.h"
#include "taishell/utils.h"

#define BUFFER_SIZE 1024*1024
#define NET_PORT 8200

typedef struct {
  int sockfd;
  SceNetSockaddrIn addr;
} client_t;

static int server_sockfd;
static SceUID server_thid;

static void *buffer;

static client_t client;
static bool connected = false;

bool streamerIsConnected() {
  return connected;
}

void streamerUpdate(uintptr_t addr, int size) {
  uint32_t state;

  if(!connected)
    return;

  ENTER_SYSCALL(state);
  ksceKernelMemcpyUserToKernel(buffer, addr, size);
  int ret = ksceNetSendto(
    client.sockfd,
    buffer,
    size, 0,
    (SceNetSockaddr *)&client.addr,
    sizeof(client.addr)
  );

  if(ret < 0)
    connected = false; // really? should check for specific error probably

  EXIT_SYSCALL(state);
}

static int server_thread(SceSize args, void *argp) {
  SceNetSockaddrIn serveraddr;
  int ret;

  LOG_D("Server thread started!\n");

  /* Create server socket */
  server_sockfd = ksceNetSocket("Streamer_server_sock",
    SCE_NET_AF_INET,
    SCE_NET_SOCK_STREAM,
    0);

  LOG_D("Server socket FD: %d\n", server_sockfd);

  /* Fill the server's address */
  serveraddr.sin_family = SCE_NET_AF_INET;
  serveraddr.sin_addr.s_addr = ksceNetHtonl(SCE_NET_INADDR_ANY);
  serveraddr.sin_port = ksceNetHtons(NET_PORT);

  /* Bind the server's address to the socket */
  ret = ksceNetBind(server_sockfd, (SceNetSockaddr *)&serveraddr, sizeof(serveraddr));
  LOG_D("ksceNetBind(): 0x%08X\n", ret);

  /* Start listening */
  ret = ksceNetListen(server_sockfd, 128);
  LOG_D("ksceNetListen(): 0x%08X\n", ret);

  while (1) {
    /* Accept clients */
    SceNetSockaddrIn clientaddr;
    int client_sockfd;
    unsigned int addrlen = sizeof(clientaddr);

    LOG_D("Waiting for incoming connections...\n");

    client_sockfd = ksceNetAccept(server_sockfd, (SceNetSockaddr *)&clientaddr, &addrlen);
    if (client_sockfd >= 0) {
      LOG_D("New connection, client FD: 0x%08X\n", client_sockfd);
      LOG_I("Client connected, port: %i\n", clientaddr.sin_port);

      client.sockfd = client_sockfd;
      memcpy(&client.addr, &clientaddr, sizeof(client.addr));
      connected = true;

      while(connected) {
        // Probably we should check periodically if the client
        // is still connected while there's no data sent
      }
    } else {
      /* if ksceNetAccept returns < 0, it means that the listening
       * socket has been closed, this means that we want to
       * finish the server thread */
      LOG_D("Server socket closed, 0x%08X\n", client_sockfd);
      break;
    }
  }

  LOG_D("Server thread exiting!\n");

  ksceKernelExitDeleteThread(0);
  return 0;
}

static tai_hook_ref_t unload_allowed_hook;
static SceUID unload_allowed_uid;
int unload_allowed_patched(void) {
  TAI_CONTINUE(int, unload_allowed_hook);
  return 1;
}

void _start() __attribute__ ((weak, alias ("module_start")));
int module_start(SceSize argc, const void *args) {
  LOG_D("VitaStream kmodule loaded");

  unload_allowed_uid =
    taiHookFunctionImportForKernel(KERNEL_PID,
                          &unload_allowed_hook,     // Output a reference
                          "SceKernelModulemgr",     // Name of module being hooked
                          0x11F9B314,               // NID specifying SceSblACMgrForKernel
                          0xBBA13D9C,               // Function NID
                          unload_allowed_patched);  // Name of the hook function

  buffer = ktshMemAlloc(
    SCE_KERNEL_MEMBLOCK_TYPE_KERNEL_RW,
    ALIGN(BUFFER_SIZE, 0xfff)
  );

  if(buffer) {
    server_thid = ksceKernelCreateThread("Stream_server_thread", server_thread, 0x10000100, 0x10000, 0, 0, NULL);
    LOG_D("Server thread UID: 0x%08X\n", server_thid);
    ksceKernelStartThread(server_thid, 0, NULL);
  }
  return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize argc, const void *args) {
  LOG_D("Stopping VitaStream");

  ksceNetSocketClose(server_sockfd);
  /* Wait until the server threads ends */
  ksceKernelWaitThreadEnd(server_thid, NULL, NULL);

  if(connected) {
    ksceNetSocketClose(client.sockfd);
  }

  taiHookReleaseForKernel(unload_allowed_uid, unload_allowed_hook);

  return SCE_KERNEL_STOP_SUCCESS;
}
