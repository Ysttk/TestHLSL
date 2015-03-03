#include "DXUT.h"
#include "Animation.h"
#include "Global.h"
#include "SDKmisc.h"
#include <time.h>
#include <map>




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
	D3DXMATRIX* pBoneOffsetMatrices;	//查询pSkinInfo接口将所有相关骨骼的矩阵保存到本数组中；其中存储的每一个矩阵代表世界坐标系到所对应的骨骼坐标系的变换（参考：http://lib.yoekey.com/?p=264）


	DWORD NumInfl;		//查询SkinInfo->ConvertToBlendedMesh接口所得的每个顶点最多被NumInfl根骨骼影响
	LPD3DXBUFFER pBoneCombinationBuf;	//D3DXBONECOMBINATION*类型，每个元素为一个draw call单元，包含draw的起点顶点和终点顶点、起点终点mesh以及涉及到的骨骼；
	DWORD NumAttributeGroups; //查询SkinInfo->ConvertToBlendedMesh接口返回pBoneCombinationBuf总共有多少个元素；

	DWORD NumPaletteEntries;	//作用同NumInfl，
	DWORD iAttributeSW;     // used to denote the split between SW and HW if necessary for non-indexed skinning

	D3DXMATRIX** ppBoneMatrixPtrs;	//SetupBoneMatrixPointersOnMesh函数中保存指向骨骼（D3DXFRAME_DERIVED）的CombinedTransformationMatrix地址，每帧更新时候直接访问CombinedTransformationMatrix得到本帧骨骼相对于世界坐标系的变换

	LPD3DXATTRIBUTERANGE pAttributeTable;	//用途和结构和pBoneCombinationBuf类似，只不过每个元素不包含骨骼信息（因为是独立维护的），用于软件渲染
	bool UseSoftwareVP;
};



struct AniVertex {
	float x,y,z;
	float u,v;
	AniVertex(float ax,float ay,float az,float au,float av) :
	x(az), y(ay), z(az), u(au), v(av){}
};

#define D3DFVF_ANIVERTEX (D3DFVF_XYZ | D3DFVF_TEX1)

LPDIRECT3DVERTEXBUFFER9 g_pFloor;
LPDIRECT3DTEXTURE9 g_pFloorTexture;
D3DLIGHT9 g_Light;
LPD3DXFRAME g_pRootFrame;

void DoJW3AnimationInit()
{

	g_pDevice->CreateVertexBuffer(4*sizeof(AniVertex), 0, D3DFVF_ANIVERTEX, D3DPOOL_MANAGED, &g_pFloor, NULL);
	AniVertex* pVertex = NULL;
	g_pFloor->Lock(0, 0, (void**)&pVertex, 0);
	pVertex[0] = AniVertex(-5000.0f, 0.0f, -5000.0f, 0.0f, 30.0f);
	pVertex[1] = AniVertex(-5000.0f, 0.0f, 5000.0f, 0.0f, 0.0f);
	pVertex[2] = AniVertex(5000.0f, 0.0f, -5000.0f, 30.0f, 30.0f);
	pVertex[3] = AniVertex(5000.0f, 0.0f, 5000.0f, 30.0f, 0.0f);
	g_pFloor->Unlock();

	D3DXCreateTextureFromFileW(g_pDevice, L"GameMedia\\wood.jpg", &g_pFloorTexture);
	g_pDevice->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
	g_pDevice->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
	g_pDevice->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	g_pDevice->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);


	::ZeroMemory(&g_Light, sizeof(g_Light));
	g_Light.Type = D3DLIGHT_DIRECTIONAL;
	g_Light.Ambient = D3DXCOLOR(0.7f, 0.7f, 0.7f, 1.0f);
	g_Light.Diffuse = D3DXCOLOR(1.0f, 1.0f, 1.0f, 1.0f);
	g_Light.Specular = D3DXCOLOR(0.9f, 0.9f, 0.9f, 1.0f);
	g_Light.Direction = D3DXVECTOR3(1.0f, 1.0f, 1.0f);

	g_pDevice->SetLight(0, &g_Light);
	g_pDevice->LightEnable(0, true);

	g_pDevice->SetRenderState(D3DRS_NORMALIZENORMALS, true);
	g_pDevice->SetRenderState(D3DRS_SPECULARENABLE, true);

	D3DXVECTOR3 vEyePt(0.0f, 200.0f, -800.0f);
	D3DXVECTOR3 vLookatPt(0.0f, 400.0f, 0.0f);
	D3DXVECTOR3 vLook = vLookatPt - vEyePt;
	D3DXVec3Normalize(&vLook, &vLook);
	D3DXVECTOR3 vUp(0.0f, 1.0f, 0.0f);
	D3DXMATRIXA16 matView;
	D3DXMatrixLookAtLH(&matView, &vEyePt, &vLookatPt, &vUp);
	g_pDevice->SetTransform(D3DTS_VIEW, &matView);
	D3DXMATRIX matProj;
	D3DXMatrixPerspectiveFovLH(&matProj, D3DX_PI/4.0f, 1.0f, 1.0f, 200000.0f);
	g_pDevice->SetTransform(D3DTS_PROJECTION, &matProj);


}


