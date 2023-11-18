#include "VirtualizationPlusS3Backend.h"

#include <Async/TaskGraphInterfaces.h>
#include <Interfaces/IHttpResponse.h>
#include <Serialization/BufferArchive.h>
#include <Serialization/MemoryReader.h>
#include <String/Join.h>
#include <String/ParseTokens.h>

#include "VirtualizationPlusUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogVirtualizationPlusS3, Log, All);

FVirtualizationPlusS3Backend::FVirtualizationPlusS3Backend(FStringView ProjectName, FStringView ConfigName, FStringView DebugName)
	: IVirtualizationBackend(ConfigName, DebugName, EOperations::Push | EOperations::Pull)
{
}

bool FVirtualizationPlusS3Backend::Initialize(const FString& ConfigEntry)
{
	if (!FParse::Value(*ConfigEntry, TEXT("BucketUrl="), BucketUrl))
	{
		UE_LOG(LogVirtualization, Error, TEXT("[%s] Config file entry 'BucketUrl=' not specified"), *GetDebugName());
		return false;
	}

	if (!FParse::Value(*ConfigEntry, TEXT("Region="), Region))
	{
		UE_LOG(LogVirtualization, Error, TEXT("[%s] Config file entry 'Region=' not specified"), *GetDebugName());
		return false;
	}

	if (!FParse::Value(*ConfigEntry, TEXT("AccessKey="), AccessKey))
	{
		UE_LOG(LogVirtualization, Error, TEXT("[%s] Config file entry 'AccessKey=' not specified"), *GetDebugName());
		return false;
	}

	if (!FParse::Value(*ConfigEntry, TEXT("SecretKey="), SecretKey))
	{
		UE_LOG(LogVirtualization, Error, TEXT("[%s] Config file entry 'SecretKey=' not specified"), *GetDebugName());
		return false;
	}

	FParse::Bool(*ConfigEntry, TEXT("CheckExistsBeforePush="), bCheckExistsBeforePush);

	HttpManager = MakeShared<FHttpRetrySystem::FManager>(3, FHttpRetrySystem::FRetryTimeoutRelativeSecondsSetting());

	return true;
}

UE::Virtualization::IVirtualizationBackend::EConnectionStatus FVirtualizationPlusS3Backend::OnConnect()
{
	EConnectionStatus Result = EConnectionStatus::Error;

	const FHttpRequestRef HeadBucketRequest = HttpManager->CreateRequest();
	HeadBucketRequest->SetVerb(TEXT("HEAD"));
	HeadBucketRequest->SetURL(BucketUrl);

	HeadBucketRequest->OnProcessRequestComplete().BindLambda([&Result](FHttpRequestPtr, const FHttpResponsePtr& Response, bool)
	{
		if (Response.IsValid() && EHttpResponseCodes::IsOk(Response->GetResponseCode()))
		{
			Result = EConnectionStatus::Connected;
		}
	});

	if (AuthorizeRequest(HeadBucketRequest))
	{
		WaitForRequests();
	}

	return Result;
}

bool FVirtualizationPlusS3Backend::PushData(TArrayView<UE::Virtualization::FPushRequest> Requests, EPushFlags Flags)
{
	bool bResult = true;

	TArray<FHttpRequestRef> PendingRequests;
	PendingRequests.Reserve(Requests.Num());

	for (UE::Virtualization::FPushRequest& Request : Requests)
	{
		if (!EnumHasAllFlags(Flags, EPushFlags::Force) && bCheckExistsBeforePush && DoesPayloadExist(Request.GetIdentifier()))
		{
			Request.SetResult(UE::Virtualization::FPushResult::GetAsAlreadyExists());
			continue;
		}

		FHttpRequestRef PutObjectRequest = HttpManager->CreateRequest();
		PutObjectRequest->SetVerb(TEXT("PUT"));
		PutObjectRequest->SetURL(BucketUrl / LexToString(Request.GetIdentifier()));

		PutObjectRequest->SetHeader(TEXT("Content-Length"), LexToString(Request.GetPayloadSize()));
		PutObjectRequest->SetHeader(TEXT("x-amz-meta-payload-context"), Request.GetContext());

		TSharedRef<FCompositeBufferReaderArchive> PayloadStream = MakeShared<FCompositeBufferReaderArchive>(Request.GetPayload().GetCompressed());
		if (!PutObjectRequest->SetContentFromStream(PayloadStream))
		{
			FBufferArchive PayloadArchive;
			Request.GetPayload().Save(PayloadArchive);

			PutObjectRequest->SetContent(MoveTemp(PayloadArchive));
		}

		PutObjectRequest->OnProcessRequestComplete().BindLambda([&Request, &bResult](FHttpRequestPtr, const FHttpResponsePtr& Response, bool)
		{
			if (Response.IsValid() && EHttpResponseCodes::IsOk(Response->GetResponseCode()))
			{
				Request.SetResult(UE::Virtualization::FPushResult::GetAsPushed());
			}
			else
			{
				Request.SetResult(UE::Virtualization::FPushResult::GetAsError());
				bResult = false;
			}
		});

		if (AuthorizeRequest(PutObjectRequest))
		{
			PendingRequests.Emplace(MoveTemp(PutObjectRequest));
		}
		else
		{
			Request.SetResult(UE::Virtualization::FPushResult::GetAsError());
			bResult = false;
		}
	}

	UE_LOG(LogVirtualizationPlusS3, Verbose, TEXT("Waiting for %d push requests"), PendingRequests.Num());
	WaitForRequests();

	return bResult;
}

