#include "DXUT.h"
#include "VetexProcess.h"
#include "Global.h"

struct CUSTOMVERTEX
{
	FLOAT x,y,z;
	DWORD color;
};

#define D3DFVF_CUSTOMVERTEX (D3DFVF_XYZ|D3DFVF_DIFFUSE)

CUSTOMVERTEX vertices[] =
{
	{ 250.0f,  250.0f, 0.5f,  0xffff0000, }, // x, y, z, rhw, color
	{ 50.0f, 50.0f, 0.5f,  0xff00ff00, },
	{  250.0f, 50.0f, 0.5f, 0xff0000ff, },
	{ 50.0f, 50.0f, 1000.0f, 0xff00ff00, },
	{  250.0f, 50.0f, 1000.0f, 0xff0000ff, },
	{  50.0f, 250.0f, 1000.0f, 0xffff0000, },
};


HRESULT ProcessVertex(IDirect3DDevice9* pDevice)
{


	IDirect3DVertexBuffer9* pVB;
	if (!pDevice) return E_FAIL;

	if (FAILED(g_pDevice->CreateVertexBuffer(6*sizeof(CUSTOMVERTEX), 0, D3DFVF_CUSTOMVERTEX, D3DPOOL_DEFAULT, &pVB, NULL))) return E_FAIL;

	VOID* pVertices;
	if (FAILED(pVB->Lock(0, sizeof(vertices), (void**)&pVertices, 0))) return E_FAIL;

	memcpy(pVertices, vertices, sizeof(vertices));
	pVB->Unlock();




	pDevice->SetStreamSource(0, pVB, 0, sizeof(CUSTOMVERTEX));

	pDevice->SetFVF(D3DFVF_CUSTOMVERTEX);

	pDevice->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 2);
	pVB->Release();

	return S_OK;
}

//所谓的“骨骼”，实际上是一个矩阵，本质上描述的是一种相对于父骨骼（或者世界坐标系）的变换；因此，骨骼蒙皮动画方法实际上是首先保存蒙皮顶点的局部坐标（相对于某块骨骼），然后
//通过递归计算累计所有父骨骼的变换，最后将此变换应用到蒙皮顶点坐标上，完成顶点在相邻两帧的坐标变化。

struct D3DXFRAME_DERIVED : public D3DXFRAME
{
	D3DXMATRIXA16 CombinedTransformationMatrix;	//该矩阵保存本骨骼每帧相对于世界坐标系的运动矩阵
	// 每帧开始时候，首先计算世界坐标系和视角变换，然后调用动画控制对象g_pAnimationController->AdvanceTime函数，更新所有骨骼中的TransformationMatrix矩阵；
	// 最后根据变化后的世界坐标系，每根骨骼变化后的TransformationMatrix得到没根骨骼的最后相对于世界坐标系的CombinedTransformationMatrix矩阵
};

//--------------------------------------------------------------------------------------
// Name: struct D3DXMESHCONTAINER_DERIVED
// Desc: Structure derived from D3DXMESHCONTAINER so we can add some app-specific
//       info that will be stored with each mesh
//--------------------------------------------------------------------------------------
struct D3DXMESHCONTAINER_DERIVED : public D3DXMESHCONTAINER
{
	LPDIRECT3DTEXTURE9* ppTextures;       // array of textures, entries are NULL if no texture specified    
										//base中的pMaterials如果指明了使用Texture，那么就把TextureFileName指定的贴图读进来，存在该结构中；如果没有，那么可以直接设置pMaterials元素为需要使用的材质（材质包含：diffuse, ambient, specular, emissive, power)

	// SkinMesh info             
	LPD3DXMESH pOrigMesh;	//用来保存base中的MeshData，MeshData会在每帧用变换矩阵计算变换后的最终坐标（保存之前需要把相关的数据准备下，比如没有面向量的Mesh需要计算保存面向量等）
	D3DXMATRIX* pBoneOffsetMatrices;	//查询pSkinInfo接口将所有相关骨骼的矩阵保存到本数组中

	LPD3DXATTRIBUTERANGE pAttributeTable;
	
	DWORD NumInfl;		//查询SkinInfo->ConvertToBlendedMesh接口所得的每个顶点最多被NumInfl根骨骼影响
	LPD3DXBUFFER pBoneCombinationBuf;	//D3DXBONECOMBINATION*类型，每个元素为一个draw call单元，包含draw的起点顶点和终点顶点、起点终点mesh以及涉及到的骨骼；
	DWORD NumAttributeGroups; //查询SkinInfo->ConvertToBlendedMesh接口返回pBoneCombinationBuf总共有多少个元素

	D3DXMATRIX** ppBoneMatrixPtrs;	//SetupBoneMatrixPointersOnMesh函数中保存指向骨骼（D3DXFRAME_DERIVED）的CombinedTransformationMatrix地址，每帧更新时候直接访问CombinedTransformationMatrix得到本帧骨骼相对于世界坐标系的变换
	DWORD iAttributeSW;     // used to denote the split between SW and HW if necessary for non-indexed skinning
	DWORD NumPaletteEntries;
	bool UseSoftwareVP;

};