/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2021, Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include <stdint.h>
#include <stdio.h>
#include "bsp/board.h"
#include "tusb.h"

void printBits(size_t const size, void const *const ptr)
{
    unsigned char *b = (unsigned char *)ptr;
    unsigned char byte;
    int i, j;

    for(i = size - 1; i >= 0; i--)
    {
        for(j = 7; j >= 0; j--)
        {
            byte = (b[i] >> j) & 1;
            printf("%u", byte);
        }
    }
    puts("");
}

//--------------------------------------------------------------------+
// MACRO TYPEDEF CONSTANT ENUM DECLARATION
//--------------------------------------------------------------------+

// If your host terminal support ansi escape code such as TeraTerm
// it can be use to simulate mouse cursor movement within terminal
#define USE_ANSI_ESCAPE 0

#define MAX_REPORT 4

static uint8_t const keycode2ascii[128][2] = {HID_KEYCODE_TO_ASCII};

// Each HID instance can has multiple reports
static struct
{
    uint8_t report_count;
    tuh_hid_report_info_t report_info[MAX_REPORT];
} hid_info[CFG_TUH_HID];

static void process_kbd_report(hid_keyboard_report_t const *report);
static void process_mouse_report(hid_mouse_report_t const *report);
static void process_gamepad_report(hid_gamepad_report_t const *report);
static void process_generic_report(uint8_t dev_addr, uint8_t instance,
                                   uint8_t const *report, uint16_t len);

bool is_mounted = false;
uint8_t device_address = 1;
uint8_t device_instance = 0;

void hid_app_task(void)
{
    // const uint32_t interval_ms = 1000;
    // static uint32_t start_ms = 0;
    // // Blink every interval ms
    // if(board_millis() - start_ms < interval_ms)
    // {
    //     return; // not enough time
    // }
    // start_ms += interval_ms;
    if(is_mounted)
    {
        TU_LOG3("TU_LOG3 Send new report request.\n");
        // printf("Request report from %d %d\n", device_address, device_instance);
        if(!tuh_hid_receive_report(device_address, device_instance))
        {
            printf("Error: cannot request to receive report\r\n");
        }
    }
}

//--------------------------------------------------------------------+
// TinyUSB Callbacks
//--------------------------------------------------------------------+

// Invoked when device with hid interface is mounted
// Report descriptor is also available for use.
// tuh_hid_parse_report_descriptor() can be used to parse common/simple enough
// descriptor. Note: if report descriptor length > CFG_TUH_ENUMERATION_BUFSIZE,
// it will be skipped therefore report_desc = NULL, desc_len = 0
void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance,
                      uint8_t const *desc_report, uint16_t desc_len)
{
    const uint8_t instance_count = tuh_hid_instance_count(dev_addr);

    TU_LOG3("HID device address = %d, instance = %d, number of instances = %d "
            "is mounted\r\n",
            dev_addr, instance, instance_count);

    // Interface protocol (hid_interface_protocol_enum_t)
    const char *protocol_str[] = {"None", "Keyboard", "Mouse"};
    uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);

    TU_LOG3("HID Interface Protocol = %s\r\n", protocol_str[itf_protocol]);

    // By default host stack will use activate boot protocol on supported
    // interface. Therefore for this simple example, we only need to parse
    // generic report descriptor (with built-in parser)
    if(itf_protocol == HID_ITF_PROTOCOL_NONE)
    {
        hid_info[instance].report_count = tuh_hid_parse_report_descriptor(
            hid_info[instance].report_info, MAX_REPORT, desc_report, desc_len);
        TU_LOG3("HID has %u reports \r\n", hid_info[instance].report_count);
        TU_LOG3("HID report has report_id = %d, usage = %d, usage_page = %d.\n",
                hid_info[instance].report_info->report_id,
                hid_info[instance].report_info->usage,
                hid_info[instance].report_info->usage_page);
    }

    // request to receive report
    // tuh_hid_report_received_cb() will be invoked when report is available
    if(!tuh_hid_receive_report(dev_addr, instance))
    {
        printf("Error: cannot request to receive report\r\n");
    }

    device_address = dev_addr;
    device_instance = instance;
    is_mounted = true;
}