LPD3DXANIMATIONCONTROLLER g_pAnimationController;

class MyFrameAlloc : public ID3DXAllocateHierarchy
{

public:
	STDMETHOD(CreateFrame)(THIS_ LPCSTR Name, LPD3DXFRAME* ppNewFrame);
	STDMETHOD(CreateMeshContainer) (
		THIS_ LPCSTR Name, 
		CONST D3DXMESHDATA *pMeshData,
		CONST D3DXMATERIAL *pMaterials,
		CONST D3DXEFFECTINSTANCE* pEffectInst,
		DWORD NumMeterials,
		CONST DWORD* pAdjacency,
		LPD3DXSKININFO pSkinInfo,
		LPD3DXMESHCONTAINER* ppNewMeshContainer);
	STDMETHOD(DestroyFrame) (THIS_ LPD3DXFRAME pFrame);
	STDMETHOD(DestroyMeshContainer) (THIS_ LPD3DXMESHCONTAINER pMeshContainer);

};

struct MyFrame : D3DXFRAME 
{
	D3DXMATRIXA16 CombinedTransformMatrix;
};

HRESULT MyFrameAlloc::CreateFrame(LPCSTR Name, LPD3DXFRAME* ppNewFrame)
{
	*ppNewFrame = new MyFrame;
	MyFrame* pMyFrame = (MyFrame*)(*ppNewFrame);
	int len = strlen(Name)+1;
	(*ppNewFrame)->Name = new CHAR[len];
	memcpy((*ppNewFrame)->Name, Name, len);
	(*ppNewFrame)->pFrameFirstChild=NULL;
	(*ppNewFrame)->pFrameSibling = NULL;
	(*ppNewFrame)->pMeshContainer = NULL;
	D3DXMatrixIdentity(&(*ppNewFrame)->TransformationMatrix);
	D3DXMatrixIdentity(&pMyFrame->CombinedTransformMatrix);
	return S_OK;
}

struct MyMeshContainer : D3DXMESHCONTAINER 
{
	D3DXMESHDATA OriginalMesh;
	DWORD* pOrigAdjacency;
	DWORD AdjacencyLen;
	LPD3DXMATRIX* pRelatedBoneCombineTransformMatrices;
	LPD3DXMATRIX pBoneOffsetMatrices;

	// Add for NONINDEXED style
	DWORD MaxNumInfl, NumAttriGroup;
	DWORD* pNumInflList;
	LPD3DXBUFFER pBoneCombineBuff;

	// Add for INDEXED style
	DWORD NumMaxFaceInfl, NumPalattes;


	LPDIRECT3DTEXTURE9* ppTextures;
};

