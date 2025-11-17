cbuffer RootConstants : register(b0) {
	float boost;
};

float4 main(noperspective float2 uv : TEXCOORD) : SV_Target {
	const float3 p3_r = { 1.22494, -0.042057, -0.019638 };
	const float3 p3_g = { -0.22494, 1.042057, -0.078636 };
	const float3 p3_b = { 0, 0, 1.098274 };
	
	float3 c1 = lerp(p3_b, (p3_r + p3_b) / 2, uv.x);
	float3 c2 = lerp(p3_g, p3_r, uv.x);
	return float4(lerp(c1, c2, uv.y) * boost, 1);
}
