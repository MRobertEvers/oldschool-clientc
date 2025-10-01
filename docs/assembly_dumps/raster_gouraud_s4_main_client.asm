
build_release/main_client:     file format elf64-x86-64


Disassembly of section .init:

Disassembly of section .plt:

Disassembly of section .plt.got:

Disassembly of section .plt.sec:

Disassembly of section .text:

0000000000025120 <raster_gouraud_s4>:
   25120:	f3 0f 1e fa          	endbr64
   25124:	41 57                	push   %r15
   25126:	45 89 c3             	mov    %r8d,%r11d
   25129:	41 56                	push   %r14
   2512b:	41 55                	push   %r13
   2512d:	41 54                	push   %r12
   2512f:	41 89 cc             	mov    %ecx,%r12d
   25132:	55                   	push   %rbp
   25133:	89 f5                	mov    %esi,%ebp
   25135:	44 89 ce             	mov    %r9d,%esi
   25138:	45 29 e3             	sub    %r12d,%r11d
   2513b:	53                   	push   %rbx
   2513c:	89 f3                	mov    %esi,%ebx
   2513e:	29 cb                	sub    %ecx,%ebx
   25140:	89 d8                	mov    %ebx,%eax
   25142:	48 83 ec 38          	sub    $0x38,%rsp
   25146:	44 8b 4c 24 70       	mov    0x70(%rsp),%r9d
   2514b:	8b 8c 24 80 00 00 00 	mov    0x80(%rsp),%ecx
   25152:	89 54 24 1c          	mov    %edx,0x1c(%rsp)
   25156:	44 8b 54 24 78       	mov    0x78(%rsp),%r10d
   2515b:	44 29 c9             	sub    %r9d,%ecx
   2515e:	45 29 ca             	sub    %r9d,%r10d
   25161:	89 ca                	mov    %ecx,%edx
   25163:	41 0f af d3          	imul   %r11d,%edx
   25167:	41 0f af c2          	imul   %r10d,%eax
   2516b:	29 c2                	sub    %eax,%edx
   2516d:	0f 84 eb 02 00 00    	je     2545e <raster_gouraud_s4+0x33e>
   25173:	8b 84 24 90 00 00 00 	mov    0x90(%rsp),%eax
   2517a:	2b 84 24 88 00 00 00 	sub    0x88(%rsp),%eax
   25181:	41 89 d7             	mov    %edx,%r15d
   25184:	44 8b b4 24 98 00 00 	mov    0x98(%rsp),%r14d
   2518b:	00 
   2518c:	89 44 24 08          	mov    %eax,0x8(%rsp)
   25190:	44 2b b4 24 88 00 00 	sub    0x88(%rsp),%r14d
   25197:	00 
   25198:	44 3b 4c 24 78       	cmp    0x78(%rsp),%r9d
   2519d:	0f 8e cd 02 00 00    	jle    25470 <raster_gouraud_s4+0x350>
   251a3:	44 39 8c 24 80 00 00 	cmp    %r9d,0x80(%rsp)
   251aa:	00 
   251ab:	0f 8c 37 03 00 00    	jl     254e8 <raster_gouraud_s4+0x3c8>
   251b1:	44 29 c6             	sub    %r8d,%esi
   251b4:	8b 94 24 80 00 00 00 	mov    0x80(%rsp),%edx
   251bb:	8b 84 24 80 00 00 00 	mov    0x80(%rsp),%eax
   251c2:	41 89 cd             	mov    %ecx,%r13d
   251c5:	89 74 24 20          	mov    %esi,0x20(%rsp)
   251c9:	8b b4 24 88 00 00 00 	mov    0x88(%rsp),%esi
   251d0:	2b 44 24 78          	sub    0x78(%rsp),%eax
   251d4:	44 89 8c 24 80 00 00 	mov    %r9d,0x80(%rsp)
   251db:	00 
   251dc:	89 b4 24 98 00 00 00 	mov    %esi,0x98(%rsp)
   251e3:	8b b4 24 90 00 00 00 	mov    0x90(%rsp),%esi
   251ea:	89 44 24 0c          	mov    %eax,0xc(%rsp)
   251ee:	44 8b 4c 24 78       	mov    0x78(%rsp),%r9d
   251f3:	89 d8                	mov    %ebx,%eax
   251f5:	89 b4 24 88 00 00 00 	mov    %esi,0x88(%rsp)
   251fc:	44 89 e6             	mov    %r12d,%esi
   251ff:	45 89 c4             	mov    %r8d,%r12d
   25202:	89 54 24 78          	mov    %edx,0x78(%rsp)
   25206:	44 39 8c 24 80 00 00 	cmp    %r9d,0x80(%rsp)
   2520d:	00 
   2520e:	0f 8d 2c 03 00 00    	jge    25540 <raster_gouraud_s4+0x420>
   25214:	44 89 e2             	mov    %r12d,%edx
   25217:	45 31 c0             	xor    %r8d,%r8d
   2521a:	29 f2                	sub    %esi,%edx
   2521c:	89 54 24 18          	mov    %edx,0x18(%rsp)
   25220:	44 89 ca             	mov    %r9d,%edx
   25223:	2b 94 24 80 00 00 00 	sub    0x80(%rsp),%edx
   2522a:	89 54 24 14          	mov    %edx,0x14(%rsp)
   2522e:	45 85 ed             	test   %r13d,%r13d
   25231:	7e 1c                	jle    2524f <raster_gouraud_s4+0x12f>
   25233:	c1 e0 08             	shl    $0x8,%eax
   25236:	99                   	cltd
   25237:	41 f7 fd             	idiv   %r13d
   2523a:	41 89 c0             	mov    %eax,%r8d
   2523d:	44 8b 6c 24 14       	mov    0x14(%rsp),%r13d
   25242:	c7 44 24 10 00 00 00 	movl   $0x0,0x10(%rsp)
   25249:	00 
   2524a:	45 85 ed             	test   %r13d,%r13d
   2524d:	7e 10                	jle    2525f <raster_gouraud_s4+0x13f>
   2524f:	8b 44 24 18          	mov    0x18(%rsp),%eax
   25253:	c1 e0 08             	shl    $0x8,%eax
   25256:	99                   	cltd
   25257:	f7 7c 24 14          	idivl  0x14(%rsp)
   2525b:	89 44 24 10          	mov    %eax,0x10(%rsp)
   2525f:	8b 54 24 0c          	mov    0xc(%rsp),%edx
   25263:	c7 44 24 18 00 00 00 	movl   $0x0,0x18(%rsp)
   2526a:	00 
   2526b:	85 d2                	test   %edx,%edx
   2526d:	7e 12                	jle    25281 <raster_gouraud_s4+0x161>
   2526f:	8b 44 24 20          	mov    0x20(%rsp),%eax
   25273:	41 89 d5             	mov    %edx,%r13d
   25276:	c1 e0 08             	shl    $0x8,%eax
   25279:	99                   	cltd
   2527a:	41 f7 fd             	idiv   %r13d
   2527d:	89 44 24 18          	mov    %eax,0x18(%rsp)
   25281:	8b 44 24 08          	mov    0x8(%rsp),%eax
   25285:	45 0f af d6          	imul   %r14d,%r10d
   25289:	41 89 f5             	mov    %esi,%r13d
   2528c:	41 c1 e4 08          	shl    $0x8,%r12d
   25290:	45 0f af de          	imul   %r14d,%r11d
   25294:	44 8b b4 24 98 00 00 	mov    0x98(%rsp),%r14d
   2529b:	00 
   2529c:	41 c1 e5 08          	shl    $0x8,%r13d
   252a0:	0f af c1             	imul   %ecx,%eax
   252a3:	41 c1 e6 08          	shl    $0x8,%r14d
   252a7:	44 29 d0             	sub    %r10d,%eax
   252aa:	44 8b 94 24 80 00 00 	mov    0x80(%rsp),%r10d
   252b1:	00 
   252b2:	c1 e0 08             	shl    $0x8,%eax
   252b5:	99                   	cltd
   252b6:	41 f7 ff             	idiv   %r15d
   252b9:	8b 54 24 08          	mov    0x8(%rsp),%edx
   252bd:	0f af d3             	imul   %ebx,%edx
   252c0:	89 44 24 0c          	mov    %eax,0xc(%rsp)
   252c4:	44 89 d8             	mov    %r11d,%eax
   252c7:	29 d0                	sub    %edx,%eax
   252c9:	c1 e0 08             	shl    $0x8,%eax
   252cc:	99                   	cltd
   252cd:	41 f7 ff             	idiv   %r15d
   252d0:	89 44 24 08          	mov    %eax,0x8(%rsp)
   252d4:	8b 44 24 0c          	mov    0xc(%rsp),%eax
   252d8:	0f af f0             	imul   %eax,%esi
   252db:	41 01 c6             	add    %eax,%r14d
   252de:	41 29 f6             	sub    %esi,%r14d
   252e1:	45 85 d2             	test   %r10d,%r10d
   252e4:	0f 88 ee 02 00 00    	js     255d8 <raster_gouraud_s4+0x4b8>
   252ea:	8b 84 24 80 00 00 00 	mov    0x80(%rsp),%eax
   252f1:	45 89 eb             	mov    %r13d,%r11d
   252f4:	0f af c5             	imul   %ebp,%eax
   252f7:	45 85 c9             	test   %r9d,%r9d
   252fa:	0f 88 b0 02 00 00    	js     255b0 <raster_gouraud_s4+0x490>
   25300:	8b 5c 24 1c          	mov    0x1c(%rsp),%ebx
   25304:	8d 53 ff             	lea    -0x1(%rbx),%edx
   25307:	44 39 cb             	cmp    %r9d,%ebx
   2530a:	44 0f 4e ca          	cmovle %edx,%r9d
   2530e:	44 39 8c 24 80 00 00 	cmp    %r9d,0x80(%rsp)
   25315:	00 
   25316:	0f 8d fd 02 00 00    	jge    25619 <raster_gouraud_s4+0x4f9>
   2531c:	8b 9c 24 80 00 00 00 	mov    0x80(%rsp),%ebx
   25323:	44 89 44 24 14       	mov    %r8d,0x14(%rsp)
   25328:	44 89 f2             	mov    %r14d,%edx
   2532b:	45 89 ef             	mov    %r13d,%r15d
   2532e:	44 89 5c 24 20       	mov    %r11d,0x20(%rsp)
   25333:	41 89 d5             	mov    %edx,%r13d
   25336:	89 44 24 28          	mov    %eax,0x28(%rsp)
   2533a:	44 89 4c 24 70       	mov    %r9d,0x70(%rsp)
   2533f:	44 89 64 24 24       	mov    %r12d,0x24(%rsp)
   25344:	41 89 dc             	mov    %ebx,%r12d
   25347:	44 89 db             	mov    %r11d,%ebx
   2534a:	44 89 74 24 2c       	mov    %r14d,0x2c(%rsp)
   2534f:	41 89 c6             	mov    %eax,%r14d
   25352:	66 0f 1f 44 00 00    	nopw   0x0(%rax,%rax,1)
   25358:	48 83 ec 08          	sub    $0x8,%rsp
   2535c:	89 d9                	mov    %ebx,%ecx
   2535e:	45 89 f8             	mov    %r15d,%r8d
   25361:	44 89 f6             	mov    %r14d,%esi
   25364:	8b 44 24 14          	mov    0x14(%rsp),%eax
   25368:	c1 f9 08             	sar    $0x8,%ecx
   2536b:	45 89 e9             	mov    %r13d,%r9d
   2536e:	41 c1 f8 08          	sar    $0x8,%r8d
   25372:	89 ea                	mov    %ebp,%edx
   25374:	41 01 ee             	add    %ebp,%r14d
   25377:	41 83 c4 01          	add    $0x1,%r12d
   2537b:	50                   	push   %rax
   2537c:	e8 6f f4 ff ff       	call   247f0 <draw_scanline_gouraud_s4>
   25381:	8b 44 24 24          	mov    0x24(%rsp),%eax
   25385:	01 c3                	add    %eax,%ebx
   25387:	8b 44 24 20          	mov    0x20(%rsp),%eax
   2538b:	41 01 c7             	add    %eax,%r15d
   2538e:	8b 44 24 18          	mov    0x18(%rsp),%eax
   25392:	59                   	pop    %rcx
   25393:	5e                   	pop    %rsi
   25394:	41 01 c5             	add    %eax,%r13d
   25397:	44 39 64 24 70       	cmp    %r12d,0x70(%rsp)
   2539c:	75 ba                	jne    25358 <raster_gouraud_s4+0x238>
   2539e:	44 89 e3             	mov    %r12d,%ebx
   253a1:	89 ee                	mov    %ebp,%esi
   253a3:	8b 44 24 28          	mov    0x28(%rsp),%eax
   253a7:	44 8b 44 24 14       	mov    0x14(%rsp),%r8d
   253ac:	89 da                	mov    %ebx,%edx
   253ae:	44 8b 5c 24 20       	mov    0x20(%rsp),%r11d
   253b3:	44 8b 74 24 2c       	mov    0x2c(%rsp),%r14d
   253b8:	2b 94 24 80 00 00 00 	sub    0x80(%rsp),%edx
   253bf:	01 e8                	add    %ebp,%eax
   253c1:	44 8b 64 24 24       	mov    0x24(%rsp),%r12d
   253c6:	83 ea 01             	sub    $0x1,%edx
   253c9:	45 01 c3             	add    %r8d,%r11d
   253cc:	0f af f2             	imul   %edx,%esi
   253cf:	01 f0                	add    %esi,%eax
   253d1:	89 d6                	mov    %edx,%esi
   253d3:	41 0f af f0          	imul   %r8d,%esi
   253d7:	41 01 f3             	add    %esi,%r11d
   253da:	8b 74 24 08          	mov    0x8(%rsp),%esi
   253de:	0f af d6             	imul   %esi,%edx
   253e1:	41 01 f6             	add    %esi,%r14d
   253e4:	41 01 d6             	add    %edx,%r14d
   253e7:	3b 5c 24 78          	cmp    0x78(%rsp),%ebx
   253eb:	7d 71                	jge    2545e <raster_gouraud_s4+0x33e>
   253ed:	8b 74 24 1c          	mov    0x1c(%rsp),%esi
   253f1:	3b 74 24 78          	cmp    0x78(%rsp),%esi
   253f5:	7f 0b                	jg     25402 <raster_gouraud_s4+0x2e2>
   253f7:	83 ee 01             	sub    $0x1,%esi
   253fa:	89 74 24 78          	mov    %esi,0x78(%rsp)
   253fe:	39 de                	cmp    %ebx,%esi
   25400:	7e 5c                	jle    2545e <raster_gouraud_s4+0x33e>
   25402:	44 89 44 24 10       	mov    %r8d,0x10(%rsp)
   25407:	45 89 f5             	mov    %r14d,%r13d
   2540a:	41 89 c7             	mov    %eax,%r15d
   2540d:	41 89 de             	mov    %ebx,%r14d
   25410:	44 89 db             	mov    %r11d,%ebx
   25413:	0f 1f 44 00 00       	nopl   0x0(%rax,%rax,1)
   25418:	48 83 ec 08          	sub    $0x8,%rsp
   2541c:	89 d9                	mov    %ebx,%ecx
   2541e:	45 89 e0             	mov    %r12d,%r8d
   25421:	45 89 e9             	mov    %r13d,%r9d
   25424:	8b 44 24 14          	mov    0x14(%rsp),%eax
   25428:	89 ea                	mov    %ebp,%edx
   2542a:	44 89 fe             	mov    %r15d,%esi
   2542d:	c1 f9 08             	sar    $0x8,%ecx
   25430:	41 c1 f8 08          	sar    $0x8,%r8d
   25434:	41 01 ef             	add    %ebp,%r15d
   25437:	41 83 c6 01          	add    $0x1,%r14d
   2543b:	50                   	push   %rax
   2543c:	e8 af f3 ff ff       	call   247f0 <draw_scanline_gouraud_s4>
   25441:	8b 44 24 20          	mov    0x20(%rsp),%eax
   25445:	01 c3                	add    %eax,%ebx
   25447:	8b 44 24 28          	mov    0x28(%rsp),%eax
   2544b:	41 01 c4             	add    %eax,%r12d
   2544e:	8b 44 24 18          	mov    0x18(%rsp),%eax
   25452:	41 01 c5             	add    %eax,%r13d
   25455:	58                   	pop    %rax
   25456:	5a                   	pop    %rdx
   25457:	44 3b 74 24 78       	cmp    0x78(%rsp),%r14d
   2545c:	75 ba                	jne    25418 <raster_gouraud_s4+0x2f8>
   2545e:	48 83 c4 38          	add    $0x38,%rsp
   25462:	5b                   	pop    %rbx
   25463:	5d                   	pop    %rbp
   25464:	41 5c                	pop    %r12
   25466:	41 5d                	pop    %r13
   25468:	41 5e                	pop    %r14
   2546a:	41 5f                	pop    %r15
   2546c:	c3                   	ret
   2546d:	0f 1f 00             	nopl   (%rax)
   25470:	8b 44 24 78          	mov    0x78(%rsp),%eax
   25474:	39 84 24 80 00 00 00 	cmp    %eax,0x80(%rsp)
   2547b:	0f 8c 1f 01 00 00    	jl     255a0 <raster_gouraud_s4+0x480>
   25481:	8b 94 24 80 00 00 00 	mov    0x80(%rsp),%edx
   25488:	44 29 c6             	sub    %r8d,%esi
   2548b:	44 89 54 24 14       	mov    %r10d,0x14(%rsp)
   25490:	41 89 cd             	mov    %ecx,%r13d
   25493:	8b 84 24 80 00 00 00 	mov    0x80(%rsp),%eax
   2549a:	2b 44 24 78          	sub    0x78(%rsp),%eax
   2549e:	89 74 24 20          	mov    %esi,0x20(%rsp)
   254a2:	44 89 8c 24 80 00 00 	mov    %r9d,0x80(%rsp)
   254a9:	00 
   254aa:	8b b4 24 88 00 00 00 	mov    0x88(%rsp),%esi
   254b1:	44 8b 4c 24 78       	mov    0x78(%rsp),%r9d
   254b6:	44 89 5c 24 18       	mov    %r11d,0x18(%rsp)
   254bb:	89 54 24 78          	mov    %edx,0x78(%rsp)
   254bf:	89 44 24 0c          	mov    %eax,0xc(%rsp)
   254c3:	89 d8                	mov    %ebx,%eax
   254c5:	89 b4 24 98 00 00 00 	mov    %esi,0x98(%rsp)
   254cc:	44 89 e6             	mov    %r12d,%esi
   254cf:	45 89 c4             	mov    %r8d,%r12d
   254d2:	45 31 c0             	xor    %r8d,%r8d
   254d5:	45 85 ed             	test   %r13d,%r13d
   254d8:	0f 8f 55 fd ff ff    	jg     25233 <raster_gouraud_s4+0x113>
   254de:	e9 5a fd ff ff       	jmp    2523d <raster_gouraud_s4+0x11d>
   254e3:	0f 1f 44 00 00       	nopl   0x0(%rax,%rax,1)
   254e8:	44 89 e0             	mov    %r12d,%eax
   254eb:	44 29 c0             	sub    %r8d,%eax
   254ee:	89 44 24 20          	mov    %eax,0x20(%rsp)
   254f2:	44 89 c8             	mov    %r9d,%eax
   254f5:	2b 44 24 78          	sub    0x78(%rsp),%eax
   254f9:	89 44 24 0c          	mov    %eax,0xc(%rsp)
   254fd:	8b 84 24 90 00 00 00 	mov    0x90(%rsp),%eax
   25504:	89 84 24 88 00 00 00 	mov    %eax,0x88(%rsp)
   2550b:	44 89 c8             	mov    %r9d,%eax
   2550e:	44 8b 4c 24 78       	mov    0x78(%rsp),%r9d
   25513:	89 44 24 78          	mov    %eax,0x78(%rsp)
   25517:	44 89 e0             	mov    %r12d,%eax
   2551a:	45 89 c4             	mov    %r8d,%r12d
   2551d:	41 89 c0             	mov    %eax,%r8d
   25520:	44 89 c0             	mov    %r8d,%eax
   25523:	44 8b 6c 24 78       	mov    0x78(%rsp),%r13d
   25528:	44 2b ac 24 80 00 00 	sub    0x80(%rsp),%r13d
   2552f:	00 
   25530:	29 f0                	sub    %esi,%eax
   25532:	44 39 8c 24 80 00 00 	cmp    %r9d,0x80(%rsp)
   25539:	00 
   2553a:	0f 8c d4 fc ff ff    	jl     25214 <raster_gouraud_s4+0xf4>
   25540:	89 f2                	mov    %esi,%edx
   25542:	44 29 e2             	sub    %r12d,%edx
   25545:	89 54 24 18          	mov    %edx,0x18(%rsp)
   25549:	8b 94 24 80 00 00 00 	mov    0x80(%rsp),%edx
   25550:	44 29 ca             	sub    %r9d,%edx
   25553:	89 54 24 14          	mov    %edx,0x14(%rsp)
   25557:	44 89 ea             	mov    %r13d,%edx
   2555a:	44 8b 6c 24 0c       	mov    0xc(%rsp),%r13d
   2555f:	89 54 24 0c          	mov    %edx,0xc(%rsp)
   25563:	89 c2                	mov    %eax,%edx
   25565:	8b 44 24 20          	mov    0x20(%rsp),%eax
   25569:	89 54 24 20          	mov    %edx,0x20(%rsp)
   2556d:	8b 94 24 88 00 00 00 	mov    0x88(%rsp),%edx
   25574:	89 94 24 98 00 00 00 	mov    %edx,0x98(%rsp)
   2557b:	8b 94 24 80 00 00 00 	mov    0x80(%rsp),%edx
   25582:	44 89 8c 24 80 00 00 	mov    %r9d,0x80(%rsp)
   25589:	00 
   2558a:	41 89 d1             	mov    %edx,%r9d
   2558d:	89 f2                	mov    %esi,%edx
   2558f:	44 89 e6             	mov    %r12d,%esi
   25592:	41 89 d4             	mov    %edx,%r12d
   25595:	e9 38 ff ff ff       	jmp    254d2 <raster_gouraud_s4+0x3b2>
   2559a:	66 0f 1f 44 00 00    	nopw   0x0(%rax,%rax,1)
   255a0:	44 89 54 24 0c       	mov    %r10d,0xc(%rsp)
   255a5:	44 89 5c 24 20       	mov    %r11d,0x20(%rsp)
   255aa:	e9 71 ff ff ff       	jmp    25520 <raster_gouraud_s4+0x400>
   255af:	90                   	nop
   255b0:	8b 54 24 18          	mov    0x18(%rsp),%edx
   255b4:	8b 74 24 1c          	mov    0x1c(%rsp),%esi
   255b8:	bb 01 00 00 00       	mov    $0x1,%ebx
   255bd:	41 0f af d1          	imul   %r9d,%edx
   255c1:	41 29 d4             	sub    %edx,%r12d
   255c4:	85 f6                	test   %esi,%esi
   255c6:	0f 4e de             	cmovle %esi,%ebx
   255c9:	83 eb 01             	sub    $0x1,%ebx
   255cc:	e9 16 fe ff ff       	jmp    253e7 <raster_gouraud_s4+0x2c7>
   255d1:	0f 1f 80 00 00 00 00 	nopl   0x0(%rax)
   255d8:	8b 84 24 80 00 00 00 	mov    0x80(%rsp),%eax
   255df:	45 89 eb             	mov    %r13d,%r11d
   255e2:	41 0f af c0          	imul   %r8d,%eax
   255e6:	41 29 c3             	sub    %eax,%r11d
   255e9:	8b 44 24 10          	mov    0x10(%rsp),%eax
   255ed:	0f af 84 24 80 00 00 	imul   0x80(%rsp),%eax
   255f4:	00 
   255f5:	41 29 c5             	sub    %eax,%r13d
   255f8:	8b 44 24 08          	mov    0x8(%rsp),%eax
   255fc:	0f af 84 24 80 00 00 	imul   0x80(%rsp),%eax
   25603:	00 
   25604:	c7 84 24 80 00 00 00 	movl   $0x0,0x80(%rsp)
   2560b:	00 00 00 00 
   2560f:	41 29 c6             	sub    %eax,%r14d
   25612:	31 c0                	xor    %eax,%eax
   25614:	e9 de fc ff ff       	jmp    252f7 <raster_gouraud_s4+0x1d7>
   25619:	44 89 cb             	mov    %r9d,%ebx
   2561c:	e9 c6 fd ff ff       	jmp    253e7 <raster_gouraud_s4+0x2c7>

Disassembly of section .fini:
