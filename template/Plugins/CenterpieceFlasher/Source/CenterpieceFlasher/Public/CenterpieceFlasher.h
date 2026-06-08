#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FUICommandList;
class FExtender;
class FToolBarBuilder;

class FCenterpieceFlasherModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void AddToolbarButton(FToolBarBuilder& Builder);
	void OpenDialog();

	TSharedPtr<FUICommandList> CommandList;
	TSharedPtr<FExtender>      ToolbarExtender;
};
