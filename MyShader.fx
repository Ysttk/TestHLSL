
float4x4 g_WorldTransform;

float4x4 g_ViewProject;
static const int MAX_MATRICES = 26;
float4x3 mWorldMatrixArray[MAX_MATRICES];


struct VS_INPUT
{
	float4 vPos: POSITION;
	float4 BlendWeights	: BLENDWEIGHT;
	float4 BlendIndices :BLENDINDICES;
	float3 vNormal: NORMAL;
	float2 vTexCoord: TEXCOORD0;
};

struct VS_OUTPUT
{
	float4 vPos: POSITION;
	float2 vTexCoord: TEXCOORD0;
	float4 vDiffuse : COLOR0;
};


VS_OUTPUT RenderVS(VS_INPUT vsIn)
{
	VS_OUTPUT output;
	output.vTexCoord = vsIn.vTexCoord;
	output.vPos = mul(mul(vsIn.vPos, g_WorldTransform), g_ViewProject);
	float3 vNormalInWorldSpace;
	vNormalInWorldSpace = normalize(mul(vsIn.vNormal, g_WorldTransform));

}

struct PS_OUTPUT
{
	float4 RGBColor: COLOR0;
};

PS_OUTPUT RenderPS(VS_OUTPUT psIn)
{

}


technique Tech1
{
	pass P0
	{
		vs = compile vs_1_1 RenderVS();
		ps = compile ps_1_1 RenderPS();
	}
}

