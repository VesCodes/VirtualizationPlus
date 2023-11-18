# VirtualizationPlus

Additional backends for Unreal Engine's [Virtual Assets](https://docs.unrealengine.com/en-US/virtual-assets-in-unreal-engine/) system.

## Usage

For detailed instructions on how to set up asset virtualization please follow the [official documentation](https://docs.unrealengine.com/en-US/virtual-assets-quickstart-in-unreal-engine/). The configuration below includes examples for all provided backends, you will need to adapt it for your use:

```ini
[Core.ContentVirtualization]
SystemName=Default

[Core.VirtualizationModule]
BackendGraph=VirtualizationBackendGraph_Example

[VirtualizationBackendGraph_Example]
PersistentStorageHierarchy=(Entry=ExampleS3)
CacheStorageHierarchy=(Entry=ExampleDDC)
ExampleS3=(Type=VirtualizationPlusS3, BucketUrl="https://s3.amazonaws.com/examplebucket", Region="us-east-1", AccessKey=" AKIAIOSFODNN7EXAMPLE", SecretKey="wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY")
ExampleDDC=(Type=DDCBackend)
```

## S3 compatible object storage

The `VirtualizationPlusS3` backend supports any S3 compatible object storage. Tested with AWS S3, Backblaze B2, and Wasabi.

Required configuration:
* **BucketUrl**: Target bucket location
* **Region**: Target bucket region
* **AccessKey**: Access key ID
* **SecretKey**: Secret access key

Optional configuration:
* **CheckExistsBeforePush**: (defaults to `true`) Whether to check if a payload exists before attempting to push; useful as a cost-cutting measure for providers that charge more for `GetObject` than `PutObject` requests (e.g. Backblaze)