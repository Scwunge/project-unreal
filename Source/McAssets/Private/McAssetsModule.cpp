#include "McAssets.h"
#include "McBlockTextures.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogMcAssets);

class FMcAssetsModule : public FDefaultModuleImpl
{
public:
	virtual void ShutdownModule() override
	{
		FMcBlockTextures::Get().Shutdown();
	}
};

IMPLEMENT_MODULE(FMcAssetsModule, McAssets);
