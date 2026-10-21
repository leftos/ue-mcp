#pragma once

// Reflection into the engine Fab plugin's UFabBrowserApi, the object its web
// window calls native code through. The class lives in the plugin's Private
// headers, so it is reached by name and its UFUNCTIONs are invoked with
// ProcessEvent rather than linked. Shared by the fab handler files: the module
// is a unity build, so a helper two of them need lives here, not file-local.

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Misc/Base64.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Class.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"

namespace MCPFab
{
	/**
	 * The Fab plugin is an engine editor plugin, enabled by default on 5.8 but not
	 * guaranteed to be present or loaded (disabled, or an older engine). Every fab
	 * action checks this first so callers get a clear message instead of a crash
	 * or an "Unknown method".
	 */
	inline bool IsModuleLoaded()
	{
		return FModuleManager::Get().IsModuleLoaded(TEXT("Fab"));
	}

	inline UClass* FindBrowserApiClass()
	{
		return FindObject<UClass>(nullptr, TEXT("/Script/Fab.FabBrowserApi"));
	}

	/**
	 * The UFabBrowserApi class default object, the receiver for its UFUNCTIONs.
	 * GetAuthToken and AddToProject read no instance state, and calling them on
	 * the CDO leaves no live instance behind, which fab(status) reads as "the Fab
	 * window was opened this session".
	 */
	inline UObject* GetBrowserApi(FString& OutError)
	{
		UClass* ApiClass = FindBrowserApiClass();
		if (!ApiClass)
		{
			OutError = TEXT("The Fab plugin's /Script/Fab.FabBrowserApi class is not loaded, so its native API cannot be reached on this engine.");
			return nullptr;
		}
		return ApiClass->GetDefaultObject();
	}

	inline UFunction* FindBrowserApiFunction(UObject* Api, const TCHAR* Name, FString& OutError)
	{
		UFunction* Func = Api->GetClass()->FindFunctionByName(Name);
		if (!Func)
		{
			OutError = FString::Printf(TEXT("This engine's Fab plugin has no UFabBrowserApi::%s, so the action cannot run here."), Name);
		}
		return Func;
	}

	/**
	 * Whether UFabBrowserApi::AddToProject can use Url for the workflow it will
	 * start: a web or file URL for any workflow, and a \\share path for a pack
	 * only, the one workflow whose URL the plugin rewrites to file://.
	 */
	inline bool AcceptsDownloadUrl(const FString& Url, const FString& Workflow)
	{
		if (Url.StartsWith(TEXT("\\\\"))) return Workflow == TEXT("pack");
		return Url.StartsWith(TEXT("https://")) || Url.StartsWith(TEXT("http://")) || Url.StartsWith(TEXT("file://"));
	}

	/**
	 * The session token the Fab plugin holds, through UFabBrowserApi::GetAuthToken.
	 * Empty when no account is signed in. The caller must never log or return it.
	 */
	inline bool ReadAuthToken(FString& OutToken, FString& OutError)
	{
		UObject* Api = GetBrowserApi(OutError);
		if (!Api) return false;
		UFunction* Func = FindBrowserApiFunction(Api, TEXT("GetAuthToken"), OutError);
		if (!Func) return false;
		FStrProperty* ReturnProp = CastField<FStrProperty>(Func->GetReturnProperty());
		if (!ReturnProp)
		{
			OutError = TEXT("UFabBrowserApi::GetAuthToken does not return a string on this engine, so the sign-in state cannot be read.");
			return false;
		}
		FStructOnScope Frame(Func);
		Api->ProcessEvent(Func, Frame.GetStructMemory());
		OutToken = ReturnProp->GetPropertyValue_InContainer(Frame.GetStructMemory());
		return true;
	}

	/**
	 * The Epic account id an EOS session token names, from the "sub" claim of the
	 * JWT it carries (an "eg1~" style prefix is skipped). Decoded locally; nothing
	 * is sent anywhere. Empty when the token is not a JWT or carries no subject.
	 */
	inline FString AccountIdFromToken(const FString& Token)
	{
		FString Jwt = Token;
		int32 Tilde = INDEX_NONE;
		if (Jwt.FindChar(TEXT('~'), Tilde))
		{
			Jwt.RightChopInline(Tilde + 1);
		}
		TArray<FString> Parts;
		Jwt.ParseIntoArray(Parts, TEXT("."), /*InCullEmpty=*/false);
		if (Parts.Num() != 3) return FString();

		FString Payload = Parts[1];
		while (Payload.Len() % 4 != 0)
		{
			Payload.AppendChar(TEXT('='));
		}
		FString ClaimsText;
		if (!FBase64::Decode(Payload, ClaimsText, EBase64Mode::UrlSafe)) return FString();

		TSharedPtr<FJsonObject> Claims;
		const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(ClaimsText);
		if (!FJsonSerializer::Deserialize(Reader, Claims) || !Claims.IsValid()) return FString();
		FString Subject;
		Claims->TryGetStringField(TEXT("sub"), Subject);
		return Subject;
	}
}
