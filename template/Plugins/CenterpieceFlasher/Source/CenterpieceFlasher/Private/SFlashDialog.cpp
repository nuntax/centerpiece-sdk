#include "SFlashDialog.h"
#include "CenterpieceFlasherSettings.h"
#include "FlashProtocol.h"
#include "HidDevice.h"

#include "Logging/LogMacros.h"
DEFINE_LOG_CATEGORY_STATIC(LogFlashDialog, Log, All);

#include "Async/Async.h"
#include "DesktopPlatformModule.h"
#include "HAL/FileManager.h"
#include "IDesktopPlatform.h"
#include "Interfaces/IMainFrameModule.h"
#include "Misc/FileHelper.h"
#include "Misc/MonitoredProcess.h"
#include "Misc/Paths.h"
#include "EditorStyleSet.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FCenterpieceFlasherModule"

void SFlashDialog::Construct(const FArguments& InArgs)
{
	ParentWindow = InArgs._ParentWindow;

	const UCenterpieceFlasherSettings* Settings = GetDefault<UCenterpieceFlasherSettings>();
	ChunkID = Settings->DefaultChunkID;
	SelectedSlot = Settings->DefaultSlot;

	// Populate the 1..5 slot list. Indices are 1-based in the UI, the protocol gets slot-1.
	SlotOptions.Reset();
	for (int32 i = 1; i <= 5; ++i)
	{
		SlotOptions.Add(MakeShared<int32>(i));
	}

	ChildSlot
	[
		SNew(SBorder)
		.Padding(FMargin(16))
		.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
		[
			SNew(SVerticalBox)

			// ── form row 1: slot + chunk ──────────────────────────────────────
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(0, 0, 0, 8))
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(0, 0, 8, 0))
				[
					SNew(STextBlock).Text(LOCTEXT("SlotLabel", "Slot"))
				]

				+ SHorizontalBox::Slot()
				.FillWidth(0.4f)
				.VAlign(VAlign_Center)
				.Padding(FMargin(0, 0, 16, 0))
				[
					SAssignNew(SlotCombo, SComboBox<TSharedPtr<int32>>)
					.OptionsSource(&SlotOptions)
					.InitiallySelectedItem(SlotOptions.IsValidIndex(SelectedSlot - 1) ? SlotOptions[SelectedSlot - 1] : SlotOptions[0])
					.OnGenerateWidget(this, &SFlashDialog::MakeSlotItem)
					.OnSelectionChanged(this, &SFlashDialog::OnSlotChanged)
					[
						SNew(STextBlock).Text(this, &SFlashDialog::GetSelectedSlotText)
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(0, 0, 8, 0))
				[
					SNew(STextBlock).Text(LOCTEXT("ChunkLabel", "Pak chunk ID"))
				]

				+ SHorizontalBox::Slot()
				.FillWidth(0.6f)
				.VAlign(VAlign_Center)
				[
					SAssignNew(ChunkSpin, SSpinBox<int32>)
					.MinValue(0)
					.MaxValue(65535)
					.Value(ChunkID)
					.OnValueChanged_Lambda([this](int32 V) { ChunkID = V; })
				]
			]

			// ── form row 2: use-prebuilt checkbox ───────────────────────────────
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(0, 0, 0, 4))
			[
				SAssignNew(UsePrebuiltCheck, SCheckBox)
				.IsChecked(ECheckBoxState::Unchecked)
				.OnCheckStateChanged(this, &SFlashDialog::OnUsePrebuiltChanged)
				[
					SNew(STextBlock).Text(LOCTEXT("UsePrebuilt", "Use an existing pak file (skip cook)"))
				]
			]

			// ── form row 3: pak file path ─────────────────────────────────────
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(0, 0, 0, 12))
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				.Padding(FMargin(0, 0, 8, 0))
				[
					SAssignNew(PrebuiltPathBox, SEditableTextBox)
					.IsEnabled(false)
					.HintText(LOCTEXT("PathHint", "Path to .pak (used only when checkbox above is on)"))
					.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type)
					{
						PrebuiltPath = Text.ToString();
					})
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SAssignNew(PickPakButton, SButton)
					.IsEnabled(false)
					.Text(LOCTEXT("Browse", "Browse..."))
					.OnClicked(this, &SFlashDialog::OnPickPakClicked)
				]
			]

			+ SVerticalBox::Slot().AutoHeight()[SNew(SSeparator)]

			// ── status text ───────────────────────────────────────────────────
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(0, 12, 0, 4))
			[
				SAssignNew(StatusText, STextBlock)
				.Text(LOCTEXT("StatusIdle", "Idle. Pick slot + chunk, then click Flash."))
			]

			// ── progress bar ──────────────────────────────────────────────────
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(0, 0, 0, 8))
			[
				SAssignNew(ProgressBar, SProgressBar).Percent(0.f)
			]

			// ── log box ───────────────────────────────────────────────────────
			+ SVerticalBox::Slot()
			.FillHeight(1.f)
			.Padding(FMargin(0, 0, 0, 12))
			[
				SAssignNew(LogBox, SMultiLineEditableTextBox)
				.IsReadOnly(true)
				.AlwaysShowScrollbars(true)
				.AutoWrapText(false)
			]

			// ── buttons ───────────────────────────────────────────────────────
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot().FillWidth(1.f)[SNew(SSpacer)]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(FMargin(0, 0, 8, 0))
				[
					SNew(SButton)
					.Text(LOCTEXT("Close", "Close"))
					.OnClicked(this, &SFlashDialog::OnCloseClicked)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SAssignNew(FlashButton, SButton)
					.Text(LOCTEXT("Flash", "Flash"))
					.OnClicked(this, &SFlashDialog::OnFlashClicked)
				]
			]
		]
	];
}

