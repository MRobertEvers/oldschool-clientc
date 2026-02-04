__int64 __fastcall jag::oldscape::dash3d::world::Fill(jag::oldscape::dash3d::world *a1, __int64 a2, char a3, int a4)
{
  jag::oldscape::dash3d::world *v5; // r15
  char *v6; // r12
  __int64 result; // rax
  __int64 v8; // rax
  __int64 Square; // rbx
  int v10; // eax
  __int64 v11; // rbx
  bool v12; // zf
  __int64 v13; // rbx
  int v14; // ecx
  __int64 v15; // rbx
  char v16; // al
  __int64 v17; // rax
  __int64 v18; // rbx
  __int64 v19; // rbx
  __int64 v20; // rax
  __int64 v21; // rax
  __int64 v22; // rax
  __int64 v23; // rax
  __int64 v24; // r14
  __int64 v25; // rax
  __int64 v26; // rbx
  __int64 v27; // rax
  int v28; // r15d
  int v29; // r12d
  int v30; // r13d
  __int64 v31; // r14
  __int64 v32; // rax
  jag::oldscape::dash3d::ModelSource *i; // r14
  __int64 v34; // rbx
  __int64 v35; // rax
  __int64 v36; // rax
  __int64 v37; // rbx
  __int64 v38; // rax
  int v39; // r14d
  int v40; // r13d
  int v41; // r15d
  __int64 v42; // r12
  __int64 v43; // rax
  __int64 v44; // rbx
  __int64 v45; // r14
  __int64 v46; // rbx
  jag::oldscape::dash3d::Square *v47; // rbx
  int v48; // r15d
  __int64 v49; // rax
  __int64 v50; // rax
  __int64 v51; // rcx
  __int64 v52; // rbx
  void (__fastcall *v53)(jag::oldscape::dash3d::NXTWorldRenderer *, jag::oldscape::dash3d::world *, __int64, __int64, _QWORD, _QWORD); // r10
  __int64 v54; // r14
  jag::oldscape::dash3d::ModelSource *v55; // r12
  __int64 v56; // rbx
  unsigned int v57; // ecx
  jag::oldscape::dash3d::Square *v58; // rdx
  int v59; // eax
  int v60; // ecx
  int v61; // ebx
  __int64 v62; // rax
  unsigned int v63; // ebx
  __int64 v64; // rax
  __int64 v65; // rax
  int v66; // r13d
  int v67; // ebx
  int v68; // r12d
  __int64 v69; // r15
  __int64 v70; // rax
  unsigned int v71; // ebx
  __int64 v72; // rax
  __int64 v73; // rax
  int v74; // r15d
  int v75; // r13d
  int v76; // ebx
  __int64 v77; // r12
  __int64 v78; // rax
  jag::oldscape::dash3d::ModelSource *v79; // r13
  __int64 v80; // rax
  __int64 v81; // rax
  unsigned int v82; // ebx
  __int64 v83; // rax
  __int64 v84; // rax
  __int64 v85; // rax
  int v86; // r14d
  int v87; // ebx
  int v88; // r14d
  int v89; // r15d
  int v90; // r12d
  int v91; // ebx
  int v92; // r12d
  jag::oldscape::dash3d::ModelSource *v93; // rdi
  __int64 v94; // r13
  __int64 v95; // rax
  int v96; // r15d
  int v97; // r14d
  int v98; // r12d
  int v99; // eax
  int v100; // edx
  int v101; // ecx
  __int64 v102; // rax
  unsigned int v103; // ebx
  __int64 v104; // rax
  __int64 v105; // rax
  __int64 v106; // rax
  int v107; // ebx
  int v108; // r15d
  int v109; // r12d
  jag::oldscape::dash3d::ModelSource *v110; // rdi
  __int64 v111; // r13
  __int64 v112; // rax
  __int64 v113; // rax
  __int64 v114; // rax
  unsigned int v115; // ebx
  __int64 v116; // rax
  __int64 v117; // rax
  __int64 v118; // rax
  int v119; // ebx
  jag::oldscape::dash3d::ModelSource *v120; // rdi
  __int64 v121; // r13
  __int64 v122; // rax
  __int64 v123; // rbx
  __int64 v124; // rax
  __int64 v125; // rax
  int v126; // r14d
  int v127; // r12d
  int v128; // r13d
  __int64 v129; // r15
  __int64 v130; // rax
  __int64 v131; // r14
  __int64 v132; // rax
  __int64 v133; // rax
  __int64 v134; // rax
  int v135; // r15d
  int v136; // r12d
  int v137; // r13d
  __int64 v138; // rbx
  __int64 v139; // rax
  __int64 v140; // rax
  __int64 v141; // rax
  int v142; // r15d
  int v143; // r12d
  int v144; // r13d
  __int64 v145; // rbx
  __int64 v146; // rax
  __int64 v147; // rax
  __int64 v148; // rax
  int v149; // r15d
  int v150; // r12d
  int v151; // r13d
  __int64 v152; // rbx
  __int64 v153; // rax
  jag::oldscape::dash3d::EntityHighlight *TileHighlightData; // rax
  jag::oldscape::dash3d::EntityHighlight *v155; // rbx
  int v156; // r15d
  char v157; // r14
  int TileOutlineWidth; // r12d
  int Colour; // r13d
  unsigned __int8 TileTintAlpha; // al
  int v161; // r14d
  jag::oldscape::dash3d::world *v162; // r15
  __int64 v163; // rbx
  __int64 v164; // rbx
  __int64 v165; // rbx
  __int64 v166; // rbx
  bool v167; // r15
  __int64 v168; // r12
  __int64 v169; // r14
  __int64 v170; // rbx
  __int64 v171; // rax
  __int64 v172; // rax
  _DWORD *v173; // rax
  __int64 v174; // r14
  unsigned int v175; // ebx
  __int64 v176; // rax
  __int64 v177; // rax
  int v178; // r15d
  int v179; // r12d
  int v180; // r13d
  __int64 v181; // rbx
  __int64 v182; // rax
  __int64 v183; // rax
  jag::oldscape::dash3d::Square *v184; // rax
  int v185; // ecx
  int v186; // r14d
  int v187; // ecx
  __int64 v188; // rbx
  __int64 v189; // rax
  __int64 v190; // rax
  int j; // r12d
  __int64 v192; // rbx
  char v193; // al
  __int64 v194; // r13
  __int64 v195; // rax
  bool v196; // r15
  __int64 v197; // rax
  BOOL v198; // r14d
  int v199; // r15d
  int v200; // r14d
  __int64 v201; // rax
  int v202; // edx
  int v203; // edx
  int v204; // eax
  __int64 v205; // rax
  __int64 v206; // r13
  int v207; // ebx
  int v208; // ebx
  int v209; // r12d
  int v210; // r15d
  int v211; // r15d
  int v212; // ebx
  __int64 v213; // rax
  int v214; // r13d
  jag::oldscape::dash3d::ModelSource *v215; // r14
  __int64 v216; // rbx
  int v217; // r12d
  __int64 v218; // rax
  int v219; // ebx
  __int64 v220; // r13
  __int64 v221; // rax
  jag::oldscape::dash3d::world *v222; // rdi
  int v223; // r15d
  __int64 v224; // rax
  __int64 v225; // rax
  unsigned int v226; // ecx
  int v227; // eax
  __int64 v228; // r14
  int v229; // ebx
  __int64 v230; // rax
  __int64 v231; // rax
  __int64 v232; // rax
  __int64 v233; // rax
  __int64 v234; // rax
  __int64 v235; // rax
  __int64 v236; // rax
  char v237; // al
  int v238; // r15d
  __int64 v239; // rax
  char v240; // r12
  __int64 v241; // rax
  unsigned __int16 v242; // r13
  __int64 v243; // rax
  unsigned __int16 v244; // bx
  __int64 v245; // rax
  unsigned __int8 v246; // al
  __int64 v247; // r12
  __int64 v248; // rbx
  __int64 v249; // rax
  __int64 v250; // rax
  char v251; // r12
  __int64 v252; // rax
  unsigned __int16 v253; // bx
  __int64 v254; // rax
  unsigned __int16 v255; // r13
  __int64 v256; // rax
  unsigned __int8 v257; // al
  unsigned int v258; // r15d
  int v259; // ebx
  int v260; // r12d
  int v261; // r13d
  __int64 v262; // rax
  __int64 v263; // rax
  __int64 v264; // rax
  int v265; // r15d
  int v266; // r12d
  int v267; // r13d
  __int64 v268; // rbx
  __int64 v269; // rax
  int v270; // r12d
  int k; // r13d
  __int64 v272; // rbx
  int v273; // eax
  __int64 v274; // rbx
  __int64 v275; // rbx
  int v276; // eax
  __int64 v277; // rbx
  __int64 v278; // rbx
  __int64 v279; // r14
  __int64 v280; // rax
  __int64 v281; // rax
  __int64 v282; // rax
  int v283; // r15d
  int v284; // r13d
  int v285; // r13d
  int v286; // ebx
  __int64 v287; // r12
  __int64 v288; // rax
  __int64 v289; // rax
  __int64 v290; // rax
  int v291; // r15d
  int v292; // r13d
  int v293; // r13d
  int v294; // ebx
  __int64 v295; // r12
  __int64 v296; // rax
  __int64 v297; // rax
  __int64 v298; // rax
  int v299; // r15d
  int v300; // r13d
  int v301; // r13d
  int v302; // ebx
  __int64 v303; // r12
  __int64 v304; // rax
  __int64 v305; // r14
  __int64 v306; // rax
  __int64 v307; // rax
  unsigned int v308; // ebx
  __int64 v309; // rax
  __int64 v310; // rax
  __int64 v311; // rax
  int v312; // r15d
  int v313; // ebx
  int v314; // r15d
  int v315; // r12d
  int v316; // r13d
  int v317; // ebx
  int v318; // r13d
  __int64 v319; // rbx
  __int64 v320; // rax
  int v321; // r15d
  int v322; // r12d
  int v323; // r13d
  int v324; // eax
  int v325; // edx
  int v326; // ecx
  __int64 v327; // rax
  __int64 v328; // rax
  unsigned int v329; // ebx
  __int64 v330; // rax
  __int64 v331; // rax
  __int64 v332; // rax
  __int64 v333; // rbx
  __int64 v334; // rax
  __int64 v335; // rax
  unsigned int v336; // ebx
  __int64 v337; // rax
  __int64 v338; // rax
  __int64 v339; // rax
  int v340; // r15d
  int v341; // r13d
  __int64 v342; // rbx
  __int64 v343; // rax
  __int64 v344; // r13
  int v345; // r14d
  int v346; // r15d
  __int64 v347; // rax
  __int64 v348; // rax
  __int64 v349; // rax
  unsigned int v350; // ebx
  __int64 v351; // rax
  __int64 v352; // rax
  int v353; // r14d
  int v354; // r15d
  int v355; // r12d
  __int64 v356; // rbx
  __int64 v357; // rax
  __int64 v358; // rax
  unsigned int v359; // ebx
  __int64 v360; // rax
  __int64 v361; // rax
  int v362; // r14d
  int v363; // r15d
  int v364; // r12d
  __int64 v365; // rbx
  __int64 v366; // rax
  __int64 v367; // rbx
  __int64 v368; // rbx
  __int64 v369; // rbx
  __int64 v370; // rbx
  __int64 v371; // rbx
  int v372; // [rsp+0h] [rbp-160h]
  _BYTE v373[20]; // [rsp+50h] [rbp-110h] BYREF
  int v374; // [rsp+64h] [rbp-FCh]
  char *v375; // [rsp+68h] [rbp-F8h]
  char v376[8]; // [rsp+70h] [rbp-F0h] BYREF
  char v377[8]; // [rsp+78h] [rbp-E8h] BYREF
  __int64 v378; // [rsp+80h] [rbp-E0h]
  jag::oldscape::dash3d::NXTWorldRenderer *v379; // [rsp+88h] [rbp-D8h]
  __int128 v380; // [rsp+90h] [rbp-D0h] BYREF
  __int64 v381; // [rsp+A0h] [rbp-C0h]
  char *v382; // [rsp+A8h] [rbp-B8h]
  int v383; // [rsp+B4h] [rbp-ACh]
  char *v384; // [rsp+B8h] [rbp-A8h]
  int v385; // [rsp+C4h] [rbp-9Ch]
  __int64 v386; // [rsp+C8h] [rbp-98h]
  int v387; // [rsp+D4h] [rbp-8Ch]
  __int64 v388; // [rsp+D8h] [rbp-88h]
  jag::oldscape::dash3d::Square *v389; // [rsp+E0h] [rbp-80h]
  unsigned int v390; // [rsp+E8h] [rbp-78h] BYREF
  int v391; // [rsp+ECh] [rbp-74h]
  jag::oldscape::dash3d::ModelSource *v392; // [rsp+F0h] [rbp-70h]
  int v393[2]; // [rsp+F8h] [rbp-68h]
  jag::oldscape::dash3d::world *v394; // [rsp+100h] [rbp-60h]
  __int64 v395; // [rsp+108h] [rbp-58h]
  jag::oldscape::dash3d::ModelSource *v396; // [rsp+110h] [rbp-50h]
  unsigned __int64 v397; // [rsp+118h] [rbp-48h]
  int v398[2]; // [rsp+120h] [rbp-40h]
  jag::oldscape::dash3d::MousePickingHelper *v399; // [rsp+128h] [rbp-38h]
  char v400; // [rsp+137h] [rbp-29h]

  v391 = a4;
  v5 = a1;
  v379 = (jag::oldscape::dash3d::NXTWorldRenderer *)jag::oldscape::dash3d::world::m_worldRenderer;
  v6 = (char *)a1 + 21784;
  jag::SharedPtrList<jag::oldscape::dash3d::Square>::push((char *)a1 + 21784);
  result = jag::SharedPtrList<jag::oldscape::dash3d::Square>::empty((char *)a1 + 21784);
  if ( !(_BYTE)result )
  {
    v384 = (char *)a1 + 30072;
    v375 = (char *)a1 + 21840;
    v394 = a1;
    v382 = (char *)a1 + 21784;
    do
    {
      jag::SharedPtrList<jag::oldscape::dash3d::Square>::takeFrontAndPop(v373, v6);
      v8 = jag::shared_ptr<jag::oldscape::dash3d::Square>::operator*(v373);
      v381 = v8;
      if ( !*(_BYTE *)(v8 + 353) )
        goto LABEL_5;
      v386 = *(unsigned int *)(v8 + 32);
      v399 = (jag::oldscape::dash3d::MousePickingHelper *)*(unsigned int *)(v8 + 36);
      *(_QWORD *)v398 = *(unsigned int *)(v8 + 40);
      v390 = *(_DWORD *)(v8 + 44);
      v389 = (jag::oldscape::dash3d::Square *)v8;
      if ( !*(_BYTE *)(v8 + 352) )
      {
        v400 = a3;
        goto LABEL_148;
      }
      v400 = 1;
      if ( (a3 & 1) != 0 )
      {
        if ( (int)v386 > 0 )
        {
          Square = jag::oldscape::dash3d::world::GetSquare(v5, (int)v386 - 1, (_DWORD)v399, v398[0]);
          if ( (unsigned __int8)jag::operator!=<jag::oldscape::ClientObj>(Square, 0) )
          {
            if ( *(_BYTE *)(jag::shared_ptr<jag::oldscape::dash3d::Square>::operator->(Square) + 353) )
              goto LABEL_5;
          }
        }
        v10 = (int)v399;
        if ( (int)v399 <= jag::oldscape::dash3d::world::m_gx && (int)v399 > jag::oldscape::dash3d::world::m_minX )
        {
          v11 = jag::oldscape::dash3d::world::GetSquare(v5, v386, (int)v399 - 1, v398[0]);
          v12 = (unsigned __int8)jag::operator!=<jag::oldscape::ClientObj>(v11, 0) == 0;
          v10 = (int)v399;
          if ( !v12 )
          {
            v12 = *(_BYTE *)(jag::shared_ptr<jag::oldscape::dash3d::Square>::operator->(v11) + 353) == 0;
            v10 = (int)v399;
            if ( !v12 )
            {
              if ( *(_BYTE *)(jag::shared_ptr<jag::oldscape::dash3d::Square>::operator->(v11) + 352) )
                goto LABEL_5;
              v10 = (int)v399;
              if ( (*((_BYTE *)v389 + 344) & 1) == 0 )
                goto LABEL_5;
            }
          }
        }
        if ( v10 >= jag::oldscape::dash3d::world::m_gx && (int)v399 < jag::oldscape::dash3d::world::m_maxX - 1 )
        {
          v13 = jag::oldscape::dash3d::world::GetSquare(v5, v386, (int)v399 + 1, v398[0]);
          if ( (unsigned __int8)jag::operator!=<jag::oldscape::ClientObj>(v13, 0) )
          {
            if ( *(_BYTE *)(jag::shared_ptr<jag::oldscape::dash3d::Square>::operator->(v13) + 353)
              && (*(_BYTE *)(jag::shared_ptr<jag::oldscape::dash3d::Square>::operator->(v13) + 352)
               || (*((_BYTE *)v389 + 344) & 4) == 0) )
            {
              goto LABEL_5;
            }
          }
        }
        v14 = v398[0];
        if ( v398[0] <= jag::oldscape::dash3d::world::m_gz && v398[0] > jag::oldscape::dash3d::world::m_minZ )
        {
          v15 = jag::oldscape::dash3d::world::GetSquare(v5, v386, (_DWORD)v399, v398[0] - 1);
          v16 = jag::operator!=<jag::oldscape::ClientObj>(v15, 0);
          v14 = v398[0];
          if ( v16 )
          {
            v17 = jag::shared_ptr<jag::oldscape::dash3d::Square>::operator->(v15);
            v14 = v398[0];
            if ( *(_BYTE *)(v17 + 353) )
            {
              if ( *(_BYTE *)(jag::shared_ptr<jag::oldscape::dash3d::Square>::operator->(v15) + 352) )
                goto LABEL_5;
              v14 = v398[0];
              if ( (*((_BYTE *)v389 + 344) & 8) == 0 )
                goto LABEL_5;
            }
          }
        }
        if ( v14 >= jag::oldscape::dash3d::world::m_gz
          && v14 < jag::oldscape::dash3d::world::m_maxZ - 1
          && (v18 = jag::oldscape::dash3d::world::GetSquare(v5, v386, (_DWORD)v399, v14 + 1),
              (unsigned __int8)jag::operator!=<jag::oldscape::ClientObj>(v18, 0))
          && *(_BYTE *)(jag::shared_ptr<jag::oldscape::dash3d::Square>::operator->(v18) + 353) )
        {
          if ( *(_BYTE *)(jag::shared_ptr<jag::oldscape::dash3d::Square>::operator->(v18) + 352) )
            goto LABEL_5;
          v12 = (*((_BYTE *)v389 + 344) & 2) == 0;
          v400 = a3;
          if ( v12 )
            goto LABEL_5;
        }
        else
        {
          v400 = a3;
        }
      }
      *((_BYTE *)v389 + 352) = 0;
      v19 = v381 + 376;
      if ( (unsigned __int8)jag::operator!=<jag::oldscape::ClientObj>(v381 + 376, 0) )
      {
        v20 = jag::shared_ptr<jag::oldscape::dash3d::Square>::operator->(v19);
        if ( (unsigned __int8)jag::operator!=<jag::oldscape::ClientObj>(v20 + 48, 0) )
        {
          if ( !(unsigned __int8)jag::oldscape::dash3d::world::GroundoOcluded(v5, 0, (_DWORD)v399, v398[0]) )
          {
            v21 = jag::shared_ptr<jag::oldscape::dash3d::Square>::operator->(v19);
            jag::shared_ptr<jag::oldscape::dash3d::QuickGround>::operator*(v21 + 48);
            v372 = jag::oldscape::dash3d::world::m_cameraCosY;
            (*(void (__fastcall **)(jag::oldscape::dash3d::NXTWorldRenderer *, jag::oldscape::dash3d::world *, _QWORD, _QWORD, _QWORD, _QWORD))(*(_QWORD *)v379 + 24LL))(
              v379,
              v5,
              0,
              (unsigned int)jag::oldscape::dash3d::world::m_cameraSinX,
              (unsigned int)jag::oldscape::dash3d::world::m_cameraCosX,
              (unsigned int)jag::oldscape::dash3d::world::m_cameraSinY);
          }
        }
        else
        {
          v22 = jag::shared_ptr<jag::oldscape::dash3d::Square>::operator->(v19);
          if ( (unsigned __int8)jag::operator!=<jag::oldscape::ClientObj>(v22 + 64, 0)
            && !(unsigned __int8)jag::oldscape::dash3d::world::GroundoOcluded(v5, 0, (_DWORD)v399, v398[0]) )
          {
            v23 = jag::shared_ptr<jag::oldscape::dash3d::Square>::operator->(v19);
            v24 = jag::shared_ptr<jag::oldscape::dash3d::Ground>::operator*(v23 + 64);
            v25 = jag::shared_ptr<jag::oldscape::dash3d::Square>::operator->(v19);
            v372 = jag::oldscape::dash3d::world::m_cameraSinY;
            (*(void (__fastcall **)(jag::oldscape::dash3d::NXTWorldRenderer *, jag::oldscape::dash3d::world *, __int64, _QWORD, _QWORD, _QWORD))(*(_QWORD *)v379 + 32LL))(
              v379,
              v5,
              v24,
              *(unsigned int *)(v25 + 32),
              (unsigned int)jag::oldscape::dash3d::world::m_cameraSinX,
              (unsigned int)jag::oldscape::dash3d::world::m_cameraCosX);
          }
        }
        v395 = v19;
        v26 = jag::shared_ptr<jag::oldscape::dash3d::Square>::operator->(v19) + 80;
        if ( (unsigned __int8)jag::operator!=<jag::oldscape::ClientObj>(v26, 0) )
        {
          v27 = jag::shared_ptr<jag::oldscape::dash3d::Wall>::operator->(v26);
          v397 = jag::shared_ptr<jag::oldscape::dash3d::ModelSource>::operator*(v27 + 24);
          LODWORD(v392) = jag::oldscape::dash3d::world::m_cameraSinX;
          LODWORD(v396) = jag::oldscape::dash3d::world::m_cameraCosX;
          v393[0] = jag::oldscape::dash3d::world::m_cameraSinY;
          LODWORD(v388) = jag::oldscape::dash3d::world::m_cameraCosY;
          v28 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Wall>::operator->(v26) + 4)
              - jag::oldscape::dash3d::world::m_cx;
          v29 = *(_DWORD *)jag::shared_ptr<jag::oldscape::dash3d::Wall>::operator->(v26)
              - jag::oldscape::dash3d::world::m_cy;
          v30 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Wall>::operator->(v26) + 8)
              - jag::oldscape::dash3d::world::m_cz;
          v31 = *(_QWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Wall>::operator->(v26) + 56);
          v32 = jag::shared_ptr<jag::oldscape::dash3d::Wall>::operator->(v26);
          jag::oldscape::dash3d::world::SubmitModelForRender(
            v394,
            (jag::oldscape::dash3d::ModelSource *)v397,
            0,
            (_DWORD)v392,
            (_DWORD)v396,
            v393[0],
            v388,
            v28,
            v29,
            v30,
            v31,
            v391,
            (const jag::oldscape::dash3d::EntityHighlight *)(v32 + 64));
        }
        for ( i = 0; ; i = (jag::oldscape::dash3d::ModelSource *)(v397 + 1) )
        {
          v34 = v395;
          v35 = jag::shared_ptr<jag::oldscape::dash3d::Square>::operator->(v395);
          v5 = v394;
          if ( (__int64)i >= *(int *)(v35 + 156) )
            break;
          v397 = (unsigned __int64)i;
          v36 = jag::shared_ptr<jag::oldscape::dash3d::Square>::operator->(v34);
          v37 = eastl::vector<jag::shared_ptr<jag::oldscape::dash3d::Sprite>,eastl::fixed_vector_allocator<16ul,5ul,8ul,0ul,true,eastl::allocator>>::operator[](
                  v36 + 160,
                  v397);
          if ( (unsigned __int8)jag::operator!=<jag::oldscape::ClientObj>(v37, 0) )
          {
            v38 = jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v37);
            v392 = (jag::oldscape::dash3d::ModelSource *)jag::shared_ptr<jag::oldscape::dash3d::ModelSource>::operator*(v38 + 32);
            LODWORD(v396) = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v37) + 48);
            v393[0] = jag::oldscape::dash3d::world::m_cameraSinX;
            LODWORD(v388) = jag::oldscape::dash3d::world::m_cameraCosX;
            v387 = jag::oldscape::dash3d::world::m_cameraSinY;
            v385 = jag::oldscape::dash3d::world::m_cameraCosY;
            v39 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v37) + 8)
                - jag::oldscape::dash3d::world::m_cx;
            v40 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v37) + 4)
                - jag::oldscape::dash3d::world::m_cy;
            v41 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v37) + 12)
                - jag::oldscape::dash3d::world::m_cz;
            v42 = *(_QWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v37) + 80);
            v43 = jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v37);
            jag::oldscape::dash3d::world::SubmitModelForRender(
              v394,
              v392,
              (_DWORD)v396,
              v393[0],
              v388,
              v387,
              v385,
              v39,
              v40,
              v41,
              v42,
              v391,
              (const jag::oldscape::dash3d::EntityHighlight *)(v43 + 92));
          }
        }
      }
      v44 = v381;
      v45 = v381 + 48;
      if ( (unsigned __int8)jag::operator!=<jag::oldscape::ClientObj>(v381 + 48, 0) )
      {
        if ( (unsigned __int8)jag::oldscape::dash3d::world::GroundoOcluded(v5, v390, (_DWORD)v399, v398[0]) )
        {
          LODWORD(v395) = 0;
        }
        else
        {
          v49 = jag::shared_ptr<jag::oldscape::dash3d::QuickGround>::operator->(v45);
          v12 = *(_DWORD *)(v49 + 12) == 12345678;
          LOBYTE(v49) = 1;
          LODWORD(v395) = v49;
          if ( !v12 || (int)v386 <= jag::oldscape::dash3d::world::m_groundPickingLevel )
          {
            jag::shared_ptr<jag::oldscape::dash3d::QuickGround>::operator*(v45);
            v372 = jag::oldscape::dash3d::world::m_cameraCosY;
            (*(void (__fastcall **)(jag::oldscape::dash3d::NXTWorldRenderer *, jag::oldscape::dash3d::world *, _QWORD, _QWORD, _QWORD, _QWORD))(*(_QWORD *)v379 + 24LL))(
              v379,
              v5,
              v390,
              (unsigned int)jag::oldscape::dash3d::world::m_cameraSinX,
              (unsigned int)jag::oldscape::dash3d::world::m_cameraCosX,
              (unsigned int)jag::oldscape::dash3d::world::m_cameraSinY);
          }
        }
      }
      else
      {
        v46 = v44 + 64;
        if ( !(unsigned __int8)jag::operator!=<jag::oldscape::ClientObj>(v46, 0) )
        {
          v47 = v389;
          if ( (unsigned __int8)jag::oldscape::dash3d::Square::IsEmpty(v389) )
          {
            LODWORD(v395) = 0;
            v48 = v398[0];
          }
          else
          {
            v48 = v398[0];
            *(_QWORD *)&v380 = jag::oldscape::dash3d::MousePickingHelper::GetPackedTypeCode(
                                 v399,
                                 v398[0],
                                 *((_DWORD *)v47 + 8),
                                 4,
                                 0,
                                 0,
                                 v372);
            eastl::fixed_vector<long long,10000ul,true,eastl::allocator>::push_back(v384, &v380);
            LODWORD(v395) = 0;
          }
          goto LABEL_68;
        }
        if ( (unsigned __int8)jag::oldscape::dash3d::world::GroundoOcluded(v5, v390, (_DWORD)v399, v398[0]) )
        {
          LODWORD(v395) = 0;
        }
        else
        {
          v50 = jag::shared_ptr<jag::oldscape::dash3d::Ground>::operator*(v46);
          v51 = *((unsigned int *)v389 + 8);
          v52 = *(_QWORD *)v379;
          v53 = *(void (__fastcall **)(jag::oldscape::dash3d::NXTWorldRenderer *, jag::oldscape::dash3d::world *, __int64, __int64, _QWORD, _QWORD))(*(_QWORD *)v379 + 32LL);
          LOBYTE(v52) = 1;
          LODWORD(v395) = v52;
          v372 = jag::oldscape::dash3d::world::m_cameraSinY;
          v53(
            v379,
            v5,
            v50,
            v51,
            (unsigned int)jag::oldscape::dash3d::world::m_cameraSinX,
            (unsigned int)jag::oldscape::dash3d::world::m_cameraCosX);
        }
      }
      v48 = v398[0];
      *(_QWORD *)&v380 = jag::oldscape::dash3d::MousePickingHelper::GetPackedTypeCode(
                           v399,
                           v398[0],
                           *((_DWORD *)v389 + 8),
                           4,
                           0,
                           0,
                           v372);
      eastl::fixed_vector<long long,10000ul,true,eastl::allocator>::push_back(v384, &v380);