bool FVirtualizationPlusS3Backend::PullData(TArrayView<UE::Virtualization::FPullRequest> Requests, EPullFlags Flags, FText& OutErrors)
{
	bool bResult = true;

	TArray<FHttpRequestRef> PendingRequests;
	PendingRequests.Reserve(Requests.Num());

	for (UE::Virtualization::FPullRequest& Request : Requests)
	{
		FHttpRequestRef GetObjectRequest = HttpManager->CreateRequest();
		GetObjectRequest->SetVerb(TEXT("GET"));
		GetObjectRequest->SetURL(BucketUrl / LexToString(Request.GetIdentifier()));

		GetObjectRequest->OnProcessRequestComplete().BindLambda([&Request](FHttpRequestPtr, const FHttpResponsePtr& Response, bool)
		{
			if (Response.IsValid() && EHttpResponseCodes::IsOk(Response->GetResponseCode()))
			{
				FMemoryReader PayloadArchive(Response->GetContent());
				Request.SetPayload(FCompressedBuffer::Load(PayloadArchive));
			}
			else
			{
				Request.SetError();
			}
		});

		if (AuthorizeRequest(GetObjectRequest))
		{
			PendingRequests.Emplace(MoveTemp(GetObjectRequest));
		}
		else
		{
			Request.SetError();
			bResult = false;
		}
	}

	UE_LOG(LogVirtualizationPlusS3, Verbose, TEXT("Waiting for %d pull requests"), PendingRequests.Num());
	WaitForRequests();

	return bResult;
}

bool FVirtualizationPlusS3Backend::DoesPayloadExist(const FIoHash& Id)
{
	bool bResult = false;

	const FHttpRequestRef HeadObjectRequest = HttpManager->CreateRequest();
	HeadObjectRequest->SetVerb(TEXT("HEAD"));
	HeadObjectRequest->SetURL(BucketUrl / LexToString(Id));

	HeadObjectRequest->OnProcessRequestComplete().BindLambda([&bResult](FHttpRequestPtr, const FHttpResponsePtr& Response, bool)
	{
		bResult = Response.IsValid() && EHttpResponseCodes::IsOk(Response->GetResponseCode());
	});

	if (AuthorizeRequest(HeadObjectRequest))
	{
		WaitForRequests();
	}

	return bResult;
}

