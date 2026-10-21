#include "FabHandlers.h"
#include "FabBrowserApiReflection.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"

#include "UObject/UObjectHash.h"
#include "Misc/PackageName.h"
#include "Engine/Engine.h"

#ifndef WITH_FAB_PLUGIN
#define WITH_FAB_PLUGIN 0
#endif

#if WITH_FAB_PLUGIN
#include "Importers/GenericAssetImporter.h"
#include "Utilities/FabAssetsCache.h"
#endif

// Engines whose Fab plugin lets other plugins register workflows for asset types
// of their own; AddToProject hands those types to the registered factory.
#if WITH_FAB_PLUGIN && __has_include("FabWorkflowFactoryRegistry.h")
#include "FabWorkflowFactoryRegistry.h"
#define MCP_FAB_HAS_WORKFLOW_FACTORIES 1
#else
#define MCP_FAB_HAS_WORKFLOW_FACTORIES 0
#endif

// ─── Helpers ──────────────────────────────────────────────────────────────

// Run one of the Fab plugin's registered console commands (Fab.Login,
// Fab.Logout, Fab.ClearCache, Fab.TEDS.MyFolderIntegration, ...). These are
// FAutoConsoleCommands, so they route through GEngine->Exec without any
// compile-time dependency on the Fab module - the login/sync/clear paths work
// even when WITH_FAB_PLUGIN is off (as long as the module is loaded).
static bool RunFabConsoleCommand(const FString& Command)
{
	if (!GEngine) return false;
	return GEngine->Exec(nullptr, *Command);
}

// Whether a UFabBrowserApi (the JS<->native bridge object created with the Fab
// window) currently exists, the "has the Fab window been opened this session"
// signal. Reached by reflection so we never link the plugin's Private headers.
// Not an auth check: fab(auth_state) reads the sign-in.
static bool HasFabBrowserApiInstance()
{
	UClass* ApiClass = MCPFab::FindBrowserApiClass();
	if (!ApiClass) return false;
	TArray<UObject*> Instances;
	GetObjectsOfClass(ApiClass, Instances, /*bIncludeDerivedClasses=*/true, RF_ClassDefaultObject);
	return Instances.Num() > 0;
}

