__int64 __fastcall jag::game::SceneRenderManager::RenderViewImpl(
        jag::graphics::RT7Core **a1,
        jag::render::RenderTargets *a2,
        jag::graphics::RT7Core **a3,
        jag::game::World *a4,
        const char *a5,
        int a6,
        __int64 a7,
        jag::graphics::SkyBox *a8,
        __int64 a9,
        char a10,
        __int64 a11,
        _DWORD *a12,
        __m128 *a13,
        int a14,
        unsigned int a15)
{
  __int64 CentreSquare; // rax
  __int64 HeapSize; // rax
  jag::render::RenderTargets *v19; // r13
  __int64 ShadowBufferRT; // rax
  bool v21; // al
  __int64 ColourTargetTextures; // rax
  _QWORD *v23; // rax
  __int64 v24; // rax
  unsigned __int8 HasVisibleSkyTexture; // al
  int v26; // ebx
  int SceneGraphRoot; // eax
  _BYTE *v28; // rcx
  __int64 v29; // r15
  __int64 v30; // r13
  __int64 v31; // rax
  _QWORD *v32; // rax
  int v33; // ebx
  __int64 v34; // rax
  _QWORD *v35; // rax
  __int64 v36; // rax
  jag::graphics::GraphRoot *v37; // rax
  __int64 Root; // rax
  __int64 GeometryPass; // rax
  jag::graphics::RenderPass *v40; // rax
  jag::graphics::RenderPass *ForwardLightingPass; // rax
  jag::graphics::SceneRenderPass *v42; // rax
  bool v43; // bl
  jag::graphics::SceneRenderPass *v44; // rax
  __int64 v45; // r9
  __int64 v46; // rdx
  __int64 v47; // r9
  __int64 v48; // r9
  int v49; // ebx
  __int64 ClearColour; // rax
  int v51; // edx
  __int64 v52; // rax
  __int64 v53; // rdx
  __int64 v54; // r8
  __int64 v55; // r15
  jag::graphics::RenderPass *DeferredShadowPass; // rax
  __int64 v57; // rax
  unsigned __int8 v58; // bl
  char IsEnabled; // al
  __m128 v60; // xmm1
  __m128 v61; // xmm0
  __int64 v62; // rdi
  bool v64; // r13
  __int64 DepthStencilTargetTexture; // rax
  int DepthTextureClearFlags; // eax
  char v67; // r12
  jag::graphics::RenderPass *SSAOPass; // rax
  jag::graphics::RenderPass *v69; // rax
  __int64 v70; // rax
  jag::graphics::RT7Core **v71; // r14
  int v72; // r15d
  unsigned int v73; // ebx
  int v74; // r13d
  jag::graphics::RT7Core **v75; // r14
  jag::graphics::RenderPass *ObjectHighlightPass; // rax
  unsigned __int8 v77; // bl
  jag::graphics::RenderPass *v78; // rax
  jag::graphics::RenderPass *QueryPass; // rax
  __int64 v80; // rax
  __int64 v81; // rax
  __int64 v82; // rax
  __int64 ViewFrustum; // rax
  __int64 v84; // rax
  __int64 EndValue; // rax
  __m128 v87; // [rsp+20h] [rbp-3E0h] BYREF
  _DWORD v88[6]; // [rsp+38h] [rbp-3C8h] BYREF
  jag::render::RenderTargets *v89; // [rsp+50h] [rbp-3B0h]
  jag::graphics::RT7Core **v90; // [rsp+58h] [rbp-3A8h]
  __m128 v91; // [rsp+60h] [rbp-3A0h] BYREF
  jag::graphics::RT7Core **v92; // [rsp+78h] [rbp-388h]
  __int64 v93; // [rsp+80h] [rbp-380h]
  unsigned int v94; // [rsp+88h] [rbp-378h]
  char v95; // [rsp+8Dh] [rbp-373h] BYREF
  bool v96; // [rsp+8Eh] [rbp-372h] BYREF
  bool v97; // [rsp+8Fh] [rbp-371h]
  jag::render::RenderTargets *v98[2]; // [rsp+90h] [rbp-370h] BYREF
  _BYTE v99[32]; // [rsp+A0h] [rbp-360h] BYREF
  int v100; // [rsp+C0h] [rbp-340h]
  _BYTE v101[88]; // [rsp+D8h] [rbp-328h] BYREF
  _BYTE v102[208]; // [rsp+130h] [rbp-2D0h] BYREF
  _BYTE v103[224]; // [rsp+200h] [rbp-200h] BYREF
  char v104[192]; // [rsp+2E0h] [rbp-120h] BYREF
  char v105; // [rsp+3A0h] [rbp-60h]

  LODWORD(v93) = a6;
  v94 = (unsigned int)a5;
  v92 = a3;
  if ( a4 )
  {
    CentreSquare = jag::game::World::GetCentreSquare(a4);
    if ( !(unsigned __int8)jag::shared_ptr<jag::oldscape::rs2lib::worldmap::MapElement>::operator bool(CentreSquare) )
    {
      jag::LogAssert(
        (jag *)"!world || world->GetCentreSquare()",
        "/Users/bamboo/bamboo-home-01/xml-data/build-dir/OSRS-OS1669-BOXR/libs/game/runetek5/scene/SceneRenderManager.cpp",
        (const char *)0xBB,
        0,
        a5);
      __debugbreak();
    }
  }
  if ( !a2 )
  {
    jag::LogAssert(
      (jag *)"pDstRT != nullptr",
      "/Users/bamboo/bamboo-home-01/xml-data/build-dir/OSRS-OS1669-BOXR/libs/game/runetek5/scene/SceneRenderManager.cpp",
      (const char *)0xBC,
      0,
      a5);
    __debugbreak();
  }
  v96 = 0;
  v95 = 0;
  HeapSize = eastl::basic_string<char,eastl::allocator>::Layout::GetHeapSize(v92);
  jag::game::SceneRenderManager::SetupGeometryPass(a1, &v96, &v95, a2, HeapSize, a7);
  jag::graphics::SceneViewData::SceneViewData((jag::graphics::SceneViewData *)v102);
  eastl::fixed_vector<jag::render::Viewport,2ul,true,eastl::allocator>::fixed_vector(v101);
  eastl::fixed_vector<jag::render::Viewport,2ul,true,eastl::allocator>::push_back(v101, a12);
  jag::graphics::SceneViewData::SetRenderTargetViewports(v102, v101);
  jag::graphics::SceneViewData::SetTransforms((jag::oldscape::rs2lib::InputField *)v102);
  v89 = a2;
  jag::graphics::SceneViewData::SetOcclusionTestEnabled((jag::graphics::SceneViewData *)v102, (a15 & 1) == 0);
  jag::graphics::SceneViewData::SetMinOcclusionDrawableRadius((jag::graphics::SceneViewData *)v102, 100.0);
  jag::graphics::SceneViewData::SetMinOcclusionDrawableAxisLength((jag::graphics::SceneViewData *)v102, 200.0);
  jag::graphics::SceneViewData::SetStippleNearClipParams(
    (jag::graphics::SceneViewData *)v102,
    jag::game::SceneRenderManager::m_stencilTransparencyNearClipThreshold,
    jag::game::SceneRenderManager::m_stencilTransparencyNearClipRange);
  jag::graphics::SceneViewData::SetStippleFarClipParams(
    (jag::graphics::SceneViewData *)v102,
    jag::game::SceneRenderManager::m_stencilTransparencyFarClipThreshold,
    jag::game::SceneRenderManager::m_stencilTransparencyFarClipRange);
  v105 = a10;
  v19 = a2;
  jag::graphics::SceneViewData::SetRenderFlags((jag::graphics::SceneViewData *)v102, a15);
  v97 = v96;
  if ( v96 )
  {
    if ( jag::game::SceneRenderManager::m_enableDeferredShadows )
    {
      ShadowBufferRT = jag::graphics::RT7Core::GetShadowBufferRT(a1[1]);
      v21 = eastl::basic_string<char,eastl::allocator>::Layout::GetHeapSize(ShadowBufferRT) != 0;
    }
    else
    {
      v21 = 0;
    }
    jag::graphics::EffectManager::EnableEffect(a7, 28, v21);
  }
  ColourTargetTextures = jag::render::RenderTargets::GetColourTargetTextures(v89);
  v23 = (_QWORD *)eastl::vector<jag::render::Texture *,eastl::fixed_vector_allocator<8ul,2ul,8ul,0ul,true,eastl::allocator>>::operator[](
                    ColourTargetTextures,
                    0);
  v24 = eastl::compressed_pair_imp<eastl::basic_string<char,eastl::allocator>::Layout,eastl::allocator,0>::second(*v23);
  jag::graphics::EffectManager::EnableEffect(a7, 89, *(_DWORD *)(v24 + 16) >= 2u);
  if ( a8 )
    HasVisibleSkyTexture = jag::graphics::SkyBox::HasVisibleSkyTexture(a8);
  else
    HasVisibleSkyTexture = 0;
  jag::graphics::EffectManager::EnableEffect(a7, 85, HasVisibleSkyTexture);
  v90 = a1 + 2;
  eastl::vector<jag::oldscape::rs2lib::CoordMapSquare,eastl::fixed_vector_allocator<8ul,20ul,4ul,0ul,true,eastl::allocator>>::clear();
  if ( a4 )
  {
    v26 = 1;
    if ( (_DWORD)v93 != -1 )
      v26 = v93;
    SceneGraphRoot = jag::game::World::GetSceneGraphRoot(a4);
    v28 = v103;
    if ( jag::game::SceneRenderManager::m_freezeCulling )
      LODWORD(v28) = (_DWORD)a1 + 14960;
    jag::graphics::GraphRoot::VisibilityTest(
      SceneGraphRoot,
      (_DWORD)v90,
      v94,
      (_DWORD)v28,
      v26,
      v26,
      (unsigned __int64)(a1 + 1634),
      0);
  }
  if ( (unsigned __int8)jag::graphics::EffectManager::IsEnabled(a7, 26) )
  {
    if ( a4 )
    {
      jag::game::SceneRenderManager::GetVisiblePointLights(a1, a7, v102, a4);
    }
    else if ( a9 )
    {
      jag::game::SceneRenderManager::GetVisiblePointLights(a1, a7, v102);
    }
  }
  if ( (unsigned __int8)jag::graphics::EffectManager::IsEnabled(a7, 106) )
  {
    v29 = jag::graphics::EffectManager::GetEffect<jag::graphics::LightScatteringEffect>(a7, 106);
    v30 = eastl::vector<jag::render::Viewport,eastl::fixed_vector_allocator<24ul,2ul,4ul,0ul,true,eastl::allocator>>::operator[](
            v104,
            0);
    v31 = jag::render::RenderTargets::GetColourTargetTextures(v89);
    v32 = (_QWORD *)eastl::vector<jag::render::Texture *,eastl::fixed_vector_allocator<8ul,2ul,8ul,0ul,true,eastl::allocator>>::operator[](
                      v31,
                      0);
    v33 = *(_DWORD *)eastl::compressed_pair_imp<eastl::basic_string<char,eastl::allocator>::Layout,eastl::allocator,0>::second(*v32);
    v34 = jag::render::RenderTargets::GetColourTargetTextures(v89);
    v35 = (_QWORD *)eastl::vector<jag::render::Texture *,eastl::fixed_vector_allocator<8ul,2ul,8ul,0ul,true,eastl::allocator>>::operator[](
                      v34,
                      0);
    *(float *)&v93 = (float)v33;
    v36 = eastl::compressed_pair_imp<eastl::basic_string<char,eastl::allocator>::Layout,eastl::allocator,0>::second(*v35);
    jag::math::Range<float>::Range(v98, (float)v33, (float)*(int *)(v36 + 4));
    jag::graphics::ViewportLookupScale::SetViewportLookupScale(v29, v30, v98);
    v19 = v89;
  }
  if ( (a15 & 8) == 0 )
  {
    if ( a4 )
      jag::game::World::GetPlayerCameraOffsetScale(a4);
    jag::game::SceneRenderManager::UpdateSunlightShadows(a1, a7, v102);
    if ( a4 )
    {
      jag::game::SceneRenderManager::UpdateWaterReflection(a1, a7, v102, a4);
      jag::game::SceneRenderManager::UpdateWaterCaustics(a1, a7, v102);
    }
  }
  jag::game::SceneRenderManager::UpdateRefraction(a1, a7, a12);
  jag::game::SceneRenderManager::UpdateSkyBoxes(a1, a7, v102);
  jag::game::SceneRenderManager::UpdateViewportMap(a1, a7);
  if ( a4 )
  {
    v37 = (jag::graphics::GraphRoot *)jag::game::World::GetSceneGraphRoot(a4);
    Root = jag::graphics::GraphRoot::GetRoot(v37);
  }
  else
  {
    Root = 0;
  }
  v93 = Root;
  if ( v97 )
  {
    GeometryPass = jag::graphics::RT7Core::GetGeometryPass(a1[1]);
    jag::graphics::GeometryPass::SetGBufferRT(GeometryPass, v92);
    v40 = (jag::graphics::RenderPass *)jag::graphics::RT7Core::GetGeometryPass(a1[1]);
    jag::graphics::SceneViewData::RenderPassPushBack((jag::graphics::SceneViewData *)v102, v40);
    ForwardLightingPass = (jag::graphics::RenderPass *)jag::graphics::RT7Core::GetForwardLightingPass(a1[1]);
    jag::graphics::SceneViewData::RenderPassPushBack((jag::graphics::SceneViewData *)v102, ForwardLightingPass);
    jag::Error::Error((jag::Error *)v98);
    jag::graphics::RenderPassBitSet::AddRenderPass((jag::graphics::RenderPassBitSet *)v98, 3u);
    jag::graphics::Renderer::RenderView((_DWORD)a1 + 6408, (_DWORD)v90, (unsigned int)v102, v93, a7, 1, (__int64)v98);
    jag::graphics::SceneViewData::RenderPassPopBack((jag::graphics::SceneViewData *)v102);
    jag::graphics::SceneViewData::RenderPassPopBack((jag::graphics::SceneViewData *)v102);
  }
  if ( (a15 & 1) == 0 )
  {
    v42 = (jag::graphics::SceneRenderPass *)jag::graphics::RT7Core::GetGeometryPass(a1[1]);
    jag::graphics::SceneRenderPass::SetOcclusionTestEnabled(v42, v97);
    v43 = !v97;
    v44 = (jag::graphics::SceneRenderPass *)jag::graphics::RT7Core::GetForwardLightingPass(a1[1]);
    jag::graphics::SceneRenderPass::SetOcclusionTestEnabled(v44, v43);
  }
  if ( (a15 & 8) == 0 )
  {
    if ( a4 )
      v45 = jag::game::World::GetSceneGraphRoot(a4);
    else
      v45 = 0;
    jag::game::SceneRenderManager::RenderSkyBoxes(a1, v94, 0, a7, v102, v45);
    if ( a4 )
      v47 = jag::game::World::GetSceneGraphRoot(a4);
    else
      v47 = 0;
    jag::game::SceneRenderManager::RenderSunlightShadows(a1, v94, v46, a7, v102, v47);
    if ( a4 )
      v48 = jag::game::World::GetSceneGraphRoot(a4);
    else
      v48 = 0;
    jag::game::SceneRenderManager::RenderPointLightShadows(a1, v94, 0, a7, v102, v48);
    if ( a4 )
    {
      v49 = jag::game::World::GetSceneGraphRoot(a4);
      ClearColour = jag::game::World::GetClearColour(a4);
      jag::game::SceneRenderManager::RenderWaterReflection(
        (_DWORD)a1,
        v94,
        v51,
        a7,
        (unsigned int)v102,
        v49,
        ClearColour);
      v52 = jag::game::World::GetSceneGraphRoot(a4);
      jag::game::SceneRenderManager::RenderWaterCaustics(a1, v94, v53, a7, v54, v52);
    }
    if ( v97 )
    {
      jag::graphics::ClippingState::ClippingState((jag::graphics::ClippingState *)v98, a1[1]);
      v55 = jag::graphics::EffectManager::GetEffect<jag::graphics::LightScatteringEffect>(a7, 28);
      if ( (unsigned __int8)jag::graphics::EffectManager::IsEnabled(a7, 28) )
      {
        DeferredShadowPass = (jag::graphics::RenderPass *)jag::graphics::RT7Core::GetDeferredShadowPass(a1[1]);
        jag::graphics::SceneViewData::RenderPassPushBack((jag::graphics::SceneViewData *)v102, DeferredShadowPass);
        v57 = jag::graphics::RT7Core::GetShadowBufferRT(a1[1]);
        jag::graphics::DeferredShadows::SetShadowBufferRT(v55, v57);
      }
      else
      {
        jag::weak_ptr<jag::oldscape::rs2lib::IfType>::weak_ptr(v88);
        jag::graphics::DeferredShadows::SetShadowBufferRT(v55, v88);
        jag::shared_ptr<jag::oldscape::FileRSevenDebugPresetProvider>::~shared_ptr(v88);
      }
      v58 = (jag::game::SceneRenderManager::m_debugRender != 0) | ((unsigned __int8)(a15 & 2) >> 1);
      v88[0] = 0;
      jag::Error::Error((jag::Error *)v88);
      jag::graphics::Renderer::RenderView(a1 + 801, v102, a7, v58, v88);
      IsEnabled = jag::graphics::EffectManager::IsEnabled(a7, 28);
      v19 = v89;
      if ( IsEnabled )
        jag::graphics::SceneViewData::RenderPassPopBack((jag::graphics::SceneViewData *)v102);
      jag::graphics::ClippingState::~ClippingState((jag::graphics::ClippingState *)v98);
    }
  }
  jag::graphics::ClippingState::ClippingState((jag::graphics::ClippingState *)v88, a1[1]);
  v60 = 0;
  jag::math::Vector4T<float>::Vector4T(&v91, 0.0, 0.0, 0.0, COERCE_DOUBLE(1065353216));
  jag::render::RenderTargetData::RenderTargetData(v98, 0, &v91, 1, 0, 1.0);
  v98[0] = v19;
  v100 = a14;
  if ( a8 )
  {
    if ( a4 )
    {
      v61 = *(__m128 *)jag::game::World::GetClearColour(a4);
      v87 = v61;
    }
    else
    {
      v61 = 0;
      v60 = 0;
      jag::math::Vector4T<float>::Vector4T(&v87, 0.0, 0.0, 0.0, COERCE_DOUBLE(1065353216));
    }
    *(double *)v61.m128_u64 = jag::graphics::SkyBox::GetDesiredClearColour(a8, &v87);
    v91 = _mm_movelh_ps(v61, v60);
    a13 = &v91;
    v62 = std::array<jag::math::Vector4T<float>,2ul>::operator[](v99);
  }
  else
  {
    v62 = std::array<jag::math::Vector4T<float>,2ul>::operator[](v99);
  }
  jag::math::QuaternionT<float>::operator=(v62, a13);
  v64 = v96;
  if ( v96 )
  {
    v105 = 1;
  }
  else
  {
    DepthStencilTargetTexture = jag::render::RenderTargets::GetDepthStencilTargetTexture(v98[0]);
    DepthTextureClearFlags = jag::render::GetDepthTextureClearFlags(DepthStencilTargetTexture);
    v100 |= DepthTextureClearFlags;
  }
  jag::graphics::SceneViewData::SetRenderTargets(v102, v98);
  v67 = v95;
  if ( v95 )
  {
    SSAOPass = (jag::graphics::RenderPass *)jag::graphics::RT7Core::GetSSAOPass(a1[1]);
    jag::graphics::SceneViewData::RenderPassPushBack((jag::graphics::SceneViewData *)v102, SSAOPass);
  }
  v69 = (jag::graphics::RenderPass *)jag::graphics::RT7Core::GetForwardLightingPass(a1[1]);
  jag::graphics::SceneViewData::RenderPassPushBack((jag::graphics::SceneViewData *)v102, v69);
  v70 = jag::graphics::RT7Core::GetSSAOPass(a1[1]);
  if ( !v64 )
    v92 = (jag::graphics::RT7Core **)&jag::render::RenderTargets::NullRenderTargets;
  jag::graphics::SSAOPass::SetGBufferRT(v70, v92);
  jag::graphics::RT7Core::SetClippingRectangle(a1[1], *a12, a12[1], a12[2], a12[3], 1);
  v92 = a1;
  v71 = a1 + 801;
  v72 = (unsigned __int8)(a15 & 2) >> 1;
  v73 = (jag::game::SceneRenderManager::m_debugRender != 0) | ((unsigned __int8)(a15 & 2) >> 1);
  v91.m128_i32[0] = 0;
  jag::Error::Error((jag::Error *)&v91);
  if ( v64 )
  {
    v74 = (int)v71;
    jag::graphics::Renderer::RenderView(v71, v102, a7, v73, &v91);
  }
  else
  {
    jag::graphics::Renderer::RenderView((_DWORD)v71, (_DWORD)v90, (unsigned int)v102, v93, a7, v73, (__int64)&v91);
    v74 = (int)v71;
  }
  v75 = v92;
  jag::graphics::SceneViewData::RenderPassPopBack((jag::graphics::SceneViewData *)v102);
  if ( v67 )
    jag::graphics::SceneViewData::RenderPassPopBack((jag::graphics::SceneViewData *)v102);
  if ( (a15 & 8) == 0 )
  {
    if ( (a15 & 0x80u) == 0 )
    {
      ObjectHighlightPass = (jag::graphics::RenderPass *)jag::graphics::RT7Core::GetObjectHighlightPass(v75[1]);
      jag::graphics::SceneViewData::RenderPassPushBack((jag::graphics::SceneViewData *)v102, ObjectHighlightPass);
      v77 = v72 | (jag::game::SceneRenderManager::m_debugRender != 0);
      v91.m128_i32[0] = 0;
      jag::Error::Error((jag::Error *)&v91);
      jag::graphics::Renderer::RenderView(v74, (_DWORD)v90, (unsigned int)v102, v93, a7, v77, (__int64)&v91);
      jag::graphics::SceneViewData::RenderPassPopBack((jag::graphics::SceneViewData *)v102);
    }
    if ( (a15 & 0x40) == 0 )
    {
      v78 = (jag::graphics::RenderPass *)jag::graphics::RT7Core::GetForwardLightingPass(v75[1]);
      jag::graphics::SceneViewData::RenderPassPushBack((jag::graphics::SceneViewData *)v102, v78);
      QueryPass = (jag::graphics::RenderPass *)jag::graphics::RT7Core::GetQueryPass(v75[1]);
      jag::graphics::SceneViewData::RenderPassPushBack((jag::graphics::SceneViewData *)v102, QueryPass);
      jag::Error::Error((jag::Error *)&v91);
      jag::graphics::RenderPassBitSet::AddRenderPass((jag::graphics::RenderPassBitSet *)&v91, 9u);
      jag::graphics::Renderer::RenderView(
        v74,
        (_DWORD)v90,
        (unsigned int)v102,
        v93,
        a7,
        (unsigned __int8)v72 | (jag::game::SceneRenderManager::m_debugRender != 0),
        (__int64)&v91);
      jag::graphics::SceneViewData::RenderPassPopBack((jag::graphics::SceneViewData *)v102);
      jag::graphics::SceneViewData::RenderPassPopBack((jag::graphics::SceneViewData *)v102);
    }
  }
  v80 = jag::graphics::RT7Core::GetGeometryPass(v75[1]);
  jag::graphics::GeometryPass::SetGBufferRT(v80, &jag::render::RenderTargets::NullRenderTargets);
  v81 = jag::graphics::RT7Core::GetSSAOPass(v75[1]);
  jag::graphics::SSAOPass::SetGBufferRT(v81, &jag::render::RenderTargets::NullRenderTargets);
  jag::game::SceneRenderManager::UnregisterRenderTargets(v75, a7);
  v82 = eastl::compressed_pair_imp<eastl::basic_string<char,eastl::allocator>::Layout,eastl::allocator,0>::first(v102);
  jag::ViewTransforms::operator=(v75 + 1816, v82);
  ViewFrustum = jag::graphics::SceneViewData::GetViewFrustum((jag::graphics::SceneViewData *)v102);
  jag::math::Frustum::operator=(v75 + 1842, ViewFrustum);
  v84 = eastl::compressed_pair_imp<eastl::basic_string<char,eastl::allocator>::Layout,eastl::allocator,0>::first(v102);
  EndValue = jag::math::Transitionable<jag::graphics::IrradianceSHCoefConsts>::GetEndValue(v84);
  jag::math::Matrix44T<float>::operator=(v75 + 1862, EndValue);
  jag::math::Matrix44T<float>::Invert(v75 + 1862, 0);
  if ( !jag::game::SceneRenderManager::m_freezeCulling )
    jag::math::Frustum::operator=(v75 + 1870, v103);
  jag::graphics::ClippingState::~ClippingState((jag::graphics::ClippingState *)v88);
  eastl::fixed_vector<jag::render::Viewport,2ul,true,eastl::allocator>::~fixed_vector(v101);
  jag::graphics::SceneViewData::~SceneViewData((jag::graphics::SceneViewData *)v102);
  return __stack_chk_guard;
}