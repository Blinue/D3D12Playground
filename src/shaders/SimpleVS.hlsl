void main(
	float2 pos : POSITION,
	float4 color : COLOR,
	out noperspective float4 outColor : COLOR,
	out noperspective float4 outPos : SV_POSITION
) {
	outPos = float4(pos, 0, 1);
	outColor = color;
}
