#include <string.h>
#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>
#include "version.h"

// Uncomment to be able to make a screenshot
//#define USB_HID_AUTOFIRE_SCREENSHOT

typedef enum {
    EventTypeInput,
} EventType;

typedef struct {
    union {
        InputEvent input;
    };
    EventType type;
} UsbMouseEvent;

bool btn_left_autofire = false;
uint32_t autofire_delay = 60;
uint32_t secondClick = 0;

// Timer variables for non-blocking delays
uint32_t last_click_time = 0;
bool mouse_pressed = false;
uint32_t mouse_press_time = 0;
uint32_t pause_until = 0;

static void usb_hid_autofire_render_callback(Canvas* canvas, void* ctx) {
    UNUSED(ctx);
    char autofire_delay_str[12];
    itoa(autofire_delay, autofire_delay_str, 10);

    canvas_clear(canvas);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 10, "USB HID Autofire");
    canvas_draw_str(canvas, 0, 34, btn_left_autofire ? "<active>" : "<inactive>");

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 90, 10, "v");
    canvas_draw_str(canvas, 96, 10, VERSION);
    canvas_draw_str(canvas, 0, 22, "Press [ok] for auto left clicking");
    canvas_draw_str(canvas, 0, 46, "delay [s]:");
    canvas_draw_str(canvas, 50, 46, autofire_delay_str);
    canvas_draw_str(canvas, 0, 63, "Press [back] to exit");
}

static void usb_hid_autofire_input_callback(InputEvent* input_event, void* ctx) {
    FuriMessageQueue* event_queue = ctx;

    UsbMouseEvent event;
    event.type = EventTypeInput;
    event.input = *input_event;
    furi_message_queue_put(event_queue, &event, FuriWaitForever);
}

int32_t usb_hid_autofire_app(void* p) {
    UNUSED(p);
    FuriMessageQueue* event_queue = furi_message_queue_alloc(8, sizeof(UsbMouseEvent));
    furi_check(event_queue);
    ViewPort* view_port = view_port_alloc();

    FuriHalUsbInterface* usb_mode_prev = furi_hal_usb_get_config();
#ifndef USB_HID_AUTOFIRE_SCREENSHOT
    furi_hal_usb_unlock();
    furi_check(furi_hal_usb_set_config(&usb_hid, NULL) == true);
#endif

    view_port_draw_callback_set(view_port, usb_hid_autofire_render_callback, NULL);
    view_port_input_callback_set(view_port, usb_hid_autofire_input_callback, event_queue);

    // Open GUI and register view_port
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    UsbMouseEvent event;
    while(1) {
        FuriStatus event_status = furi_message_queue_get(event_queue, &event, 50);
        uint32_t current_time = furi_get_tick();

        if(event_status == FuriStatusOk) {
            if(event.type == EventTypeInput) {
                if(event.input.key == InputKeyBack) {
                    break;
                }

                if(event.input.type != InputTypeRelease) {
                    continue;
                }

                switch(event.input.key) {
                case InputKeyOk:
                    btn_left_autofire = !btn_left_autofire;
                    // Reset timer states when toggling
                    if(btn_left_autofire) {
                        last_click_time = current_time;
                        mouse_pressed = false;
                        secondClick = 0;
                        pause_until = 0;
                    } else {
                        // Release mouse if it's currently pressed
                        if(mouse_pressed) {
                            furi_hal_hid_mouse_release(HID_MOUSE_BTN_LEFT);
                            mouse_pressed = false;
                        }
                    }
                    break;
                case InputKeyLeft:
                    if(autofire_delay > 0) {
                        autofire_delay -= 60;
                    }
                    break;
                case InputKeyRight:
                    autofire_delay += 60;
                    break;
                default:
                    break;
                }
            }
        }

        // Non-blocking autofire logic
        if(btn_left_autofire && autofire_delay > 0) {
            // Check if we're in a pause period (after every 3rd click)
            if(pause_until > 0 && current_time < pause_until) {
                // Still in pause, do nothing
            } else if(pause_until > 0) {
                // Pause period ended, reset
                pause_until = 0;
                secondClick = 0;
                last_click_time = current_time;
            } else if(!mouse_pressed) {
                // Time to press mouse
                uint32_t delay_ms = (autofire_delay * 1000) / 2; // Half the delay for press duration
                if(current_time - last_click_time >= delay_ms) {
                    furi_hal_hid_mouse_press(HID_MOUSE_BTN_LEFT);
                    mouse_pressed = true;
                    mouse_press_time = current_time;
                }
            } else {
                // Mouse is pressed, check if it's time to release
                uint32_t delay_ms = (autofire_delay * 1000) / 2; // Half the delay for press duration
                if(current_time - mouse_press_time >= delay_ms) {
                    furi_hal_hid_mouse_release(HID_MOUSE_BTN_LEFT);
                    mouse_pressed = false;
                    last_click_time = current_time;
                    secondClick++;
                }
            }
        }

        view_port_update(view_port);
    }

    // Clean up: release mouse if it's pressed when exiting
    if(mouse_pressed) {
        furi_hal_hid_mouse_release(HID_MOUSE_BTN_LEFT);
    }

    furi_hal_usb_set_config(usb_mode_prev, NULL);

    // remove & free all stuff created by app
    gui_remove_view_port(gui, view_port);
    view_port_free(view_port);
    furi_message_queue_free(event_queue);

    return 0;
}
