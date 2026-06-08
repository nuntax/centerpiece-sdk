#include "FlashProtocol.h"
#include "HidDevice.h"
#include "Misc/Guid.h"

using namespace FlashProtocol;

namespace
{
	// Build a single output report into Buffer (which is sized to ReportLen + 1 for the
	// leading report id). The frame layout matches the JS flasher: len_lo, len_hi, cmd,
	// then up to CHUNK bytes of payload, then zero padding.
	void BuildReport(TArray<uint8>& Buffer, uint8 Cmd, const uint8* Payload, int32 PayloadLen)
	{
		const int32 ClippedLen = FMath::Min(PayloadLen, CHUNK);
		const int32 Total = 1 + REPORT_LEN;
		Buffer.SetNumZeroed(Total);
		Buffer[0] = REPORT_ID;
		Buffer[1] = static_cast<uint8>(ClippedLen & 0xff);
		Buffer[2] = static_cast<uint8>((ClippedLen >> 8) & 0xff);
		Buffer[3] = Cmd;
		if (Payload && ClippedLen > 0)
		{
			FMemory::Memcpy(Buffer.GetData() + 4, Payload, ClippedLen);
		}
	}

	bool SendCommand(FCenterpieceHid& Hid, uint8 Cmd, const uint8* Payload, int32 PayloadLen, FString& OutError)
	{
		TArray<uint8> Report;
		BuildReport(Report, Cmd, Payload, PayloadLen);
		return Hid.WriteReport(Report.GetData(), Report.Num(), OutError);
	}

	// Match flash.ts safeFileName: keep printable ASCII, replace path-illegal chars, trim.
	// The firmware's manifest parser is strict; non-ASCII silently kills the upload.
	FString SanitiseFileName(const FString& Input)
	{
		FString Out;
		Out.Reserve(Input.Len());
		for (const TCHAR C : Input)
		{
			if (C >= 0x20 && C < 0x7F)
			{
				if (C == TEXT('\\') || C == TEXT('/') || C == TEXT(':') || C == TEXT('*') ||
					C == TEXT('?') || C == TEXT('"') || C == TEXT('<') || C == TEXT('>') || C == TEXT('|'))
				{
					Out.AppendChar(TEXT('_'));
				}
				else
				{
					Out.AppendChar(C);
				}
			}
		}
		Out.TrimStartAndEndInline();
		return Out.IsEmpty() ? FString(TEXT("skin")) : Out;
	}

	FString BuildManifestJson(int32 Slot, const FString& FileName, const FString& FileExtension, int64 FileSize, const FString& FileId)
	{
		// Field order matters less than the field set: the firmware does a key-by-key parse.
		// We follow the JS flasher's order for parity.
		return FString::Printf(
			TEXT("{\"slot\":%d,\"fileName\":\"%s\",\"fileExtension\":\"%s\",\"fileSize\":%lld,\"fileID\":\"%s\"}"),
			Slot,
			*FileName,
			*FileExtension,
			FileSize,
			*FileId);
	}
}

