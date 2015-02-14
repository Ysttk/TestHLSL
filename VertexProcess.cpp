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
