#include "FabHandlers.h"
#include "FabBrowserApiReflection.h"
#include "HandlerUtils.h"

#include "HAL/Event.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

// fab(library): one page of the signed-in account's Fab library, read from the
// same endpoint the Fab plugin's own "My Folder" sync calls. The plugin's sync
// (FFabTedsMyFolderIntegration) is a plain C++ class with no reflected entry
// point, and it writes its results only into the editor data storage, so this
// action makes the request itself with the session token the plugin holds.

namespace MCPFabLibrary
{
	// Both stay under the game-thread executor's 30 s budget for a handler.
	constexpr float RequestTimeoutSeconds = 20.0f;
	constexpr uint32 WaitTimeoutMs = 25000;

	struct FResponse
	{
		FEventRef Done;
		bool bStarted = true;
		bool bConnected = false;
		int32 Status = 0;
		FString ContentType;
		FString Body;
	};

	// The Fab API host for the plugin's Prod environment. The plugin itself
	// builds its library URL on https://fab.com (UFabSettings::GetUrlFromEnvironment),
	// but fab.com's CDN answers that path 404 with an empty body; the API is
	// served on www.fab.com. The other environments are Epic-internal, and a
	// custom URL is unset by default.
	inline bool ReadFabHost(FString& OutHost, FString& OutError)
	{
		UClass* SettingsClass = FindObject<UClass>(nullptr, TEXT("/Script/Fab.FabSettings"));
		const UObject* Settings = SettingsClass ? SettingsClass->GetDefaultObject() : nullptr;
		FEnumProperty* EnvProp = SettingsClass ? FindFProperty<FEnumProperty>(SettingsClass, TEXT("Environment")) : nullptr;
		if (!Settings || !EnvProp)
		{
			OutError = TEXT("The Fab plugin's settings (UFabSettings::Environment) are not readable on this engine, so the library host is unknown.");
			return false;
		}
		const int64 Value = EnvProp->GetUnderlyingProperty()->GetSignedIntPropertyValue(EnvProp->ContainerPtrToValuePtr<void>(Settings));
		const FString Environment = EnvProp->GetEnum()->GetNameStringByValue(Value);
		if (Environment != TEXT("Prod"))
		{
			OutError = FString::Printf(TEXT("fab(library) reads the Prod Fab library only; the Fab plugin is set to the %s environment."), *Environment);
			return false;
		}
		OutHost = TEXT("https://www.fab.com");
		return true;
	}