LABEL_68:
      v54 = v381 + 80;
      v55 = (jag::oldscape::dash3d::ModelSource *)(v381 + 96);
      if ( (unsigned __int8)jag::operator!=<jag::oldscape::ClientObj>(v381 + 80, 0)
        || (v397 = 0, LODWORD(v56) = 0, (unsigned __int8)jag::operator!=<jag::oldscape::ClientObj>(v55, 0)) )
      {
        v57 = 2 * (jag::oldscape::dash3d::world::m_gx < (int)v399);
        if ( jag::oldscape::dash3d::world::m_gx == (_DWORD)v399 )
          v57 = 1;
        v56 = v57 + 6;
        if ( jag::oldscape::dash3d::world::m_gz <= v48 )
          v56 = v57;
        if ( jag::oldscape::dash3d::world::m_gz == v48 )
          v56 = v57 + 3;
        v397 = jag::oldscape::dash3d::world::PRETAB[v56];
        *((_DWORD *)v389 + 92) = jag::oldscape::dash3d::world::POSTTAB[v56];
      }
      v392 = v55;
      if ( (unsigned __int8)jag::operator!=<jag::oldscape::ClientObj>(v54, 0) )
      {
        if ( (jag::oldscape::dash3d::world::MIDTAB[(unsigned int)v56]
            & *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Wall>::operator->(v54) + 12)) != 0 )
        {
          if ( *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Wall>::operator->(v54) + 12) == 16 )
          {
            v58 = v389;
            *((_DWORD *)v389 + 89) = 3;
            v59 = jag::oldscape::dash3d::world::MIDDEP_16[(unsigned int)v56];
            *((_DWORD *)v58 + 90) = v59;
            v60 = 3;
          }
          else if ( *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Wall>::operator->(v54) + 12) == 32 )
          {
            v58 = v389;
            *((_DWORD *)v389 + 89) = 6;
            v59 = jag::oldscape::dash3d::world::MIDDEP_32[(unsigned int)v56];
            *((_DWORD *)v58 + 90) = v59;
            v60 = 6;
          }
          else if ( *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Wall>::operator->(v54) + 12) == 64 )
          {
            v58 = v389;
            *((_DWORD *)v389 + 89) = 12;
            v59 = jag::oldscape::dash3d::world::MIDDEP_64[(unsigned int)v56];
            *((_DWORD *)v58 + 90) = v59;
            v60 = 12;
          }
          else
          {
            v58 = v389;
            *((_DWORD *)v389 + 89) = 9;
            v59 = jag::oldscape::dash3d::world::MIDDEP_128[(unsigned int)v56];
            *((_DWORD *)v58 + 90) = v59;
            v60 = 9;
          }
          *((_DWORD *)v58 + 91) = v60 - v59;
        }
        else
        {
          *((_DWORD *)v389 + 89) = 0;
        }
        v61 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Wall>::operator->(v54) + 12);
        LODWORD(v396) = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Wall>::operator->(v54) + 16);
        if ( (((unsigned int)v396 | v61) & (unsigned int)v397) != 0 )
        {
          v62 = jag::shared_ptr<jag::oldscape::dash3d::Wall>::operator->(v54);
          eastl::fixed_vector<long long,10000ul,true,eastl::allocator>::push_back(v384, v62 + 56);
          if ( (v61 & (unsigned int)v397) != 0 )
          {
            v63 = v390;
            v64 = jag::shared_ptr<jag::oldscape::dash3d::Wall>::operator->(v54);
            if ( !(unsigned __int8)jag::oldscape::dash3d::world::WallOccluded(
                                     v394,
                                     v63,
                                     (_DWORD)v399,
                                     v398[0],
                                     *(_DWORD *)(v64 + 12)) )
            {
              v65 = jag::shared_ptr<jag::oldscape::dash3d::Wall>::operator->(v54);
              *(_QWORD *)v393 = jag::shared_ptr<jag::oldscape::dash3d::ModelSource>::operator*(v65 + 24);
              LODWORD(v388) = jag::oldscape::dash3d::world::m_cameraSinX;
              v387 = jag::oldscape::dash3d::world::m_cameraCosX;
              v385 = jag::oldscape::dash3d::world::m_cameraSinY;
              v383 = jag::oldscape::dash3d::world::m_cameraCosY;
              v66 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Wall>::operator->(v54) + 4)
                  - jag::oldscape::dash3d::world::m_cx;
              v67 = *(_DWORD *)jag::shared_ptr<jag::oldscape::dash3d::Wall>::operator->(v54)
                  - jag::oldscape::dash3d::world::m_cy;
              v68 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Wall>::operator->(v54) + 8)
                  - jag::oldscape::dash3d::world::m_cz;
              v69 = *(_QWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Wall>::operator->(v54) + 56);
              v70 = jag::shared_ptr<jag::oldscape::dash3d::Wall>::operator->(v54);
              jag::oldscape::dash3d::world::SubmitModelForRender(
                v394,
                *(jag::oldscape::dash3d::ModelSource **)v393,
                0,
                v388,
                v387,
                v385,
                v383,
                v66,
                v67,
                v68,
                v69,
                v391,
                (const jag::oldscape::dash3d::EntityHighlight *)(v70 + 64));
            }
          }
          if ( ((unsigned int)v396 & (unsigned int)v397) != 0 )
          {
            v71 = v390;
            v72 = jag::shared_ptr<jag::oldscape::dash3d::Wall>::operator->(v54);
            if ( !(unsigned __int8)jag::oldscape::dash3d::world::WallOccluded(
                                     v394,
                                     v71,
                                     (_DWORD)v399,
                                     v398[0],
                                     *(_DWORD *)(v72 + 16)) )
            {
              v73 = jag::shared_ptr<jag::oldscape::dash3d::Wall>::operator->(v54);
              v396 = (jag::oldscape::dash3d::ModelSource *)jag::shared_ptr<jag::oldscape::dash3d::ModelSource>::operator*(v73 + 40);
              v393[0] = jag::oldscape::dash3d::world::m_cameraSinX;
              LODWORD(v388) = jag::oldscape::dash3d::world::m_cameraCosX;
              v387 = jag::oldscape::dash3d::world::m_cameraSinY;
              v385 = jag::oldscape::dash3d::world::m_cameraCosY;
              v74 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Wall>::operator->(v54) + 4)
                  - jag::oldscape::dash3d::world::m_cx;
              v75 = *(_DWORD *)jag::shared_ptr<jag::oldscape::dash3d::Wall>::operator->(v54)
                  - jag::oldscape::dash3d::world::m_cy;
              v76 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Wall>::operator->(v54) + 8)
                  - jag::oldscape::dash3d::world::m_cz;
              v77 = *(_QWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Wall>::operator->(v54) + 56);
              v78 = jag::shared_ptr<jag::oldscape::dash3d::Wall>::operator->(v54);
              jag::oldscape::dash3d::world::SubmitModelForRender(
                v394,
                v396,
                0,
                v393[0],
                v388,
                v387,
                v385,
                v74,
                v75,
                v76,
                v77,
                v391,
                (const jag::oldscape::dash3d::EntityHighlight *)(v78 + 64));
            }
          }
        }
      }
      v79 = v392;
      if ( (unsigned __int8)jag::operator!=<jag::oldscape::ClientObj>(v392, 0) )
      {
        v80 = jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v79);
        if ( ((unsigned int)v397 & *(_DWORD *)(v80 + 12)) != 0 )
        {
          v81 = jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v79);
          eastl::fixed_vector<long long,10000ul,true,eastl::allocator>::push_back(v384, v81 + 64);
          v82 = v390;
          v83 = jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v79);
          v84 = jag::shared_ptr<jag::oldscape::dash3d::ModelSource>::operator->(v83 + 32);
          if ( !(unsigned __int8)jag::oldscape::dash3d::world::SpriteOccluded(
                                   v394,
                                   v82,
                                   (_DWORD)v399,
                                   v398[0],
                                   *(_DWORD *)(v84 + 8)) )
          {
            v85 = jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v79);
            v397 = jag::shared_ptr<jag::oldscape::dash3d::ModelSource>::operator*(v85 + 32);
            LODWORD(v396) = jag::oldscape::dash3d::world::m_cameraSinX;
            v393[0] = jag::oldscape::dash3d::world::m_cameraCosX;
            LODWORD(v388) = jag::oldscape::dash3d::world::m_cameraSinY;
            v387 = jag::oldscape::dash3d::world::m_cameraCosY;
            v86 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v79) + 4);
            v87 = jag::oldscape::dash3d::world::m_cx;
            v88 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v79) + 20) + v86 - v87;
            v89 = *(_DWORD *)jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v79)
                - jag::oldscape::dash3d::world::m_cy;
            v90 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v79) + 8);
            v91 = jag::oldscape::dash3d::world::m_cz;
            v92 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v79) + 24) + v90 - v91;
            v93 = v79;
            v94 = *(_QWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v79) + 64);
            v95 = jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v93);
            jag::oldscape::dash3d::world::SubmitModelForRender(
              v394,
              (jag::oldscape::dash3d::ModelSource *)v397,
              0,
              (_DWORD)v396,
              v393[0],
              v388,
              v387,
              v88,
              v89,
              v92,
              v94,
              v391,
              (const jag::oldscape::dash3d::EntityHighlight *)(v95 + 76));
          }
        }
        else if ( *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v79) + 12) == 256 )
        {
          v96 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v79) + 4)
              - jag::oldscape::dash3d::world::m_cx;
          v97 = *(_DWORD *)jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v79)
              - jag::oldscape::dash3d::world::m_cy;
          v98 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v79) + 8)
              - jag::oldscape::dash3d::world::m_cz;
          v99 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v79) + 16);
          v100 = -v96;
          if ( (unsigned int)(v99 - 1) >= 2 )
            v100 = v96;
          v101 = -v98;
          if ( (v99 & 0xFFFFFFFE) != 2 )
            v101 = v98;
          if ( v101 >= v100 )
          {
            v113 = jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v79);
            if ( (unsigned __int8)jag::operator!=<jag::oldscape::ClientObj>(v113 + 48, 0) )
            {
              v114 = jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v79);
              eastl::fixed_vector<long long,10000ul,true,eastl::allocator>::push_back(v384, v114 + 64);
              v115 = v390;
              v116 = jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v79);
              v117 = jag::shared_ptr<jag::oldscape::dash3d::ModelSource>::operator->(v116 + 32);
              if ( !(unsigned __int8)jag::oldscape::dash3d::world::SpriteOccluded(
                                       v394,
                                       v115,
                                       (_DWORD)v399,
                                       v398[0],
                                       *(_DWORD *)(v117 + 8)) )
              {
                v118 = jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v79);
                v397 = jag::shared_ptr<jag::oldscape::dash3d::ModelSource>::operator*(v118 + 48);
                LODWORD(v396) = jag::oldscape::dash3d::world::m_cameraSinX;
                v393[0] = jag::oldscape::dash3d::world::m_cameraCosX;
                LODWORD(v388) = jag::oldscape::dash3d::world::m_cameraSinY;
                v119 = jag::oldscape::dash3d::world::m_cameraCosY;
                v120 = v79;
                v121 = *(_QWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v79) + 64);
                v122 = jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v120);
                jag::oldscape::dash3d::world::SubmitModelForRender(
                  v394,
                  (jag::oldscape::dash3d::ModelSource *)v397,
                  0,
                  (_DWORD)v396,
                  v393[0],
                  v388,
                  v119,
                  v96,
                  v97,
                  v98,
                  v121,
                  v391,
                  (const jag::oldscape::dash3d::EntityHighlight *)(v122 + 76));
              }
            }
          }
          else
          {
            v102 = jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v79);
            eastl::fixed_vector<long long,10000ul,true,eastl::allocator>::push_back(v384, v102 + 64);
            v103 = v390;
            v104 = jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v79);
            v105 = jag::shared_ptr<jag::oldscape::dash3d::ModelSource>::operator->(v104 + 32);
            if ( !(unsigned __int8)jag::oldscape::dash3d::world::SpriteOccluded(
                                     v394,
                                     v103,
                                     (_DWORD)v399,
                                     v398[0],
                                     *(_DWORD *)(v105 + 8)) )
            {
              v106 = jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v79);
              v397 = jag::shared_ptr<jag::oldscape::dash3d::ModelSource>::operator*(v106 + 32);
              LODWORD(v396) = jag::oldscape::dash3d::world::m_cameraSinX;
              v393[0] = jag::oldscape::dash3d::world::m_cameraCosX;
              LODWORD(v388) = jag::oldscape::dash3d::world::m_cameraSinY;
              v107 = jag::oldscape::dash3d::world::m_cameraCosY;
              v108 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v79) + 20) + v96;
              v109 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v79) + 24) + v98;
              v110 = v79;
              v111 = *(_QWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v79) + 64);
              v112 = jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v110);
              jag::oldscape::dash3d::world::SubmitModelForRender(
                v394,
                (jag::oldscape::dash3d::ModelSource *)v397,
                0,
                (_DWORD)v396,
                v393[0],
                v388,
                v107,
                v108,
                v97,
                v109,
                v111,
                v391,
                (const jag::oldscape::dash3d::EntityHighlight *)(v112 + 76));
            }
          }
        }
      }
      v123 = v381 + 112;
      if ( (unsigned __int8)jag::operator!=<jag::oldscape::ClientObj>(v381 + 112, 0) )
      {
        v124 = jag::shared_ptr<jag::oldscape::dash3d::GroundDecor>::operator->(v123);
        eastl::fixed_vector<long long,10000ul,true,eastl::allocator>::push_back(v384, v124 + 32);
        if ( (_BYTE)v395 )
        {
          v125 = jag::shared_ptr<jag::oldscape::dash3d::GroundDecor>::operator->(v123);
          v397 = jag::shared_ptr<jag::oldscape::dash3d::ModelSource>::operator*(v125 + 16);
          LODWORD(v392) = jag::oldscape::dash3d::world::m_cameraSinX;
          LODWORD(v396) = jag::oldscape::dash3d::world::m_cameraCosX;
          v393[0] = jag::oldscape::dash3d::world::m_cameraSinY;
          LODWORD(v388) = jag::oldscape::dash3d::world::m_cameraCosY;
          v126 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::GroundDecor>::operator->(v123) + 4)
               - jag::oldscape::dash3d::world::m_cx;
          v127 = *(_DWORD *)jag::shared_ptr<jag::oldscape::dash3d::GroundDecor>::operator->(v123)
               - jag::oldscape::dash3d::world::m_cy;
          v128 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::GroundDecor>::operator->(v123) + 8)
               - jag::oldscape::dash3d::world::m_cz;
          v129 = *(_QWORD *)(jag::shared_ptr<jag::oldscape::dash3d::GroundDecor>::operator->(v123) + 32);
          v130 = jag::shared_ptr<jag::oldscape::dash3d::GroundDecor>::operator->(v123);
          jag::oldscape::dash3d::world::SubmitModelForRender(
            v394,
            (jag::oldscape::dash3d::ModelSource *)v397,
            0,
            (_DWORD)v392,
            (_DWORD)v396,
            v393[0],
            v388,
            v126,
            v127,
            v128,
            v129,
            v391,
            (const jag::oldscape::dash3d::EntityHighlight *)(v130 + 44));
        }
      }
      v131 = v381 + 128;
      if ( (unsigned __int8)jag::operator!=<jag::oldscape::ClientObj>(v381 + 128, 0) )
      {
        if ( !*(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::GroundObject>::operator->(v131) + 72) )
        {
          v132 = jag::shared_ptr<jag::oldscape::dash3d::GroundObject>::operator->(v131);
          eastl::fixed_vector<long long,10000ul,true,eastl::allocator>::push_back(v384, v132 + 64);
          if ( (_BYTE)v395 )
          {
            v133 = jag::shared_ptr<jag::oldscape::dash3d::GroundObject>::operator->(v131);
            if ( (unsigned __int8)jag::operator!=<jag::oldscape::ClientObj>(v133 + 32, 0) )
            {
              v134 = jag::shared_ptr<jag::oldscape::dash3d::GroundObject>::operator->(v131);
              v397 = jag::shared_ptr<jag::oldscape::dash3d::ModelSource>::operator*(v134 + 32);
              LODWORD(v392) = jag::oldscape::dash3d::world::m_cameraSinX;
              LODWORD(v396) = jag::oldscape::dash3d::world::m_cameraCosX;
              v393[0] = jag::oldscape::dash3d::world::m_cameraSinY;
              LODWORD(v388) = jag::oldscape::dash3d::world::m_cameraCosY;
              v135 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::GroundObject>::operator->(v131) + 4)
                   - jag::oldscape::dash3d::world::m_cx;
              v136 = *(_DWORD *)jag::shared_ptr<jag::oldscape::dash3d::GroundObject>::operator->(v131)
                   - jag::oldscape::dash3d::world::m_cy;
              v137 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::GroundObject>::operator->(v131) + 8)
                   - jag::oldscape::dash3d::world::m_cz;
              v138 = *(_QWORD *)(jag::shared_ptr<jag::oldscape::dash3d::GroundObject>::operator->(v131) + 64);
              v139 = jag::shared_ptr<jag::oldscape::dash3d::GroundObject>::operator->(v131);
              jag::oldscape::dash3d::world::SubmitModelForRender(
                v394,
                (jag::oldscape::dash3d::ModelSource *)v397,
                0,
                (_DWORD)v392,
                (_DWORD)v396,
                v393[0],
                v388,
                v135,
                v136,
                v137,
                v138,
                v391,
                (const jag::oldscape::dash3d::EntityHighlight *)(v139 + 76));
            }
            v140 = jag::shared_ptr<jag::oldscape::dash3d::GroundObject>::operator->(v131);
            if ( (unsigned __int8)jag::operator!=<jag::oldscape::ClientObj>(v140 + 48, 0) )
            {
              v141 = jag::shared_ptr<jag::oldscape::dash3d::GroundObject>::operator->(v131);
              v397 = jag::shared_ptr<jag::oldscape::dash3d::ModelSource>::operator*(v141 + 48);
              LODWORD(v392) = jag::oldscape::dash3d::world::m_cameraSinX;
              LODWORD(v396) = jag::oldscape::dash3d::world::m_cameraCosX;
              v393[0] = jag::oldscape::dash3d::world::m_cameraSinY;
              LODWORD(v388) = jag::oldscape::dash3d::world::m_cameraCosY;
              v142 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::GroundObject>::operator->(v131) + 4)
                   - jag::oldscape::dash3d::world::m_cx;
              v143 = *(_DWORD *)jag::shared_ptr<jag::oldscape::dash3d::GroundObject>::operator->(v131)
                   - jag::oldscape::dash3d::world::m_cy;
              v144 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::GroundObject>::operator->(v131) + 8)
                   - jag::oldscape::dash3d::world::m_cz;
              v145 = *(_QWORD *)(jag::shared_ptr<jag::oldscape::dash3d::GroundObject>::operator->(v131) + 64);
              v146 = jag::shared_ptr<jag::oldscape::dash3d::GroundObject>::operator->(v131);
              jag::oldscape::dash3d::world::SubmitModelForRender(
                v394,
                (jag::oldscape::dash3d::ModelSource *)v397,
                0,
                (_DWORD)v392,
                (_DWORD)v396,
                v393[0],
                v388,
                v142,
                v143,
                v144,
                v145,
                v391,
                (const jag::oldscape::dash3d::EntityHighlight *)(v146 + 76));
            }
            v147 = jag::shared_ptr<jag::oldscape::dash3d::GroundObject>::operator->(v131);
            if ( (unsigned __int8)jag::operator!=<jag::oldscape::ClientObj>(v147 + 16, 0) )
            {
              v148 = jag::shared_ptr<jag::oldscape::dash3d::GroundObject>::operator->(v131);
              v397 = jag::shared_ptr<jag::oldscape::dash3d::ModelSource>::operator*(v148 + 16);
              LODWORD(v392) = jag::oldscape::dash3d::world::m_cameraSinX;
              LODWORD(v396) = jag::oldscape::dash3d::world::m_cameraCosX;
              v393[0] = jag::oldscape::dash3d::world::m_cameraSinY;
              LODWORD(v388) = jag::oldscape::dash3d::world::m_cameraCosY;
              v149 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::GroundObject>::operator->(v131) + 4)
                   - jag::oldscape::dash3d::world::m_cx;
              v150 = *(_DWORD *)jag::shared_ptr<jag::oldscape::dash3d::GroundObject>::operator->(v131)
                   - jag::oldscape::dash3d::world::m_cy;
              v151 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::GroundObject>::operator->(v131) + 8)
                   - jag::oldscape::dash3d::world::m_cz;
              v152 = *(_QWORD *)(jag::shared_ptr<jag::oldscape::dash3d::GroundObject>::operator->(v131) + 64);
              v153 = jag::shared_ptr<jag::oldscape::dash3d::GroundObject>::operator->(v131);
              jag::oldscape::dash3d::world::SubmitModelForRender(
                v394,
                (jag::oldscape::dash3d::ModelSource *)v397,
                0,
                (_DWORD)v392,
                (_DWORD)v396,
                v393[0],
                v388,
                v149,
                v150,
                v151,
                v152,
                v391,
                (const jag::oldscape::dash3d::EntityHighlight *)(v153 + 76));
            }
          }
        }
      }
      if ( (_BYTE)v395 )
      {
        TileHighlightData = (jag::oldscape::dash3d::EntityHighlight *)jag::oldscape::dash3d::Square::GetTileHighlightData(v389);
        v155 = TileHighlightData;
        if ( TileHighlightData )
        {
          if ( (unsigned __int8)jag::oldscape::dash3d::EntityHighlight::AlwaysOnTop(TileHighlightData) )
          {
            jag::oldscape::widgets::Padding::Padding(
              (jag::oldscape::widgets::Padding *)&v380,
              (_DWORD)v399,
              v398[0],
              1,
              1);
            std::make_unique<jag::oldscape::dash3d::DeferredGroundGridHighlight,int &,jag::oldscape::dash3d::GridRect,jag::oldscape::dash3d::EntityHighlight const&>(
              v377,
              &v390,
              &v380,
              v155);
            std::unique_ptr<jag::oldscape::dash3d::DeferredHighlight>::unique_ptr<jag::oldscape::dash3d::DeferredGroundGridHighlight,std::default_delete<jag::oldscape::dash3d::DeferredGroundGridHighlight>,void,void>(
              v376,
              v377);
            eastl::fixed_vector<std::unique_ptr<jag::oldscape::dash3d::DeferredHighlight>,1024ul,true,eastl::allocator>::push_back(
              v375,
              v376);
            std::unique_ptr<jag::oldscape::dash3d::DeferredHighlight>::~unique_ptr(v376);
            std::unique_ptr<jag::oldscape::dash3d::DeferredGroundGridHighlight>::~unique_ptr(v377);
          }
          else
          {
            LODWORD(v397) = v390;
            LODWORD(v395) = jag::oldscape::dash3d::world::m_cameraSinX;
            LODWORD(v392) = jag::oldscape::dash3d::world::m_cameraCosX;
            LODWORD(v396) = jag::oldscape::dash3d::world::m_cameraSinY;
            v156 = jag::oldscape::dash3d::world::m_cameraCosY;
            v157 = jag::oldscape::dash3d::EntityHighlight::ShowInsideEdges(v155);
            TileOutlineWidth = (unsigned __int16)jag::oldscape::dash3d::EntityHighlight::GetTileOutlineWidth(v155);
            Colour = (unsigned __int16)jag::oldscape::dash3d::EntityHighlight::GetColour(v155);
            TileTintAlpha = jag::oldscape::dash3d::EntityHighlight::GetTileTintAlpha(v155);
            jag::oldscape::dash3d::NXTWorldRenderer::RenderGroundOverlayGrid(
              v379,
              v394,
              v397,
              v395,
              (_DWORD)v392,
              (_DWORD)v396,
              v156,
              (_DWORD)v399,
              v398[0],
              1,
              1,
              v157,
              TileOutlineWidth,
              Colour,
              TileTintAlpha);
          }
        }
      }
      v161 = *((_DWORD *)v389 + 86);
      v162 = v394;
      if ( v161 )
      {
        if ( (int)v399 < jag::oldscape::dash3d::world::m_gx && (v161 & 4) != 0 )
        {
          v163 = jag::oldscape::dash3d::world::GetSquare(v394, v386, (int)v399 + 1, v398[0]);
          if ( (unsigned __int8)jag::operator!=<jag::oldscape::ClientObj>(v163, 0) )
          {
            if ( *(_BYTE *)(jag::shared_ptr<jag::oldscape::dash3d::Square>::operator->(v163) + 353) )
              jag::SharedPtrList<jag::oldscape::dash3d::Square>::push(v382);
          }
        }
        if ( v398[0] < jag::oldscape::dash3d::world::m_gz && (v161 & 2) != 0 )
        {
          v164 = jag::oldscape::dash3d::world::GetSquare(v162, v386, (_DWORD)v399, v398[0] + 1);
          if ( (unsigned __int8)jag::operator!=<jag::oldscape::ClientObj>(v164, 0) )
          {
            if ( *(_BYTE *)(jag::shared_ptr<jag::oldscape::dash3d::Square>::operator->(v164) + 353) )
              jag::SharedPtrList<jag::oldscape::dash3d::Square>::push(v382);
          }
        }
        if ( (int)v399 > jag::oldscape::dash3d::world::m_gx && (v161 & 1) != 0 )
        {
          v165 = jag::oldscape::dash3d::world::GetSquare(v162, v386, (int)v399 - 1, v398[0]);
          if ( (unsigned __int8)jag::operator!=<jag::oldscape::ClientObj>(v165, 0) )
          {
            if ( *(_BYTE *)(jag::shared_ptr<jag::oldscape::dash3d::Square>::operator->(v165) + 353) )
              jag::SharedPtrList<jag::oldscape::dash3d::Square>::push(v382);
          }
        }
        if ( v398[0] > jag::oldscape::dash3d::world::m_gz && (v161 & 8) != 0 )
        {
          v166 = jag::oldscape::dash3d::world::GetSquare(v162, v386, (_DWORD)v399, v398[0] - 1);
          if ( (unsigned __int8)jag::operator!=<jag::oldscape::ClientObj>(v166, 0) )
          {
            if ( *(_BYTE *)(jag::shared_ptr<jag::oldscape::dash3d::Square>::operator->(v166) + 353) )
              jag::SharedPtrList<jag::oldscape::dash3d::Square>::push(v382);
          }
        }
      }