// Invoked when device with hid interface is un-mounted
void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance)
{
    printf("HID device address = %d, instance = %d is unmounted\r\n", dev_addr,
           instance);
    is_mounted = false;
}

// Invoked when received report from device via interrupt endpoint
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance,
                                uint8_t const *report, uint16_t len)
{
    // printf("Received report from %d %d\n", dev_addr, instance);
    uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);
    switch(itf_protocol)
    {
        case HID_ITF_PROTOCOL_KEYBOARD:
            TU_LOG2("HID receive boot keyboard report\r\n");
            process_kbd_report((hid_keyboard_report_t const *)report);
            break;

        case HID_ITF_PROTOCOL_MOUSE:
            TU_LOG2("HID receive boot mouse report\r\n");
            process_mouse_report((hid_mouse_report_t const *)report);
            break;

        default:
            // Generic report requires matching ReportID and contents with
            // previous parsed report info
            process_generic_report(dev_addr, instance, report, len);
            break;
    }
    // continue to request to receive report
    // if(!tuh_hid_receive_report(dev_addr, instance))
    // {
    //     printf("Error: cannot request to receive report\r\n");
    // }
}

//--------------------------------------------------------------------+
// Keyboard
//--------------------------------------------------------------------+

// look up new key in previous keys
static inline bool find_key_in_report(hid_keyboard_report_t const *report,
                                      uint8_t keycode)
{
    for(uint8_t i = 0; i < 6; i++)
    {
        if(report->keycode[i] == keycode)
        {
            return true;
        }
    }

    return false;
}

static void process_kbd_report(hid_keyboard_report_t const *report)
{
    static hid_keyboard_report_t prev_report = {
        0, 0, {0}}; // previous report to check key released

    //------------- example code ignore control (non-printable) key affects
    //-------------//
    for(uint8_t i = 0; i < 6; i++)
    {
        if(report->keycode[i])
        {
            if(find_key_in_report(&prev_report, report->keycode[i]))
            {
                // exist in previous report means the current key is holding
            }
            else
            {
                // not existed in previous report means the current key is
                // pressed
                bool const is_shift = report->modifier
                                      & (KEYBOARD_MODIFIER_LEFTSHIFT
                                         | KEYBOARD_MODIFIER_RIGHTSHIFT);
                uint8_t ch =
                    keycode2ascii[report->keycode[i]][is_shift ? 1 : 0];
                putchar(ch);
                if(ch == '\r')
                {
                    putchar('\n'); // added new line for enter key
                }

                fflush(stdout); // flush right away, else nanolib will wait for
                                // newline
            }
        }
        // TODO example skips key released
    }

    prev_report = *report;
}

//--------------------------------------------------------------------+
// Mouse
//--------------------------------------------------------------------+

void cursor_movement(int8_t x, int8_t y, int8_t wheel)
{
#if USE_ANSI_ESCAPE
    // Move X using ansi escape
    if(x < 0)
    {
        printf(ANSI_CURSOR_BACKWARD(% d), (-x)); // move left
    }
    else if(x > 0)
    {
        printf(ANSI_CURSOR_FORWARD(% d), x); // move right
    }

    // Move Y using ansi escape
    if(y < 0)
    {
        printf(ANSI_CURSOR_UP(% d), (-y)); // move up
    }
    else if(y > 0)
    {
        printf(ANSI_CURSOR_DOWN(% d), y); // move down
    }

    // Scroll using ansi escape
    if(wheel < 0)
    {
        printf(ANSI_SCROLL_UP(% d), (-wheel)); // scroll up
    }
    else if(wheel > 0)
    {
        printf(ANSI_SCROLL_DOWN(% d), wheel); // scroll down
    }

    printf("\r\n");
#else
    printf("(%d %d %d)\r\n", x, y, wheel);
#endif
}

static void process_mouse_report(hid_mouse_report_t const *report)
{
    static hid_mouse_report_t prev_report = {0};

    //------------- button state  -------------//
    uint8_t button_changed_mask = report->buttons ^ prev_report.buttons;
    if(button_changed_mask & report->buttons)
    {
        printf(" %c%c%c ", report->buttons & MOUSE_BUTTON_LEFT ? 'L' : '-',
               report->buttons & MOUSE_BUTTON_MIDDLE ? 'M' : '-',
               report->buttons & MOUSE_BUTTON_RIGHT ? 'R' : '-');
    }

    //------------- cursor movement -------------//
    cursor_movement(report->x, report->y, report->wheel);
}

