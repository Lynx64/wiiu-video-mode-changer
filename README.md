# Wii U Video Mode Changer Aroma Port
This allows quick changes to the currently displayed video mode. It's also temporary so after a reboot or something like that the settings will be reset to the ones you set in the system settings.

You can change between NTSC and PAL, all the different ports, and change the resolution to whatever you want. I did not include any safe guards for invalid settings so keep that in mind (for example you will not get composite to go to 720p etc.).

## Installation
For convenience, you can download Video Mode Changer directly on your console from the [Homebrew App Store](https://github.com/fortheusers/hb-appstore).

<p align="center">
  <a href="https://hb-app.store/wiiu/VideoModeChangerAroma">
    <img width="335" alt="Get it on the Homebrew App Store!" src="https://github.com/user-attachments/assets/4471a846-9e8f-4a93-9a5c-a252e70d053a" />
  </a>
</p>

Alternatively, download the latest release from the [Releases page](https://github.com/Lynx64/wiiu-video-mode-changer/releases) by clicking on `VideoModeChanger.zip`.<br/>
Extract the `VideoModeChanger.zip` file to the root of your SD card.

## Building
For building you need:
- [wut](https://github.com/devkitPro/wut)

then run `make`
