__int64 __fastcall jag::oldscape::dash3d::world::Render(
    jag::oldscape::dash3d::world *this,
    jag::oldscape::dash3d::NXTWorldRenderer *a2,
    int a3,
    int a4,
    int a5,
    int a6,
    int a7,
    int a8,
    unsigned int a9)
{
jag::oldscape::dash3d::world *v9; // r14
__int64 result; // rax
int v13; // r12d
int v14; // r13d
int i; // eax
int v16; // ebx
int v17; // r13d
jag::oldscape::dash3d::world *v18; // r12
int v19; // r14d
__int64 v20; // rax
bool v21; // zf
bool v22; // sf
__int64 Square; // r15
int v24; // edx
unsigned __int64 v25; // rcx
const char *v26; // rdx
int v27; // r12d
unsigned int v28; // ebx
unsigned int v29; // r13d
int v30; // r15d
int v31; // ebx
__int64 v32; // r15
__int64 v33; // rbx
bool v34; // cf
int v35; // r12d
unsigned int v36; // ebx
unsigned int v37; // r13d
int v38; // r15d
int v39; // ebx
__int64 v40; // r15
__int64 v41; // rbx
__int64 v42; // [rsp+0h] [rbp-60h] BYREF
unsigned int v43; // [rsp+8h] [rbp-58h]
const char *v44; // [rsp+10h] [rbp-50h]
__int64 v45; // [rsp+18h] [rbp-48h]
__int64 v46; // [rsp+20h] [rbp-40h]
int v47; // [rsp+2Ch] [rbp-34h]
unsigned int v48; // [rsp+30h] [rbp-30h]
char v49; // [rsp+37h] [rbp-29h]

v9 = this;
if ( jag::oldscape::dash3d::g_usingRSeven )
{
jag::oldscape::dash3d::world::UpdateMousePickingRSeven(this, a3, a4, a5, a6, a7);
jag::oldscape::dash3d::RSevenModelLitRenderer::OnBeginWorldRender(
  (jag::oldscape::dash3d::RSevenModelLitRenderer *)jag::oldscape::dash3d::world::m_modelLitRendererRt7,
  this);
jag::oldscape::dash3d::world::SubmitAllModelsRt7(this, a9);
jag::oldscape::dash3d::world::SubmitDebugPointLight(this, a9);
jag::oldscape::dash3d::RSevenModelLitRenderer::OnEndWorldRender((jag::oldscape::dash3d::RSevenModelLitRenderer *)jag::oldscape::dash3d::world::m_modelLitRendererRt7);
jag::math::Vector3T<int>::Vector3T(
  &v42,
  *((unsigned int *)this + 27530),
  (unsigned int)(a3 >> 7),
  (unsigned int)(a5 >> 7));
return jag::oldscape::dash3d::MousePickingHelper::SortPickingEntities(v42, v43);
}
LODWORD(v45) = a4;
jag::oldscape::dash3d::world::m_worldRenderer = (__int64)a2;
jag::oldscape::dash3d::world::m_modelLitRenderer = (*(__int64 (__fastcall **)(jag::oldscape::dash3d::NXTWorldRenderer *))(*(_QWORD *)a2 + 8LL))(a2);
jag::oldscape::dash3d::world::CalcOcclude(this);
jag::oldscape::dash3d::world::m_fillLeft = 0;
v13 = *((_DWORD *)this + 27530);
if ( v13 < *((_DWORD *)this + 5450) )
{
v14 = a8;
do
{
  for ( i = jag::oldscape::dash3d::world::m_minX; ; i = v47 + 1 )
  {
    v47 = i;
    if ( i >= jag::oldscape::dash3d::world::m_maxX )
      break;
    v16 = jag::oldscape::dash3d::world::m_minZ;
    if ( jag::oldscape::dash3d::world::m_minZ < jag::oldscape::dash3d::world::m_maxZ )
    {
      while ( 1 )
      {
        Square = jag::oldscape::dash3d::world::GetSquare(v9, v13, v47, v16);
        if ( !(unsigned __int8)jag::operator!=<jag::oldscape::ClientObj>(Square, 0) )
          goto LABEL_12;
        if ( *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Square>::operator->(Square) + 348) <= v14
          && ((unsigned __int8)jag::oldscape::dash3d::world::GetTileVisibility(
                                 (jag::oldscape::dash3d::world *)(unsigned int)(jag::oldscape::dash3d::world::m_drawDistanceRange
                                                                              + v47
                                                                              - jag::oldscape::dash3d::world::m_gx),
                                 v16
                               + jag::oldscape::dash3d::world::m_drawDistanceRange
                               - jag::oldscape::dash3d::world::m_gz,
                                 v24)
           || (int)(jag::oldscape::dash3d::world::GetGroundHeight(v9, v13, v47, v16) - v45) > 1999) )
        {
          *(_BYTE *)(jag::shared_ptr<jag::oldscape::dash3d::Square>::operator->(Square) + 352) = 1;
          *(_BYTE *)(jag::shared_ptr<jag::oldscape::dash3d::Square>::operator->(Square) + 353) = 1;
          v17 = v13;
          v18 = v9;
          v19 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Square>::operator->(Square) + 156);
          v20 = jag::shared_ptr<jag::oldscape::dash3d::Square>::operator->(Square);
          v21 = v19 == 0;
          v22 = v19 < 0;
          v9 = v18;
          v13 = v17;
          v14 = a8;
          *(_BYTE *)(v20 + 354) = !v22 && !v21;
          ++jag::oldscape::dash3d::world::m_fillLeft;
LABEL_12:
          if ( ++v16 >= jag::oldscape::dash3d::world::m_maxZ )
            break;
        }
        else
        {
          *(_BYTE *)(jag::shared_ptr<jag::oldscape::dash3d::Square>::operator->(Square) + 352) = 0;
          *(_BYTE *)(jag::shared_ptr<jag::oldscape::dash3d::Square>::operator->(Square) + 353) = 0;
          *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Square>::operator->(Square) + 356) = 0;
          if ( ++v16 >= jag::oldscape::dash3d::world::m_maxZ )
            break;
        }
      }
    }
  }
  ++v13;
}
while ( v13 < *((_DWORD *)v9 + 5450) );
}
v25 = *((unsigned int *)v9 + 27530);
if ( (int)v25 < *((_DWORD *)v9 + 5450) )
{
while ( jag::oldscape::dash3d::world::m_drawDistanceRange < 0 )
{
  result = 14;
LABEL_56:
  if ( (_DWORD)result != 14 )
    goto LABEL_59;
LABEL_57:
  v25 = (unsigned int)(v25 + 1);
  if ( (int)v25 >= *((_DWORD *)v9 + 5450) )
    goto LABEL_58;
}
v26 = (const char *)(unsigned int)-jag::oldscape::dash3d::world::m_drawDistanceRange;
v48 = v25;
while ( 1 )
{
  v47 = jag::oldscape::dash3d::world::m_gx + (_DWORD)v26;
  if ( jag::oldscape::dash3d::world::m_gx + (int)v26 < jag::oldscape::dash3d::world::m_minX
    && jag::oldscape::dash3d::world::m_gx - (int)v26 >= jag::oldscape::dash3d::world::m_maxX )
  {
    goto LABEL_49;
  }
  v27 = jag::oldscape::dash3d::world::m_drawDistanceRange;
  result = 17;
  LOBYTE(v28) = jag::oldscape::dash3d::world::m_drawDistanceRange >= 0;
  if ( jag::oldscape::dash3d::world::m_drawDistanceRange >= 0 )
    break;
LABEL_48:
  if ( (v28 & 1) == 0 )
    goto LABEL_49;
LABEL_50:
  if ( (_DWORD)result )
    goto LABEL_56;
  v21 = (_DWORD)v26 == 0;
  v26 = (const char *)(unsigned int)((_DWORD)v26 + 1);
  if ( v21 )
    goto LABEL_57;
}
v45 = (unsigned int)(jag::oldscape::dash3d::world::m_gx - (_DWORD)v26);
v44 = v26;
v29 = -jag::oldscape::dash3d::world::m_drawDistanceRange;
LOBYTE(v28) = 1;
while ( 1 )
{
  v49 = v28;
  v30 = jag::oldscape::dash3d::world::m_gz + v29;
  v31 = v27 + jag::oldscape::dash3d::world::m_gz;
  if ( v47 >= jag::oldscape::dash3d::world::m_minX )
  {
    if ( v30 >= jag::oldscape::dash3d::world::m_minZ )
    {
      v46 = jag::oldscape::dash3d::world::GetSquare(v9, v48, v47, v30);
      if ( (unsigned __int8)jag::operator!=<jag::oldscape::ClientObj>(v46, 0) )
      {
        if ( *(_BYTE *)(jag::shared_ptr<jag::oldscape::dash3d::Square>::operator->(v46) + 352) )
          jag::oldscape::dash3d::world::Fill(v9, v46, 1, a9);
      }
    }
    if ( v31 < jag::oldscape::dash3d::world::m_maxZ )
    {
      v46 = jag::oldscape::dash3d::world::GetSquare(v9, v48, v47, v31);
      if ( (unsigned __int8)jag::operator!=<jag::oldscape::ClientObj>(v46, 0) )
      {
        if ( *(_BYTE *)(jag::shared_ptr<jag::oldscape::dash3d::Square>::operator->(v46) + 352) )
          jag::oldscape::dash3d::world::Fill(v9, v46, 1, a9);
      }
    }
  }
  if ( (int)v45 < jag::oldscape::dash3d::world::m_maxX )
  {
    if ( v30 >= jag::oldscape::dash3d::world::m_minZ )
    {
      v32 = jag::oldscape::dash3d::world::GetSquare(v9, v48, v45, v30);
      if ( (unsigned __int8)jag::operator!=<jag::oldscape::ClientObj>(v32, 0) )
      {
        if ( *(_BYTE *)(jag::shared_ptr<jag::oldscape::dash3d::Square>::operator->(v32) + 352) )
          jag::oldscape::dash3d::world::Fill(v9, v32, 1, a9);
      }
    }
    if ( v31 < jag::oldscape::dash3d::world::m_maxZ )
    {
      v33 = jag::oldscape::dash3d::world::GetSquare(v9, v48, v45, v31);
      if ( (unsigned __int8)jag::operator!=<jag::oldscape::ClientObj>(v33, 0) )
      {
        if ( *(_BYTE *)(jag::shared_ptr<jag::oldscape::dash3d::Square>::operator->(v33) + 352) )
          jag::oldscape::dash3d::world::Fill(v9, v33, 1, a9);
      }
    }
  }
  if ( !jag::oldscape::dash3d::world::m_fillLeft )
    break;
  v28 = v29++ >> 31;
  v34 = v27-- != 0;
  if ( !v34 )
  {
    v25 = v48;
    LODWORD(v26) = (_DWORD)v44;
    result = 17;
    goto LABEL_48;
  }
}
jag::oldscape::dash3d::world::m_click = 0;
result = 1;
v25 = v48;
LODWORD(v26) = (_DWORD)v44;
if ( (v49 & 1) != 0 )
  goto LABEL_50;
LABEL_49:
result = 0;
goto LABEL_50;
}
LABEL_58:
result = 11;
LABEL_59:
if ( (_DWORD)result == 11 )
{
v48 = *((_DWORD *)v9 + 27530);
if ( (signed int)v48 < *((_DWORD *)v9 + 5450) )
{
  while ( 1 )
  {
    v25 = (unsigned int)jag::oldscape::dash3d::world::m_drawDistanceRange;
    if ( jag::oldscape::dash3d::world::m_drawDistanceRange >= 0 )
      break;
    result = 23;
LABEL_96:
    if ( (_DWORD)result != 23 )
      goto LABEL_99;
LABEL_97:
    if ( (signed int)++v48 >= *((_DWORD *)v9 + 5450) )
      goto LABEL_98;
  }
  v25 = (unsigned int)-jag::oldscape::dash3d::world::m_drawDistanceRange;
  while ( 1 )
  {
    v47 = jag::oldscape::dash3d::world::m_gx + v25;
    if ( jag::oldscape::dash3d::world::m_gx + (int)v25 < jag::oldscape::dash3d::world::m_minX
      && jag::oldscape::dash3d::world::m_gx - (int)v25 >= jag::oldscape::dash3d::world::m_maxX )
    {
      goto LABEL_89;
    }
    v35 = jag::oldscape::dash3d::world::m_drawDistanceRange;
    result = 26;
    LOBYTE(v36) = jag::oldscape::dash3d::world::m_drawDistanceRange >= 0;
    if ( jag::oldscape::dash3d::world::m_drawDistanceRange >= 0 )
      break;
LABEL_88:
    if ( (v36 & 1) == 0 )
      goto LABEL_89;
LABEL_90:
    if ( (_DWORD)result )
      goto LABEL_96;
    v21 = (_DWORD)v25 == 0;
    v25 = (unsigned int)(v25 + 1);
    if ( v21 )
      goto LABEL_97;
  }
  v45 = (unsigned int)(jag::oldscape::dash3d::world::m_gx - v25);
  v44 = (const char *)v25;
  v37 = -jag::oldscape::dash3d::world::m_drawDistanceRange;
  LOBYTE(v36) = 1;
  while ( 1 )
  {
    v49 = v36;
    v38 = jag::oldscape::dash3d::world::m_gz + v37;
    v39 = v35 + jag::oldscape::dash3d::world::m_gz;
    if ( v47 >= jag::oldscape::dash3d::world::m_minX )
    {
      if ( v38 >= jag::oldscape::dash3d::world::m_minZ )
      {
        v46 = jag::oldscape::dash3d::world::GetSquare(v9, v48, v47, v38);
        if ( (unsigned __int8)jag::operator!=<jag::oldscape::ClientObj>(v46, 0) )
        {
          if ( *(_BYTE *)(jag::shared_ptr<jag::oldscape::dash3d::Square>::operator->(v46) + 352) )
            jag::oldscape::dash3d::world::Fill(v9, v46, 0, a9);
        }
      }
      if ( v39 < jag::oldscape::dash3d::world::m_maxZ )
      {
        v46 = jag::oldscape::dash3d::world::GetSquare(v9, v48, v47, v39);
        if ( (unsigned __int8)jag::operator!=<jag::oldscape::ClientObj>(v46, 0) )
        {
          if ( *(_BYTE *)(jag::shared_ptr<jag::oldscape::dash3d::Square>::operator->(v46) + 352) )
            jag::oldscape::dash3d::world::Fill(v9, v46, 0, a9);
        }
      }
    }
    if ( (int)v45 < jag::oldscape::dash3d::world::m_maxX )
    {
      if ( v38 >= jag::oldscape::dash3d::world::m_minZ )
      {
        v40 = jag::oldscape::dash3d::world::GetSquare(v9, v48, v45, v38);
        if ( (unsigned __int8)jag::operator!=<jag::oldscape::ClientObj>(v40, 0) )
        {
          if ( *(_BYTE *)(jag::shared_ptr<jag::oldscape::dash3d::Square>::operator->(v40) + 352) )
            jag::oldscape::dash3d::world::Fill(v9, v40, 0, a9);
        }
      }
      if ( v39 < jag::oldscape::dash3d::world::m_maxZ )
      {
        v41 = jag::oldscape::dash3d::world::GetSquare(v9, v48, v45, v39);
        if ( (unsigned __int8)jag::operator!=<jag::oldscape::ClientObj>(v41, 0) )
        {
          if ( *(_BYTE *)(jag::shared_ptr<jag::oldscape::dash3d::Square>::operator->(v41) + 352) )
            jag::oldscape::dash3d::world::Fill(v9, v41, 0, a9);
        }
      }
    }
    if ( !jag::oldscape::dash3d::world::m_fillLeft )
      break;
    v36 = v37++ >> 31;
    v34 = v35-- != 0;
    if ( !v34 )
    {
      v25 = (unsigned __int64)v44;
      result = 26;
      goto LABEL_88;
    }
  }
  jag::oldscape::dash3d::world::m_click = 0;
  result = 1;
  v25 = (unsigned __int64)v44;
  if ( (v49 & 1) != 0 )
    goto LABEL_90;
LABEL_89:
  result = 0;
  goto LABEL_90;
}
LABEL_98:
result = 20;
LABEL_99:
if ( (_DWORD)result == 20 )
{
  jag::LogAssertAlways(
    (jag *)"/Users/bamboo/bamboo-home-01/xml-data/build-dir/OSRS-OS1669-BOXR/libs/oldscape/jagex3/dash3d/World.cpp",
    (const char *)0x807,
    (unsigned int)"Render::Fill failure, some squares won't be rendered.",
    (const char *)v25);
  __debugbreak();
}
}
return result;
}