SFlashDialog::~SFlashDialog()
{
	if (CookProcess.IsValid())
	{
		CookProcess->Cancel(true);
		CookProcess.Reset();
	}
}

// ── small helpers ───────────────────────────────────────────────────────────

TSharedRef<SWidget> SFlashDialog::MakeSlotItem(TSharedPtr<int32> Item)
{
	return SNew(STextBlock).Text(FText::AsNumber(Item.IsValid() ? *Item : 0));
}

void SFlashDialog::OnSlotChanged(TSharedPtr<int32> NewValue, ESelectInfo::Type)
{
	if (NewValue.IsValid())
	{
		SelectedSlot = *NewValue;
	}
}

FText SFlashDialog::GetSelectedSlotText() const
{
	return FText::AsNumber(SelectedSlot);
}

void SFlashDialog::OnUsePrebuiltChanged(ECheckBoxState NewState)
{
	bUsePrebuilt = (NewState == ECheckBoxState::Checked);
	if (PrebuiltPathBox.IsValid()) PrebuiltPathBox->SetEnabled(bUsePrebuilt);
	if (PickPakButton.IsValid())   PickPakButton->SetEnabled(bUsePrebuilt);
}

FReply SFlashDialog::OnPickPakClicked()
{
	IDesktopPlatform* DP = FDesktopPlatformModule::Get();
	if (!DP) return FReply::Handled();

	const void* ParentHandle = nullptr;
	if (TSharedPtr<SWindow> Win = ParentWindow.Pin())
	{
		ParentHandle = Win->GetNativeWindow().IsValid() ? Win->GetNativeWindow()->GetOSWindowHandle() : nullptr;
	}

	TArray<FString> Chosen;
	if (DP->OpenFileDialog(
		ParentHandle,
		TEXT("Pick a Centerpiece pak"),
		FPaths::ProjectSavedDir() / TEXT("StagedBuilds"),
		TEXT(""),
		TEXT("Centerpiece pak (*.pak)|*.pak"),
		EFileDialogFlags::None,
		Chosen) && Chosen.Num() > 0)
	{
		PrebuiltPath = Chosen[0];
		if (PrebuiltPathBox.IsValid()) PrebuiltPathBox->SetText(FText::FromString(PrebuiltPath));
	}
	return FReply::Handled();
}

FReply SFlashDialog::OnCloseClicked()
{
	if (TSharedPtr<SWindow> Win = ParentWindow.Pin())
	{
		Win->RequestDestroyWindow();
	}
	return FReply::Handled();
}