void GenerateDrawableMesh(MyMeshContainer* pMeshContainer)
{
#ifdef NONINDEXED
	D3DCAPS9 d3dCaps9;
	LPDIRECT3DDEVICE9 pDevice;
	pMeshContainer->OriginalMesh.pMesh->GetDevice(&pDevice);
	pDevice->GetDeviceCaps(&d3dCaps9);

	pMeshContainer->pSkinInfo->ConvertToBlendedMesh(
		pMeshContainer->OriginalMesh.pMesh,
		D3DXMESH_MANAGED|D3DXMESHOPT_VERTEXCACHE,
		pMeshContainer->pAdjacency,
		NULL, NULL, NULL,
		&pMeshContainer->MaxNumInfl, 
		&pMeshContainer->NumAttriGroup,
		&pMeshContainer->pBoneCombineBuff,
		&pMeshContainer->MeshData.pMesh);
	pMeshContainer->MeshData.Type = pMeshContainer->OriginalMesh.Type;
	LPD3DXBONECOMBINATION pBoneCombinationBuf = reinterpret_cast<LPD3DXBONECOMBINATION>(pMeshContainer->pBoneCombineBuff->GetBufferPointer());
	pMeshContainer->pNumInflList = new DWORD[pMeshContainer->NumAttriGroup];
	bool hardwareBlend = true;
	for (int iAttriIdx=0; iAttriIdx<pMeshContainer->NumAttriGroup; iAttriIdx++) {
		printf("\n");
		DWORD cInfl = 0;
		for (int iInfl=0; iInfl<pMeshContainer->MaxNumInfl; iInfl++)
			if (pBoneCombinationBuf[iAttriIdx].BoneId[iInfl] != UINT_MAX) {
				cInfl ++; 
				printf("%d ", pBoneCombinationBuf[iAttriIdx].BoneId[iInfl]);
			} else printf("-1 ");
		printf("AttriIdx %d, NumInfl %d", iAttriIdx, cInfl);

		pMeshContainer->pNumInflList[iAttriIdx] = cInfl;
		if (cInfl > d3dCaps9.MaxVertexBlendMatrices) hardwareBlend=false;
	}

	if (!hardwareBlend) {
		LPD3DXMESH pNewMesh;
		pMeshContainer->MeshData.pMesh->CloneMeshFVF(D3DXMESH_SOFTWAREPROCESSING|pMeshContainer->MeshData.pMesh->GetOptions(),
			pMeshContainer->MeshData.pMesh->GetFVF(), pDevice, &pNewMesh);
		pMeshContainer->MeshData.pMesh->Release();
		pMeshContainer->MeshData.pMesh = pNewMesh;
		pNewMesh = NULL;
	}
	
#elif defined INDEXED

	LPDIRECT3DINDEXBUFFER9 pIB;
	pMeshContainer->OriginalMesh.pMesh->GetIndexBuffer(&pIB);
	DWORD NumMaxFaceInfl;
	pMeshContainer->pSkinInfo->GetMaxFaceInfluences(pIB, pMeshContainer->OriginalMesh.pMesh->GetNumFaces(), &NumMaxFaceInfl);
	pIB->Release();
	NumMaxFaceInfl = NumMaxFaceInfl>12?12:NumMaxFaceInfl;
	LPDIRECT3DDEVICE9 pDevice;
	D3DCAPS9 d3dCaps;
	pMeshContainer->OriginalMesh.pMesh->GetDevice(&pDevice);
	pDevice->GetDeviceCaps(&d3dCaps);
	pMeshContainer->NumMaxFaceInfl = NumMaxFaceInfl;
	DWORD flags = D3DXMESHOPT_VERTEXCACHE;
	if (NumMaxFaceInfl > d3dCaps.MaxVertexBlendMatrixIndex) {
		pMeshContainer->NumPalattes = MIN(256, pMeshContainer->pSkinInfo->GetNumBones());
		flags |= D3DXMESH_SYSTEMMEM;
	} else {
		pMeshContainer->NumPalattes = MIN( (d3dCaps.MaxVertexBlendMatrixIndex+1)/2, pMeshContainer->pSkinInfo->GetNumBones());
		flags |= D3DXMESH_MANAGED;
	}

	pMeshContainer->pSkinInfo->ConvertToIndexedBlendedMesh(
		pMeshContainer->OriginalMesh.pMesh,
		flags,
		pMeshContainer->NumPalattes,
		pMeshContainer->pAdjacency,
		NULL, NULL, NULL,
		&pMeshContainer->MaxNumInfl,
		&pMeshContainer->NumAttriGroup,
		&pMeshContainer->pBoneCombineBuff,
		&pMeshContainer->MeshData.pMesh);
	pMeshContainer->MeshData.Type = pMeshContainer->OriginalMesh.Type;

#elif defined SOFTWARE

	LPDIRECT3DDEVICE9 pDevice;
	pMeshContainer->OriginalMesh.pMesh->GetDevice(&pDevice);
	LPD3DXMESHDATA pMeshData = &pMeshContainer->OriginalMesh;
	pMeshData->pMesh->AddRef();
	if (!(pMeshData->pMesh->GetFVF() & D3DFVF_NORMAL)) 
	{
		LPD3DXMESH newMesh = NULL;
		pMeshContainer->OriginalMesh.pMesh->CloneMeshFVF(pMeshData->pMesh->GetOptions(), pMeshData->pMesh->GetFVF()|D3DFVF_NORMAL, pDevice, 
			&newMesh);
		D3DXComputeNormals(newMesh, NULL);
		pMeshContainer->OriginalMesh.Type = D3DXMESHTYPE_MESH;
		pMeshContainer->OriginalMesh.pMesh->Release();
		pMeshContainer->OriginalMesh.pMesh = newMesh;
		pMeshContainer->OriginalMesh.pMesh->AddRef();
		newMesh->Release();
	}
	memset(&pMeshContainer->MeshData, 0, sizeof(pMeshContainer->MeshData));
	HRESULT hr = pMeshContainer->OriginalMesh.pMesh->CloneMeshFVF(D3DXMESH_MANAGED, pMeshContainer->OriginalMesh.pMesh->GetFVF(), pDevice, 
		&pMeshContainer->MeshData.pMesh);
	pMeshContainer->MeshData.Type = pMeshContainer->OriginalMesh.Type;
	pMeshData->pMesh->Release();

#endif
}

