Texture2D<float4> backBuffer : register(t0);
SamplerState backBufferSampler : register(s0);

float4 main(
	float4 position : SV_POSITION,
	float2 tex : TEXCOORD
) : SV_TARGET {
	return backBuffer.SampleLevel(backBufferSampler, tex, 0);
}