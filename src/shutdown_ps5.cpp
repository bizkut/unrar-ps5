#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <sys/syscall.h>

typedef struct notify_request
{
  char unused[45];
  char message[3075];
} notify_request_t;

extern "C" int sceKernelSendNotificationRequest(int, notify_request_t *, size_t, int);

int main()
{
  notify_request_t req;
  memset(&req, 0, sizeof(req));
  snprintf(req.message, sizeof(req.message), "PS5: Shutting down console in 3 seconds...");
  sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);

  sync();
  sleep(3);
  sync();
  syscall(55, 0x4008);
  return 0;
}
