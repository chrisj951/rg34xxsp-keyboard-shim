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

// Mapping hardware BTN_* codes to virtual joystick buttons 0-10
struct keymap {
    int hw_code;    // hardware BTN_* code
    int joy_code;   // uinput BTN_JOY* code
};

struct keymap keys[] = {
    {304, BTN_JOYSTICK+0},  // A
    {305, BTN_JOYSTICK+1},  // B
    {306, BTN_JOYSTICK+2},  // C
    {307, BTN_JOYSTICK+3},  // X
    {308, BTN_JOYSTICK+4},  // Y
    {309, BTN_JOYSTICK+5},  // Z
    {310, BTN_JOYSTICK+6},  // L1
    {311, BTN_JOYSTICK+7},  // R1
    {312, BTN_JOYSTICK+8},  // L2
    {313, BTN_JOYSTICK+9},  // R2
    {314, BTN_JOYSTICK+10}, // SELECT
    {315, BTN_JOYSTICK+11}, // START
    {316, BTN_JOYSTICK+12}, // HOTKEY / MENU
};

// Map ABS_HAT0X/Y to SDL2-style hat events
int hat_x = 0;
int hat_y = 0;

int setup_uinput() {
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if(fd < 0) { perror("open /dev/uinput"); return -1; }

    // Enable key events
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_EVBIT, EV_ABS);

    // Enable joystick buttons
    for(int i = 0; i < sizeof(keys)/sizeof(keys[0]); i++)
        ioctl(fd, UI_SET_KEYBIT, keys[i].joy_code);

    // Enable hat
    ioctl(fd, UI_SET_ABSBIT, ABS_HAT0X);
    ioctl(fd, UI_SET_ABSBIT, ABS_HAT0Y);

    struct uinput_user_dev uidev;
    memset(&uidev, 0, sizeof(uidev));
    snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "rg34xx-shim");
    uidev.id.bustype = BUS_USB;
    uidev.id.vendor  = 0x1234;
    uidev.id.product = 0x5678;
    uidev.id.version = 1;

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

void emit_key(int fd, int hw_code, int value) {
    for(int i = 0; i < sizeof(keys)/sizeof(keys[0]); i++) {
        if(keys[i].hw_code == hw_code) {
            emit(fd, EV_KEY, keys[i].joy_code, value);
            emit(fd, EV_SYN, SYN_REPORT, 0);
            break;
        }
    }
}

void emit_hat(int fd, int code, int value) {
    if(code == ABS_HAT0X) hat_x = value;
    if(code == ABS_HAT0Y) hat_y = value;
    emit(fd, EV_ABS, ABS_HAT0X, hat_x);
    emit(fd, EV_ABS, ABS_HAT0Y, hat_y);
    emit(fd, EV_SYN, SYN_REPORT, 0);
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
            if(ev.type == EV_KEY) {
                emit_key(ufd, ev.code, ev.value);
            } else if(ev.type == EV_ABS && (ev.code == ABS_HAT0X || ev.code == ABS_HAT0Y)) {
                emit_hat(ufd, ev.code, ev.value);
            }
        }
    }

    ioctl(ufd, UI_DEV_DESTROY);
    close(ufd);
    close(ifd);
    return 0;
}