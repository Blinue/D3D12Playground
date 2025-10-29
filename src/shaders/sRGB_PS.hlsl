float4 main(noperspective float2 uv : TEXCOORD) : SV_Target {
	float3 c1 = lerp(float3(1, 0, 0), float3(0.5, 0, 0.5), uv.x);
	float3 c2 = lerp(float3(0, 1, 0), float3(0, 0, 1), uv.x);
	return float4(lerp(c1, c2, uv.y), 1);
}
