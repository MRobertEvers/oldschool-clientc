
build_release/CMakeFiles/model_viewer.dir/src/gouraud_deob.c.o:     file format elf64-x86-64


Disassembly of section .text:

00000000000000c0 <gouraud_deob_draw_triangle>:
      c0:	f3 0f 1e fa          	endbr64
      c4:	41 57                	push   %r15
      c6:	41 89 f3             	mov    %esi,%r11d
      c9:	41 89 d2             	mov    %edx,%r10d
      cc:	41 56                	push   %r14
      ce:	41 55                	push   %r13
      d0:	41 89 cd             	mov    %ecx,%r13d
      d3:	29 f1                	sub    %esi,%ecx
      d5:	41 54                	push   %r12
      d7:	55                   	push   %rbp
      d8:	53                   	push   %rbx
      d9:	44 89 cb             	mov    %r9d,%ebx
      dc:	44 29 c3             	sub    %r8d,%ebx
      df:	48 83 ec 48          	sub    $0x48,%rsp
      e3:	44 8b b4 24 80 00 00 	mov    0x80(%rsp),%r14d
      ea:	00 
      eb:	48 89 7c 24 10       	mov    %rdi,0x10(%rsp)
      f0:	89 d7                	mov    %edx,%edi
      f2:	29 f7                	sub    %esi,%edi
      f4:	44 89 f5             	mov    %r14d,%ebp
      f7:	44 29 c5             	sub    %r8d,%ebp
      fa:	44 39 ea             	cmp    %r13d,%edx
      fd:	0f 84 e5 02 00 00    	je     3e8 <gouraud_deob_draw_triangle+0x328>
     103:	44 89 f0             	mov    %r14d,%eax
     106:	44 89 ee             	mov    %r13d,%esi
     109:	44 29 c8             	sub    %r9d,%eax
     10c:	29 d6                	sub    %edx,%esi
     10e:	c1 e0 0e             	shl    $0xe,%eax
     111:	99                   	cltd
     112:	f7 fe                	idiv   %esi
     114:	89 44 24 20          	mov    %eax,0x20(%rsp)
     118:	45 39 da             	cmp    %r11d,%r10d
     11b:	0f 84 a7 02 00 00    	je     3c8 <gouraud_deob_draw_triangle+0x308>
     121:	89 d8                	mov    %ebx,%eax
     123:	c7 44 24 0c 00 00 00 	movl   $0x0,0xc(%rsp)
     12a:	00 
     12b:	c1 e0 0e             	shl    $0xe,%eax
     12e:	99                   	cltd
     12f:	f7 ff                	idiv   %edi
     131:	89 44 24 18          	mov    %eax,0x18(%rsp)
     135:	45 39 eb             	cmp    %r13d,%r11d
     138:	0f 85 92 02 00 00    	jne    3d0 <gouraud_deob_draw_triangle+0x310>
     13e:	89 de                	mov    %ebx,%esi
     140:	89 f8                	mov    %edi,%eax
     142:	0f af f1             	imul   %ecx,%esi
     145:	0f af c5             	imul   %ebp,%eax
     148:	29 c6                	sub    %eax,%esi
     14a:	0f 84 65 02 00 00    	je     3b5 <gouraud_deob_draw_triangle+0x2f5>
     150:	44 8b a4 24 90 00 00 	mov    0x90(%rsp),%r12d
     157:	00 
     158:	44 8b bc 24 98 00 00 	mov    0x98(%rsp),%r15d
     15f:	00 
     160:	44 2b a4 24 88 00 00 	sub    0x88(%rsp),%r12d
     167:	00 
     168:	44 2b bc 24 88 00 00 	sub    0x88(%rsp),%r15d
     16f:	00 
     170:	41 0f af cc          	imul   %r12d,%ecx
     174:	41 0f af ff          	imul   %r15d,%edi
     178:	41 0f af df          	imul   %r15d,%ebx
     17c:	44 0f af e5          	imul   %ebp,%r12d
     180:	29 f9                	sub    %edi,%ecx
     182:	c1 e1 08             	shl    $0x8,%ecx
     185:	89 c8                	mov    %ecx,%eax
     187:	44 29 e3             	sub    %r12d,%ebx
     18a:	99                   	cltd
     18b:	c1 e3 08             	shl    $0x8,%ebx
     18e:	f7 fe                	idiv   %esi
     190:	89 44 24 1c          	mov    %eax,0x1c(%rsp)
     194:	89 d8                	mov    %ebx,%eax
     196:	99                   	cltd
     197:	f7 fe                	idiv   %esi
     199:	45 39 ea             	cmp    %r13d,%r10d
     19c:	89 c3                	mov    %eax,%ebx
     19e:	44 89 e8             	mov    %r13d,%eax
     1a1:	41 0f 4e c2          	cmovle %r10d,%eax
     1a5:	41 39 c3             	cmp    %eax,%r11d
     1a8:	0f 8f 62 02 00 00    	jg     410 <gouraud_deob_draw_triangle+0x350>
     1ae:	41 81 fb 7f 01 00 00 	cmp    $0x17f,%r11d
     1b5:	0f 8f fa 01 00 00    	jg     3b5 <gouraud_deob_draw_triangle+0x2f5>
     1bb:	b8 80 01 00 00       	mov    $0x180,%eax
     1c0:	8b 54 24 1c          	mov    0x1c(%rsp),%edx
     1c4:	8b ac 24 88 00 00 00 	mov    0x88(%rsp),%ebp
     1cb:	41 39 c2             	cmp    %eax,%r10d
     1ce:	89 c1                	mov    %eax,%ecx
     1d0:	89 c7                	mov    %eax,%edi
     1d2:	41 0f 4e ca          	cmovle %r10d,%ecx
     1d6:	41 39 c5             	cmp    %eax,%r13d
     1d9:	89 d0                	mov    %edx,%eax
     1db:	41 0f 4e fd          	cmovle %r13d,%edi
     1df:	c1 e5 08             	shl    $0x8,%ebp
     1e2:	41 0f af c0          	imul   %r8d,%eax
     1e6:	41 c1 e0 0e          	shl    $0xe,%r8d
     1ea:	45 89 c4             	mov    %r8d,%r12d
     1ed:	29 c5                	sub    %eax,%ebp
     1ef:	01 d5                	add    %edx,%ebp
     1f1:	39 cf                	cmp    %ecx,%edi
     1f3:	0f 8e 57 06 00 00    	jle    850 <gouraud_deob_draw_triangle+0x790>
     1f9:	45 85 db             	test   %r11d,%r11d
     1fc:	0f 88 9e 0b 00 00    	js     da0 <gouraud_deob_draw_triangle+0xce0>
     202:	44 89 d8             	mov    %r11d,%eax
     205:	45 89 c6             	mov    %r8d,%r14d
     208:	c1 e0 09             	shl    $0x9,%eax
     20b:	8b 54 24 18          	mov    0x18(%rsp),%edx
     20f:	41 c1 e1 0e          	shl    $0xe,%r9d
     213:	39 54 24 0c          	cmp    %edx,0xc(%rsp)
     217:	45 89 cf             	mov    %r9d,%r15d
     21a:	0f 9d c2             	setge  %dl
     21d:	45 85 d2             	test   %r10d,%r10d
     220:	0f 88 9a 0d 00 00    	js     fc0 <gouraud_deob_draw_triangle+0xf00>
     226:	29 cf                	sub    %ecx,%edi
     228:	41 39 cb             	cmp    %ecx,%r11d
     22b:	0f 84 4f 15 00 00    	je     1780 <gouraud_deob_draw_triangle+0x16c0>
     231:	84 d2                	test   %dl,%dl
     233:	0f 85 c7 13 00 00    	jne    1600 <gouraud_deob_draw_triangle+0x1540>
     239:	44 29 d9             	sub    %r11d,%ecx
     23c:	41 89 ca             	mov    %ecx,%r10d
     23f:	83 e9 01             	sub    $0x1,%ecx
     242:	0f 88 e7 00 00 00    	js     32f <gouraud_deob_draw_triangle+0x26f>
     248:	41 c1 e2 09          	shl    $0x9,%r10d
     24c:	89 7c 24 28          	mov    %edi,0x28(%rsp)
     250:	41 89 e8             	mov    %ebp,%r8d
     253:	45 89 f5             	mov    %r14d,%r13d
     256:	44 89 4c 24 30       	mov    %r9d,0x30(%rsp)
     25b:	41 01 c2             	add    %eax,%r10d
     25e:	41 89 db             	mov    %ebx,%r11d
     261:	89 c6                	mov    %eax,%esi
     263:	89 44 24 34          	mov    %eax,0x34(%rsp)
     267:	45 89 c7             	mov    %r8d,%r15d
     26a:	44 89 d3             	mov    %r10d,%ebx
     26d:	89 4c 24 3c          	mov    %ecx,0x3c(%rsp)
     271:	89 6c 24 2c          	mov    %ebp,0x2c(%rsp)
     275:	8b 6c 24 0c          	mov    0xc(%rsp),%ebp
     279:	44 89 74 24 38       	mov    %r14d,0x38(%rsp)
     27e:	44 8b 74 24 18       	mov    0x18(%rsp),%r14d
     283:	0f 1f 44 00 00       	nopl   0x0(%rax,%rax,1)
     288:	44 89 e1             	mov    %r12d,%ecx
     28b:	44 89 ea             	mov    %r13d,%edx
     28e:	44 89 e0             	mov    %r12d,%eax
     291:	c1 f9 0e             	sar    $0xe,%ecx
     294:	c1 fa 0e             	sar    $0xe,%edx
     297:	44 09 e8             	or     %r13d,%eax
     29a:	78 41                	js     2dd <gouraud_deob_draw_triangle+0x21d>
     29c:	81 fa 00 02 00 00    	cmp    $0x200,%edx
     2a2:	40 0f 9f c7          	setg   %dil
     2a6:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
     2ac:	41 0f 9f c1          	setg   %r9b
     2b0:	44 08 cf             	or     %r9b,%dil
     2b3:	75 28                	jne    2dd <gouraud_deob_draw_triangle+0x21d>
     2b5:	39 d1                	cmp    %edx,%ecx
     2b7:	7e 24                	jle    2dd <gouraud_deob_draw_triangle+0x21d>
     2b9:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
     2be:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
     2c3:	45 89 f8             	mov    %r15d,%r8d
     2c6:	89 74 24 18          	mov    %esi,0x18(%rsp)
     2ca:	44 89 5c 24 24       	mov    %r11d,0x24(%rsp)
     2cf:	e8 2c fd ff ff       	call   0 <gouraud_deob_draw_scanline.part.0>
     2d4:	44 8b 5c 24 24       	mov    0x24(%rsp),%r11d
     2d9:	8b 74 24 18          	mov    0x18(%rsp),%esi
     2dd:	81 c6 00 02 00 00    	add    $0x200,%esi
     2e3:	41 01 ed             	add    %ebp,%r13d
     2e6:	45 01 f4             	add    %r14d,%r12d
     2e9:	45 01 df             	add    %r11d,%r15d
     2ec:	39 f3                	cmp    %esi,%ebx
     2ee:	75 98                	jne    288 <gouraud_deob_draw_triangle+0x1c8>
     2f0:	8b 4c 24 3c          	mov    0x3c(%rsp),%ecx
     2f4:	8b 74 24 0c          	mov    0xc(%rsp),%esi
     2f8:	44 89 da             	mov    %r11d,%edx
     2fb:	44 89 db             	mov    %r11d,%ebx
     2fe:	44 8b 74 24 38       	mov    0x38(%rsp),%r14d
     303:	8b 6c 24 2c          	mov    0x2c(%rsp),%ebp
     307:	0f af d1             	imul   %ecx,%edx
     30a:	8b 44 24 34          	mov    0x34(%rsp),%eax
     30e:	8b 7c 24 28          	mov    0x28(%rsp),%edi
     312:	41 01 f6             	add    %esi,%r14d
     315:	0f af f1             	imul   %ecx,%esi
     318:	44 01 dd             	add    %r11d,%ebp
     31b:	c1 e1 09             	shl    $0x9,%ecx
     31e:	44 8b 7c 24 30       	mov    0x30(%rsp),%r15d
     323:	8d 84 08 00 02 00 00 	lea    0x200(%rax,%rcx,1),%eax
     32a:	01 d5                	add    %edx,%ebp
     32c:	41 01 f6             	add    %esi,%r14d
     32f:	c1 e7 09             	shl    $0x9,%edi
     332:	41 89 e8             	mov    %ebp,%r8d
     335:	41 89 c5             	mov    %eax,%r13d
     338:	8b 6c 24 20          	mov    0x20(%rsp),%ebp
     33c:	41 89 fc             	mov    %edi,%r12d
     33f:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
     344:	41 01 c4             	add    %eax,%r12d
     347:	89 d8                	mov    %ebx,%eax
     349:	8b 5c 24 0c          	mov    0xc(%rsp),%ebx
     34d:	0f 1f 00             	nopl   (%rax)
     350:	44 89 f9             	mov    %r15d,%ecx
     353:	44 89 f2             	mov    %r14d,%edx
     356:	44 89 fe             	mov    %r15d,%esi
     359:	c1 f9 0e             	sar    $0xe,%ecx
     35c:	c1 fa 0e             	sar    $0xe,%edx
     35f:	44 09 f6             	or     %r14d,%esi
     362:	78 3c                	js     3a0 <gouraud_deob_draw_triangle+0x2e0>
     364:	81 fa 00 02 00 00    	cmp    $0x200,%edx
     36a:	40 0f 9f c6          	setg   %sil
     36e:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
     374:	41 0f 9f c1          	setg   %r9b
     378:	44 08 ce             	or     %r9b,%sil
     37b:	75 23                	jne    3a0 <gouraud_deob_draw_triangle+0x2e0>
     37d:	39 d1                	cmp    %edx,%ecx
     37f:	7e 1f                	jle    3a0 <gouraud_deob_draw_triangle+0x2e0>
     381:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
     386:	44 89 ee             	mov    %r13d,%esi
     389:	89 44 24 18          	mov    %eax,0x18(%rsp)
     38d:	44 89 44 24 0c       	mov    %r8d,0xc(%rsp)
     392:	e8 69 fc ff ff       	call   0 <gouraud_deob_draw_scanline.part.0>
     397:	8b 44 24 18          	mov    0x18(%rsp),%eax
     39b:	44 8b 44 24 0c       	mov    0xc(%rsp),%r8d
     3a0:	41 81 c5 00 02 00 00 	add    $0x200,%r13d
     3a7:	41 01 de             	add    %ebx,%r14d
     3aa:	41 01 ef             	add    %ebp,%r15d
     3ad:	41 01 c0             	add    %eax,%r8d
     3b0:	45 39 ec             	cmp    %r13d,%r12d
     3b3:	75 9b                	jne    350 <gouraud_deob_draw_triangle+0x290>
     3b5:	48 83 c4 48          	add    $0x48,%rsp
     3b9:	5b                   	pop    %rbx
     3ba:	5d                   	pop    %rbp
     3bb:	41 5c                	pop    %r12
     3bd:	41 5d                	pop    %r13
     3bf:	41 5e                	pop    %r14
     3c1:	41 5f                	pop    %r15
     3c3:	c3                   	ret
     3c4:	0f 1f 40 00          	nopl   0x0(%rax)
     3c8:	c7 44 24 18 00 00 00 	movl   $0x0,0x18(%rsp)
     3cf:	00 
     3d0:	89 e8                	mov    %ebp,%eax
     3d2:	c1 e0 0e             	shl    $0xe,%eax
     3d5:	99                   	cltd
     3d6:	f7 f9                	idiv   %ecx
     3d8:	89 44 24 0c          	mov    %eax,0xc(%rsp)
     3dc:	e9 5d fd ff ff       	jmp    13e <gouraud_deob_draw_triangle+0x7e>
     3e1:	0f 1f 80 00 00 00 00 	nopl   0x0(%rax)
     3e8:	39 f2                	cmp    %esi,%edx
     3ea:	0f 84 40 04 00 00    	je     830 <gouraud_deob_draw_triangle+0x770>
     3f0:	89 d8                	mov    %ebx,%eax
     3f2:	c7 44 24 20 00 00 00 	movl   $0x0,0x20(%rsp)
     3f9:	00 
     3fa:	c1 e0 0e             	shl    $0xe,%eax
     3fd:	99                   	cltd
     3fe:	f7 ff                	idiv   %edi
     400:	89 44 24 18          	mov    %eax,0x18(%rsp)
     404:	eb ca                	jmp    3d0 <gouraud_deob_draw_triangle+0x310>
     406:	66 2e 0f 1f 84 00 00 	cs nopw 0x0(%rax,%rax,1)
     40d:	00 00 00 
     410:	45 39 ea             	cmp    %r13d,%r10d
     413:	0f 8f 17 02 00 00    	jg     630 <gouraud_deob_draw_triangle+0x570>
     419:	41 81 fa 7f 01 00 00 	cmp    $0x17f,%r10d
     420:	7f 93                	jg     3b5 <gouraud_deob_draw_triangle+0x2f5>
     422:	b8 80 01 00 00       	mov    $0x180,%eax
     427:	8b 7c 24 1c          	mov    0x1c(%rsp),%edi
     42b:	41 39 c5             	cmp    %eax,%r13d
     42e:	41 0f 4e c5          	cmovle %r13d,%eax
     432:	89 fa                	mov    %edi,%edx
     434:	89 c1                	mov    %eax,%ecx
     436:	b8 80 01 00 00       	mov    $0x180,%eax
     43b:	41 39 c3             	cmp    %eax,%r11d
     43e:	41 0f 4e c3          	cmovle %r11d,%eax
     442:	41 0f af d1          	imul   %r9d,%edx
     446:	41 c1 e1 0e          	shl    $0xe,%r9d
     44a:	44 89 cd             	mov    %r9d,%ebp
     44d:	89 c6                	mov    %eax,%esi
     44f:	8b 84 24 90 00 00 00 	mov    0x90(%rsp),%eax
     456:	c1 e0 08             	shl    $0x8,%eax
     459:	29 d0                	sub    %edx,%eax
     45b:	01 c7                	add    %eax,%edi
     45d:	39 f1                	cmp    %esi,%ecx
     45f:	0f 8d bb 05 00 00    	jge    a20 <gouraud_deob_draw_triangle+0x960>
     465:	45 85 d2             	test   %r10d,%r10d
     468:	0f 88 62 0d 00 00    	js     11d0 <gouraud_deob_draw_triangle+0x1110>
     46e:	45 89 d7             	mov    %r10d,%r15d
     471:	45 89 cc             	mov    %r9d,%r12d
     474:	41 c1 e7 09          	shl    $0x9,%r15d
     478:	8b 54 24 18          	mov    0x18(%rsp),%edx
     47c:	41 c1 e6 0e          	shl    $0xe,%r14d
     480:	39 54 24 20          	cmp    %edx,0x20(%rsp)
     484:	0f 9e c0             	setle  %al
     487:	45 85 ed             	test   %r13d,%r13d
     48a:	0f 88 98 0b 00 00    	js     1028 <gouraud_deob_draw_triangle+0xf68>
     490:	29 ce                	sub    %ecx,%esi
     492:	41 39 ca             	cmp    %ecx,%r10d
     495:	0f 84 7c 14 00 00    	je     1917 <gouraud_deob_draw_triangle+0x1857>
     49b:	84 c0                	test   %al,%al
     49d:	0f 85 f5 12 00 00    	jne    1798 <gouraud_deob_draw_triangle+0x16d8>
     4a3:	89 c8                	mov    %ecx,%eax
     4a5:	44 29 d0             	sub    %r10d,%eax
     4a8:	89 c1                	mov    %eax,%ecx
     4aa:	83 e9 01             	sub    $0x1,%ecx
     4ad:	0f 88 e8 00 00 00    	js     59b <gouraud_deob_draw_triangle+0x4db>
     4b3:	c1 e0 09             	shl    $0x9,%eax
     4b6:	44 89 7c 24 30       	mov    %r15d,0x30(%rsp)
     4bb:	45 89 e5             	mov    %r12d,%r13d
     4be:	89 4c 24 34          	mov    %ecx,0x34(%rsp)
     4c2:	46 8d 14 38          	lea    (%rax,%r15,1),%r10d
     4c6:	89 d8                	mov    %ebx,%eax
     4c8:	8b 5c 24 18          	mov    0x18(%rsp),%ebx
     4cc:	89 7c 24 38          	mov    %edi,0x38(%rsp)
     4d0:	89 74 24 3c          	mov    %esi,0x3c(%rsp)
     4d4:	44 89 64 24 2c       	mov    %r12d,0x2c(%rsp)
     4d9:	44 8b 64 24 20       	mov    0x20(%rsp),%r12d
     4de:	44 89 74 24 28       	mov    %r14d,0x28(%rsp)
     4e3:	41 89 fe             	mov    %edi,%r14d
     4e6:	66 2e 0f 1f 84 00 00 	cs nopw 0x0(%rax,%rax,1)
     4ed:	00 00 00 
     4f0:	89 e9                	mov    %ebp,%ecx
     4f2:	44 89 ea             	mov    %r13d,%edx
     4f5:	89 ef                	mov    %ebp,%edi
     4f7:	c1 f9 0e             	sar    $0xe,%ecx
     4fa:	c1 fa 0e             	sar    $0xe,%edx
     4fd:	44 09 ef             	or     %r13d,%edi
     500:	78 44                	js     546 <gouraud_deob_draw_triangle+0x486>
     502:	81 fa 00 02 00 00    	cmp    $0x200,%edx
     508:	40 0f 9f c7          	setg   %dil
     50c:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
     512:	41 0f 9f c1          	setg   %r9b
     516:	44 08 cf             	or     %r9b,%dil
     519:	75 2b                	jne    546 <gouraud_deob_draw_triangle+0x486>
     51b:	39 d1                	cmp    %edx,%ecx
     51d:	7e 27                	jle    546 <gouraud_deob_draw_triangle+0x486>
     51f:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
     524:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
     529:	45 89 f0             	mov    %r14d,%r8d
     52c:	44 89 fe             	mov    %r15d,%esi
     52f:	89 44 24 24          	mov    %eax,0x24(%rsp)
     533:	44 89 54 24 20       	mov    %r10d,0x20(%rsp)
     538:	e8 c3 fa ff ff       	call   0 <gouraud_deob_draw_scanline.part.0>
     53d:	8b 44 24 24          	mov    0x24(%rsp),%eax
     541:	44 8b 54 24 20       	mov    0x20(%rsp),%r10d
     546:	41 81 c7 00 02 00 00 	add    $0x200,%r15d
     54d:	41 01 dd             	add    %ebx,%r13d
     550:	44 01 e5             	add    %r12d,%ebp
     553:	41 01 c6             	add    %eax,%r14d
     556:	45 39 d7             	cmp    %r10d,%r15d
     559:	75 95                	jne    4f0 <gouraud_deob_draw_triangle+0x430>
     55b:	8b 4c 24 34          	mov    0x34(%rsp),%ecx
     55f:	89 c3                	mov    %eax,%ebx
     561:	44 8b 64 24 2c       	mov    0x2c(%rsp),%r12d
     566:	8b 44 24 18          	mov    0x18(%rsp),%eax
     56a:	89 da                	mov    %ebx,%edx
     56c:	8b 7c 24 38          	mov    0x38(%rsp),%edi
     570:	0f af d1             	imul   %ecx,%edx
     573:	44 8b 7c 24 30       	mov    0x30(%rsp),%r15d
     578:	44 8b 74 24 28       	mov    0x28(%rsp),%r14d
     57d:	41 01 c4             	add    %eax,%r12d
     580:	0f af c1             	imul   %ecx,%eax
     583:	8b 74 24 3c          	mov    0x3c(%rsp),%esi
     587:	c1 e1 09             	shl    $0x9,%ecx
     58a:	45 8d bc 0f 00 02 00 	lea    0x200(%r15,%rcx,1),%r15d
     591:	00 
     592:	41 01 c4             	add    %eax,%r12d
     595:	8d 04 1f             	lea    (%rdi,%rbx,1),%eax
     598:	8d 3c 02             	lea    (%rdx,%rax,1),%edi
     59b:	c1 e6 09             	shl    $0x9,%esi
     59e:	41 89 f8             	mov    %edi,%r8d
     5a1:	89 d8                	mov    %ebx,%eax
     5a3:	44 8b 6c 24 0c       	mov    0xc(%rsp),%r13d
     5a8:	89 f5                	mov    %esi,%ebp
     5aa:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
     5af:	8b 5c 24 18          	mov    0x18(%rsp),%ebx
     5b3:	44 01 fd             	add    %r15d,%ebp
     5b6:	66 2e 0f 1f 84 00 00 	cs nopw 0x0(%rax,%rax,1)
     5bd:	00 00 00 
     5c0:	44 89 f1             	mov    %r14d,%ecx
     5c3:	44 89 e2             	mov    %r12d,%edx
     5c6:	44 89 f6             	mov    %r14d,%esi
     5c9:	c1 f9 0e             	sar    $0xe,%ecx
     5cc:	c1 fa 0e             	sar    $0xe,%edx
     5cf:	44 09 e6             	or     %r12d,%esi
     5d2:	78 3c                	js     610 <gouraud_deob_draw_triangle+0x550>
     5d4:	81 fa 00 02 00 00    	cmp    $0x200,%edx
     5da:	40 0f 9f c6          	setg   %sil
     5de:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
     5e4:	41 0f 9f c1          	setg   %r9b
     5e8:	44 08 ce             	or     %r9b,%sil
     5eb:	75 23                	jne    610 <gouraud_deob_draw_triangle+0x550>
     5ed:	39 d1                	cmp    %edx,%ecx
     5ef:	7e 1f                	jle    610 <gouraud_deob_draw_triangle+0x550>
     5f1:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
     5f6:	44 89 fe             	mov    %r15d,%esi
     5f9:	89 44 24 18          	mov    %eax,0x18(%rsp)
     5fd:	44 89 44 24 0c       	mov    %r8d,0xc(%rsp)
     602:	e8 f9 f9 ff ff       	call   0 <gouraud_deob_draw_scanline.part.0>
     607:	8b 44 24 18          	mov    0x18(%rsp),%eax
     60b:	44 8b 44 24 0c       	mov    0xc(%rsp),%r8d
     610:	41 81 c7 00 02 00 00 	add    $0x200,%r15d
     617:	41 01 dc             	add    %ebx,%r12d
     61a:	45 01 ee             	add    %r13d,%r14d
     61d:	41 01 c0             	add    %eax,%r8d
     620:	41 39 ef             	cmp    %ebp,%r15d
     623:	75 9b                	jne    5c0 <gouraud_deob_draw_triangle+0x500>
     625:	e9 8b fd ff ff       	jmp    3b5 <gouraud_deob_draw_triangle+0x2f5>
     62a:	66 0f 1f 44 00 00    	nopw   0x0(%rax,%rax,1)
     630:	41 81 fd 7f 01 00 00 	cmp    $0x17f,%r13d
     637:	0f 8f 78 fd ff ff    	jg     3b5 <gouraud_deob_draw_triangle+0x2f5>
     63d:	b8 80 01 00 00       	mov    $0x180,%eax
     642:	8b 7c 24 1c          	mov    0x1c(%rsp),%edi
     646:	41 39 c3             	cmp    %eax,%r11d
     649:	41 0f 4e c3          	cmovle %r11d,%eax
     64d:	89 fa                	mov    %edi,%edx
     64f:	89 c1                	mov    %eax,%ecx
     651:	b8 80 01 00 00       	mov    $0x180,%eax
     656:	41 39 c2             	cmp    %eax,%r10d
     659:	41 0f 4e c2          	cmovle %r10d,%eax
     65d:	41 0f af d6          	imul   %r14d,%edx
     661:	41 c1 e6 0e          	shl    $0xe,%r14d
     665:	89 c6                	mov    %eax,%esi
     667:	8b 84 24 98 00 00 00 	mov    0x98(%rsp),%eax
     66e:	c1 e0 08             	shl    $0x8,%eax
     671:	29 d0                	sub    %edx,%eax
     673:	01 c7                	add    %eax,%edi
     675:	39 ce                	cmp    %ecx,%esi
     677:	0f 8e 63 05 00 00    	jle    be0 <gouraud_deob_draw_triangle+0xb20>
     67d:	45 85 ed             	test   %r13d,%r13d
     680:	0f 88 ea 0a 00 00    	js     1170 <gouraud_deob_draw_triangle+0x10b0>
     686:	45 89 ef             	mov    %r13d,%r15d
     689:	44 89 f5             	mov    %r14d,%ebp
     68c:	41 c1 e7 09          	shl    $0x9,%r15d
     690:	41 c1 e0 0e          	shl    $0xe,%r8d
     694:	45 89 c4             	mov    %r8d,%r12d
     697:	45 85 db             	test   %r11d,%r11d
     69a:	0f 88 a0 0a 00 00    	js     1140 <gouraud_deob_draw_triangle+0x1080>
     6a0:	89 c8                	mov    %ecx,%eax
     6a2:	8b 54 24 0c          	mov    0xc(%rsp),%edx
     6a6:	44 8b 44 24 20       	mov    0x20(%rsp),%r8d
     6ab:	29 ce                	sub    %ecx,%esi
     6ad:	44 29 e8             	sub    %r13d,%eax
     6b0:	8d 48 ff             	lea    -0x1(%rax),%ecx
     6b3:	44 39 c2             	cmp    %r8d,%edx
     6b6:	0f 8e d7 0d 00 00    	jle    1493 <gouraud_deob_draw_triangle+0x13d3>
     6bc:	85 c9                	test   %ecx,%ecx
     6be:	0f 88 df 00 00 00    	js     7a3 <gouraud_deob_draw_triangle+0x6e3>
     6c4:	89 7c 24 34          	mov    %edi,0x34(%rsp)
     6c8:	45 89 fa             	mov    %r15d,%r10d
     6cb:	41 89 ed             	mov    %ebp,%r13d
     6ce:	41 89 db             	mov    %ebx,%r11d
     6d1:	89 4c 24 3c          	mov    %ecx,0x3c(%rsp)
     6d5:	89 d3                	mov    %edx,%ebx
     6d7:	89 6c 24 28          	mov    %ebp,0x28(%rsp)
     6db:	89 cd                	mov    %ecx,%ebp
     6dd:	44 89 64 24 2c       	mov    %r12d,0x2c(%rsp)
     6e2:	45 89 c4             	mov    %r8d,%r12d
     6e5:	44 89 7c 24 30       	mov    %r15d,0x30(%rsp)
     6ea:	41 89 ff             	mov    %edi,%r15d
     6ed:	89 74 24 38          	mov    %esi,0x38(%rsp)
     6f1:	44 89 d6             	mov    %r10d,%esi
     6f4:	0f 1f 40 00          	nopl   0x0(%rax)
     6f8:	44 89 f1             	mov    %r14d,%ecx
     6fb:	44 89 ea             	mov    %r13d,%edx
     6fe:	44 89 f0             	mov    %r14d,%eax
     701:	c1 f9 0e             	sar    $0xe,%ecx
     704:	c1 fa 0e             	sar    $0xe,%edx
     707:	44 09 e8             	or     %r13d,%eax
     70a:	78 41                	js     74d <gouraud_deob_draw_triangle+0x68d>
     70c:	81 fa 00 02 00 00    	cmp    $0x200,%edx
     712:	40 0f 9f c7          	setg   %dil
     716:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
     71c:	41 0f 9f c1          	setg   %r9b
     720:	44 08 cf             	or     %r9b,%dil
     723:	75 28                	jne    74d <gouraud_deob_draw_triangle+0x68d>
     725:	39 d1                	cmp    %edx,%ecx
     727:	7e 24                	jle    74d <gouraud_deob_draw_triangle+0x68d>
     729:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
     72e:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
     733:	45 89 f8             	mov    %r15d,%r8d
     736:	89 74 24 0c          	mov    %esi,0xc(%rsp)
     73a:	44 89 5c 24 24       	mov    %r11d,0x24(%rsp)
     73f:	e8 bc f8 ff ff       	call   0 <gouraud_deob_draw_scanline.part.0>
     744:	44 8b 5c 24 24       	mov    0x24(%rsp),%r11d
     749:	8b 74 24 0c          	mov    0xc(%rsp),%esi
     74d:	45 01 e5             	add    %r12d,%r13d
     750:	41 01 de             	add    %ebx,%r14d
     753:	45 01 df             	add    %r11d,%r15d
     756:	81 c6 00 02 00 00    	add    $0x200,%esi
     75c:	83 ed 01             	sub    $0x1,%ebp
     75f:	73 97                	jae    6f8 <gouraud_deob_draw_triangle+0x638>
     761:	8b 4c 24 3c          	mov    0x3c(%rsp),%ecx
     765:	8b 44 24 20          	mov    0x20(%rsp),%eax
     769:	44 89 da             	mov    %r11d,%edx
     76c:	44 89 db             	mov    %r11d,%ebx
     76f:	8b 6c 24 28          	mov    0x28(%rsp),%ebp
     773:	8b 7c 24 34          	mov    0x34(%rsp),%edi
     777:	0f af d1             	imul   %ecx,%edx
     77a:	44 8b 7c 24 30       	mov    0x30(%rsp),%r15d
     77f:	44 8b 64 24 2c       	mov    0x2c(%rsp),%r12d
     784:	01 c5                	add    %eax,%ebp
     786:	0f af c1             	imul   %ecx,%eax
     789:	8b 74 24 38          	mov    0x38(%rsp),%esi
     78d:	01 c5                	add    %eax,%ebp
     78f:	42 8d 04 1f          	lea    (%rdi,%r11,1),%eax
     793:	8d 3c 02             	lea    (%rdx,%rax,1),%edi
     796:	89 c8                	mov    %ecx,%eax
     798:	c1 e0 09             	shl    $0x9,%eax
     79b:	45 8d bc 07 00 02 00 	lea    0x200(%r15,%rax,1),%r15d
     7a2:	00 
     7a3:	c1 e6 09             	shl    $0x9,%esi
     7a6:	41 89 f8             	mov    %edi,%r8d
     7a9:	89 d8                	mov    %ebx,%eax
     7ab:	44 8b 74 24 20       	mov    0x20(%rsp),%r14d
     7b0:	41 89 f5             	mov    %esi,%r13d
     7b3:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
     7b8:	8b 5c 24 18          	mov    0x18(%rsp),%ebx
     7bc:	45 01 fd             	add    %r15d,%r13d
     7bf:	90                   	nop
     7c0:	44 89 e1             	mov    %r12d,%ecx
     7c3:	89 ea                	mov    %ebp,%edx
     7c5:	44 89 e6             	mov    %r12d,%esi
     7c8:	c1 f9 0e             	sar    $0xe,%ecx
     7cb:	c1 fa 0e             	sar    $0xe,%edx
     7ce:	09 ee                	or     %ebp,%esi
     7d0:	78 3c                	js     80e <gouraud_deob_draw_triangle+0x74e>
     7d2:	81 fa 00 02 00 00    	cmp    $0x200,%edx
     7d8:	40 0f 9f c6          	setg   %sil
     7dc:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
     7e2:	41 0f 9f c1          	setg   %r9b
     7e6:	44 08 ce             	or     %r9b,%sil
     7e9:	75 23                	jne    80e <gouraud_deob_draw_triangle+0x74e>
     7eb:	39 d1                	cmp    %edx,%ecx
     7ed:	7e 1f                	jle    80e <gouraud_deob_draw_triangle+0x74e>
     7ef:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
     7f4:	44 89 fe             	mov    %r15d,%esi
     7f7:	89 44 24 18          	mov    %eax,0x18(%rsp)
     7fb:	44 89 44 24 0c       	mov    %r8d,0xc(%rsp)
     800:	e8 fb f7 ff ff       	call   0 <gouraud_deob_draw_scanline.part.0>
     805:	8b 44 24 18          	mov    0x18(%rsp),%eax
     809:	44 8b 44 24 0c       	mov    0xc(%rsp),%r8d
     80e:	41 81 c7 00 02 00 00 	add    $0x200,%r15d
     815:	44 01 f5             	add    %r14d,%ebp
     818:	41 01 dc             	add    %ebx,%r12d
     81b:	41 01 c0             	add    %eax,%r8d
     81e:	45 39 fd             	cmp    %r15d,%r13d
     821:	75 9d                	jne    7c0 <gouraud_deob_draw_triangle+0x700>
     823:	e9 8d fb ff ff       	jmp    3b5 <gouraud_deob_draw_triangle+0x2f5>
     828:	0f 1f 84 00 00 00 00 	nopl   0x0(%rax,%rax,1)
     82f:	00 
     830:	c7 44 24 18 00 00 00 	movl   $0x0,0x18(%rsp)
     837:	00 
     838:	c7 44 24 20 00 00 00 	movl   $0x0,0x20(%rsp)
     83f:	00 
     840:	c7 44 24 0c 00 00 00 	movl   $0x0,0xc(%rsp)
     847:	00 
     848:	e9 f1 f8 ff ff       	jmp    13e <gouraud_deob_draw_triangle+0x7e>
     84d:	0f 1f 00             	nopl   (%rax)
     850:	45 85 db             	test   %r11d,%r11d
     853:	0f 88 77 05 00 00    	js     dd0 <gouraud_deob_draw_triangle+0xd10>
     859:	44 89 d8             	mov    %r11d,%eax
     85c:	8b 54 24 18          	mov    0x18(%rsp),%edx
     860:	41 c1 e6 0e          	shl    $0xe,%r14d
     864:	45 89 c7             	mov    %r8d,%r15d
     867:	c1 e0 09             	shl    $0x9,%eax
     86a:	39 54 24 0c          	cmp    %edx,0xc(%rsp)
     86e:	0f 9c c2             	setl   %dl
     871:	45 85 ed             	test   %r13d,%r13d
     874:	0f 88 86 07 00 00    	js     1000 <gouraud_deob_draw_triangle+0xf40>
     87a:	29 f9                	sub    %edi,%ecx
     87c:	44 39 df             	cmp    %r11d,%edi
     87f:	0f 84 ac 05 00 00    	je     e31 <gouraud_deob_draw_triangle+0xd71>
     885:	84 d2                	test   %dl,%dl
     887:	0f 84 9f 05 00 00    	je     e2c <gouraud_deob_draw_triangle+0xd6c>
     88d:	44 29 df             	sub    %r11d,%edi
     890:	41 89 fa             	mov    %edi,%r10d
     893:	83 ef 01             	sub    $0x1,%edi
     896:	0f 88 ea 00 00 00    	js     986 <gouraud_deob_draw_triangle+0x8c6>
     89c:	89 44 24 30          	mov    %eax,0x30(%rsp)
     8a0:	41 89 e8             	mov    %ebp,%r8d
     8a3:	41 c1 e2 09          	shl    $0x9,%r10d
     8a7:	41 89 db             	mov    %ebx,%r11d
     8aa:	89 7c 24 34          	mov    %edi,0x34(%rsp)
     8ae:	8b 5c 24 18          	mov    0x18(%rsp),%ebx
     8b2:	45 89 fd             	mov    %r15d,%r13d
     8b5:	41 01 c2             	add    %eax,%r10d
     8b8:	89 4c 24 38          	mov    %ecx,0x38(%rsp)
     8bc:	89 6c 24 2c          	mov    %ebp,0x2c(%rsp)
     8c0:	8b 6c 24 0c          	mov    0xc(%rsp),%ebp
     8c4:	44 89 74 24 28       	mov    %r14d,0x28(%rsp)
     8c9:	45 89 c6             	mov    %r8d,%r14d
     8cc:	44 89 7c 24 3c       	mov    %r15d,0x3c(%rsp)
     8d1:	41 89 c7             	mov    %eax,%r15d
     8d4:	0f 1f 40 00          	nopl   0x0(%rax)
     8d8:	44 89 e9             	mov    %r13d,%ecx
     8db:	44 89 e2             	mov    %r12d,%edx
     8de:	44 89 e8             	mov    %r13d,%eax
     8e1:	c1 f9 0e             	sar    $0xe,%ecx
     8e4:	c1 fa 0e             	sar    $0xe,%edx
     8e7:	44 09 e0             	or     %r12d,%eax
     8ea:	78 46                	js     932 <gouraud_deob_draw_triangle+0x872>
     8ec:	81 fa 00 02 00 00    	cmp    $0x200,%edx
     8f2:	40 0f 9f c7          	setg   %dil
     8f6:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
     8fc:	41 0f 9f c1          	setg   %r9b
     900:	44 08 cf             	or     %r9b,%dil
     903:	75 2d                	jne    932 <gouraud_deob_draw_triangle+0x872>
     905:	39 d1                	cmp    %edx,%ecx
     907:	7e 29                	jle    932 <gouraud_deob_draw_triangle+0x872>
     909:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
     90e:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
     913:	45 89 f0             	mov    %r14d,%r8d
     916:	44 89 fe             	mov    %r15d,%esi
     919:	44 89 5c 24 24       	mov    %r11d,0x24(%rsp)
     91e:	44 89 54 24 0c       	mov    %r10d,0xc(%rsp)
     923:	e8 d8 f6 ff ff       	call   0 <gouraud_deob_draw_scanline.part.0>
     928:	44 8b 5c 24 24       	mov    0x24(%rsp),%r11d
     92d:	44 8b 54 24 0c       	mov    0xc(%rsp),%r10d
     932:	41 81 c7 00 02 00 00 	add    $0x200,%r15d
     939:	41 01 ec             	add    %ebp,%r12d
     93c:	41 01 dd             	add    %ebx,%r13d
     93f:	45 01 de             	add    %r11d,%r14d
     942:	45 39 d7             	cmp    %r10d,%r15d
     945:	75 91                	jne    8d8 <gouraud_deob_draw_triangle+0x818>
     947:	8b 7c 24 34          	mov    0x34(%rsp),%edi
     94b:	8b 74 24 18          	mov    0x18(%rsp),%esi
     94f:	44 89 da             	mov    %r11d,%edx
     952:	44 89 db             	mov    %r11d,%ebx
     955:	44 8b 7c 24 3c       	mov    0x3c(%rsp),%r15d
     95a:	8b 6c 24 2c          	mov    0x2c(%rsp),%ebp
     95e:	0f af d7             	imul   %edi,%edx
     961:	8b 44 24 30          	mov    0x30(%rsp),%eax
     965:	44 8b 74 24 28       	mov    0x28(%rsp),%r14d
     96a:	41 01 f7             	add    %esi,%r15d
     96d:	0f af f7             	imul   %edi,%esi
     970:	44 01 dd             	add    %r11d,%ebp
     973:	c1 e7 09             	shl    $0x9,%edi
     976:	8b 4c 24 38          	mov    0x38(%rsp),%ecx
     97a:	8d 84 38 00 02 00 00 	lea    0x200(%rax,%rdi,1),%eax
     981:	01 d5                	add    %edx,%ebp
     983:	41 01 f7             	add    %esi,%r15d
     986:	85 c9                	test   %ecx,%ecx
     988:	0f 8e 27 fa ff ff    	jle    3b5 <gouraud_deob_draw_triangle+0x2f5>
     98e:	c1 e1 09             	shl    $0x9,%ecx
     991:	41 89 e8             	mov    %ebp,%r8d
     994:	41 89 c5             	mov    %eax,%r13d
     997:	8b 6c 24 18          	mov    0x18(%rsp),%ebp
     99b:	41 89 cc             	mov    %ecx,%r12d
     99e:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
     9a3:	41 01 c4             	add    %eax,%r12d
     9a6:	89 d8                	mov    %ebx,%eax
     9a8:	8b 5c 24 20          	mov    0x20(%rsp),%ebx
     9ac:	0f 1f 40 00          	nopl   0x0(%rax)
     9b0:	44 89 f9             	mov    %r15d,%ecx
     9b3:	44 89 f2             	mov    %r14d,%edx
     9b6:	44 89 fe             	mov    %r15d,%esi
     9b9:	c1 f9 0e             	sar    $0xe,%ecx
     9bc:	c1 fa 0e             	sar    $0xe,%edx
     9bf:	44 09 f6             	or     %r14d,%esi
     9c2:	78 3c                	js     a00 <gouraud_deob_draw_triangle+0x940>
     9c4:	81 fa 00 02 00 00    	cmp    $0x200,%edx
     9ca:	40 0f 9f c6          	setg   %sil
     9ce:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
     9d4:	41 0f 9f c1          	setg   %r9b
     9d8:	44 08 ce             	or     %r9b,%sil
     9db:	75 23                	jne    a00 <gouraud_deob_draw_triangle+0x940>
     9dd:	39 d1                	cmp    %edx,%ecx
     9df:	7e 1f                	jle    a00 <gouraud_deob_draw_triangle+0x940>
     9e1:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
     9e6:	44 89 ee             	mov    %r13d,%esi
     9e9:	89 44 24 18          	mov    %eax,0x18(%rsp)
     9ed:	44 89 44 24 0c       	mov    %r8d,0xc(%rsp)
     9f2:	e8 09 f6 ff ff       	call   0 <gouraud_deob_draw_scanline.part.0>
     9f7:	8b 44 24 18          	mov    0x18(%rsp),%eax
     9fb:	44 8b 44 24 0c       	mov    0xc(%rsp),%r8d
     a00:	41 81 c5 00 02 00 00 	add    $0x200,%r13d
     a07:	41 01 de             	add    %ebx,%r14d
     a0a:	41 01 ef             	add    %ebp,%r15d
     a0d:	41 01 c0             	add    %eax,%r8d
     a10:	45 39 ec             	cmp    %r13d,%r12d
     a13:	75 9b                	jne    9b0 <gouraud_deob_draw_triangle+0x8f0>
     a15:	e9 9b f9 ff ff       	jmp    3b5 <gouraud_deob_draw_triangle+0x2f5>
     a1a:	66 0f 1f 44 00 00    	nopw   0x0(%rax,%rax,1)
     a20:	45 85 d2             	test   %r10d,%r10d
     a23:	0f 88 77 07 00 00    	js     11a0 <gouraud_deob_draw_triangle+0x10e0>
     a29:	45 89 d6             	mov    %r10d,%r14d
     a2c:	45 89 cd             	mov    %r9d,%r13d
     a2f:	41 c1 e6 09          	shl    $0x9,%r14d
     a33:	41 c1 e0 0e          	shl    $0xe,%r8d
     a37:	45 89 c4             	mov    %r8d,%r12d
     a3a:	45 85 db             	test   %r11d,%r11d
     a3d:	0f 88 bd 07 00 00    	js     1200 <gouraud_deob_draw_triangle+0x1140>
     a43:	89 f0                	mov    %esi,%eax
     a45:	29 f1                	sub    %esi,%ecx
     a47:	44 29 d0             	sub    %r10d,%eax
     a4a:	44 8b 54 24 18       	mov    0x18(%rsp),%r10d
     a4f:	8d 70 ff             	lea    -0x1(%rax),%esi
     a52:	44 39 54 24 20       	cmp    %r10d,0x20(%rsp)
     a57:	0f 8e 4d 08 00 00    	jle    12aa <gouraud_deob_draw_triangle+0x11ea>
     a5d:	85 f6                	test   %esi,%esi
     a5f:	0f 88 e8 00 00 00    	js     b4d <gouraud_deob_draw_triangle+0xa8d>
     a65:	44 89 44 24 28       	mov    %r8d,0x28(%rsp)
     a6a:	45 89 f2             	mov    %r14d,%r10d
     a6d:	45 89 ef             	mov    %r13d,%r15d
     a70:	41 89 db             	mov    %ebx,%r11d
     a73:	89 4c 24 2c          	mov    %ecx,0x2c(%rsp)
     a77:	8b 5c 24 20          	mov    0x20(%rsp),%ebx
     a7b:	41 89 f4             	mov    %esi,%r12d
     a7e:	89 7c 24 38          	mov    %edi,0x38(%rsp)
     a82:	44 89 6c 24 34       	mov    %r13d,0x34(%rsp)
     a87:	44 8b 6c 24 18       	mov    0x18(%rsp),%r13d
     a8c:	44 89 74 24 30       	mov    %r14d,0x30(%rsp)
     a91:	41 89 fe             	mov    %edi,%r14d
     a94:	89 74 24 3c          	mov    %esi,0x3c(%rsp)
     a98:	44 89 d6             	mov    %r10d,%esi
     a9b:	0f 1f 44 00 00       	nopl   0x0(%rax,%rax,1)
     aa0:	44 89 f9             	mov    %r15d,%ecx
     aa3:	89 ea                	mov    %ebp,%edx
     aa5:	44 89 f8             	mov    %r15d,%eax
     aa8:	c1 f9 0e             	sar    $0xe,%ecx
     aab:	c1 fa 0e             	sar    $0xe,%edx
     aae:	09 e8                	or     %ebp,%eax
     ab0:	78 41                	js     af3 <gouraud_deob_draw_triangle+0xa33>
     ab2:	81 fa 00 02 00 00    	cmp    $0x200,%edx
     ab8:	40 0f 9f c7          	setg   %dil
     abc:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
     ac2:	41 0f 9f c1          	setg   %r9b
     ac6:	44 08 cf             	or     %r9b,%dil
     ac9:	75 28                	jne    af3 <gouraud_deob_draw_triangle+0xa33>
     acb:	39 d1                	cmp    %edx,%ecx
     acd:	7e 24                	jle    af3 <gouraud_deob_draw_triangle+0xa33>
     acf:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
     ad4:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
     ad9:	45 89 f0             	mov    %r14d,%r8d
     adc:	89 74 24 18          	mov    %esi,0x18(%rsp)
     ae0:	44 89 5c 24 24       	mov    %r11d,0x24(%rsp)
     ae5:	e8 16 f5 ff ff       	call   0 <gouraud_deob_draw_scanline.part.0>
     aea:	44 8b 5c 24 24       	mov    0x24(%rsp),%r11d
     aef:	8b 74 24 18          	mov    0x18(%rsp),%esi
     af3:	44 01 ed             	add    %r13d,%ebp
     af6:	41 01 df             	add    %ebx,%r15d
     af9:	45 01 de             	add    %r11d,%r14d
     afc:	81 c6 00 02 00 00    	add    $0x200,%esi
     b02:	41 83 ec 01          	sub    $0x1,%r12d
     b06:	73 98                	jae    aa0 <gouraud_deob_draw_triangle+0x9e0>
     b08:	8b 74 24 3c          	mov    0x3c(%rsp),%esi
     b0c:	8b 44 24 20          	mov    0x20(%rsp),%eax
     b10:	44 89 da             	mov    %r11d,%edx
     b13:	44 89 db             	mov    %r11d,%ebx
     b16:	44 8b 6c 24 34       	mov    0x34(%rsp),%r13d
     b1b:	8b 7c 24 38          	mov    0x38(%rsp),%edi
     b1f:	0f af d6             	imul   %esi,%edx
     b22:	44 8b 74 24 30       	mov    0x30(%rsp),%r14d
     b27:	44 8b 64 24 28       	mov    0x28(%rsp),%r12d
     b2c:	41 01 c5             	add    %eax,%r13d
     b2f:	0f af c6             	imul   %esi,%eax
     b32:	8b 4c 24 2c          	mov    0x2c(%rsp),%ecx
     b36:	41 01 c5             	add    %eax,%r13d
     b39:	42 8d 04 1f          	lea    (%rdi,%r11,1),%eax
     b3d:	8d 3c 02             	lea    (%rdx,%rax,1),%edi
     b40:	89 f0                	mov    %esi,%eax
     b42:	c1 e0 09             	shl    $0x9,%eax
     b45:	45 8d b4 06 00 02 00 	lea    0x200(%r14,%rax,1),%r14d
     b4c:	00 
     b4d:	85 c9                	test   %ecx,%ecx
     b4f:	0f 8e 60 f8 ff ff    	jle    3b5 <gouraud_deob_draw_triangle+0x2f5>
     b55:	c1 e1 09             	shl    $0x9,%ecx
     b58:	41 89 f8             	mov    %edi,%r8d
     b5b:	89 d8                	mov    %ebx,%eax
     b5d:	44 8b 7c 24 0c       	mov    0xc(%rsp),%r15d
     b62:	89 cd                	mov    %ecx,%ebp
     b64:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
     b69:	8b 5c 24 20          	mov    0x20(%rsp),%ebx
     b6d:	44 01 f5             	add    %r14d,%ebp
     b70:	44 89 e9             	mov    %r13d,%ecx
     b73:	44 89 e2             	mov    %r12d,%edx
     b76:	44 89 ee             	mov    %r13d,%esi
     b79:	c1 f9 0e             	sar    $0xe,%ecx
     b7c:	c1 fa 0e             	sar    $0xe,%edx
     b7f:	44 09 e6             	or     %r12d,%esi
     b82:	78 3c                	js     bc0 <gouraud_deob_draw_triangle+0xb00>
     b84:	81 fa 00 02 00 00    	cmp    $0x200,%edx
     b8a:	40 0f 9f c6          	setg   %sil
     b8e:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
     b94:	41 0f 9f c1          	setg   %r9b
     b98:	44 08 ce             	or     %r9b,%sil
     b9b:	75 23                	jne    bc0 <gouraud_deob_draw_triangle+0xb00>
     b9d:	39 d1                	cmp    %edx,%ecx
     b9f:	7e 1f                	jle    bc0 <gouraud_deob_draw_triangle+0xb00>
     ba1:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
     ba6:	44 89 f6             	mov    %r14d,%esi
     ba9:	89 44 24 18          	mov    %eax,0x18(%rsp)
     bad:	44 89 44 24 0c       	mov    %r8d,0xc(%rsp)
     bb2:	e8 49 f4 ff ff       	call   0 <gouraud_deob_draw_scanline.part.0>
     bb7:	8b 44 24 18          	mov    0x18(%rsp),%eax
     bbb:	44 8b 44 24 0c       	mov    0xc(%rsp),%r8d
     bc0:	41 81 c6 00 02 00 00 	add    $0x200,%r14d
     bc7:	45 01 fc             	add    %r15d,%r12d
     bca:	41 01 dd             	add    %ebx,%r13d
     bcd:	41 01 c0             	add    %eax,%r8d
     bd0:	41 39 ee             	cmp    %ebp,%r14d
     bd3:	75 9b                	jne    b70 <gouraud_deob_draw_triangle+0xab0>
     bd5:	e9 db f7 ff ff       	jmp    3b5 <gouraud_deob_draw_triangle+0x2f5>
     bda:	66 0f 1f 44 00 00    	nopw   0x0(%rax,%rax,1)
     be0:	45 85 ed             	test   %r13d,%r13d
     be3:	0f 88 27 05 00 00    	js     1110 <gouraud_deob_draw_triangle+0x1050>
     be9:	45 89 ef             	mov    %r13d,%r15d
     bec:	44 89 f5             	mov    %r14d,%ebp
     bef:	41 c1 e7 09          	shl    $0x9,%r15d
     bf3:	41 c1 e1 0e          	shl    $0xe,%r9d
     bf7:	45 89 cc             	mov    %r9d,%r12d
     bfa:	45 85 d2             	test   %r10d,%r10d
     bfd:	0f 88 5d 04 00 00    	js     1060 <gouraud_deob_draw_triangle+0xfa0>
     c03:	29 f1                	sub    %esi,%ecx
     c05:	44 29 ee             	sub    %r13d,%esi
     c08:	8b 54 24 20          	mov    0x20(%rsp),%edx
     c0c:	44 8d 46 ff          	lea    -0x1(%rsi),%r8d
     c10:	39 54 24 0c          	cmp    %edx,0xc(%rsp)
     c14:	0f 8e 86 07 00 00    	jle    13a0 <gouraud_deob_draw_triangle+0x12e0>
     c1a:	45 85 c0             	test   %r8d,%r8d
     c1d:	0f 88 e5 00 00 00    	js     d08 <gouraud_deob_draw_triangle+0xc48>
     c23:	44 89 4c 24 28       	mov    %r9d,0x28(%rsp)
     c28:	41 89 ed             	mov    %ebp,%r13d
     c2b:	41 89 db             	mov    %ebx,%r11d
     c2e:	8b 5c 24 0c          	mov    0xc(%rsp),%ebx
     c32:	89 4c 24 2c          	mov    %ecx,0x2c(%rsp)
     c36:	44 89 fe             	mov    %r15d,%esi
     c39:	45 89 c4             	mov    %r8d,%r12d
     c3c:	89 7c 24 38          	mov    %edi,0x38(%rsp)
     c40:	44 89 44 24 3c       	mov    %r8d,0x3c(%rsp)
     c45:	89 6c 24 34          	mov    %ebp,0x34(%rsp)
     c49:	8b 6c 24 20          	mov    0x20(%rsp),%ebp
     c4d:	44 89 7c 24 30       	mov    %r15d,0x30(%rsp)
     c52:	41 89 ff             	mov    %edi,%r15d
     c55:	0f 1f 00             	nopl   (%rax)
     c58:	44 89 e9             	mov    %r13d,%ecx
     c5b:	44 89 f2             	mov    %r14d,%edx
     c5e:	44 89 e8             	mov    %r13d,%eax
     c61:	c1 f9 0e             	sar    $0xe,%ecx
     c64:	c1 fa 0e             	sar    $0xe,%edx
     c67:	44 09 f0             	or     %r14d,%eax
     c6a:	78 41                	js     cad <gouraud_deob_draw_triangle+0xbed>
     c6c:	81 fa 00 02 00 00    	cmp    $0x200,%edx
     c72:	40 0f 9f c7          	setg   %dil
     c76:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
     c7c:	41 0f 9f c1          	setg   %r9b
     c80:	44 08 cf             	or     %r9b,%dil
     c83:	75 28                	jne    cad <gouraud_deob_draw_triangle+0xbed>
     c85:	39 d1                	cmp    %edx,%ecx
     c87:	7e 24                	jle    cad <gouraud_deob_draw_triangle+0xbed>
     c89:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
     c8e:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
     c93:	45 89 f8             	mov    %r15d,%r8d
     c96:	89 74 24 20          	mov    %esi,0x20(%rsp)
     c9a:	44 89 5c 24 24       	mov    %r11d,0x24(%rsp)
     c9f:	e8 5c f3 ff ff       	call   0 <gouraud_deob_draw_scanline.part.0>
     ca4:	44 8b 5c 24 24       	mov    0x24(%rsp),%r11d
     ca9:	8b 74 24 20          	mov    0x20(%rsp),%esi
     cad:	41 01 ee             	add    %ebp,%r14d
     cb0:	41 01 dd             	add    %ebx,%r13d
     cb3:	45 01 df             	add    %r11d,%r15d
     cb6:	81 c6 00 02 00 00    	add    $0x200,%esi
     cbc:	41 83 ec 01          	sub    $0x1,%r12d
     cc0:	73 96                	jae    c58 <gouraud_deob_draw_triangle+0xb98>
     cc2:	44 8b 44 24 3c       	mov    0x3c(%rsp),%r8d
     cc7:	8b 44 24 0c          	mov    0xc(%rsp),%eax
     ccb:	44 89 da             	mov    %r11d,%edx
     cce:	44 89 db             	mov    %r11d,%ebx
     cd1:	8b 6c 24 34          	mov    0x34(%rsp),%ebp
     cd5:	8b 7c 24 38          	mov    0x38(%rsp),%edi
     cd9:	41 0f af d0          	imul   %r8d,%edx
     cdd:	44 8b 7c 24 30       	mov    0x30(%rsp),%r15d
     ce2:	44 8b 64 24 28       	mov    0x28(%rsp),%r12d
     ce7:	01 c5                	add    %eax,%ebp
     ce9:	41 0f af c0          	imul   %r8d,%eax
     ced:	8b 4c 24 2c          	mov    0x2c(%rsp),%ecx
     cf1:	01 c5                	add    %eax,%ebp
     cf3:	42 8d 04 1f          	lea    (%rdi,%r11,1),%eax
     cf7:	8d 3c 02             	lea    (%rdx,%rax,1),%edi
     cfa:	44 89 c0             	mov    %r8d,%eax
     cfd:	c1 e0 09             	shl    $0x9,%eax
     d00:	45 8d bc 07 00 02 00 	lea    0x200(%r15,%rax,1),%r15d
     d07:	00 
     d08:	85 c9                	test   %ecx,%ecx
     d0a:	0f 8e a5 f6 ff ff    	jle    3b5 <gouraud_deob_draw_triangle+0x2f5>
     d10:	c1 e1 09             	shl    $0x9,%ecx
     d13:	41 89 f8             	mov    %edi,%r8d
     d16:	89 d8                	mov    %ebx,%eax
     d18:	44 8b 74 24 0c       	mov    0xc(%rsp),%r14d
     d1d:	41 89 cd             	mov    %ecx,%r13d
     d20:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
     d25:	8b 5c 24 18          	mov    0x18(%rsp),%ebx
     d29:	45 01 fd             	add    %r15d,%r13d
     d2c:	0f 1f 40 00          	nopl   0x0(%rax)
     d30:	89 e9                	mov    %ebp,%ecx
     d32:	44 89 e2             	mov    %r12d,%edx
     d35:	89 ee                	mov    %ebp,%esi
     d37:	c1 f9 0e             	sar    $0xe,%ecx
     d3a:	c1 fa 0e             	sar    $0xe,%edx
     d3d:	44 09 e6             	or     %r12d,%esi
     d40:	78 3c                	js     d7e <gouraud_deob_draw_triangle+0xcbe>
     d42:	81 fa 00 02 00 00    	cmp    $0x200,%edx
     d48:	40 0f 9f c6          	setg   %sil
     d4c:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
     d52:	41 0f 9f c1          	setg   %r9b
     d56:	44 08 ce             	or     %r9b,%sil
     d59:	75 23                	jne    d7e <gouraud_deob_draw_triangle+0xcbe>
     d5b:	39 d1                	cmp    %edx,%ecx
     d5d:	7e 1f                	jle    d7e <gouraud_deob_draw_triangle+0xcbe>
     d5f:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
     d64:	44 89 fe             	mov    %r15d,%esi
     d67:	89 44 24 18          	mov    %eax,0x18(%rsp)
     d6b:	44 89 44 24 0c       	mov    %r8d,0xc(%rsp)
     d70:	e8 8b f2 ff ff       	call   0 <gouraud_deob_draw_scanline.part.0>
     d75:	8b 44 24 18          	mov    0x18(%rsp),%eax
     d79:	44 8b 44 24 0c       	mov    0xc(%rsp),%r8d
     d7e:	41 81 c7 00 02 00 00 	add    $0x200,%r15d
     d85:	41 01 dc             	add    %ebx,%r12d
     d88:	44 01 f5             	add    %r14d,%ebp
     d8b:	41 01 c0             	add    %eax,%r8d
     d8e:	45 39 fd             	cmp    %r15d,%r13d
     d91:	75 9d                	jne    d30 <gouraud_deob_draw_triangle+0xc70>
     d93:	e9 1d f6 ff ff       	jmp    3b5 <gouraud_deob_draw_triangle+0x2f5>
     d98:	0f 1f 84 00 00 00 00 	nopl   0x0(%rax,%rax,1)
     d9f:	00 
     da0:	8b 44 24 0c          	mov    0xc(%rsp),%eax
     da4:	45 89 c6             	mov    %r8d,%r14d
     da7:	41 0f af c3          	imul   %r11d,%eax
     dab:	41 29 c6             	sub    %eax,%r14d
     dae:	8b 44 24 18          	mov    0x18(%rsp),%eax
     db2:	41 0f af c3          	imul   %r11d,%eax
     db6:	44 0f af db          	imul   %ebx,%r11d
     dba:	41 29 c4             	sub    %eax,%r12d
     dbd:	31 c0                	xor    %eax,%eax
     dbf:	44 29 dd             	sub    %r11d,%ebp
     dc2:	45 31 db             	xor    %r11d,%r11d
     dc5:	e9 41 f4 ff ff       	jmp    20b <gouraud_deob_draw_triangle+0x14b>
     dca:	66 0f 1f 44 00 00    	nopw   0x0(%rax,%rax,1)
     dd0:	8b 74 24 0c          	mov    0xc(%rsp),%esi
     dd4:	44 8b 54 24 18       	mov    0x18(%rsp),%r10d
     dd9:	44 89 c0             	mov    %r8d,%eax
     ddc:	45 89 c7             	mov    %r8d,%r15d
     ddf:	41 c1 e6 0e          	shl    $0xe,%r14d
     de3:	89 f2                	mov    %esi,%edx
     de5:	41 0f af d3          	imul   %r11d,%edx
     de9:	29 d0                	sub    %edx,%eax
     deb:	44 89 d2             	mov    %r10d,%edx
     dee:	41 0f af d3          	imul   %r11d,%edx
     df2:	44 0f af db          	imul   %ebx,%r11d
     df6:	41 29 d7             	sub    %edx,%r15d
     df9:	44 29 dd             	sub    %r11d,%ebp
     dfc:	45 85 ed             	test   %r13d,%r13d
     dff:	78 17                	js     e18 <gouraud_deob_draw_triangle+0xd58>
     e01:	44 39 d6             	cmp    %r10d,%esi
     e04:	41 89 c4             	mov    %eax,%r12d
     e07:	0f 9c c2             	setl   %dl
     e0a:	31 c0                	xor    %eax,%eax
     e0c:	45 31 db             	xor    %r11d,%r11d
     e0f:	e9 66 fa ff ff       	jmp    87a <gouraud_deob_draw_triangle+0x7ba>
     e14:	0f 1f 40 00          	nopl   0x0(%rax)
     e18:	8b 54 24 20          	mov    0x20(%rsp),%edx
     e1c:	41 89 c4             	mov    %eax,%r12d
     e1f:	45 31 db             	xor    %r11d,%r11d
     e22:	31 c0                	xor    %eax,%eax
     e24:	0f af d7             	imul   %edi,%edx
     e27:	41 29 d6             	sub    %edx,%r14d
     e2a:	31 ff                	xor    %edi,%edi
     e2c:	44 39 df             	cmp    %r11d,%edi
     e2f:	75 0e                	jne    e3f <gouraud_deob_draw_triangle+0xd7f>
     e31:	8b 54 24 18          	mov    0x18(%rsp),%edx
     e35:	39 54 24 20          	cmp    %edx,0x20(%rsp)
     e39:	0f 8f 47 fb ff ff    	jg     986 <gouraud_deob_draw_triangle+0x8c6>
     e3f:	44 29 df             	sub    %r11d,%edi
     e42:	89 fa                	mov    %edi,%edx
     e44:	83 ef 01             	sub    $0x1,%edi
     e47:	0f 88 e9 00 00 00    	js     f36 <gouraud_deob_draw_triangle+0xe76>
     e4d:	89 44 24 30          	mov    %eax,0x30(%rsp)
     e51:	41 89 e8             	mov    %ebp,%r8d
     e54:	c1 e2 09             	shl    $0x9,%edx
     e57:	41 89 db             	mov    %ebx,%r11d
     e5a:	89 7c 24 34          	mov    %edi,0x34(%rsp)
     e5e:	8b 5c 24 18          	mov    0x18(%rsp),%ebx
     e62:	45 89 fd             	mov    %r15d,%r13d
     e65:	44 8d 14 02          	lea    (%rdx,%rax,1),%r10d
     e69:	89 4c 24 38          	mov    %ecx,0x38(%rsp)
     e6d:	89 6c 24 2c          	mov    %ebp,0x2c(%rsp)
     e71:	8b 6c 24 0c          	mov    0xc(%rsp),%ebp
     e75:	44 89 74 24 28       	mov    %r14d,0x28(%rsp)
     e7a:	45 89 c6             	mov    %r8d,%r14d
     e7d:	44 89 7c 24 3c       	mov    %r15d,0x3c(%rsp)
     e82:	41 89 c7             	mov    %eax,%r15d
     e85:	0f 1f 00             	nopl   (%rax)
     e88:	44 89 e1             	mov    %r12d,%ecx
     e8b:	44 89 ea             	mov    %r13d,%edx
     e8e:	44 89 e0             	mov    %r12d,%eax
     e91:	c1 f9 0e             	sar    $0xe,%ecx
     e94:	c1 fa 0e             	sar    $0xe,%edx
     e97:	44 09 e8             	or     %r13d,%eax
     e9a:	78 46                	js     ee2 <gouraud_deob_draw_triangle+0xe22>
     e9c:	81 fa 00 02 00 00    	cmp    $0x200,%edx
     ea2:	40 0f 9f c7          	setg   %dil
     ea6:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
     eac:	41 0f 9f c1          	setg   %r9b
     eb0:	44 08 cf             	or     %r9b,%dil
     eb3:	75 2d                	jne    ee2 <gouraud_deob_draw_triangle+0xe22>
     eb5:	39 d1                	cmp    %edx,%ecx
     eb7:	7e 29                	jle    ee2 <gouraud_deob_draw_triangle+0xe22>
     eb9:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
     ebe:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
     ec3:	45 89 f0             	mov    %r14d,%r8d
     ec6:	44 89 fe             	mov    %r15d,%esi
     ec9:	44 89 5c 24 24       	mov    %r11d,0x24(%rsp)
     ece:	44 89 54 24 0c       	mov    %r10d,0xc(%rsp)
     ed3:	e8 28 f1 ff ff       	call   0 <gouraud_deob_draw_scanline.part.0>
     ed8:	44 8b 5c 24 24       	mov    0x24(%rsp),%r11d
     edd:	44 8b 54 24 0c       	mov    0xc(%rsp),%r10d
     ee2:	41 81 c7 00 02 00 00 	add    $0x200,%r15d
     ee9:	41 01 ec             	add    %ebp,%r12d
     eec:	41 01 dd             	add    %ebx,%r13d
     eef:	45 01 de             	add    %r11d,%r14d
     ef2:	45 39 d7             	cmp    %r10d,%r15d
     ef5:	75 91                	jne    e88 <gouraud_deob_draw_triangle+0xdc8>
     ef7:	8b 7c 24 34          	mov    0x34(%rsp),%edi
     efb:	8b 74 24 18          	mov    0x18(%rsp),%esi
     eff:	44 89 da             	mov    %r11d,%edx
     f02:	44 89 db             	mov    %r11d,%ebx
     f05:	44 8b 7c 24 3c       	mov    0x3c(%rsp),%r15d
     f0a:	8b 6c 24 2c          	mov    0x2c(%rsp),%ebp
     f0e:	0f af d7             	imul   %edi,%edx
     f11:	8b 44 24 30          	mov    0x30(%rsp),%eax
     f15:	44 8b 74 24 28       	mov    0x28(%rsp),%r14d
     f1a:	41 01 f7             	add    %esi,%r15d
     f1d:	0f af f7             	imul   %edi,%esi
     f20:	44 01 dd             	add    %r11d,%ebp
     f23:	c1 e7 09             	shl    $0x9,%edi
     f26:	8b 4c 24 38          	mov    0x38(%rsp),%ecx
     f2a:	8d 84 38 00 02 00 00 	lea    0x200(%rax,%rdi,1),%eax
     f31:	01 d5                	add    %edx,%ebp
     f33:	41 01 f7             	add    %esi,%r15d
     f36:	83 e9 01             	sub    $0x1,%ecx
     f39:	41 89 cc             	mov    %ecx,%r12d
     f3c:	0f 88 73 f4 ff ff    	js     3b5 <gouraud_deob_draw_triangle+0x2f5>
     f42:	41 89 e8             	mov    %ebp,%r8d
     f45:	44 8b 6c 24 20       	mov    0x20(%rsp),%r13d
     f4a:	89 dd                	mov    %ebx,%ebp
     f4c:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
     f51:	8b 5c 24 18          	mov    0x18(%rsp),%ebx
     f55:	0f 1f 00             	nopl   (%rax)
     f58:	44 89 f1             	mov    %r14d,%ecx
     f5b:	44 89 fa             	mov    %r15d,%edx
     f5e:	44 89 f6             	mov    %r14d,%esi
     f61:	c1 f9 0e             	sar    $0xe,%ecx
     f64:	c1 fa 0e             	sar    $0xe,%edx
     f67:	44 09 fe             	or     %r15d,%esi
     f6a:	78 3b                	js     fa7 <gouraud_deob_draw_triangle+0xee7>
     f6c:	81 fa 00 02 00 00    	cmp    $0x200,%edx
     f72:	40 0f 9f c6          	setg   %sil
     f76:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
     f7c:	41 0f 9f c1          	setg   %r9b
     f80:	44 08 ce             	or     %r9b,%sil
     f83:	75 22                	jne    fa7 <gouraud_deob_draw_triangle+0xee7>
     f85:	39 d1                	cmp    %edx,%ecx
     f87:	7e 1e                	jle    fa7 <gouraud_deob_draw_triangle+0xee7>
     f89:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
     f8e:	89 c6                	mov    %eax,%esi
     f90:	44 89 44 24 18       	mov    %r8d,0x18(%rsp)
     f95:	89 44 24 0c          	mov    %eax,0xc(%rsp)
     f99:	e8 62 f0 ff ff       	call   0 <gouraud_deob_draw_scanline.part.0>
     f9e:	44 8b 44 24 18       	mov    0x18(%rsp),%r8d
     fa3:	8b 44 24 0c          	mov    0xc(%rsp),%eax
     fa7:	45 01 ee             	add    %r13d,%r14d
     faa:	41 01 df             	add    %ebx,%r15d
     fad:	41 01 e8             	add    %ebp,%r8d
     fb0:	05 00 02 00 00       	add    $0x200,%eax
     fb5:	41 83 ec 01          	sub    $0x1,%r12d
     fb9:	73 9d                	jae    f58 <gouraud_deob_draw_triangle+0xe98>
     fbb:	e9 f5 f3 ff ff       	jmp    3b5 <gouraud_deob_draw_triangle+0x2f5>
     fc0:	8b 74 24 20          	mov    0x20(%rsp),%esi
     fc4:	0f af ce             	imul   %esi,%ecx
     fc7:	41 29 cf             	sub    %ecx,%r15d
     fca:	45 85 db             	test   %r11d,%r11d
     fcd:	74 04                	je     fd3 <gouraud_deob_draw_triangle+0xf13>
     fcf:	84 d2                	test   %dl,%dl
     fd1:	74 17                	je     fea <gouraud_deob_draw_triangle+0xf2a>
     fd3:	8b 54 24 20          	mov    0x20(%rsp),%edx
     fd7:	39 54 24 0c          	cmp    %edx,0xc(%rsp)
     fdb:	0f 8e 4f 09 00 00    	jle    1930 <gouraud_deob_draw_triangle+0x1870>
     fe1:	45 85 db             	test   %r11d,%r11d
     fe4:	0f 85 46 09 00 00    	jne    1930 <gouraud_deob_draw_triangle+0x1870>
     fea:	45 85 ed             	test   %r13d,%r13d
     fed:	0f 8e c2 f3 ff ff    	jle    3b5 <gouraud_deob_draw_triangle+0x2f5>
     ff3:	e9 37 f3 ff ff       	jmp    32f <gouraud_deob_draw_triangle+0x26f>
     ff8:	0f 1f 84 00 00 00 00 	nopl   0x0(%rax,%rax,1)
     fff:	00 
    1000:	8b 74 24 20          	mov    0x20(%rsp),%esi
    1004:	0f af f7             	imul   %edi,%esi
    1007:	41 29 f6             	sub    %esi,%r14d
    100a:	45 85 db             	test   %r11d,%r11d
    100d:	0f 84 17 fe ff ff    	je     e2a <gouraud_deob_draw_triangle+0xd6a>
    1013:	84 d2                	test   %dl,%dl
    1015:	0f 85 6b f9 ff ff    	jne    986 <gouraud_deob_draw_triangle+0x8c6>
    101b:	31 ff                	xor    %edi,%edi
    101d:	e9 0a fe ff ff       	jmp    e2c <gouraud_deob_draw_triangle+0xd6c>
    1022:	66 0f 1f 44 00 00    	nopw   0x0(%rax,%rax,1)
    1028:	8b 54 24 0c          	mov    0xc(%rsp),%edx
    102c:	0f af d1             	imul   %ecx,%edx
    102f:	41 29 d6             	sub    %edx,%r14d
    1032:	45 85 d2             	test   %r10d,%r10d
    1035:	74 04                	je     103b <gouraud_deob_draw_triangle+0xf7b>
    1037:	84 c0                	test   %al,%al
    1039:	74 17                	je     1052 <gouraud_deob_draw_triangle+0xf92>
    103b:	8b 54 24 18          	mov    0x18(%rsp),%edx
    103f:	39 54 24 0c          	cmp    %edx,0xc(%rsp)
    1043:	0f 8d 1f 09 00 00    	jge    1968 <gouraud_deob_draw_triangle+0x18a8>
    1049:	45 85 d2             	test   %r10d,%r10d
    104c:	0f 85 16 09 00 00    	jne    1968 <gouraud_deob_draw_triangle+0x18a8>
    1052:	45 85 db             	test   %r11d,%r11d
    1055:	0f 8e 5a f3 ff ff    	jle    3b5 <gouraud_deob_draw_triangle+0x2f5>
    105b:	e9 3b f5 ff ff       	jmp    59b <gouraud_deob_draw_triangle+0x4db>
    1060:	8b 44 24 18          	mov    0x18(%rsp),%eax
    1064:	0f af c6             	imul   %esi,%eax
    1067:	8b 74 24 20          	mov    0x20(%rsp),%esi
    106b:	41 29 c4             	sub    %eax,%r12d
    106e:	39 74 24 0c          	cmp    %esi,0xc(%rsp)
    1072:	0f 8f 90 fc ff ff    	jg     d08 <gouraud_deob_draw_triangle+0xc48>
    1078:	85 c9                	test   %ecx,%ecx
    107a:	0f 8e 35 f3 ff ff    	jle    3b5 <gouraud_deob_draw_triangle+0x2f5>
    1080:	c1 e1 09             	shl    $0x9,%ecx
    1083:	41 89 f8             	mov    %edi,%r8d
    1086:	89 d8                	mov    %ebx,%eax
    1088:	44 8b 74 24 0c       	mov    0xc(%rsp),%r14d
    108d:	41 89 cd             	mov    %ecx,%r13d
    1090:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
    1095:	8b 5c 24 18          	mov    0x18(%rsp),%ebx
    1099:	45 01 fd             	add    %r15d,%r13d
    109c:	0f 1f 40 00          	nopl   0x0(%rax)
    10a0:	44 89 e1             	mov    %r12d,%ecx
    10a3:	89 ea                	mov    %ebp,%edx
    10a5:	44 89 e6             	mov    %r12d,%esi
    10a8:	c1 f9 0e             	sar    $0xe,%ecx
    10ab:	c1 fa 0e             	sar    $0xe,%edx
    10ae:	09 ee                	or     %ebp,%esi
    10b0:	78 3c                	js     10ee <gouraud_deob_draw_triangle+0x102e>
    10b2:	81 fa 00 02 00 00    	cmp    $0x200,%edx
    10b8:	40 0f 9f c6          	setg   %sil
    10bc:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
    10c2:	41 0f 9f c1          	setg   %r9b
    10c6:	44 08 ce             	or     %r9b,%sil
    10c9:	75 23                	jne    10ee <gouraud_deob_draw_triangle+0x102e>
    10cb:	39 d1                	cmp    %edx,%ecx
    10cd:	7e 1f                	jle    10ee <gouraud_deob_draw_triangle+0x102e>
    10cf:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
    10d4:	44 89 fe             	mov    %r15d,%esi
    10d7:	89 44 24 18          	mov    %eax,0x18(%rsp)
    10db:	44 89 44 24 0c       	mov    %r8d,0xc(%rsp)
    10e0:	e8 1b ef ff ff       	call   0 <gouraud_deob_draw_scanline.part.0>
    10e5:	8b 44 24 18          	mov    0x18(%rsp),%eax
    10e9:	44 8b 44 24 0c       	mov    0xc(%rsp),%r8d
    10ee:	41 81 c7 00 02 00 00 	add    $0x200,%r15d
    10f5:	41 01 dc             	add    %ebx,%r12d
    10f8:	44 01 f5             	add    %r14d,%ebp
    10fb:	41 01 c0             	add    %eax,%r8d
    10fe:	45 39 fd             	cmp    %r15d,%r13d
    1101:	75 9d                	jne    10a0 <gouraud_deob_draw_triangle+0xfe0>
    1103:	e9 ad f2 ff ff       	jmp    3b5 <gouraud_deob_draw_triangle+0x2f5>
    1108:	0f 1f 84 00 00 00 00 	nopl   0x0(%rax,%rax,1)
    110f:	00 
    1110:	8b 44 24 20          	mov    0x20(%rsp),%eax
    1114:	8b 54 24 0c          	mov    0xc(%rsp),%edx
    1118:	44 89 f5             	mov    %r14d,%ebp
    111b:	45 31 ff             	xor    %r15d,%r15d
    111e:	41 0f af c5          	imul   %r13d,%eax
    1122:	41 0f af d5          	imul   %r13d,%edx
    1126:	44 0f af eb          	imul   %ebx,%r13d
    112a:	41 29 c6             	sub    %eax,%r14d
    112d:	29 d5                	sub    %edx,%ebp
    112f:	44 29 ef             	sub    %r13d,%edi
    1132:	45 31 ed             	xor    %r13d,%r13d
    1135:	e9 b9 fa ff ff       	jmp    bf3 <gouraud_deob_draw_triangle+0xb33>
    113a:	66 0f 1f 44 00 00    	nopw   0x0(%rax,%rax,1)
    1140:	8b 44 24 18          	mov    0x18(%rsp),%eax
    1144:	8b 54 24 20          	mov    0x20(%rsp),%edx
    1148:	0f af c1             	imul   %ecx,%eax
    114b:	41 29 c4             	sub    %eax,%r12d
    114e:	39 54 24 0c          	cmp    %edx,0xc(%rsp)
    1152:	0f 8e fc 07 00 00    	jle    1954 <gouraud_deob_draw_triangle+0x1894>
    1158:	45 85 d2             	test   %r10d,%r10d
    115b:	0f 8e 54 f2 ff ff    	jle    3b5 <gouraud_deob_draw_triangle+0x2f5>
    1161:	e9 3d f6 ff ff       	jmp    7a3 <gouraud_deob_draw_triangle+0x6e3>
    1166:	66 2e 0f 1f 84 00 00 	cs nopw 0x0(%rax,%rax,1)
    116d:	00 00 00 
    1170:	8b 44 24 20          	mov    0x20(%rsp),%eax
    1174:	44 89 f5             	mov    %r14d,%ebp
    1177:	45 31 ff             	xor    %r15d,%r15d
    117a:	41 0f af c5          	imul   %r13d,%eax
    117e:	29 c5                	sub    %eax,%ebp
    1180:	8b 44 24 0c          	mov    0xc(%rsp),%eax
    1184:	41 0f af c5          	imul   %r13d,%eax
    1188:	44 0f af eb          	imul   %ebx,%r13d
    118c:	41 29 c6             	sub    %eax,%r14d
    118f:	44 29 ef             	sub    %r13d,%edi
    1192:	45 31 ed             	xor    %r13d,%r13d
    1195:	e9 f6 f4 ff ff       	jmp    690 <gouraud_deob_draw_triangle+0x5d0>
    119a:	66 0f 1f 44 00 00    	nopw   0x0(%rax,%rax,1)
    11a0:	8b 44 24 18          	mov    0x18(%rsp),%eax
    11a4:	8b 54 24 20          	mov    0x20(%rsp),%edx
    11a8:	45 89 cd             	mov    %r9d,%r13d
    11ab:	45 31 f6             	xor    %r14d,%r14d
    11ae:	41 0f af c2          	imul   %r10d,%eax
    11b2:	41 0f af d2          	imul   %r10d,%edx
    11b6:	44 0f af d3          	imul   %ebx,%r10d
    11ba:	29 c5                	sub    %eax,%ebp
    11bc:	41 29 d5             	sub    %edx,%r13d
    11bf:	44 29 d7             	sub    %r10d,%edi
    11c2:	45 31 d2             	xor    %r10d,%r10d
    11c5:	e9 69 f8 ff ff       	jmp    a33 <gouraud_deob_draw_triangle+0x973>
    11ca:	66 0f 1f 44 00 00    	nopw   0x0(%rax,%rax,1)
    11d0:	8b 44 24 18          	mov    0x18(%rsp),%eax
    11d4:	45 89 cc             	mov    %r9d,%r12d
    11d7:	45 31 ff             	xor    %r15d,%r15d
    11da:	41 0f af c2          	imul   %r10d,%eax
    11de:	41 29 c4             	sub    %eax,%r12d
    11e1:	8b 44 24 20          	mov    0x20(%rsp),%eax
    11e5:	41 0f af c2          	imul   %r10d,%eax
    11e9:	44 0f af d3          	imul   %ebx,%r10d
    11ed:	29 c5                	sub    %eax,%ebp
    11ef:	44 29 d7             	sub    %r10d,%edi
    11f2:	45 31 d2             	xor    %r10d,%r10d
    11f5:	e9 7e f2 ff ff       	jmp    478 <gouraud_deob_draw_triangle+0x3b8>
    11fa:	66 0f 1f 44 00 00    	nopw   0x0(%rax,%rax,1)
    1200:	8b 44 24 0c          	mov    0xc(%rsp),%eax
    1204:	0f af c6             	imul   %esi,%eax
    1207:	8b 74 24 18          	mov    0x18(%rsp),%esi
    120b:	41 29 c4             	sub    %eax,%r12d
    120e:	39 74 24 20          	cmp    %esi,0x20(%rsp)
    1212:	0f 8f 35 f9 ff ff    	jg     b4d <gouraud_deob_draw_triangle+0xa8d>
    1218:	85 c9                	test   %ecx,%ecx
    121a:	0f 8e 95 f1 ff ff    	jle    3b5 <gouraud_deob_draw_triangle+0x2f5>
    1220:	c1 e1 09             	shl    $0x9,%ecx
    1223:	41 89 f8             	mov    %edi,%r8d
    1226:	89 d8                	mov    %ebx,%eax
    1228:	44 8b 7c 24 0c       	mov    0xc(%rsp),%r15d
    122d:	89 cd                	mov    %ecx,%ebp
    122f:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
    1234:	8b 5c 24 20          	mov    0x20(%rsp),%ebx
    1238:	44 01 f5             	add    %r14d,%ebp
    123b:	0f 1f 44 00 00       	nopl   0x0(%rax,%rax,1)
    1240:	44 89 e1             	mov    %r12d,%ecx
    1243:	44 89 ea             	mov    %r13d,%edx
    1246:	44 89 e6             	mov    %r12d,%esi
    1249:	c1 f9 0e             	sar    $0xe,%ecx
    124c:	c1 fa 0e             	sar    $0xe,%edx
    124f:	44 09 ee             	or     %r13d,%esi
    1252:	78 3c                	js     1290 <gouraud_deob_draw_triangle+0x11d0>
    1254:	81 fa 00 02 00 00    	cmp    $0x200,%edx
    125a:	40 0f 9f c6          	setg   %sil
    125e:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
    1264:	41 0f 9f c1          	setg   %r9b
    1268:	44 08 ce             	or     %r9b,%sil
    126b:	75 23                	jne    1290 <gouraud_deob_draw_triangle+0x11d0>
    126d:	39 d1                	cmp    %edx,%ecx
    126f:	7e 1f                	jle    1290 <gouraud_deob_draw_triangle+0x11d0>
    1271:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
    1276:	44 89 f6             	mov    %r14d,%esi
    1279:	89 44 24 18          	mov    %eax,0x18(%rsp)
    127d:	44 89 44 24 0c       	mov    %r8d,0xc(%rsp)
    1282:	e8 79 ed ff ff       	call   0 <gouraud_deob_draw_scanline.part.0>
    1287:	8b 44 24 18          	mov    0x18(%rsp),%eax
    128b:	44 8b 44 24 0c       	mov    0xc(%rsp),%r8d
    1290:	41 81 c6 00 02 00 00 	add    $0x200,%r14d
    1297:	45 01 fc             	add    %r15d,%r12d
    129a:	41 01 dd             	add    %ebx,%r13d
    129d:	41 01 c0             	add    %eax,%r8d
    12a0:	44 39 f5             	cmp    %r14d,%ebp
    12a3:	75 9b                	jne    1240 <gouraud_deob_draw_triangle+0x1180>
    12a5:	e9 0b f1 ff ff       	jmp    3b5 <gouraud_deob_draw_triangle+0x2f5>
    12aa:	85 c0                	test   %eax,%eax
    12ac:	0f 8e 66 ff ff ff    	jle    1218 <gouraud_deob_draw_triangle+0x1158>
    12b2:	44 89 44 24 28       	mov    %r8d,0x28(%rsp)
    12b7:	45 89 f2             	mov    %r14d,%r10d
    12ba:	45 89 ef             	mov    %r13d,%r15d
    12bd:	41 89 db             	mov    %ebx,%r11d
    12c0:	89 4c 24 2c          	mov    %ecx,0x2c(%rsp)
    12c4:	8b 5c 24 20          	mov    0x20(%rsp),%ebx
    12c8:	41 89 f4             	mov    %esi,%r12d
    12cb:	89 7c 24 38          	mov    %edi,0x38(%rsp)
    12cf:	44 89 6c 24 34       	mov    %r13d,0x34(%rsp)
    12d4:	44 8b 6c 24 18       	mov    0x18(%rsp),%r13d
    12d9:	44 89 74 24 30       	mov    %r14d,0x30(%rsp)
    12de:	41 89 fe             	mov    %edi,%r14d
    12e1:	89 74 24 3c          	mov    %esi,0x3c(%rsp)
    12e5:	44 89 d6             	mov    %r10d,%esi
    12e8:	0f 1f 84 00 00 00 00 	nopl   0x0(%rax,%rax,1)
    12ef:	00 
    12f0:	89 e9                	mov    %ebp,%ecx
    12f2:	44 89 fa             	mov    %r15d,%edx
    12f5:	89 e8                	mov    %ebp,%eax
    12f7:	c1 f9 0e             	sar    $0xe,%ecx
    12fa:	c1 fa 0e             	sar    $0xe,%edx
    12fd:	44 09 f8             	or     %r15d,%eax
    1300:	78 41                	js     1343 <gouraud_deob_draw_triangle+0x1283>
    1302:	81 fa 00 02 00 00    	cmp    $0x200,%edx
    1308:	40 0f 9f c7          	setg   %dil
    130c:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
    1312:	41 0f 9f c1          	setg   %r9b
    1316:	44 08 cf             	or     %r9b,%dil
    1319:	75 28                	jne    1343 <gouraud_deob_draw_triangle+0x1283>
    131b:	39 d1                	cmp    %edx,%ecx
    131d:	7e 24                	jle    1343 <gouraud_deob_draw_triangle+0x1283>
    131f:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
    1324:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
    1329:	45 89 f0             	mov    %r14d,%r8d
    132c:	89 74 24 18          	mov    %esi,0x18(%rsp)
    1330:	44 89 5c 24 24       	mov    %r11d,0x24(%rsp)
    1335:	e8 c6 ec ff ff       	call   0 <gouraud_deob_draw_scanline.part.0>
    133a:	44 8b 5c 24 24       	mov    0x24(%rsp),%r11d
    133f:	8b 74 24 18          	mov    0x18(%rsp),%esi
    1343:	44 01 ed             	add    %r13d,%ebp
    1346:	41 01 df             	add    %ebx,%r15d
    1349:	45 01 de             	add    %r11d,%r14d
    134c:	81 c6 00 02 00 00    	add    $0x200,%esi
    1352:	41 83 ec 01          	sub    $0x1,%r12d
    1356:	73 98                	jae    12f0 <gouraud_deob_draw_triangle+0x1230>
    1358:	8b 74 24 3c          	mov    0x3c(%rsp),%esi
    135c:	8b 44 24 20          	mov    0x20(%rsp),%eax
    1360:	44 89 da             	mov    %r11d,%edx
    1363:	44 89 db             	mov    %r11d,%ebx
    1366:	44 8b 6c 24 34       	mov    0x34(%rsp),%r13d
    136b:	8b 7c 24 38          	mov    0x38(%rsp),%edi
    136f:	0f af d6             	imul   %esi,%edx
    1372:	44 8b 74 24 30       	mov    0x30(%rsp),%r14d
    1377:	44 8b 64 24 28       	mov    0x28(%rsp),%r12d
    137c:	41 01 c5             	add    %eax,%r13d
    137f:	0f af c6             	imul   %esi,%eax
    1382:	c1 e6 09             	shl    $0x9,%esi
    1385:	8b 4c 24 2c          	mov    0x2c(%rsp),%ecx
    1389:	45 8d b4 36 00 02 00 	lea    0x200(%r14,%rsi,1),%r14d
    1390:	00 
    1391:	41 01 c5             	add    %eax,%r13d
    1394:	42 8d 04 1f          	lea    (%rdi,%r11,1),%eax
    1398:	8d 3c 02             	lea    (%rdx,%rax,1),%edi
    139b:	e9 78 fe ff ff       	jmp    1218 <gouraud_deob_draw_triangle+0x1158>
    13a0:	85 f6                	test   %esi,%esi
    13a2:	0f 8e d0 fc ff ff    	jle    1078 <gouraud_deob_draw_triangle+0xfb8>
    13a8:	44 89 4c 24 28       	mov    %r9d,0x28(%rsp)
    13ad:	41 89 ed             	mov    %ebp,%r13d
    13b0:	41 89 db             	mov    %ebx,%r11d
    13b3:	8b 5c 24 0c          	mov    0xc(%rsp),%ebx
    13b7:	89 4c 24 2c          	mov    %ecx,0x2c(%rsp)
    13bb:	44 89 fe             	mov    %r15d,%esi
    13be:	45 89 c4             	mov    %r8d,%r12d
    13c1:	89 7c 24 38          	mov    %edi,0x38(%rsp)
    13c5:	44 89 44 24 3c       	mov    %r8d,0x3c(%rsp)
    13ca:	89 6c 24 34          	mov    %ebp,0x34(%rsp)
    13ce:	8b 6c 24 20          	mov    0x20(%rsp),%ebp
    13d2:	44 89 7c 24 30       	mov    %r15d,0x30(%rsp)
    13d7:	41 89 ff             	mov    %edi,%r15d
    13da:	66 0f 1f 44 00 00    	nopw   0x0(%rax,%rax,1)
    13e0:	44 89 f1             	mov    %r14d,%ecx
    13e3:	44 89 ea             	mov    %r13d,%edx
    13e6:	44 89 f0             	mov    %r14d,%eax
    13e9:	c1 f9 0e             	sar    $0xe,%ecx
    13ec:	c1 fa 0e             	sar    $0xe,%edx
    13ef:	44 09 e8             	or     %r13d,%eax
    13f2:	78 41                	js     1435 <gouraud_deob_draw_triangle+0x1375>
    13f4:	81 fa 00 02 00 00    	cmp    $0x200,%edx
    13fa:	40 0f 9f c7          	setg   %dil
    13fe:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
    1404:	41 0f 9f c1          	setg   %r9b
    1408:	44 08 cf             	or     %r9b,%dil
    140b:	75 28                	jne    1435 <gouraud_deob_draw_triangle+0x1375>
    140d:	39 d1                	cmp    %edx,%ecx
    140f:	7e 24                	jle    1435 <gouraud_deob_draw_triangle+0x1375>
    1411:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
    1416:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
    141b:	45 89 f8             	mov    %r15d,%r8d
    141e:	89 74 24 20          	mov    %esi,0x20(%rsp)
    1422:	44 89 5c 24 24       	mov    %r11d,0x24(%rsp)
    1427:	e8 d4 eb ff ff       	call   0 <gouraud_deob_draw_scanline.part.0>
    142c:	44 8b 5c 24 24       	mov    0x24(%rsp),%r11d
    1431:	8b 74 24 20          	mov    0x20(%rsp),%esi
    1435:	41 01 ee             	add    %ebp,%r14d
    1438:	41 01 dd             	add    %ebx,%r13d
    143b:	45 01 df             	add    %r11d,%r15d
    143e:	81 c6 00 02 00 00    	add    $0x200,%esi
    1444:	41 83 ec 01          	sub    $0x1,%r12d
    1448:	73 96                	jae    13e0 <gouraud_deob_draw_triangle+0x1320>
    144a:	44 8b 44 24 3c       	mov    0x3c(%rsp),%r8d
    144f:	8b 44 24 0c          	mov    0xc(%rsp),%eax
    1453:	44 89 da             	mov    %r11d,%edx
    1456:	44 89 db             	mov    %r11d,%ebx
    1459:	8b 6c 24 34          	mov    0x34(%rsp),%ebp
    145d:	8b 7c 24 38          	mov    0x38(%rsp),%edi
    1461:	41 0f af d0          	imul   %r8d,%edx
    1465:	44 8b 7c 24 30       	mov    0x30(%rsp),%r15d
    146a:	44 8b 64 24 28       	mov    0x28(%rsp),%r12d
    146f:	01 c5                	add    %eax,%ebp
    1471:	41 0f af c0          	imul   %r8d,%eax
    1475:	41 c1 e0 09          	shl    $0x9,%r8d
    1479:	8b 4c 24 2c          	mov    0x2c(%rsp),%ecx
    147d:	47 8d bc 07 00 02 00 	lea    0x200(%r15,%r8,1),%r15d
    1484:	00 
    1485:	01 c5                	add    %eax,%ebp
    1487:	42 8d 04 1f          	lea    (%rdi,%r11,1),%eax
    148b:	8d 3c 02             	lea    (%rdx,%rax,1),%edi
    148e:	e9 e5 fb ff ff       	jmp    1078 <gouraud_deob_draw_triangle+0xfb8>
    1493:	85 c0                	test   %eax,%eax
    1495:	0f 8e de 00 00 00    	jle    1579 <gouraud_deob_draw_triangle+0x14b9>
    149b:	89 7c 24 34          	mov    %edi,0x34(%rsp)
    149f:	45 89 fa             	mov    %r15d,%r10d
    14a2:	41 89 db             	mov    %ebx,%r11d
    14a5:	8b 5c 24 0c          	mov    0xc(%rsp),%ebx
    14a9:	89 4c 24 3c          	mov    %ecx,0x3c(%rsp)
    14ad:	41 89 ed             	mov    %ebp,%r13d
    14b0:	44 89 64 24 2c       	mov    %r12d,0x2c(%rsp)
    14b5:	44 8b 64 24 20       	mov    0x20(%rsp),%r12d
    14ba:	89 6c 24 28          	mov    %ebp,0x28(%rsp)
    14be:	89 cd                	mov    %ecx,%ebp
    14c0:	44 89 7c 24 30       	mov    %r15d,0x30(%rsp)
    14c5:	41 89 ff             	mov    %edi,%r15d
    14c8:	89 74 24 38          	mov    %esi,0x38(%rsp)
    14cc:	44 89 d6             	mov    %r10d,%esi
    14cf:	90                   	nop
    14d0:	44 89 e9             	mov    %r13d,%ecx
    14d3:	44 89 f2             	mov    %r14d,%edx
    14d6:	44 89 e8             	mov    %r13d,%eax
    14d9:	c1 f9 0e             	sar    $0xe,%ecx
    14dc:	c1 fa 0e             	sar    $0xe,%edx
    14df:	44 09 f0             	or     %r14d,%eax
    14e2:	78 41                	js     1525 <gouraud_deob_draw_triangle+0x1465>
    14e4:	81 fa 00 02 00 00    	cmp    $0x200,%edx
    14ea:	40 0f 9f c7          	setg   %dil
    14ee:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
    14f4:	41 0f 9f c1          	setg   %r9b
    14f8:	44 08 cf             	or     %r9b,%dil
    14fb:	75 28                	jne    1525 <gouraud_deob_draw_triangle+0x1465>
    14fd:	39 d1                	cmp    %edx,%ecx
    14ff:	7e 24                	jle    1525 <gouraud_deob_draw_triangle+0x1465>
    1501:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
    1506:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
    150b:	45 89 f8             	mov    %r15d,%r8d
    150e:	89 74 24 0c          	mov    %esi,0xc(%rsp)
    1512:	44 89 5c 24 24       	mov    %r11d,0x24(%rsp)
    1517:	e8 e4 ea ff ff       	call   0 <gouraud_deob_draw_scanline.part.0>
    151c:	44 8b 5c 24 24       	mov    0x24(%rsp),%r11d
    1521:	8b 74 24 0c          	mov    0xc(%rsp),%esi
    1525:	45 01 e5             	add    %r12d,%r13d
    1528:	41 01 de             	add    %ebx,%r14d
    152b:	45 01 df             	add    %r11d,%r15d
    152e:	81 c6 00 02 00 00    	add    $0x200,%esi
    1534:	83 ed 01             	sub    $0x1,%ebp
    1537:	73 97                	jae    14d0 <gouraud_deob_draw_triangle+0x1410>
    1539:	8b 4c 24 3c          	mov    0x3c(%rsp),%ecx
    153d:	8b 44 24 20          	mov    0x20(%rsp),%eax
    1541:	44 89 da             	mov    %r11d,%edx
    1544:	44 89 db             	mov    %r11d,%ebx
    1547:	8b 6c 24 28          	mov    0x28(%rsp),%ebp
    154b:	8b 7c 24 34          	mov    0x34(%rsp),%edi
    154f:	0f af d1             	imul   %ecx,%edx
    1552:	44 8b 7c 24 30       	mov    0x30(%rsp),%r15d
    1557:	44 8b 64 24 2c       	mov    0x2c(%rsp),%r12d
    155c:	01 c5                	add    %eax,%ebp
    155e:	0f af c1             	imul   %ecx,%eax
    1561:	8b 74 24 38          	mov    0x38(%rsp),%esi
    1565:	c1 e1 09             	shl    $0x9,%ecx
    1568:	45 8d bc 0f 00 02 00 	lea    0x200(%r15,%rcx,1),%r15d
    156f:	00 
    1570:	01 c5                	add    %eax,%ebp
    1572:	42 8d 04 1f          	lea    (%rdi,%r11,1),%eax
    1576:	8d 3c 02             	lea    (%rdx,%rax,1),%edi
    1579:	c1 e6 09             	shl    $0x9,%esi
    157c:	41 89 f8             	mov    %edi,%r8d
    157f:	89 d8                	mov    %ebx,%eax
    1581:	44 8b 74 24 20       	mov    0x20(%rsp),%r14d
    1586:	41 89 f5             	mov    %esi,%r13d
    1589:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
    158e:	8b 5c 24 18          	mov    0x18(%rsp),%ebx
    1592:	45 01 fd             	add    %r15d,%r13d
    1595:	0f 1f 00             	nopl   (%rax)
    1598:	89 e9                	mov    %ebp,%ecx
    159a:	44 89 e2             	mov    %r12d,%edx
    159d:	89 ee                	mov    %ebp,%esi
    159f:	c1 f9 0e             	sar    $0xe,%ecx
    15a2:	c1 fa 0e             	sar    $0xe,%edx
    15a5:	44 09 e6             	or     %r12d,%esi
    15a8:	78 3c                	js     15e6 <gouraud_deob_draw_triangle+0x1526>
    15aa:	81 fa 00 02 00 00    	cmp    $0x200,%edx
    15b0:	40 0f 9f c6          	setg   %sil
    15b4:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
    15ba:	41 0f 9f c1          	setg   %r9b
    15be:	44 08 ce             	or     %r9b,%sil
    15c1:	75 23                	jne    15e6 <gouraud_deob_draw_triangle+0x1526>
    15c3:	39 d1                	cmp    %edx,%ecx
    15c5:	7e 1f                	jle    15e6 <gouraud_deob_draw_triangle+0x1526>
    15c7:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
    15cc:	44 89 fe             	mov    %r15d,%esi
    15cf:	89 44 24 18          	mov    %eax,0x18(%rsp)
    15d3:	44 89 44 24 0c       	mov    %r8d,0xc(%rsp)
    15d8:	e8 23 ea ff ff       	call   0 <gouraud_deob_draw_scanline.part.0>
    15dd:	8b 44 24 18          	mov    0x18(%rsp),%eax
    15e1:	44 8b 44 24 0c       	mov    0xc(%rsp),%r8d
    15e6:	41 81 c7 00 02 00 00 	add    $0x200,%r15d
    15ed:	44 01 f5             	add    %r14d,%ebp
    15f0:	41 01 dc             	add    %ebx,%r12d
    15f3:	41 01 c0             	add    %eax,%r8d
    15f6:	45 39 fd             	cmp    %r15d,%r13d
    15f9:	75 9d                	jne    1598 <gouraud_deob_draw_triangle+0x14d8>
    15fb:	e9 b5 ed ff ff       	jmp    3b5 <gouraud_deob_draw_triangle+0x2f5>
    1600:	41 39 cb             	cmp    %ecx,%r11d
    1603:	0f 84 77 01 00 00    	je     1780 <gouraud_deob_draw_triangle+0x16c0>
    1609:	89 ca                	mov    %ecx,%edx
    160b:	44 29 da             	sub    %r11d,%edx
    160e:	89 d1                	mov    %edx,%ecx
    1610:	83 e9 01             	sub    $0x1,%ecx
    1613:	0f 88 7b 03 00 00    	js     1994 <gouraud_deob_draw_triangle+0x18d4>
    1619:	c1 e2 09             	shl    $0x9,%edx
    161c:	89 7c 24 28          	mov    %edi,0x28(%rsp)
    1620:	41 89 e8             	mov    %ebp,%r8d
    1623:	45 89 f5             	mov    %r14d,%r13d
    1626:	89 44 24 34          	mov    %eax,0x34(%rsp)
    162a:	44 8d 14 02          	lea    (%rdx,%rax,1),%r10d
    162e:	41 89 db             	mov    %ebx,%r11d
    1631:	89 c6                	mov    %eax,%esi
    1633:	89 4c 24 3c          	mov    %ecx,0x3c(%rsp)
    1637:	44 89 d3             	mov    %r10d,%ebx
    163a:	89 6c 24 2c          	mov    %ebp,0x2c(%rsp)
    163e:	8b 6c 24 0c          	mov    0xc(%rsp),%ebp
    1642:	44 89 74 24 38       	mov    %r14d,0x38(%rsp)
    1647:	44 8b 74 24 18       	mov    0x18(%rsp),%r14d
    164c:	44 89 7c 24 30       	mov    %r15d,0x30(%rsp)
    1651:	45 89 c7             	mov    %r8d,%r15d
    1654:	0f 1f 40 00          	nopl   0x0(%rax)
    1658:	44 89 e9             	mov    %r13d,%ecx
    165b:	44 89 e2             	mov    %r12d,%edx
    165e:	44 89 e8             	mov    %r13d,%eax
    1661:	c1 f9 0e             	sar    $0xe,%ecx
    1664:	c1 fa 0e             	sar    $0xe,%edx
    1667:	44 09 e0             	or     %r12d,%eax
    166a:	78 41                	js     16ad <gouraud_deob_draw_triangle+0x15ed>
    166c:	81 fa 00 02 00 00    	cmp    $0x200,%edx
    1672:	40 0f 9f c7          	setg   %dil
    1676:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
    167c:	41 0f 9f c1          	setg   %r9b
    1680:	44 08 cf             	or     %r9b,%dil
    1683:	75 28                	jne    16ad <gouraud_deob_draw_triangle+0x15ed>
    1685:	39 d1                	cmp    %edx,%ecx
    1687:	7e 24                	jle    16ad <gouraud_deob_draw_triangle+0x15ed>
    1689:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
    168e:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
    1693:	45 89 f8             	mov    %r15d,%r8d
    1696:	89 74 24 18          	mov    %esi,0x18(%rsp)
    169a:	44 89 5c 24 24       	mov    %r11d,0x24(%rsp)
    169f:	e8 5c e9 ff ff       	call   0 <gouraud_deob_draw_scanline.part.0>
    16a4:	44 8b 5c 24 24       	mov    0x24(%rsp),%r11d
    16a9:	8b 74 24 18          	mov    0x18(%rsp),%esi
    16ad:	81 c6 00 02 00 00    	add    $0x200,%esi
    16b3:	41 01 ed             	add    %ebp,%r13d
    16b6:	45 01 f4             	add    %r14d,%r12d
    16b9:	45 01 df             	add    %r11d,%r15d
    16bc:	39 f3                	cmp    %esi,%ebx
    16be:	75 98                	jne    1658 <gouraud_deob_draw_triangle+0x1598>
    16c0:	44 8b 6c 24 0c       	mov    0xc(%rsp),%r13d
    16c5:	8b 4c 24 3c          	mov    0x3c(%rsp),%ecx
    16c9:	44 8b 74 24 38       	mov    0x38(%rsp),%r14d
    16ce:	8b 6c 24 2c          	mov    0x2c(%rsp),%ebp
    16d2:	44 89 ea             	mov    %r13d,%edx
    16d5:	8b 7c 24 28          	mov    0x28(%rsp),%edi
    16d9:	8b 44 24 34          	mov    0x34(%rsp),%eax
    16dd:	0f af d1             	imul   %ecx,%edx
    16e0:	45 01 ee             	add    %r13d,%r14d
    16e3:	44 01 dd             	add    %r11d,%ebp
    16e6:	44 8b 7c 24 30       	mov    0x30(%rsp),%r15d
    16eb:	44 8d 67 ff          	lea    -0x1(%rdi),%r12d
    16ef:	8b 5c 24 20          	mov    0x20(%rsp),%ebx
    16f3:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
    16f8:	41 01 d6             	add    %edx,%r14d
    16fb:	44 89 da             	mov    %r11d,%edx
    16fe:	0f af d1             	imul   %ecx,%edx
    1701:	c1 e1 09             	shl    $0x9,%ecx
    1704:	8d 84 08 00 02 00 00 	lea    0x200(%rax,%rcx,1),%eax
    170b:	01 d5                	add    %edx,%ebp
    170d:	41 89 e8             	mov    %ebp,%r8d
    1710:	44 89 dd             	mov    %r11d,%ebp
    1713:	0f 1f 44 00 00       	nopl   0x0(%rax,%rax,1)
    1718:	44 89 f1             	mov    %r14d,%ecx
    171b:	44 89 fa             	mov    %r15d,%edx
    171e:	44 89 f6             	mov    %r14d,%esi
    1721:	c1 f9 0e             	sar    $0xe,%ecx
    1724:	c1 fa 0e             	sar    $0xe,%edx
    1727:	44 09 fe             	or     %r15d,%esi
    172a:	78 3b                	js     1767 <gouraud_deob_draw_triangle+0x16a7>
    172c:	81 fa 00 02 00 00    	cmp    $0x200,%edx
    1732:	40 0f 9f c6          	setg   %sil
    1736:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
    173c:	41 0f 9f c1          	setg   %r9b
    1740:	44 08 ce             	or     %r9b,%sil
    1743:	75 22                	jne    1767 <gouraud_deob_draw_triangle+0x16a7>
    1745:	39 d1                	cmp    %edx,%ecx
    1747:	7e 1e                	jle    1767 <gouraud_deob_draw_triangle+0x16a7>
    1749:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
    174e:	89 c6                	mov    %eax,%esi
    1750:	44 89 44 24 18       	mov    %r8d,0x18(%rsp)
    1755:	89 44 24 0c          	mov    %eax,0xc(%rsp)
    1759:	e8 a2 e8 ff ff       	call   0 <gouraud_deob_draw_scanline.part.0>
    175e:	44 8b 44 24 18       	mov    0x18(%rsp),%r8d
    1763:	8b 44 24 0c          	mov    0xc(%rsp),%eax
    1767:	45 01 ee             	add    %r13d,%r14d
    176a:	41 01 df             	add    %ebx,%r15d
    176d:	41 01 e8             	add    %ebp,%r8d
    1770:	05 00 02 00 00       	add    $0x200,%eax
    1775:	41 83 ec 01          	sub    $0x1,%r12d
    1779:	73 9d                	jae    1718 <gouraud_deob_draw_triangle+0x1658>
    177b:	e9 35 ec ff ff       	jmp    3b5 <gouraud_deob_draw_triangle+0x2f5>
    1780:	8b 54 24 20          	mov    0x20(%rsp),%edx
    1784:	39 54 24 0c          	cmp    %edx,0xc(%rsp)
    1788:	0f 8e 7b fe ff ff    	jle    1609 <gouraud_deob_draw_triangle+0x1549>
    178e:	e9 9c eb ff ff       	jmp    32f <gouraud_deob_draw_triangle+0x26f>
    1793:	0f 1f 44 00 00       	nopl   0x0(%rax,%rax,1)
    1798:	41 39 ca             	cmp    %ecx,%r10d
    179b:	0f 84 76 01 00 00    	je     1917 <gouraud_deob_draw_triangle+0x1857>
    17a1:	89 c8                	mov    %ecx,%eax
    17a3:	44 29 d0             	sub    %r10d,%eax
    17a6:	89 c1                	mov    %eax,%ecx
    17a8:	83 e9 01             	sub    $0x1,%ecx
    17ab:	0f 88 de 01 00 00    	js     198f <gouraud_deob_draw_triangle+0x18cf>
    17b1:	44 89 7c 24 30       	mov    %r15d,0x30(%rsp)
    17b6:	c1 e0 09             	shl    $0x9,%eax
    17b9:	45 89 e5             	mov    %r12d,%r13d
    17bc:	41 89 da             	mov    %ebx,%r10d
    17bf:	89 4c 24 34          	mov    %ecx,0x34(%rsp)
    17c3:	8b 5c 24 18          	mov    0x18(%rsp),%ebx
    17c7:	46 8d 1c 38          	lea    (%rax,%r15,1),%r11d
    17cb:	89 7c 24 38          	mov    %edi,0x38(%rsp)
    17cf:	89 74 24 3c          	mov    %esi,0x3c(%rsp)
    17d3:	44 89 64 24 2c       	mov    %r12d,0x2c(%rsp)
    17d8:	44 8b 64 24 20       	mov    0x20(%rsp),%r12d
    17dd:	44 89 74 24 28       	mov    %r14d,0x28(%rsp)
    17e2:	41 89 fe             	mov    %edi,%r14d
    17e5:	0f 1f 00             	nopl   (%rax)
    17e8:	44 89 e9             	mov    %r13d,%ecx
    17eb:	89 ea                	mov    %ebp,%edx
    17ed:	44 89 e8             	mov    %r13d,%eax
    17f0:	c1 f9 0e             	sar    $0xe,%ecx
    17f3:	c1 fa 0e             	sar    $0xe,%edx
    17f6:	09 e8                	or     %ebp,%eax
    17f8:	78 46                	js     1840 <gouraud_deob_draw_triangle+0x1780>
    17fa:	81 fa 00 02 00 00    	cmp    $0x200,%edx
    1800:	40 0f 9f c7          	setg   %dil
    1804:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
    180a:	41 0f 9f c1          	setg   %r9b
    180e:	44 08 cf             	or     %r9b,%dil
    1811:	75 2d                	jne    1840 <gouraud_deob_draw_triangle+0x1780>
    1813:	39 d1                	cmp    %edx,%ecx
    1815:	7e 29                	jle    1840 <gouraud_deob_draw_triangle+0x1780>
    1817:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
    181c:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
    1821:	45 89 f0             	mov    %r14d,%r8d
    1824:	44 89 fe             	mov    %r15d,%esi
    1827:	44 89 54 24 24       	mov    %r10d,0x24(%rsp)
    182c:	44 89 5c 24 20       	mov    %r11d,0x20(%rsp)
    1831:	e8 ca e7 ff ff       	call   0 <gouraud_deob_draw_scanline.part.0>
    1836:	44 8b 54 24 24       	mov    0x24(%rsp),%r10d
    183b:	44 8b 5c 24 20       	mov    0x20(%rsp),%r11d
    1840:	41 81 c7 00 02 00 00 	add    $0x200,%r15d
    1847:	41 01 dd             	add    %ebx,%r13d
    184a:	44 01 e5             	add    %r12d,%ebp
    184d:	45 01 d6             	add    %r10d,%r14d
    1850:	45 39 df             	cmp    %r11d,%r15d
    1853:	75 93                	jne    17e8 <gouraud_deob_draw_triangle+0x1728>
    1855:	8b 4c 24 34          	mov    0x34(%rsp),%ecx
    1859:	44 89 d2             	mov    %r10d,%edx
    185c:	8b 7c 24 38          	mov    0x38(%rsp),%edi
    1860:	44 8b 64 24 2c       	mov    0x2c(%rsp),%r12d
    1865:	44 8b 7c 24 30       	mov    0x30(%rsp),%r15d
    186a:	0f af d1             	imul   %ecx,%edx
    186d:	42 8d 04 17          	lea    (%rdi,%r10,1),%eax
    1871:	8b 74 24 3c          	mov    0x3c(%rsp),%esi
    1875:	44 8b 74 24 28       	mov    0x28(%rsp),%r14d
    187a:	44 8b 6c 24 0c       	mov    0xc(%rsp),%r13d
    187f:	8d 6e ff             	lea    -0x1(%rsi),%ebp
    1882:	8d 3c 02             	lea    (%rdx,%rax,1),%edi
    1885:	8b 54 24 18          	mov    0x18(%rsp),%edx
    1889:	41 89 f8             	mov    %edi,%r8d
    188c:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
    1891:	89 d0                	mov    %edx,%eax
    1893:	41 01 d4             	add    %edx,%r12d
    1896:	89 d3                	mov    %edx,%ebx
    1898:	0f af c1             	imul   %ecx,%eax
    189b:	c1 e1 09             	shl    $0x9,%ecx
    189e:	45 8d bc 0f 00 02 00 	lea    0x200(%r15,%rcx,1),%r15d
    18a5:	00 
    18a6:	41 01 c4             	add    %eax,%r12d
    18a9:	44 89 f8             	mov    %r15d,%eax
    18ac:	45 89 d7             	mov    %r10d,%r15d
    18af:	90                   	nop
    18b0:	44 89 e1             	mov    %r12d,%ecx
    18b3:	44 89 f2             	mov    %r14d,%edx
    18b6:	44 89 e6             	mov    %r12d,%esi
    18b9:	c1 f9 0e             	sar    $0xe,%ecx
    18bc:	c1 fa 0e             	sar    $0xe,%edx
    18bf:	44 09 f6             	or     %r14d,%esi
    18c2:	78 3b                	js     18ff <gouraud_deob_draw_triangle+0x183f>
    18c4:	81 fa 00 02 00 00    	cmp    $0x200,%edx
    18ca:	40 0f 9f c6          	setg   %sil
    18ce:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
    18d4:	41 0f 9f c1          	setg   %r9b
    18d8:	44 08 ce             	or     %r9b,%sil
    18db:	75 22                	jne    18ff <gouraud_deob_draw_triangle+0x183f>
    18dd:	39 d1                	cmp    %edx,%ecx
    18df:	7e 1e                	jle    18ff <gouraud_deob_draw_triangle+0x183f>
    18e1:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
    18e6:	89 c6                	mov    %eax,%esi
    18e8:	44 89 44 24 18       	mov    %r8d,0x18(%rsp)
    18ed:	89 44 24 0c          	mov    %eax,0xc(%rsp)
    18f1:	e8 0a e7 ff ff       	call   0 <gouraud_deob_draw_scanline.part.0>
    18f6:	44 8b 44 24 18       	mov    0x18(%rsp),%r8d
    18fb:	8b 44 24 0c          	mov    0xc(%rsp),%eax
    18ff:	41 01 dc             	add    %ebx,%r12d
    1902:	45 01 ee             	add    %r13d,%r14d
    1905:	45 01 f8             	add    %r15d,%r8d
    1908:	05 00 02 00 00       	add    $0x200,%eax
    190d:	83 ed 01             	sub    $0x1,%ebp
    1910:	73 9e                	jae    18b0 <gouraud_deob_draw_triangle+0x17f0>
    1912:	e9 9e ea ff ff       	jmp    3b5 <gouraud_deob_draw_triangle+0x2f5>
    1917:	8b 54 24 18          	mov    0x18(%rsp),%edx
    191b:	39 54 24 0c          	cmp    %edx,0xc(%rsp)
    191f:	0f 8d 7c fe ff ff    	jge    17a1 <gouraud_deob_draw_triangle+0x16e1>
    1925:	e9 71 ec ff ff       	jmp    59b <gouraud_deob_draw_triangle+0x4db>
    192a:	66 0f 1f 44 00 00    	nopw   0x0(%rax,%rax,1)
    1930:	83 ef 01             	sub    $0x1,%edi
    1933:	41 89 fc             	mov    %edi,%r12d
    1936:	0f 88 79 ea ff ff    	js     3b5 <gouraud_deob_draw_triangle+0x2f5>
    193c:	41 89 e8             	mov    %ebp,%r8d
    193f:	44 8b 6c 24 0c       	mov    0xc(%rsp),%r13d
    1944:	89 dd                	mov    %ebx,%ebp
    1946:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
    194b:	8b 5c 24 20          	mov    0x20(%rsp),%ebx
    194f:	e9 c4 fd ff ff       	jmp    1718 <gouraud_deob_draw_triangle+0x1658>
    1954:	45 85 d2             	test   %r10d,%r10d
    1957:	0f 8e 58 ea ff ff    	jle    3b5 <gouraud_deob_draw_triangle+0x2f5>
    195d:	e9 17 fc ff ff       	jmp    1579 <gouraud_deob_draw_triangle+0x14b9>
    1962:	66 0f 1f 44 00 00    	nopw   0x0(%rax,%rax,1)
    1968:	83 ee 01             	sub    $0x1,%esi
    196b:	89 f5                	mov    %esi,%ebp
    196d:	0f 88 42 ea ff ff    	js     3b5 <gouraud_deob_draw_triangle+0x2f5>
    1973:	44 89 f8             	mov    %r15d,%eax
    1976:	41 89 f8             	mov    %edi,%r8d
    1979:	41 89 df             	mov    %ebx,%r15d
    197c:	44 8b 6c 24 0c       	mov    0xc(%rsp),%r13d
    1981:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
    1986:	8b 5c 24 18          	mov    0x18(%rsp),%ebx
    198a:	e9 21 ff ff ff       	jmp    18b0 <gouraud_deob_draw_triangle+0x17f0>
    198f:	8d 6e ff             	lea    -0x1(%rsi),%ebp
    1992:	eb df                	jmp    1973 <gouraud_deob_draw_triangle+0x18b3>
    1994:	44 8d 67 ff          	lea    -0x1(%rdi),%r12d
    1998:	eb a2                	jmp    193c <gouraud_deob_draw_triangle+0x187c>