//--------------------------------------------------------------------+
// Gamepad
//--------------------------------------------------------------------+

static inline void process_hat(const uint8_t hat)
{
    static uint8_t prev_hat = 0;
    if(prev_hat == hat)
    {
        return;
    }

    printBits(sizeof(hat), &hat);

    prev_hat = hat;
}

static inline void process_buttons(const uint32_t buttons)
{
    static uint32_t prev_buttons = 0;
    if(prev_buttons == buttons)
    {
        return;
    }

    printBits(sizeof(buttons), &buttons);

    prev_buttons = buttons;
}

static void process_gamepad_report(hid_gamepad_report_t const *report)
{
    printf("\nProcess new gamepad Report.\n");
    printf("Delta x movement = %d\n", report->x);
    printf("Delta y movement = %d\n", report->y);
    printf("Delta z movement = %d\n", report->z);
    printf("Delta rx movement = %d\n", report->rx);
    printf("Delta ry movement = %d\n", report->ry);
    printf("Delta rz movement = %d\n", report->rz);
    printf("Hat = %02X\n", report->hat);
    printf("Buttons = %08X\n", report->buttons);

    fflush(stdout);

    // process_hat(report->hat);

    // process_buttons(report->buttons);
}

static void print_report(uint8_t const *report, uint16_t len) {
    static uint8_t buffer[40];
    printf("New report:\n");
    for (int i = 0; i < len; i++) {
        if (buffer[i] != report[i]) {
            printf("%d:%02X ", i, report[i]);
        }
        if (i == 20) {
            printf("\n");
        }
        buffer[i] = report[i];
    }
    printf("\n");
}

//--------------------------------------------------------------------+
// Generic Report
//--------------------------------------------------------------------+
static void process_generic_report(uint8_t dev_addr, uint8_t instance,
                                   uint8_t const *report, uint16_t len)
{
    (void)dev_addr;

    uint8_t const rpt_count = hid_info[instance].report_count;
    tuh_hid_report_info_t *rpt_info_arr = hid_info[instance].report_info;
    tuh_hid_report_info_t *rpt_info = NULL;

    if(rpt_count == 1 && rpt_info_arr[0].report_id == 0)
    {
        // Simple report without report ID as 1st byte
        rpt_info = &rpt_info_arr[0];
    }
    else
    {
        // Composite report, 1st byte is report ID, data starts from 2nd byte
        uint8_t const rpt_id = report[0];

        // Find report id in the array
        for(uint8_t i = 0; i < rpt_count; i++)
        {
            if(rpt_id == rpt_info_arr[i].report_id)
            {
                rpt_info = &rpt_info_arr[i];
                break;
            }
        }

        report++;
        len--;
    }

    if(!rpt_info)
    {
        printf("Couldn't find the report info for this report !\r\n");
        return;
    }

    // For complete list of Usage Page & Usage checkout src/class/hid/hid.h. For
    // examples:
    // - Keyboard                     : Desktop, Keyboard
    // - Mouse                        : Desktop, Mouse
    // - Gamepad                      : Desktop, Gamepad
    // - Consumer Control (Media Key) : Consumer, Consumer Control
    // - System Control (Power key)   : Desktop, System Control
    // - Generic (vendor)             : 0xFFxx, xx
    if(rpt_info->usage_page == HID_USAGE_PAGE_DESKTOP)
    {
        switch(rpt_info->usage)
        {
            case HID_USAGE_DESKTOP_KEYBOARD:
                TU_LOG1("HID receive keyboard report\r\n");
                // Assume keyboard follow boot report layout
                process_kbd_report((hid_keyboard_report_t const *)report);
                break;

            case HID_USAGE_DESKTOP_MOUSE:
                TU_LOG1("HID receive mouse report\r\n");
                // Assume mouse follow boot report layout
                process_mouse_report((hid_mouse_report_t const *)report);
                break;

            case HID_USAGE_DESKTOP_GAMEPAD:
                // process_gamepad_report((hid_gamepad_report_t const *)report);
                print_report(report, len);
                break;

            default:
                break;
        }
    }
}