// ─── Actions ──────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FFabHandlers::Status(const TSharedPtr<FJsonObject>& Params)
{
	auto Res = MCPSuccess();
	const bool bLoaded = MCPFab::IsModuleLoaded();
	Res->SetBoolField(TEXT("pluginLoaded"), bLoaded);
	// Compile-time link state: whether cache/import actions are backed by the
	// native Fab API in this build, vs. console-command-only.
	Res->SetBoolField(TEXT("nativeApiLinked"), WITH_FAB_PLUGIN ? true : false);
	Res->SetBoolField(TEXT("fabWindowOpened"), HasFabBrowserApiInstance());

	if (!bLoaded)
	{
		Res->SetStringField(TEXT("note"), TEXT("Fab plugin module not loaded. It ships enabled by default on UE 5.8; enable it in the editor's Plugins panel if missing."));
		return MCPResult(Res);
	}

#if WITH_FAB_PLUGIN
	Res->SetStringField(TEXT("cacheLocation"), FFabAssetsCache::GetCacheLocation());
	Res->SetNumberField(TEXT("cacheSizeBytes"), (double)FFabAssetsCache::GetCacheSize());
	Res->SetStringField(TEXT("cacheSize"), FFabAssetsCache::GetCacheSizeString().ToString());
#endif
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FFabHandlers::Login(const TSharedPtr<FJsonObject>& Params)
{
	if (!MCPFab::IsModuleLoaded()) return MCPError(TEXT("Fab plugin not loaded"));
	// The sign-in state before the flow opens, read as fab(auth_state) reads it.
	// The token stays in this function; only whether there is one leaves it.
	FString Token;
	FString ReadError;
	const bool bStateReadable = MCPFab::ReadAuthToken(Token, ReadError);
	const bool bWasSignedIn = bStateReadable && !Token.IsEmpty();
	Token.Reset();

	// Fab.Login opens the EOS account-portal login flow. Asynchronous: this
	// returns once the flow is triggered, not once the user has authenticated.
	if (!RunFabConsoleCommand(TEXT("Fab.Login")))
		return MCPError(TEXT("Failed to invoke Fab.Login console command"));
	auto Res = MCPSuccess();
	if (bStateReadable)
	{
		Res->SetBoolField(TEXT("alreadySignedIn"), bWasSignedIn);
	}
	else
	{
		Res->SetStringField(TEXT("signInStateError"), ReadError);
	}
	Res->SetStringField(TEXT("note"), TEXT("Login flow triggered. Complete authentication in the account-portal window if prompted; call fab(auth_state) afterward."));
	// No idempotency flag, and the reason is stated rather than left as an
	// absence: alreadySignedIn is the state before the call, and whether the
	// flow ends in a sign-in is decided in the account portal after it returns.
	// A fabricated unchanged=false would be a claim, not a reading.
	Res->SetBoolField(TEXT("idempotencyObservable"), false);
	Res->SetStringField(TEXT("idempotencyNote"),
		TEXT("alreadySignedIn reports whether an account was signed in before this call. Whether the flow it opens ends in a sign-in is decided in the account portal after it returns, so this call cannot say whether it changed "
			 "anything. fab(auth_state) reads the sign-in afterward."));

	// No inverse. This opens the EOS account-portal flow and returns before
	// anything has authenticated, so it creates no state of its own to undo.
	// fab(logout) is not the inverse: it would clear whatever session the user
	// already had, including one this call had nothing to do with.
	Res->SetBoolField(TEXT("rollbackPossible"), false);
	Res->SetStringField(TEXT("rollbackNote"),
		TEXT("Triggering the login flow creates no state this call owns - the user authenticates, or does not, in the "
			 "account portal. fab(logout) would clear a session that may predate this call, so it is not offered as "
			 "an inverse."));
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FFabHandlers::Logout(const TSharedPtr<FJsonObject>& Params)
{
	if (!MCPFab::IsModuleLoaded()) return MCPError(TEXT("Fab plugin not loaded"));
	if (!RunFabConsoleCommand(TEXT("Fab.Logout")))
		return MCPError(TEXT("Failed to invoke Fab.Logout console command"));
	auto Res = MCPSuccess();
	Res->SetStringField(TEXT("note"), TEXT("Persistent Fab auth cleared."));
	// No idempotency flag, and the reason is stated rather than left as an
	// absence: fab(auth_state) reads this editor's session, but the persistent
	// credentials this clears are stored where nothing here can read them.
	// A fabricated unchanged=false would be a claim, not a reading.
	Res->SetBoolField(TEXT("idempotencyObservable"), false);
	Res->SetStringField(TEXT("idempotencyNote"),
		TEXT("fab(auth_state) reads whether this editor's session is signed in, but the stored credentials this call clears are not readable from here, so it cannot say whether there was anything to clear."));

	// No inverse. Logging back in means the user authenticating in the EOS
	// account portal; fab(login) only opens that window and cannot restore the
	// credentials this call destroyed.
	Res->SetBoolField(TEXT("rollbackPossible"), false);
	Res->SetStringField(TEXT("rollbackNote"),
		TEXT("The stored credentials are gone. fab(login) opens the account-portal flow for a person to complete; no "
			 "call restores a cleared session, so this has no inverse."));
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FFabHandlers::SyncLibrary(const TSharedPtr<FJsonObject>& Params)
{
	if (!MCPFab::IsModuleLoaded()) return MCPError(TEXT("Fab plugin not loaded"));
	// Loads the user's owned Fab library ("My Folder") into TEDS so it surfaces
	// in the Content Browser. Optional batch size controls how many items are
	// pulled per sync request.
	const int32 BatchSize = OptionalInt(Params, TEXT("batchSize"), 0);
	const FString Command = BatchSize > 0
		? FString::Printf(TEXT("Fab.TEDS.MyFolderIntegration %d"), BatchSize)
		: FString(TEXT("Fab.TEDS.MyFolderIntegration"));
	if (!RunFabConsoleCommand(Command))
		return MCPError(TEXT("Failed to invoke Fab.TEDS.MyFolderIntegration console command"));
	auto Res = MCPSuccess();
	Res->SetStringField(TEXT("note"), TEXT("Library sync queued. Owned Fab items load into the Content Browser asynchronously; requires an active Fab login."));
	// No idempotency flag, and the reason is stated rather than left as an
	// absence: the sync's results land in the editor's data storage after this
	// call returns, and no action reads them back. A fabricated unchanged=false
	// would be a claim, not a reading.
	Res->SetBoolField(TEXT("idempotencyObservable"), false);
	Res->SetStringField(TEXT("idempotencyNote"),
		TEXT("The sync runs asynchronously and its results land in the Content Browser after this call has returned, where no action reads them back, so this call cannot say whether it changed "
			 "anything. fab(auth_state) reads whether the session the sync needs is signed in."));

	// No inverse. This loads what the user already owns into the editor's TEDS
	// index so the Content Browser can show it. Nothing un-lists a library, and
	// the listing is rebuilt from the account on the next sync anyway.
	Res->SetBoolField(TEXT("rollbackPossible"), false);
	Res->SetStringField(TEXT("rollbackNote"),
		TEXT("Syncing populates an in-editor index of the library the account already owns. There is no call that "
			 "un-lists it, and nothing on disk or in the project changed."));
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FFabHandlers::ListCached(const TSharedPtr<FJsonObject>& Params)
{
	if (!MCPFab::IsModuleLoaded()) return MCPError(TEXT("Fab plugin not loaded"));
#if WITH_FAB_PLUGIN
	auto Res = MCPSuccess();
	const TArray<FString> Cached = FFabAssetsCache::GetCachedAssets();
	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FString& Entry : Cached)
	{
		Arr.Add(MakeShared<FJsonValueString>(Entry));
	}
	Res->SetArrayField(TEXT("cachedAssets"), Arr);
	Res->SetNumberField(TEXT("count"), Cached.Num());
	Res->SetStringField(TEXT("cacheLocation"), FFabAssetsCache::GetCacheLocation());
	return MCPResult(Res);
#else
	return MCPError(TEXT("Fab native API not linked in this build (Fab plugin absent at build time). Cache listing unavailable."));
#endif
}

TSharedPtr<FJsonValue> FFabHandlers::CacheInfo(const TSharedPtr<FJsonObject>& Params)
{
	if (!MCPFab::IsModuleLoaded()) return MCPError(TEXT("Fab plugin not loaded"));
#if WITH_FAB_PLUGIN
	auto Res = MCPSuccess();
	Res->SetStringField(TEXT("cacheLocation"), FFabAssetsCache::GetCacheLocation());
	Res->SetNumberField(TEXT("cacheSizeBytes"), (double)FFabAssetsCache::GetCacheSize());
	Res->SetStringField(TEXT("cacheSize"), FFabAssetsCache::GetCacheSizeString().ToString());
	Res->SetNumberField(TEXT("count"), FFabAssetsCache::GetCachedAssets().Num());
	return MCPResult(Res);
#else
	return MCPError(TEXT("Fab native API not linked in this build (Fab plugin absent at build time). Cache info unavailable."));
#endif
}

TSharedPtr<FJsonValue> FFabHandlers::ClearCache(const TSharedPtr<FJsonObject>& Params)
{
	if (!MCPFab::IsModuleLoaded()) return MCPError(TEXT("Fab plugin not loaded"));

	// Read the cache size first so the result can say whether this call actually
	// deleted anything. Only available when the native API is linked; without it
	// the console command is still the right thing to run, there is just nothing
	// to measure it against.
	int64 PreviousBytes = -1;
#if WITH_FAB_PLUGIN
	PreviousBytes = (int64)FFabAssetsCache::GetCacheSize();
#endif

	// Routed through the console command so this works without WITH_FAB_PLUGIN.
	if (!RunFabConsoleCommand(TEXT("Fab.ClearCache")))
		return MCPError(TEXT("Failed to invoke Fab.ClearCache console command"));
	auto Res = MCPSuccess();
	Res->SetStringField(TEXT("note"), TEXT("Fab download cache cleared."));
	if (PreviousBytes >= 0)
	{
		Res->SetNumberField(TEXT("previousCacheSizeBytes"), (double)PreviousBytes);
		Res->SetBoolField(TEXT("unchanged"), PreviousBytes == 0);
	}
	// No inverse. The cached downloads are deleted from disk and nothing puts
	// them back: they return only by downloading them from Fab again, which is
	// a fresh transfer rather than a restoration.
	Res->SetBoolField(TEXT("rollbackPossible"), false);
	Res->SetStringField(TEXT("rollbackNote"),
		TEXT("The cached downloads are deleted from disk. No call restores them; they come back only by downloading "
			 "the assets from Fab again. Nothing in the project changed - the cache is a download staging area."));
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FFabHandlers::ImportFile(const TSharedPtr<FJsonObject>& Params)
{
	// Both parameters are read before anything can fail, whether or not the Fab
	// plugin is present; sourceFile and destPath are aliases the registry
	// resolves (#1057).
	FString Source;
	if (auto Err = RequireString(Params, TEXT("source"), Source)) return Err;
	FString Destination;
	if (auto Err = RequireString(Params, TEXT("destination"), Destination)) return Err;

	if (!MCPFab::IsModuleLoaded()) return MCPError(TEXT("Fab plugin not loaded"));
#if WITH_FAB_PLUGIN
	if (!FPaths::FileExists(Source))
		return MCPError(FString::Printf(TEXT("Source file not found on disk: %s"), *Source));
	if (!Destination.StartsWith(TEXT("/")))
		return MCPError(FString::Printf(TEXT("Destination must be a content path like /Game/Fab/Imported (got '%s')"), *Destination));
	if (!FPackageName::IsValidLongPackageName(Destination, /*bIncludeReadOnlyRoots=*/true))
		return MCPError(FString::Printf(TEXT("Destination is not a valid content path: %s"), *Destination));

	// FFabGenericImporter::ImportAsset runs the Fab Interchange pipeline and
	// delivers the created objects via callback. Single source files (fbx,
	// textures) complete on the game thread synchronously, so the callback has
	// fired by the time ImportAsset returns; pack/quixel workflows can defer.
	// We capture whatever the callback produced and report accordingly rather
	// than blocking the game thread.
	TSharedRef<bool> bCompleted = MakeShared<bool>(false);
	TSharedRef<TArray<FString>> ImportedPaths = MakeShared<TArray<FString>>();

	FFabGenericImporter::ImportAsset(
		{ Source },
		Destination,
		[bCompleted, ImportedPaths](const TArray<UObject*>& Objects)
		{
			*bCompleted = true;
			for (const UObject* Obj : Objects)
			{
				if (Obj) ImportedPaths->Add(Obj->GetPathName());
			}
		});

	auto Res = MCPSuccess();
	Res->SetStringField(TEXT("source"), Source);
	Res->SetStringField(TEXT("destination"), Destination);
	if (*bCompleted)
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FString& P : *ImportedPaths)
		{
			Arr.Add(MakeShared<FJsonValueString>(P));
		}
		Res->SetArrayField(TEXT("importedAssets"), Arr);
		Res->SetNumberField(TEXT("count"), ImportedPaths->Num());
		MCPSetCreated(Res);

		if (ImportedPaths->Num() > 0)
		{
			// The inverse deletes exactly the assets this import wrote, which is
			// only expressible because the callback handed back their paths.
			// force is true because the imported set references itself - a
			// material referencing its textures - and a reference check would
			// refuse to delete the half that is still pointed at.
			TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
			Payload->SetArrayField(TEXT("assetPaths"), Arr);
			Payload->SetBoolField(TEXT("force"), true);
			MCPSetRollback(Res, TEXT("delete_asset_batch"), Payload);
			Res->SetBoolField(TEXT("rollbackLossy"), false);
		}
		else
		{
			Res->SetBoolField(TEXT("unchanged"), true);
			Res->SetBoolField(TEXT("rollbackPossible"), false);
			Res->SetStringField(TEXT("rollbackNote"),
				TEXT("The import produced no objects, so nothing was written and there is nothing to delete."));
		}
	}
	else
	{
		// Async workflow (pack extraction, quixel gltf, plugin install): the
		// import is running and will land in Destination shortly.
		Res->SetBoolField(TEXT("async"), true);
		Res->SetStringField(TEXT("note"), FString::Printf(TEXT("Import running asynchronously into %s; poll asset(list) on that path to confirm."), *Destination));
		// No inverse for this branch. The importer has not reported what it
		// created yet, and a record naming the destination FOLDER would delete
		// whatever else already lives there. Read the path afterwards and delete
		// deliberately instead.
		Res->SetBoolField(TEXT("rollbackPossible"), false);
		Res->SetStringField(TEXT("rollbackNote"), FString::Printf(
			TEXT("The import is still running, so the paths it writes are not known yet and no inverse can name them. "
				 "A record targeting '%s' would delete assets that were already there. List the destination after the "
				 "import lands and delete what it added with asset(delete_batch)."),
			*Destination));
	}
	return MCPResult(Res);
#else
	return MCPError(TEXT("Fab native API not linked in this build (Fab plugin absent at build time). Import unavailable."));
#endif
}

TSharedPtr<FJsonValue> FFabHandlers::AuthState(const TSharedPtr<FJsonObject>& Params)
{
	if (!MCPFab::IsModuleLoaded()) return MCPError(TEXT("Fab plugin not loaded"));
	// The plugin's own reading: UFabBrowserApi::GetAuthToken copies the EOS
	// session token, or returns empty when no account is signed in. The token
	// stays in this function; only whether there is one, and the account it
	// names, leave it.
	FString Token;
	FString Error;
	if (!MCPFab::ReadAuthToken(Token, Error)) return MCPError(Error);

	auto Res = MCPSuccess();
	const bool bSignedIn = !Token.IsEmpty();
	Res->SetBoolField(TEXT("signedIn"), bSignedIn);
	if (bSignedIn)
	{
		const FString AccountId = MCPFab::AccountIdFromToken(Token);
		if (!AccountId.IsEmpty())
		{
			Res->SetStringField(TEXT("accountId"), AccountId);
		}
	}
	else
	{
		Res->SetStringField(TEXT("note"),
			TEXT("No Fab session in this editor. The plugin restores a remembered sign-in only when a Fab tab opens; "
				 "fab(login) starts the account-portal sign-in, which a person completes."));
	}
	Token.Reset();
	return MCPResult(Res);
}

// The workflow UFabBrowserApi::AddToProject hands an asset to, tested in the
// order that function tests them. Empty when none takes it, where the plugin
// would only log "Asset type not handled". FString's operator== ignores case,
// as the plugin's own comparisons do.
static FString ChooseFabWorkflow(const FString& AssetType, bool bIsQuixel)
{
	if (AssetType == TEXT("unreal-engine")) return TEXT("pack");
	if (bIsQuixel) return TEXT("quixel");
	if (AssetType == TEXT("gltf") || AssetType == TEXT("glb") || AssetType == TEXT("fbx")) return TEXT("generic");
	if (AssetType == TEXT("metahuman")) return TEXT("metahuman");
#if MCP_FAB_HAS_WORKFLOW_FACTORIES
	if (FFabWorkflowFactoryRegistry::GetFactory(AssetType).IsValid()) return TEXT("factory");
#endif
	return FString();
}

// Where each workflow puts what it imports, for the caller to look afterwards.
static FString DescribeFabWorkflowDestination(const FString& Workflow)
{
	if (Workflow == TEXT("pack")) return TEXT("The pack installs into the project directory and its content folder is scanned when the install ends.");
	if (Workflow == TEXT("quixel")) return TEXT("Megascans content imports under /Game/Fab/Megascans.");
	if (Workflow == TEXT("generic")) return TEXT("The model imports under /Game/Fab.");
	return TEXT("The workflow picks its own destination.");
}

struct FFabMetadataValues
{
	FString AssetId;
	FString AssetName;
	FString AssetType;
	FString ListingType;
	FString AssetNamespace;
	TArray<FString> DistributionPointBaseUrls;
	bool bIsQuixel = false;
};

// Fill the plugin's FFabAssetMetadata, reached through the AddToProject
// parameter's reflected struct so no Private header is linked. Fails naming the
// field this engine's struct lacks.
static bool WriteFabAssetMetadata(const UScriptStruct* Struct, void* Data, const FFabMetadataValues& Values, FString& OutError)
{
	const TPair<const TCHAR*, const FString*> StringFields[] = {
		{ TEXT("AssetId"), &Values.AssetId },
		{ TEXT("AssetName"), &Values.AssetName },
		{ TEXT("AssetType"), &Values.AssetType },
		{ TEXT("ListingType"), &Values.ListingType },
		{ TEXT("AssetNamespace"), &Values.AssetNamespace },
	};
	for (const TPair<const TCHAR*, const FString*>& Field : StringFields)
	{
		FStrProperty* Prop = FindFProperty<FStrProperty>(Struct, Field.Key);
		if (!Prop)
		{
			OutError = FString::Printf(TEXT("This engine's FFabAssetMetadata has no string field %s, so AddToProject cannot be called here."), Field.Key);
			return false;
		}
		Prop->SetPropertyValue_InContainer(Data, *Field.Value);
	}

	FBoolProperty* QuixelProp = FindFProperty<FBoolProperty>(Struct, TEXT("IsQuixel"));
	FArrayProperty* UrlsProp = FindFProperty<FArrayProperty>(Struct, TEXT("DistributionPointBaseUrls"));
	FStrProperty* UrlProp = UrlsProp ? CastField<FStrProperty>(UrlsProp->Inner) : nullptr;
	if (!QuixelProp || !UrlProp)
	{
		OutError = TEXT("This engine's FFabAssetMetadata lacks IsQuixel or a string-array DistributionPointBaseUrls, so AddToProject cannot be called here.");
		return false;
	}
	QuixelProp->SetPropertyValue_InContainer(Data, Values.bIsQuixel);
	FScriptArrayHelper Urls(UrlsProp, UrlsProp->ContainerPtrToValuePtr<void>(Data));
	Urls.Resize(Values.DistributionPointBaseUrls.Num());
	for (int32 Index = 0; Index < Values.DistributionPointBaseUrls.Num(); ++Index)
	{
		UrlProp->SetPropertyValue(Urls.GetRawPtr(Index), Values.DistributionPointBaseUrls[Index]);
	}
	return true;
}

// Call UFabBrowserApi::AddToProject(DownloadUrl, AssetMetadata) by reflection.
static bool CallFabAddToProject(const FString& DownloadUrl, const FFabMetadataValues& Values, FString& OutError)
{
	UObject* Api = MCPFab::GetBrowserApi(OutError);
	if (!Api) return false;
	UFunction* Func = MCPFab::FindBrowserApiFunction(Api, TEXT("AddToProject"), OutError);
	if (!Func) return false;
	FStrProperty* UrlProp = FindFProperty<FStrProperty>(Func, TEXT("DownloadUrl"));
	FStructProperty* MetadataProp = FindFProperty<FStructProperty>(Func, TEXT("AssetMetadata"));
	if (!UrlProp || !MetadataProp)
	{
		OutError = TEXT("This engine's UFabBrowserApi::AddToProject does not take (DownloadUrl, AssetMetadata), so it cannot be called here.");
		return false;
	}

	FStructOnScope Frame(Func);
	uint8* Memory = Frame.GetStructMemory();
	UrlProp->SetPropertyValue_InContainer(Memory, DownloadUrl);
	if (!WriteFabAssetMetadata(MetadataProp->Struct, MetadataProp->ContainerPtrToValuePtr<void>(Memory), Values, OutError)) return false;
	Api->ProcessEvent(Func, Memory);
	return true;
}

// Read every add_to_project parameter before anything can fail (#1057).
static TSharedPtr<FJsonValue> ReadFabAddToProjectParams(const TSharedPtr<FJsonObject>& Params, FString& DownloadUrl, FFabMetadataValues& Values)
{
	if (auto Err = RequireString(Params, TEXT("downloadUrl"), DownloadUrl)) return Err;
	if (auto Err = RequireString(Params, TEXT("assetId"), Values.AssetId)) return Err;
	if (auto Err = RequireString(Params, TEXT("assetName"), Values.AssetName)) return Err;
	if (auto Err = RequireString(Params, TEXT("assetType"), Values.AssetType)) return Err;
	Values.ListingType = OptionalString(Params, TEXT("listingType"));
	Values.AssetNamespace = OptionalString(Params, TEXT("assetNamespace"));
	Values.bIsQuixel = OptionalBool(Params, TEXT("isQuixel"), false);
	const TArray<TSharedPtr<FJsonValue>>* BaseUrlValues = nullptr;
	if (TryGetArrayParam(Params, TEXT("distributionPointBaseUrls"), BaseUrlValues) && BaseUrlValues)
	{
		for (const TSharedPtr<FJsonValue>& Value : *BaseUrlValues)
		{
			FString BaseUrl;
			if (!Value.IsValid() || !Value->TryGetString(BaseUrl) || BaseUrl.IsEmpty())
				return MCPError(TEXT("distributionPointBaseUrls must be a list of non-empty strings"));
			Values.DistributionPointBaseUrls.Add(BaseUrl);
		}
	}
	return nullptr;
}

// The workflow AddToProject will start for these values, or an error naming
// why none would.
static TSharedPtr<FJsonValue> ResolveFabWorkflow(const FFabMetadataValues& Values, FString& OutWorkflow)
{
	OutWorkflow = ChooseFabWorkflow(Values.AssetType, Values.bIsQuixel);
	if (OutWorkflow.IsEmpty())
		return MCPError(FString::Printf(
			TEXT("No Fab workflow takes assetType '%s'. The plugin handles unreal-engine (a pack), gltf, glb, fbx and metahuman, "
				 "isQuixel=true for Megascans, and any type a plugin registered a workflow factory for."),
			*Values.AssetType));
	if (OutWorkflow == TEXT("pack") && Values.DistributionPointBaseUrls.IsEmpty())
		return MCPError(TEXT("An unreal-engine pack downloads its chunks from distributionPointBaseUrls; pass the base URLs the listing's download info names."));
	return nullptr;
}

TSharedPtr<FJsonValue> FFabHandlers::AddToProject(const TSharedPtr<FJsonObject>& Params)
{
	FString DownloadUrl;
	FFabMetadataValues Values;
	if (auto Err = ReadFabAddToProjectParams(Params, DownloadUrl, Values)) return Err;
	if (!MCPFab::IsModuleLoaded()) return MCPError(TEXT("Fab plugin not loaded"));

	FString Workflow;
	if (auto Err = ResolveFabWorkflow(Values, Workflow)) return Err;
	if (!MCPFab::AcceptsDownloadUrl(DownloadUrl, Workflow))
		return MCPError(FString::Printf(
			TEXT("downloadUrl must be an http(s) or file URL, or a \\\\share path for an unreal-engine pack (got '%s' for the %s workflow)"),
			*DownloadUrl, *Workflow));

	FString Error;
	if (!CallFabAddToProject(DownloadUrl, Values, Error)) return MCPError(Error);

	auto Res = MCPSuccess();
	Res->SetStringField(TEXT("assetId"), Values.AssetId);
	Res->SetStringField(TEXT("workflow"), Workflow);
	// AddToProject only starts the workflow: the download and import run on
	// after this returns and report through editor notifications, so no asset
	// path is known here.
	Res->SetBoolField(TEXT("async"), true);
	Res->SetStringField(TEXT("note"), FString::Printf(
		TEXT("The %s workflow is downloading and importing asynchronously. %s A bad or expired URL fails in the workflow, "
			 "shown in the editor's notifications and the LogFab log, not in this result."),
		*Workflow, *DescribeFabWorkflowDestination(Workflow)));
	MCPSetIdempotencyUnobservable(Res,
		TEXT("Whether this asset was already in the project, or already being added, is decided inside the plugin's workflow "
			 "after this call returns, and nothing reports it back here."));
	// No inverse, as for import_file's asynchronous branch: the paths the
	// workflow writes are not known yet, and a record naming the destination
	// folder would delete whatever else lives there.
	MCPSetNoRollback(Res,
		TEXT("The workflow is still running, so the paths it writes are not known yet and no inverse can name them. "
			 "A record targeting the destination folder would delete assets that were already there. List the destination "
			 "after the import lands and delete what it added with asset(delete_batch)."));
	return MCPResult(Res);
}

// ─── Registration ──────────────────────────────────────────────────────────

void FFabHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	// Reports parameters its handlers never read (#1057).
	FMCPHandlerRegistry::FCategoryScope CategoryScope(Registry, TEXT("fab"));
	// #1057: every handler declares its parameters here. The ones that act on the
	// account or the cache run as soon as they are called, whatever the values,
	// so they are contract-exempt.
	using EType = EMCPParamType;
	Registry.RegisterHandler(TEXT("fab_status"), &Status, {});
	Registry.RegisterHandler(TEXT("fab_login"), &Login, {},
		MCPSpec::ContractExempt(TEXT("Takes no parameters and opens the EOS account-portal login flow when called")));
	Registry.RegisterHandler(TEXT("fab_logout"), &Logout, {},
		MCPSpec::ContractExempt(TEXT("Takes no parameters and clears the stored Fab credentials when called")));
	Registry.RegisterHandler(TEXT("fab_sync_library"), &SyncLibrary, {
		MCPParam::Optional(TEXT("batchSize"), EType::Integer, TEXT("Library items to pull per sync request (default the plugin's own)")),
	}, MCPSpec::ContractExempt(TEXT("Queues a Fab library sync whatever the batch size")));
	Registry.RegisterHandler(TEXT("fab_list_cached"), &ListCached, {});
	Registry.RegisterHandler(TEXT("fab_cache_info"), &CacheInfo, {});
	Registry.RegisterHandler(TEXT("fab_clear_cache"), &ClearCache, {},
		MCPSpec::ContractExempt(TEXT("Takes no parameters and deletes the Fab download cache when called")));
	Registry.RegisterHandler(TEXT("fab_import_file"), &ImportFile, {
		MCPParam::Required(TEXT("source"), EType::String, TEXT("Absolute path of the source file on disk")).Alias(TEXT("sourceFile")),
		MCPParam::Required(TEXT("destination"), EType::String, TEXT("Destination content path, e.g. /Game/Fab/Imported")).Alias(TEXT("destPath")),
	});
	Registry.RegisterHandler(TEXT("fab_auth_state"), &AuthState, {},
		MCPSpec::ContractExempt(TEXT("Takes no parameters, and the plugin's token read it makes logs an EOS error whenever no account is signed in")));
	Registry.RegisterHandler(TEXT("fab_add_to_project"), &AddToProject, {
		MCPParam::Required(TEXT("downloadUrl"), EType::String, TEXT("Signed download URL for the listing (a pack's manifest URL), as fab.com issues it")),
		MCPParam::Required(TEXT("assetId"), EType::String, TEXT("Fab asset id of the listing")),
		MCPParam::Required(TEXT("assetName"), EType::String, TEXT("Listing name, used for the import folder and notifications")),
		MCPParam::Required(TEXT("assetType"), EType::String, TEXT("Fab asset type: unreal-engine (a pack), gltf, glb, fbx, metahuman, or a type a plugin registered")),
		MCPParam::Optional(TEXT("isQuixel"), EType::Boolean, TEXT("Megascans content; picks the Quixel workflow for any type but unreal-engine (default false)")),
		MCPParam::Optional(TEXT("distributionPointBaseUrls"), EType::Array, TEXT("Chunk base URLs a pack downloads from; required for unreal-engine")).Items(EType::String),
		MCPParam::Optional(TEXT("listingType"), EType::String, TEXT("Fab listing type, passed through to the workflow")),
		MCPParam::Optional(TEXT("assetNamespace"), EType::String, TEXT("Fab asset namespace, passed through to the workflow")),
	});
}
