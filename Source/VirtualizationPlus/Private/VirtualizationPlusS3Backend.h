#pragma once

#include <HttpRetrySystem.h>
#include <IVirtualizationBackend.h>

class FVirtualizationPlusS3Backend : public UE::Virtualization::IVirtualizationBackend
{
public:
	explicit FVirtualizationPlusS3Backend(FStringView ProjectName, FStringView ConfigName, FStringView DebugName);

	virtual bool Initialize(const FString& ConfigEntry) override;
	virtual bool PushData(TArrayView<UE::Virtualization::FPushRequest> Requests, EPushFlags Flags) override;
	virtual bool PullData(TArrayView<UE::Virtualization::FPullRequest> Requests, EPullFlags Flags, FText& OutErrors) override;
	virtual bool DoesPayloadExist(const FIoHash& Id) override;

private:
	virtual EConnectionStatus OnConnect() override;

	bool AuthorizeRequest(const FHttpRequestRef& Request) const;
	void WaitForRequests() const;

	FString BucketUrl;
	FString Region;
	FString AccessKey;
	FString SecretKey;

	/// Whether to check if a payload exists before attempting to push
	bool bCheckExistsBeforePush = true;

	TSharedPtr<FHttpRetrySystem::FManager> HttpManager = nullptr;
};