LABEL_148:
      if ( *((_DWORD *)v389 + 89) )
      {
        v167 = *((_DWORD *)v389 + 39) > 0;
        if ( *((int *)v389 + 39) > 0 )
        {
          v168 = v381 + 160;
          v169 = v381 + 280;
          v170 = 0;
          do
          {
            v172 = eastl::vector<jag::shared_ptr<jag::oldscape::dash3d::Sprite>,eastl::fixed_vector_allocator<16ul,5ul,8ul,0ul,true,eastl::allocator>>::operator[](
                     v168,
                     v170);
            if ( *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v172) + 72) != jag::oldscape::dash3d::world::m_cycleNo )
            {
              v173 = (_DWORD *)eastl::vector<int,eastl::fixed_vector_allocator<4ul,5ul,4ul,0ul,true,eastl::allocator>>::operator[](
                                 v169,
                                 v170);
              if ( (*v173 & *((_DWORD *)v389 + 89)) == *((_DWORD *)v389 + 90) )
                break;
            }
            ++v170;
            v171 = *((int *)v389 + 39);
            v167 = v170 < v171;
          }
          while ( v170 < v171 );
        }
        v174 = v381 + 80;
        if ( !v167 )
        {
          if ( (unsigned __int8)jag::shared_ptr<jag::oldscape::rs2lib::worldmap::MapElement>::operator bool(v381 + 80) )
          {
            v175 = v390;
            v176 = jag::shared_ptr<jag::oldscape::dash3d::Wall>::operator->(v174);
            if ( !(unsigned __int8)jag::oldscape::dash3d::world::WallOccluded(
                                     v394,
                                     v175,
                                     (_DWORD)v399,
                                     v398[0],
                                     *(_DWORD *)(v176 + 12)) )
            {
              v177 = jag::shared_ptr<jag::oldscape::dash3d::Wall>::operator->(v174);
              v397 = jag::shared_ptr<jag::oldscape::dash3d::ModelSource>::operator*(v177 + 24);
              LODWORD(v395) = jag::oldscape::dash3d::world::m_cameraSinX;
              LODWORD(v392) = jag::oldscape::dash3d::world::m_cameraCosX;
              LODWORD(v396) = jag::oldscape::dash3d::world::m_cameraSinY;
              v393[0] = jag::oldscape::dash3d::world::m_cameraCosY;
              v178 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Wall>::operator->(v174) + 4)
                   - jag::oldscape::dash3d::world::m_cx;
              v179 = *(_DWORD *)jag::shared_ptr<jag::oldscape::dash3d::Wall>::operator->(v174)
                   - jag::oldscape::dash3d::world::m_cy;
              v180 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Wall>::operator->(v174) + 8)
                   - jag::oldscape::dash3d::world::m_cz;
              v181 = *(_QWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Wall>::operator->(v174) + 56);
              v182 = jag::shared_ptr<jag::oldscape::dash3d::Wall>::operator->(v174);
              jag::oldscape::dash3d::world::SubmitModelForRender(
                v394,
                (jag::oldscape::dash3d::ModelSource *)v397,
                0,
                v395,
                (_DWORD)v392,
                (_DWORD)v396,
                v393[0],
                v178,
                v179,
                v180,
                v181,
                v391,
                (const jag::oldscape::dash3d::EntityHighlight *)(v182 + 64));
            }
          }
          *((_DWORD *)v389 + 89) = 0;
        }
        if ( (unsigned __int8)jag::shared_ptr<jag::oldscape::rs2lib::worldmap::MapElement>::operator bool(v174) )
        {
          v183 = jag::shared_ptr<jag::oldscape::dash3d::Wall>::operator->(v174);
          eastl::fixed_vector<long long,10000ul,true,eastl::allocator>::push_back(v384, v183 + 56);
        }
      }
      v184 = v389;
      if ( *((_BYTE *)v389 + 354) )
      {
        v185 = *((_DWORD *)v389 + 39);
        *((_BYTE *)v389 + 354) = 0;
        v393[0] = v185;
        v5 = v394;
        if ( v185 <= 0 )
        {
          LODWORD(v396) = 0;
        }
        else
        {
          v388 = v381 + 160;
          LODWORD(v396) = 0;
          v186 = 0;
          do
          {
            while ( 1 )
            {
              v188 = eastl::vector<jag::shared_ptr<jag::oldscape::dash3d::Sprite>,eastl::fixed_vector_allocator<16ul,5ul,8ul,0ul,true,eastl::allocator>>::operator[](
                       v388,
                       v186);
              v189 = jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v188);
              v395 = v188;
              v187 = 0;
              if ( *(_DWORD *)(v189 + 72) != jag::oldscape::dash3d::world::m_cycleNo )
              {
                LODWORD(v397) = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v395) + 52);
                while ( 1 )
                {
                  v190 = jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v395);
                  if ( (int)v397 > *(_DWORD *)(v190 + 56) )
                    break;
                  for ( j = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v395) + 60);
                        j <= *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v395) + 64);
                        ++j )
                  {
                    v192 = jag::oldscape::dash3d::world::GetSquare(v5, v386, v397, j);
                    v193 = jag::shared_ptr<jag::oldscape::rs2lib::worldmap::MapElement>::operator bool(v192);
                    v187 = 19;
                    if ( v193 )
                    {
                      if ( *(_BYTE *)(jag::shared_ptr<jag::oldscape::dash3d::Square>::operator->(v192) + 352) )
                      {
                        *((_BYTE *)v389 + 354) = 1;
                        ++v186;
                        v187 = 10;
                      }
                      else
                      {
                        if ( !*(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Square>::operator->(v192) + 356) )
                          goto LABEL_189;
                        LODWORD(v392) = v186;
                        v194 = v395;
                        v195 = jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v395);
                        v196 = (int)v397 > *(_DWORD *)(v195 + 52);
                        v197 = jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v194);
                        v198 = v196;
                        v199 = v196 + 4;
                        if ( (int)v397 >= *(_DWORD *)(v197 + 56) )
                          v199 = v198;
                        if ( j > *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v194) + 60) )
                          v199 += 8;
                        v200 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v194) + 64);
                        v201 = jag::shared_ptr<jag::oldscape::dash3d::Square>::operator->(v192);
                        v202 = v199 | 2;
                        if ( j >= v200 )
                          v202 = v199;
                        v203 = *(_DWORD *)(v201 + 356) & v202;
                        v204 = *((_DWORD *)v389 + 91);
                        v187 = 0;
                        if ( v203 == v204 )
                        {
                          *((_BYTE *)v389 + 354) = 1;
                          v186 = (_DWORD)v392 + 1;
                          v187 = 10;
                        }
                        else
                        {
                          v186 = (int)v392;
                        }
                        v5 = v394;
                        if ( v203 != v204 )
