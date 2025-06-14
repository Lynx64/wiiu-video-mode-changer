/*
 * Copyright (C) 2017 FIX94
 * Copyright (C) 2025 Lynx64
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */
#include "main.h"

#include <cstdio>
#include <cstdlib>
#include <iterator>

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

extern "C" int AVMSetTVTileMode(unsigned char mode);

#define CONSOLE_FRAME_HEAP_TAG (0x000DECAF)

#define OSScreenEnable(enable)      OSScreenEnableEx(SCREEN_TV, enable); OSScreenEnableEx(SCREEN_DRC, enable);
#define OSScreenClearBuffer(colour) OSScreenClearBufferEx(SCREEN_TV, colour); OSScreenClearBufferEx(SCREEN_DRC, colour);
#define OSScreenPutFont(x, y, buf)  OSScreenPutFontEx(SCREEN_TV, x, y, buf); OSScreenPutFontEx(SCREEN_DRC, x, y, buf);
#define OSScreenFlipBuffers()       OSScreenFlipBuffersEx(SCREEN_TV); OSScreenFlipBuffersEx(SCREEN_DRC);

static const char *verStr = "Wii U Video Mode Changer Aroma Port v2.0";
static const char *authorStr = "By Lynx64. Original by FIX94.";

static const char * const portStr[] = {"HDMI", "Component", "Composite", "SCART"};
static const char *aspectRatioStr[] = {"4:3", "16:9"};

struct Resolution {
    const char *name;
    const AVMTvResolution value;
};

static constexpr const Resolution resolutions[] = {
        {"576i", AVM_TV_RESOLUTION_576I},
        {"480i", AVM_TV_RESOLUTION_480I},
        {"480p", AVM_TV_RESOLUTION_480P},
        {"720p", AVM_TV_RESOLUTION_720P},
        {"720p 3D", AVM_TV_RESOLUTION_720P_3D},
        {"1080i", AVM_TV_RESOLUTION_1080I},
        {"1080p", AVM_TV_RESOLUTION_1080P},
        {"480i PAL60", AVM_TV_RESOLUTION_480I_PAL60},
        {"576p", AVM_TV_RESOLUTION_576P},
        {"720p 50Hz (glitchy GamePad)", AVM_TV_RESOLUTION_720P_50HZ},
        {"1080i 50Hz (glitchy GamePad)", AVM_TV_RESOLUTION_1080I_50HZ},
        {"1080p 50Hz (glitchy GamePad)", AVM_TV_RESOLUTION_1080P_50HZ}
        };

static constexpr std::size_t RESOLUTION_COUNT = std::size(resolutions);

static unsigned int sScreenBufTvSize = 0;
static unsigned int sScreenBufDrcSize = 0;
static void *sScreenBufTv = nullptr;
static void *sScreenBufDrc = nullptr;

static unsigned int wpadToVpad(unsigned int buttons)
{
    unsigned int convButtons = 0;

    if (buttons & WPAD_BUTTON_LEFT)
        convButtons |= VPAD_BUTTON_LEFT;

    if (buttons & WPAD_BUTTON_RIGHT)
        convButtons |= VPAD_BUTTON_RIGHT;

    if (buttons & WPAD_BUTTON_DOWN)
        convButtons |= VPAD_BUTTON_DOWN;

    if (buttons & WPAD_BUTTON_UP)
        convButtons |= VPAD_BUTTON_UP;

    if (buttons & WPAD_BUTTON_PLUS)
        convButtons |= VPAD_BUTTON_PLUS;

    if (buttons & WPAD_BUTTON_B)
        convButtons |= VPAD_BUTTON_B;

    if (buttons & WPAD_BUTTON_A)
        convButtons |= VPAD_BUTTON_A;

    if (buttons & WPAD_BUTTON_MINUS)
        convButtons |= VPAD_BUTTON_MINUS;

    if (buttons & WPAD_BUTTON_HOME)
        convButtons |= VPAD_BUTTON_HOME;

    return convButtons;
}