	// The plugin's own request: GET {host}/e/accounts/{account}/ue/library?count=N[&cursor="..."],
	// with the cursor quoted and the session token as a Bearer header. Blocks
	// the calling thread until the response or the timeout; completion runs on
	// the HTTP thread, so it does not need the game thread to tick.
	inline TSharedRef<FResponse> Fetch(const FString& Url, const FString& Token)
	{
		TSharedRef<FResponse> Response = MakeShared<FResponse>();
		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
		Request->SetVerb(TEXT("GET"));
		Request->SetURL(Url);
		Request->SetHeader(TEXT("accept"), TEXT("application/json"));
		Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *Token));
		Request->SetTimeout(RequestTimeoutSeconds);
		Request->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread);
		Request->OnProcessRequestComplete().BindLambda(
			[Response](FHttpRequestPtr, FHttpResponsePtr HttpResponse, bool bConnected)
			{
				Response->bConnected = bConnected && HttpResponse.IsValid();
				if (Response->bConnected)
				{
					Response->Status = HttpResponse->GetResponseCode();
					Response->ContentType = HttpResponse->GetContentType();
					Response->Body = HttpResponse->GetContentAsString();
				}
				Response->Done->Trigger();
			});
		if (!Request->ProcessRequest())
		{
			// A refused start may still complete on the HTTP thread, so the
			// caller gets a fresh answer rather than the one it could write.
			TSharedRef<FResponse> Refused = MakeShared<FResponse>();
			Refused->bStarted = false;
			return Refused;
		}
		if (!Response->Done->Wait(WaitTimeoutMs))
		{
			// The completion may still land on the HTTP thread, so the caller
			// gets a fresh, unconnected answer rather than one being written.
			Request->CancelRequest();
			return MakeShared<FResponse>();
		}
		return Response;
	}

	// One library row: the fields the plugin's sync reads, plus projectVersions,
	// which a pack's artifact and engine versions come in.
	inline TSharedPtr<FJsonObject> DescribeListing(const FJsonObject& Row)
	{
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		static const TCHAR* const StringFields[] = {
			TEXT("assetId"), TEXT("title"), TEXT("assetNamespace"), TEXT("listingType"), TEXT("distributionMethod"),
			TEXT("source"), TEXT("seller"), TEXT("url"),
		};
		for (const TCHAR* Field : StringFields)
		{
			FString Value;
			if (Row.TryGetStringField(Field, Value))
			{
				Out->SetStringField(Field, Value);
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* Versions = nullptr;
		if (Row.TryGetArrayField(TEXT("projectVersions"), Versions) && Versions)
		{
			Out->SetArrayField(TEXT("projectVersions"), *Versions);
		}
		return Out;
	}

	inline TSharedPtr<FJsonValue> DescribePage(const FResponse& Response)
	{
		TSharedPtr<FJsonObject> Page;
		const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(Response.Body);
		if (!Response.ContentType.StartsWith(TEXT("application/json")) || !FJsonSerializer::Deserialize(Reader, Page) || !Page.IsValid())
			return MCPError(FString::Printf(TEXT("fab.com answered the library request with something other than JSON (%s)."), *Response.ContentType));

		const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
		if (!Page->TryGetArrayField(TEXT("results"), Rows) || !Rows)
			return MCPError(TEXT("fab.com's library answer has no results list."));

		TArray<TSharedPtr<FJsonValue>> Listings;
		for (const TSharedPtr<FJsonValue>& Row : *Rows)
		{
			const TSharedPtr<FJsonObject>* RowObject = nullptr;
			if (Row.IsValid() && Row->TryGetObject(RowObject) && RowObject && RowObject->IsValid())
			{
				Listings.Add(MakeShared<FJsonValueObject>(DescribeListing(**RowObject)));
			}
		}
		auto Res = MCPSuccess();
		Res->SetArrayField(TEXT("listings"), Listings);
		Res->SetNumberField(TEXT("count"), Listings.Num());
		const TSharedPtr<FJsonObject>* Cursors = nullptr;
		FString Next;
		if (Page->TryGetObjectField(TEXT("cursors"), Cursors) && Cursors && (*Cursors)->TryGetStringField(TEXT("next"), Next))
		{
			Res->SetStringField(TEXT("nextCursor"), Next);
		}
		return MCPResult(Res);
	}

	inline TSharedPtr<FJsonValue> DescribeFailure(const FResponse& Response)
	{
		if (!Response.bStarted)
			return MCPError(TEXT("The editor's HTTP module refused to start the library request to fab.com."));
		if (!Response.bConnected)
			return MCPError(FString::Printf(TEXT("The library request to fab.com got no answer or the connection failed within %.0f s."), RequestTimeoutSeconds));
		if (Response.Status == 401 || Response.Status == 403)
			return MCPError(FString::Printf(TEXT("fab.com refused the editor's Fab session (HTTP %d). Sign in again with fab(login)."), Response.Status));
		return MCPError(FString::Printf(TEXT("fab.com answered the library request with HTTP %d: %s"), Response.Status, *Response.Body.Left(300)));
	}
}

TSharedPtr<FJsonValue> FFabHandlers::Library(const TSharedPtr<FJsonObject>& Params)
{
	const int32 Count = OptionalInt(Params, TEXT("count"), 100);
	const FString Cursor = OptionalString(Params, TEXT("cursor"));
	if (Count <= 0) return MCPError(FString::Printf(TEXT("count must be a positive number of listings (got %d)"), Count));
	if (!MCPFab::IsModuleLoaded()) return MCPError(TEXT("Fab plugin not loaded"));

	FString Token;
	FString Error;
	if (!MCPFab::ReadAuthToken(Token, Error)) return MCPError(Error);
	if (Token.IsEmpty())
		return MCPError(TEXT("Not signed in to Fab, so no library request was sent. fab(auth_state) reads the sign-in; fab(login) starts one."));
	const FString AccountId = MCPFab::AccountIdFromToken(Token);
	if (AccountId.IsEmpty())
	{
		Token.Reset();
		return MCPError(TEXT("The Fab session token names no account id this bridge can read, so the library URL cannot be built."));
	}
	FString Host;
	if (!MCPFabLibrary::ReadFabHost(Host, Error))
	{
		Token.Reset();
		return MCPError(Error);
	}

	FString Url = FString::Printf(TEXT("%s/e/accounts/%s/ue/library?count=%d"), *Host, *AccountId, Count);
	if (!Cursor.IsEmpty())
	{
		Url += FString::Printf(TEXT("&cursor=\"%s\""), *Cursor);
	}
	const TSharedRef<MCPFabLibrary::FResponse> Response = MCPFabLibrary::Fetch(Url, Token);
	Token.Reset();
	if (!Response->bConnected || Response->Status != 200) return MCPFabLibrary::DescribeFailure(*Response);
	return MCPFabLibrary::DescribePage(*Response);
}