HRESULT MyFrameAlloc::CreateMeshContainer(LPCSTR Name, CONST D3DXMESHDATA *pMeshData, CONST D3DXMATERIAL* pMaterials, 
	CONST D3DXEFFECTINSTANCE* pEffectInst, DWORD NumMaterials, CONST DWORD* pAdjacency, LPD3DXSKININFO pSkinInfo, LPD3DXMESHCONTAINER* ppNewMeshContainer)
{
	LPDIRECT3DDEVICE9 pDevice = NULL;

	*ppNewMeshContainer = new MyMeshContainer;
	MyMeshContainer* pMeshContainer = (MyMeshContainer*)(*ppNewMeshContainer);
	int len = strlen(Name)+1;
	(*ppNewMeshContainer)->Name = new CHAR[len];
	memcpy((*ppNewMeshContainer)->Name, Name, len);

	
	pMeshContainer->OriginalMesh = *pMeshData;
	pMeshData->pMesh->AddRef();
	pMeshData->pMesh->GetDevice(&pDevice);

	
	//HRESULT hr = pMeshData->pMesh->CloneMeshFVF(pMeshData->pMesh->GetOptions(), pMeshData->pMesh->GetFVF(), pDevice, &pMeshContainer->MeshData.pMesh);


	pMeshContainer->AdjacencyLen = pMeshData->pMesh->GetNumFaces();
	pMeshContainer->pOrigAdjacency = new DWORD[pMeshContainer->AdjacencyLen*3];
	memcpy(pMeshContainer->pOrigAdjacency, pAdjacency, pMeshContainer->AdjacencyLen*3*sizeof(DWORD));
	pMeshContainer->pAdjacency = pMeshContainer->pOrigAdjacency;

	if (pSkinInfo) {
		(*ppNewMeshContainer)->pSkinInfo = pSkinInfo;
		pSkinInfo->AddRef();
		DWORD numBones = pSkinInfo->GetNumBones();
		pMeshContainer->pBoneOffsetMatrices = new D3DXMATRIX[numBones];
		for (int idx=0; idx<numBones; idx++)
			pMeshContainer->pBoneOffsetMatrices[idx] = *(pSkinInfo->GetBoneOffsetMatrix(idx));
	}
	(*ppNewMeshContainer)->pEffects = NULL;

	(*ppNewMeshContainer)->pMaterials = new D3DXMATERIAL[NumMaterials];
	pMeshContainer->ppTextures = new LPDIRECT3DTEXTURE9[NumMaterials];
	memcpy((*ppNewMeshContainer)->pMaterials, pMaterials, sizeof(D3DXMATERIAL)*NumMaterials);
	for (int idx=0; idx<NumMaterials; idx++) {
		if ((*ppNewMeshContainer)->pMaterials[idx].pTextureFilename) {
			WCHAR name[1024], fullPath[1024];
			MultiByteToWideChar(CP_ACP, 0, pMaterials[idx].pTextureFilename, -1, name, 1024);
			name[1023]=0; fullPath[0]=0;
			lstrcat(fullPath, L"GameMedia\\Tiny\\");
			lstrcat(fullPath, name);
			//DXUTFindDXSDKMediaFileCch(fullPath, 1024, name);
			if (FAILED(D3DXCreateTextureFromFile(pDevice, fullPath, &pMeshContainer->ppTextures[idx])))
				pMeshContainer->ppTextures[idx]=NULL;
			(*ppNewMeshContainer)->pMaterials[idx].pTextureFilename = NULL;
		}
	}
	if (NumMaterials==0) {
		pMeshContainer->pMaterials = new D3DXMATERIAL[1];
		memset(&pMeshContainer->pMaterials[0].MatD3D, 0, sizeof(D3DMATERIAL9));
		pMeshContainer->pMaterials[0].MatD3D.Diffuse.r =
			pMeshContainer->pMaterials[0].MatD3D.Diffuse.g =
			pMeshContainer->pMaterials[0].MatD3D.Diffuse.b =
			pMeshContainer->pMaterials[0].MatD3D.Diffuse.a = 0.5f;
		pMeshContainer->pMaterials[0].MatD3D.Specular = pMeshContainer->pMaterials[0].MatD3D.Diffuse;
	}
	(*ppNewMeshContainer)->NumMaterials = NumMaterials;
	(*ppNewMeshContainer)->pNextMeshContainer = NULL;

	GenerateDrawableMesh(pMeshContainer);

	return S_OK;
}