bool FVirtualizationPlusS3Backend::AuthorizeRequest(const FHttpRequestRef& Request) const
{
	// Docs: https://docs.aws.amazon.com/AmazonS3/latest/API/sig-v4-header-based-auth.html
	// #TODO(Ves): Chunked upload (https://docs.aws.amazon.com/AmazonS3/latest/API/sigv4-streaming.html)

	const FString Url = Request->GetURL();

	const TCHAR* SchemeEnd = FCString::Strchr(*Url, TEXT(':'));
	if (SchemeEnd == nullptr || (SchemeEnd[1] != TEXT('/') && SchemeEnd[2] != TEXT('/')))
	{
		UE_LOG(LogVirtualizationPlusS3, Error, TEXT("Failed to process URL: %s"), *Url);
		return false;
	}

	const TCHAR* HostStart = SchemeEnd + 3;
	const TCHAR* HostEnd = FCString::Strchr(HostStart, TEXT('/'));

	const FString Host = HostEnd ? FString(UE_PTRDIFF_TO_INT32(HostEnd - HostStart), HostStart) : FString(HostStart);
	Request->SetHeader(TEXT("Host"), Host);

	const FString ContentHash = TEXT("UNSIGNED-PAYLOAD");
	Request->SetHeader(TEXT("x-amz-content-sha256"), ContentHash);

	TAnsiStringBuilder<24> Timestamp;
	const FDateTime Now = FDateTime::UtcNow();
	Timestamp.Appendf("%04d%02d%02dT%02d%02d%02dZ", Now.GetYear(), Now.GetMonth(), Now.GetDay(), Now.GetHour(), Now.GetMinute(), Now.GetSecond());
	Request->SetHeader(TEXT("x-amz-date"), Timestamp.ToString());

	// 1) Create a canonical request

	FStringView CanonicalUri;
	TStringBuilder<128> CanonicalQuery;

	if (HostEnd)
	{
		if (const TCHAR* QueryStart = FCString::Strchr(HostEnd, TEXT('?')))
		{
			CanonicalUri = FStringView(HostEnd, UE_PTRDIFF_TO_INT32(QueryStart - HostEnd));

			TArray<FStringView> QueryParams;
			UE::String::ParseTokens(QueryStart + 1, TEXT('&'), QueryParams);
			Algo::Sort(QueryParams);

			UE::String::JoinTo(QueryParams, TEXT('&'), CanonicalQuery);
		}
		else
		{
			CanonicalUri = FStringView(HostEnd);
		}
	}
	else
	{
		CanonicalUri = TEXTVIEW("/");
	}

	TAnsiStringBuilder<256> CanonicalHeaders;
	TAnsiStringBuilder<128> SignedHeaders;

	TArray<FString> Headers = Request->GetAllHeaders();
	Algo::Sort(Headers);

	for (const FString& Header : Headers)
	{
		FString HeaderName, HeaderValue;
		if (Header.Split(TEXT(":"), &HeaderName, &HeaderValue))
		{
			HeaderName.ToLowerInline();

			CanonicalHeaders << HeaderName << ':' << HeaderValue.TrimStartAndEnd() << '\n';
			SignedHeaders << HeaderName << ';';
		}
	}

	// Strip trailing separator from signed headers
	SignedHeaders.RemoveSuffix(1);

	TAnsiStringBuilder<512> CanonicalRequest;
	CanonicalRequest << Request->GetVerb() << "\n";
	CanonicalRequest << CanonicalUri << "\n";
	CanonicalRequest << CanonicalQuery << "\n";
	CanonicalRequest << CanonicalHeaders << "\n";
	CanonicalRequest << SignedHeaders << "\n";
	CanonicalRequest << ContentHash;

	// 2) Create a string to sign

	TAnsiStringBuilder<48> Scope;
	Scope.Appendf("%.8s/%s/s3/aws4_request", *Timestamp, TCHAR_TO_ANSI(*Region));

	TAnsiStringBuilder<72> CanonicalRequestHash;
	CanonicalRequestHash << FSha256::HashBuffer(CanonicalRequest.ToView());

	TAnsiStringBuilder<160> StringToSign;
	StringToSign.Appendf("AWS4-HMAC-SHA256\n%s\n%s\n%s", *Timestamp, *Scope, *CanonicalRequestHash);

	// 3) Calculate signature

	TAnsiStringBuilder<48> SignatureKey;
	SignatureKey.Appendf("AWS4%s", TCHAR_TO_ANSI(*SecretKey));

	FSha256 Signature = FSha256::HmacBuffer(SignatureKey.ToView(), Timestamp.ToView().Left(8));
	Signature.HmacUpdate(TCHAR_TO_ANSI(*Region));
	Signature.HmacUpdate("s3");
	Signature.HmacUpdate("aws4_request");
	Signature.HmacUpdate(StringToSign);

	TStringBuilder<256> Authorization;
	Authorization.Appendf(TEXT("AWS4-HMAC-SHA256 Credential=%s/%s, SignedHeaders=%s, Signature="), *AccessKey, ANSI_TO_TCHAR(*Scope), ANSI_TO_TCHAR(*SignedHeaders));
	Authorization << Signature;

	Request->SetHeader(TEXT("Authorization"), Authorization.ToString());

	return Request->ProcessRequest();
}

void FVirtualizationPlusS3Backend::WaitForRequests() const
{
	// Blocking game thread is not ideal; any alternatives short of a custom HTTP system?
	if (IsInGameThread())
	{
		const double StartTime = FPlatformTime::Seconds();
		HttpManager->BlockUntilFlushed(FLT_MAX);
		UE_LOG(LogVirtualizationPlusS3, Verbose, TEXT("WaitForRequests took %0.2f seconds (direct)"), FPlatformTime::Seconds() - StartTime);
	}
	else
	{
		FFunctionGraphTask::CreateAndDispatchWhenReady([this]
		{
			const double StartTime = FPlatformTime::Seconds();
			HttpManager->BlockUntilFlushed(FLT_MAX);
			UE_LOG(LogVirtualizationPlusS3, Verbose, TEXT("WaitForRequests took %0.2f seconds (task)"), FPlatformTime::Seconds() - StartTime);
		}, TStatId(), nullptr, ENamedThreads::GameThread)->Wait();
	}
}

UE_REGISTER_VIRTUALIZATION_BACKEND_FACTORY(FVirtualizationPlusS3Backend, VirtualizationPlusS3);
