#pragma once

#include "CoreMinimal.h"

// Thin wrapper over Win32 SetupAPI + HidD_*. Sync open/write/read with timeout, modelled
// directly after how the web flasher (lib/hid/flash.ts) talks to the device. All sizes are
// in *raw bytes including the report id*: callers must prepend a leading byte 0x01 to every
// output buffer.
class FCenterpieceHid
{
public:
	FCenterpieceHid();
	~FCenterpieceHid();

	// Find and open the first HID device that matches all four filter values. Returns false
	// when nothing matches (no keyboard plugged in, wrong VID/PID, or no collection with the
	// requested usagePage/usage). The collection check is what avoids the multi-interface trap
	// the WebHID port hit.
	bool Open(uint16 VendorId, uint16 ProductId, uint16 UsagePage, uint16 Usage, FString& OutError);

	void Close();

	bool IsOpen() const;

	// Send a single output report. Buffer MUST start with the report id byte (0x01 for the
	// vendor command channel). Size MUST equal MaxOutputReportSize for the device's HID
	// descriptor (the firmware drops short writes).
	bool WriteReport(const uint8* Bytes, int32 NumBytes, FString& OutError);

	// Block on the next input report up to TimeoutMs. The returned buffer starts with the
	// report id byte (0x01); callers should skip byte 0 to match the web flasher's frame
	// layout (len_lo at 0, len_hi at 1, cmd at 2 after the report id is stripped).
	// Returns false on timeout, IO error, or cancel.
	bool ReadReport(TArray<uint8>& OutBytes, uint32 TimeoutMs, FString& OutError);

	int32 GetMaxOutputReportSize() const { return MaxOutputReport; }
	int32 GetMaxInputReportSize() const { return MaxInputReport; }

private:
	void* DeviceHandle = nullptr; // HANDLE (kept as void* so the header doesn't pull in Windows.h).
	int32 MaxOutputReport = 0;
	int32 MaxInputReport = 0;
};