HRESULT MyFrameAlloc::DestroyFrame(LPD3DXFRAME pFrame)
{
	SAFE_DELETE_ARRAY(pFrame->Name);
	SAFE_DELETE(pFrame);
	return S_OK;
}

HRESULT MyFrameAlloc::DestroyMeshContainer(LPD3DXMESHCONTAINER pMeshContainer)
{
	MyMeshContainer* pMyMeshContainer = (MyMeshContainer*)pMeshContainer;
	SAFE_DELETE_ARRAY(pMyMeshContainer->pAdjacency);
	SAFE_DELETE_ARRAY(pMyMeshContainer->pMaterials);
	SAFE_RELEASE(pMyMeshContainer->OriginalMesh.pMesh);
	SAFE_RELEASE(pMyMeshContainer->pSkinInfo);
	for (int idx=0; idx<pMyMeshContainer->NumMaterials; idx++) 
		SAFE_RELEASE(pMyMeshContainer->ppTextures[idx])
	SAFE_DELETE_ARRAY(pMyMeshContainer->ppTextures);
	SAFE_DELETE_ARRAY(pMyMeshContainer->pBoneOffsetMatrices);
#ifdef NONINDEXED
	SAFE_DELETE_ARRAY(pMyMeshContainer->pNumInflList);
#endif
	SAFE_DELETE(pMyMeshContainer);
	return S_OK;
}

void SetupMeshContainer2BoneMatrices(LPD3DXFRAME pFrame)
{
	MyMeshContainer* meshContainer = (MyMeshContainer*)pFrame->pMeshContainer;
	while (meshContainer) {
		DWORD numBones = meshContainer->pSkinInfo->GetNumBones();
		meshContainer->pRelatedBoneCombineTransformMatrices = new LPD3DXMATRIX[numBones];
		for (int idx=0; idx<numBones; idx++) {
			MyFrame* tmpFrame = (MyFrame*)D3DXFrameFind(g_pRootFrame, meshContainer->pSkinInfo->GetBoneName(idx));
			meshContainer->pRelatedBoneCombineTransformMatrices[idx] = &tmpFrame->CombinedTransformMatrix;
		}
		meshContainer = (MyMeshContainer*)meshContainer->pNextMeshContainer;
	}

	if (pFrame->pFrameFirstChild) SetupMeshContainer2BoneMatrices(pFrame->pFrameFirstChild);
	if (pFrame->pFrameSibling) SetupMeshContainer2BoneMatrices(pFrame->pFrameSibling);
}

D3DXVECTOR3                 g_vObjectCenter;        // Center of bounding sphere of object
FLOAT                       g_fObjectRadius;        // Radius of bounding sphere of object
//CD3DArcBall                 g_ArcBall;              // Arcball for model control
D3DXMATRIXA16 g_matProj;

void DoD3DAnimationInit()
{
	LPDIRECT3DDEVICE9 pd3dDevice = g_pDevice;
	pd3dDevice->SetRenderState( D3DRS_LIGHTING, TRUE );
	pd3dDevice->SetRenderState( D3DRS_DITHERENABLE, TRUE );
	pd3dDevice->SetRenderState( D3DRS_ZENABLE, TRUE );
	pd3dDevice->SetRenderState( D3DRS_CULLMODE, D3DCULL_CCW );
	pd3dDevice->SetRenderState( D3DRS_AMBIENT, 0x33333333 );
	pd3dDevice->SetRenderState( D3DRS_NORMALIZENORMALS, TRUE );
	pd3dDevice->SetSamplerState( 0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR );
	pd3dDevice->SetSamplerState( 0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR );

	MyFrameAlloc Alloc;
	TCHAR wd[1024];
	GetCurrentDirectory(1024, wd);
	D3DXLoadMeshHierarchyFromX(L"GameMedia\\Tiny\\tiny.x", D3DXMESH_MANAGED, g_pDevice, &Alloc, NULL, &g_pRootFrame, &g_pAnimationController);
	SetupMeshContainer2BoneMatrices(g_pRootFrame);
	HRESULT hr = D3DXFrameCalculateBoundingSphere( g_pRootFrame, &g_vObjectCenter, &g_fObjectRadius );

	float fAspect = 1.3333334f;
	D3DXMatrixPerspectiveFovLH( &g_matProj, D3DX_PI / 4, fAspect,
		g_fObjectRadius / 64.0f, g_fObjectRadius * 200.0f );
	g_pDevice->SetTransform( D3DTS_PROJECTION, &g_matProj );

}

