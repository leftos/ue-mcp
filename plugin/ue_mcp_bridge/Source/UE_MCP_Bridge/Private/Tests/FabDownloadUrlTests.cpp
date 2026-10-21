// fab(add_to_project)'s URL check. UFabBrowserApi::AddToProject rewrites a
// leading \\ to file:// for an unreal-engine pack only, so a share path handed
// to any other workflow reaches a downloader that cannot read it.

#if WITH_DEV_AUTOMATION_TESTS

#include "Handlers/Fab/FabBrowserApiReflection.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFabDownloadUrlTest,
	"UE.MCP.Fab.DownloadUrlForm",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFabDownloadUrlTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("an https URL for a generic import"), MCPFab::AcceptsDownloadUrl(TEXT("https://example.com/model.glb"), TEXT("generic")));
	TestTrue(TEXT("an http URL for a Megascans import"), MCPFab::AcceptsDownloadUrl(TEXT("http://example.com/plant.zip"), TEXT("quixel")));
	TestTrue(TEXT("a file URL for a MetaHuman import"), MCPFab::AcceptsDownloadUrl(TEXT("file:///C:/Fab/character.zip"), TEXT("metahuman")));
	TestTrue(TEXT("a share path for a pack"), MCPFab::AcceptsDownloadUrl(TEXT("\\\\server\\share\\manifest"), TEXT("pack")));

	TestFalse(TEXT("a share path for a generic import"), MCPFab::AcceptsDownloadUrl(TEXT("\\\\server\\share\\model.glb"), TEXT("generic")));
	TestFalse(TEXT("a share path for a Megascans import"), MCPFab::AcceptsDownloadUrl(TEXT("\\\\server\\share\\plant.zip"), TEXT("quixel")));
	TestFalse(TEXT("a share path for a factory workflow"), MCPFab::AcceptsDownloadUrl(TEXT("\\\\server\\share\\asset"), TEXT("factory")));
	TestFalse(TEXT("a bare disk path for a pack"), MCPFab::AcceptsDownloadUrl(TEXT("C:/Fab/manifest"), TEXT("pack")));
	return true;
}

#endif