LABEL_189:
                          v187 = 0;
                      }
                    }
                    if ( v187 != 19 && v187 )
                      goto LABEL_170;
                  }
                  v187 = 17;
LABEL_170:
                  LODWORD(v397) = v397 + 1;
                  if ( v187 != 17 )
                    goto LABEL_196;
                }
                v187 = 14;
LABEL_196:
                if ( v187 == 14 )
                {
                  v205 = eastl::vector<jag::shared_ptr<jag::oldscape::dash3d::Sprite>,eastl::fixed_vector_allocator<16ul,100ul,8ul,0ul,true,eastl::allocator>>::operator[](
                           v5,
                           (int)v396);
                  v206 = v395;
                  jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator=(v205, v395);
                  v207 = jag::oldscape::dash3d::world::m_gx;
                  v208 = v207 - *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v206) + 52);
                  v209 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v206) + 56)
                       - jag::oldscape::dash3d::world::m_gx;
                  if ( v209 <= v208 )
                    v209 = v208;
                  v210 = jag::oldscape::dash3d::world::m_gz;
                  v211 = v210 - *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v206) + 60);
                  v212 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v206) + 64)
                       - jag::oldscape::dash3d::world::m_gz;
                  v213 = jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v395);
                  if ( v212 <= v211 )
                    v212 = v211;
                  v5 = v394;
                  LODWORD(v396) = (_DWORD)v396 + 1;
                  *(_DWORD *)(v213 + 68) = v209 + v212;
                  v187 = 0;
                }
              }
              if ( !v187 )
                break;
              if ( v186 >= v393[0] )
                goto LABEL_204;
            }
            ++v186;
          }
          while ( v186 < v393[0] );
        }
