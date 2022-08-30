struct Size {
	uint2 size;
};

ConstantBuffer<Size> childSize : register(b0);
Texture2D<float> parentMIP : register(t0);
RWTexture2D<float> childMIP : register(u0);
SamplerState parentMIPSampler : register(s0);

[RootSignature(
	"RootFlags(0),"\
	"RootConstants(num32BitConstants = 2, b0),"\
	"DescriptorTable(SRV(t0)),"\
	"DescriptorTable(UAV(u0)),"\
	"StaticSampler("\
		"s0,"\
		"filter = FILTER_MAXIMUM_MIN_MAG_LINEAR_MIP_POINT,"\
		"addressU = TEXTURE_ADDRESS_CLAMP,"\
		"addressV = TEXTURE_ADDRESS_CLAMP,"\
		"addressW = TEXTURE_ADDRESS_CLAMP"\
	")"
)]
[numthreads(32, 32, 1)]
void main(
	uint3 index : SV_DispatchThreadID
) {
	if(index.x < childSize.size.x && index.y < childSize.size.y)
		childMIP[index.xy] = parentMIP.SampleLevel(parentMIPSampler, (float2(index.xy) + 0.5f) / childSize.size, 0);
}