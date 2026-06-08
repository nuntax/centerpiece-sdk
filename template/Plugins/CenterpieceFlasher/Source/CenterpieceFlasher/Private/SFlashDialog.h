#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SWindow;
class STextBlock;
class SProgressBar;
class SButton;
class SCheckBox;
class SMultiLineEditableTextBox;
class SEditableTextBox;
template <typename T> class SSpinBox;
template <typename T> class SComboBox;
class FMonitoredProcess;

// One-shot dialog: pick slot + chunk, cook (or load a prebuilt pak), then flash. Owns the
// async machinery for both phases. State flows Idle → Cooking → Flashing → Done | Error,
// and the buttons enable/disable based on bRunning.
class SFlashDialog : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFlashDialog) {}
		SLATE_ARGUMENT(TWeakPtr<SWindow>, ParentWindow)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual ~SFlashDialog();

private:
	// ── UI callbacks ─────────────────────────────────────────────────────────
	FReply OnFlashClicked();
	FReply OnCloseClicked();
	FReply OnPickPakClicked();
	void   OnUsePrebuiltChanged(ECheckBoxState NewState);
	void   OnSlotChanged(TSharedPtr<int32> NewValue, ESelectInfo::Type);
	TSharedRef<SWidget> MakeSlotItem(TSharedPtr<int32> Item);
	FText  GetSelectedSlotText() const;

	// ── Phase orchestration ──────────────────────────────────────────────────
	void StartCookAndFlash();
	void StartFlash(const FString& PakPath);
	void OnCookOutput(FString Line);
	void OnCookCanceled(); // FMonitoredProcess::OnCanceled is FSimpleDelegate in 4.27 (no args).
	void OnCookCompleted(int32 ReturnCode);
	void FinishWithError(const FString& Error);
	void FinishWithSuccess();

	FString FindPakInStagedBuilds(int32 ChunkID, const FString& PlatformName) const;

	// ── UI plumbing ──────────────────────────────────────────────────────────
	void AppendLog(const FString& Line);
	void FlushPendingLog();
	void SetStatus(const FString& Line);
	void SetProgress(float Fraction);
	void SetRunning(bool bRunning);

	// ── State ────────────────────────────────────────────────────────────────
	TWeakPtr<SWindow>            ParentWindow;
	TSharedPtr<STextBlock>       StatusText;
	TSharedPtr<SProgressBar>     ProgressBar;
	TSharedPtr<SMultiLineEditableTextBox> LogBox;
	TSharedPtr<SButton>          FlashButton;
	TSharedPtr<SButton>          PickPakButton;
	TSharedPtr<SCheckBox>        UsePrebuiltCheck;
	TSharedPtr<SEditableTextBox> PrebuiltPathBox;
	TSharedPtr<SSpinBox<int32>>  ChunkSpin;
	TSharedPtr<SComboBox<TSharedPtr<int32>>> SlotCombo;

	TArray<TSharedPtr<int32>>    SlotOptions;
	int32                        SelectedSlot = 1;     // 1..5 display
	int32                        ChunkID      = 1337;
	bool                         bUsePrebuilt = false;
	FString                      PrebuiltPath;

	TSharedPtr<FMonitoredProcess> CookProcess;
	bool                         bRunning = false;

	// Cook produces thousands of log lines; if we marshal each one to the game thread and
	// call SetText on the whole log box, the game thread freezes. Instead we accumulate
	// lines under a lock and flush once per batch on the game thread, and cap the log box
	// to the last N lines so SetText doesn't have to retokenise hundreds of KB each flush.
	FCriticalSection             LogCS;
	FString                      PendingLog;
	bool                         bLogFlushScheduled = false;
	TArray<FString>              LogLines;
	static constexpr int32       MAX_LOG_LINES = 400;
};