void DoAnimationInit()
{
#ifdef USE_JW3ANIM
	DoJW3AnimationInit();
#else
	DoD3DAnimationInit();
#endif
}

#define MAX_NUM_BONES 1024

void DoMeshContainerRender(MyMeshContainer* pMeshContainer)
{
#ifdef NONINDEXED
	LPDIRECT3DDEVICE9 pDevice;
	pMeshContainer->MeshData.pMesh->GetDevice(&pDevice);
	D3DCAPS9 d3dCaps;
	pDevice->GetDeviceCaps(&d3dCaps);
	LPD3DXBONECOMBINATION pBoneCombinationBuff = reinterpret_cast<LPD3DXBONECOMBINATION>(pMeshContainer->pBoneCombineBuff->GetBufferPointer());

	printf("\nFace:%d Vertex:%d\n", pMeshContainer->MeshData.pMesh->GetNumFaces(), pMeshContainer->MeshData.pMesh->GetNumVertices());
	printf("Original Face:%d Original Vertex:%d\n", pMeshContainer->OriginalMesh.pMesh->GetNumFaces(), pMeshContainer->OriginalMesh.pMesh->GetNumVertices());
	for (int i=0; i<pMeshContainer->NumAttriGroup; i++) {
		printf("\n");
		printf("%d %d %d %d", pBoneCombinationBuff[i].FaceStart, pBoneCombinationBuff[i].FaceCount, pBoneCombinationBuff[i].VertexStart, pBoneCombinationBuff[i].VertexCount);

	}

	for (int iAttriIdx=0; iAttriIdx<pMeshContainer->NumAttriGroup; iAttriIdx++) {
		DWORD numInfl = pMeshContainer->pNumInflList[iAttriIdx];
		if (numInfl > d3dCaps.MaxVertexBlendMatrices) {
			pDevice->SetSoftwareVertexProcessing(TRUE);
		} 
		for (int boneInfl=0; boneInfl<numInfl; boneInfl++) {
			int boneIdx = pBoneCombinationBuff[iAttriIdx].BoneId[boneInfl];
			D3DXMATRIX transformMatrix;
			D3DXMatrixMultiply(&transformMatrix, 
				pMeshContainer->pSkinInfo->GetBoneOffsetMatrix(boneIdx),
				pMeshContainer->pRelatedBoneCombineTransformMatrices[boneIdx]);
			pDevice->SetTransform(D3DTS_WORLDMATRIX(boneInfl), &transformMatrix);
		}
		pDevice->SetRenderState(D3DRS_VERTEXBLEND, numInfl);

		pDevice->SetMaterial(&pMeshContainer->pMaterials[pBoneCombinationBuff[iAttriIdx].AttribId].MatD3D);
		pDevice->SetTexture(0, pMeshContainer->ppTextures[pBoneCombinationBuff[iAttriIdx].AttribId]);
		pMeshContainer->MeshData.pMesh->DrawSubset(iAttriIdx);

		if (numInfl > d3dCaps.MaxVertexBlendMatrices)
			pDevice->SetSoftwareVertexProcessing(FALSE);
	}

	pDevice->SetRenderState(D3DRS_VERTEXBLEND, 0);
#elif defined INDEXED
	LPDIRECT3DDEVICE9 pDevice;
	pMeshContainer->MeshData.pMesh->GetDevice(&pDevice);
	D3DCAPS9 d3dCaps;
	pDevice->GetDeviceCaps(&d3dCaps);
	LPD3DXBONECOMBINATION pBoneCombinationBuff = reinterpret_cast<LPD3DXBONECOMBINATION>(pMeshContainer->pBoneCombineBuff->GetBufferPointer());
	//以下打印出的信息完全不能理解！！！*_*！
	printf("\nFace:%d Vertex:%d\n", pMeshContainer->MeshData.pMesh->GetNumFaces(), pMeshContainer->MeshData.pMesh->GetNumVertices());
	printf("Original Face:%d Original Vertex:%d\n", pMeshContainer->OriginalMesh.pMesh->GetNumFaces(), pMeshContainer->OriginalMesh.pMesh->GetNumVertices());
	for (int i=0; i<pMeshContainer->NumAttriGroup; i++) {
		printf("\n");
		printf("%d %d %d %d", pBoneCombinationBuff[i].FaceStart, pBoneCombinationBuff[i].FaceCount, pBoneCombinationBuff[i].VertexStart, pBoneCombinationBuff[i].VertexCount);
			
	}

	if (pMeshContainer->NumMaxFaceInfl>d3dCaps.MaxVertexBlendMatrixIndex)
		pDevice->SetSoftwareVertexProcessing(TRUE);

	if( pMeshContainer->MaxNumInfl == 1 )
	{
		 pDevice->SetRenderState(D3DRS_VERTEXBLEND, D3DVBF_0WEIGHTS);
	}
	else
	{
		pDevice->SetRenderState( D3DRS_VERTEXBLEND, pMeshContainer->MaxNumInfl - 1 );
	}

	pDevice->SetRenderState(D3DRS_INDEXEDVERTEXBLENDENABLE, TRUE);

	for (int iAttriIdx=0; iAttriIdx<pMeshContainer->NumAttriGroup; iAttriIdx++) {
		int count=0;
		for (int iInfl=0; iInfl<pMeshContainer->NumPalattes; iInfl++) {
			UINT boneIdx = pBoneCombinationBuff[iAttriIdx].BoneId[iInfl];
			if (boneIdx != UINT_MAX) {
				D3DXMATRIX targetMatrix;
				D3DXMatrixMultiply(&targetMatrix, pMeshContainer->pSkinInfo->GetBoneOffsetMatrix(boneIdx), pMeshContainer->pRelatedBoneCombineTransformMatrices[boneIdx]);
				
				pDevice->SetTransform(D3DTS_WORLDMATRIX(iInfl), &targetMatrix);
				count ++;
			}
		}
		//pDevice->SetRenderState(D3DRS_VERTEXBLEND, count);

		pDevice->SetMaterial(&pMeshContainer->pMaterials[pBoneCombinationBuff[iAttriIdx].AttribId].MatD3D);
		pDevice->SetTexture(0, pMeshContainer->ppTextures[pBoneCombinationBuff[iAttriIdx].AttribId]);

		pMeshContainer->MeshData.pMesh->DrawSubset(iAttriIdx);
	}

	pDevice->SetRenderState(D3DRS_INDEXEDVERTEXBLENDENABLE, FALSE);
	pDevice->SetRenderState(D3DRS_VERTEXBLEND, 0);
	
	if (pMeshContainer->NumMaxFaceInfl > d3dCaps.MaxVertexBlendMatrixIndex)
		pDevice->SetSoftwareVertexProcessing(FALSE);

#elif defined SOFTWARE
	
	DWORD NumBones = pMeshContainer->pSkinInfo->GetNumBones();
	D3DXMATRIXA16 boneMatrices[MAX_NUM_BONES];
	for (int idx=0; idx<NumBones; idx++) {
		LPD3DXMATRIX boneOffsetMatrix = pMeshContainer->pSkinInfo->GetBoneOffsetMatrix(idx);
		//D3DXMatrixMultiply(boneMatrices+idx, boneOffsetMatrix, pMeshContainer->pRelatedBoneCombineTransformMatrices[idx]);	
		D3DXMatrixMultiply(boneMatrices+idx, &(pMeshContainer->pBoneOffsetMatrices[idx]), pMeshContainer->pRelatedBoneCombineTransformMatrices[idx]);	
	}

	LPDIRECT3DDEVICE9 pDevice;
	pMeshContainer->MeshData.pMesh->GetDevice(&pDevice);

	//D3DXMATRIX identity;
	//D3DXMatrixIdentity(&identity);
	//pDevice->SetTransform(D3DTS_WORLD, &identity);

	float *pVSrc, *pVDest;
	pMeshContainer->OriginalMesh.pMesh->LockVertexBuffer(D3DLOCK_READONLY, (LPVOID*)&pVSrc);
	pMeshContainer->MeshData.pMesh->LockVertexBuffer(0, (LPVOID*)&pVDest);
	pMeshContainer->pSkinInfo->UpdateSkinnedMesh(boneMatrices, NULL, pVSrc, pVDest);
	pMeshContainer->MeshData.pMesh->UnlockVertexBuffer();
	pMeshContainer->OriginalMesh.pMesh->UnlockVertexBuffer();

	DWORD numAttribute; 
	D3DXATTRIBUTERANGE* pAttributeTbl;
	pMeshContainer->MeshData.pMesh->GetAttributeTable(NULL, &numAttribute);
	pAttributeTbl = new D3DXATTRIBUTERANGE[numAttribute];
	pMeshContainer->MeshData.pMesh->GetAttributeTable(pAttributeTbl, NULL);


	for (int idx=0; idx<numAttribute; idx++) {
		pDevice->SetMaterial(&(pMeshContainer->pMaterials[pAttributeTbl[idx].AttribId].MatD3D));
		pDevice->SetTexture(0, pMeshContainer->ppTextures[pAttributeTbl[idx].AttribId]);
		pMeshContainer->MeshData.pMesh->DrawSubset(pAttributeTbl[idx].AttribId);
	}

	SAFE_DELETE_ARRAY(pAttributeTbl);
#endif
}

