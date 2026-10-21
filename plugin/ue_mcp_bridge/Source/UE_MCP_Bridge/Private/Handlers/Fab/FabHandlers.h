#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

// Fab asset importer. Fab is Epic's unified content marketplace; the engine
// ships an editor plugin (/Engine/Plugins/Fab) whose window is a web frontend
// (catalog browse, purchase, adding a listing to the library and producing a
// signed download URL all happen in fab.com's pages) sitting on top of a native
// download+import layer. This category drives the parts of that native layer
// that don't need the web frontend: login lifecycle and sign-in state, syncing
// and listing the user's owned library, inspecting/clearing the download cache,
// importing local source files, and handing a signed download URL to the
// plugin's own add-to-project workflows.
//
// The signed URL itself is out of the plugin's reach to produce: the web page
// generates it and passes it to UFabBrowserApi::AddToProject. A URL obtained
// elsewhere is usable here by reflection into that same function. Store catalog
// browsing and buying stay in the web window.
class FFabHandlers
{
public:
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
	static TSharedPtr<FJsonValue> Status(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> Login(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> Logout(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SyncLibrary(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListCached(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CacheInfo(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ClearCache(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ImportFile(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AuthState(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddToProject(const TSharedPtr<FJsonObject>& Params);
	// Defined in FabHandlers_Library.cpp, the one fab action that calls fab.com itself.
	static TSharedPtr<FJsonValue> Library(const TSharedPtr<FJsonObject>& Params);
};