void SFlashDialog::AppendLog(const FString& Line)
{
	// Game-thread-only fast path: stash the line in the pending buffer and ensure a single
	// game-thread flush is scheduled. OnCookOutput (worker thread) takes the same path.
	bool bShouldSchedule = false;
	{
		FScopeLock Lock(&LogCS);
		PendingLog += Line;
		PendingLog += TEXT("\n");
		if (!bLogFlushScheduled)
		{
			bLogFlushScheduled = true;
			bShouldSchedule = true;
		}
	}
	if (bShouldSchedule)
	{
		TWeakPtr<SFlashDialog> WeakSelf = SharedThis(this);
		AsyncTask(ENamedThreads::GameThread, [WeakSelf]()
		{
			if (TSharedPtr<SFlashDialog> Self = WeakSelf.Pin())
			{
				Self->FlushPendingLog();
			}
		});
	}
}

void SFlashDialog::FlushPendingLog()
{
	if (!LogBox.IsValid()) return;

	FString Batch;
	{
		FScopeLock Lock(&LogCS);
		Batch = MoveTemp(PendingLog);
		PendingLog.Reset();
		bLogFlushScheduled = false;
	}
	if (Batch.IsEmpty()) return;

	// Split the batch into lines and append, then trim to MAX_LOG_LINES. SetText is called
	// at most once per flush regardless of how many lines came in, so cook output (thousands
	// of lines) doesn't lock the game thread.
	TArray<FString> NewLines;
	Batch.ParseIntoArrayLines(NewLines, /*InCullEmpty=*/false);
	LogLines.Append(MoveTemp(NewLines));

	if (LogLines.Num() > MAX_LOG_LINES)
	{
		LogLines.RemoveAt(0, LogLines.Num() - MAX_LOG_LINES, /*bAllowShrinking=*/false);
	}

	LogBox->SetText(FText::FromString(FString::Join(LogLines, TEXT("\n"))));
}

void SFlashDialog::SetStatus(const FString& Line)
{
	if (StatusText.IsValid())
	{
		StatusText->SetText(FText::FromString(Line));
	}
}

void SFlashDialog::SetProgress(float Fraction)
{
	if (ProgressBar.IsValid())
	{
		ProgressBar->SetPercent(FMath::Clamp(Fraction, 0.f, 1.f));
	}
}

void SFlashDialog::SetRunning(bool bInRunning)
{
	bRunning = bInRunning;
	if (FlashButton.IsValid())     FlashButton->SetEnabled(!bRunning);
	if (UsePrebuiltCheck.IsValid()) UsePrebuiltCheck->SetEnabled(!bRunning);
	if (ChunkSpin.IsValid())        ChunkSpin->SetEnabled(!bRunning);
	if (SlotCombo.IsValid())        SlotCombo->SetEnabled(!bRunning);
	if (PrebuiltPathBox.IsValid())  PrebuiltPathBox->SetEnabled(!bRunning && bUsePrebuilt);
	if (PickPakButton.IsValid())    PickPakButton->SetEnabled(!bRunning && bUsePrebuilt);
}

// ── flash button: cook (or skip) then flash ─────────────────────────────────

FReply SFlashDialog::OnFlashClicked()
{
	if (bRunning) return FReply::Handled();

	SetRunning(true);
	SetProgress(0.f);

	if (bUsePrebuilt)
	{
		if (PrebuiltPath.IsEmpty() || !FPaths::FileExists(PrebuiltPath))
		{
			FinishWithError(TEXT("Pak file path is empty or not found."));
			return FReply::Handled();
		}
		StartFlash(PrebuiltPath);
	}
	else
	{
		StartCookAndFlash();
	}
	return FReply::Handled();
}