static unsigned int wpadClassicToVpad(unsigned int buttons)
{
    unsigned int convButtons = 0;

    if (buttons & WPAD_CLASSIC_BUTTON_LEFT)
        convButtons |= VPAD_BUTTON_LEFT;

    if (buttons & WPAD_CLASSIC_BUTTON_RIGHT)
        convButtons |= VPAD_BUTTON_RIGHT;

    if (buttons & WPAD_CLASSIC_BUTTON_DOWN)
        convButtons |= VPAD_BUTTON_DOWN;

    if (buttons & WPAD_CLASSIC_BUTTON_UP)
        convButtons |= VPAD_BUTTON_UP;

    if (buttons & WPAD_CLASSIC_BUTTON_PLUS)
        convButtons |= VPAD_BUTTON_PLUS;

    if (buttons & WPAD_CLASSIC_BUTTON_X)
        convButtons |= VPAD_BUTTON_X;

    if (buttons & WPAD_CLASSIC_BUTTON_Y)
        convButtons |= VPAD_BUTTON_Y;

    if (buttons & WPAD_CLASSIC_BUTTON_B)
        convButtons |= VPAD_BUTTON_B;

    if (buttons & WPAD_CLASSIC_BUTTON_A)
        convButtons |= VPAD_BUTTON_A;

    if (buttons & WPAD_CLASSIC_BUTTON_MINUS)
        convButtons |= VPAD_BUTTON_MINUS;

    if (buttons & WPAD_CLASSIC_BUTTON_HOME)
        convButtons |= VPAD_BUTTON_HOME;

    if (buttons & WPAD_CLASSIC_BUTTON_ZR)
        convButtons |= VPAD_BUTTON_ZR;

    if (buttons & WPAD_CLASSIC_BUTTON_ZL)
        convButtons |= VPAD_BUTTON_ZL;

    if (buttons & WPAD_CLASSIC_BUTTON_R)
        convButtons |= VPAD_BUTTON_R;

    if (buttons & WPAD_CLASSIC_BUTTON_L)
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

static unsigned int setDefaultResIndex(const int port)
{
    switch (port) {
        case 0: //HDMI
            return 3; //720p
        case 1: //Component
            return 2; //480p
        case 2: //Composite
            return 1; //480i
        case 3: //SCART
            return 7; //480i PAL60
        default:
            return 3; //720p
    }
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

    AVMSetTVTileMode(1); //otherwise when we set the TV the tile mode changes to 4 making the TV screen illegible

    AXInit();

    KPADInit();
    WPADEnableURCC(TRUE);

    //garbage read
    getButtonsDown();

    int curSel = 0;
    bool applyChanges = false;
    bool redraw = true;
    bool quitOnApply = true;

    bool isNTSC = AVMDebugIsNTSC();
    bool wantNTSC = isNTSC;

    int isPort = TVEGetCurrentPort();
    int wantPort = isPort;

    unsigned int wantResIndex = setDefaultResIndex(isPort); //fallback if we fail to get/find the current resolution
    // Since 1080p 50hz is never output by GetTVScanMode, we can
    // use it to check if the function failed/succeeded (see documentation)
    AVMTvResolution isRes = AVM_TV_RESOLUTION_1080P_50HZ;
    AVMGetTVScanMode(&isRes);
    if (isRes != AVM_TV_RESOLUTION_1080P_50HZ) {
        for (unsigned int i = 0; i < RESOLUTION_COUNT; i++) {
            if (resolutions[i].value == isRes) {
                wantResIndex = i;
                break;
            }
        }
    }

    AVMTvAspectRatio isAspectRatio = AVM_TV_ASPECT_RATIO_16_9;
    AVMGetTVAspectRatio(&isAspectRatio);
    AVMTvAspectRatio wantAspectRatio = isAspectRatio;

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
                wantResIndex = setDefaultResIndex(wantPort);
            } else if (curSel == 2) { //Resolution
                wantResIndex++;
                if (wantResIndex >= RESOLUTION_COUNT)
                    wantResIndex = 0;
            } else if (curSel == 3) { //Aspect Ratio
                wantAspectRatio = static_cast<AVMTvAspectRatio>(!wantAspectRatio);
            } else if (curSel == 4) {
                quitOnApply = !quitOnApply;
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
                wantResIndex = setDefaultResIndex(wantPort);
            } else if (curSel == 2) { //Resolution
                if (wantResIndex == 0) { //this check is different from the others because it's an unsigned int
                    wantResIndex = RESOLUTION_COUNT - 1;
                } else {
                    wantResIndex--;
                }
            } else if (curSel == 3) { //Aspect Ratio
                wantAspectRatio = static_cast<AVMTvAspectRatio>(!wantAspectRatio);
            } else if (curSel == 4) {
                quitOnApply = !quitOnApply;
            }
            redraw = true;
        }

        if (btnDown & VPAD_BUTTON_DOWN) {
            curSel++;
            if (curSel > 4)
                curSel = 0;
            redraw = true;
        }

        if (btnDown & VPAD_BUTTON_UP) {
            curSel--;
            if (curSel < 0)
                curSel = 4;
            redraw = true;
        }

        if (btnDown & VPAD_BUTTON_A) {
            applyChanges = true;
        }

        if (applyChanges) {
            applyChanges = false;
            int applySuccess = 0;
            if (isNTSC != wantNTSC) {
                if ((applySuccess = AVMSetTVVideoRegion(wantNTSC ? AVM_TV_VIDEO_REGION_NTSC : AVM_TV_VIDEO_REGION_PAL, (TVEPort) wantPort, resolutions[wantResIndex].value)) == 0) {
                    isNTSC = wantNTSC;
                    isPort = wantPort;
                }
            } else if (isPort != wantPort) {
                if ((applySuccess = AVMSetTVOutPort((TVEPort) wantPort, resolutions[wantResIndex].value)) == 0) {
                    isPort = wantPort;
                }
            } else { //only set resolution
                applySuccess = AVMSetTVScanResolution(resolutions[wantResIndex].value);
            }

            if (isAspectRatio != wantAspectRatio) {
                if (AVMSetTVAspectRatio(wantAspectRatio) == TRUE) {
                    isAspectRatio = wantAspectRatio;
                }
            }

            if (quitOnApply && applySuccess == 0) {
                if (runningFromMiiMaker()) {
                    SYSRelaunchTitle(0, 0);
                } else {
                    SYSLaunchMenu();
                }
            }
        }

        if (redraw) {
            OSScreenClearBuffer(0);

            OSScreenPutFont(0, 0, verStr);
            char printStr[256];
            snprintf(printStr, sizeof(printStr), "%s Video Region: %s", curSel == 0 ? ">" : " ", wantNTSC ? "NTSC" : "PAL");
            OSScreenPutFont(0, 1, printStr);
            snprintf(printStr, sizeof(printStr), "%s Output Port: %s", curSel == 1 ? ">" : " ", portStr[wantPort]);
            OSScreenPutFont(0, 2, printStr);
            snprintf(printStr, sizeof(printStr), "%s Output Resolution: %s", curSel == 2 ? ">" : " ", resolutions[wantResIndex].name);
            OSScreenPutFont(0, 3, printStr);
            snprintf(printStr, sizeof(printStr), "%s Aspect Ratio: %s", curSel == 3 ? ">" : " ", aspectRatioStr[wantAspectRatio]);
            OSScreenPutFont(0, 4, printStr);
            snprintf(printStr, sizeof(printStr), "%s Exit after Applying: %s", curSel == 4 ? ">" : " ", quitOnApply ? "Yes" : "No");
            OSScreenPutFont(0, 5, printStr);
            OSScreenPutFont(0, 6, "<>: Change value");
            OSScreenPutFont(0, 7, "A: Apply settings");
            OSScreenPutFont(0, 8, "HOME: Exit");

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