void DoFrameRender(LPD3DXFRAME pFrame)
{
	LPD3DXMESHCONTAINER pMeshContainer;
	pMeshContainer = pFrame->pMeshContainer;

	while (pMeshContainer) {
		DoMeshContainerRender((MyMeshContainer*)pMeshContainer);
		pMeshContainer = pMeshContainer->pNextMeshContainer;
	}

	if (pFrame->pFrameFirstChild) DoFrameRender(pFrame->pFrameFirstChild);
	if (pFrame->pFrameSibling) DoFrameRender(pFrame->pFrameSibling);
}

void DoFrameMove(LPD3DXFRAME pFrame, LPD3DXMATRIX pMatParent)
{
	MyFrame* pMyFrame = (MyFrame*)pFrame;
	D3DXMatrixMultiply(&pMyFrame->CombinedTransformMatrix,  &pFrame->TransformationMatrix, pMatParent);
	if (pFrame->pFrameFirstChild) DoFrameMove(pFrame->pFrameFirstChild, &pMyFrame->CombinedTransformMatrix);
	if (pFrame->pFrameSibling) DoFrameMove(pFrame->pFrameSibling, pMatParent);
}

LARGE_INTEGER GetTimeNow()
{
	LARGE_INTEGER qwTime;
	QueryPerformanceCounter(&qwTime);

	return qwTime;
}

