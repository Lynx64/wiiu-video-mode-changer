/*
 * Copyright (C) 2017 FIX94
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include "main.h"

#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <malloc.h>
#include <stdio.h>
#include <vector>
#include <string>

#include <avm/tv.h>
#include <coreinit/foreground.h>
#include <coreinit/memfrmheap.h>
#include <coreinit/screen.h>
#include <coreinit/thread.h>
#include <coreinit/title.h>
#include <padscore/kpad.h>
#include <proc_ui/procui.h>
#include <vpad/input.h>

#define CONSOLE_FRAME_HEAP_TAG (0x000DECAF)

#define OSScreenEnable(enable)      OSScreenEnableEx(SCREEN_TV, enable); OSScreenEnableEx(SCREEN_DRC, enable);
#define OSScreenClearBuffer(colour) OSScreenClearBufferEx(SCREEN_TV, colour); OSScreenClearBufferEx(SCREEN_DRC, colour);
#define OSScreenPutFont(x, y, buf)  OSScreenPutFontEx(SCREEN_TV, x, y, buf); OSScreenPutFontEx(SCREEN_DRC, x, y, buf);
#define OSScreenFlipBuffers()       OSScreenFlipBuffersEx(SCREEN_TV); OSScreenFlipBuffersEx(SCREEN_DRC);

static const char *verChar = "WiiU Video Mode Changer v1.0 by FIX94";
static const char * const outPortStr[] = {"HDMI", "Component", "Composite/S-Video", "Composite/SCART"};
static const char * const outResStr[] = {"Unused [0]", "576i PAL50", "480i", "480p", "720p", "720p 3D?", "1080i", "1080p", "Unused [8]", "Unused [9]", "480i PAL60", "576p", "720p 50Hz (glitchy GamePad)", "1080i 50Hz (glitchy GamePad)", "1080p 50Hz (glitchy GamePad)"};

static unsigned int sScreenBufTvSize = 0;
static unsigned int sScreenBufDrcSize = 0;

static unsigned int wpadToVpad(unsigned int buttons)
{
    unsigned int conv_buttons = 0;

    if(buttons & WPAD_BUTTON_LEFT)
        conv_buttons |= VPAD_BUTTON_LEFT;

    if(buttons & WPAD_BUTTON_RIGHT)
        conv_buttons |= VPAD_BUTTON_RIGHT;

    if(buttons & WPAD_BUTTON_DOWN)
        conv_buttons |= VPAD_BUTTON_DOWN;

    if(buttons & WPAD_BUTTON_UP)
        conv_buttons |= VPAD_BUTTON_UP;

    if(buttons & WPAD_BUTTON_PLUS)
        conv_buttons |= VPAD_BUTTON_PLUS;

    if(buttons & WPAD_BUTTON_B)
        conv_buttons |= VPAD_BUTTON_B;

    if(buttons & WPAD_BUTTON_A)
        conv_buttons |= VPAD_BUTTON_A;

    if(buttons & WPAD_BUTTON_MINUS)
        conv_buttons |= VPAD_BUTTON_MINUS;

    if(buttons & WPAD_BUTTON_HOME)
        conv_buttons |= VPAD_BUTTON_HOME;

    return conv_buttons;
}

static unsigned int wpadClassicToVpad(unsigned int buttons)
{
    unsigned int conv_buttons = 0;

    if(buttons & WPAD_CLASSIC_BUTTON_LEFT)
        conv_buttons |= VPAD_BUTTON_LEFT;

    if(buttons & WPAD_CLASSIC_BUTTON_RIGHT)
        conv_buttons |= VPAD_BUTTON_RIGHT;

    if(buttons & WPAD_CLASSIC_BUTTON_DOWN)
        conv_buttons |= VPAD_BUTTON_DOWN;

    if(buttons & WPAD_CLASSIC_BUTTON_UP)
        conv_buttons |= VPAD_BUTTON_UP;

    if(buttons & WPAD_CLASSIC_BUTTON_PLUS)
        conv_buttons |= VPAD_BUTTON_PLUS;

    if(buttons & WPAD_CLASSIC_BUTTON_X)
        conv_buttons |= VPAD_BUTTON_X;

    if(buttons & WPAD_CLASSIC_BUTTON_Y)
        conv_buttons |= VPAD_BUTTON_Y;

    if(buttons & WPAD_CLASSIC_BUTTON_B)
        conv_buttons |= VPAD_BUTTON_B;

    if(buttons & WPAD_CLASSIC_BUTTON_A)
        conv_buttons |= VPAD_BUTTON_A;

    if(buttons & WPAD_CLASSIC_BUTTON_MINUS)
        conv_buttons |= VPAD_BUTTON_MINUS;

    if(buttons & WPAD_CLASSIC_BUTTON_HOME)
        conv_buttons |= VPAD_BUTTON_HOME;

    if(buttons & WPAD_CLASSIC_BUTTON_ZR)
        conv_buttons |= VPAD_BUTTON_ZR;

    if(buttons & WPAD_CLASSIC_BUTTON_ZL)
        conv_buttons |= VPAD_BUTTON_ZL;

    if(buttons & WPAD_CLASSIC_BUTTON_R)
        conv_buttons |= VPAD_BUTTON_R;

    if(buttons & WPAD_CLASSIC_BUTTON_L)
        conv_buttons |= VPAD_BUTTON_L;

    return conv_buttons;
}

static unsigned int getButtonsDown()
{
    unsigned int btnDown = 0;

    VPADReadError vpadError = VPAD_READ_UNINITIALIZED;
    VPADStatus vpad{};
    VPADRead(VPAD_CHAN_0, &vpad, 1, &vpadError);
    if(vpadError == 0)
        btnDown |= vpad.trigger;

    int i;
    for(i = 0; i < 4; i++)
    {
        WPADExtensionType controller_type;
        if(WPADProbe((WPADChan) i, &controller_type) != 0)
            continue;
        KPADStatus kpadData{};
        KPADRead((KPADChan) i, &kpadData, 1);
        if(kpadData.extensionType <= 1)
            btnDown |= wpadToVpad(kpadData.trigger);
        else
            btnDown |= wpadClassicToVpad(kpadData.classic.trigger);
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
    void *screenBufTv = MEMAllocFromFrmHeapEx(heap, sScreenBufTvSize, 0x100);
    void *screenBufDrc = MEMAllocFromFrmHeapEx(heap, sScreenBufDrcSize, 0x100);
    OSScreenSetBufferEx(SCREEN_TV, screenBufTv);
    OSScreenSetBufferEx(SCREEN_DRC, screenBufDrc);
    return 0;
}

unsigned int procUiCallbackRelease([[maybe_unused]] void *context)
{
    MEMHeapHandle heap = MEMGetBaseHeapHandle(MEM_BASE_HEAP_MEM1);
    MEMFreeByStateToFrmHeap(heap, CONSOLE_FRAME_HEAP_TAG);
    return 0;
}

static inline bool runningFromMiiMaker()
{
    return (OSGetTitleID() & 0xFFFFFFFFFFFFF0FFull) == 0x000500101004A000ull;
}

int main(int argc, char **argv)
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

    KPADInit();
    WPADEnableURCC(TRUE);

    //garbage read
    getButtonsDown();

    int curSel = 0;
    int redraw = 1;

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
        OSSleepTicks(OSMicrosecondsToTicks(25000));
        if (status == PROCUI_STATUS_RELEASE_FOREGROUND) {
            redraw = 1; //for when we return to foreground
            ProcUIDrawDoneRelease();
            continue;
        } else if (status != PROCUI_STATUS_IN_FOREGROUND) {
            continue;
        }
        
        unsigned int btnDown = getButtonsDown();

        if (btnDown & VPAD_BUTTON_HOME) {
            if (runningFromMiiMaker()) {
                break;
            }
        } else if( btnDown & VPAD_BUTTON_RIGHT )
        {
            if(curSel == 0) //NTSC/PAL
            {
                wantNTSC = !wantNTSC;
                if(wantNTSC)
                {
                    //no Composite/SCART
                    if(wantPort == 3)
                    {
                        wantPort = 2; //Composite/S-Video
                        wantRes = 2; //480i
                    }
                }
                else //want PAL
                {
                    //no Composite/S-Video
                    if(wantPort == 2)
                    {
                        wantPort = 3; //Composite/SCART
                        wantRes = 10; //480i PAL60
                    }
                }
            }
            else if(curSel == 1) //Port
            {
                wantPort++;
                if(wantPort > 3)
                    wantPort = 0;
                if(wantNTSC)
                {
                    //no Composite/SCART
                    if(wantPort == 3)
                        wantPort = 0;
                }
                else //want PAL
                {
                    //no Composite/S-Video
                    if(wantPort == 2)
                        wantPort = 3;
                }
                //Set default res for port
                if(wantPort == 0) wantRes = 4; //720p from HDMI
                else if(wantPort == 1) wantRes = 3; //480p from Component
                else if(wantPort == 2) wantRes = 2; //480i from Composite/S-Video
                else if(wantPort == 3) wantRes = 10; //480i PAL60 from Composite/SCART
            }
            else if(curSel == 2) //Resolution
            {
                wantRes++;
                if(wantRes > 14)
                    wantRes = 1;
            }
            redraw = 1;
        }

        if( btnDown & VPAD_BUTTON_LEFT )
        {
            if(curSel == 0) //NTSC/PAL
            {
                wantNTSC = !wantNTSC;
                if(wantNTSC)
                {
                    //no Composite/SCART
                    if(wantPort == 3)
                    {
                        wantPort = 2; //Composite/S-Video
                        wantRes = 2; //480i
                    }
                }
                else //want PAL
                {
                    //no Composite/S-Video
                    if(wantPort == 2)
                    {
                        wantPort = 3; //Composite/SCART
                        wantRes = 10; //480i PAL60
                    }
                }
            }
            else if(curSel == 1) //Port
            {
                wantPort--;
                if(wantPort < 0)
                    wantPort = 3;
                if(wantNTSC)
                {
                    //no Composite/SCART
                    if(wantPort == 3)
                        wantPort = 2;
                }
                else //want PAL
                {
                    //no Composite/S-Video
                    if(wantPort == 2)
                        wantPort = 1;
                }
                //Set default res for port
                if(wantPort == 0) wantRes = 4; //720p from HDMI
                else if(wantPort == 1) wantRes = 3; //480p from Component
                else if(wantPort == 2) wantRes = 2; //480i from Composite/S-Video
                else if(wantPort == 3) wantRes = 10; //480i PAL60 from Composite/SCART
            }
            else if(curSel == 2) //Resolution
            {
                wantRes--;
                if(wantRes < 0)
                    wantRes = 14;
            }
            redraw = 1;
        }

        if( btnDown & VPAD_BUTTON_DOWN )
        {
            curSel++;
            if(curSel > 2) curSel = 0;
            redraw = 1;
        }

        if( btnDown & VPAD_BUTTON_UP )
        {
            curSel--;
            if(curSel < 0) curSel = 2;
            redraw = 1;
        }

        if( btnDown & VPAD_BUTTON_A )
        {
            if((isNTSC && !wantNTSC) || (!isNTSC && wantNTSC))
                AVMSetTVVideoRegion(wantNTSC ? AVM_TV_VIDEO_REGION_NTSC : AVM_TV_VIDEO_REGION_PAL, (TVEPort) wantPort, (AVMTvResolution) wantRes);
            else if(outPort != wantPort)
                AVMSetTVOutPort((TVEPort) wantPort, (AVMTvResolution) wantRes);
            else //only set resolution
                AVMSetTVScanResolution((AVMTvResolution) wantRes);
        }

        if( redraw )
        {
            OSScreenClearBuffer(0);
            OSScreenPutFont(0, 0, verChar);
            char printStr[256];
            sprintf(printStr,"%s Video Region: %s", (curSel==0)?">":" ", wantNTSC ? "NTSC" : "PAL");
            OSScreenPutFont(0, 1, printStr);
            sprintf(printStr,"%s Output Port: %s", (curSel==1)?">":" ", outPortStr[wantPort]);
            OSScreenPutFont(0, 2, printStr);
            sprintf(printStr,"%s Output Resolution: %s", (curSel==2)?">":" ", outResStr[wantRes]);
            OSScreenPutFont(0, 3, printStr);
            OSScreenPutFont(0, 4, "Press A to apply settings.");
            OSScreenPutFont(0, 5, "Press Home to exit without changes.");
            OSScreenFlipBuffers();
            redraw = 0;
        }
    }

    if (ProcUIInForeground()) {
        OSScreenEnable(FALSE);
        OSScreenShutdown();
        procUiCallbackRelease(nullptr);
    }

    ProcUIShutdown();

    return EXIT_SUCCESS;
}
