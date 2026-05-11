/*
 * Userspace program that communicates with the vga_ball device driver
 * through ioctls
 *
 * Stephen A. Edwards
 * Columbia University
 */

#include <stdio.h>
#include "vga_ball.h"
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#define SCREEN_WIDTH 640
#define SCREEN_HEIGHT 480
#define BALL_RADIUS 16

int vga_ball_fd;

/* Read and print the background color */
void print_background_color() {
  vga_ball_arg_t vla;
  
  if (ioctl(vga_ball_fd, VGA_BALL_READ_BACKGROUND, &vla)) {
      perror("ioctl(VGA_BALL_READ_BACKGROUND) failed");
      return;
  }
  printf("%02x %02x %02x\n",
	 vla.background.red, vla.background.green, vla.background.blue);
}

/* Set the background color */
void set_background_color(const vga_ball_color_t *c)
{
  vga_ball_arg_t vla;
  vla.background = *c;
  if (ioctl(vga_ball_fd, VGA_BALL_WRITE_BACKGROUND, &vla)) {
      perror("ioctl(VGA_BALL_SET_BACKGROUND) failed");
      return;
  }
}

void print_ball_position() {
  vga_ball_arg_t vla;

  if (ioctl(vga_ball_fd, VGA_BALL_READ_POSITION, &vla)) {
      perror("ioctl(VGA_BALL_READ_POSITION) failed");
      return;
  }

  printf("ball @ (%u, %u)\n", vla.position.x, vla.position.y);
}

void set_ball_position(unsigned short x, unsigned short y)
{
  vga_ball_arg_t vla;

  vla.position.x = x;
  vla.position.y = y;
  if (ioctl(vga_ball_fd, VGA_BALL_WRITE_POSITION, &vla)) {
      perror("ioctl(VGA_BALL_WRITE_POSITION) failed");
      return;
  }
}

int main()
{
  int i;
  int x = SCREEN_WIDTH / 2;
  int y = SCREEN_HEIGHT / 2;
  int dx = 5;
  int dy = 4;
  static const char filename[] = "/dev/vga_ball";

  static const vga_ball_color_t beige = { 0xf9, 0xe4, 0xb7 };

  printf("VGA ball Userspace program started\n");

  if ( (vga_ball_fd = open(filename, O_RDWR)) == -1) {
    fprintf(stderr, "could not open %s\n", filename);
    return -1;
  }

  printf("initial state: ");
  print_background_color();
  print_ball_position();

  set_background_color(&beige);

  for (i = 0 ; i < 1000 ; i++) {
    x += dx;
    y += dy;

    if (x <= BALL_RADIUS || x >= SCREEN_WIDTH - 1 - BALL_RADIUS) {
      dx = -dx;
      x += dx;
    }
    if (y <= BALL_RADIUS || y >= SCREEN_HEIGHT - 1 - BALL_RADIUS) {
      dy = -dy;
      y += dy;
    }

    set_ball_position((unsigned short) x, (unsigned short) y);
    usleep(16000);
  }
  
  printf("VGA BALL Userspace program terminating\n");
  return 0;
}
