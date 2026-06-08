#pragma once

#include "CoreMinimal.h"

class FCenterpieceHid;

// Mirrors the byte-level upload protocol from centerpiece-web/web/lib/hid/flash.ts. All
// numbers were reverse-engineered from sniffing the official xpanel + CLI; nothing comes
// from the official SDK source.
namespace FlashProtocol
{
	constexpr uint8 REPORT_ID         = 0x01;
	constexpr int32 REPORT_LEN        = 1023; // raw output report size (excluding the report id byte)
	constexpr int32 CHUNK             = 1020; // header(3) + payload(1020) = 1023
	constexpr int32 MB_BEAT           = 1024 * 1024;
	constexpr uint32 CONFIRM_TIMEOUT_MS = 10000;

	constexpr uint8 CMD_SKIN_START       = 0x00;
	constexpr uint8 CMD_JSON_START       = 0x01;
	constexpr uint8 CMD_THUMBNAIL_GET    = 0x07;
	constexpr uint8 CMD_SKIN_DETAILS_GET = 0x08;
	constexpr uint8 CMD_FILE_DATA        = 0x10;
	constexpr uint8 CMD_FILE_STATUS      = 0x11;
	constexpr uint8 CMD_FILE_END         = 0x20;
	constexpr uint8 CMD_SKIN_CHANGE      = 0x30;
}

struct FFlashOptions
{
	TArray<uint8> Data;             // .pak bytes
	FString FileName;               // base name (no extension), shown to the firmware
	FString FileExtension = TEXT("pak");
	int32 Slot = 1;                 // 1..5 on the wire (same value as the UI display)
};

DECLARE_DELEGATE_FourParams(FFlashProgressDelegate, int64 /*BytesSent*/, int64 /*TotalBytes*/, float /*MBperSec*/, const FString& /*PhaseLabel*/);

class FFlasher
{
public:
	// Run the entire flash sequence: manifest -> skin payload -> verify queries. Returns
	// false with OutError populated on any failure. Progress reports come in on the calling
	// thread (the dialog drives this from a TaskGraph worker and marshals UI updates back).
	static bool Run(FCenterpieceHid& Hid, const FFlashOptions& Options, const FFlashProgressDelegate& OnProgress, FString& OutError);
};