bool FFlasher::Run(FCenterpieceHid& Hid, const FFlashOptions& Options, const FFlashProgressDelegate& OnProgress, FString& OutError)
{
	if (!Hid.IsOpen())
	{
		OutError = TEXT("HID device not open.");
		return false;
	}

	const int64 TotalBytes = Options.Data.Num();

	auto Report = [&](int64 Sent, float MBps, const TCHAR* Phase)
	{
		OnProgress.ExecuteIfBound(Sent, TotalBytes, MBps, FString(Phase));
	};

	Report(0, 0.f, TEXT("Preparing manifest"));

	// ── Phase 1: JSON manifest ─────────────────────────────────────────────────
	const FString SafeFileName = SanitiseFileName(Options.FileName);
	const FString FileId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens).ToLower();
	const FString ManifestJson = BuildManifestJson(Options.Slot, SafeFileName, Options.FileExtension, TotalBytes, FileId);

	FTCHARToUTF8 ManifestUtf8(*ManifestJson);
	const uint8* ManifestBytes = reinterpret_cast<const uint8*>(ManifestUtf8.Get());
	const int32 ManifestLen = ManifestUtf8.Length();

	if (!SendCommand(Hid, CMD_JSON_START, nullptr, 0, OutError)) return false;
	for (int32 Offset = 0; Offset < ManifestLen; Offset += CHUNK)
	{
		const int32 Take = FMath::Min(CHUNK, ManifestLen - Offset);
		if (!SendCommand(Hid, CMD_FILE_DATA, ManifestBytes + Offset, Take, OutError)) return false;
	}
	if (!SendCommand(Hid, CMD_FILE_END, nullptr, 0, OutError)) return false;

	// ── Phase 2: skin payload ──────────────────────────────────────────────────
	Report(0, 0.f, TEXT("Sending pak"));

	if (!SendCommand(Hid, CMD_SKIN_START, nullptr, 0, OutError)) return false;

	const double StartTime = FPlatformTime::Seconds();
	int64 TotalSent = 0;
	int64 BeatBucket = 0;
	int32 ReportCounter = 0;

	for (int64 Offset = 0; Offset < TotalBytes; Offset += CHUNK)
	{
		const int32 Take = static_cast<int32>(FMath::Min<int64>(CHUNK, TotalBytes - Offset));
		if (!SendCommand(Hid, CMD_FILE_DATA, Options.Data.GetData() + Offset, Take, OutError)) return false;

		TotalSent += Take;
		BeatBucket += Take;

		// Heartbeat every 1 MiB. Matches the official xpanel/CLI flow control; the firmware
		// stalls without it on slow USB hubs.
		if (BeatBucket >= MB_BEAT)
		{
			if (!SendCommand(Hid, CMD_FILE_STATUS, nullptr, 0, OutError)) return false;
			BeatBucket = 0;
		}

		// Throttle progress reports so we don't queue thousands of UI updates per second.
		if ((++ReportCounter & 0x7F) == 0)
		{
			const double Elapsed = FPlatformTime::Seconds() - StartTime;
			const float MBps = Elapsed > 0.0 ? static_cast<float>(TotalSent / 1024.0 / 1024.0 / Elapsed) : 0.f;
			Report(TotalSent, MBps, TEXT("Sending pak"));
		}
	}

	if (!SendCommand(Hid, CMD_FILE_END, nullptr, 0, OutError)) return false;

	// ── Phase 3: trigger the commit via two query commands ─────────────────────
	// The device only writes the slot to flash once SKIN_DETAILS_GET + THUMBNAIL_GET come in
	// after FILE_END. The web flasher discovered this the hard way; we replicate it 1:1.
	Report(TotalBytes, 0.f, TEXT("Verifying"));

	const uint8 SlotByte = static_cast<uint8>(Options.Slot);
	if (!SendCommand(Hid, CMD_SKIN_DETAILS_GET, &SlotByte, 1, OutError)) return false;
	if (!SendCommand(Hid, CMD_THUMBNAIL_GET, &SlotByte, 1, OutError)) return false;

	// Wait for a non-keepalive reply. The Centerpiece emits frequent keepalive input reports
	// (frame where len=0); the web flasher's `awaitAnyReply` explicitly skips them. Without
	// the same skip we'd read the very next keepalive and call the upload done before the
	// device has had a chance to actually process SKIN_DETAILS_GET / THUMBNAIL_GET. Track a
	// total time budget across the (possibly many) ReadReport calls.
	const double WaitStart = FPlatformTime::Seconds();
	TArray<uint8> Reply;
	FString ReadErr;
	for (;;)
	{
		const double ElapsedMs = (FPlatformTime::Seconds() - WaitStart) * 1000.0;
		if (ElapsedMs >= CONFIRM_TIMEOUT_MS)
		{
			OutError = TEXT("No confirmation reply (timed out) — flash may have succeeded; verify on the device.");
			Report(TotalBytes, 0.f, TEXT("Done (no confirmation)"));
			return true; // soft success, matches the web flasher's lenient behaviour
		}

		const uint32 RemainingMs = static_cast<uint32>(CONFIRM_TIMEOUT_MS - ElapsedMs);
		if (!Hid.ReadReport(Reply, RemainingMs, ReadErr))
		{
			OutError = FString::Printf(TEXT("No confirmation reply (%s) — flash may have succeeded; verify on the device."), *ReadErr);
			Report(TotalBytes, 0.f, TEXT("Done (no confirmation)"));
			return true;
		}

		// Report layout from the device, after the leading report id byte at index 0:
		//   [1] = len_lo, [2] = len_hi, [3] = cmd, [4..] = payload
		if (Reply.Num() < 4)
		{
			continue; // malformed — keep listening
		}
		const int32 PayLen = static_cast<int32>(Reply[1]) | (static_cast<int32>(Reply[2]) << 8);
		if (PayLen == 0)
		{
			continue; // keepalive — keep listening for the real reply
		}

		// Real reply received. The protocol doesn't require us to introspect it; the
		// SKIN_DETAILS_GET / THUMBNAIL_GET queries themselves are what commits the slot.
		Report(TotalBytes, 0.f, TEXT("Done"));
		OutError.Reset();
		return true;
	}
}