void SFlashDialog::StartCookAndFlash()
{
	const UCenterpieceFlasherSettings* Settings = GetDefault<UCenterpieceFlasherSettings>();
	const FString Project = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
	const FString EngineDir = FPaths::ConvertRelativePathToFull(FPaths::EngineDir());
#if PLATFORM_WINDOWS
	const FString Uat = EngineDir / TEXT("Build/BatchFiles/RunUAT.bat");
#else
	const FString Uat = EngineDir / TEXT("Build/BatchFiles/RunUAT.sh");
#endif

	// Matches the working CLI invocation but with -compressed dropped: the inline-edit
	// website needs to pattern-scan + splice the pak, and Oodle (UE 4.27 default compression)
	// has no browser implementation. The skin pak is the only one ever flashed, so cooking
	// the whole stage uncompressed costs us nothing on-device.
	FString FlavorArg;
	if (!Settings->CookFlavor.IsEmpty())
	{
		FlavorArg = FString::Printf(TEXT(" -cookflavor=%s"), *Settings->CookFlavor);
	}
	const FString Args = FString::Printf(
		TEXT("BuildCookRun -project=\"%s\" -noP4 -platform=%s -clientconfig=%s -cook -stage -pak -skipbuild%s %s"),
		*Project,
		*Settings->CookPlatform,
		*Settings->ClientConfig,
		*FlavorArg,
		*Settings->CookExtraArgs);

	SetStatus(TEXT("Cooking..."));
	AppendLog(FString::Printf(TEXT("$ RunUAT %s"), *Args));

	CookProcess = MakeShared<FMonitoredProcess>(Uat, Args, /*bHidden=*/true);
	CookProcess->OnOutput().BindRaw(this, &SFlashDialog::OnCookOutput);
	CookProcess->OnCanceled().BindRaw(this, &SFlashDialog::OnCookCanceled);
	CookProcess->OnCompleted().BindRaw(this, &SFlashDialog::OnCookCompleted);

	if (!CookProcess->Launch())
	{
		CookProcess.Reset();
		FinishWithError(TEXT("Failed to launch RunUAT. Check the engine path."));
	}
}

void SFlashDialog::OnCookOutput(FString Line)
{
	// AppendLog is thread-safe and self-marshals to the game thread for the actual UI write,
	// batching multiple lines per flush. Calling it directly from this worker thread is fine
	// and avoids redundant scheduling.
	AppendLog(Line);
}

void SFlashDialog::OnCookCanceled()
{
	AsyncTask(ENamedThreads::GameThread, [this]()
	{
		FinishWithError(TEXT("Cook canceled."));
	});
}

void SFlashDialog::OnCookCompleted(int32 ReturnCode)
{
	AsyncTask(ENamedThreads::GameThread, [this, ReturnCode]()
	{
		CookProcess.Reset();
		if (ReturnCode != 0)
		{
			FinishWithError(FString::Printf(TEXT("UAT exited with code %d."), ReturnCode));
			return;
		}

		const UCenterpieceFlasherSettings* Settings = GetDefault<UCenterpieceFlasherSettings>();
		// StagedBuilds subdir name combines platform + flavor (e.g. Android_ASTC).
		const FString StagedDirName = Settings->CookFlavor.IsEmpty()
			? Settings->CookPlatform
			: FString::Printf(TEXT("%s_%s"), *Settings->CookPlatform, *Settings->CookFlavor);
		const FString PakPath = FindPakInStagedBuilds(ChunkID, StagedDirName);
		if (PakPath.IsEmpty())
		{
			FinishWithError(FString::Printf(
				TEXT("Cook finished but pakchunk%d-*.pak was not found under Saved/StagedBuilds/%s. Check the project's chunk config."),
				ChunkID, *StagedDirName));
			return;
		}

		AppendLog(FString::Printf(TEXT("Found pak: %s"), *PakPath));
		StartFlash(PakPath);
	});
}

FString SFlashDialog::FindPakInStagedBuilds(int32 InChunkID, const FString& PlatformName) const
{
	// Standard staged output:
	//   <Project>/Saved/StagedBuilds/<Platform>/<Project>/Content/Paks/pakchunk<N>-<Platform>.pak
	// Some flavours (e.g. Android_ASTC variants) use slightly different platform names in the
	// pak filename, so we glob the Paks dir rather than assuming the exact suffix.
	const FString StagedDir = FPaths::ProjectSavedDir() / TEXT("StagedBuilds") / PlatformName;
	if (!FPaths::DirectoryExists(StagedDir))
	{
		return FString();
	}

	const FString Wanted = FString::Printf(TEXT("pakchunk%d-*.pak"), InChunkID);

	TArray<FString> Found;
	IFileManager::Get().FindFilesRecursive(Found, *StagedDir, *Wanted, /*Files=*/true, /*Dirs=*/false);
	return Found.Num() > 0 ? Found[0] : FString();
}

// ── flash: read pak bytes, then run protocol on a worker thread ─────────────

