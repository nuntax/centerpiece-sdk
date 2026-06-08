#include "HidDevice.h"
#include "Logging/LogMacros.h"

// File-scoped log category so HID enumeration logs land in the editor's Output Log without
// linker dependency on the one defined in CenterpieceFlasher.cpp.
DEFINE_LOG_CATEGORY_STATIC(LogCenterpieceFlasherHid, Log, All);

#define HID_LOG(Verbosity, Format, ...) UE_LOG(LogCenterpieceFlasherHid, Verbosity, Format, ##__VA_ARGS__)

#if PLATFORM_WINDOWS

// SetupAPI.h conditionally compiles around struct versioning macros (USE_SP_ALTPLATFORM_INFO_V*
// etc). UE builds with /WX and warning C4668 (undefined preprocessor macro treated as 0), so
// without these explicit zeros the include fails to compile under MSVC. Defining them to 0
// matches what the header itself does as the default branch.
#ifndef USE_SP_ALTPLATFORM_INFO_V1
#define USE_SP_ALTPLATFORM_INFO_V1 0
#endif
#ifndef USE_SP_ALTPLATFORM_INFO_V3
#define USE_SP_ALTPLATFORM_INFO_V3 0
#endif
#ifndef USE_SP_DRVINFO_DATA_V1
#define USE_SP_DRVINFO_DATA_V1 0
#endif
#ifndef USE_SP_BACKUP_QUEUE_PARAMS_V1
#define USE_SP_BACKUP_QUEUE_PARAMS_V1 0
#endif
#ifndef USE_SP_INF_SIGNER_INFO_V1
#define USE_SP_INF_SIGNER_INFO_V1 0
#endif

#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include <SetupAPI.h>
extern "C" {
#include <hidsdi.h>
}
#include "Windows/HideWindowsPlatformTypes.h"

namespace
{
	// RAII wrapper for the SetupAPI device-info enumerator so we don't leak on early returns.
	struct FDevInfo
	{
		HDEVINFO Handle = INVALID_HANDLE_VALUE;
		~FDevInfo() { if (Handle != INVALID_HANDLE_VALUE) SetupDiDestroyDeviceInfoList(Handle); }
	};
}

FCenterpieceHid::FCenterpieceHid() = default;

FCenterpieceHid::~FCenterpieceHid()
{
	Close();
}

