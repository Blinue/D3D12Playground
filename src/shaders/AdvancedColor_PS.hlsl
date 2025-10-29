cbuffer RootConstants : register(b0) {
	float boost;
};

float4 main(noperspective float2 uv : TEXCOORD) : SV_Target {
	const float3 p3_r = { 1.052922, -0.036065, -0.016857 };
	const float3 p3_g = { -0.304578, 1.411079, -0.106501 };
	const float3 p3_b = { 0, 0, 1 };
	
	float3 c1 = lerp(p3_r, (p3_r + p3_b) / 2, uv.x);
	float3 c2 = lerp(p3_g, p3_b, uv.x);
	return float4(lerp(c1, c2, uv.y) * boost, 1);
}
