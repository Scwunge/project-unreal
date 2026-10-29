#include "McAssets.h"
#include "McBlockTextures.h"
#include "McHdBlocks.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogMcAssets);

class FMcAssetsModule : public FDefaultModuleImpl
{
public:
	virtual void ShutdownModule() override
	{
		FMcHdBlocks::Get().Shutdown();
		FMcBlockTextures::Get().Shutdown();
	}
};

IMPLEMENT_MODULE(FMcAssetsModule, McAssets);
