/*  OnePAD - author: arcum42(@gmail.com)
 *  Copyright (C) 2009
 *
 *  Based on ZeroPAD, author zerofrog@gmail.com
 *  Copyright (C) 2006-2007
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

/*
  * Theoretically, this header is for anything to do with keyboard input.
  * Pragmatically, event handing's going in here too.
  */

#if defined(__unix__)
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#elif defined(__APPLE__)
#include <Carbon/Carbon.h>
#endif

#include "keyboard.h"

#ifdef _WIN32
char *KeysymToChar(int keysym)
{
    LPWORD temp;

    ToAscii((UINT)keysym, NULL, NULL, temp, NULL);
    return (char *)temp;
}
#endif

/// g_key_status.press but with proper handling for analog buttons
static void PressButton(u32 pad, u32 button) {
    // Analog controls.
    if (IsAnalogKey(button)) {
        switch (button) {
            case PAD_R_LEFT:
            case PAD_R_UP:
            case PAD_L_LEFT:
            case PAD_L_UP:
                g_key_status.press(pad, button, -MAX_ANALOG_VALUE);
                break;
            case PAD_R_RIGHT:
            case PAD_R_DOWN:
            case PAD_L_RIGHT:
            case PAD_L_DOWN:
                g_key_status.press(pad, button, MAX_ANALOG_VALUE);
                break;
        }
    } else {
        g_key_status.press(pad, button);
    }
}

#if defined(__APPLE__)
// Mac keyboard input code is based on Dolphin's Source/Core/InputCommon/ControllerInterface/Quartz/QuartzKeyboardAndMouse.mm
// Copyright 2016 Dolphin Emulator Project
// Licensed under GPLv2+

// All keycodes are 16 bits or less, so we use top 16 bits to differentiate source
// Keyboard keys use discriminator 0
// Mouse buttons use discriminator 1

void UpdateKeyboardInput() {
    for (int pad = 0; pad < GAMEPAD_NUMBER; pad++) {
        const auto& map = g_conf.keysym_map[pad];
        // If we loop over all keys press/release based on current state,
        // joystick axes (which have two bound keys) will always go to the later-polled key
        // Instead, release all keys first and then set the ones that are pressed
        for (const auto& key : map) g_key_status.release(pad, key.second);
        for (const auto& key : map) {
            bool state;
            if (key.first >> 16 == 0) {
                state = CGEventSourceKeyState(kCGEventSourceStateHIDSystemState, key.first);
            } else {
                state = CGEventSourceButtonState(kCGEventSourceStateHIDSystemState, (CGMouseButton)(key.first & 0xFFFF));
            }
            if (state) PressButton(pad, key.second);
        }
    }
}

bool PollForNewKeyboardKeys(u32 &pkey) {
    // All keycodes in <HIToolbox/Events.h> are 0x7e or lower. If you notice
    // keys that aren't being recognized, bump this number up!
    for (int key = 0; key < 0x80; key++) {
        if (CGEventSourceKeyState(kCGEventSourceStateHIDSystemState, key)) {
            pkey = key == kVK_Escape ? 0 : key;
            return true;
        }
    }
    for (auto btn : {kCGMouseButtonLeft, kCGMouseButtonCenter, kCGMouseButtonRight}) {
        if (CGEventSourceButtonState(kCGEventSourceStateHIDSystemState, btn)) {
            pkey = btn | (1 << 16);
            return true;
        }
    }
    return false;
}
#elif defined(__unix__)
static bool s_grab_input = false;
static bool s_Shift = false;
static unsigned int s_previous_mouse_x = 0;
static unsigned int s_previous_mouse_y = 0;