LONGLONG GetFrequence()
{
	LARGE_INTEGER time;
	QueryPerformanceFrequency(&time);
	return time.QuadPart;
}

LARGE_INTEGER PreviousRenderTime = GetTimeNow();
LONGLONG TickPerSecond = GetFrequence();

void DoAnimationRender()
{

	D3DXMATRIX matWorld;
	D3DXMatrixIdentity(&matWorld);
	D3DXMatrixTranslation( &matWorld, -g_vObjectCenter.x,
		-g_vObjectCenter.y,
		-g_vObjectCenter.z );
	g_pDevice->SetTransform( D3DTS_WORLD, &matWorld );

	D3DXMATRIX g_matView;
	D3DXVECTOR3 vEye( 0, 0, -3 * g_fObjectRadius );
	D3DXVECTOR3 vAt( 0, 0, 0 );
	D3DXVECTOR3 vUp( 0, 1, 0 );
	D3DXMatrixLookAtLH( &g_matView, &vEye, &vAt, &vUp );
	g_pDevice->SetTransform( D3DTS_VIEW, &g_matView );

	float timeElapsed = (GetTimeNow().QuadPart - PreviousRenderTime.QuadPart)/(double)TickPerSecond;
	g_pAnimationController->AdvanceTime(timeElapsed, NULL);
	PreviousRenderTime = GetTimeNow();

	DoFrameMove(g_pRootFrame, &matWorld);

	g_pDevice->Clear(0, NULL, D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER, D3DCOLOR_ARGB(0,66,75,255), 1.0f, 0);

	D3DLIGHT9 light;
	D3DXVECTOR3 vecLightDirUnnormalized( 0.0f, -1.0f, 1.0f );
	ZeroMemory( &light, sizeof( D3DLIGHT9 ) );
	light.Type = D3DLIGHT_DIRECTIONAL;
	light.Diffuse.r = 1.0f;
	light.Diffuse.g = 1.0f;
	light.Diffuse.b = 1.0f;
	D3DXVec3Normalize( ( D3DXVECTOR3* )&light.Direction, &vecLightDirUnnormalized );
	light.Position.x = 0.0f;
	light.Position.y = 1000.0f;
	light.Position.z = -1000.0f;
	light.Range = 10000.0f;

	g_pDevice->SetLight(0, &light);
	g_pDevice->LightEnable(0, TRUE);

	g_pDevice->BeginScene();

	DoFrameRender(g_pRootFrame);

	g_pDevice->EndScene();
	g_pDevice->Present(NULL, NULL, NULL, NULL);
}