void SFlashDialog::StartFlash(const FString& PakPath)
{
	SetStatus(TEXT("Reading pak..."));
	SetProgress(0.f);

	TArray<uint8> PakBytes;
	if (!FFileHelper::LoadFileToArray(PakBytes, *PakPath))
	{
		FinishWithError(FString::Printf(TEXT("Failed to read pak: %s"), *PakPath));
		return;
	}

	const UCenterpieceFlasherSettings* Settings = GetDefault<UCenterpieceFlasherSettings>();
	const uint16 VendorId  = static_cast<uint16>(Settings->VendorID);
	const uint16 ProductId = static_cast<uint16>(Settings->ProductID);
	const uint16 UsagePage = static_cast<uint16>(Settings->UsagePage);
	const uint16 Usage     = static_cast<uint16>(Settings->Usage);

	FFlashOptions Options;
	Options.Data = MoveTemp(PakBytes);
	Options.FileName = FPaths::GetBaseFilename(PakPath);
	Options.FileExtension = TEXT("pak");
	Options.Slot = FMath::Clamp(SelectedSlot, 1, 5); // wire format matches the UI value (1..5)

	const FString PakDisplay = PakPath;
	AppendLog(FString::Printf(TEXT("Flashing %s (slot %d, %d bytes)"), *PakDisplay, SelectedSlot, Options.Data.Num()));

	// Capture a weak handle into the lambda so the dialog can close while the flash runs
	// without dereferencing freed memory.
	TWeakPtr<SFlashDialog> WeakSelf = SharedThis(this);

	UE_LOG(LogFlashDialog, Display, TEXT("Dispatching flash worker task..."));

	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask,
		[WeakSelf, Options = MoveTemp(Options), VendorId, ProductId, UsagePage, Usage]() mutable
	{
		UE_LOG(LogFlashDialog, Display, TEXT("Flash worker started."));
		FCenterpieceHid Hid;
		FString OpenErr;
		const bool bOpened = Hid.Open(VendorId, ProductId, UsagePage, Usage, OpenErr);
		UE_LOG(LogFlashDialog, Display, TEXT("Flash worker: Hid.Open returned %s (err: %s)"),
			bOpened ? TEXT("true") : TEXT("false"), *OpenErr);
		if (!bOpened)
		{
			AsyncTask(ENamedThreads::GameThread, [WeakSelf, OpenErr]()
			{
				if (TSharedPtr<SFlashDialog> Self = WeakSelf.Pin())
				{
					Self->FinishWithError(OpenErr);
				}
			});
			return;
		}

		FFlashProgressDelegate Progress;
		Progress.BindLambda([WeakSelf](int64 Sent, int64 Total, float MBps, const FString& Phase)
		{
			AsyncTask(ENamedThreads::GameThread, [WeakSelf, Sent, Total, MBps, Phase]()
			{
				if (TSharedPtr<SFlashDialog> Self = WeakSelf.Pin())
				{
					const float Frac = Total > 0 ? static_cast<float>(static_cast<double>(Sent) / static_cast<double>(Total)) : 0.f;
					Self->SetProgress(Frac);
					Self->SetStatus(FString::Printf(TEXT("%s  %lld / %lld bytes  (%.1f MB/s)"), *Phase, Sent, Total, MBps));
				}
			});
		});

		UE_LOG(LogFlashDialog, Display, TEXT("Flash worker: calling FFlasher::Run..."));
		FString FlashErr;
		const bool bOk = FFlasher::Run(Hid, Options, Progress, FlashErr);
		UE_LOG(LogFlashDialog, Display, TEXT("Flash worker: FFlasher::Run returned %s (err: %s)"),
			bOk ? TEXT("true") : TEXT("false"), *FlashErr);

		AsyncTask(ENamedThreads::GameThread, [WeakSelf, bOk, FlashErr]()
		{
			if (TSharedPtr<SFlashDialog> Self = WeakSelf.Pin())
			{
				if (bOk)
				{
					if (!FlashErr.IsEmpty()) Self->AppendLog(FlashErr); // soft warning (no confirmation reply)
					Self->FinishWithSuccess();
				}
				else
				{
					Self->FinishWithError(FlashErr);
				}
			}
		});
	});
}

void SFlashDialog::FinishWithError(const FString& Error)
{
	AppendLog(FString::Printf(TEXT("ERROR: %s"), *Error));
	SetStatus(FString::Printf(TEXT("Error: %s"), *Error));
	SetProgress(0.f);
	SetRunning(false);
}

void SFlashDialog::FinishWithSuccess()
{
	SetStatus(TEXT("Flash complete."));
	SetProgress(1.f);
	SetRunning(false);
}

#undef LOCTEXT_NAMESPACE
