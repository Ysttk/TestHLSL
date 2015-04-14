
float4x4 g_WorldTransform;

float4x4 g_ViewProject : VIEWPROJECTION;
static const int MAX_MATRICES = 26;
float4x3 mWorldMatrixArray[MAX_MATRICES] : WORLDMATRIXARRAY;

float4 MaterialAmbient : MATERIALAMBIENT = {0.1f, 0.1f, 0.1f, 1.0f};
float4 MaterialDiffuse : MATERIALDIFFUSE = {0.8f, 0.8f, 0.8f, 1.0f};
float4 lhtDir = {0.0f, 0.0f, -1.0f, 1.0f};    //light Direction 
float4 lightDiffuse = {0.6f, 0.6f, 0.6f, 1.0f}; // Light Diffuse

texture g_MeshTexture;
sampler MeshTextureSampler =
sampler_state
{
	Texture = <g_MeshTexture>;
	MipFilter = LINEAR;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
};

struct VS_INPUT
{
	float4 vPos: POSITION;
	float4 BlendWeights	: BLENDWEIGHT;
	float4 BlendIndices :BLENDINDICES;
	float3 vNormal: NORMAL;
	float3 vTexCoord: TEXCOORD0;
};

struct VS_OUTPUT
{
	float4 vPos: POSITION;
	float4 vDiffuse : COLOR0;
	float2 vTexCoord: TEXCOORD0;
};

float3 Diffuse(float3 Normal)
{
	float CosTheta;
	
	CosTheta = max(0.0f, dot(Normal, lhtDir.xyz));

	return (CosTheta);
}


VS_OUTPUT RenderVS(VS_INPUT vsIn, uniform int NumBones)
{
	VS_OUTPUT output;
	output.vTexCoord = vsIn.vTexCoord.xy;
	//output.vPos = mul(mul(vsIn.vPos, g_WorldTransform), g_ViewProject);
	float3 Pos= 0.0f;
	float3 Normal = 0.0f;
	float LastWeight = 0.0f;

	float BlendWeightsArray[4] = (float[4])vsIn.BlendWeights;
	int IndexArray[4] =	(int[4])D3DCOLORtoUBYTE4(vsIn.BlendIndices);
	//IndexArray[3] = IndexArray[2] = IndexArray[1] = IndexArray[0] = 0 ;

	for (int iBone=0; iBone<NumBones-1; iBone++) {
		LastWeight = LastWeight + BlendWeightsArray[iBone];
		Pos += mul(vsIn.vPos, mWorldMatrixArray[IndexArray[iBone]]) * BlendWeightsArray[iBone];
		Normal += mul(vsIn.vNormal, mWorldMatrixArray[IndexArray[iBone]]) * BlendWeightsArray[iBone];
	}

	LastWeight = 1 - LastWeight;

	Pos += mul(vsIn.vPos, mWorldMatrixArray[IndexArray[NumBones-1]]) * LastWeight;
	Normal += mul(vsIn.vNormal, mWorldMatrixArray[IndexArray[NumBones-1]]) * LastWeight;
	
	Normal = normalize(Normal);

	output.vPos = mul(float4(Pos.xyz, 1.0f), g_ViewProject);

	output.vDiffuse.xyz = MaterialAmbient.xyz + Diffuse(Normal) * MaterialDiffuse.xyz;
	output.vDiffuse.w = 1.0f;

	return output;
}

struct PS_OUTPUT
{
	float4 RGBColor: COLOR0;
};

PS_OUTPUT RenderPS(VS_OUTPUT psIn)
{
	PS_OUTPUT psOut;
	psOut.RGBColor = tex2D(MeshTextureSampler, psIn.vTexCoord)*psIn.vDiffuse;
	return psOut;
}

int CurNumBones = 2;
VertexShader vsArray[4] = { compile vs_2_0 RenderVS(1),
							compile vs_2_0 RenderVS(2),
							compile vs_2_0 RenderVS(3),
							compile vs_2_0 RenderVS(4)
};

technique Tech1
{
	pass P0
	{
		VertexShader = (vsArray[CurNumBones]);
		PixelShader = compile ps_2_0 RenderPS();
	}
}