void AnalyzeKeyEvent(keyEvent &evt)
{
    KeySym key = (KeySym)evt.key;
    int pad = 0;
    int index = -1;

    for (int cpad = 0; cpad < GAMEPAD_NUMBER; cpad++) {
        int tmp_index = get_keyboard_key(cpad, key);
        if (tmp_index != -1) {
            pad = cpad;
            index = tmp_index;
        }
    }

    switch (evt.evt) {
        case KeyPress:
            // Shift F12 is not yet use by pcsx2. So keep it to grab/ungrab input
            // I found it very handy vs the automatic fullscreen detection
            // 1/ Does not need to detect full-screen
            // 2/ Can use a debugger in full-screen
            // 3/ Can grab input in window without the need of a pixelated full-screen
            if (key == XK_Shift_R || key == XK_Shift_L)
                s_Shift = true;
            if (key == XK_F12 && s_Shift) {
                if (!s_grab_input) {
                    s_grab_input = true;
                    XGrabPointer(GSdsp, GSwin, True, ButtonPressMask, GrabModeAsync, GrabModeAsync, GSwin, None, CurrentTime);
                    XGrabKeyboard(GSdsp, GSwin, True, GrabModeAsync, GrabModeAsync, CurrentTime);
                } else {
                    s_grab_input = false;
                    XUngrabPointer(GSdsp, CurrentTime);
                    XUngrabKeyboard(GSdsp, CurrentTime);
                }
            }

            if (index != -1)
                PressButton(pad, index);

            //PAD_LOG("Key pressed:%d\n", index);

            event.evt = KEYPRESS;
            event.key = key;
            break;

        case KeyRelease:
            if (key == XK_Shift_R || key == XK_Shift_L)
                s_Shift = false;

            if (index != -1)
                g_key_status.release(pad, index);

            event.evt = KEYRELEASE;
            event.key = key;
            break;

        case FocusIn:
            break;

        case FocusOut:
            s_Shift = false;
            break;

        case ButtonPress:
            if (index != -1)
                g_key_status.press(pad, index);
            break;

        case ButtonRelease:
            if (index != -1)
                g_key_status.release(pad, index);
            break;

        case MotionNotify:
            // FIXME: How to handle when the mouse does not move, no event generated!!!
            // 1/ small move == no move. Cons : can not do small movement
            // 2/ use a watchdog timer thread
            // 3/ ??? idea welcome ;)
            if (g_conf.pad_options[pad].mouse_l | g_conf.pad_options[pad].mouse_r) {
                unsigned int pad_x;
                unsigned int pad_y;
                // Note when both PADOPTION_MOUSE_R and PADOPTION_MOUSE_L are set, take only the right one
                if (g_conf.pad_options[pad].mouse_r) {
                    pad_x = PAD_R_RIGHT;
                    pad_y = PAD_R_UP;
                } else {
                    pad_x = PAD_L_RIGHT;
                    pad_y = PAD_L_UP;
                }

                unsigned x = evt.key & 0xFFFF;
                unsigned int value = (s_previous_mouse_x > x) ? s_previous_mouse_x - x : x - s_previous_mouse_x;
                value *= g_conf.get_sensibility();

                if (x == 0)
                    g_key_status.press(pad, pad_x, -MAX_ANALOG_VALUE);
                else if (x == 0xFFFF)
                    g_key_status.press(pad, pad_x, MAX_ANALOG_VALUE);
                else if (x < (s_previous_mouse_x - 2))
                    g_key_status.press(pad, pad_x, -value);
                else if (x > (s_previous_mouse_x + 2))
                    g_key_status.press(pad, pad_x, value);
                else
                    g_key_status.release(pad, pad_x);


                unsigned y = evt.key >> 16;
                value = (s_previous_mouse_y > y) ? s_previous_mouse_y - y : y - s_previous_mouse_y;
                value *= g_conf.get_sensibility();

                if (y == 0)
                    g_key_status.press(pad, pad_y, -MAX_ANALOG_VALUE);
                else if (y == 0xFFFF)
                    g_key_status.press(pad, pad_y, MAX_ANALOG_VALUE);
                else if (y < (s_previous_mouse_y - 2))
                    g_key_status.press(pad, pad_y, -value);
                else if (y > (s_previous_mouse_y + 2))
                    g_key_status.press(pad, pad_y, value);
                else
                    g_key_status.release(pad, pad_y);

                s_previous_mouse_x = x;
                s_previous_mouse_y = y;
            }

            break;
    }
}

void UpdateKeyboardInput()
{
    keyEvent evt = {0};
    XEvent E = {0};

    // Keyboard input send by PCSX2
    g_ev_fifo.consume_all(AnalyzeKeyEvent);

    // keyboard input
    while (XPending(GSdsp) > 0) {
        XNextEvent(GSdsp, &E);

        // Change the format of the structure to be compatible with GSOpen2
        // mode (event come from pcsx2 not X)
        evt.evt = E.type;
        switch (E.type) {
            case MotionNotify:
                evt.key = (E.xbutton.x & 0xFFFF) | (E.xbutton.y << 16);
                break;
            case ButtonRelease:
            case ButtonPress:
                evt.key = E.xbutton.button;
                break;
            default:
                evt.key = (int)XLookupKeysym(&E.xkey, 0);
        }

        AnalyzeKeyEvent(evt);
    }
}

bool PollForNewKeyboardKeys(u32 &pkey)
{
    GdkEvent *ev = gdk_event_get();

    if (ev != NULL) {
        if (ev->type == GDK_KEY_PRESS) {
            pkey = ev->key.keyval != GDK_KEY_Escape ? ev->key.keyval : 0;
            return true;
        } else if (ev->type == GDK_BUTTON_PRESS) {
            pkey = ev->button.button;
            return true;
        }
    }

    return false;
}

#else
LRESULT WINAPI PADwndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static bool lbutton = false, rbutton = false;
    for (int pad = 0; pad < GAMEPAD_NUMBER; ++pad) 
    {
        g_key_status.keyboard_state_acces(pad);
    }

    switch (msg) {
        case WM_KEYDOWN:
            if (lParam & 0x40000000)
                return TRUE;

            for (int pad = 0; pad < GAMEPAD_NUMBER; ++pad) {
                for (int i = 0; i < MAX_KEYS; i++) {
                    assert(0);
#if 0
                    if (wParam == get_key(pad, i)) {
                        g_key_status.press(pad, i);
                        break;
                    }
#endif
                }
            }

            event.evt = KEYPRESS;
            event.key = wParam;
            break;

        case WM_KEYUP:
            for (int pad = 0; pad < GAMEPAD_NUMBER; ++pad) {
                for (int i = 0; i < MAX_KEYS; i++) {
                    assert(0);
#if 0
                    if (wParam == get_key(pad, i)) {
                        g_key_status.release(pad, i);
                        break;
                    }
#endif
                }
            }


            event.evt = KEYRELEASE;
            event.key = wParam;
            break;

        case WM_DESTROY:
        case WM_QUIT:
            event.evt = KEYPRESS;
            event.key = VK_ESCAPE;
            return GSwndProc(hWnd, msg, wParam, lParam);

        default:
            return GSwndProc(hWnd, msg, wParam, lParam);
    }

    for (int pad = 0; pad < GAMEPAD_NUMBER; ++pad)
        g_key_status.commit_status(pad);

    return TRUE;
}
#endif