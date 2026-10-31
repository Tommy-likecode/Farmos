#include "farm_scene.h"
#include "farm_rt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

typedef struct {
  farm_Renderer* r;
  uint8_t* rgb;
  int32_t count;
  int32_t cap;
  int32_t w;
  int32_t h;
} FarmVideoCap;

static FarmVideoCap g_vid;

static int video_env_on(const char* name) {
  const char* e = getenv(name);
  return e && e[0] == '1' && e[1] == '\0';
}

static void video_fail(void) {
  farm_trap(123, "MP4 encode failed");
}

static void video_reset(void) {
  free(g_vid.rgb);
  memset(&g_vid, 0, sizeof(g_vid));
}

static void fb_size(farm_Renderer* self, int32_t* w, int32_t* h) {
  *w = self->fb_width > 0 ? self->fb_width : self->width;
  *h = self->fb_height > 0 ? self->fb_height : self->height;
}

void farm_video_on_render(farm_Renderer* self) {
  if (!self || self->disposed || !self->framebuffer) return;
  int32_t w, h;
  fb_size(self, &w, &h);
  if (w < 1 || h < 1) return;
  if (g_vid.rgb && (g_vid.w != w || g_vid.h != h || g_vid.r != self)) {
    video_reset();
  }
  g_vid.r = self;
  g_vid.w = w;
  g_vid.h = h;
  if (g_vid.count == g_vid.cap) {
    int32_t ncap = g_vid.cap ? g_vid.cap * 2 : 16;
    size_t frame = (size_t)w * (size_t)h * 3u;
    uint8_t* nb = (uint8_t*)realloc(g_vid.rgb, (size_t)ncap * frame);
    if (!nb) video_fail();
    g_vid.rgb = nb;
    g_vid.cap = ncap;
  }
  memcpy(g_vid.rgb + (size_t)g_vid.count * (size_t)w * (size_t)h * 3u,
         self->framebuffer, (size_t)w * (size_t)h * 3u);
  g_vid.count++;
}

static int run_ffmpeg(char** argv) {
#ifdef _WIN32
  char cmd[4096];
  cmd[0] = 0;
  size_t used = 0;
  for (int i = 0; argv[i]; i++) {
    size_t n = strlen(argv[i]);
    if (used + n + 4 >= sizeof(cmd)) return 1;
    if (i) cmd[used++] = ' ';
    cmd[used++] = '"';
    memcpy(cmd + used, argv[i], n);
    used += n;
    cmd[used++] = '"';
    cmd[used] = 0;
  }
  STARTUPINFOA si;
  PROCESS_INFORMATION pi;
  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  ZeroMemory(&pi, sizeof(pi));
  if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
    return 1;
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return (int)code;
#else
  pid_t pid = fork();
  if (pid < 0) return 1;
  if (pid == 0) {
    execvp(argv[0], argv);
    _exit(127);
  }
  int st = 0;
  if (waitpid(pid, &st, 0) < 0) return 1;
  if (WIFEXITED(st)) return WEXITSTATUS(st);
  return 1;
#endif
}

static int encode_mp4(const char* rgb_path, const char* out_path, int32_t w, int32_t h) {
  char size[64];
  snprintf(size, sizeof(size), "%dx%d", (int)w, (int)h);
  char* argv_x264[] = {
      "ffmpeg", "-y", "-loglevel", "error",
      "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", size, "-r", "30",
      "-i", (char*)rgb_path,
      "-c:v", "libx264", "-preset", "veryfast", "-pix_fmt", "yuv420p",
      "-movflags", "+faststart",
      (char*)out_path, NULL};
  if (run_ffmpeg(argv_x264) == 0) return 0;
  char* argv_mpeg[] = {
      "ffmpeg", "-y", "-loglevel", "error",
      "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", size, "-r", "30",
      "-i", (char*)rgb_path,
      "-c:v", "mpeg4", "-pix_fmt", "yuv420p",
      (char*)out_path, NULL};
  return run_ffmpeg(argv_mpeg);
}

void farm_Renderer_saveMP4(farm_Renderer* self, FarmString path) {
  if (!self || self->disposed) farm_trap(105, "use after dispose");
  int strict = video_env_on("FARMOS_VIDEO_STRICT");
  if (video_env_on("FARMOS_VIDEO_FORCE_FAIL")) {
    if (strict) video_fail();
    return;
  }
  char* path_cstr = (char*)farm_arena_alloc((size_t)path.len + 1);
  memcpy(path_cstr, path.ptr, (size_t)path.len);
  path_cstr[path.len] = '\0';

  if (g_vid.count < 1 || !g_vid.rgb || g_vid.r != self) {
    if (strict) video_fail();
    return;
  }

  char rgb_path[512];
#ifdef _WIN32
  char tmp[MAX_PATH];
  DWORD n = GetTempPathA((DWORD)sizeof(tmp), tmp);
  if (n == 0 || n >= sizeof(tmp)) {
    if (strict) video_fail();
    return;
  }
  snprintf(rgb_path, sizeof(rgb_path), "%sfarmc_vid_%lu.rgb", tmp, (unsigned long)GetCurrentProcessId());
#else
  snprintf(rgb_path, sizeof(rgb_path), "/tmp/farmc_vid_%ld.rgb", (long)getpid());
#endif

  FILE* f = fopen(rgb_path, "wb");
  if (!f) {
    if (strict) video_fail();
    return;
  }
  size_t bytes = (size_t)g_vid.count * (size_t)g_vid.w * (size_t)g_vid.h * 3u;
  size_t wrote = fwrite(g_vid.rgb, 1, bytes, f);
  fclose(f);
  if (wrote != bytes) {
    remove(rgb_path);
    if (strict) video_fail();
    return;
  }

  int rc = encode_mp4(rgb_path, path_cstr, g_vid.w, g_vid.h);
  remove(rgb_path);
  video_reset();
  if (rc != 0) {
    if (strict) video_fail();
  }
}
