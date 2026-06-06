# cproskin

Community SDK for the Finalmouse Centerpiece Pro under-key display. Reverse-engineered from the stock skin PAKs.

Write-up: https://nun.tax/blog/reverse-engineering-the-centerpiece-pro/

## Setup

Needs Unreal Engine 4.27 (binary install from the Epic Games Launcher works fine).

For packaging skins to the device you also need Android Studio with NDK 21.4.7075529, SDK Platform 30, Build Tools 30.0.3, and JDK 8.

Open `cproskin.uproject`. The entry map is `Content/map/M_EntryPoint`. There is an example skin in `Content/SG_MySkin`.

## Packaging

File > Package Project > Android > Android (ASTC).

The PAK ends up at:
```
Saved/StagedBuilds/Android_ASTC/cproskin/Content/Paks/pakchunk1001-Android_ASTC.pak
```

There is a HID upload script in `uploadscript/upload_ue.js` if you want to push it over USB.

## Notes

Entry map has to be at `Content/map/M_EntryPoint`. Logic goes in Blueprints, not C++. Do not override `GameInstance`, the SkinEngine uses a fixed one.

`GetPositionByKeyIndex` returns `(0, 0)` in PIE on Windows. Real coordinates only show up on a device.

## License

MIT. Credit is appreciated if you build on top of this.
