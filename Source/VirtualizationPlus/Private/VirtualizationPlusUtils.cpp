#include "VirtualizationPlusUtils.h"

#include <Containers/StringView.h>

#if WITH_SSL
#include <openssl/hmac.h>
#include <openssl/sha.h>
#endif

FSha256 FSha256::HashBuffer(const uint8* Input, size_t InputLength)
{
	FSha256 Result;
	Result.Update(Input, InputLength);
	return Result;
}

FSha256 FSha256::HashBuffer(const FAnsiStringView& Input)
{
	return HashBuffer(reinterpret_cast<const uint8*>(Input.GetData()), Input.Len());
}

void FSha256::Update(const uint8* Input, size_t InputLength)
{
	SHA256(Input, InputLength, Digest);
}

FSha256 FSha256::HmacBuffer(const void* Key, size_t KeyLength, const uint8* Input, size_t InputLength)
{
	FSha256 Result;
	HMAC(EVP_sha256(), Key, KeyLength, Input, InputLength, Result.Digest, nullptr);
	return Result;
}

FSha256 FSha256::HmacBuffer(const FAnsiStringView& Key, const FAnsiStringView& Input)
{
	return HmacBuffer(Key.GetData(), Key.Len(), reinterpret_cast<const uint8*>(Input.GetData()), Input.Len());
}

void FSha256::HmacUpdate(const uint8* Input, size_t InputLength)
{
	HMAC(EVP_sha256(), Digest, sizeof(Digest), Input, InputLength, Digest, nullptr);
}

void FSha256::HmacUpdate(const FAnsiStringView& Input)
{
	HmacUpdate(reinterpret_cast<const uint8*>(Input.GetData()), Input.Len());
}

FCompositeBufferReaderArchive::FCompositeBufferReaderArchive(const FCompositeBuffer& InBuffer)
	: Buffer(InBuffer)
{
	SetIsLoading(true);
	SetIsPersistent(false);

	BufferSize = static_cast<int64>(Buffer.GetSize());
}

void FCompositeBufferReaderArchive::Serialize(void* Data, int64 Length)
{
	if (Length && !IsError())
	{
		if (CurrentPos + Length <= BufferSize)
		{
			Buffer.CopyTo(MakeMemoryView(Data, Length), CurrentPos);
			CurrentPos += Length;
		}
		else
		{
			SetError();
		}
	}
}

void FCompositeBufferReaderArchive::Seek(int64 InPos)
{
	CurrentPos = InPos;
}

int64 FCompositeBufferReaderArchive::Tell()
{
	return CurrentPos;
}

int64 FCompositeBufferReaderArchive::TotalSize()
{
	return BufferSize;
}
