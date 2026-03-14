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

#define INPUT_DEV "/dev/input/event1"  // your source device

void emit(int fd, int type, int code, int value)
{
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    gettimeofday(&ev.time, NULL);

    ev.type = type;
    ev.code = code;
    ev.value = value;

    if (write(fd, &ev, sizeof(ev)) < 0)
        perror("write event");
}

int setup_uinput()
{
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("open /dev/uinput");
        return -1;
    }

    // Event types
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_EVBIT, EV_ABS);
    ioctl(fd, UI_SET_EVBIT, EV_SYN);

    int buttons[] = {
        BTN_SOUTH,   // A
        BTN_EAST,    // B
        BTN_WEST,    // X
        BTN_NORTH,   // Y
        BTN_TL,      // LB
        BTN_TR,      // RB
        BTN_TL2,     // L2
        BTN_TR2,     // R2
        BTN_SELECT,  // Back
        BTN_START,   // Start
        BTN_MODE,    // Guide
        BTN_THUMBL,  // L3
        BTN_THUMBR,  // R3
        BTN_DPAD_UP, 
        BTN_DPAD_DOWN, 
        BTN_DPAD_LEFT, 
        BTN_DPAD_RIGHT
    };

    for (size_t i = 0; i < sizeof(buttons)/sizeof(buttons[0]); i++)
        ioctl(fd, UI_SET_KEYBIT, buttons[i]);

    // Axes (sticks + triggers + D-pad)
    int axes[] = { ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_Z, ABS_RZ, ABS_HAT0X, ABS_HAT0Y };
    for (size_t i = 0; i < sizeof(axes)/sizeof(axes[0]); i++)
        ioctl(fd, UI_SET_ABSBIT, axes[i]);

    struct uinput_user_dev uidev;
    memset(&uidev, 0, sizeof(uidev));

    snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "Microsoft X-Box 360 pad");
    uidev.id.bustype = BUS_USB;
    uidev.id.vendor  = 0x045e;
    uidev.id.product = 0x028e;
    uidev.id.version = 0x0114;

    // Left stick
    uidev.absmin[ABS_X] = -32768; uidev.absmax[ABS_X] = 32767;
    uidev.absmin[ABS_Y] = -32768; uidev.absmax[ABS_Y] = 32767;

    // Right stick
    uidev.absmin[ABS_RX] = -32768; uidev.absmax[ABS_RX] = 32767;
    uidev.absmin[ABS_RY] = -32768; uidev.absmax[ABS_RY] = 32767;

    // Triggers
    uidev.absmin[ABS_Z] = 0; uidev.absmax[ABS_Z] = 255;
    uidev.absmin[ABS_RZ] = 0; uidev.absmax[ABS_RZ] = 255;

    // D-pad axes
    uidev.absmin[ABS_HAT0X] = -1; uidev.absmax[ABS_HAT0X] = 1;
    uidev.absmin[ABS_HAT0Y] = -1; uidev.absmax[ABS_HAT0Y] = 1;

    if (write(fd, &uidev, sizeof(uidev)) < 0) {
        perror("write uinput_user_dev");
        close(fd);
        return -1;
    }

    if (ioctl(fd, UI_DEV_CREATE) < 0) {
        perror("UI_DEV_CREATE");
        close(fd);
        return -1;
    }

    sleep(1); // allow device node creation
    return fd;
}

int main()
{
    int ufd = setup_uinput();
    if (ufd < 0) return 1;

    int ifd = open(INPUT_DEV, O_RDONLY);
    if (ifd < 0) {
        perror("open input device");
        return 1;
    }

    struct input_event ev;

    while (1) {
        int rd = read(ifd, &ev, sizeof(ev));
        if (rd != sizeof(ev)) continue;

        if (ev.type == EV_KEY) {
            int code = ev.code;

            // Remap non-standard source keys to Xbox 360 layout
            switch (ev.code) {
                case BTN_SOUTH: code = BTN_EAST; break;  //A
                case BTN_EAST:  code = BTN_SOUTH; break;  //B
                case BTN_C:     code = BTN_WEST;  break;  //Y
                case BTN_NORTH: code = BTN_NORTH; break;  //X
                case BTN_WEST:  code = BTN_TL;   break;  //LB
                case BTN_SELECT: code = BTN_TL2; break;  //L2
                case BTN_Z:     code = BTN_TR;   break;  //RB
                case BTN_START: code = BTN_TR2;  break;  //R2
                case BTN_TR:    code = BTN_START; break; //Start
                case BTN_TL:    code = BTN_SELECT; break; //Select
                case KEY_GOTO:  code = BTN_MODE; break;   //Guide
            }
            emit(ufd, EV_KEY, code, ev.value);
        }
        else if (ev.type == EV_ABS) {
            switch (ev.code) {
                case ABS_X:
                case ABS_Y:
                case ABS_RX:
                case ABS_RY:
                case ABS_Z:
                case ABS_RZ:
                    emit(ufd, EV_ABS, ev.code, ev.value);
                    break;

                case ABS_HAT0X:
                    emit(ufd, EV_ABS, ABS_HAT0X, ev.value);
                    emit(ufd, EV_KEY, BTN_DPAD_LEFT,  (ev.value < 0) ? 1 : 0);
                    emit(ufd, EV_KEY, BTN_DPAD_RIGHT, (ev.value > 0) ? 1 : 0);
                    break;

                case ABS_HAT0Y:
                    emit(ufd, EV_ABS, ABS_HAT0Y, ev.value);
                    emit(ufd, EV_KEY, BTN_DPAD_UP,   (ev.value < 0) ? 1 : 0);
                    emit(ufd, EV_KEY, BTN_DPAD_DOWN, (ev.value > 0) ? 1 : 0);
                    break;
            }
        }

        emit(ufd, EV_SYN, SYN_REPORT, 0);
    }

    ioctl(ufd, UI_DEV_DESTROY);
    close(ufd);
    close(ifd);
    return 0;
}