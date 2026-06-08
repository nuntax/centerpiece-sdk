#include "CenterpieceFlasherCommands.h"
#include "EditorStyleSet.h"

#define LOCTEXT_NAMESPACE "FCenterpieceFlasherModule"

FCenterpieceFlasherCommands::FCenterpieceFlasherCommands()
	: TCommands<FCenterpieceFlasherCommands>(
		TEXT("CenterpieceFlasher"),
		NSLOCTEXT("Contexts", "CenterpieceFlasher", "Centerpiece Flasher"),
		NAME_None,
		FEditorStyle::GetStyleSetName())
{
}

void FCenterpieceFlasherCommands::RegisterCommands()
{
	UI_COMMAND(
		OpenFlashDialog,
		"Flash skin",
		"Cook the project and push a pak chunk to the Centerpiece Pro.",
		EUserInterfaceActionType::Button,
		FInputChord());
}

#undef LOCTEXT_NAMESPACE
