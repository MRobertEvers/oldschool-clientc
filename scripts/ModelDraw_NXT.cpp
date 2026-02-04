__int64 __fastcall jag::oldscape::dash3d::SoftwareModelLitRenderer::WorldRender(
    jag::oldscape::dash3d::SoftwareModelLitRenderer *a1,
    __int64 a2,
    unsigned int a3,
    int a4,
    int a5,
    int a6,
    int a7,
    unsigned int a8,
    unsigned int a9,
    unsigned int a10,
    jag::oldscape::dash3d::MousePickingHelper *a11,
    unsigned int a12)
{
__int64 v12; // rax
const char *v13; // r8
jag::oldscape::dash3d::ModelLitImpl *v14; // rax
jag::oldscape::dash3d::ModelLitImpl *v15; // rbx
int v16; // r12d
int v17; // r15d
int v18; // r14d
int RenderDepth; // eax
int v20; // esi
unsigned int *v21; // rcx
jag::oldscape::dash3d::MousePickingHelper *v22; // rdi
int v23; // ebx
int v24; // r13d
int v25; // esi
int v26; // r12d
int v27; // r8d
__int64 v28; // rsi
int v29; // r9d
unsigned int v30; // r14d
unsigned __int8 IsActiveEntityTypecode; // al
BOOL v32; // ecx
__int64 v33; // rsi
int v34; // ecx
int v35; // r8d
jag::oldscape::dash3d::Pix3D *v36; // rax
int v37; // edi
int v38; // ebx
jag::oldscape::dash3d::Pix3D *v39; // rax
jag::oldscape::dash3d::ModelLitImpl *v40; // rsi
__int64 v41; // r12
int v42; // ebx
__int64 v43; // rsi
int v44; // r12d
int v45; // r15d
int v46; // ecx
unsigned int v47; // ebx
int v48; // r12d
unsigned int v49; // r15d
int v50; // r13d
int v51; // r15d
int v52; // r14d
int v53; // r15d
int v54; // ebx
__int64 v55; // r12
_DWORD *v56; // rax
jag::oldscape::dash3d::Pix3D *v57; // r15
int v58; // eax
int v59; // r13d
int v60; // ebx
int v61; // ebx
char *v62; // rdi
int v63; // r15d
int *v64; // rax
char *v66; // [rsp+8h] [rbp-E8h]
char *v67; // [rsp+10h] [rbp-E0h]
char *v68; // [rsp+18h] [rbp-D8h]
char *v69; // [rsp+20h] [rbp-D0h]
_BYTE v70[20]; // [rsp+28h] [rbp-C8h] BYREF
BOOL v71; // [rsp+3Ch] [rbp-B4h]
__int64 CosTable; // [rsp+40h] [rbp-B0h]
char *v73; // [rsp+48h] [rbp-A8h]
char *v74; // [rsp+50h] [rbp-A0h]
char *v75; // [rsp+58h] [rbp-98h]
__int64 SinTable; // [rsp+60h] [rbp-90h]
__int64 v77; // [rsp+68h] [rbp-88h]
char *v78; // [rsp+70h] [rbp-80h]
char *v79; // [rsp+78h] [rbp-78h]
int v80; // [rsp+84h] [rbp-6Ch]
__int64 v81; // [rsp+88h] [rbp-68h]
int v82; // [rsp+94h] [rbp-5Ch]
unsigned int v83; // [rsp+98h] [rbp-58h]
int v84; // [rsp+9Ch] [rbp-54h]
jag::oldscape::dash3d::ModelLitImpl *v85; // [rsp+A0h] [rbp-50h]
jag::oldscape::dash3d::Pix3D **v86; // [rsp+A8h] [rbp-48h]
int v87; // [rsp+B0h] [rbp-40h]
int v88; // [rsp+B4h] [rbp-3Ch]
int MouseScreenX; // [rsp+B8h] [rbp-38h]
int MouseScreenY; // [rsp+BCh] [rbp-34h]
int v91; // [rsp+C0h] [rbp-30h]
bool v92; // [rsp+C7h] [rbp-29h]

v82 = a6;
v87 = a5;
v88 = a4;
v83 = a3;
v86 = (jag::oldscape::dash3d::Pix3D **)a1;
SinTable = jag::oldscape::dash3d::Pix3D::GetSinTable(a1);
CosTable = jag::oldscape::dash3d::Pix3D::GetCosTable(a1);
(*(void (__fastcall **)(_BYTE *, __int64, _QWORD))(*(_QWORD *)a2 + 16LL))(v70, a2, a12);
if ( !(unsigned __int8)jag::operator==<jag::oldscape::rs2lib::worldmap::MapElement>(v70, 0) )
{
v12 = jag::shared_ptr<jag::oldscape::dash3d::ModelLit>::operator->(v70);
if ( (*(unsigned __int8 (__fastcall **)(__int64))(*(_QWORD *)v12 + 56LL))(v12) )
{
  jag::LogAssert(
    (jag *)"!mPtr->IsForRSeven()",
    "/Users/bamboo/bamboo-home-01/xml-data/build-dir/OSRS-OS1669-BOXR/libs/oldscape/jagex3/dash3d/SoftwareModelLitRenderer.cpp",
    (const char *)0xAD,
    0,
    v13);
  __debugbreak();
}
v14 = (jag::oldscape::dash3d::ModelLitImpl *)jag::shared_ptr<jag::oldscape::dash3d::ModelLit>::operator*(v70);
v15 = v14;
if ( *((_DWORD *)v14 + 126) != 1 )
  jag::oldscape::dash3d::ModelLitImpl::CalcBoundingCylinder(v14);
jag::oldscape::dash3d::ModelLitImpl::CalcAABB(v15, v83);
v16 = (int)(a7 * a10 - v82 * a8) >> 16;
v85 = v15;
v17 = (v87 * *((_DWORD *)v15 + 128)) >> 16;
v77 = (unsigned int)((int)(v88 * a9 + v87 * v16) >> 16);
v18 = v17 + v77;
if ( v17 + (int)v77 >= 51 )
{
  RenderDepth = jag::oldscape::dash3d::Pix3D::GetRenderDepth(v86[804653]);
  if ( (int)v77 < RenderDepth )
  {
    v84 = v82 * a10 + a7 * a8;
    v20 = *((_DWORD *)v85 + 128);
    v21 = (unsigned int *)v86[804653];
    v22 = (jag::oldscape::dash3d::MousePickingHelper *)v21[4];
    v23 = (_DWORD)v22 * ((v84 >> 16) - v20);
    LODWORD(v73) = v23 / v18;
    if ( v23 / v18 < (int)v21[10] )
    {
      v24 = (_DWORD)v22 * (v20 + (v84 >> 16));
      LODWORD(v74) = v24 / v18;
      if ( v24 / v18 > (int)v21[9] )
      {
        LODWORD(v78) = v87 * a9 - v88 * v16;
        v25 = (v88 * v20) >> 16;
        LODWORD(v75) = (_DWORD)v22 * (v25 + ((int)v78 >> 16));
        if ( (int)v75 / v18 > (int)v21[11] )
        {
          v26 = (int)v75 / v18;
          v27 = *((_DWORD *)v85 + 2);
          v28 = (unsigned int)(((v87 * v27) >> 16) + v25);
          v29 = (_DWORD)v22 * (((int)v78 >> 16) - v28);
          if ( v29 / v18 < (int)v21[12] )
          {
            v91 = v29 / v18;
            v81 = (unsigned int)((_DWORD)v22 * (((int)v78 >> 16) - v28));
            v92 = 1;
            if ( (int)v77 - (v17 + ((v88 * v27) >> 16)) >= 51 )
              v92 = *((_DWORD *)v85 + 79) > 0;
            MouseScreenX = jag::oldscape::dash3d::MousePickingHelper::GetMouseScreenX(v22);
            v30 = v83;
            MouseScreenY = jag::oldscape::dash3d::MousePickingHelper::GetMouseScreenY(v22);
            LOBYTE(v79) = jag::oldscape::dash3d::MousePickingHelper::IsMousePickingEnabled(v22);
            IsActiveEntityTypecode = jag::oldscape::dash3d::MousePickingHelper::IsActiveEntityTypecode(a11, v28);
            v80 = 0;
            v32 = 0;
            if ( ((unsigned __int8)v79 & IsActiveEntityTypecode) != 1 )
              goto LABEL_34;
            if ( jag::oldscape::dash3d::MousePickingHelper::m_useAABBMouseCheck )
            {
              v33 = v30;
              if ( !(unsigned __int8)jag::oldscape::dash3d::MousePickingHelper::IsMouseIntersectingModelAABB<jag::oldscape::dash3d::SoftwarePix3D>(
                                       v85,
                                       v30,
                                       a8,
                                       a9,
                                       a10,
                                       v86) )
                goto LABEL_32;
            }
            else
            {
              v34 = 50;
              if ( (int)v77 - v17 >= 51 )
                v34 = v77 - v17;
              if ( v84 < 0x10000 )
              {
                v33 = (unsigned int)(v23 / v34);
                v35 = (int)v74;
              }
              else
              {
                v35 = v24 / v34;
                v33 = (unsigned int)v73;
              }
              if ( (int)v78 < 0x10000 )
                v91 = (int)v81 / v34;
              else
                v26 = (int)v75 / v34;
              v36 = v86[804653];
              v37 = MouseScreenX - *((_DWORD *)v36 + 5);
              v32 = 0;
              if ( v37 <= (int)v33 )
                goto LABEL_34;
              if ( v37 >= v35 )
                goto LABEL_34;
              v38 = MouseScreenY - *((_DWORD *)v36 + 6);
              if ( v38 <= v91 || v38 >= v26 )
                goto LABEL_34;
            }
            if ( !*((_BYTE *)v85 + 16) )
            {
              LOBYTE(v32) = 1;
              goto LABEL_34;
            }
            jag::oldscape::dash3d::MousePickingHelper::AddPickingEntity(a11, v33);
LABEL_32:
            v32 = 0;
LABEL_34:
            v71 = v32;
            v39 = v86[804653];
            MouseScreenX = *((_DWORD *)v39 + 5);
            MouseScreenY = *((_DWORD *)v39 + 6);
            v91 = 0;
            if ( v30 )
            {
              v80 = *(_DWORD *)std::array<int,2048ul>::operator[](SinTable, (int)v30);
              v91 = *(_DWORD *)std::array<int,2048ul>::operator[](CosTable, (int)v30);
            }
            v40 = v85;
            if ( *((int *)v85 + 5) <= 0 )
            {
              LODWORD(v81) = 0;
            }
            else
            {
              v73 = (char *)v85 + 24;
              v74 = (char *)v85 + 48;
              v78 = (char *)v85 + 72;
              v75 = (char *)(v86 + 8126);
              v79 = (char *)(v86 + 1626);
              v66 = (char *)(v86 + 4876);
              v67 = (char *)(v86 + 11376);
              v68 = (char *)(v86 + 14626);
              v69 = (char *)(v86 + 17876);
              v41 = 0;
              LODWORD(v81) = 0;
              do
              {
                v42 = *(_DWORD *)jag::Array<int,eastl::allocator>::operator[](v73, v41);
                v43 = v41;
                v44 = *(_DWORD *)jag::Array<int,eastl::allocator>::operator[](v74, v41);
                CosTable = v43;
                v45 = *(_DWORD *)jag::Array<int,eastl::allocator>::operator[](v78, v43);
                if ( v30 )
                {
                  v46 = (v80 * v45 + v91 * v42) >> 16;
                  v45 = (v91 * v45 - v80 * v42) >> 16;
                  v42 = v46;
                }
                v47 = a8 + v42;
                v48 = a9 + v44;
                v49 = a10 + v45;
                v50 = (int)(v82 * v49 + a7 * v47) >> 16;
                v51 = (int)(a7 * v49 - v82 * v47) >> 16;
                v52 = (v87 * v48 - v88 * v51) >> 16;
                v53 = v88 * v48 + v87 * v51;
                LODWORD(SinTable) = v53 >> 16;
                v54 = (v53 >> 16) - v77;
                v55 = CosTable;
                v56 = (_DWORD *)std::array<int,6500ul>::operator[](v75, CosTable);
                *v56 = v54;
                v84 = v50;
                if ( v53 < 3276800 )
                {
                  LOBYTE(v56) = 1;
                  LODWORD(v81) = (_DWORD)v56;
                  v61 = -5000;
                  v62 = v79;
                  v63 = SinTable;
                }
                else
                {
                  v57 = v86[804653];
                  v58 = v50 * *((_DWORD *)v57 + 4);
                  v59 = SinTable;
                  v60 = MouseScreenX + v58 / (int)SinTable;
                  *(_DWORD *)std::array<int,6500ul>::operator[](v79, v55) = v60;
                  v61 = MouseScreenY + v52 * *((_DWORD *)v57 + 4) / v59;
                  v62 = v66;
                  v63 = v59;
                }
                *(_DWORD *)std::array<int,6500ul>::operator[](v62, v55) = v61;
                if ( v92 )
                {
                  v64 = (int *)std::array<int,6500ul>::operator[](v67, v55);
                  *v64 = v84;
                  *(_DWORD *)std::array<int,6500ul>::operator[](v68, v55) = v52;
                  *(_DWORD *)std::array<int,6500ul>::operator[](v69, v55) = v63;
                }
                v41 = v55 + 1;
                v40 = v85;
                v30 = v83;
              }
              while ( v41 < *((int *)v85 + 5) );
            }
            jag::oldscape::dash3d::SoftwareModelLitRenderer::Render2(
              (jag::oldscape::dash3d::SoftwareModelLitRenderer *)v86,
              v40,
              v81 & 1,
              v71,
              (__int64)a11);
          }
        }
      }
    }
  }
}
}
return jag::shared_ptr<jag::oldscape::FileRSevenDebugPresetProvider>::~shared_ptr(v70);
}