﻿#include "WebApiPrivatePCH.h"
#include "WebApiResponseBodyString.h"
#include "WebApi.h"

UWebApi::UWebApi(const class FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, ResultResponseBody(nullptr)
	, bProcessing(false)
	, SentProgress(0)
	, ReceivedProgress(0)
{
	RequestBodyOrg = (UWebApiRequestBodyUrlParameter*)NewObject<UObject>(GetTransientPackage(), UWebApiRequestBodyUrlParameter::StaticClass());
	SuccessResponseCodes.Add(200);
}

UWebApi* UWebApi::CreateWebApi(UClass* Class)
{
	return (UWebApi*)NewObject<UObject>(GetTransientPackage(), Class);
}

void UWebApi::SetRequestParameter(const FString& Key, const FString& Value)
{
	RequestBodyOrg->SetParameter(Key, Value);
}

const FString& UWebApi::GetRequestParameter(const FString& Key) const
{
	return RequestBodyOrg->GetParameter(Key);
}

void UWebApi::AddPreFilter(const TScriptInterface<IWebApiPreFilterInterface>& PreFilter)
{
	auto PreFilterInterface = (IWebApiPreFilterInterface*)PreFilter.GetInterface();
	PreFilters.Enqueue(PreFilterInterface);
}

void UWebApi::AddPostFilter(const TScriptInterface<IWebApiPostFilterInterface>& PostFilter)
{
	auto PostFilterInterface = (IWebApiPostFilterInterface*)PostFilter.GetInterface();
	PostFilters.Enqueue(PostFilterInterface);
}

void UWebApi::OnStartProcessRequest_Implementation()
{
}

bool UWebApi::ProcessRequest()
{
	if(bProcessing)
	{
		UE_LOG(LogTemp, Warning, TEXT("Request is processing already."));
		return false;
	}

	OnStartProcessRequest();

	UWebApiRequestBodyBase* RequestBody = RequestBodyOrg;
	while (PreFilters.IsEmpty() == false)
	{
		IWebApiPreFilterInterface* PreFilter = nullptr;
		if(PreFilters.Dequeue(PreFilter))
		{
			RequestBody = PreFilter->Execute_ExecuteWebApiPreFilter(this, RequestBody);
			if( RequestBody == nullptr )
			{
				UE_LOG(LogTemp, Error, TEXT("RequestBody is null."));
			}
		}
	}

	auto& Module = FHttpModule::Get();
	auto& Manager = Module.GetHttpManager();

	FHttpRequestPtr ProcessingRequest = Module.CreateRequest();

	auto RequestType = RequestBody->GetRequestType();
	ProcessingRequest->SetVerb(EWebApiRequestType::ToString(RequestType));

	if(RequestType == EWebApiRequestType::GET)
	{
		auto UrlParameter = (UWebApiRequestBodyUrlParameter*)RequestBody;
		if(UrlParameter->GetParameterCount() > 0)
		{
			FString ContentString;
			if(RequestBody->GetRequestBodyAsString(ContentString))
			{
				ProcessingRequest->SetURL(Url + TEXT("?") + ContentString);
			}
			else
			{
				ProcessingRequest->SetURL(Url);
			}
		}
		else
		{
			ProcessingRequest->SetURL(Url);
		}
	}
	else
	{
		ProcessingRequest->SetURL(Url);

		switch (RequestBody->GetRequestParameterType())
		{
		case EWebApiRequestParameterType::STRING:
		{
			FString ContentString;
			if(RequestBody->GetRequestBodyAsString(ContentString))
			{
				ProcessingRequest->SetContentAsString(ContentString);
			}
			break;
		}
		case EWebApiRequestParameterType::BYTES:
		{
			TArray<uint8> ContentBytes;
			if(RequestBody->GetRequestBodyAsBytes(ContentBytes))
			{
				ProcessingRequest->SetContent(ContentBytes);
			}
			break;
		}
		}
	}

	for (const auto& Entry : RequestBody->GetHeaders())
	{
		ProcessingRequest->SetHeader(Entry.Key, Entry.Value);
	}

	ProcessingRequest->OnProcessRequestComplete().BindUObject(this, &UWebApi::OnRequestCompletedInternal);
	ProcessingRequest->OnRequestProgress().BindUObject(this, &UWebApi::OnRequestProgressInternal);

	ProcessingRequest->ProcessRequest();

	Manager.AddRequest(ProcessingRequest.ToSharedRef());
	if(Manager.IsValidRequest(ProcessingRequest.Get()) == false)
	{
		UE_LOG(LogTemp, Warning, TEXT("Add request failed."));
		ProcessingRequest->CancelRequest();
		return false;
	}

	bProcessing = true;

	OnRequestStart.Broadcast();

	return true;
}

void UWebApi::CancelRequest()
{
	if(bProcessing == false)
	{
		return;
	}

	ProcessingRequest->CancelRequest();
	bProcessing = false;
}

bool UWebApi::IsProcessingRequest() const
{
	return bProcessing;
}

int32 UWebApi::GetSentProgress() const
{
	if(bProcessing == false)
	{
		return 0;
	}

	return SentProgress;
}

int32 UWebApi::GetReceivedProgress() const
{
	if(bProcessing == false)
	{
		return 0;
	}

	return ReceivedProgress;
}

void UWebApi::GetResponseBody(UWebApiResponseBodyBase*& ResponseBody) const
{
	ResponseBody = ResultResponseBody;
}

void UWebApi::OnRequestCompletedInternal(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccessed)
{
	UWebApiResponseBodyString* ResponseBodyString = (UWebApiResponseBodyString*)NewObject<UObject>(GetTransientPackage(), UWebApiResponseBodyString::StaticClass());
	UWebApiResponseBodyBase* ResponseBody = (UWebApiResponseBodyBase*)ResponseBodyString;

	ResponseBodyString->Code = Response->GetResponseCode();
	ResponseBodyString->SetResponse(bSuccessed ? Response->GetContentAsString() : TEXT("Request failed."));

	while (PostFilters.IsEmpty() == false)
	{
		IWebApiPostFilterInterface* PostFilter = nullptr;
		if(PostFilters.Dequeue(PostFilter))
		{
			ResponseBody = PostFilter->Execute_ExecuteWebApiPostFilter(this, ResponseBody);
			if( ResponseBody == nullptr )
			{
				UE_LOG(LogTemp, Error, TEXT("ResponseBody is null."));
			}
		}
	}

	ResultResponseBody = ResponseBody;

	if(bSuccessed && SuccessResponseCodes.Find(ResultResponseBody->Code) >= 0)
	{
		OnRequestSuccessed.Broadcast();
	}
	else
	{
		OnRequestFailed.Broadcast();
	}

	OnRequestCompleted.Broadcast();

	bProcessing = false;
}

void UWebApi::OnRequestProgressInternal(FHttpRequestPtr Request, int32 Sent, int32 Received)
{
	SentProgress = Sent;
	ReceivedProgress = Received;

	OnRequestProgress.Broadcast();
}