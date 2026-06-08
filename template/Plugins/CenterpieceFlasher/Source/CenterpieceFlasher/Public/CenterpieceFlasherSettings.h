#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CenterpieceFlasherSettings.generated.h"

UCLASS(config = EditorPerProjectUserSettings, defaultconfig, meta = (DisplayName = "Centerpiece Flasher"))
class CENTERPIECEFLASHER_API UCenterpieceFlasherSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// The pakchunk number to flash. After cooking, the cooker produces
	// pakchunk<N>-<Platform>.pak inside Saved/StagedBuilds/<Platform>/<Project>/Content/Paks.
	UPROPERTY(config, EditAnywhere, Category = "Flash", meta = (ClampMin = 0, ClampMax = 65535))
	int32 DefaultChunkID = 1337;

	// Default destination slot shown in the dialog. UI value matches what gets sent to the
	// device (1..5).
	UPROPERTY(config, EditAnywhere, Category = "Flash", meta = (ClampMin = 1, ClampMax = 5))
	int32 DefaultSlot = 1;

	// UAT's -platform= value. For the Centerpiece Pro this is "Android"; the texture format
	// flavor goes in CookFlavor below, which UAT treats as a separate flag.
	UPROPERTY(config, EditAnywhere, Category = "Cook")
	FString CookPlatform = TEXT("Android");

	// UAT's -cookflavor= value. The Centerpiece Pro uses ASTC textures. Leave empty if your
	// target platform has no flavors (e.g. Win64).
	UPROPERTY(config, EditAnywhere, Category = "Cook")
	FString CookFlavor = TEXT("ASTC");

	// Client config used by the cook (Development matches the working CLI invocation).
	UPROPERTY(config, EditAnywhere, Category = "Cook")
	FString ClientConfig = TEXT("Development");

	// Extra UAT BuildCookRun flags appended verbatim. Use this for project-specific overrides
	// (e.g. -CookCultures=, -map=) without editing the plugin.
	UPROPERTY(config, EditAnywhere, Category = "Cook", meta = (MultiLine = true))
	FString CookExtraArgs;

	// HID device filter. Defaults match the Centerpiece Pro's vendor command interface.
	UPROPERTY(config, EditAnywhere, Category = "Device", meta = (ClampMin = 0, ClampMax = 65535))
	int32 VendorID = 13853;

	UPROPERTY(config, EditAnywhere, Category = "Device", meta = (ClampMin = 0, ClampMax = 65535))
	int32 ProductID = 514;

	// Usage page + usage of the specific HID collection on the keyboard that owns the vendor
	// command channel. Devices with the same VID/PID expose multiple HID collections; without
	// this filter we'd silently grab the wrong one.
	UPROPERTY(config, EditAnywhere, Category = "Device", meta = (ClampMin = 0, ClampMax = 65535))
	int32 UsagePage = 0xFF00;

	UPROPERTY(config, EditAnywhere, Category = "Device", meta = (ClampMin = 0, ClampMax = 65535))
	int32 Usage = 1;

	virtual FName GetContainerName() const override { return TEXT("Project"); }
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
	virtual FName GetSectionName() const override { return TEXT("Centerpiece Flasher"); }
};
