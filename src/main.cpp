/*
 * Copyright (C) 2017 FIX94
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include "main.h"

#include <cstdio>
#include <cstdlib>

#include <avm/tv.h>
#include <coreinit/foreground.h>
#include <coreinit/memfrmheap.h>
#include <coreinit/screen.h>
#include <coreinit/thread.h>
#include <coreinit/title.h>
#include <padscore/kpad.h>
#include <proc_ui/procui.h>
#include <sndcore2/core.h>
#include <sysapp/launch.h>
#include <vpad/input.h>

#define CONSOLE_FRAME_HEAP_TAG (0x000DECAF)

#define OSScreenEnable(enable)      OSScreenEnableEx(SCREEN_TV, enable); OSScreenEnableEx(SCREEN_DRC, enable);
#define OSScreenClearBuffer(colour) OSScreenClearBufferEx(SCREEN_TV, colour); OSScreenClearBufferEx(SCREEN_DRC, colour);
#define OSScreenPutFont(x, y, buf)  OSScreenPutFontEx(SCREEN_TV, x, y, buf); OSScreenPutFontEx(SCREEN_DRC, x, y, buf);
#define OSScreenFlipBuffers()       OSScreenFlipBuffersEx(SCREEN_TV); OSScreenFlipBuffersEx(SCREEN_DRC);

static const char *verStr = "Wii U Video Mode Changer 2025 Port v2.0";
static const char *authorStr = "By Lynx64. Original by FIX94.";

static const char * const outPortStr[] = {"HDMI", "Component", "Composite/S-Video", "Composite/SCART"};
static const char * const outResStr[] = {"Unused [0]", "576i PAL50", "480i", "480p", "720p", "720p 3D?", "1080i", "1080p", "Unused [8]", "Unused [9]", "480i PAL60", "576p", "720p 50Hz (glitchy GamePad)", "1080i 50Hz (glitchy GamePad)", "1080p 50Hz (glitchy GamePad)"};

static unsigned int sScreenBufTvSize = 0;
static unsigned int sScreenBufDrcSize = 0;
static void *sScreenBufTv = nullptr;
static void *sScreenBufDrc = nullptr;

static unsigned int wpadToVpad(unsigned int buttons)
{
    unsigned int convButtons = 0;

    if(buttons & WPAD_BUTTON_LEFT)
        convButtons |= VPAD_BUTTON_LEFT;

    if(buttons & WPAD_BUTTON_RIGHT)
        convButtons |= VPAD_BUTTON_RIGHT;

    if(buttons & WPAD_BUTTON_DOWN)
        convButtons |= VPAD_BUTTON_DOWN;

    if(buttons & WPAD_BUTTON_UP)
        convButtons |= VPAD_BUTTON_UP;

    if(buttons & WPAD_BUTTON_PLUS)
        convButtons |= VPAD_BUTTON_PLUS;

    if(buttons & WPAD_BUTTON_B)
        convButtons |= VPAD_BUTTON_B;

    if(buttons & WPAD_BUTTON_A)
        convButtons |= VPAD_BUTTON_A;

    if(buttons & WPAD_BUTTON_MINUS)
        convButtons |= VPAD_BUTTON_MINUS;

    if(buttons & WPAD_BUTTON_HOME)
        convButtons |= VPAD_BUTTON_HOME;

    return convButtons;
}

static unsigned int wpadClassicToVpad(unsigned int buttons)
{
    unsigned int convButtons = 0;

    if(buttons & WPAD_CLASSIC_BUTTON_LEFT)
        convButtons |= VPAD_BUTTON_LEFT;

    if(buttons & WPAD_CLASSIC_BUTTON_RIGHT)
        convButtons |= VPAD_BUTTON_RIGHT;

    if(buttons & WPAD_CLASSIC_BUTTON_DOWN)
        convButtons |= VPAD_BUTTON_DOWN;

    if(buttons & WPAD_CLASSIC_BUTTON_UP)
        convButtons |= VPAD_BUTTON_UP;

    if(buttons & WPAD_CLASSIC_BUTTON_PLUS)
        convButtons |= VPAD_BUTTON_PLUS;

    if(buttons & WPAD_CLASSIC_BUTTON_X)
        convButtons |= VPAD_BUTTON_X;

    if(buttons & WPAD_CLASSIC_BUTTON_Y)
        convButtons |= VPAD_BUTTON_Y;

    if(buttons & WPAD_CLASSIC_BUTTON_B)
        convButtons |= VPAD_BUTTON_B;

    if(buttons & WPAD_CLASSIC_BUTTON_A)
        convButtons |= VPAD_BUTTON_A;

    if(buttons & WPAD_CLASSIC_BUTTON_MINUS)
        convButtons |= VPAD_BUTTON_MINUS;

    if(buttons & WPAD_CLASSIC_BUTTON_HOME)
        convButtons |= VPAD_BUTTON_HOME;

    if(buttons & WPAD_CLASSIC_BUTTON_ZR)
        convButtons |= VPAD_BUTTON_ZR;

    if(buttons & WPAD_CLASSIC_BUTTON_ZL)
        convButtons |= VPAD_BUTTON_ZL;

    if(buttons & WPAD_CLASSIC_BUTTON_R)
        convButtons |= VPAD_BUTTON_R;

    if(buttons & WPAD_CLASSIC_BUTTON_L)
        convButtons |= VPAD_BUTTON_L;

    return convButtons;
}

static unsigned int getButtonsDown()
{
    unsigned int btnDown = 0;

    VPADStatus vpadStatus{};
    VPADReadError vpadError = VPAD_READ_UNINITIALIZED;
    if (VPADRead(VPAD_CHAN_0, &vpadStatus, 1, &vpadError) > 0 && vpadError == VPAD_READ_SUCCESS)
        btnDown = vpadStatus.trigger;

    KPADStatus kpadStatus{};
    KPADError kpadError = KPAD_ERROR_UNINITIALIZED;
    for (int chan = 0; chan < 4; chan++) {
        if (KPADReadEx((KPADChan) chan, &kpadStatus, 1, &kpadError) > 0) {
            if (kpadError == KPAD_ERROR_OK && kpadStatus.extensionType != 0xFF) {
                if (kpadStatus.extensionType == WPAD_EXT_CORE || kpadStatus.extensionType == WPAD_EXT_NUNCHUK ||
                    kpadStatus.extensionType == WPAD_EXT_MPLUS || kpadStatus.extensionType == WPAD_EXT_MPLUS_NUNCHUK) {
                    btnDown |= wpadToVpad(kpadStatus.trigger);
                } else {
                    btnDown |= wpadClassicToVpad(kpadStatus.classic.trigger);
                }
            }
        }
    }

    return btnDown;
}

void procUiSaveCallback()
{
    OSSavesDone_ReadyToRelease();
}

unsigned int procUiCallbackAcquire([[maybe_unused]] void *context)
{
    MEMHeapHandle heap = MEMGetBaseHeapHandle(MEM_BASE_HEAP_MEM1);
    sScreenBufTv = MEMAllocFromFrmHeapEx(heap, sScreenBufTvSize, 0x100);
    sScreenBufDrc = MEMAllocFromFrmHeapEx(heap, sScreenBufDrcSize, 0x100);
    OSScreenSetBufferEx(SCREEN_TV, sScreenBufTv);
    OSScreenSetBufferEx(SCREEN_DRC, sScreenBufDrc);
    OSScreenFlipBuffers();
    return 0;
}

unsigned int procUiCallbackRelease([[maybe_unused]] void *context)
{
    MEMHeapHandle heap = MEMGetBaseHeapHandle(MEM_BASE_HEAP_MEM1);
    MEMFreeByStateToFrmHeap(heap, CONSOLE_FRAME_HEAP_TAG);
    return 0;
}

// copied from https://github.com/wiiu-env/AromaUpdater/blob/9be5f01ca6b00a09fcaa5f6e46ca00a429539937/source/common.h#L20
static inline bool runningFromMiiMaker()
{
    return (OSGetTitleID() & 0xFFFFFFFFFFFFF0FFull) == 0x000500101004A000ull;
}

int main([[maybe_unused]] int argc, [[maybe_unused]] char **argv)
{
    if (runningFromMiiMaker()) {
        OSEnableHomeButtonMenu(FALSE);
    }
    
    ProcUIInit(procUiSaveCallback);

    OSScreenInit();
    sScreenBufTvSize = OSScreenGetBufferSizeEx(SCREEN_TV);
    sScreenBufDrcSize = OSScreenGetBufferSizeEx(SCREEN_DRC);
    procUiCallbackAcquire(nullptr);
    OSScreenEnable(TRUE);

    ProcUIRegisterCallback(PROCUI_CALLBACK_ACQUIRE, procUiCallbackAcquire, nullptr, 100);
    ProcUIRegisterCallback(PROCUI_CALLBACK_RELEASE, procUiCallbackRelease, nullptr, 100);

    AXInit();

    KPADInit();
    WPADEnableURCC(TRUE);

    //garbage read
    getButtonsDown();

    int curSel = 0;
    bool applyChanges = false;
    bool redraw = true;

    bool isNTSC = AVMDebugIsNTSC();
    bool wantNTSC = isNTSC;

    int outPort = TVEGetCurrentPort();
    if(outPort > 3) outPort = 0;
    int wantPort = outPort;

    //int outRes; //still need to get resolution somehow...
    int wantRes = 2; //default 480i
    if(outPort == 0) wantRes = 4; //720p from HDMI
    else if(outPort == 1) wantRes = 3; //480p from Component
    else if(outPort == 3) wantRes = 10; //480i PAL60 from Composite/SCART

    ProcUIStatus status = PROCUI_STATUS_IN_FOREGROUND;
    while ((status = ProcUIProcessMessages(TRUE)) != PROCUI_STATUS_EXITING) {
        OSSleepTicks(OSMillisecondsToTicks(25));
        if (status == PROCUI_STATUS_RELEASE_FOREGROUND) {
            redraw = true; //for when we return to foreground
            ProcUIDrawDoneRelease();
            continue;
        } else if (status != PROCUI_STATUS_IN_FOREGROUND) {
            continue;
        }
        
        unsigned int btnDown = getButtonsDown();

        if (btnDown & VPAD_BUTTON_HOME) {
            if (runningFromMiiMaker()) {
                SYSRelaunchTitle(0, 0);
            }
        }
        
        if (btnDown & VPAD_BUTTON_RIGHT) {
            if (curSel == 0) { //NTSC/PAL
                wantNTSC = !wantNTSC;
            } else if (curSel == 1) { //Port
                wantPort++;
                if (wantPort > 3)
                    wantPort = 0;
                
                //Set default res for port
                if(wantPort == 0) wantRes = 4; //720p from HDMI
                else if(wantPort == 1) wantRes = 3; //480p from Component
                else if(wantPort == 2) wantRes = 2; //480i from Composite/S-Video
                else if(wantPort == 3) wantRes = 10; //480i PAL60 from Composite/SCART
            } else if (curSel == 2) { //Resolution
                wantRes++;
                if (wantRes > 14)
                    wantRes = 1;
            }
            redraw = true;
        }

        if (btnDown & VPAD_BUTTON_LEFT) {
            if (curSel == 0) { //NTSC/PAL
                wantNTSC = !wantNTSC;
            } else if (curSel == 1) { //Port
                wantPort--;
                if (wantPort < 0)
                    wantPort = 3;
                
                //Set default res for port
                if(wantPort == 0) wantRes = 4; //720p from HDMI
                else if(wantPort == 1) wantRes = 3; //480p from Component
                else if(wantPort == 2) wantRes = 2; //480i from Composite/S-Video
                else if(wantPort == 3) wantRes = 10; //480i PAL60 from Composite/SCART
            } else if (curSel == 2) { //Resolution
                wantRes--;
                if (wantRes < 1)
                    wantRes = 14;
            }
            redraw = true;
        }

        if (btnDown & VPAD_BUTTON_DOWN) {
            curSel++;
            if (curSel > 2) curSel = 0;
            redraw = true;
        }

        if (btnDown & VPAD_BUTTON_UP) {
            curSel--;
            if (curSel < 0) curSel = 2;
            redraw = true;
        }

        if (btnDown & VPAD_BUTTON_A) {
            applyChanges = true;
        }

        if (applyChanges) {
            applyChanges = false;
            if((isNTSC && !wantNTSC) || (!isNTSC && wantNTSC))
                AVMSetTVVideoRegion(wantNTSC ? AVM_TV_VIDEO_REGION_NTSC : AVM_TV_VIDEO_REGION_PAL, (TVEPort) wantPort, (AVMTvResolution) wantRes);
            else if(outPort != wantPort)
                AVMSetTVOutPort((TVEPort) wantPort, (AVMTvResolution) wantRes);
            else //only set resolution
                AVMSetTVScanResolution((AVMTvResolution) wantRes);
        }

        if (redraw) {
            OSScreenClearBuffer(0);

            OSScreenPutFont(0, 0, verStr);
            char printStr[256];
            snprintf(printStr, sizeof(printStr), "%s Video Region: %s", curSel == 0 ? ">" : " ", wantNTSC ? "NTSC" : "PAL");
            OSScreenPutFont(0, 1, printStr);
            snprintf(printStr, sizeof(printStr), "%s Output Port: %s", curSel == 1 ? ">" : " ", outPortStr[wantPort]);
            OSScreenPutFont(0, 2, printStr);
            snprintf(printStr, sizeof(printStr), "%s Output Resolution: %s", curSel == 2 ? ">" : " ", outResStr[wantRes]);
            OSScreenPutFont(0, 3, printStr);
            OSScreenPutFont(0, 4, "<>: Change value");
            OSScreenPutFont(0, 5, "A: Apply settings");
            OSScreenPutFont(0, 6, "HOME: Exit");

            OSScreenPutFont(0, 16, authorStr);

            OSScreenFlipBuffers();
            redraw = false;
        }
    }

    KPADShutdown();

    AXQuit();

    if (ProcUIInForeground()) {
        OSScreenEnable(FALSE);
        OSScreenShutdown();
        procUiCallbackRelease(nullptr);
    }

    ProcUIShutdown();

    return EXIT_SUCCESS;
}