LABEL_204:
        if ( (int)v396 > 0 )
        {
          v397 = (unsigned int)v396;
          do
          {
            LODWORD(v395) = -1;
            v214 = -50;
            v215 = 0;
            do
            {
              v216 = eastl::vector<jag::shared_ptr<jag::oldscape::dash3d::Sprite>,eastl::fixed_vector_allocator<16ul,100ul,8ul,0ul,true,eastl::allocator>>::operator[](
                       v5,
                       v215);
              if ( *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v216) + 72) != jag::oldscape::dash3d::world::m_cycleNo )
              {
                if ( *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v216) + 68) <= v214 )
                {
                  if ( *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v216) + 68) == v214 )
                  {
                    v217 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v216) + 8)
                         - jag::oldscape::dash3d::world::m_cx;
                    v218 = jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v216);
                    LODWORD(v392) = v214;
                    v219 = *(_DWORD *)(v218 + 12) - jag::oldscape::dash3d::world::m_cz;
                    v220 = (int)v395;
                    v221 = eastl::vector<jag::shared_ptr<jag::oldscape::dash3d::Sprite>,eastl::fixed_vector_allocator<16ul,100ul,8ul,0ul,true,eastl::allocator>>::operator[](
                             v5,
                             (int)v395);
                    v222 = v5;
                    v223 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v221) + 8)
                         - jag::oldscape::dash3d::world::m_cx;
                    v224 = eastl::vector<jag::shared_ptr<jag::oldscape::dash3d::Sprite>,eastl::fixed_vector_allocator<16ul,100ul,8ul,0ul,true,eastl::allocator>>::operator[](
                             v222,
                             v220);
                    v225 = jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v224);
                    v226 = v223 * v223
                         + (*(_DWORD *)(v225 + 12) - jag::oldscape::dash3d::world::m_cz)
                         * (*(_DWORD *)(v225 + 12) - jag::oldscape::dash3d::world::m_cz);
                    v227 = (int)v215;
                    if ( v217 * v217 + v219 * v219 <= v226 )
                      v227 = v395;
                    LODWORD(v395) = v227;
                    v5 = v394;
                    v214 = (int)v392;
                  }
                }
                else
                {
                  v214 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v216) + 68);
                  LODWORD(v395) = (_DWORD)v215;
                }
              }
              v215 = (jag::oldscape::dash3d::ModelSource *)((char *)v215 + 1);
            }
            while ( (jag::oldscape::dash3d::ModelSource *)v397 != v215 );
            if ( (_DWORD)v395 != -1 )
            {
              v228 = eastl::vector<jag::shared_ptr<jag::oldscape::dash3d::Sprite>,eastl::fixed_vector_allocator<16ul,100ul,8ul,0ul,true,eastl::allocator>>::operator[](
                       v5,
                       (int)v395);
              v229 = jag::oldscape::dash3d::world::m_cycleNo;
              *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v228) + 72) = v229;
              v230 = jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v228);
              eastl::fixed_vector<long long,10000ul,true,eastl::allocator>::push_back(v384, v230 + 80);
              v231 = jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v228);
              if ( (unsigned __int8)jag::oldscape::dash3d::EntityHighlight::HasTileHighlight((jag::oldscape::dash3d::EntityHighlight *)(v231 + 92)) )
              {
                v380 = *(_OWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v228) + 16);
                v232 = jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v228);
                if ( (unsigned __int8)jag::oldscape::dash3d::EntityHighlight::SouthWestOnly((jag::oldscape::dash3d::EntityHighlight *)(v232 + 92)) )
                  *((_QWORD *)&v380 + 1) = 0x100000001LL;
                v233 = jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v228);
                if ( (unsigned __int8)jag::oldscape::dash3d::EntityHighlight::AlwaysOnTop((jag::oldscape::dash3d::EntityHighlight *)(v233 + 92)) )
                {
                  v234 = jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v228);
                  if ( (unsigned __int8)jag::oldscape::dash3d::EntityHighlight::SnapToGrid((jag::oldscape::dash3d::EntityHighlight *)(v234 + 92)) )
                  {
                    v235 = jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v228);
                    std::make_unique<jag::oldscape::dash3d::DeferredGroundGridHighlight,int &,jag::oldscape::dash3d::GridRect &,jag::oldscape::dash3d::EntityHighlight &>(
                      v377,
                      &v390,
                      &v380,
                      v235 + 92);
                    std::unique_ptr<jag::oldscape::dash3d::DeferredHighlight>::unique_ptr<jag::oldscape::dash3d::DeferredGroundGridHighlight,std::default_delete<jag::oldscape::dash3d::DeferredGroundGridHighlight>,void,void>(
                      v376,
                      v377);
                    eastl::fixed_vector<std::unique_ptr<jag::oldscape::dash3d::DeferredHighlight>,1024ul,true,eastl::allocator>::push_back(
                      v375,
                      v376);
                    std::unique_ptr<jag::oldscape::dash3d::DeferredHighlight>::~unique_ptr(v376);
                    std::unique_ptr<jag::oldscape::dash3d::DeferredGroundGridHighlight>::~unique_ptr(v377);
                  }
                  else
                  {
                    v247 = jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v228);
                    v248 = jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v228);
                    v249 = jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v228);
                    std::make_unique<jag::oldscape::dash3d::DeferredGroundFineHighlight,int &,int &,int &,jag::oldscape::dash3d::GridRect &,jag::oldscape::dash3d::EntityHighlight &>(
                      v377,
                      &v390,
                      v247 + 8,
                      v248 + 12,
                      &v380,
                      v249 + 92);
                    std::unique_ptr<jag::oldscape::dash3d::DeferredHighlight>::unique_ptr<jag::oldscape::dash3d::DeferredGroundFineHighlight,std::default_delete<jag::oldscape::dash3d::DeferredGroundFineHighlight>,void,void>(
                      v376,
                      v377);
                    eastl::fixed_vector<std::unique_ptr<jag::oldscape::dash3d::DeferredHighlight>,1024ul,true,eastl::allocator>::push_back(
                      v375,
                      v376);
                    std::unique_ptr<jag::oldscape::dash3d::DeferredHighlight>::~unique_ptr(v376);
                    std::unique_ptr<jag::oldscape::dash3d::DeferredGroundFineHighlight>::~unique_ptr(v377);
                  }
                }
                else
                {
                  v236 = jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v228);
                  v237 = jag::oldscape::dash3d::EntityHighlight::SnapToGrid((jag::oldscape::dash3d::EntityHighlight *)(v236 + 92));
                  LODWORD(v392) = v390;
                  v393[0] = jag::oldscape::dash3d::world::m_cameraSinX;
                  v387 = jag::oldscape::dash3d::world::m_cameraCosX;
                  LODWORD(v388) = jag::oldscape::dash3d::world::m_cameraSinY;
                  v385 = jag::oldscape::dash3d::world::m_cameraCosY;
                  if ( v237 )
                  {
                    v378 = *(_QWORD *)((char *)&v380 + 4);
                    v383 = v380;
                    v238 = HIDWORD(v380);
                    v239 = jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v228);
                    v240 = jag::oldscape::dash3d::EntityHighlight::ShowInsideEdges((jag::oldscape::dash3d::EntityHighlight *)(v239 + 92));
                    v241 = jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v228);
                    v242 = jag::oldscape::dash3d::EntityHighlight::GetTileOutlineWidth((jag::oldscape::dash3d::EntityHighlight *)(v241 + 92));
                    v243 = jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v228);
                    v244 = jag::oldscape::dash3d::EntityHighlight::GetColour((jag::oldscape::dash3d::EntityHighlight *)(v243 + 92));
                    v245 = jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v228);
                    v246 = jag::oldscape::dash3d::EntityHighlight::GetTileTintAlpha((jag::oldscape::dash3d::EntityHighlight *)(v245 + 92));
                    jag::oldscape::dash3d::NXTWorldRenderer::RenderGroundOverlayGrid(
                      v379,
                      v394,
                      (_DWORD)v392,
                      v393[0],
                      v387,
                      v388,
                      v385,
                      v383,
                      v378,
                      HIDWORD(v378),
                      v238,
                      v240,
                      v242,
                      v244,
                      v246);
                  }
                  else
                  {
                    v383 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v228) + 8);
                    LODWORD(v378) = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v228) + 12);
                    v374 = HIDWORD(v380);
                    HIDWORD(v378) = DWORD2(v380);
                    v250 = jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v228);
                    v251 = jag::oldscape::dash3d::EntityHighlight::ShowInsideEdges((jag::oldscape::dash3d::EntityHighlight *)(v250 + 92));
                    v252 = jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v228);
                    v253 = jag::oldscape::dash3d::EntityHighlight::GetTileOutlineWidth((jag::oldscape::dash3d::EntityHighlight *)(v252 + 92));
                    v254 = jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v228);
                    v255 = jag::oldscape::dash3d::EntityHighlight::GetColour((jag::oldscape::dash3d::EntityHighlight *)(v254 + 92));
                    v256 = jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v228);
                    v257 = jag::oldscape::dash3d::EntityHighlight::GetTileTintAlpha((jag::oldscape::dash3d::EntityHighlight *)(v256 + 92));
                    jag::oldscape::dash3d::NXTWorldRenderer::RenderGroundOverlayFine(
                      v379,
                      v394,
                      (_DWORD)v392,
                      v393[0],
                      v387,
                      v388,
                      v385,
                      v383,
                      v378,
                      HIDWORD(v378),
                      v374,
                      v251,
                      v253,
                      v255,
                      v257);
                  }
                }
              }
              v258 = v390;
              LODWORD(v392) = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v228) + 52);
              v259 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v228) + 56);
              v260 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v228) + 60);
              v261 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v228) + 64);
              v262 = jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v228);
              v263 = jag::shared_ptr<jag::oldscape::dash3d::ModelSource>::operator->(v262 + 32);
              if ( !(unsigned __int8)jag::oldscape::dash3d::world::SpriteOccluded(
                                       v394,
                                       v258,
                                       (_DWORD)v392,
                                       v259,
                                       v260,
                                       v261,
                                       *(_DWORD *)(v263 + 8)) )
              {
                v264 = jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v228);
                v392 = (jag::oldscape::dash3d::ModelSource *)jag::shared_ptr<jag::oldscape::dash3d::ModelSource>::operator*(v264 + 32);
                v393[0] = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v228) + 48);
                LODWORD(v388) = jag::oldscape::dash3d::world::m_cameraSinX;
                v387 = jag::oldscape::dash3d::world::m_cameraCosX;
                v385 = jag::oldscape::dash3d::world::m_cameraSinY;
                v383 = jag::oldscape::dash3d::world::m_cameraCosY;
                v265 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v228) + 8)
                     - jag::oldscape::dash3d::world::m_cx;
                v266 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v228) + 4)
                     - jag::oldscape::dash3d::world::m_cy;
                v267 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v228) + 12)
                     - jag::oldscape::dash3d::world::m_cz;
                v268 = *(_QWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v228) + 80);
                v269 = jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v228);
                jag::oldscape::dash3d::world::SubmitModelForRender(
                  v394,
                  v392,
                  v393[0],
                  v388,
                  v387,
                  v385,
                  v383,
                  v265,
                  v266,
                  v267,
                  v268,
                  v391,
                  (const jag::oldscape::dash3d::EntityHighlight *)(v269 + 92));
              }
              v270 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v228) + 52);
              v5 = v394;
              while ( v270 <= *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v228) + 56) )
              {
                for ( k = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v228) + 60);
                      k <= *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Sprite>::operator->(v228) + 64);
                      ++k )
                {
                  v272 = jag::oldscape::dash3d::world::GetSquare(v5, v386, v270, k);
                  if ( (unsigned __int8)jag::shared_ptr<jag::oldscape::rs2lib::worldmap::MapElement>::operator bool(v272)
                    && (*(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Square>::operator->(v272) + 356)
                     || (v270 != (_DWORD)v399 || v398[0] != k)
                     && *(_BYTE *)(jag::shared_ptr<jag::oldscape::dash3d::Square>::operator->(v272) + 353)) )
                  {
                    jag::SharedPtrList<jag::oldscape::dash3d::Square>::push(v382);
                  }
                }
                ++v270;
              }
            }
          }
          while ( (_DWORD)v395 != -1 && (int)v396 > 0 );
        }
        v184 = v389;
        if ( !*((_BYTE *)v389 + 354) && *((_BYTE *)v389 + 353) )
        {
LABEL_245:
          if ( !*((_DWORD *)v184 + 89) )
          {
            v273 = (int)v399;
            if ( (int)v399 > jag::oldscape::dash3d::world::m_gx
              || (int)v399 <= jag::oldscape::dash3d::world::m_minX
              || (v274 = jag::oldscape::dash3d::world::GetSquare(v5, v386, (int)v399 - 1, v398[0]),
                  v12 = (unsigned __int8)jag::operator!=<jag::oldscape::ClientObj>(v274, 0) == 0,
                  v273 = (int)v399,
                  v12)
              || (v12 = *(_BYTE *)(jag::shared_ptr<jag::oldscape::dash3d::Square>::operator->(v274) + 353) == 0,
                  v273 = (int)v399,
                  v12) )
            {
              if ( v273 < jag::oldscape::dash3d::world::m_gx
                || (int)v399 >= jag::oldscape::dash3d::world::m_maxX - 1
                || (v275 = jag::oldscape::dash3d::world::GetSquare(v5, v386, (int)v399 + 1, v398[0]),
                    !(unsigned __int8)jag::operator!=<jag::oldscape::ClientObj>(v275, 0))
                || !*(_BYTE *)(jag::shared_ptr<jag::oldscape::dash3d::Square>::operator->(v275) + 353) )
              {
                v276 = v398[0];
                if ( v398[0] > jag::oldscape::dash3d::world::m_gz
                  || v398[0] <= jag::oldscape::dash3d::world::m_minZ
                  || (v277 = jag::oldscape::dash3d::world::GetSquare(v5, v386, (_DWORD)v399, v398[0] - 1),
                      v12 = (unsigned __int8)jag::operator!=<jag::oldscape::ClientObj>(v277, 0) == 0,
                      v276 = v398[0],
                      v12)
                  || (v12 = *(_BYTE *)(jag::shared_ptr<jag::oldscape::dash3d::Square>::operator->(v277) + 353) == 0,
                      v276 = v398[0],
                      v12) )
                {
                  if ( v276 < jag::oldscape::dash3d::world::m_gz
                    || v398[0] >= jag::oldscape::dash3d::world::m_maxZ - 1
                    || (v278 = jag::oldscape::dash3d::world::GetSquare(v5, v386, (_DWORD)v399, v398[0] + 1),
                        !(unsigned __int8)jag::operator!=<jag::oldscape::ClientObj>(v278, 0))
                    || !*(_BYTE *)(jag::shared_ptr<jag::oldscape::dash3d::Square>::operator->(v278) + 353) )
                  {
                    *((_BYTE *)v389 + 353) = 0;
                    --jag::oldscape::dash3d::world::m_fillLeft;
                    v279 = v381 + 128;
                    if ( (unsigned __int8)jag::operator!=<jag::oldscape::ClientObj>(v381 + 128, 0)
                      && *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::GroundObject>::operator->(v279) + 72) )
                    {
                      v280 = jag::shared_ptr<jag::oldscape::dash3d::GroundObject>::operator->(v279);
                      eastl::fixed_vector<long long,10000ul,true,eastl::allocator>::push_back(v384, v280 + 64);
                      v281 = jag::shared_ptr<jag::oldscape::dash3d::GroundObject>::operator->(v279);
                      if ( (unsigned __int8)jag::operator!=<jag::oldscape::ClientObj>(v281 + 32, 0) )
                      {
                        v282 = jag::shared_ptr<jag::oldscape::dash3d::GroundObject>::operator->(v279);
                        v397 = jag::shared_ptr<jag::oldscape::dash3d::ModelSource>::operator*(v282 + 32);
                        LODWORD(v395) = jag::oldscape::dash3d::world::m_cameraSinX;
                        LODWORD(v392) = jag::oldscape::dash3d::world::m_cameraCosX;
                        LODWORD(v396) = jag::oldscape::dash3d::world::m_cameraSinY;
                        v393[0] = jag::oldscape::dash3d::world::m_cameraCosY;
                        v283 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::GroundObject>::operator->(v279) + 4)
                             - jag::oldscape::dash3d::world::m_cx;
                        LODWORD(v388) = *(_DWORD *)jag::shared_ptr<jag::oldscape::dash3d::GroundObject>::operator->(v279);
                        v284 = jag::oldscape::dash3d::world::m_cy;
                        v285 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::GroundObject>::operator->(v279) + 72)
                             + v284;
                        v286 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::GroundObject>::operator->(v279) + 8)
                             - jag::oldscape::dash3d::world::m_cz;
                        v287 = *(_QWORD *)(jag::shared_ptr<jag::oldscape::dash3d::GroundObject>::operator->(v279) + 64);
                        v288 = jag::shared_ptr<jag::oldscape::dash3d::GroundObject>::operator->(v279);
                        jag::oldscape::dash3d::world::SubmitModelForRender(
                          v394,
                          (jag::oldscape::dash3d::ModelSource *)v397,
                          0,
                          v395,
                          (_DWORD)v392,
                          (_DWORD)v396,
                          v393[0],
                          v283,
                          v388 - v285,
                          v286,
                          v287,
                          v391,
                          (const jag::oldscape::dash3d::EntityHighlight *)(v288 + 76));
                      }
                      v289 = jag::shared_ptr<jag::oldscape::dash3d::GroundObject>::operator->(v279);
                      if ( (unsigned __int8)jag::operator!=<jag::oldscape::ClientObj>(v289 + 48, 0) )
                      {
                        v290 = jag::shared_ptr<jag::oldscape::dash3d::GroundObject>::operator->(v279);
                        v397 = jag::shared_ptr<jag::oldscape::dash3d::ModelSource>::operator*(v290 + 48);
                        LODWORD(v395) = jag::oldscape::dash3d::world::m_cameraSinX;
                        LODWORD(v392) = jag::oldscape::dash3d::world::m_cameraCosX;
                        LODWORD(v396) = jag::oldscape::dash3d::world::m_cameraSinY;
                        v393[0] = jag::oldscape::dash3d::world::m_cameraCosY;
                        v291 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::GroundObject>::operator->(v279) + 4)
                             - jag::oldscape::dash3d::world::m_cx;
                        LODWORD(v388) = *(_DWORD *)jag::shared_ptr<jag::oldscape::dash3d::GroundObject>::operator->(v279);
                        v292 = jag::oldscape::dash3d::world::m_cy;
                        v293 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::GroundObject>::operator->(v279) + 72)
                             + v292;
                        v294 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::GroundObject>::operator->(v279) + 8)
                             - jag::oldscape::dash3d::world::m_cz;
                        v295 = *(_QWORD *)(jag::shared_ptr<jag::oldscape::dash3d::GroundObject>::operator->(v279) + 64);
                        v296 = jag::shared_ptr<jag::oldscape::dash3d::GroundObject>::operator->(v279);
                        jag::oldscape::dash3d::world::SubmitModelForRender(
                          v394,
                          (jag::oldscape::dash3d::ModelSource *)v397,
                          0,
                          v395,
                          (_DWORD)v392,
                          (_DWORD)v396,
                          v393[0],
                          v291,
                          v388 - v293,
                          v294,
                          v295,
                          v391,
                          (const jag::oldscape::dash3d::EntityHighlight *)(v296 + 76));
                      }
                      v297 = jag::shared_ptr<jag::oldscape::dash3d::GroundObject>::operator->(v279);
                      if ( (unsigned __int8)jag::operator!=<jag::oldscape::ClientObj>(v297 + 16, 0) )
                      {
                        v298 = jag::shared_ptr<jag::oldscape::dash3d::GroundObject>::operator->(v279);
                        v397 = jag::shared_ptr<jag::oldscape::dash3d::ModelSource>::operator*(v298 + 16);
                        LODWORD(v395) = jag::oldscape::dash3d::world::m_cameraSinX;
                        LODWORD(v392) = jag::oldscape::dash3d::world::m_cameraCosX;
                        LODWORD(v396) = jag::oldscape::dash3d::world::m_cameraSinY;
                        v393[0] = jag::oldscape::dash3d::world::m_cameraCosY;
                        v299 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::GroundObject>::operator->(v279) + 4)
                             - jag::oldscape::dash3d::world::m_cx;
                        LODWORD(v388) = *(_DWORD *)jag::shared_ptr<jag::oldscape::dash3d::GroundObject>::operator->(v279);
                        v300 = jag::oldscape::dash3d::world::m_cy;
                        v301 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::GroundObject>::operator->(v279) + 72)
                             + v300;
                        v302 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::GroundObject>::operator->(v279) + 8)
                             - jag::oldscape::dash3d::world::m_cz;
                        v303 = *(_QWORD *)(jag::shared_ptr<jag::oldscape::dash3d::GroundObject>::operator->(v279) + 64);
                        v304 = jag::shared_ptr<jag::oldscape::dash3d::GroundObject>::operator->(v279);
                        jag::oldscape::dash3d::world::SubmitModelForRender(
                          v394,
                          (jag::oldscape::dash3d::ModelSource *)v397,
                          0,
                          v395,
                          (_DWORD)v392,
                          (_DWORD)v396,
                          v393[0],
                          v299,
                          v388 - v301,
                          v302,
                          v303,
                          v391,
                          (const jag::oldscape::dash3d::EntityHighlight *)(v304 + 76));
                      }
                    }
                    if ( *((_DWORD *)v389 + 92) )
                    {
                      v305 = v381 + 96;
                      if ( (unsigned __int8)jag::operator!=<jag::oldscape::ClientObj>(v381 + 96, 0) )
                      {
                        v306 = jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v305);
                        if ( (*((_DWORD *)v389 + 92) & *(_DWORD *)(v306 + 12)) != 0 )
                        {
                          v307 = jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v305);
                          eastl::fixed_vector<long long,10000ul,true,eastl::allocator>::push_back(v384, v307 + 64);
                          v308 = v390;
                          v309 = jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v305);
                          v310 = jag::shared_ptr<jag::oldscape::dash3d::ModelSource>::operator->(v309 + 32);
                          if ( !(unsigned __int8)jag::oldscape::dash3d::world::SpriteOccluded(
                                                   v394,
                                                   v308,
                                                   (_DWORD)v399,
                                                   v398[0],
                                                   *(_DWORD *)(v310 + 8)) )
                          {
                            v311 = jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v305);
                            v397 = jag::shared_ptr<jag::oldscape::dash3d::ModelSource>::operator*(v311 + 32);
                            LODWORD(v395) = jag::oldscape::dash3d::world::m_cameraSinX;
                            LODWORD(v392) = jag::oldscape::dash3d::world::m_cameraCosX;
                            LODWORD(v396) = jag::oldscape::dash3d::world::m_cameraSinY;
                            v393[0] = jag::oldscape::dash3d::world::m_cameraCosY;
                            v312 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v305) + 4);
                            v313 = jag::oldscape::dash3d::world::m_cx;
                            v314 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v305) + 20)
                                 + v312
                                 - v313;
                            v315 = *(_DWORD *)jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v305)
                                 - jag::oldscape::dash3d::world::m_cy;
                            v316 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v305) + 8);
                            v317 = jag::oldscape::dash3d::world::m_cz;
                            v318 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v305) + 24)
                                 + v316
                                 - v317;
                            v319 = *(_QWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v305) + 64);
                            v320 = jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v305);
                            jag::oldscape::dash3d::world::SubmitModelForRender(
                              v394,
                              (jag::oldscape::dash3d::ModelSource *)v397,
                              0,
                              v395,
                              (_DWORD)v392,
                              (_DWORD)v396,
                              v393[0],
                              v314,
                              v315,
                              v318,
                              v319,
                              v391,
                              (const jag::oldscape::dash3d::EntityHighlight *)(v320 + 76));
                          }
                        }
                        else if ( *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v305) + 12) == 256 )
                        {
                          v321 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v305) + 4)
                               - jag::oldscape::dash3d::world::m_cx;
                          v322 = *(_DWORD *)jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v305)
                               - jag::oldscape::dash3d::world::m_cy;
                          v323 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v305) + 8)
                               - jag::oldscape::dash3d::world::m_cz;
                          v324 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v305) + 16);
                          v325 = -v321;
                          if ( (unsigned int)(v324 - 1) >= 2 )
                            v325 = v321;
                          v326 = -v323;
                          if ( (v324 & 0xFFFFFFFE) != 2 )
                            v326 = v323;
                          if ( v326 >= v325 )
                          {
                            v335 = jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v305);
                            eastl::fixed_vector<long long,10000ul,true,eastl::allocator>::push_back(v384, v335 + 64);
                            v336 = v390;
                            v337 = jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v305);
                            v338 = jag::shared_ptr<jag::oldscape::dash3d::ModelSource>::operator->(v337 + 32);
                            if ( !(unsigned __int8)jag::oldscape::dash3d::world::SpriteOccluded(
                                                     v394,
                                                     v336,
                                                     (_DWORD)v399,
                                                     v398[0],
                                                     *(_DWORD *)(v338 + 8)) )
                            {
                              v339 = jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v305);
                              v397 = jag::shared_ptr<jag::oldscape::dash3d::ModelSource>::operator*(v339 + 32);
                              LODWORD(v395) = jag::oldscape::dash3d::world::m_cameraSinX;
                              LODWORD(v392) = jag::oldscape::dash3d::world::m_cameraCosX;
                              LODWORD(v396) = jag::oldscape::dash3d::world::m_cameraSinY;
                              v393[0] = jag::oldscape::dash3d::world::m_cameraCosY;
                              v340 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v305) + 20)
                                   + v321;
                              v341 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v305) + 24)
                                   + v323;
                              v342 = *(_QWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v305) + 64);
                              v343 = jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v305);
                              jag::oldscape::dash3d::world::SubmitModelForRender(
                                v394,
                                (jag::oldscape::dash3d::ModelSource *)v397,
                                0,
                                v395,
                                (_DWORD)v392,
                                (_DWORD)v396,
                                v393[0],
                                v340,
                                v322,
                                v341,
                                v342,
                                v391,
                                (const jag::oldscape::dash3d::EntityHighlight *)(v343 + 76));
                            }
                          }
                          else
                          {
                            v327 = jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v305);
                            if ( (unsigned __int8)jag::operator!=<jag::oldscape::ClientObj>(v327 + 48, 0) )
                            {
                              v328 = jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v305);
                              eastl::fixed_vector<long long,10000ul,true,eastl::allocator>::push_back(v384, v328 + 64);
                              v329 = v390;
                              v330 = jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v305);
                              v331 = jag::shared_ptr<jag::oldscape::dash3d::ModelSource>::operator->(v330 + 32);
                              if ( !(unsigned __int8)jag::oldscape::dash3d::world::SpriteOccluded(
                                                       v394,
                                                       v329,
                                                       (_DWORD)v399,
                                                       v398[0],
                                                       *(_DWORD *)(v331 + 8)) )
                              {
                                v332 = jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v305);
                                v397 = jag::shared_ptr<jag::oldscape::dash3d::ModelSource>::operator*(v332 + 48);
                                LODWORD(v395) = jag::oldscape::dash3d::world::m_cameraSinX;
                                LODWORD(v392) = jag::oldscape::dash3d::world::m_cameraCosX;
                                LODWORD(v396) = jag::oldscape::dash3d::world::m_cameraSinY;
                                v393[0] = jag::oldscape::dash3d::world::m_cameraCosY;
                                v333 = *(_QWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v305) + 64);
                                v334 = jag::shared_ptr<jag::oldscape::dash3d::Decor>::operator->(v305);
                                jag::oldscape::dash3d::world::SubmitModelForRender(
                                  v394,
                                  (jag::oldscape::dash3d::ModelSource *)v397,
                                  0,
                                  v395,
                                  (_DWORD)v392,
                                  (_DWORD)v396,
                                  v393[0],
                                  v321,
                                  v322,
                                  v323,
                                  v333,
                                  v391,
                                  (const jag::oldscape::dash3d::EntityHighlight *)(v334 + 76));
                              }
                            }
                          }
                        }
                      }
                      v344 = v381 + 80;
                      if ( (unsigned __int8)jag::operator!=<jag::oldscape::ClientObj>(v381 + 80, 0) )
                      {
                        v345 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Wall>::operator->(v344) + 12);
                        v346 = *((_DWORD *)v389 + 92);
                        v347 = jag::shared_ptr<jag::oldscape::dash3d::Wall>::operator->(v344);
                        if ( (v345 & v346) != 0 || (*((_DWORD *)v389 + 92) & *(_DWORD *)(v347 + 16)) != 0 )
                        {
                          v348 = jag::shared_ptr<jag::oldscape::dash3d::Wall>::operator->(v344);
                          eastl::fixed_vector<long long,10000ul,true,eastl::allocator>::push_back(v384, v348 + 56);
                          v349 = jag::shared_ptr<jag::oldscape::dash3d::Wall>::operator->(v344);
                          if ( (*((_DWORD *)v389 + 92) & *(_DWORD *)(v349 + 16)) != 0 )
                          {
                            v350 = v390;
                            v351 = jag::shared_ptr<jag::oldscape::dash3d::Wall>::operator->(v344);
                            if ( !(unsigned __int8)jag::oldscape::dash3d::world::WallOccluded(
                                                     v394,
                                                     v350,
                                                     (_DWORD)v399,
                                                     v398[0],
                                                     *(_DWORD *)(v351 + 16)) )
                            {
                              v352 = jag::shared_ptr<jag::oldscape::dash3d::Wall>::operator->(v344);
                              v397 = jag::shared_ptr<jag::oldscape::dash3d::ModelSource>::operator*(v352 + 40);
                              LODWORD(v395) = jag::oldscape::dash3d::world::m_cameraSinX;
                              LODWORD(v392) = jag::oldscape::dash3d::world::m_cameraCosX;
                              LODWORD(v396) = jag::oldscape::dash3d::world::m_cameraSinY;
                              v393[0] = jag::oldscape::dash3d::world::m_cameraCosY;
                              v353 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Wall>::operator->(v344) + 4)
                                   - jag::oldscape::dash3d::world::m_cx;
                              v354 = *(_DWORD *)jag::shared_ptr<jag::oldscape::dash3d::Wall>::operator->(v344)
                                   - jag::oldscape::dash3d::world::m_cy;
                              v355 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Wall>::operator->(v344) + 8)
                                   - jag::oldscape::dash3d::world::m_cz;
                              v356 = *(_QWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Wall>::operator->(v344) + 56);
                              v357 = jag::shared_ptr<jag::oldscape::dash3d::Wall>::operator->(v344);
                              jag::oldscape::dash3d::world::SubmitModelForRender(
                                v394,
                                (jag::oldscape::dash3d::ModelSource *)v397,
                                0,
                                v395,
                                (_DWORD)v392,
                                (_DWORD)v396,
                                v393[0],
                                v353,
                                v354,
                                v355,
                                v356,
                                v391,
                                (const jag::oldscape::dash3d::EntityHighlight *)(v357 + 64));
                            }
                          }
                          v358 = jag::shared_ptr<jag::oldscape::dash3d::Wall>::operator->(v344);
                          if ( (*((_DWORD *)v389 + 92) & *(_DWORD *)(v358 + 12)) != 0 )
                          {
                            v359 = v390;
                            v360 = jag::shared_ptr<jag::oldscape::dash3d::Wall>::operator->(v344);
                            if ( !(unsigned __int8)jag::oldscape::dash3d::world::WallOccluded(
                                                     v394,
                                                     v359,
                                                     (_DWORD)v399,
                                                     v398[0],
                                                     *(_DWORD *)(v360 + 12)) )
                            {
                              v361 = jag::shared_ptr<jag::oldscape::dash3d::Wall>::operator->(v344);
                              v397 = jag::shared_ptr<jag::oldscape::dash3d::ModelSource>::operator*(v361 + 24);
                              LODWORD(v395) = jag::oldscape::dash3d::world::m_cameraSinX;
                              LODWORD(v389) = jag::oldscape::dash3d::world::m_cameraCosX;
                              LODWORD(v392) = jag::oldscape::dash3d::world::m_cameraSinY;
                              LODWORD(v396) = jag::oldscape::dash3d::world::m_cameraCosY;
                              v362 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Wall>::operator->(v344) + 4)
                                   - jag::oldscape::dash3d::world::m_cx;
                              v363 = *(_DWORD *)jag::shared_ptr<jag::oldscape::dash3d::Wall>::operator->(v344)
                                   - jag::oldscape::dash3d::world::m_cy;
                              v364 = *(_DWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Wall>::operator->(v344) + 8)
                                   - jag::oldscape::dash3d::world::m_cz;
                              v365 = *(_QWORD *)(jag::shared_ptr<jag::oldscape::dash3d::Wall>::operator->(v344) + 56);
                              v366 = jag::shared_ptr<jag::oldscape::dash3d::Wall>::operator->(v344);
                              jag::oldscape::dash3d::world::SubmitModelForRender(
                                v394,
                                (jag::oldscape::dash3d::ModelSource *)v397,
                                0,
                                v395,
                                (_DWORD)v389,
                                (_DWORD)v392,
                                (_DWORD)v396,
                                v362,
                                v363,
                                v364,
                                v365,
                                v391,
                                (const jag::oldscape::dash3d::EntityHighlight *)(v366 + 64));
                            }
                          }
                        }
                      }
                    }
                    v5 = v394;
                    if ( (int)v386 < *((_DWORD *)v394 + 5450) - 1 )
                    {
                      v367 = jag::oldscape::dash3d::world::GetSquare(v394, (int)v386 + 1, (_DWORD)v399, v398[0]);
                      if ( (unsigned __int8)jag::operator!=<jag::oldscape::ClientObj>(v367, 0) )
                      {
                        if ( *(_BYTE *)(jag::shared_ptr<jag::oldscape::dash3d::Square>::operator->(v367) + 353) )
                          jag::SharedPtrList<jag::oldscape::dash3d::Square>::push(v382);
                      }
                    }
                    if ( (int)v399 < jag::oldscape::dash3d::world::m_gx )
                    {
                      v368 = jag::oldscape::dash3d::world::GetSquare(v5, v386, (int)v399 + 1, v398[0]);
                      if ( (unsigned __int8)jag::operator!=<jag::oldscape::ClientObj>(v368, 0) )
                      {
                        if ( *(_BYTE *)(jag::shared_ptr<jag::oldscape::dash3d::Square>::operator->(v368) + 353) )
                          jag::SharedPtrList<jag::oldscape::dash3d::Square>::push(v382);
                      }
                    }
                    if ( v398[0] < jag::oldscape::dash3d::world::m_gz )
                    {
                      v369 = jag::oldscape::dash3d::world::GetSquare(v5, v386, (_DWORD)v399, v398[0] + 1);
                      if ( (unsigned __int8)jag::operator!=<jag::oldscape::ClientObj>(v369, 0) )
                      {
                        if ( *(_BYTE *)(jag::shared_ptr<jag::oldscape::dash3d::Square>::operator->(v369) + 353) )
                          jag::SharedPtrList<jag::oldscape::dash3d::Square>::push(v382);
                      }
                    }
                    if ( (int)v399 > jag::oldscape::dash3d::world::m_gx )
                    {
                      v370 = jag::oldscape::dash3d::world::GetSquare(v5, v386, (int)v399 - 1, v398[0]);
                      if ( (unsigned __int8)jag::operator!=<jag::oldscape::ClientObj>(v370, 0) )
                      {
                        if ( *(_BYTE *)(jag::shared_ptr<jag::oldscape::dash3d::Square>::operator->(v370) + 353) )
                          jag::SharedPtrList<jag::oldscape::dash3d::Square>::push(v382);
                      }
                    }
                    if ( v398[0] > jag::oldscape::dash3d::world::m_gz )
                    {
                      v371 = jag::oldscape::dash3d::world::GetSquare(v5, v386, (_DWORD)v399, v398[0] - 1);
                      if ( (unsigned __int8)jag::operator!=<jag::oldscape::ClientObj>(v371, 0) )
                      {
                        if ( *(_BYTE *)(jag::shared_ptr<jag::oldscape::dash3d::Square>::operator->(v371) + 353) )
                        {
                          jag::SharedPtrList<jag::oldscape::dash3d::Square>::push(v382);
                          a3 = v400;
                          goto LABEL_5;
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
      else
      {
        v5 = v394;
        if ( *((_BYTE *)v389 + 353) )
          goto LABEL_245;
      }
      a3 = v400;
LABEL_5:
      jag::shared_ptr<jag::oldscape::FileRSevenDebugPresetProvider>::~shared_ptr(v373);
      v6 = v382;
      result = jag::SharedPtrList<jag::oldscape::dash3d::Square>::empty(v382);
    }
    while ( !(_BYTE)result );
  }
  return result;
}