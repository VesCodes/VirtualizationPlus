#pragma once

#include <Memory/CompositeBuffer.h>
#include <Serialization/Archive.h>
#include <String/BytesToHex.h>

struct FSha256
{
	static FSha256 HashBuffer(const uint8* Input, size_t InputLength);
	static FSha256 HashBuffer(const FAnsiStringView& Input);

	static FSha256 HmacBuffer(const void* Key, size_t KeyLength, const uint8* Input, size_t InputLength);
	static FSha256 HmacBuffer(const FAnsiStringView& Key, const FAnsiStringView& Input);

	void Update(const uint8* Input, size_t InputLength);
	void HmacUpdate(const uint8* Input, size_t InputLength);
	void HmacUpdate(const FAnsiStringView& Input);

	uint8 Digest[32] = {};
};

template<typename CharType>
TStringBuilderBase<CharType>& operator<<(TStringBuilderBase<CharType>& Builder, const FSha256& Hash)
{
	UE::String::BytesToHexLower(Hash.Digest, Builder);
	return Builder;
}

class FCompositeBufferReaderArchive : public FArchive
{
public:
	explicit FCompositeBufferReaderArchive(const FCompositeBuffer& InBuffer);

	virtual void Serialize(void* Data, int64 Length) override;
	virtual void Seek(int64 InPos) override;
	virtual int64 Tell() override;
	virtual int64 TotalSize() override;

private:
	FCompositeBuffer Buffer;

	int64 CurrentPos = 0;
	int64 BufferSize = 0;
};
