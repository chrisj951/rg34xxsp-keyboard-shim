// rg34xx_joypad_shim.c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <linux/uinput.h>
#include <linux/input.h>
#include <errno.h>
#include <sys/time.h>

#define INPUT_DEV "/dev/input/event1"

int setup_uinput() {
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if(fd < 0) { perror("open /dev/uinput"); return -1; }

    // Mark device as a gamepad
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_EVBIT, EV_ABS);
    ioctl(fd, UI_SET_PROPBIT, INPUT_PROP_DIRECT);

    // Map all gamepad buttons RetroArch expects
    int btns[] = {
        BTN_SOUTH, BTN_EAST, BTN_NORTH, BTN_WEST,  // A/B/X/Y
        BTN_TL, BTN_TR, BTN_TL2, BTN_TR2,          // L/R, L2/R2
        BTN_START, BTN_SELECT, BTN_MODE            // Start/Select/Menu/Hotkey
    };
    for(int i = 0; i < sizeof(btns)/sizeof(btns[0]); i++)
        ioctl(fd, UI_SET_KEYBIT, btns[i]);

    // D-pad (ABS_HAT0X/Y)
    ioctl(fd, UI_SET_ABSBIT, ABS_HAT0X);
    ioctl(fd, UI_SET_ABSBIT, ABS_HAT0Y);

    struct uinput_user_dev uidev;
    memset(&uidev, 0, sizeof(uidev));
    snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "rg34xx-shim");
    uidev.id.bustype = BUS_USB;
    uidev.id.vendor  = 0x1234;
    uidev.id.product = 0x5678;
    uidev.id.version = 1;

    // D-pad range
    uidev.absmin[ABS_HAT0X] = -1;
    uidev.absmax[ABS_HAT0X] = 1;
    uidev.absmin[ABS_HAT0Y] = -1;
    uidev.absmax[ABS_HAT0Y] = 1;

    if(write(fd, &uidev, sizeof(uidev)) < 0) { perror("write uinput_user_dev"); close(fd); return -1; }
    if(ioctl(fd, UI_DEV_CREATE) < 0) { perror("UI_DEV_CREATE"); close(fd); return -1; }

    return fd;
}

void emit(int fd, int type, int code, int value) {
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = type;
    ev.code = code;
    ev.value = value;
    gettimeofday(&ev.time, NULL);
    if(write(fd, &ev, sizeof(ev)) < 0) perror("write event");
}

int main() {
    int ufd = setup_uinput();
    if(ufd < 0) return 1;

    int ifd = open(INPUT_DEV, O_RDONLY);
    if(ifd < 0) { perror("open input device"); return 1; }

    struct input_event ev;
    while(1) {
        int rd = read(ifd, &ev, sizeof(ev));
        if(rd == sizeof(ev)) {
            // Forward EV_KEY and EV_ABS events
            if(ev.type == EV_KEY || ev.type == EV_ABS) {
                emit(ufd, ev.type, ev.code, ev.value);
                emit(ufd, EV_SYN, SYN_REPORT, 0);
            }
        }
    }

    ioctl(ufd, UI_DEV_DESTROY);
    close(ufd);
    close(ifd);
    return 0;
}