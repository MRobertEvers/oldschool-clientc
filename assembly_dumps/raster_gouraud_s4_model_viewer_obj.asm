
build_release/CMakeFiles/model_viewer.dir/src/gouraud.c.o:     file format elf64-x86-64


Disassembly of section .text:

0000000000000fb0 <raster_gouraud_s4>:
     fb0:	f3 0f 1e fa          	endbr64
     fb4:	41 57                	push   %r15
     fb6:	45 89 c3             	mov    %r8d,%r11d
     fb9:	41 56                	push   %r14
     fbb:	41 55                	push   %r13
     fbd:	41 54                	push   %r12
     fbf:	41 89 cc             	mov    %ecx,%r12d
     fc2:	55                   	push   %rbp
     fc3:	89 f5                	mov    %esi,%ebp
     fc5:	44 89 ce             	mov    %r9d,%esi
     fc8:	45 29 e3             	sub    %r12d,%r11d
     fcb:	53                   	push   %rbx
     fcc:	89 f3                	mov    %esi,%ebx
     fce:	29 cb                	sub    %ecx,%ebx
     fd0:	89 d8                	mov    %ebx,%eax
     fd2:	48 83 ec 38          	sub    $0x38,%rsp
     fd6:	44 8b 4c 24 70       	mov    0x70(%rsp),%r9d
     fdb:	8b 8c 24 80 00 00 00 	mov    0x80(%rsp),%ecx
     fe2:	89 54 24 1c          	mov    %edx,0x1c(%rsp)
     fe6:	44 8b 54 24 78       	mov    0x78(%rsp),%r10d
     feb:	44 29 c9             	sub    %r9d,%ecx
     fee:	45 29 ca             	sub    %r9d,%r10d
     ff1:	89 ca                	mov    %ecx,%edx
     ff3:	41 0f af d3          	imul   %r11d,%edx
     ff7:	41 0f af c2          	imul   %r10d,%eax
     ffb:	29 c2                	sub    %eax,%edx
     ffd:	0f 84 eb 02 00 00    	je     12ee <raster_gouraud_s4+0x33e>
    1003:	8b 84 24 90 00 00 00 	mov    0x90(%rsp),%eax
    100a:	2b 84 24 88 00 00 00 	sub    0x88(%rsp),%eax
    1011:	41 89 d7             	mov    %edx,%r15d
    1014:	44 8b b4 24 98 00 00 	mov    0x98(%rsp),%r14d
    101b:	00 
    101c:	89 44 24 08          	mov    %eax,0x8(%rsp)
    1020:	44 2b b4 24 88 00 00 	sub    0x88(%rsp),%r14d
    1027:	00 
    1028:	44 3b 4c 24 78       	cmp    0x78(%rsp),%r9d
    102d:	0f 8e cd 02 00 00    	jle    1300 <raster_gouraud_s4+0x350>
    1033:	44 39 8c 24 80 00 00 	cmp    %r9d,0x80(%rsp)
    103a:	00 
    103b:	0f 8c 37 03 00 00    	jl     1378 <raster_gouraud_s4+0x3c8>
    1041:	44 29 c6             	sub    %r8d,%esi
    1044:	8b 94 24 80 00 00 00 	mov    0x80(%rsp),%edx
    104b:	8b 84 24 80 00 00 00 	mov    0x80(%rsp),%eax
    1052:	41 89 cd             	mov    %ecx,%r13d
    1055:	89 74 24 20          	mov    %esi,0x20(%rsp)
    1059:	8b b4 24 88 00 00 00 	mov    0x88(%rsp),%esi
    1060:	2b 44 24 78          	sub    0x78(%rsp),%eax
    1064:	44 89 8c 24 80 00 00 	mov    %r9d,0x80(%rsp)
    106b:	00 
    106c:	89 b4 24 98 00 00 00 	mov    %esi,0x98(%rsp)
    1073:	8b b4 24 90 00 00 00 	mov    0x90(%rsp),%esi
    107a:	89 44 24 0c          	mov    %eax,0xc(%rsp)
    107e:	44 8b 4c 24 78       	mov    0x78(%rsp),%r9d
    1083:	89 d8                	mov    %ebx,%eax
    1085:	89 b4 24 88 00 00 00 	mov    %esi,0x88(%rsp)
    108c:	44 89 e6             	mov    %r12d,%esi
    108f:	45 89 c4             	mov    %r8d,%r12d
    1092:	89 54 24 78          	mov    %edx,0x78(%rsp)
    1096:	44 39 8c 24 80 00 00 	cmp    %r9d,0x80(%rsp)
    109d:	00 
    109e:	0f 8d 2c 03 00 00    	jge    13d0 <raster_gouraud_s4+0x420>
    10a4:	44 89 e2             	mov    %r12d,%edx
    10a7:	45 31 c0             	xor    %r8d,%r8d
    10aa:	29 f2                	sub    %esi,%edx
    10ac:	89 54 24 18          	mov    %edx,0x18(%rsp)
    10b0:	44 89 ca             	mov    %r9d,%edx
    10b3:	2b 94 24 80 00 00 00 	sub    0x80(%rsp),%edx
    10ba:	89 54 24 14          	mov    %edx,0x14(%rsp)
    10be:	45 85 ed             	test   %r13d,%r13d
    10c1:	7e 1c                	jle    10df <raster_gouraud_s4+0x12f>
    10c3:	c1 e0 08             	shl    $0x8,%eax
    10c6:	99                   	cltd
    10c7:	41 f7 fd             	idiv   %r13d
    10ca:	41 89 c0             	mov    %eax,%r8d
    10cd:	44 8b 6c 24 14       	mov    0x14(%rsp),%r13d
    10d2:	c7 44 24 10 00 00 00 	movl   $0x0,0x10(%rsp)
    10d9:	00 
    10da:	45 85 ed             	test   %r13d,%r13d
    10dd:	7e 10                	jle    10ef <raster_gouraud_s4+0x13f>
    10df:	8b 44 24 18          	mov    0x18(%rsp),%eax
    10e3:	c1 e0 08             	shl    $0x8,%eax
    10e6:	99                   	cltd
    10e7:	f7 7c 24 14          	idivl  0x14(%rsp)
    10eb:	89 44 24 10          	mov    %eax,0x10(%rsp)
    10ef:	8b 54 24 0c          	mov    0xc(%rsp),%edx
    10f3:	c7 44 24 18 00 00 00 	movl   $0x0,0x18(%rsp)
    10fa:	00 
    10fb:	85 d2                	test   %edx,%edx
    10fd:	7e 12                	jle    1111 <raster_gouraud_s4+0x161>
    10ff:	8b 44 24 20          	mov    0x20(%rsp),%eax
    1103:	41 89 d5             	mov    %edx,%r13d
    1106:	c1 e0 08             	shl    $0x8,%eax
    1109:	99                   	cltd
    110a:	41 f7 fd             	idiv   %r13d
    110d:	89 44 24 18          	mov    %eax,0x18(%rsp)
    1111:	8b 44 24 08          	mov    0x8(%rsp),%eax
    1115:	45 0f af d6          	imul   %r14d,%r10d
    1119:	41 89 f5             	mov    %esi,%r13d
    111c:	41 c1 e4 08          	shl    $0x8,%r12d
    1120:	45 0f af de          	imul   %r14d,%r11d
    1124:	44 8b b4 24 98 00 00 	mov    0x98(%rsp),%r14d
    112b:	00 
    112c:	41 c1 e5 08          	shl    $0x8,%r13d
    1130:	0f af c1             	imul   %ecx,%eax
    1133:	41 c1 e6 08          	shl    $0x8,%r14d
    1137:	44 29 d0             	sub    %r10d,%eax
    113a:	44 8b 94 24 80 00 00 	mov    0x80(%rsp),%r10d
    1141:	00 
    1142:	c1 e0 08             	shl    $0x8,%eax
    1145:	99                   	cltd
    1146:	41 f7 ff             	idiv   %r15d
    1149:	8b 54 24 08          	mov    0x8(%rsp),%edx
    114d:	0f af d3             	imul   %ebx,%edx
    1150:	89 44 24 0c          	mov    %eax,0xc(%rsp)
    1154:	44 89 d8             	mov    %r11d,%eax
    1157:	29 d0                	sub    %edx,%eax
    1159:	c1 e0 08             	shl    $0x8,%eax
    115c:	99                   	cltd
    115d:	41 f7 ff             	idiv   %r15d
    1160:	89 44 24 08          	mov    %eax,0x8(%rsp)
    1164:	8b 44 24 0c          	mov    0xc(%rsp),%eax
    1168:	0f af f0             	imul   %eax,%esi
    116b:	41 01 c6             	add    %eax,%r14d
    116e:	41 29 f6             	sub    %esi,%r14d
    1171:	45 85 d2             	test   %r10d,%r10d
    1174:	0f 88 ee 02 00 00    	js     1468 <raster_gouraud_s4+0x4b8>
    117a:	8b 84 24 80 00 00 00 	mov    0x80(%rsp),%eax
    1181:	45 89 eb             	mov    %r13d,%r11d
    1184:	0f af c5             	imul   %ebp,%eax
    1187:	45 85 c9             	test   %r9d,%r9d
    118a:	0f 88 b0 02 00 00    	js     1440 <raster_gouraud_s4+0x490>
    1190:	8b 5c 24 1c          	mov    0x1c(%rsp),%ebx
    1194:	8d 53 ff             	lea    -0x1(%rbx),%edx
    1197:	44 39 cb             	cmp    %r9d,%ebx
    119a:	44 0f 4e ca          	cmovle %edx,%r9d
    119e:	44 39 8c 24 80 00 00 	cmp    %r9d,0x80(%rsp)
    11a5:	00 
    11a6:	0f 8d fd 02 00 00    	jge    14a9 <raster_gouraud_s4+0x4f9>
    11ac:	8b 9c 24 80 00 00 00 	mov    0x80(%rsp),%ebx
    11b3:	44 89 44 24 14       	mov    %r8d,0x14(%rsp)
    11b8:	44 89 f2             	mov    %r14d,%edx
    11bb:	45 89 ef             	mov    %r13d,%r15d
    11be:	44 89 5c 24 20       	mov    %r11d,0x20(%rsp)
    11c3:	41 89 d5             	mov    %edx,%r13d
    11c6:	89 44 24 28          	mov    %eax,0x28(%rsp)
    11ca:	44 89 4c 24 70       	mov    %r9d,0x70(%rsp)
    11cf:	44 89 64 24 24       	mov    %r12d,0x24(%rsp)
    11d4:	41 89 dc             	mov    %ebx,%r12d
    11d7:	44 89 db             	mov    %r11d,%ebx
    11da:	44 89 74 24 2c       	mov    %r14d,0x2c(%rsp)
    11df:	41 89 c6             	mov    %eax,%r14d
    11e2:	66 0f 1f 44 00 00    	nopw   0x0(%rax,%rax,1)
    11e8:	48 83 ec 08          	sub    $0x8,%rsp
    11ec:	89 d9                	mov    %ebx,%ecx
    11ee:	45 89 f8             	mov    %r15d,%r8d
    11f1:	44 89 f6             	mov    %r14d,%esi
    11f4:	8b 44 24 14          	mov    0x14(%rsp),%eax
    11f8:	c1 f9 08             	sar    $0x8,%ecx
    11fb:	45 89 e9             	mov    %r13d,%r9d
    11fe:	41 c1 f8 08          	sar    $0x8,%r8d
    1202:	89 ea                	mov    %ebp,%edx
    1204:	41 01 ee             	add    %ebp,%r14d
    1207:	41 83 c4 01          	add    $0x1,%r12d
    120b:	50                   	push   %rax
    120c:	e8 00 00 00 00       	call   1211 <raster_gouraud_s4+0x261>
    1211:	8b 44 24 24          	mov    0x24(%rsp),%eax
    1215:	01 c3                	add    %eax,%ebx
    1217:	8b 44 24 20          	mov    0x20(%rsp),%eax
    121b:	41 01 c7             	add    %eax,%r15d
    121e:	8b 44 24 18          	mov    0x18(%rsp),%eax
    1222:	59                   	pop    %rcx
    1223:	5e                   	pop    %rsi
    1224:	41 01 c5             	add    %eax,%r13d
    1227:	44 39 64 24 70       	cmp    %r12d,0x70(%rsp)
    122c:	75 ba                	jne    11e8 <raster_gouraud_s4+0x238>
    122e:	44 89 e3             	mov    %r12d,%ebx
    1231:	89 ee                	mov    %ebp,%esi
    1233:	8b 44 24 28          	mov    0x28(%rsp),%eax
    1237:	44 8b 44 24 14       	mov    0x14(%rsp),%r8d
    123c:	89 da                	mov    %ebx,%edx
    123e:	44 8b 5c 24 20       	mov    0x20(%rsp),%r11d
    1243:	44 8b 74 24 2c       	mov    0x2c(%rsp),%r14d
    1248:	2b 94 24 80 00 00 00 	sub    0x80(%rsp),%edx
    124f:	01 e8                	add    %ebp,%eax
    1251:	44 8b 64 24 24       	mov    0x24(%rsp),%r12d
    1256:	83 ea 01             	sub    $0x1,%edx
    1259:	45 01 c3             	add    %r8d,%r11d
    125c:	0f af f2             	imul   %edx,%esi
    125f:	01 f0                	add    %esi,%eax
    1261:	89 d6                	mov    %edx,%esi
    1263:	41 0f af f0          	imul   %r8d,%esi
    1267:	41 01 f3             	add    %esi,%r11d
    126a:	8b 74 24 08          	mov    0x8(%rsp),%esi
    126e:	0f af d6             	imul   %esi,%edx
    1271:	41 01 f6             	add    %esi,%r14d
    1274:	41 01 d6             	add    %edx,%r14d
    1277:	3b 5c 24 78          	cmp    0x78(%rsp),%ebx
    127b:	7d 71                	jge    12ee <raster_gouraud_s4+0x33e>
    127d:	8b 74 24 1c          	mov    0x1c(%rsp),%esi
    1281:	3b 74 24 78          	cmp    0x78(%rsp),%esi
    1285:	7f 0b                	jg     1292 <raster_gouraud_s4+0x2e2>
    1287:	83 ee 01             	sub    $0x1,%esi
    128a:	89 74 24 78          	mov    %esi,0x78(%rsp)
    128e:	39 de                	cmp    %ebx,%esi
    1290:	7e 5c                	jle    12ee <raster_gouraud_s4+0x33e>
    1292:	44 89 44 24 10       	mov    %r8d,0x10(%rsp)
    1297:	45 89 f5             	mov    %r14d,%r13d
    129a:	41 89 c7             	mov    %eax,%r15d
    129d:	41 89 de             	mov    %ebx,%r14d
    12a0:	44 89 db             	mov    %r11d,%ebx
    12a3:	0f 1f 44 00 00       	nopl   0x0(%rax,%rax,1)
    12a8:	48 83 ec 08          	sub    $0x8,%rsp
    12ac:	89 d9                	mov    %ebx,%ecx
    12ae:	45 89 e0             	mov    %r12d,%r8d
    12b1:	45 89 e9             	mov    %r13d,%r9d
    12b4:	8b 44 24 14          	mov    0x14(%rsp),%eax
    12b8:	89 ea                	mov    %ebp,%edx
    12ba:	44 89 fe             	mov    %r15d,%esi
    12bd:	c1 f9 08             	sar    $0x8,%ecx
    12c0:	41 c1 f8 08          	sar    $0x8,%r8d
    12c4:	41 01 ef             	add    %ebp,%r15d
    12c7:	41 83 c6 01          	add    $0x1,%r14d
    12cb:	50                   	push   %rax
    12cc:	e8 00 00 00 00       	call   12d1 <raster_gouraud_s4+0x321>
    12d1:	8b 44 24 20          	mov    0x20(%rsp),%eax
    12d5:	01 c3                	add    %eax,%ebx
    12d7:	8b 44 24 28          	mov    0x28(%rsp),%eax
    12db:	41 01 c4             	add    %eax,%r12d
    12de:	8b 44 24 18          	mov    0x18(%rsp),%eax
    12e2:	41 01 c5             	add    %eax,%r13d
    12e5:	58                   	pop    %rax
    12e6:	5a                   	pop    %rdx
    12e7:	44 3b 74 24 78       	cmp    0x78(%rsp),%r14d
    12ec:	75 ba                	jne    12a8 <raster_gouraud_s4+0x2f8>
    12ee:	48 83 c4 38          	add    $0x38,%rsp
    12f2:	5b                   	pop    %rbx
    12f3:	5d                   	pop    %rbp
    12f4:	41 5c                	pop    %r12
    12f6:	41 5d                	pop    %r13
    12f8:	41 5e                	pop    %r14
    12fa:	41 5f                	pop    %r15
    12fc:	c3                   	ret
    12fd:	0f 1f 00             	nopl   (%rax)
    1300:	8b 44 24 78          	mov    0x78(%rsp),%eax
    1304:	39 84 24 80 00 00 00 	cmp    %eax,0x80(%rsp)
    130b:	0f 8c 1f 01 00 00    	jl     1430 <raster_gouraud_s4+0x480>
    1311:	8b 94 24 80 00 00 00 	mov    0x80(%rsp),%edx
    1318:	44 29 c6             	sub    %r8d,%esi
    131b:	44 89 54 24 14       	mov    %r10d,0x14(%rsp)
    1320:	41 89 cd             	mov    %ecx,%r13d
    1323:	8b 84 24 80 00 00 00 	mov    0x80(%rsp),%eax
    132a:	2b 44 24 78          	sub    0x78(%rsp),%eax
    132e:	89 74 24 20          	mov    %esi,0x20(%rsp)
    1332:	44 89 8c 24 80 00 00 	mov    %r9d,0x80(%rsp)
    1339:	00 
    133a:	8b b4 24 88 00 00 00 	mov    0x88(%rsp),%esi
    1341:	44 8b 4c 24 78       	mov    0x78(%rsp),%r9d
    1346:	44 89 5c 24 18       	mov    %r11d,0x18(%rsp)
    134b:	89 54 24 78          	mov    %edx,0x78(%rsp)
    134f:	89 44 24 0c          	mov    %eax,0xc(%rsp)
    1353:	89 d8                	mov    %ebx,%eax
    1355:	89 b4 24 98 00 00 00 	mov    %esi,0x98(%rsp)
    135c:	44 89 e6             	mov    %r12d,%esi
    135f:	45 89 c4             	mov    %r8d,%r12d
    1362:	45 31 c0             	xor    %r8d,%r8d
    1365:	45 85 ed             	test   %r13d,%r13d
    1368:	0f 8f 55 fd ff ff    	jg     10c3 <raster_gouraud_s4+0x113>
    136e:	e9 5a fd ff ff       	jmp    10cd <raster_gouraud_s4+0x11d>
    1373:	0f 1f 44 00 00       	nopl   0x0(%rax,%rax,1)
    1378:	44 89 e0             	mov    %r12d,%eax
    137b:	44 29 c0             	sub    %r8d,%eax
    137e:	89 44 24 20          	mov    %eax,0x20(%rsp)
    1382:	44 89 c8             	mov    %r9d,%eax
    1385:	2b 44 24 78          	sub    0x78(%rsp),%eax
    1389:	89 44 24 0c          	mov    %eax,0xc(%rsp)
    138d:	8b 84 24 90 00 00 00 	mov    0x90(%rsp),%eax
    1394:	89 84 24 88 00 00 00 	mov    %eax,0x88(%rsp)
    139b:	44 89 c8             	mov    %r9d,%eax
    139e:	44 8b 4c 24 78       	mov    0x78(%rsp),%r9d
    13a3:	89 44 24 78          	mov    %eax,0x78(%rsp)
    13a7:	44 89 e0             	mov    %r12d,%eax
    13aa:	45 89 c4             	mov    %r8d,%r12d
    13ad:	41 89 c0             	mov    %eax,%r8d
    13b0:	44 89 c0             	mov    %r8d,%eax
    13b3:	44 8b 6c 24 78       	mov    0x78(%rsp),%r13d
    13b8:	44 2b ac 24 80 00 00 	sub    0x80(%rsp),%r13d
    13bf:	00 
    13c0:	29 f0                	sub    %esi,%eax
    13c2:	44 39 8c 24 80 00 00 	cmp    %r9d,0x80(%rsp)
    13c9:	00 
    13ca:	0f 8c d4 fc ff ff    	jl     10a4 <raster_gouraud_s4+0xf4>
    13d0:	89 f2                	mov    %esi,%edx
    13d2:	44 29 e2             	sub    %r12d,%edx
    13d5:	89 54 24 18          	mov    %edx,0x18(%rsp)
    13d9:	8b 94 24 80 00 00 00 	mov    0x80(%rsp),%edx
    13e0:	44 29 ca             	sub    %r9d,%edx
    13e3:	89 54 24 14          	mov    %edx,0x14(%rsp)
    13e7:	44 89 ea             	mov    %r13d,%edx
    13ea:	44 8b 6c 24 0c       	mov    0xc(%rsp),%r13d
    13ef:	89 54 24 0c          	mov    %edx,0xc(%rsp)
    13f3:	89 c2                	mov    %eax,%edx
    13f5:	8b 44 24 20          	mov    0x20(%rsp),%eax
    13f9:	89 54 24 20          	mov    %edx,0x20(%rsp)
    13fd:	8b 94 24 88 00 00 00 	mov    0x88(%rsp),%edx
    1404:	89 94 24 98 00 00 00 	mov    %edx,0x98(%rsp)
    140b:	8b 94 24 80 00 00 00 	mov    0x80(%rsp),%edx
    1412:	44 89 8c 24 80 00 00 	mov    %r9d,0x80(%rsp)
    1419:	00 
    141a:	41 89 d1             	mov    %edx,%r9d
    141d:	89 f2                	mov    %esi,%edx
    141f:	44 89 e6             	mov    %r12d,%esi
    1422:	41 89 d4             	mov    %edx,%r12d
    1425:	e9 38 ff ff ff       	jmp    1362 <raster_gouraud_s4+0x3b2>
    142a:	66 0f 1f 44 00 00    	nopw   0x0(%rax,%rax,1)
    1430:	44 89 54 24 0c       	mov    %r10d,0xc(%rsp)
    1435:	44 89 5c 24 20       	mov    %r11d,0x20(%rsp)
    143a:	e9 71 ff ff ff       	jmp    13b0 <raster_gouraud_s4+0x400>
    143f:	90                   	nop
    1440:	8b 54 24 18          	mov    0x18(%rsp),%edx
    1444:	8b 74 24 1c          	mov    0x1c(%rsp),%esi
    1448:	bb 01 00 00 00       	mov    $0x1,%ebx
    144d:	41 0f af d1          	imul   %r9d,%edx
    1451:	41 29 d4             	sub    %edx,%r12d
    1454:	85 f6                	test   %esi,%esi
    1456:	0f 4e de             	cmovle %esi,%ebx
    1459:	83 eb 01             	sub    $0x1,%ebx
    145c:	e9 16 fe ff ff       	jmp    1277 <raster_gouraud_s4+0x2c7>
    1461:	0f 1f 80 00 00 00 00 	nopl   0x0(%rax)
    1468:	8b 84 24 80 00 00 00 	mov    0x80(%rsp),%eax
    146f:	45 89 eb             	mov    %r13d,%r11d
    1472:	41 0f af c0          	imul   %r8d,%eax
    1476:	41 29 c3             	sub    %eax,%r11d
    1479:	8b 44 24 10          	mov    0x10(%rsp),%eax
    147d:	0f af 84 24 80 00 00 	imul   0x80(%rsp),%eax
    1484:	00 
    1485:	41 29 c5             	sub    %eax,%r13d
    1488:	8b 44 24 08          	mov    0x8(%rsp),%eax
    148c:	0f af 84 24 80 00 00 	imul   0x80(%rsp),%eax
    1493:	00 
    1494:	c7 84 24 80 00 00 00 	movl   $0x0,0x80(%rsp)
    149b:	00 00 00 00 
    149f:	41 29 c6             	sub    %eax,%r14d
    14a2:	31 c0                	xor    %eax,%eax
    14a4:	e9 de fc ff ff       	jmp    1187 <raster_gouraud_s4+0x1d7>
    14a9:	44 89 cb             	mov    %r9d,%ebx
    14ac:	e9 c6 fd ff ff       	jmp    1277 <raster_gouraud_s4+0x2c7>
