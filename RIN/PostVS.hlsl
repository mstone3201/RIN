struct vsOutput {
	float4 position : SV_POSITION;
	float2 tex : TEXCOORD;
};

[RootSignature(
	"RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |"\
		"DENY_VERTEX_SHADER_ROOT_ACCESS |"\
		"DENY_HULL_SHADER_ROOT_ACCESS |"\
		"DENY_DOMAIN_SHADER_ROOT_ACCESS |"\
		"DENY_GEOMETRY_SHADER_ROOT_ACCESS"\
	"),"\
	"DescriptorTable(SRV(t0), visibility = SHADER_VISIBILITY_PIXEL),"\
	"StaticSampler("\
		"s0,"\
		"filter = FILTER_MIN_MAG_LINEAR_MIP_POINT,"\
		"addressU = TEXTURE_ADDRESS_CLAMP,"\
		"addressV = TEXTURE_ADDRESS_CLAMP,"\
		"addressW = TEXTURE_ADDRESS_CLAMP,"\
		"visibility = SHADER_VISIBILITY_PIXEL"\
	")"
)]
vsOutput main(
	float2 position : POSITION,
	float2 tex : TEXCOORD
) {
	vsOutput output;
	output.position = float4(position, 0.0f, 1.0f);
	output.tex = tex;
	return output;
}