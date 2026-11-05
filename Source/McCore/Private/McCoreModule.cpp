#include "McCore.h"
#include "McPerf.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogMcCore);

class FMcCoreModule : public FDefaultModuleImpl
{
public:
	virtual void StartupModule() override
	{
		McPerf::Start();
	}
};

IMPLEMENT_MODULE(FMcCoreModule, McCore);
