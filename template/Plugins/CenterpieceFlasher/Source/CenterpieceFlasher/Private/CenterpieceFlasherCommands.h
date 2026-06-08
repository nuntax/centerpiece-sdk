#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"

class FCenterpieceFlasherCommands : public TCommands<FCenterpieceFlasherCommands>
{
public:
	FCenterpieceFlasherCommands();

	TSharedPtr<FUICommandInfo> OpenFlashDialog;

	virtual void RegisterCommands() override;
};
