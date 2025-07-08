_BYTE* __fastcall jag::oldscape::dash3d::NXTWorldRenderer::RenderQuickGround(
    jag::oldscape::dash3d::NXTWorldRenderer* this,
    const jag::oldscape::dash3d::world* a2,
    int a3,
    int a4,
    int a5,
    int a6,
    int a7,
    int a8,
    int a9,
    unsigned int a10,
    int a11,
    int a12,
    int a13,
    int a14,
    int a15)
{
    _BYTE* result;                        // rax
    _DWORD* v17;                          // rdi
    int v18;                              // eax
    int v19;                              // r11d
    int v20;                              // r15d
    __int64 v21;                          // rax
    unsigned __int64 v22;                 // rdi
    int v23;                              // r12d
    int v24;                              // ebx
    int v25;                              // r13d
    int v26;                              // r14d
    jag::oldscape::dash3d::NXTPix3D* v27; // rsi
    int v28;                              // ecx
    unsigned int MouseScreenX;            // r15d
    int MouseScreenY;                     // eax
    char v31;                             // al
    int v32;                              // r14d
    __int64 v33;                          // rax
    unsigned int v34;                     // ebx
    int v35;                              // r15d
    int v36;                              // r12d
    int v37;                              // edx
    __int16 v38;                          // r13
    int v39;                              // edx
    __int16 v40;                          // r14
    int v41;                              // edx
    __int16 v42;                          // ax
    int v43;                              // r15d
    int v44;                              // r12d
    int v45;                              // r13d
    __int64 v46;                          // rax
    jag::oldscape::dash3d::NXTPix3D* v47; // rdx
    int v48;                              // r14d
    int v49;                              // ecx
    unsigned int v50;                     // ebx
    int v51;                              // eax
    char v52;                             // al
    int v53;                              // r14d
    __int64 v54;                          // rax
    unsigned int v55;                     // ebx
    int v56;                              // r13d
    int v57;                              // r15d
    int v58;                              // edx
    __int16 v59;                          // r12
    int v60;                              // edx
    __int16 v61;                          // r14
    int v62;                              // edx
    __int16 v63;                          // ax
    int v64;                              // [rsp-8h] [rbp-B8h]
    int v65;                              // [rsp-8h] [rbp-B8h]
    int v66;                              // [rsp+0h] [rbp-B0h]
    int v67;                              // [rsp+0h] [rbp-B0h]
    int v68;                              // [rsp+8h] [rbp-A8h]
    unsigned int v69;                     // [rsp+Ch] [rbp-A4h]
    _BYTE v70[4];                         // [rsp+10h] [rbp-A0h] BYREF
    int v71;                              // [rsp+14h] [rbp-9Ch]
    __int16 v72[2];                       // [rsp+18h] [rbp-98h]
    int v73;                              // [rsp+1Ch] [rbp-94h]
    __int16 v74[2];                       // [rsp+20h] [rbp-90h]
    int v75;                              // [rsp+24h] [rbp-8Ch]
    int v76;                              // [rsp+28h] [rbp-88h]
    int v77;                              // [rsp+2Ch] [rbp-84h]
    int v78;                              // [rsp+30h] [rbp-80h]
    __int16 v79[2];                       // [rsp+34h] [rbp-7Ch]
    __int16 v80[2];                       // [rsp+38h] [rbp-78h]
    __int16 v81[2];                       // [rsp+3Ch] [rbp-74h]
    __int16 v82[2];                       // [rsp+40h] [rbp-70h]
    int v83;                              // [rsp+44h] [rbp-6Ch]
    int v84;                              // [rsp+48h] [rbp-68h]
    int v85;                              // [rsp+4Ch] [rbp-64h]
    unsigned int v86;                     // [rsp+50h] [rbp-60h]
    int v87;                              // [rsp+54h] [rbp-5Ch]
    int v88;                              // [rsp+58h] [rbp-58h]
    int v89;                              // [rsp+5Ch] [rbp-54h]
    int v90;                              // [rsp+60h] [rbp-50h]
    int v91;                              // [rsp+64h] [rbp-4Ch]
    jag::oldscape::dash3d::NXTPix3D* v92; // [rsp+68h] [rbp-48h]
    jag::oldscape::dash3d::NXTPix3D* v93; // [rsp+70h] [rbp-40h]
    int v94;                              // [rsp+7Ch] [rbp-34h]
    __int16 v95[24];                      // [rsp+80h] [rbp-30h]

    result = jag::oldscape::dash3d::WorldRenderer_renderGround;
    if( jag::oldscape::dash3d::WorldRenderer_renderGround[0] )
    {
        if( a15 )
        {
            v17 = (_DWORD*)*((_QWORD*)this + 1);
            v18 = v17[4];
            v19 = v17[5];
            v20 = v17[6];
            v94 = a3;
            result = (_BYTE*)jag::oldscape::dash3d::CalculateGroundProjectionPointsGrid(
                (jag::oldscape::dash3d*)v70, a2, a3, a4, a5, a6, a7, a8, a9, v19, v20, v18);
            if( v70[0] )
            {
                *(_QWORD*)v95 = this;
                v21 = *((_QWORD*)this + 1);
                *(_DWORD*)(v21 + 12) = a15;
                v22 = v86;
                v23 = v87;
                v24 = v90;
                v25 = v88;
                if( (int)((v87 - v89) * (v86 - v90) + (v88 - v90) * (v89 - v85)) > 0 )
                {
                    LODWORD(v92) = v86;
                    *(_BYTE*)(v21 + 8) = 0;
                    v26 = v85;
                    v27 = (jag::oldscape::dash3d::NXTPix3D*)(unsigned int)v89;
                    if( v23 < 0 || v89 < 0 || v85 < 0 || (v28 = *(_DWORD*)(v21 + 28), v23 > v28) ||
                        v89 > v28 || v85 > v28 )
                        *(_BYTE*)(v21 + 8) = 1;
                    v93 = v27;
                    MouseScreenX = jag::oldscape::dash3d::MousePickingHelper::GetMouseScreenX(
                        (jag::oldscape::dash3d::MousePickingHelper*)v22);
                    MouseScreenY = jag::oldscape::dash3d::MousePickingHelper::GetMouseScreenY(
                        (jag::oldscape::dash3d::MousePickingHelper*)v22);
                    v22 = MouseScreenX;
                    v31 = jag::oldscape::dash3d::world::InsideTriangle(
                        (jag::oldscape::dash3d::world*)MouseScreenX,
                        MouseScreenY,
                        v25,
                        v24,
                        (int)v92,
                        v23,
                        (int)v93,
                        v26,
                        v66);
                    v32 = v94;
                    if( v31 )
                    {
                        jag::oldscape::dash3d::world::m_mouseoverGroundX = a8;
                        jag::oldscape::dash3d::world::m_mouseoverGroundZ = a9;
                        jag::oldscape::dash3d::world::m_mouseoverGroundY = v94;
                    }
                    if( jag::oldscape::dash3d::world::m_click )
                    {
                        v22 = (unsigned int)jag::oldscape::dash3d::world::m_clickX;
                        if( (unsigned __int8)jag::oldscape::dash3d::world::InsideTriangle(
                                (jag::oldscape::dash3d::world*)(unsigned int)
                                    jag::oldscape::dash3d::world::m_clickX,
                                jag::oldscape::dash3d::world::m_clickY,
                                v88,
                                v90,
                                v86,
                                v87,
                                v89,
                                v85,
                                v66) )
                        {
                            jag::oldscape::dash3d::world::m_groundX = a8;
                            jag::oldscape::dash3d::world::m_groundZ = a9;
                            jag::oldscape::dash3d::world::m_groundY = v32;
                        }
                    }
                    if( a10 == -1 )
                    {
                        if( a13 != 12345678 )
                        {
                            v22 = *(_QWORD*)(*(_QWORD*)v95 + 8LL);
                            jag::oldscape::dash3d::NXTPix3D::GouraudTriangleOpenGL(
                                (jag::oldscape::dash3d::NXTPix3D*)v22,
                                v77,
                                v78,
                                v76,
                                v73,
                                *(int*)v74,
                                v72[0],
                                v81[0],
                                v82[0],
                                v80[0],
                                a13,
                                a14,
                                a12,
                                v64);
                        }
                    }
                    else if( jag::oldscape::dash3d::world::m_lowMem )
                    {
                        v33 = jag::shared_ptr<jag::oldscape::dash3d::TextureProvider>::operator->(
                            &jag::oldscape::dash3d::Pix3D::m_textureProvider);
                        v34 = (*(__int64(__fastcall**)(__int64, _QWORD))(*(_QWORD*)v33 + 24LL))(
                            v33, a10);
                        v93 = *(jag::oldscape::dash3d::NXTPix3D**)(*(_QWORD*)v95 + 8LL);
                        v91 = v88;
                        v68 = v90;
                        v92 = (jag::oldscape::dash3d::NXTPix3D*)(unsigned int)v85;
                        v69 = v86;
                        v35 = v87;
                        v36 = v89;
                        v38 = jag::oldscape::dash3d::Pix3D::TextureLightColour(
                            (jag::oldscape::dash3d::Pix3D*)v34, a13, v37);
                        v40 = jag::oldscape::dash3d::Pix3D::TextureLightColour(
                            (jag::oldscape::dash3d::Pix3D*)v34, a14, v39);
                        v42 = jag::oldscape::dash3d::Pix3D::TextureLightColour(
                            (jag::oldscape::dash3d::Pix3D*)v34, a12, v41);
                        v22 = (unsigned __int64)v93;
                        jag::oldscape::dash3d::NXTPix3D::GouraudTriangle(
                            v93, v91, v68, v69, v35, v36, (__int16)v92, v38, v40, v42);
                    }
                    else
                    {
                        v22 = *(_QWORD*)(*(_QWORD*)v95 + 8LL);
                        jag::oldscape::dash3d::NXTPix3D::TextureTriangleOpenGL(
                            (jag::oldscape::dash3d::NXTPix3D*)v22,
                            v77,
                            v78,
                            v76,
                            v73,
                            *(int*)v74,
                            v72[0],
                            *(int*)v81,
                            *(int*)v82,
                            *(int*)v80,
                            a13,
                            a14,
                            a12,
                            1.0,
                            0.0,
                            1.0,
                            1.0,
                            1.0,
                            0.0,
                            a10);
                    }
                }
                v43 = v83;
                v44 = v90;
                v45 = v86;
                result = *(_BYTE**)v95;
                if( (int)((v83 - v85) * (v90 - v86) + (v84 - v86) * (v85 - v89)) > 0 )
                {
                    LODWORD(v92) = v84;
                    v46 = *(_QWORD*)(*(_QWORD*)v95 + 8LL);
                    *(_BYTE*)(v46 + 8) = 0;
                    v47 = (jag::oldscape::dash3d::NXTPix3D*)(unsigned int)v85;
                    v48 = v89;
                    if( v43 < 0 || v85 < 0 || v89 < 0 || (v49 = *(_DWORD*)(v46 + 28), v43 > v49) ||
                        v85 > v49 || v89 > v49 )
                        *(_BYTE*)(v46 + 8) = 1;
                    v93 = v47;
                    v50 = jag::oldscape::dash3d::MousePickingHelper::GetMouseScreenX(
                        (jag::oldscape::dash3d::MousePickingHelper*)v22);
                    v51 = jag::oldscape::dash3d::MousePickingHelper::GetMouseScreenY(
                        (jag::oldscape::dash3d::MousePickingHelper*)v22);
                    v52 = jag::oldscape::dash3d::world::InsideTriangle(
                        (jag::oldscape::dash3d::world*)v50,
                        v51,
                        (int)v92,
                        v45,
                        v44,
                        v43,
                        (int)v93,
                        v48,
                        v66);
                    v53 = v94;
                    if( v52 )
                    {
                        jag::oldscape::dash3d::world::m_mouseoverGroundX = a8;
                        jag::oldscape::dash3d::world::m_mouseoverGroundZ = a9;
                        jag::oldscape::dash3d::world::m_mouseoverGroundY = v94;
                    }
                    if( jag::oldscape::dash3d::world::m_click &&
                        (unsigned __int8)jag::oldscape::dash3d::world::InsideTriangle(
                            (jag::oldscape::dash3d::world*)(unsigned int)
                                jag::oldscape::dash3d::world::m_clickX,
                            jag::oldscape::dash3d::world::m_clickY,
                            v84,
                            v86,
                            v90,
                            v83,
                            v85,
                            v89,
                            v67) )
                    {
                        jag::oldscape::dash3d::world::m_groundX = a8;
                        jag::oldscape::dash3d::world::m_groundZ = a9;
                        jag::oldscape::dash3d::world::m_groundY = v53;
                    }
                    if( a10 == -1 )
                    {
                        result = *(_BYTE**)v95;
                        if( a11 != 12345678 )
                            return (_BYTE*)jag::oldscape::dash3d::NXTPix3D::GouraudTriangleOpenGL(
                                *(jag::oldscape::dash3d::NXTPix3D**)(*(_QWORD*)v95 + 8LL),
                                v75,
                                v76,
                                v78,
                                v71,
                                *(int*)v72,
                                v74[0],
                                v79[0],
                                v80[0],
                                v82[0],
                                a11,
                                a12,
                                a14,
                                v65);
                    }
                    else if( jag::oldscape::dash3d::world::m_lowMem )
                    {
                        v54 = jag::shared_ptr<jag::oldscape::dash3d::TextureProvider>::operator->(
                            &jag::oldscape::dash3d::Pix3D::m_textureProvider);
                        v55 = (*(__int64(__fastcall**)(__int64, _QWORD))(*(_QWORD*)v54 + 24LL))(
                            v54, a10);
                        v92 = *(jag::oldscape::dash3d::NXTPix3D**)(*(_QWORD*)v95 + 8LL);
                        v94 = v83;
                        v56 = v84;
                        v91 = v86;
                        v57 = v90;
                        LODWORD(v93) = v85;
                        *(_QWORD*)v95 = (unsigned int)v89;
                        v59 = jag::oldscape::dash3d::Pix3D::TextureLightColour(
                            (jag::oldscape::dash3d::Pix3D*)v55, a11, v58);
                        v61 = jag::oldscape::dash3d::Pix3D::TextureLightColour(
                            (jag::oldscape::dash3d::Pix3D*)v55, a12, v60);
                        v63 = jag::oldscape::dash3d::Pix3D::TextureLightColour(
                            (jag::oldscape::dash3d::Pix3D*)v55, a14, v62);
                        return (_BYTE*)jag::oldscape::dash3d::NXTPix3D::GouraudTriangle(
                            v92, v56, v91, v57, v94, (int)v93, v95[0], v59, v61, v63);
                    }
                    else
                    {
                        return (_BYTE*)jag::oldscape::dash3d::NXTPix3D::TextureTriangleOpenGL(
                            *(jag::oldscape::dash3d::NXTPix3D**)(*(_QWORD*)v95 + 8LL),
                            v75,
                            v76,
                            v78,
                            v71,
                            *(int*)v72,
                            v74[0],
                            *(int*)v79,
                            *(int*)v80,
                            *(int*)v82,
                            a11,
                            a12,
                            a14,
                            0.0,
                            1.0,
                            0.0,
                            0.0,
                            0.0,
                            1.0,
                            a10);
                    }
                }
            }
        }
    }
    return result;
}