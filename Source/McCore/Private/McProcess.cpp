#include "McProcess.h"
#include "HAL/FileManager.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include <cstdio>

void McProcess::Print(const FString& Line)
{
	const FString Stamped = FString::Printf(TEXT("[%s] %s"), *FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M:%S")), *Line);
	fprintf(stdout, "%s
", TCHAR_TO_UTF8(*Stamped));
	fflush(stdout);
	static const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Logs/McServer.log"));
	FFileHelper::SaveStringToFile(Stamped + LINE_TERMINATOR, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
}
