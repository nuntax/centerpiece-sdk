#include "CenterpieceFlasher.h"
#include "CenterpieceFlasherCommands.h"
#include "SFlashDialog.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "EditorStyleSet.h"
#include "Interfaces/IMainFrameModule.h"
#include "LevelEditor.h"
#include "Textures/SlateIcon.h"
#include "Misc/CoreDelegates.h"
#include "Widgets/SWindow.h"

DEFINE_LOG_CATEGORY_STATIC(LogCenterpieceFlasher, Log, All);

#define LOCTEXT_NAMESPACE "FCenterpieceFlasherModule"

void FCenterpieceFlasherModule::StartupModule()
{
	UE_LOG(LogCenterpieceFlasher, Display, TEXT("CenterpieceFlasher: StartupModule"));

	FCenterpieceFlasherCommands::Register();

	CommandList = MakeShared<FUICommandList>();
	CommandList->MapAction(
		FCenterpieceFlasherCommands::Get().OpenFlashDialog,
		FExecuteAction::CreateRaw(this, &FCenterpieceFlasherModule::OpenDialog),
		FCanExecuteAction());

	// FExtender path: works in UE4.27 across all editor variants. Extends the "Game" section
	// of the level editor toolbar (next to Play / Launch). UToolMenus is the UE5 way but its
	// path-based menu names changed between 4.27 and 5.x, so we stay on this older API.
	ToolbarExtender = MakeShared<FExtender>();
	ToolbarExtender->AddToolBarExtension(
		TEXT("Game"),
		EExtensionHook::After,
		CommandList,
		FToolBarExtensionDelegate::CreateRaw(this, &FCenterpieceFlasherModule::AddToolbarButton));

	// If LevelEditor isn't up yet at StartupModule time (Default phase), the extender goes
	// into a manager that hasn't been picked up by the toolbar build. Belt-and-suspenders:
	// register now if possible, and also defer to OnPostEngineInit so a later toolbar build
	// definitely sees us.
	auto Register = [this]()
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
		LevelEditorModule.GetToolBarExtensibilityManager()->AddExtender(ToolbarExtender);
		UE_LOG(LogCenterpieceFlasher, Display, TEXT("CenterpieceFlasher: toolbar extender registered"));
	};

	if (FModuleManager::Get().IsModuleLoaded(TEXT("LevelEditor")))
	{
		Register();
	}
	else
	{
		FCoreDelegates::OnPostEngineInit.AddLambda(Register);
		UE_LOG(LogCenterpieceFlasher, Display, TEXT("CenterpieceFlasher: deferring toolbar registration to OnPostEngineInit"));
	}
}

void FCenterpieceFlasherModule::ShutdownModule()
{
	if (FModuleManager::Get().IsModuleLoaded(TEXT("LevelEditor")) && ToolbarExtender.IsValid())
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
		LevelEditorModule.GetToolBarExtensibilityManager()->RemoveExtender(ToolbarExtender);
	}
	ToolbarExtender.Reset();
	FCenterpieceFlasherCommands::Unregister();
}

void FCenterpieceFlasherModule::AddToolbarButton(FToolBarBuilder& Builder)
{
	Builder.BeginSection(TEXT("Centerpiece"));
	// Borrow the keyboard icon shipped with EditorStyle (used in the Keyboard Shortcuts editor).
	// Saves us shipping our own art for a single button.
	const FSlateIcon Icon(FEditorStyle::GetStyleSetName(), TEXT("InputBindingEditor.KeyboardSettings"));
	Builder.AddToolBarButton(
		FCenterpieceFlasherCommands::Get().OpenFlashDialog,
		NAME_None,
		TAttribute<FText>(),
		TAttribute<FText>(),
		Icon);
	Builder.EndSection();
}

void FCenterpieceFlasherModule::OpenDialog()
{
	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("FlashDialogTitle", "Flash to Centerpiece Pro"))
		.ClientSize(FVector2D(560, 440))
		.SupportsMaximize(false)
		.SupportsMinimize(false);

	TSharedRef<SFlashDialog> Dialog = SNew(SFlashDialog).ParentWindow(Window);
	Window->SetContent(Dialog);

	IMainFrameModule& MainFrame = FModuleManager::LoadModuleChecked<IMainFrameModule>(TEXT("MainFrame"));
	if (MainFrame.GetParentWindow().IsValid())
	{
		FSlateApplication::Get().AddWindowAsNativeChild(Window, MainFrame.GetParentWindow().ToSharedRef());
	}
	else
	{
		FSlateApplication::Get().AddWindow(Window);
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FCenterpieceFlasherModule, CenterpieceFlasher)