bool FCenterpieceHid::Open(uint16 VendorId, uint16 ProductId, uint16 UsagePage, uint16 Usage, FString& OutError)
{
	Close();

	UE_LOG(LogCenterpieceFlasherHid, Display, TEXT("HID: opening; want VID=0x%04X PID=0x%04X UsagePage=0x%04X Usage=0x%04X"),
		VendorId, ProductId, UsagePage, Usage);

	GUID HidGuid;
	HidD_GetHidGuid(&HidGuid);

	FDevInfo DevInfo;
	DevInfo.Handle = SetupDiGetClassDevsW(&HidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
	if (DevInfo.Handle == INVALID_HANDLE_VALUE)
	{
		OutError = FString::Printf(TEXT("SetupDiGetClassDevs failed (err=%lu)"), GetLastError());
		UE_LOG(LogCenterpieceFlasherHid, Error, TEXT("HID: %s"), *OutError);
		return false;
	}

	SP_DEVICE_INTERFACE_DATA InterfaceData{};
	InterfaceData.cbSize = sizeof(InterfaceData);

	int32 Considered = 0;
	int32 VidPidMatches = 0;

	for (DWORD i = 0; SetupDiEnumDeviceInterfaces(DevInfo.Handle, nullptr, &HidGuid, i, &InterfaceData); i++)
	{
		Considered++;
		// First call sizes the buffer for the device path; second call fills it in.
		DWORD RequiredSize = 0;
		SetupDiGetDeviceInterfaceDetailW(DevInfo.Handle, &InterfaceData, nullptr, 0, &RequiredSize, nullptr);
		if (RequiredSize == 0)
		{
			continue;
		}

		TArray<uint8> DetailStorage;
		DetailStorage.SetNumZeroed(RequiredSize);
		auto* Detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(DetailStorage.GetData());
		Detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

		if (!SetupDiGetDeviceInterfaceDetailW(DevInfo.Handle, &InterfaceData, Detail, RequiredSize, nullptr, nullptr))
		{
			continue;
		}

		// Open with overlapped flag so the read path can use a per-call event for timeouts.
		// Writes still complete inline because we wait on every one.
		HANDLE Candidate = CreateFileW(
			Detail->DevicePath,
			GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			nullptr,
			OPEN_EXISTING,
			FILE_FLAG_OVERLAPPED,
			nullptr);
		if (Candidate == INVALID_HANDLE_VALUE)
		{
			continue;
		}

		// Filter by VID/PID first (cheap), then usagePage/usage via the preparsed caps.
		HIDD_ATTRIBUTES Attrs{};
		Attrs.Size = sizeof(Attrs);
		if (!HidD_GetAttributes(Candidate, &Attrs))
		{
			CloseHandle(Candidate);
			continue;
		}
		if (Attrs.VendorID != VendorId || Attrs.ProductID != ProductId)
		{
			CloseHandle(Candidate);
			continue;
		}
		VidPidMatches++;

		PHIDP_PREPARSED_DATA Preparsed = nullptr;
		if (!HidD_GetPreparsedData(Candidate, &Preparsed))
		{
			CloseHandle(Candidate);
			continue;
		}

		HIDP_CAPS Caps{};
		const NTSTATUS CapStatus = HidP_GetCaps(Preparsed, &Caps);
		HidD_FreePreparsedData(Preparsed);
		if (CapStatus != HIDP_STATUS_SUCCESS)
		{
			CloseHandle(Candidate);
			continue;
		}

		UE_LOG(LogCenterpieceFlasherHid, Display,
			TEXT("HID: VID/PID matched candidate #%d  UsagePage=0x%04X Usage=0x%04X (want 0x%04X / 0x%04X)"),
			VidPidMatches, Caps.UsagePage, Caps.Usage, UsagePage, Usage);

		if (Caps.UsagePage != UsagePage || Caps.Usage != Usage)
		{
			CloseHandle(Candidate);
			continue;
		}

		UE_LOG(LogCenterpieceFlasherHid, Display,
			TEXT("HID: opened. OutputReportByteLength=%d InputReportByteLength=%d"),
			(int32)Caps.OutputReportByteLength, (int32)Caps.InputReportByteLength);

		DeviceHandle = Candidate;
		MaxOutputReport = Caps.OutputReportByteLength;
		MaxInputReport = Caps.InputReportByteLength;
		return true;
	}

	OutError = FString::Printf(
		TEXT("No matching HID interface. Considered=%d, VID/PID matched=%d. Plug the keyboard in, close other apps that might hold the handle (xpanel etc.), or check the VID/PID/usage in Project Settings."),
		Considered, VidPidMatches);
	UE_LOG(LogCenterpieceFlasherHid, Warning, TEXT("HID: %s"), *OutError);
	return false;
}

void FCenterpieceHid::Close()
{
	if (DeviceHandle)
	{
		CloseHandle(static_cast<HANDLE>(DeviceHandle));
		DeviceHandle = nullptr;
	}
	MaxOutputReport = 0;
	MaxInputReport = 0;
}

bool FCenterpieceHid::IsOpen() const
{
	return DeviceHandle != nullptr;
}

bool FCenterpieceHid::WriteReport(const uint8* Bytes, int32 NumBytes, FString& OutError)
{
	if (!DeviceHandle)
	{
		OutError = TEXT("Device not open.");
		return false;
	}

	OVERLAPPED Overlapped{};
	Overlapped.hEvent = CreateEventW(nullptr, 1, 0, nullptr);

	DWORD Written = 0;
	BOOL Ok = WriteFile(static_cast<HANDLE>(DeviceHandle), Bytes, static_cast<DWORD>(NumBytes), &Written, &Overlapped);
	if (!Ok && GetLastError() != ERROR_IO_PENDING)
	{
		OutError = FString::Printf(TEXT("WriteFile failed (err=%lu)"), GetLastError());
		CloseHandle(Overlapped.hEvent);
		return false;
	}

	if (!Ok)
	{
		// Pending: block until it completes. Short timeout is fine, USB writes finish in ms.
		const DWORD Wait = WaitForSingleObject(Overlapped.hEvent, 5000);
		if (Wait != WAIT_OBJECT_0)
		{
			CancelIoEx(static_cast<HANDLE>(DeviceHandle), &Overlapped);
			OutError = TEXT("Output report timed out.");
			CloseHandle(Overlapped.hEvent);
			return false;
		}
		if (!GetOverlappedResult(static_cast<HANDLE>(DeviceHandle), &Overlapped, &Written, 0))
		{
			OutError = FString::Printf(TEXT("GetOverlappedResult (write) failed (err=%lu)"), GetLastError());
			CloseHandle(Overlapped.hEvent);
			return false;
		}
	}
	CloseHandle(Overlapped.hEvent);

	if (static_cast<int32>(Written) != NumBytes)
	{
		OutError = FString::Printf(TEXT("Short write: %lu of %d bytes."), Written, NumBytes);
		return false;
	}
	return true;
}

bool FCenterpieceHid::ReadReport(TArray<uint8>& OutBytes, uint32 TimeoutMs, FString& OutError)
{
	if (!DeviceHandle)
	{
		OutError = TEXT("Device not open.");
		return false;
	}

	OutBytes.SetNumUninitialized(MaxInputReport > 0 ? MaxInputReport : 64);

	OVERLAPPED Overlapped{};
	Overlapped.hEvent = CreateEventW(nullptr, 1, 0, nullptr);

	DWORD Read = 0;
	BOOL Ok = ReadFile(static_cast<HANDLE>(DeviceHandle), OutBytes.GetData(), static_cast<DWORD>(OutBytes.Num()), &Read, &Overlapped);
	if (!Ok && GetLastError() != ERROR_IO_PENDING)
	{
		OutError = FString::Printf(TEXT("ReadFile failed (err=%lu)"), GetLastError());
		CloseHandle(Overlapped.hEvent);
		return false;
	}

	if (!Ok)
	{
		const DWORD Wait = WaitForSingleObject(Overlapped.hEvent, TimeoutMs);
		if (Wait != WAIT_OBJECT_0)
		{
			CancelIoEx(static_cast<HANDLE>(DeviceHandle), &Overlapped);
			OutError = TEXT("Read timed out.");
			CloseHandle(Overlapped.hEvent);
			OutBytes.Reset();
			return false;
		}
		if (!GetOverlappedResult(static_cast<HANDLE>(DeviceHandle), &Overlapped, &Read, 0))
		{
			OutError = FString::Printf(TEXT("GetOverlappedResult (read) failed (err=%lu)"), GetLastError());
			CloseHandle(Overlapped.hEvent);
			OutBytes.Reset();
			return false;
		}
	}
	CloseHandle(Overlapped.hEvent);

	OutBytes.SetNum(Read, /*bAllowShrinking=*/false);
	return Read > 0;
}

#else // !PLATFORM_WINDOWS

// Non-Windows builds: stub everything out. The .uplugin restricts the module to Win64 so
// these should never compile, but defining them keeps the build green if someone forces it.
FCenterpieceHid::FCenterpieceHid() = default;
FCenterpieceHid::~FCenterpieceHid() = default;
bool FCenterpieceHid::Open(uint16, uint16, uint16, uint16, FString& OutError) { OutError = TEXT("Centerpiece flasher is Win64-only."); return false; }
void FCenterpieceHid::Close() {}
bool FCenterpieceHid::IsOpen() const { return false; }
bool FCenterpieceHid::WriteReport(const uint8*, int32, FString& OutError) { OutError = TEXT("Centerpiece flasher is Win64-only."); return false; }
bool FCenterpieceHid::ReadReport(TArray<uint8>&, uint32, FString& OutError) { OutError = TEXT("Centerpiece flasher is Win64-only."); return false; }

#endif
