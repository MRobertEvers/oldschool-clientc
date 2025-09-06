
build_release/main_client:     file format elf64-x86-64


Disassembly of section .init:

Disassembly of section .plt:

Disassembly of section .plt.got:

Disassembly of section .plt.sec:

Disassembly of section .text:

00000000000256f0 <gouraud_deob_draw_triangle>:
   256f0:	f3 0f 1e fa          	endbr64
   256f4:	41 57                	push   %r15
   256f6:	41 89 f3             	mov    %esi,%r11d
   256f9:	41 89 d2             	mov    %edx,%r10d
   256fc:	41 56                	push   %r14
   256fe:	41 55                	push   %r13
   25700:	41 89 cd             	mov    %ecx,%r13d
   25703:	29 f1                	sub    %esi,%ecx
   25705:	41 54                	push   %r12
   25707:	55                   	push   %rbp
   25708:	53                   	push   %rbx
   25709:	44 89 cb             	mov    %r9d,%ebx
   2570c:	44 29 c3             	sub    %r8d,%ebx
   2570f:	48 83 ec 48          	sub    $0x48,%rsp
   25713:	44 8b b4 24 80 00 00 	mov    0x80(%rsp),%r14d
   2571a:	00 
   2571b:	48 89 7c 24 10       	mov    %rdi,0x10(%rsp)
   25720:	89 d7                	mov    %edx,%edi
   25722:	29 f7                	sub    %esi,%edi
   25724:	44 89 f5             	mov    %r14d,%ebp
   25727:	44 29 c5             	sub    %r8d,%ebp
   2572a:	44 39 ea             	cmp    %r13d,%edx
   2572d:	0f 84 e5 02 00 00    	je     25a18 <gouraud_deob_draw_triangle+0x328>
   25733:	44 89 f0             	mov    %r14d,%eax
   25736:	44 89 ee             	mov    %r13d,%esi
   25739:	44 29 c8             	sub    %r9d,%eax
   2573c:	29 d6                	sub    %edx,%esi
   2573e:	c1 e0 0e             	shl    $0xe,%eax
   25741:	99                   	cltd
   25742:	f7 fe                	idiv   %esi
   25744:	89 44 24 20          	mov    %eax,0x20(%rsp)
   25748:	45 39 da             	cmp    %r11d,%r10d
   2574b:	0f 84 a7 02 00 00    	je     259f8 <gouraud_deob_draw_triangle+0x308>
   25751:	89 d8                	mov    %ebx,%eax
   25753:	c7 44 24 0c 00 00 00 	movl   $0x0,0xc(%rsp)
   2575a:	00 
   2575b:	c1 e0 0e             	shl    $0xe,%eax
   2575e:	99                   	cltd
   2575f:	f7 ff                	idiv   %edi
   25761:	89 44 24 18          	mov    %eax,0x18(%rsp)
   25765:	45 39 eb             	cmp    %r13d,%r11d
   25768:	0f 85 92 02 00 00    	jne    25a00 <gouraud_deob_draw_triangle+0x310>
   2576e:	89 de                	mov    %ebx,%esi
   25770:	89 f8                	mov    %edi,%eax
   25772:	0f af f1             	imul   %ecx,%esi
   25775:	0f af c5             	imul   %ebp,%eax
   25778:	29 c6                	sub    %eax,%esi
   2577a:	0f 84 65 02 00 00    	je     259e5 <gouraud_deob_draw_triangle+0x2f5>
   25780:	44 8b a4 24 90 00 00 	mov    0x90(%rsp),%r12d
   25787:	00 
   25788:	44 8b bc 24 98 00 00 	mov    0x98(%rsp),%r15d
   2578f:	00 
   25790:	44 2b a4 24 88 00 00 	sub    0x88(%rsp),%r12d
   25797:	00 
   25798:	44 2b bc 24 88 00 00 	sub    0x88(%rsp),%r15d
   2579f:	00 
   257a0:	41 0f af cc          	imul   %r12d,%ecx
   257a4:	41 0f af ff          	imul   %r15d,%edi
   257a8:	41 0f af df          	imul   %r15d,%ebx
   257ac:	44 0f af e5          	imul   %ebp,%r12d
   257b0:	29 f9                	sub    %edi,%ecx
   257b2:	c1 e1 08             	shl    $0x8,%ecx
   257b5:	89 c8                	mov    %ecx,%eax
   257b7:	44 29 e3             	sub    %r12d,%ebx
   257ba:	99                   	cltd
   257bb:	c1 e3 08             	shl    $0x8,%ebx
   257be:	f7 fe                	idiv   %esi
   257c0:	89 44 24 1c          	mov    %eax,0x1c(%rsp)
   257c4:	89 d8                	mov    %ebx,%eax
   257c6:	99                   	cltd
   257c7:	f7 fe                	idiv   %esi
   257c9:	45 39 ea             	cmp    %r13d,%r10d
   257cc:	89 c3                	mov    %eax,%ebx
   257ce:	44 89 e8             	mov    %r13d,%eax
   257d1:	41 0f 4e c2          	cmovle %r10d,%eax
   257d5:	41 39 c3             	cmp    %eax,%r11d
   257d8:	0f 8f 62 02 00 00    	jg     25a40 <gouraud_deob_draw_triangle+0x350>
   257de:	41 81 fb 7f 01 00 00 	cmp    $0x17f,%r11d
   257e5:	0f 8f fa 01 00 00    	jg     259e5 <gouraud_deob_draw_triangle+0x2f5>
   257eb:	b8 80 01 00 00       	mov    $0x180,%eax
   257f0:	8b 54 24 1c          	mov    0x1c(%rsp),%edx
   257f4:	8b ac 24 88 00 00 00 	mov    0x88(%rsp),%ebp
   257fb:	41 39 c2             	cmp    %eax,%r10d
   257fe:	89 c1                	mov    %eax,%ecx
   25800:	89 c7                	mov    %eax,%edi
   25802:	41 0f 4e ca          	cmovle %r10d,%ecx
   25806:	41 39 c5             	cmp    %eax,%r13d
   25809:	89 d0                	mov    %edx,%eax
   2580b:	41 0f 4e fd          	cmovle %r13d,%edi
   2580f:	c1 e5 08             	shl    $0x8,%ebp
   25812:	41 0f af c0          	imul   %r8d,%eax
   25816:	41 c1 e0 0e          	shl    $0xe,%r8d
   2581a:	45 89 c4             	mov    %r8d,%r12d
   2581d:	29 c5                	sub    %eax,%ebp
   2581f:	01 d5                	add    %edx,%ebp
   25821:	39 cf                	cmp    %ecx,%edi
   25823:	0f 8e 57 06 00 00    	jle    25e80 <gouraud_deob_draw_triangle+0x790>
   25829:	45 85 db             	test   %r11d,%r11d
   2582c:	0f 88 9e 0b 00 00    	js     263d0 <gouraud_deob_draw_triangle+0xce0>
   25832:	44 89 d8             	mov    %r11d,%eax
   25835:	45 89 c6             	mov    %r8d,%r14d
   25838:	c1 e0 09             	shl    $0x9,%eax
   2583b:	8b 54 24 18          	mov    0x18(%rsp),%edx
   2583f:	41 c1 e1 0e          	shl    $0xe,%r9d
   25843:	39 54 24 0c          	cmp    %edx,0xc(%rsp)
   25847:	45 89 cf             	mov    %r9d,%r15d
   2584a:	0f 9d c2             	setge  %dl
   2584d:	45 85 d2             	test   %r10d,%r10d
   25850:	0f 88 9a 0d 00 00    	js     265f0 <gouraud_deob_draw_triangle+0xf00>
   25856:	29 cf                	sub    %ecx,%edi
   25858:	41 39 cb             	cmp    %ecx,%r11d
   2585b:	0f 84 4f 15 00 00    	je     26db0 <gouraud_deob_draw_triangle+0x16c0>
   25861:	84 d2                	test   %dl,%dl
   25863:	0f 85 c7 13 00 00    	jne    26c30 <gouraud_deob_draw_triangle+0x1540>
   25869:	44 29 d9             	sub    %r11d,%ecx
   2586c:	41 89 ca             	mov    %ecx,%r10d
   2586f:	83 e9 01             	sub    $0x1,%ecx
   25872:	0f 88 e7 00 00 00    	js     2595f <gouraud_deob_draw_triangle+0x26f>
   25878:	41 c1 e2 09          	shl    $0x9,%r10d
   2587c:	89 7c 24 28          	mov    %edi,0x28(%rsp)
   25880:	41 89 e8             	mov    %ebp,%r8d
   25883:	45 89 f5             	mov    %r14d,%r13d
   25886:	44 89 4c 24 30       	mov    %r9d,0x30(%rsp)
   2588b:	41 01 c2             	add    %eax,%r10d
   2588e:	41 89 db             	mov    %ebx,%r11d
   25891:	89 c6                	mov    %eax,%esi
   25893:	89 44 24 34          	mov    %eax,0x34(%rsp)
   25897:	45 89 c7             	mov    %r8d,%r15d
   2589a:	44 89 d3             	mov    %r10d,%ebx
   2589d:	89 4c 24 3c          	mov    %ecx,0x3c(%rsp)
   258a1:	89 6c 24 2c          	mov    %ebp,0x2c(%rsp)
   258a5:	8b 6c 24 0c          	mov    0xc(%rsp),%ebp
   258a9:	44 89 74 24 38       	mov    %r14d,0x38(%rsp)
   258ae:	44 8b 74 24 18       	mov    0x18(%rsp),%r14d
   258b3:	0f 1f 44 00 00       	nopl   0x0(%rax,%rax,1)
   258b8:	44 89 e1             	mov    %r12d,%ecx
   258bb:	44 89 ea             	mov    %r13d,%edx
   258be:	44 89 e0             	mov    %r12d,%eax
   258c1:	c1 f9 0e             	sar    $0xe,%ecx
   258c4:	c1 fa 0e             	sar    $0xe,%edx
   258c7:	44 09 e8             	or     %r13d,%eax
   258ca:	78 41                	js     2590d <gouraud_deob_draw_triangle+0x21d>
   258cc:	81 fa 00 02 00 00    	cmp    $0x200,%edx
   258d2:	40 0f 9f c7          	setg   %dil
   258d6:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
   258dc:	41 0f 9f c1          	setg   %r9b
   258e0:	44 08 cf             	or     %r9b,%dil
   258e3:	75 28                	jne    2590d <gouraud_deob_draw_triangle+0x21d>
   258e5:	39 d1                	cmp    %edx,%ecx
   258e7:	7e 24                	jle    2590d <gouraud_deob_draw_triangle+0x21d>
   258e9:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
   258ee:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
   258f3:	45 89 f8             	mov    %r15d,%r8d
   258f6:	89 74 24 18          	mov    %esi,0x18(%rsp)
   258fa:	44 89 5c 24 24       	mov    %r11d,0x24(%rsp)
   258ff:	e8 2c fd ff ff       	call   25630 <gouraud_deob_draw_scanline.part.0>
   25904:	44 8b 5c 24 24       	mov    0x24(%rsp),%r11d
   25909:	8b 74 24 18          	mov    0x18(%rsp),%esi
   2590d:	81 c6 00 02 00 00    	add    $0x200,%esi
   25913:	41 01 ed             	add    %ebp,%r13d
   25916:	45 01 f4             	add    %r14d,%r12d
   25919:	45 01 df             	add    %r11d,%r15d
   2591c:	39 f3                	cmp    %esi,%ebx
   2591e:	75 98                	jne    258b8 <gouraud_deob_draw_triangle+0x1c8>
   25920:	8b 4c 24 3c          	mov    0x3c(%rsp),%ecx
   25924:	8b 74 24 0c          	mov    0xc(%rsp),%esi
   25928:	44 89 da             	mov    %r11d,%edx
   2592b:	44 89 db             	mov    %r11d,%ebx
   2592e:	44 8b 74 24 38       	mov    0x38(%rsp),%r14d
   25933:	8b 6c 24 2c          	mov    0x2c(%rsp),%ebp
   25937:	0f af d1             	imul   %ecx,%edx
   2593a:	8b 44 24 34          	mov    0x34(%rsp),%eax
   2593e:	8b 7c 24 28          	mov    0x28(%rsp),%edi
   25942:	41 01 f6             	add    %esi,%r14d
   25945:	0f af f1             	imul   %ecx,%esi
   25948:	44 01 dd             	add    %r11d,%ebp
   2594b:	c1 e1 09             	shl    $0x9,%ecx
   2594e:	44 8b 7c 24 30       	mov    0x30(%rsp),%r15d
   25953:	8d 84 08 00 02 00 00 	lea    0x200(%rax,%rcx,1),%eax
   2595a:	01 d5                	add    %edx,%ebp
   2595c:	41 01 f6             	add    %esi,%r14d
   2595f:	c1 e7 09             	shl    $0x9,%edi
   25962:	41 89 e8             	mov    %ebp,%r8d
   25965:	41 89 c5             	mov    %eax,%r13d
   25968:	8b 6c 24 20          	mov    0x20(%rsp),%ebp
   2596c:	41 89 fc             	mov    %edi,%r12d
   2596f:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
   25974:	41 01 c4             	add    %eax,%r12d
   25977:	89 d8                	mov    %ebx,%eax
   25979:	8b 5c 24 0c          	mov    0xc(%rsp),%ebx
   2597d:	0f 1f 00             	nopl   (%rax)
   25980:	44 89 f9             	mov    %r15d,%ecx
   25983:	44 89 f2             	mov    %r14d,%edx
   25986:	44 89 fe             	mov    %r15d,%esi
   25989:	c1 f9 0e             	sar    $0xe,%ecx
   2598c:	c1 fa 0e             	sar    $0xe,%edx
   2598f:	44 09 f6             	or     %r14d,%esi
   25992:	78 3c                	js     259d0 <gouraud_deob_draw_triangle+0x2e0>
   25994:	81 fa 00 02 00 00    	cmp    $0x200,%edx
   2599a:	40 0f 9f c6          	setg   %sil
   2599e:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
   259a4:	41 0f 9f c1          	setg   %r9b
   259a8:	44 08 ce             	or     %r9b,%sil
   259ab:	75 23                	jne    259d0 <gouraud_deob_draw_triangle+0x2e0>
   259ad:	39 d1                	cmp    %edx,%ecx
   259af:	7e 1f                	jle    259d0 <gouraud_deob_draw_triangle+0x2e0>
   259b1:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
   259b6:	44 89 ee             	mov    %r13d,%esi
   259b9:	89 44 24 18          	mov    %eax,0x18(%rsp)
   259bd:	44 89 44 24 0c       	mov    %r8d,0xc(%rsp)
   259c2:	e8 69 fc ff ff       	call   25630 <gouraud_deob_draw_scanline.part.0>
   259c7:	8b 44 24 18          	mov    0x18(%rsp),%eax
   259cb:	44 8b 44 24 0c       	mov    0xc(%rsp),%r8d
   259d0:	41 81 c5 00 02 00 00 	add    $0x200,%r13d
   259d7:	41 01 de             	add    %ebx,%r14d
   259da:	41 01 ef             	add    %ebp,%r15d
   259dd:	41 01 c0             	add    %eax,%r8d
   259e0:	45 39 ec             	cmp    %r13d,%r12d
   259e3:	75 9b                	jne    25980 <gouraud_deob_draw_triangle+0x290>
   259e5:	48 83 c4 48          	add    $0x48,%rsp
   259e9:	5b                   	pop    %rbx
   259ea:	5d                   	pop    %rbp
   259eb:	41 5c                	pop    %r12
   259ed:	41 5d                	pop    %r13
   259ef:	41 5e                	pop    %r14
   259f1:	41 5f                	pop    %r15
   259f3:	c3                   	ret
   259f4:	0f 1f 40 00          	nopl   0x0(%rax)
   259f8:	c7 44 24 18 00 00 00 	movl   $0x0,0x18(%rsp)
   259ff:	00 
   25a00:	89 e8                	mov    %ebp,%eax
   25a02:	c1 e0 0e             	shl    $0xe,%eax
   25a05:	99                   	cltd
   25a06:	f7 f9                	idiv   %ecx
   25a08:	89 44 24 0c          	mov    %eax,0xc(%rsp)
   25a0c:	e9 5d fd ff ff       	jmp    2576e <gouraud_deob_draw_triangle+0x7e>
   25a11:	0f 1f 80 00 00 00 00 	nopl   0x0(%rax)
   25a18:	39 f2                	cmp    %esi,%edx
   25a1a:	0f 84 40 04 00 00    	je     25e60 <gouraud_deob_draw_triangle+0x770>
   25a20:	89 d8                	mov    %ebx,%eax
   25a22:	c7 44 24 20 00 00 00 	movl   $0x0,0x20(%rsp)
   25a29:	00 
   25a2a:	c1 e0 0e             	shl    $0xe,%eax
   25a2d:	99                   	cltd
   25a2e:	f7 ff                	idiv   %edi
   25a30:	89 44 24 18          	mov    %eax,0x18(%rsp)
   25a34:	eb ca                	jmp    25a00 <gouraud_deob_draw_triangle+0x310>
   25a36:	66 2e 0f 1f 84 00 00 	cs nopw 0x0(%rax,%rax,1)
   25a3d:	00 00 00 
   25a40:	45 39 ea             	cmp    %r13d,%r10d
   25a43:	0f 8f 17 02 00 00    	jg     25c60 <gouraud_deob_draw_triangle+0x570>
   25a49:	41 81 fa 7f 01 00 00 	cmp    $0x17f,%r10d
   25a50:	7f 93                	jg     259e5 <gouraud_deob_draw_triangle+0x2f5>
   25a52:	b8 80 01 00 00       	mov    $0x180,%eax
   25a57:	8b 7c 24 1c          	mov    0x1c(%rsp),%edi
   25a5b:	41 39 c5             	cmp    %eax,%r13d
   25a5e:	41 0f 4e c5          	cmovle %r13d,%eax
   25a62:	89 fa                	mov    %edi,%edx
   25a64:	89 c1                	mov    %eax,%ecx
   25a66:	b8 80 01 00 00       	mov    $0x180,%eax
   25a6b:	41 39 c3             	cmp    %eax,%r11d
   25a6e:	41 0f 4e c3          	cmovle %r11d,%eax
   25a72:	41 0f af d1          	imul   %r9d,%edx
   25a76:	41 c1 e1 0e          	shl    $0xe,%r9d
   25a7a:	44 89 cd             	mov    %r9d,%ebp
   25a7d:	89 c6                	mov    %eax,%esi
   25a7f:	8b 84 24 90 00 00 00 	mov    0x90(%rsp),%eax
   25a86:	c1 e0 08             	shl    $0x8,%eax
   25a89:	29 d0                	sub    %edx,%eax
   25a8b:	01 c7                	add    %eax,%edi
   25a8d:	39 f1                	cmp    %esi,%ecx
   25a8f:	0f 8d bb 05 00 00    	jge    26050 <gouraud_deob_draw_triangle+0x960>
   25a95:	45 85 d2             	test   %r10d,%r10d
   25a98:	0f 88 62 0d 00 00    	js     26800 <gouraud_deob_draw_triangle+0x1110>
   25a9e:	45 89 d7             	mov    %r10d,%r15d
   25aa1:	45 89 cc             	mov    %r9d,%r12d
   25aa4:	41 c1 e7 09          	shl    $0x9,%r15d
   25aa8:	8b 54 24 18          	mov    0x18(%rsp),%edx
   25aac:	41 c1 e6 0e          	shl    $0xe,%r14d
   25ab0:	39 54 24 20          	cmp    %edx,0x20(%rsp)
   25ab4:	0f 9e c0             	setle  %al
   25ab7:	45 85 ed             	test   %r13d,%r13d
   25aba:	0f 88 98 0b 00 00    	js     26658 <gouraud_deob_draw_triangle+0xf68>
   25ac0:	29 ce                	sub    %ecx,%esi
   25ac2:	41 39 ca             	cmp    %ecx,%r10d
   25ac5:	0f 84 7c 14 00 00    	je     26f47 <gouraud_deob_draw_triangle+0x1857>
   25acb:	84 c0                	test   %al,%al
   25acd:	0f 85 f5 12 00 00    	jne    26dc8 <gouraud_deob_draw_triangle+0x16d8>
   25ad3:	89 c8                	mov    %ecx,%eax
   25ad5:	44 29 d0             	sub    %r10d,%eax
   25ad8:	89 c1                	mov    %eax,%ecx
   25ada:	83 e9 01             	sub    $0x1,%ecx
   25add:	0f 88 e8 00 00 00    	js     25bcb <gouraud_deob_draw_triangle+0x4db>
   25ae3:	c1 e0 09             	shl    $0x9,%eax
   25ae6:	44 89 7c 24 30       	mov    %r15d,0x30(%rsp)
   25aeb:	45 89 e5             	mov    %r12d,%r13d
   25aee:	89 4c 24 34          	mov    %ecx,0x34(%rsp)
   25af2:	46 8d 14 38          	lea    (%rax,%r15,1),%r10d
   25af6:	89 d8                	mov    %ebx,%eax
   25af8:	8b 5c 24 18          	mov    0x18(%rsp),%ebx
   25afc:	89 7c 24 38          	mov    %edi,0x38(%rsp)
   25b00:	89 74 24 3c          	mov    %esi,0x3c(%rsp)
   25b04:	44 89 64 24 2c       	mov    %r12d,0x2c(%rsp)
   25b09:	44 8b 64 24 20       	mov    0x20(%rsp),%r12d
   25b0e:	44 89 74 24 28       	mov    %r14d,0x28(%rsp)
   25b13:	41 89 fe             	mov    %edi,%r14d
   25b16:	66 2e 0f 1f 84 00 00 	cs nopw 0x0(%rax,%rax,1)
   25b1d:	00 00 00 
   25b20:	89 e9                	mov    %ebp,%ecx
   25b22:	44 89 ea             	mov    %r13d,%edx
   25b25:	89 ef                	mov    %ebp,%edi
   25b27:	c1 f9 0e             	sar    $0xe,%ecx
   25b2a:	c1 fa 0e             	sar    $0xe,%edx
   25b2d:	44 09 ef             	or     %r13d,%edi
   25b30:	78 44                	js     25b76 <gouraud_deob_draw_triangle+0x486>
   25b32:	81 fa 00 02 00 00    	cmp    $0x200,%edx
   25b38:	40 0f 9f c7          	setg   %dil
   25b3c:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
   25b42:	41 0f 9f c1          	setg   %r9b
   25b46:	44 08 cf             	or     %r9b,%dil
   25b49:	75 2b                	jne    25b76 <gouraud_deob_draw_triangle+0x486>
   25b4b:	39 d1                	cmp    %edx,%ecx
   25b4d:	7e 27                	jle    25b76 <gouraud_deob_draw_triangle+0x486>
   25b4f:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
   25b54:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
   25b59:	45 89 f0             	mov    %r14d,%r8d
   25b5c:	44 89 fe             	mov    %r15d,%esi
   25b5f:	89 44 24 24          	mov    %eax,0x24(%rsp)
   25b63:	44 89 54 24 20       	mov    %r10d,0x20(%rsp)
   25b68:	e8 c3 fa ff ff       	call   25630 <gouraud_deob_draw_scanline.part.0>
   25b6d:	8b 44 24 24          	mov    0x24(%rsp),%eax
   25b71:	44 8b 54 24 20       	mov    0x20(%rsp),%r10d
   25b76:	41 81 c7 00 02 00 00 	add    $0x200,%r15d
   25b7d:	41 01 dd             	add    %ebx,%r13d
   25b80:	44 01 e5             	add    %r12d,%ebp
   25b83:	41 01 c6             	add    %eax,%r14d
   25b86:	45 39 d7             	cmp    %r10d,%r15d
   25b89:	75 95                	jne    25b20 <gouraud_deob_draw_triangle+0x430>
   25b8b:	8b 4c 24 34          	mov    0x34(%rsp),%ecx
   25b8f:	89 c3                	mov    %eax,%ebx
   25b91:	44 8b 64 24 2c       	mov    0x2c(%rsp),%r12d
   25b96:	8b 44 24 18          	mov    0x18(%rsp),%eax
   25b9a:	89 da                	mov    %ebx,%edx
   25b9c:	8b 7c 24 38          	mov    0x38(%rsp),%edi
   25ba0:	0f af d1             	imul   %ecx,%edx
   25ba3:	44 8b 7c 24 30       	mov    0x30(%rsp),%r15d
   25ba8:	44 8b 74 24 28       	mov    0x28(%rsp),%r14d
   25bad:	41 01 c4             	add    %eax,%r12d
   25bb0:	0f af c1             	imul   %ecx,%eax
   25bb3:	8b 74 24 3c          	mov    0x3c(%rsp),%esi
   25bb7:	c1 e1 09             	shl    $0x9,%ecx
   25bba:	45 8d bc 0f 00 02 00 	lea    0x200(%r15,%rcx,1),%r15d
   25bc1:	00 
   25bc2:	41 01 c4             	add    %eax,%r12d
   25bc5:	8d 04 1f             	lea    (%rdi,%rbx,1),%eax
   25bc8:	8d 3c 02             	lea    (%rdx,%rax,1),%edi
   25bcb:	c1 e6 09             	shl    $0x9,%esi
   25bce:	41 89 f8             	mov    %edi,%r8d
   25bd1:	89 d8                	mov    %ebx,%eax
   25bd3:	44 8b 6c 24 0c       	mov    0xc(%rsp),%r13d
   25bd8:	89 f5                	mov    %esi,%ebp
   25bda:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
   25bdf:	8b 5c 24 18          	mov    0x18(%rsp),%ebx
   25be3:	44 01 fd             	add    %r15d,%ebp
   25be6:	66 2e 0f 1f 84 00 00 	cs nopw 0x0(%rax,%rax,1)
   25bed:	00 00 00 
   25bf0:	44 89 f1             	mov    %r14d,%ecx
   25bf3:	44 89 e2             	mov    %r12d,%edx
   25bf6:	44 89 f6             	mov    %r14d,%esi
   25bf9:	c1 f9 0e             	sar    $0xe,%ecx
   25bfc:	c1 fa 0e             	sar    $0xe,%edx
   25bff:	44 09 e6             	or     %r12d,%esi
   25c02:	78 3c                	js     25c40 <gouraud_deob_draw_triangle+0x550>
   25c04:	81 fa 00 02 00 00    	cmp    $0x200,%edx
   25c0a:	40 0f 9f c6          	setg   %sil
   25c0e:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
   25c14:	41 0f 9f c1          	setg   %r9b
   25c18:	44 08 ce             	or     %r9b,%sil
   25c1b:	75 23                	jne    25c40 <gouraud_deob_draw_triangle+0x550>
   25c1d:	39 d1                	cmp    %edx,%ecx
   25c1f:	7e 1f                	jle    25c40 <gouraud_deob_draw_triangle+0x550>
   25c21:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
   25c26:	44 89 fe             	mov    %r15d,%esi
   25c29:	89 44 24 18          	mov    %eax,0x18(%rsp)
   25c2d:	44 89 44 24 0c       	mov    %r8d,0xc(%rsp)
   25c32:	e8 f9 f9 ff ff       	call   25630 <gouraud_deob_draw_scanline.part.0>
   25c37:	8b 44 24 18          	mov    0x18(%rsp),%eax
   25c3b:	44 8b 44 24 0c       	mov    0xc(%rsp),%r8d
   25c40:	41 81 c7 00 02 00 00 	add    $0x200,%r15d
   25c47:	41 01 dc             	add    %ebx,%r12d
   25c4a:	45 01 ee             	add    %r13d,%r14d
   25c4d:	41 01 c0             	add    %eax,%r8d
   25c50:	41 39 ef             	cmp    %ebp,%r15d
   25c53:	75 9b                	jne    25bf0 <gouraud_deob_draw_triangle+0x500>
   25c55:	e9 8b fd ff ff       	jmp    259e5 <gouraud_deob_draw_triangle+0x2f5>
   25c5a:	66 0f 1f 44 00 00    	nopw   0x0(%rax,%rax,1)
   25c60:	41 81 fd 7f 01 00 00 	cmp    $0x17f,%r13d
   25c67:	0f 8f 78 fd ff ff    	jg     259e5 <gouraud_deob_draw_triangle+0x2f5>
   25c6d:	b8 80 01 00 00       	mov    $0x180,%eax
   25c72:	8b 7c 24 1c          	mov    0x1c(%rsp),%edi
   25c76:	41 39 c3             	cmp    %eax,%r11d
   25c79:	41 0f 4e c3          	cmovle %r11d,%eax
   25c7d:	89 fa                	mov    %edi,%edx
   25c7f:	89 c1                	mov    %eax,%ecx
   25c81:	b8 80 01 00 00       	mov    $0x180,%eax
   25c86:	41 39 c2             	cmp    %eax,%r10d
   25c89:	41 0f 4e c2          	cmovle %r10d,%eax
   25c8d:	41 0f af d6          	imul   %r14d,%edx
   25c91:	41 c1 e6 0e          	shl    $0xe,%r14d
   25c95:	89 c6                	mov    %eax,%esi
   25c97:	8b 84 24 98 00 00 00 	mov    0x98(%rsp),%eax
   25c9e:	c1 e0 08             	shl    $0x8,%eax
   25ca1:	29 d0                	sub    %edx,%eax
   25ca3:	01 c7                	add    %eax,%edi
   25ca5:	39 ce                	cmp    %ecx,%esi
   25ca7:	0f 8e 63 05 00 00    	jle    26210 <gouraud_deob_draw_triangle+0xb20>
   25cad:	45 85 ed             	test   %r13d,%r13d
   25cb0:	0f 88 ea 0a 00 00    	js     267a0 <gouraud_deob_draw_triangle+0x10b0>
   25cb6:	45 89 ef             	mov    %r13d,%r15d
   25cb9:	44 89 f5             	mov    %r14d,%ebp
   25cbc:	41 c1 e7 09          	shl    $0x9,%r15d
   25cc0:	41 c1 e0 0e          	shl    $0xe,%r8d
   25cc4:	45 89 c4             	mov    %r8d,%r12d
   25cc7:	45 85 db             	test   %r11d,%r11d
   25cca:	0f 88 a0 0a 00 00    	js     26770 <gouraud_deob_draw_triangle+0x1080>
   25cd0:	89 c8                	mov    %ecx,%eax
   25cd2:	8b 54 24 0c          	mov    0xc(%rsp),%edx
   25cd6:	44 8b 44 24 20       	mov    0x20(%rsp),%r8d
   25cdb:	29 ce                	sub    %ecx,%esi
   25cdd:	44 29 e8             	sub    %r13d,%eax
   25ce0:	8d 48 ff             	lea    -0x1(%rax),%ecx
   25ce3:	44 39 c2             	cmp    %r8d,%edx
   25ce6:	0f 8e d7 0d 00 00    	jle    26ac3 <gouraud_deob_draw_triangle+0x13d3>
   25cec:	85 c9                	test   %ecx,%ecx
   25cee:	0f 88 df 00 00 00    	js     25dd3 <gouraud_deob_draw_triangle+0x6e3>
   25cf4:	89 7c 24 34          	mov    %edi,0x34(%rsp)
   25cf8:	45 89 fa             	mov    %r15d,%r10d
   25cfb:	41 89 ed             	mov    %ebp,%r13d
   25cfe:	41 89 db             	mov    %ebx,%r11d
   25d01:	89 4c 24 3c          	mov    %ecx,0x3c(%rsp)
   25d05:	89 d3                	mov    %edx,%ebx
   25d07:	89 6c 24 28          	mov    %ebp,0x28(%rsp)
   25d0b:	89 cd                	mov    %ecx,%ebp
   25d0d:	44 89 64 24 2c       	mov    %r12d,0x2c(%rsp)
   25d12:	45 89 c4             	mov    %r8d,%r12d
   25d15:	44 89 7c 24 30       	mov    %r15d,0x30(%rsp)
   25d1a:	41 89 ff             	mov    %edi,%r15d
   25d1d:	89 74 24 38          	mov    %esi,0x38(%rsp)
   25d21:	44 89 d6             	mov    %r10d,%esi
   25d24:	0f 1f 40 00          	nopl   0x0(%rax)
   25d28:	44 89 f1             	mov    %r14d,%ecx
   25d2b:	44 89 ea             	mov    %r13d,%edx
   25d2e:	44 89 f0             	mov    %r14d,%eax
   25d31:	c1 f9 0e             	sar    $0xe,%ecx
   25d34:	c1 fa 0e             	sar    $0xe,%edx
   25d37:	44 09 e8             	or     %r13d,%eax
   25d3a:	78 41                	js     25d7d <gouraud_deob_draw_triangle+0x68d>
   25d3c:	81 fa 00 02 00 00    	cmp    $0x200,%edx
   25d42:	40 0f 9f c7          	setg   %dil
   25d46:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
   25d4c:	41 0f 9f c1          	setg   %r9b
   25d50:	44 08 cf             	or     %r9b,%dil
   25d53:	75 28                	jne    25d7d <gouraud_deob_draw_triangle+0x68d>
   25d55:	39 d1                	cmp    %edx,%ecx
   25d57:	7e 24                	jle    25d7d <gouraud_deob_draw_triangle+0x68d>
   25d59:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
   25d5e:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
   25d63:	45 89 f8             	mov    %r15d,%r8d
   25d66:	89 74 24 0c          	mov    %esi,0xc(%rsp)
   25d6a:	44 89 5c 24 24       	mov    %r11d,0x24(%rsp)
   25d6f:	e8 bc f8 ff ff       	call   25630 <gouraud_deob_draw_scanline.part.0>
   25d74:	44 8b 5c 24 24       	mov    0x24(%rsp),%r11d
   25d79:	8b 74 24 0c          	mov    0xc(%rsp),%esi
   25d7d:	45 01 e5             	add    %r12d,%r13d
   25d80:	41 01 de             	add    %ebx,%r14d
   25d83:	45 01 df             	add    %r11d,%r15d
   25d86:	81 c6 00 02 00 00    	add    $0x200,%esi
   25d8c:	83 ed 01             	sub    $0x1,%ebp
   25d8f:	73 97                	jae    25d28 <gouraud_deob_draw_triangle+0x638>
   25d91:	8b 4c 24 3c          	mov    0x3c(%rsp),%ecx
   25d95:	8b 44 24 20          	mov    0x20(%rsp),%eax
   25d99:	44 89 da             	mov    %r11d,%edx
   25d9c:	44 89 db             	mov    %r11d,%ebx
   25d9f:	8b 6c 24 28          	mov    0x28(%rsp),%ebp
   25da3:	8b 7c 24 34          	mov    0x34(%rsp),%edi
   25da7:	0f af d1             	imul   %ecx,%edx
   25daa:	44 8b 7c 24 30       	mov    0x30(%rsp),%r15d
   25daf:	44 8b 64 24 2c       	mov    0x2c(%rsp),%r12d
   25db4:	01 c5                	add    %eax,%ebp
   25db6:	0f af c1             	imul   %ecx,%eax
   25db9:	8b 74 24 38          	mov    0x38(%rsp),%esi
   25dbd:	01 c5                	add    %eax,%ebp
   25dbf:	42 8d 04 1f          	lea    (%rdi,%r11,1),%eax
   25dc3:	8d 3c 02             	lea    (%rdx,%rax,1),%edi
   25dc6:	89 c8                	mov    %ecx,%eax
   25dc8:	c1 e0 09             	shl    $0x9,%eax
   25dcb:	45 8d bc 07 00 02 00 	lea    0x200(%r15,%rax,1),%r15d
   25dd2:	00 
   25dd3:	c1 e6 09             	shl    $0x9,%esi
   25dd6:	41 89 f8             	mov    %edi,%r8d
   25dd9:	89 d8                	mov    %ebx,%eax
   25ddb:	44 8b 74 24 20       	mov    0x20(%rsp),%r14d
   25de0:	41 89 f5             	mov    %esi,%r13d
   25de3:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
   25de8:	8b 5c 24 18          	mov    0x18(%rsp),%ebx
   25dec:	45 01 fd             	add    %r15d,%r13d
   25def:	90                   	nop
   25df0:	44 89 e1             	mov    %r12d,%ecx
   25df3:	89 ea                	mov    %ebp,%edx
   25df5:	44 89 e6             	mov    %r12d,%esi
   25df8:	c1 f9 0e             	sar    $0xe,%ecx
   25dfb:	c1 fa 0e             	sar    $0xe,%edx
   25dfe:	09 ee                	or     %ebp,%esi
   25e00:	78 3c                	js     25e3e <gouraud_deob_draw_triangle+0x74e>
   25e02:	81 fa 00 02 00 00    	cmp    $0x200,%edx
   25e08:	40 0f 9f c6          	setg   %sil
   25e0c:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
   25e12:	41 0f 9f c1          	setg   %r9b
   25e16:	44 08 ce             	or     %r9b,%sil
   25e19:	75 23                	jne    25e3e <gouraud_deob_draw_triangle+0x74e>
   25e1b:	39 d1                	cmp    %edx,%ecx
   25e1d:	7e 1f                	jle    25e3e <gouraud_deob_draw_triangle+0x74e>
   25e1f:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
   25e24:	44 89 fe             	mov    %r15d,%esi
   25e27:	89 44 24 18          	mov    %eax,0x18(%rsp)
   25e2b:	44 89 44 24 0c       	mov    %r8d,0xc(%rsp)
   25e30:	e8 fb f7 ff ff       	call   25630 <gouraud_deob_draw_scanline.part.0>
   25e35:	8b 44 24 18          	mov    0x18(%rsp),%eax
   25e39:	44 8b 44 24 0c       	mov    0xc(%rsp),%r8d
   25e3e:	41 81 c7 00 02 00 00 	add    $0x200,%r15d
   25e45:	44 01 f5             	add    %r14d,%ebp
   25e48:	41 01 dc             	add    %ebx,%r12d
   25e4b:	41 01 c0             	add    %eax,%r8d
   25e4e:	45 39 fd             	cmp    %r15d,%r13d
   25e51:	75 9d                	jne    25df0 <gouraud_deob_draw_triangle+0x700>
   25e53:	e9 8d fb ff ff       	jmp    259e5 <gouraud_deob_draw_triangle+0x2f5>
   25e58:	0f 1f 84 00 00 00 00 	nopl   0x0(%rax,%rax,1)
   25e5f:	00 
   25e60:	c7 44 24 18 00 00 00 	movl   $0x0,0x18(%rsp)
   25e67:	00 
   25e68:	c7 44 24 20 00 00 00 	movl   $0x0,0x20(%rsp)
   25e6f:	00 
   25e70:	c7 44 24 0c 00 00 00 	movl   $0x0,0xc(%rsp)
   25e77:	00 
   25e78:	e9 f1 f8 ff ff       	jmp    2576e <gouraud_deob_draw_triangle+0x7e>
   25e7d:	0f 1f 00             	nopl   (%rax)
   25e80:	45 85 db             	test   %r11d,%r11d
   25e83:	0f 88 77 05 00 00    	js     26400 <gouraud_deob_draw_triangle+0xd10>
   25e89:	44 89 d8             	mov    %r11d,%eax
   25e8c:	8b 54 24 18          	mov    0x18(%rsp),%edx
   25e90:	41 c1 e6 0e          	shl    $0xe,%r14d
   25e94:	45 89 c7             	mov    %r8d,%r15d
   25e97:	c1 e0 09             	shl    $0x9,%eax
   25e9a:	39 54 24 0c          	cmp    %edx,0xc(%rsp)
   25e9e:	0f 9c c2             	setl   %dl
   25ea1:	45 85 ed             	test   %r13d,%r13d
   25ea4:	0f 88 86 07 00 00    	js     26630 <gouraud_deob_draw_triangle+0xf40>
   25eaa:	29 f9                	sub    %edi,%ecx
   25eac:	44 39 df             	cmp    %r11d,%edi
   25eaf:	0f 84 ac 05 00 00    	je     26461 <gouraud_deob_draw_triangle+0xd71>
   25eb5:	84 d2                	test   %dl,%dl
   25eb7:	0f 84 9f 05 00 00    	je     2645c <gouraud_deob_draw_triangle+0xd6c>
   25ebd:	44 29 df             	sub    %r11d,%edi
   25ec0:	41 89 fa             	mov    %edi,%r10d
   25ec3:	83 ef 01             	sub    $0x1,%edi
   25ec6:	0f 88 ea 00 00 00    	js     25fb6 <gouraud_deob_draw_triangle+0x8c6>
   25ecc:	89 44 24 30          	mov    %eax,0x30(%rsp)
   25ed0:	41 89 e8             	mov    %ebp,%r8d
   25ed3:	41 c1 e2 09          	shl    $0x9,%r10d
   25ed7:	41 89 db             	mov    %ebx,%r11d
   25eda:	89 7c 24 34          	mov    %edi,0x34(%rsp)
   25ede:	8b 5c 24 18          	mov    0x18(%rsp),%ebx
   25ee2:	45 89 fd             	mov    %r15d,%r13d
   25ee5:	41 01 c2             	add    %eax,%r10d
   25ee8:	89 4c 24 38          	mov    %ecx,0x38(%rsp)
   25eec:	89 6c 24 2c          	mov    %ebp,0x2c(%rsp)
   25ef0:	8b 6c 24 0c          	mov    0xc(%rsp),%ebp
   25ef4:	44 89 74 24 28       	mov    %r14d,0x28(%rsp)
   25ef9:	45 89 c6             	mov    %r8d,%r14d
   25efc:	44 89 7c 24 3c       	mov    %r15d,0x3c(%rsp)
   25f01:	41 89 c7             	mov    %eax,%r15d
   25f04:	0f 1f 40 00          	nopl   0x0(%rax)
   25f08:	44 89 e9             	mov    %r13d,%ecx
   25f0b:	44 89 e2             	mov    %r12d,%edx
   25f0e:	44 89 e8             	mov    %r13d,%eax
   25f11:	c1 f9 0e             	sar    $0xe,%ecx
   25f14:	c1 fa 0e             	sar    $0xe,%edx
   25f17:	44 09 e0             	or     %r12d,%eax
   25f1a:	78 46                	js     25f62 <gouraud_deob_draw_triangle+0x872>
   25f1c:	81 fa 00 02 00 00    	cmp    $0x200,%edx
   25f22:	40 0f 9f c7          	setg   %dil
   25f26:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
   25f2c:	41 0f 9f c1          	setg   %r9b
   25f30:	44 08 cf             	or     %r9b,%dil
   25f33:	75 2d                	jne    25f62 <gouraud_deob_draw_triangle+0x872>
   25f35:	39 d1                	cmp    %edx,%ecx
   25f37:	7e 29                	jle    25f62 <gouraud_deob_draw_triangle+0x872>
   25f39:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
   25f3e:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
   25f43:	45 89 f0             	mov    %r14d,%r8d
   25f46:	44 89 fe             	mov    %r15d,%esi
   25f49:	44 89 5c 24 24       	mov    %r11d,0x24(%rsp)
   25f4e:	44 89 54 24 0c       	mov    %r10d,0xc(%rsp)
   25f53:	e8 d8 f6 ff ff       	call   25630 <gouraud_deob_draw_scanline.part.0>
   25f58:	44 8b 5c 24 24       	mov    0x24(%rsp),%r11d
   25f5d:	44 8b 54 24 0c       	mov    0xc(%rsp),%r10d
   25f62:	41 81 c7 00 02 00 00 	add    $0x200,%r15d
   25f69:	41 01 ec             	add    %ebp,%r12d
   25f6c:	41 01 dd             	add    %ebx,%r13d
   25f6f:	45 01 de             	add    %r11d,%r14d
   25f72:	45 39 d7             	cmp    %r10d,%r15d
   25f75:	75 91                	jne    25f08 <gouraud_deob_draw_triangle+0x818>
   25f77:	8b 7c 24 34          	mov    0x34(%rsp),%edi
   25f7b:	8b 74 24 18          	mov    0x18(%rsp),%esi
   25f7f:	44 89 da             	mov    %r11d,%edx
   25f82:	44 89 db             	mov    %r11d,%ebx
   25f85:	44 8b 7c 24 3c       	mov    0x3c(%rsp),%r15d
   25f8a:	8b 6c 24 2c          	mov    0x2c(%rsp),%ebp
   25f8e:	0f af d7             	imul   %edi,%edx
   25f91:	8b 44 24 30          	mov    0x30(%rsp),%eax
   25f95:	44 8b 74 24 28       	mov    0x28(%rsp),%r14d
   25f9a:	41 01 f7             	add    %esi,%r15d
   25f9d:	0f af f7             	imul   %edi,%esi
   25fa0:	44 01 dd             	add    %r11d,%ebp
   25fa3:	c1 e7 09             	shl    $0x9,%edi
   25fa6:	8b 4c 24 38          	mov    0x38(%rsp),%ecx
   25faa:	8d 84 38 00 02 00 00 	lea    0x200(%rax,%rdi,1),%eax
   25fb1:	01 d5                	add    %edx,%ebp
   25fb3:	41 01 f7             	add    %esi,%r15d
   25fb6:	85 c9                	test   %ecx,%ecx
   25fb8:	0f 8e 27 fa ff ff    	jle    259e5 <gouraud_deob_draw_triangle+0x2f5>
   25fbe:	c1 e1 09             	shl    $0x9,%ecx
   25fc1:	41 89 e8             	mov    %ebp,%r8d
   25fc4:	41 89 c5             	mov    %eax,%r13d
   25fc7:	8b 6c 24 18          	mov    0x18(%rsp),%ebp
   25fcb:	41 89 cc             	mov    %ecx,%r12d
   25fce:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
   25fd3:	41 01 c4             	add    %eax,%r12d
   25fd6:	89 d8                	mov    %ebx,%eax
   25fd8:	8b 5c 24 20          	mov    0x20(%rsp),%ebx
   25fdc:	0f 1f 40 00          	nopl   0x0(%rax)
   25fe0:	44 89 f9             	mov    %r15d,%ecx
   25fe3:	44 89 f2             	mov    %r14d,%edx
   25fe6:	44 89 fe             	mov    %r15d,%esi
   25fe9:	c1 f9 0e             	sar    $0xe,%ecx
   25fec:	c1 fa 0e             	sar    $0xe,%edx
   25fef:	44 09 f6             	or     %r14d,%esi
   25ff2:	78 3c                	js     26030 <gouraud_deob_draw_triangle+0x940>
   25ff4:	81 fa 00 02 00 00    	cmp    $0x200,%edx
   25ffa:	40 0f 9f c6          	setg   %sil
   25ffe:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
   26004:	41 0f 9f c1          	setg   %r9b
   26008:	44 08 ce             	or     %r9b,%sil
   2600b:	75 23                	jne    26030 <gouraud_deob_draw_triangle+0x940>
   2600d:	39 d1                	cmp    %edx,%ecx
   2600f:	7e 1f                	jle    26030 <gouraud_deob_draw_triangle+0x940>
   26011:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
   26016:	44 89 ee             	mov    %r13d,%esi
   26019:	89 44 24 18          	mov    %eax,0x18(%rsp)
   2601d:	44 89 44 24 0c       	mov    %r8d,0xc(%rsp)
   26022:	e8 09 f6 ff ff       	call   25630 <gouraud_deob_draw_scanline.part.0>
   26027:	8b 44 24 18          	mov    0x18(%rsp),%eax
   2602b:	44 8b 44 24 0c       	mov    0xc(%rsp),%r8d
   26030:	41 81 c5 00 02 00 00 	add    $0x200,%r13d
   26037:	41 01 de             	add    %ebx,%r14d
   2603a:	41 01 ef             	add    %ebp,%r15d
   2603d:	41 01 c0             	add    %eax,%r8d
   26040:	45 39 ec             	cmp    %r13d,%r12d
   26043:	75 9b                	jne    25fe0 <gouraud_deob_draw_triangle+0x8f0>
   26045:	e9 9b f9 ff ff       	jmp    259e5 <gouraud_deob_draw_triangle+0x2f5>
   2604a:	66 0f 1f 44 00 00    	nopw   0x0(%rax,%rax,1)
   26050:	45 85 d2             	test   %r10d,%r10d
   26053:	0f 88 77 07 00 00    	js     267d0 <gouraud_deob_draw_triangle+0x10e0>
   26059:	45 89 d6             	mov    %r10d,%r14d
   2605c:	45 89 cd             	mov    %r9d,%r13d
   2605f:	41 c1 e6 09          	shl    $0x9,%r14d
   26063:	41 c1 e0 0e          	shl    $0xe,%r8d
   26067:	45 89 c4             	mov    %r8d,%r12d
   2606a:	45 85 db             	test   %r11d,%r11d
   2606d:	0f 88 bd 07 00 00    	js     26830 <gouraud_deob_draw_triangle+0x1140>
   26073:	89 f0                	mov    %esi,%eax
   26075:	29 f1                	sub    %esi,%ecx
   26077:	44 29 d0             	sub    %r10d,%eax
   2607a:	44 8b 54 24 18       	mov    0x18(%rsp),%r10d
   2607f:	8d 70 ff             	lea    -0x1(%rax),%esi
   26082:	44 39 54 24 20       	cmp    %r10d,0x20(%rsp)
   26087:	0f 8e 4d 08 00 00    	jle    268da <gouraud_deob_draw_triangle+0x11ea>
   2608d:	85 f6                	test   %esi,%esi
   2608f:	0f 88 e8 00 00 00    	js     2617d <gouraud_deob_draw_triangle+0xa8d>
   26095:	44 89 44 24 28       	mov    %r8d,0x28(%rsp)
   2609a:	45 89 f2             	mov    %r14d,%r10d
   2609d:	45 89 ef             	mov    %r13d,%r15d
   260a0:	41 89 db             	mov    %ebx,%r11d
   260a3:	89 4c 24 2c          	mov    %ecx,0x2c(%rsp)
   260a7:	8b 5c 24 20          	mov    0x20(%rsp),%ebx
   260ab:	41 89 f4             	mov    %esi,%r12d
   260ae:	89 7c 24 38          	mov    %edi,0x38(%rsp)
   260b2:	44 89 6c 24 34       	mov    %r13d,0x34(%rsp)
   260b7:	44 8b 6c 24 18       	mov    0x18(%rsp),%r13d
   260bc:	44 89 74 24 30       	mov    %r14d,0x30(%rsp)
   260c1:	41 89 fe             	mov    %edi,%r14d
   260c4:	89 74 24 3c          	mov    %esi,0x3c(%rsp)
   260c8:	44 89 d6             	mov    %r10d,%esi
   260cb:	0f 1f 44 00 00       	nopl   0x0(%rax,%rax,1)
   260d0:	44 89 f9             	mov    %r15d,%ecx
   260d3:	89 ea                	mov    %ebp,%edx
   260d5:	44 89 f8             	mov    %r15d,%eax
   260d8:	c1 f9 0e             	sar    $0xe,%ecx
   260db:	c1 fa 0e             	sar    $0xe,%edx
   260de:	09 e8                	or     %ebp,%eax
   260e0:	78 41                	js     26123 <gouraud_deob_draw_triangle+0xa33>
   260e2:	81 fa 00 02 00 00    	cmp    $0x200,%edx
   260e8:	40 0f 9f c7          	setg   %dil
   260ec:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
   260f2:	41 0f 9f c1          	setg   %r9b
   260f6:	44 08 cf             	or     %r9b,%dil
   260f9:	75 28                	jne    26123 <gouraud_deob_draw_triangle+0xa33>
   260fb:	39 d1                	cmp    %edx,%ecx
   260fd:	7e 24                	jle    26123 <gouraud_deob_draw_triangle+0xa33>
   260ff:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
   26104:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
   26109:	45 89 f0             	mov    %r14d,%r8d
   2610c:	89 74 24 18          	mov    %esi,0x18(%rsp)
   26110:	44 89 5c 24 24       	mov    %r11d,0x24(%rsp)
   26115:	e8 16 f5 ff ff       	call   25630 <gouraud_deob_draw_scanline.part.0>
   2611a:	44 8b 5c 24 24       	mov    0x24(%rsp),%r11d
   2611f:	8b 74 24 18          	mov    0x18(%rsp),%esi
   26123:	44 01 ed             	add    %r13d,%ebp
   26126:	41 01 df             	add    %ebx,%r15d
   26129:	45 01 de             	add    %r11d,%r14d
   2612c:	81 c6 00 02 00 00    	add    $0x200,%esi
   26132:	41 83 ec 01          	sub    $0x1,%r12d
   26136:	73 98                	jae    260d0 <gouraud_deob_draw_triangle+0x9e0>
   26138:	8b 74 24 3c          	mov    0x3c(%rsp),%esi
   2613c:	8b 44 24 20          	mov    0x20(%rsp),%eax
   26140:	44 89 da             	mov    %r11d,%edx
   26143:	44 89 db             	mov    %r11d,%ebx
   26146:	44 8b 6c 24 34       	mov    0x34(%rsp),%r13d
   2614b:	8b 7c 24 38          	mov    0x38(%rsp),%edi
   2614f:	0f af d6             	imul   %esi,%edx
   26152:	44 8b 74 24 30       	mov    0x30(%rsp),%r14d
   26157:	44 8b 64 24 28       	mov    0x28(%rsp),%r12d
   2615c:	41 01 c5             	add    %eax,%r13d
   2615f:	0f af c6             	imul   %esi,%eax
   26162:	8b 4c 24 2c          	mov    0x2c(%rsp),%ecx
   26166:	41 01 c5             	add    %eax,%r13d
   26169:	42 8d 04 1f          	lea    (%rdi,%r11,1),%eax
   2616d:	8d 3c 02             	lea    (%rdx,%rax,1),%edi
   26170:	89 f0                	mov    %esi,%eax
   26172:	c1 e0 09             	shl    $0x9,%eax
   26175:	45 8d b4 06 00 02 00 	lea    0x200(%r14,%rax,1),%r14d
   2617c:	00 
   2617d:	85 c9                	test   %ecx,%ecx
   2617f:	0f 8e 60 f8 ff ff    	jle    259e5 <gouraud_deob_draw_triangle+0x2f5>
   26185:	c1 e1 09             	shl    $0x9,%ecx
   26188:	41 89 f8             	mov    %edi,%r8d
   2618b:	89 d8                	mov    %ebx,%eax
   2618d:	44 8b 7c 24 0c       	mov    0xc(%rsp),%r15d
   26192:	89 cd                	mov    %ecx,%ebp
   26194:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
   26199:	8b 5c 24 20          	mov    0x20(%rsp),%ebx
   2619d:	44 01 f5             	add    %r14d,%ebp
   261a0:	44 89 e9             	mov    %r13d,%ecx
   261a3:	44 89 e2             	mov    %r12d,%edx
   261a6:	44 89 ee             	mov    %r13d,%esi
   261a9:	c1 f9 0e             	sar    $0xe,%ecx
   261ac:	c1 fa 0e             	sar    $0xe,%edx
   261af:	44 09 e6             	or     %r12d,%esi
   261b2:	78 3c                	js     261f0 <gouraud_deob_draw_triangle+0xb00>
   261b4:	81 fa 00 02 00 00    	cmp    $0x200,%edx
   261ba:	40 0f 9f c6          	setg   %sil
   261be:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
   261c4:	41 0f 9f c1          	setg   %r9b
   261c8:	44 08 ce             	or     %r9b,%sil
   261cb:	75 23                	jne    261f0 <gouraud_deob_draw_triangle+0xb00>
   261cd:	39 d1                	cmp    %edx,%ecx
   261cf:	7e 1f                	jle    261f0 <gouraud_deob_draw_triangle+0xb00>
   261d1:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
   261d6:	44 89 f6             	mov    %r14d,%esi
   261d9:	89 44 24 18          	mov    %eax,0x18(%rsp)
   261dd:	44 89 44 24 0c       	mov    %r8d,0xc(%rsp)
   261e2:	e8 49 f4 ff ff       	call   25630 <gouraud_deob_draw_scanline.part.0>
   261e7:	8b 44 24 18          	mov    0x18(%rsp),%eax
   261eb:	44 8b 44 24 0c       	mov    0xc(%rsp),%r8d
   261f0:	41 81 c6 00 02 00 00 	add    $0x200,%r14d
   261f7:	45 01 fc             	add    %r15d,%r12d
   261fa:	41 01 dd             	add    %ebx,%r13d
   261fd:	41 01 c0             	add    %eax,%r8d
   26200:	41 39 ee             	cmp    %ebp,%r14d
   26203:	75 9b                	jne    261a0 <gouraud_deob_draw_triangle+0xab0>
   26205:	e9 db f7 ff ff       	jmp    259e5 <gouraud_deob_draw_triangle+0x2f5>
   2620a:	66 0f 1f 44 00 00    	nopw   0x0(%rax,%rax,1)
   26210:	45 85 ed             	test   %r13d,%r13d
   26213:	0f 88 27 05 00 00    	js     26740 <gouraud_deob_draw_triangle+0x1050>
   26219:	45 89 ef             	mov    %r13d,%r15d
   2621c:	44 89 f5             	mov    %r14d,%ebp
   2621f:	41 c1 e7 09          	shl    $0x9,%r15d
   26223:	41 c1 e1 0e          	shl    $0xe,%r9d
   26227:	45 89 cc             	mov    %r9d,%r12d
   2622a:	45 85 d2             	test   %r10d,%r10d
   2622d:	0f 88 5d 04 00 00    	js     26690 <gouraud_deob_draw_triangle+0xfa0>
   26233:	29 f1                	sub    %esi,%ecx
   26235:	44 29 ee             	sub    %r13d,%esi
   26238:	8b 54 24 20          	mov    0x20(%rsp),%edx
   2623c:	44 8d 46 ff          	lea    -0x1(%rsi),%r8d
   26240:	39 54 24 0c          	cmp    %edx,0xc(%rsp)
   26244:	0f 8e 86 07 00 00    	jle    269d0 <gouraud_deob_draw_triangle+0x12e0>
   2624a:	45 85 c0             	test   %r8d,%r8d
   2624d:	0f 88 e5 00 00 00    	js     26338 <gouraud_deob_draw_triangle+0xc48>
   26253:	44 89 4c 24 28       	mov    %r9d,0x28(%rsp)
   26258:	41 89 ed             	mov    %ebp,%r13d
   2625b:	41 89 db             	mov    %ebx,%r11d
   2625e:	8b 5c 24 0c          	mov    0xc(%rsp),%ebx
   26262:	89 4c 24 2c          	mov    %ecx,0x2c(%rsp)
   26266:	44 89 fe             	mov    %r15d,%esi
   26269:	45 89 c4             	mov    %r8d,%r12d
   2626c:	89 7c 24 38          	mov    %edi,0x38(%rsp)
   26270:	44 89 44 24 3c       	mov    %r8d,0x3c(%rsp)
   26275:	89 6c 24 34          	mov    %ebp,0x34(%rsp)
   26279:	8b 6c 24 20          	mov    0x20(%rsp),%ebp
   2627d:	44 89 7c 24 30       	mov    %r15d,0x30(%rsp)
   26282:	41 89 ff             	mov    %edi,%r15d
   26285:	0f 1f 00             	nopl   (%rax)
   26288:	44 89 e9             	mov    %r13d,%ecx
   2628b:	44 89 f2             	mov    %r14d,%edx
   2628e:	44 89 e8             	mov    %r13d,%eax
   26291:	c1 f9 0e             	sar    $0xe,%ecx
   26294:	c1 fa 0e             	sar    $0xe,%edx
   26297:	44 09 f0             	or     %r14d,%eax
   2629a:	78 41                	js     262dd <gouraud_deob_draw_triangle+0xbed>
   2629c:	81 fa 00 02 00 00    	cmp    $0x200,%edx
   262a2:	40 0f 9f c7          	setg   %dil
   262a6:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
   262ac:	41 0f 9f c1          	setg   %r9b
   262b0:	44 08 cf             	or     %r9b,%dil
   262b3:	75 28                	jne    262dd <gouraud_deob_draw_triangle+0xbed>
   262b5:	39 d1                	cmp    %edx,%ecx
   262b7:	7e 24                	jle    262dd <gouraud_deob_draw_triangle+0xbed>
   262b9:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
   262be:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
   262c3:	45 89 f8             	mov    %r15d,%r8d
   262c6:	89 74 24 20          	mov    %esi,0x20(%rsp)
   262ca:	44 89 5c 24 24       	mov    %r11d,0x24(%rsp)
   262cf:	e8 5c f3 ff ff       	call   25630 <gouraud_deob_draw_scanline.part.0>
   262d4:	44 8b 5c 24 24       	mov    0x24(%rsp),%r11d
   262d9:	8b 74 24 20          	mov    0x20(%rsp),%esi
   262dd:	41 01 ee             	add    %ebp,%r14d
   262e0:	41 01 dd             	add    %ebx,%r13d
   262e3:	45 01 df             	add    %r11d,%r15d
   262e6:	81 c6 00 02 00 00    	add    $0x200,%esi
   262ec:	41 83 ec 01          	sub    $0x1,%r12d
   262f0:	73 96                	jae    26288 <gouraud_deob_draw_triangle+0xb98>
   262f2:	44 8b 44 24 3c       	mov    0x3c(%rsp),%r8d
   262f7:	8b 44 24 0c          	mov    0xc(%rsp),%eax
   262fb:	44 89 da             	mov    %r11d,%edx
   262fe:	44 89 db             	mov    %r11d,%ebx
   26301:	8b 6c 24 34          	mov    0x34(%rsp),%ebp
   26305:	8b 7c 24 38          	mov    0x38(%rsp),%edi
   26309:	41 0f af d0          	imul   %r8d,%edx
   2630d:	44 8b 7c 24 30       	mov    0x30(%rsp),%r15d
   26312:	44 8b 64 24 28       	mov    0x28(%rsp),%r12d
   26317:	01 c5                	add    %eax,%ebp
   26319:	41 0f af c0          	imul   %r8d,%eax
   2631d:	8b 4c 24 2c          	mov    0x2c(%rsp),%ecx
   26321:	01 c5                	add    %eax,%ebp
   26323:	42 8d 04 1f          	lea    (%rdi,%r11,1),%eax
   26327:	8d 3c 02             	lea    (%rdx,%rax,1),%edi
   2632a:	44 89 c0             	mov    %r8d,%eax
   2632d:	c1 e0 09             	shl    $0x9,%eax
   26330:	45 8d bc 07 00 02 00 	lea    0x200(%r15,%rax,1),%r15d
   26337:	00 
   26338:	85 c9                	test   %ecx,%ecx
   2633a:	0f 8e a5 f6 ff ff    	jle    259e5 <gouraud_deob_draw_triangle+0x2f5>
   26340:	c1 e1 09             	shl    $0x9,%ecx
   26343:	41 89 f8             	mov    %edi,%r8d
   26346:	89 d8                	mov    %ebx,%eax
   26348:	44 8b 74 24 0c       	mov    0xc(%rsp),%r14d
   2634d:	41 89 cd             	mov    %ecx,%r13d
   26350:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
   26355:	8b 5c 24 18          	mov    0x18(%rsp),%ebx
   26359:	45 01 fd             	add    %r15d,%r13d
   2635c:	0f 1f 40 00          	nopl   0x0(%rax)
   26360:	89 e9                	mov    %ebp,%ecx
   26362:	44 89 e2             	mov    %r12d,%edx
   26365:	89 ee                	mov    %ebp,%esi
   26367:	c1 f9 0e             	sar    $0xe,%ecx
   2636a:	c1 fa 0e             	sar    $0xe,%edx
   2636d:	44 09 e6             	or     %r12d,%esi
   26370:	78 3c                	js     263ae <gouraud_deob_draw_triangle+0xcbe>
   26372:	81 fa 00 02 00 00    	cmp    $0x200,%edx
   26378:	40 0f 9f c6          	setg   %sil
   2637c:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
   26382:	41 0f 9f c1          	setg   %r9b
   26386:	44 08 ce             	or     %r9b,%sil
   26389:	75 23                	jne    263ae <gouraud_deob_draw_triangle+0xcbe>
   2638b:	39 d1                	cmp    %edx,%ecx
   2638d:	7e 1f                	jle    263ae <gouraud_deob_draw_triangle+0xcbe>
   2638f:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
   26394:	44 89 fe             	mov    %r15d,%esi
   26397:	89 44 24 18          	mov    %eax,0x18(%rsp)
   2639b:	44 89 44 24 0c       	mov    %r8d,0xc(%rsp)
   263a0:	e8 8b f2 ff ff       	call   25630 <gouraud_deob_draw_scanline.part.0>
   263a5:	8b 44 24 18          	mov    0x18(%rsp),%eax
   263a9:	44 8b 44 24 0c       	mov    0xc(%rsp),%r8d
   263ae:	41 81 c7 00 02 00 00 	add    $0x200,%r15d
   263b5:	41 01 dc             	add    %ebx,%r12d
   263b8:	44 01 f5             	add    %r14d,%ebp
   263bb:	41 01 c0             	add    %eax,%r8d
   263be:	45 39 fd             	cmp    %r15d,%r13d
   263c1:	75 9d                	jne    26360 <gouraud_deob_draw_triangle+0xc70>
   263c3:	e9 1d f6 ff ff       	jmp    259e5 <gouraud_deob_draw_triangle+0x2f5>
   263c8:	0f 1f 84 00 00 00 00 	nopl   0x0(%rax,%rax,1)
   263cf:	00 
   263d0:	8b 44 24 0c          	mov    0xc(%rsp),%eax
   263d4:	45 89 c6             	mov    %r8d,%r14d
   263d7:	41 0f af c3          	imul   %r11d,%eax
   263db:	41 29 c6             	sub    %eax,%r14d
   263de:	8b 44 24 18          	mov    0x18(%rsp),%eax
   263e2:	41 0f af c3          	imul   %r11d,%eax
   263e6:	44 0f af db          	imul   %ebx,%r11d
   263ea:	41 29 c4             	sub    %eax,%r12d
   263ed:	31 c0                	xor    %eax,%eax
   263ef:	44 29 dd             	sub    %r11d,%ebp
   263f2:	45 31 db             	xor    %r11d,%r11d
   263f5:	e9 41 f4 ff ff       	jmp    2583b <gouraud_deob_draw_triangle+0x14b>
   263fa:	66 0f 1f 44 00 00    	nopw   0x0(%rax,%rax,1)
   26400:	8b 74 24 0c          	mov    0xc(%rsp),%esi
   26404:	44 8b 54 24 18       	mov    0x18(%rsp),%r10d
   26409:	44 89 c0             	mov    %r8d,%eax
   2640c:	45 89 c7             	mov    %r8d,%r15d
   2640f:	41 c1 e6 0e          	shl    $0xe,%r14d
   26413:	89 f2                	mov    %esi,%edx
   26415:	41 0f af d3          	imul   %r11d,%edx
   26419:	29 d0                	sub    %edx,%eax
   2641b:	44 89 d2             	mov    %r10d,%edx
   2641e:	41 0f af d3          	imul   %r11d,%edx
   26422:	44 0f af db          	imul   %ebx,%r11d
   26426:	41 29 d7             	sub    %edx,%r15d
   26429:	44 29 dd             	sub    %r11d,%ebp
   2642c:	45 85 ed             	test   %r13d,%r13d
   2642f:	78 17                	js     26448 <gouraud_deob_draw_triangle+0xd58>
   26431:	44 39 d6             	cmp    %r10d,%esi
   26434:	41 89 c4             	mov    %eax,%r12d
   26437:	0f 9c c2             	setl   %dl
   2643a:	31 c0                	xor    %eax,%eax
   2643c:	45 31 db             	xor    %r11d,%r11d
   2643f:	e9 66 fa ff ff       	jmp    25eaa <gouraud_deob_draw_triangle+0x7ba>
   26444:	0f 1f 40 00          	nopl   0x0(%rax)
   26448:	8b 54 24 20          	mov    0x20(%rsp),%edx
   2644c:	41 89 c4             	mov    %eax,%r12d
   2644f:	45 31 db             	xor    %r11d,%r11d
   26452:	31 c0                	xor    %eax,%eax
   26454:	0f af d7             	imul   %edi,%edx
   26457:	41 29 d6             	sub    %edx,%r14d
   2645a:	31 ff                	xor    %edi,%edi
   2645c:	44 39 df             	cmp    %r11d,%edi
   2645f:	75 0e                	jne    2646f <gouraud_deob_draw_triangle+0xd7f>
   26461:	8b 54 24 18          	mov    0x18(%rsp),%edx
   26465:	39 54 24 20          	cmp    %edx,0x20(%rsp)
   26469:	0f 8f 47 fb ff ff    	jg     25fb6 <gouraud_deob_draw_triangle+0x8c6>
   2646f:	44 29 df             	sub    %r11d,%edi
   26472:	89 fa                	mov    %edi,%edx
   26474:	83 ef 01             	sub    $0x1,%edi
   26477:	0f 88 e9 00 00 00    	js     26566 <gouraud_deob_draw_triangle+0xe76>
   2647d:	89 44 24 30          	mov    %eax,0x30(%rsp)
   26481:	41 89 e8             	mov    %ebp,%r8d
   26484:	c1 e2 09             	shl    $0x9,%edx
   26487:	41 89 db             	mov    %ebx,%r11d
   2648a:	89 7c 24 34          	mov    %edi,0x34(%rsp)
   2648e:	8b 5c 24 18          	mov    0x18(%rsp),%ebx
   26492:	45 89 fd             	mov    %r15d,%r13d
   26495:	44 8d 14 02          	lea    (%rdx,%rax,1),%r10d
   26499:	89 4c 24 38          	mov    %ecx,0x38(%rsp)
   2649d:	89 6c 24 2c          	mov    %ebp,0x2c(%rsp)
   264a1:	8b 6c 24 0c          	mov    0xc(%rsp),%ebp
   264a5:	44 89 74 24 28       	mov    %r14d,0x28(%rsp)
   264aa:	45 89 c6             	mov    %r8d,%r14d
   264ad:	44 89 7c 24 3c       	mov    %r15d,0x3c(%rsp)
   264b2:	41 89 c7             	mov    %eax,%r15d
   264b5:	0f 1f 00             	nopl   (%rax)
   264b8:	44 89 e1             	mov    %r12d,%ecx
   264bb:	44 89 ea             	mov    %r13d,%edx
   264be:	44 89 e0             	mov    %r12d,%eax
   264c1:	c1 f9 0e             	sar    $0xe,%ecx
   264c4:	c1 fa 0e             	sar    $0xe,%edx
   264c7:	44 09 e8             	or     %r13d,%eax
   264ca:	78 46                	js     26512 <gouraud_deob_draw_triangle+0xe22>
   264cc:	81 fa 00 02 00 00    	cmp    $0x200,%edx
   264d2:	40 0f 9f c7          	setg   %dil
   264d6:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
   264dc:	41 0f 9f c1          	setg   %r9b
   264e0:	44 08 cf             	or     %r9b,%dil
   264e3:	75 2d                	jne    26512 <gouraud_deob_draw_triangle+0xe22>
   264e5:	39 d1                	cmp    %edx,%ecx
   264e7:	7e 29                	jle    26512 <gouraud_deob_draw_triangle+0xe22>
   264e9:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
   264ee:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
   264f3:	45 89 f0             	mov    %r14d,%r8d
   264f6:	44 89 fe             	mov    %r15d,%esi
   264f9:	44 89 5c 24 24       	mov    %r11d,0x24(%rsp)
   264fe:	44 89 54 24 0c       	mov    %r10d,0xc(%rsp)
   26503:	e8 28 f1 ff ff       	call   25630 <gouraud_deob_draw_scanline.part.0>
   26508:	44 8b 5c 24 24       	mov    0x24(%rsp),%r11d
   2650d:	44 8b 54 24 0c       	mov    0xc(%rsp),%r10d
   26512:	41 81 c7 00 02 00 00 	add    $0x200,%r15d
   26519:	41 01 ec             	add    %ebp,%r12d
   2651c:	41 01 dd             	add    %ebx,%r13d
   2651f:	45 01 de             	add    %r11d,%r14d
   26522:	45 39 d7             	cmp    %r10d,%r15d
   26525:	75 91                	jne    264b8 <gouraud_deob_draw_triangle+0xdc8>
   26527:	8b 7c 24 34          	mov    0x34(%rsp),%edi
   2652b:	8b 74 24 18          	mov    0x18(%rsp),%esi
   2652f:	44 89 da             	mov    %r11d,%edx
   26532:	44 89 db             	mov    %r11d,%ebx
   26535:	44 8b 7c 24 3c       	mov    0x3c(%rsp),%r15d
   2653a:	8b 6c 24 2c          	mov    0x2c(%rsp),%ebp
   2653e:	0f af d7             	imul   %edi,%edx
   26541:	8b 44 24 30          	mov    0x30(%rsp),%eax
   26545:	44 8b 74 24 28       	mov    0x28(%rsp),%r14d
   2654a:	41 01 f7             	add    %esi,%r15d
   2654d:	0f af f7             	imul   %edi,%esi
   26550:	44 01 dd             	add    %r11d,%ebp
   26553:	c1 e7 09             	shl    $0x9,%edi
   26556:	8b 4c 24 38          	mov    0x38(%rsp),%ecx
   2655a:	8d 84 38 00 02 00 00 	lea    0x200(%rax,%rdi,1),%eax
   26561:	01 d5                	add    %edx,%ebp
   26563:	41 01 f7             	add    %esi,%r15d
   26566:	83 e9 01             	sub    $0x1,%ecx
   26569:	41 89 cc             	mov    %ecx,%r12d
   2656c:	0f 88 73 f4 ff ff    	js     259e5 <gouraud_deob_draw_triangle+0x2f5>
   26572:	41 89 e8             	mov    %ebp,%r8d
   26575:	44 8b 6c 24 20       	mov    0x20(%rsp),%r13d
   2657a:	89 dd                	mov    %ebx,%ebp
   2657c:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
   26581:	8b 5c 24 18          	mov    0x18(%rsp),%ebx
   26585:	0f 1f 00             	nopl   (%rax)
   26588:	44 89 f1             	mov    %r14d,%ecx
   2658b:	44 89 fa             	mov    %r15d,%edx
   2658e:	44 89 f6             	mov    %r14d,%esi
   26591:	c1 f9 0e             	sar    $0xe,%ecx
   26594:	c1 fa 0e             	sar    $0xe,%edx
   26597:	44 09 fe             	or     %r15d,%esi
   2659a:	78 3b                	js     265d7 <gouraud_deob_draw_triangle+0xee7>
   2659c:	81 fa 00 02 00 00    	cmp    $0x200,%edx
   265a2:	40 0f 9f c6          	setg   %sil
   265a6:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
   265ac:	41 0f 9f c1          	setg   %r9b
   265b0:	44 08 ce             	or     %r9b,%sil
   265b3:	75 22                	jne    265d7 <gouraud_deob_draw_triangle+0xee7>
   265b5:	39 d1                	cmp    %edx,%ecx
   265b7:	7e 1e                	jle    265d7 <gouraud_deob_draw_triangle+0xee7>
   265b9:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
   265be:	89 c6                	mov    %eax,%esi
   265c0:	44 89 44 24 18       	mov    %r8d,0x18(%rsp)
   265c5:	89 44 24 0c          	mov    %eax,0xc(%rsp)
   265c9:	e8 62 f0 ff ff       	call   25630 <gouraud_deob_draw_scanline.part.0>
   265ce:	44 8b 44 24 18       	mov    0x18(%rsp),%r8d
   265d3:	8b 44 24 0c          	mov    0xc(%rsp),%eax
   265d7:	45 01 ee             	add    %r13d,%r14d
   265da:	41 01 df             	add    %ebx,%r15d
   265dd:	41 01 e8             	add    %ebp,%r8d
   265e0:	05 00 02 00 00       	add    $0x200,%eax
   265e5:	41 83 ec 01          	sub    $0x1,%r12d
   265e9:	73 9d                	jae    26588 <gouraud_deob_draw_triangle+0xe98>
   265eb:	e9 f5 f3 ff ff       	jmp    259e5 <gouraud_deob_draw_triangle+0x2f5>
   265f0:	8b 74 24 20          	mov    0x20(%rsp),%esi
   265f4:	0f af ce             	imul   %esi,%ecx
   265f7:	41 29 cf             	sub    %ecx,%r15d
   265fa:	45 85 db             	test   %r11d,%r11d
   265fd:	74 04                	je     26603 <gouraud_deob_draw_triangle+0xf13>
   265ff:	84 d2                	test   %dl,%dl
   26601:	74 17                	je     2661a <gouraud_deob_draw_triangle+0xf2a>
   26603:	8b 54 24 20          	mov    0x20(%rsp),%edx
   26607:	39 54 24 0c          	cmp    %edx,0xc(%rsp)
   2660b:	0f 8e 4f 09 00 00    	jle    26f60 <gouraud_deob_draw_triangle+0x1870>
   26611:	45 85 db             	test   %r11d,%r11d
   26614:	0f 85 46 09 00 00    	jne    26f60 <gouraud_deob_draw_triangle+0x1870>
   2661a:	45 85 ed             	test   %r13d,%r13d
   2661d:	0f 8e c2 f3 ff ff    	jle    259e5 <gouraud_deob_draw_triangle+0x2f5>
   26623:	e9 37 f3 ff ff       	jmp    2595f <gouraud_deob_draw_triangle+0x26f>
   26628:	0f 1f 84 00 00 00 00 	nopl   0x0(%rax,%rax,1)
   2662f:	00 
   26630:	8b 74 24 20          	mov    0x20(%rsp),%esi
   26634:	0f af f7             	imul   %edi,%esi
   26637:	41 29 f6             	sub    %esi,%r14d
   2663a:	45 85 db             	test   %r11d,%r11d
   2663d:	0f 84 17 fe ff ff    	je     2645a <gouraud_deob_draw_triangle+0xd6a>
   26643:	84 d2                	test   %dl,%dl
   26645:	0f 85 6b f9 ff ff    	jne    25fb6 <gouraud_deob_draw_triangle+0x8c6>
   2664b:	31 ff                	xor    %edi,%edi
   2664d:	e9 0a fe ff ff       	jmp    2645c <gouraud_deob_draw_triangle+0xd6c>
   26652:	66 0f 1f 44 00 00    	nopw   0x0(%rax,%rax,1)
   26658:	8b 54 24 0c          	mov    0xc(%rsp),%edx
   2665c:	0f af d1             	imul   %ecx,%edx
   2665f:	41 29 d6             	sub    %edx,%r14d
   26662:	45 85 d2             	test   %r10d,%r10d
   26665:	74 04                	je     2666b <gouraud_deob_draw_triangle+0xf7b>
   26667:	84 c0                	test   %al,%al
   26669:	74 17                	je     26682 <gouraud_deob_draw_triangle+0xf92>
   2666b:	8b 54 24 18          	mov    0x18(%rsp),%edx
   2666f:	39 54 24 0c          	cmp    %edx,0xc(%rsp)
   26673:	0f 8d 1f 09 00 00    	jge    26f98 <gouraud_deob_draw_triangle+0x18a8>
   26679:	45 85 d2             	test   %r10d,%r10d
   2667c:	0f 85 16 09 00 00    	jne    26f98 <gouraud_deob_draw_triangle+0x18a8>
   26682:	45 85 db             	test   %r11d,%r11d
   26685:	0f 8e 5a f3 ff ff    	jle    259e5 <gouraud_deob_draw_triangle+0x2f5>
   2668b:	e9 3b f5 ff ff       	jmp    25bcb <gouraud_deob_draw_triangle+0x4db>
   26690:	8b 44 24 18          	mov    0x18(%rsp),%eax
   26694:	0f af c6             	imul   %esi,%eax
   26697:	8b 74 24 20          	mov    0x20(%rsp),%esi
   2669b:	41 29 c4             	sub    %eax,%r12d
   2669e:	39 74 24 0c          	cmp    %esi,0xc(%rsp)
   266a2:	0f 8f 90 fc ff ff    	jg     26338 <gouraud_deob_draw_triangle+0xc48>
   266a8:	85 c9                	test   %ecx,%ecx
   266aa:	0f 8e 35 f3 ff ff    	jle    259e5 <gouraud_deob_draw_triangle+0x2f5>
   266b0:	c1 e1 09             	shl    $0x9,%ecx
   266b3:	41 89 f8             	mov    %edi,%r8d
   266b6:	89 d8                	mov    %ebx,%eax
   266b8:	44 8b 74 24 0c       	mov    0xc(%rsp),%r14d
   266bd:	41 89 cd             	mov    %ecx,%r13d
   266c0:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
   266c5:	8b 5c 24 18          	mov    0x18(%rsp),%ebx
   266c9:	45 01 fd             	add    %r15d,%r13d
   266cc:	0f 1f 40 00          	nopl   0x0(%rax)
   266d0:	44 89 e1             	mov    %r12d,%ecx
   266d3:	89 ea                	mov    %ebp,%edx
   266d5:	44 89 e6             	mov    %r12d,%esi
   266d8:	c1 f9 0e             	sar    $0xe,%ecx
   266db:	c1 fa 0e             	sar    $0xe,%edx
   266de:	09 ee                	or     %ebp,%esi
   266e0:	78 3c                	js     2671e <gouraud_deob_draw_triangle+0x102e>
   266e2:	81 fa 00 02 00 00    	cmp    $0x200,%edx
   266e8:	40 0f 9f c6          	setg   %sil
   266ec:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
   266f2:	41 0f 9f c1          	setg   %r9b
   266f6:	44 08 ce             	or     %r9b,%sil
   266f9:	75 23                	jne    2671e <gouraud_deob_draw_triangle+0x102e>
   266fb:	39 d1                	cmp    %edx,%ecx
   266fd:	7e 1f                	jle    2671e <gouraud_deob_draw_triangle+0x102e>
   266ff:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
   26704:	44 89 fe             	mov    %r15d,%esi
   26707:	89 44 24 18          	mov    %eax,0x18(%rsp)
   2670b:	44 89 44 24 0c       	mov    %r8d,0xc(%rsp)
   26710:	e8 1b ef ff ff       	call   25630 <gouraud_deob_draw_scanline.part.0>
   26715:	8b 44 24 18          	mov    0x18(%rsp),%eax
   26719:	44 8b 44 24 0c       	mov    0xc(%rsp),%r8d
   2671e:	41 81 c7 00 02 00 00 	add    $0x200,%r15d
   26725:	41 01 dc             	add    %ebx,%r12d
   26728:	44 01 f5             	add    %r14d,%ebp
   2672b:	41 01 c0             	add    %eax,%r8d
   2672e:	45 39 fd             	cmp    %r15d,%r13d
   26731:	75 9d                	jne    266d0 <gouraud_deob_draw_triangle+0xfe0>
   26733:	e9 ad f2 ff ff       	jmp    259e5 <gouraud_deob_draw_triangle+0x2f5>
   26738:	0f 1f 84 00 00 00 00 	nopl   0x0(%rax,%rax,1)
   2673f:	00 
   26740:	8b 44 24 20          	mov    0x20(%rsp),%eax
   26744:	8b 54 24 0c          	mov    0xc(%rsp),%edx
   26748:	44 89 f5             	mov    %r14d,%ebp
   2674b:	45 31 ff             	xor    %r15d,%r15d
   2674e:	41 0f af c5          	imul   %r13d,%eax
   26752:	41 0f af d5          	imul   %r13d,%edx
   26756:	44 0f af eb          	imul   %ebx,%r13d
   2675a:	41 29 c6             	sub    %eax,%r14d
   2675d:	29 d5                	sub    %edx,%ebp
   2675f:	44 29 ef             	sub    %r13d,%edi
   26762:	45 31 ed             	xor    %r13d,%r13d
   26765:	e9 b9 fa ff ff       	jmp    26223 <gouraud_deob_draw_triangle+0xb33>
   2676a:	66 0f 1f 44 00 00    	nopw   0x0(%rax,%rax,1)
   26770:	8b 44 24 18          	mov    0x18(%rsp),%eax
   26774:	8b 54 24 20          	mov    0x20(%rsp),%edx
   26778:	0f af c1             	imul   %ecx,%eax
   2677b:	41 29 c4             	sub    %eax,%r12d
   2677e:	39 54 24 0c          	cmp    %edx,0xc(%rsp)
   26782:	0f 8e fc 07 00 00    	jle    26f84 <gouraud_deob_draw_triangle+0x1894>
   26788:	45 85 d2             	test   %r10d,%r10d
   2678b:	0f 8e 54 f2 ff ff    	jle    259e5 <gouraud_deob_draw_triangle+0x2f5>
   26791:	e9 3d f6 ff ff       	jmp    25dd3 <gouraud_deob_draw_triangle+0x6e3>
   26796:	66 2e 0f 1f 84 00 00 	cs nopw 0x0(%rax,%rax,1)
   2679d:	00 00 00 
   267a0:	8b 44 24 20          	mov    0x20(%rsp),%eax
   267a4:	44 89 f5             	mov    %r14d,%ebp
   267a7:	45 31 ff             	xor    %r15d,%r15d
   267aa:	41 0f af c5          	imul   %r13d,%eax
   267ae:	29 c5                	sub    %eax,%ebp
   267b0:	8b 44 24 0c          	mov    0xc(%rsp),%eax
   267b4:	41 0f af c5          	imul   %r13d,%eax
   267b8:	44 0f af eb          	imul   %ebx,%r13d
   267bc:	41 29 c6             	sub    %eax,%r14d
   267bf:	44 29 ef             	sub    %r13d,%edi
   267c2:	45 31 ed             	xor    %r13d,%r13d
   267c5:	e9 f6 f4 ff ff       	jmp    25cc0 <gouraud_deob_draw_triangle+0x5d0>
   267ca:	66 0f 1f 44 00 00    	nopw   0x0(%rax,%rax,1)
   267d0:	8b 44 24 18          	mov    0x18(%rsp),%eax
   267d4:	8b 54 24 20          	mov    0x20(%rsp),%edx
   267d8:	45 89 cd             	mov    %r9d,%r13d
   267db:	45 31 f6             	xor    %r14d,%r14d
   267de:	41 0f af c2          	imul   %r10d,%eax
   267e2:	41 0f af d2          	imul   %r10d,%edx
   267e6:	44 0f af d3          	imul   %ebx,%r10d
   267ea:	29 c5                	sub    %eax,%ebp
   267ec:	41 29 d5             	sub    %edx,%r13d
   267ef:	44 29 d7             	sub    %r10d,%edi
   267f2:	45 31 d2             	xor    %r10d,%r10d
   267f5:	e9 69 f8 ff ff       	jmp    26063 <gouraud_deob_draw_triangle+0x973>
   267fa:	66 0f 1f 44 00 00    	nopw   0x0(%rax,%rax,1)
   26800:	8b 44 24 18          	mov    0x18(%rsp),%eax
   26804:	45 89 cc             	mov    %r9d,%r12d
   26807:	45 31 ff             	xor    %r15d,%r15d
   2680a:	41 0f af c2          	imul   %r10d,%eax
   2680e:	41 29 c4             	sub    %eax,%r12d
   26811:	8b 44 24 20          	mov    0x20(%rsp),%eax
   26815:	41 0f af c2          	imul   %r10d,%eax
   26819:	44 0f af d3          	imul   %ebx,%r10d
   2681d:	29 c5                	sub    %eax,%ebp
   2681f:	44 29 d7             	sub    %r10d,%edi
   26822:	45 31 d2             	xor    %r10d,%r10d
   26825:	e9 7e f2 ff ff       	jmp    25aa8 <gouraud_deob_draw_triangle+0x3b8>
   2682a:	66 0f 1f 44 00 00    	nopw   0x0(%rax,%rax,1)
   26830:	8b 44 24 0c          	mov    0xc(%rsp),%eax
   26834:	0f af c6             	imul   %esi,%eax
   26837:	8b 74 24 18          	mov    0x18(%rsp),%esi
   2683b:	41 29 c4             	sub    %eax,%r12d
   2683e:	39 74 24 20          	cmp    %esi,0x20(%rsp)
   26842:	0f 8f 35 f9 ff ff    	jg     2617d <gouraud_deob_draw_triangle+0xa8d>
   26848:	85 c9                	test   %ecx,%ecx
   2684a:	0f 8e 95 f1 ff ff    	jle    259e5 <gouraud_deob_draw_triangle+0x2f5>
   26850:	c1 e1 09             	shl    $0x9,%ecx
   26853:	41 89 f8             	mov    %edi,%r8d
   26856:	89 d8                	mov    %ebx,%eax
   26858:	44 8b 7c 24 0c       	mov    0xc(%rsp),%r15d
   2685d:	89 cd                	mov    %ecx,%ebp
   2685f:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
   26864:	8b 5c 24 20          	mov    0x20(%rsp),%ebx
   26868:	44 01 f5             	add    %r14d,%ebp
   2686b:	0f 1f 44 00 00       	nopl   0x0(%rax,%rax,1)
   26870:	44 89 e1             	mov    %r12d,%ecx
   26873:	44 89 ea             	mov    %r13d,%edx
   26876:	44 89 e6             	mov    %r12d,%esi
   26879:	c1 f9 0e             	sar    $0xe,%ecx
   2687c:	c1 fa 0e             	sar    $0xe,%edx
   2687f:	44 09 ee             	or     %r13d,%esi
   26882:	78 3c                	js     268c0 <gouraud_deob_draw_triangle+0x11d0>
   26884:	81 fa 00 02 00 00    	cmp    $0x200,%edx
   2688a:	40 0f 9f c6          	setg   %sil
   2688e:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
   26894:	41 0f 9f c1          	setg   %r9b
   26898:	44 08 ce             	or     %r9b,%sil
   2689b:	75 23                	jne    268c0 <gouraud_deob_draw_triangle+0x11d0>
   2689d:	39 d1                	cmp    %edx,%ecx
   2689f:	7e 1f                	jle    268c0 <gouraud_deob_draw_triangle+0x11d0>
   268a1:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
   268a6:	44 89 f6             	mov    %r14d,%esi
   268a9:	89 44 24 18          	mov    %eax,0x18(%rsp)
   268ad:	44 89 44 24 0c       	mov    %r8d,0xc(%rsp)
   268b2:	e8 79 ed ff ff       	call   25630 <gouraud_deob_draw_scanline.part.0>
   268b7:	8b 44 24 18          	mov    0x18(%rsp),%eax
   268bb:	44 8b 44 24 0c       	mov    0xc(%rsp),%r8d
   268c0:	41 81 c6 00 02 00 00 	add    $0x200,%r14d
   268c7:	45 01 fc             	add    %r15d,%r12d
   268ca:	41 01 dd             	add    %ebx,%r13d
   268cd:	41 01 c0             	add    %eax,%r8d
   268d0:	44 39 f5             	cmp    %r14d,%ebp
   268d3:	75 9b                	jne    26870 <gouraud_deob_draw_triangle+0x1180>
   268d5:	e9 0b f1 ff ff       	jmp    259e5 <gouraud_deob_draw_triangle+0x2f5>
   268da:	85 c0                	test   %eax,%eax
   268dc:	0f 8e 66 ff ff ff    	jle    26848 <gouraud_deob_draw_triangle+0x1158>
   268e2:	44 89 44 24 28       	mov    %r8d,0x28(%rsp)
   268e7:	45 89 f2             	mov    %r14d,%r10d
   268ea:	45 89 ef             	mov    %r13d,%r15d
   268ed:	41 89 db             	mov    %ebx,%r11d
   268f0:	89 4c 24 2c          	mov    %ecx,0x2c(%rsp)
   268f4:	8b 5c 24 20          	mov    0x20(%rsp),%ebx
   268f8:	41 89 f4             	mov    %esi,%r12d
   268fb:	89 7c 24 38          	mov    %edi,0x38(%rsp)
   268ff:	44 89 6c 24 34       	mov    %r13d,0x34(%rsp)
   26904:	44 8b 6c 24 18       	mov    0x18(%rsp),%r13d
   26909:	44 89 74 24 30       	mov    %r14d,0x30(%rsp)
   2690e:	41 89 fe             	mov    %edi,%r14d
   26911:	89 74 24 3c          	mov    %esi,0x3c(%rsp)
   26915:	44 89 d6             	mov    %r10d,%esi
   26918:	0f 1f 84 00 00 00 00 	nopl   0x0(%rax,%rax,1)
   2691f:	00 
   26920:	89 e9                	mov    %ebp,%ecx
   26922:	44 89 fa             	mov    %r15d,%edx
   26925:	89 e8                	mov    %ebp,%eax
   26927:	c1 f9 0e             	sar    $0xe,%ecx
   2692a:	c1 fa 0e             	sar    $0xe,%edx
   2692d:	44 09 f8             	or     %r15d,%eax
   26930:	78 41                	js     26973 <gouraud_deob_draw_triangle+0x1283>
   26932:	81 fa 00 02 00 00    	cmp    $0x200,%edx
   26938:	40 0f 9f c7          	setg   %dil
   2693c:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
   26942:	41 0f 9f c1          	setg   %r9b
   26946:	44 08 cf             	or     %r9b,%dil
   26949:	75 28                	jne    26973 <gouraud_deob_draw_triangle+0x1283>
   2694b:	39 d1                	cmp    %edx,%ecx
   2694d:	7e 24                	jle    26973 <gouraud_deob_draw_triangle+0x1283>
   2694f:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
   26954:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
   26959:	45 89 f0             	mov    %r14d,%r8d
   2695c:	89 74 24 18          	mov    %esi,0x18(%rsp)
   26960:	44 89 5c 24 24       	mov    %r11d,0x24(%rsp)
   26965:	e8 c6 ec ff ff       	call   25630 <gouraud_deob_draw_scanline.part.0>
   2696a:	44 8b 5c 24 24       	mov    0x24(%rsp),%r11d
   2696f:	8b 74 24 18          	mov    0x18(%rsp),%esi
   26973:	44 01 ed             	add    %r13d,%ebp
   26976:	41 01 df             	add    %ebx,%r15d
   26979:	45 01 de             	add    %r11d,%r14d
   2697c:	81 c6 00 02 00 00    	add    $0x200,%esi
   26982:	41 83 ec 01          	sub    $0x1,%r12d
   26986:	73 98                	jae    26920 <gouraud_deob_draw_triangle+0x1230>
   26988:	8b 74 24 3c          	mov    0x3c(%rsp),%esi
   2698c:	8b 44 24 20          	mov    0x20(%rsp),%eax
   26990:	44 89 da             	mov    %r11d,%edx
   26993:	44 89 db             	mov    %r11d,%ebx
   26996:	44 8b 6c 24 34       	mov    0x34(%rsp),%r13d
   2699b:	8b 7c 24 38          	mov    0x38(%rsp),%edi
   2699f:	0f af d6             	imul   %esi,%edx
   269a2:	44 8b 74 24 30       	mov    0x30(%rsp),%r14d
   269a7:	44 8b 64 24 28       	mov    0x28(%rsp),%r12d
   269ac:	41 01 c5             	add    %eax,%r13d
   269af:	0f af c6             	imul   %esi,%eax
   269b2:	c1 e6 09             	shl    $0x9,%esi
   269b5:	8b 4c 24 2c          	mov    0x2c(%rsp),%ecx
   269b9:	45 8d b4 36 00 02 00 	lea    0x200(%r14,%rsi,1),%r14d
   269c0:	00 
   269c1:	41 01 c5             	add    %eax,%r13d
   269c4:	42 8d 04 1f          	lea    (%rdi,%r11,1),%eax
   269c8:	8d 3c 02             	lea    (%rdx,%rax,1),%edi
   269cb:	e9 78 fe ff ff       	jmp    26848 <gouraud_deob_draw_triangle+0x1158>
   269d0:	85 f6                	test   %esi,%esi
   269d2:	0f 8e d0 fc ff ff    	jle    266a8 <gouraud_deob_draw_triangle+0xfb8>
   269d8:	44 89 4c 24 28       	mov    %r9d,0x28(%rsp)
   269dd:	41 89 ed             	mov    %ebp,%r13d
   269e0:	41 89 db             	mov    %ebx,%r11d
   269e3:	8b 5c 24 0c          	mov    0xc(%rsp),%ebx
   269e7:	89 4c 24 2c          	mov    %ecx,0x2c(%rsp)
   269eb:	44 89 fe             	mov    %r15d,%esi
   269ee:	45 89 c4             	mov    %r8d,%r12d
   269f1:	89 7c 24 38          	mov    %edi,0x38(%rsp)
   269f5:	44 89 44 24 3c       	mov    %r8d,0x3c(%rsp)
   269fa:	89 6c 24 34          	mov    %ebp,0x34(%rsp)
   269fe:	8b 6c 24 20          	mov    0x20(%rsp),%ebp
   26a02:	44 89 7c 24 30       	mov    %r15d,0x30(%rsp)
   26a07:	41 89 ff             	mov    %edi,%r15d
   26a0a:	66 0f 1f 44 00 00    	nopw   0x0(%rax,%rax,1)
   26a10:	44 89 f1             	mov    %r14d,%ecx
   26a13:	44 89 ea             	mov    %r13d,%edx
   26a16:	44 89 f0             	mov    %r14d,%eax
   26a19:	c1 f9 0e             	sar    $0xe,%ecx
   26a1c:	c1 fa 0e             	sar    $0xe,%edx
   26a1f:	44 09 e8             	or     %r13d,%eax
   26a22:	78 41                	js     26a65 <gouraud_deob_draw_triangle+0x1375>
   26a24:	81 fa 00 02 00 00    	cmp    $0x200,%edx
   26a2a:	40 0f 9f c7          	setg   %dil
   26a2e:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
   26a34:	41 0f 9f c1          	setg   %r9b
   26a38:	44 08 cf             	or     %r9b,%dil
   26a3b:	75 28                	jne    26a65 <gouraud_deob_draw_triangle+0x1375>
   26a3d:	39 d1                	cmp    %edx,%ecx
   26a3f:	7e 24                	jle    26a65 <gouraud_deob_draw_triangle+0x1375>
   26a41:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
   26a46:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
   26a4b:	45 89 f8             	mov    %r15d,%r8d
   26a4e:	89 74 24 20          	mov    %esi,0x20(%rsp)
   26a52:	44 89 5c 24 24       	mov    %r11d,0x24(%rsp)
   26a57:	e8 d4 eb ff ff       	call   25630 <gouraud_deob_draw_scanline.part.0>
   26a5c:	44 8b 5c 24 24       	mov    0x24(%rsp),%r11d
   26a61:	8b 74 24 20          	mov    0x20(%rsp),%esi
   26a65:	41 01 ee             	add    %ebp,%r14d
   26a68:	41 01 dd             	add    %ebx,%r13d
   26a6b:	45 01 df             	add    %r11d,%r15d
   26a6e:	81 c6 00 02 00 00    	add    $0x200,%esi
   26a74:	41 83 ec 01          	sub    $0x1,%r12d
   26a78:	73 96                	jae    26a10 <gouraud_deob_draw_triangle+0x1320>
   26a7a:	44 8b 44 24 3c       	mov    0x3c(%rsp),%r8d
   26a7f:	8b 44 24 0c          	mov    0xc(%rsp),%eax
   26a83:	44 89 da             	mov    %r11d,%edx
   26a86:	44 89 db             	mov    %r11d,%ebx
   26a89:	8b 6c 24 34          	mov    0x34(%rsp),%ebp
   26a8d:	8b 7c 24 38          	mov    0x38(%rsp),%edi
   26a91:	41 0f af d0          	imul   %r8d,%edx
   26a95:	44 8b 7c 24 30       	mov    0x30(%rsp),%r15d
   26a9a:	44 8b 64 24 28       	mov    0x28(%rsp),%r12d
   26a9f:	01 c5                	add    %eax,%ebp
   26aa1:	41 0f af c0          	imul   %r8d,%eax
   26aa5:	41 c1 e0 09          	shl    $0x9,%r8d
   26aa9:	8b 4c 24 2c          	mov    0x2c(%rsp),%ecx
   26aad:	47 8d bc 07 00 02 00 	lea    0x200(%r15,%r8,1),%r15d
   26ab4:	00 
   26ab5:	01 c5                	add    %eax,%ebp
   26ab7:	42 8d 04 1f          	lea    (%rdi,%r11,1),%eax
   26abb:	8d 3c 02             	lea    (%rdx,%rax,1),%edi
   26abe:	e9 e5 fb ff ff       	jmp    266a8 <gouraud_deob_draw_triangle+0xfb8>
   26ac3:	85 c0                	test   %eax,%eax
   26ac5:	0f 8e de 00 00 00    	jle    26ba9 <gouraud_deob_draw_triangle+0x14b9>
   26acb:	89 7c 24 34          	mov    %edi,0x34(%rsp)
   26acf:	45 89 fa             	mov    %r15d,%r10d
   26ad2:	41 89 db             	mov    %ebx,%r11d
   26ad5:	8b 5c 24 0c          	mov    0xc(%rsp),%ebx
   26ad9:	89 4c 24 3c          	mov    %ecx,0x3c(%rsp)
   26add:	41 89 ed             	mov    %ebp,%r13d
   26ae0:	44 89 64 24 2c       	mov    %r12d,0x2c(%rsp)
   26ae5:	44 8b 64 24 20       	mov    0x20(%rsp),%r12d
   26aea:	89 6c 24 28          	mov    %ebp,0x28(%rsp)
   26aee:	89 cd                	mov    %ecx,%ebp
   26af0:	44 89 7c 24 30       	mov    %r15d,0x30(%rsp)
   26af5:	41 89 ff             	mov    %edi,%r15d
   26af8:	89 74 24 38          	mov    %esi,0x38(%rsp)
   26afc:	44 89 d6             	mov    %r10d,%esi
   26aff:	90                   	nop
   26b00:	44 89 e9             	mov    %r13d,%ecx
   26b03:	44 89 f2             	mov    %r14d,%edx
   26b06:	44 89 e8             	mov    %r13d,%eax
   26b09:	c1 f9 0e             	sar    $0xe,%ecx
   26b0c:	c1 fa 0e             	sar    $0xe,%edx
   26b0f:	44 09 f0             	or     %r14d,%eax
   26b12:	78 41                	js     26b55 <gouraud_deob_draw_triangle+0x1465>
   26b14:	81 fa 00 02 00 00    	cmp    $0x200,%edx
   26b1a:	40 0f 9f c7          	setg   %dil
   26b1e:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
   26b24:	41 0f 9f c1          	setg   %r9b
   26b28:	44 08 cf             	or     %r9b,%dil
   26b2b:	75 28                	jne    26b55 <gouraud_deob_draw_triangle+0x1465>
   26b2d:	39 d1                	cmp    %edx,%ecx
   26b2f:	7e 24                	jle    26b55 <gouraud_deob_draw_triangle+0x1465>
   26b31:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
   26b36:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
   26b3b:	45 89 f8             	mov    %r15d,%r8d
   26b3e:	89 74 24 0c          	mov    %esi,0xc(%rsp)
   26b42:	44 89 5c 24 24       	mov    %r11d,0x24(%rsp)
   26b47:	e8 e4 ea ff ff       	call   25630 <gouraud_deob_draw_scanline.part.0>
   26b4c:	44 8b 5c 24 24       	mov    0x24(%rsp),%r11d
   26b51:	8b 74 24 0c          	mov    0xc(%rsp),%esi
   26b55:	45 01 e5             	add    %r12d,%r13d
   26b58:	41 01 de             	add    %ebx,%r14d
   26b5b:	45 01 df             	add    %r11d,%r15d
   26b5e:	81 c6 00 02 00 00    	add    $0x200,%esi
   26b64:	83 ed 01             	sub    $0x1,%ebp
   26b67:	73 97                	jae    26b00 <gouraud_deob_draw_triangle+0x1410>
   26b69:	8b 4c 24 3c          	mov    0x3c(%rsp),%ecx
   26b6d:	8b 44 24 20          	mov    0x20(%rsp),%eax
   26b71:	44 89 da             	mov    %r11d,%edx
   26b74:	44 89 db             	mov    %r11d,%ebx
   26b77:	8b 6c 24 28          	mov    0x28(%rsp),%ebp
   26b7b:	8b 7c 24 34          	mov    0x34(%rsp),%edi
   26b7f:	0f af d1             	imul   %ecx,%edx
   26b82:	44 8b 7c 24 30       	mov    0x30(%rsp),%r15d
   26b87:	44 8b 64 24 2c       	mov    0x2c(%rsp),%r12d
   26b8c:	01 c5                	add    %eax,%ebp
   26b8e:	0f af c1             	imul   %ecx,%eax
   26b91:	8b 74 24 38          	mov    0x38(%rsp),%esi
   26b95:	c1 e1 09             	shl    $0x9,%ecx
   26b98:	45 8d bc 0f 00 02 00 	lea    0x200(%r15,%rcx,1),%r15d
   26b9f:	00 
   26ba0:	01 c5                	add    %eax,%ebp
   26ba2:	42 8d 04 1f          	lea    (%rdi,%r11,1),%eax
   26ba6:	8d 3c 02             	lea    (%rdx,%rax,1),%edi
   26ba9:	c1 e6 09             	shl    $0x9,%esi
   26bac:	41 89 f8             	mov    %edi,%r8d
   26baf:	89 d8                	mov    %ebx,%eax
   26bb1:	44 8b 74 24 20       	mov    0x20(%rsp),%r14d
   26bb6:	41 89 f5             	mov    %esi,%r13d
   26bb9:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
   26bbe:	8b 5c 24 18          	mov    0x18(%rsp),%ebx
   26bc2:	45 01 fd             	add    %r15d,%r13d
   26bc5:	0f 1f 00             	nopl   (%rax)
   26bc8:	89 e9                	mov    %ebp,%ecx
   26bca:	44 89 e2             	mov    %r12d,%edx
   26bcd:	89 ee                	mov    %ebp,%esi
   26bcf:	c1 f9 0e             	sar    $0xe,%ecx
   26bd2:	c1 fa 0e             	sar    $0xe,%edx
   26bd5:	44 09 e6             	or     %r12d,%esi
   26bd8:	78 3c                	js     26c16 <gouraud_deob_draw_triangle+0x1526>
   26bda:	81 fa 00 02 00 00    	cmp    $0x200,%edx
   26be0:	40 0f 9f c6          	setg   %sil
   26be4:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
   26bea:	41 0f 9f c1          	setg   %r9b
   26bee:	44 08 ce             	or     %r9b,%sil
   26bf1:	75 23                	jne    26c16 <gouraud_deob_draw_triangle+0x1526>
   26bf3:	39 d1                	cmp    %edx,%ecx
   26bf5:	7e 1f                	jle    26c16 <gouraud_deob_draw_triangle+0x1526>
   26bf7:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
   26bfc:	44 89 fe             	mov    %r15d,%esi
   26bff:	89 44 24 18          	mov    %eax,0x18(%rsp)
   26c03:	44 89 44 24 0c       	mov    %r8d,0xc(%rsp)
   26c08:	e8 23 ea ff ff       	call   25630 <gouraud_deob_draw_scanline.part.0>
   26c0d:	8b 44 24 18          	mov    0x18(%rsp),%eax
   26c11:	44 8b 44 24 0c       	mov    0xc(%rsp),%r8d
   26c16:	41 81 c7 00 02 00 00 	add    $0x200,%r15d
   26c1d:	44 01 f5             	add    %r14d,%ebp
   26c20:	41 01 dc             	add    %ebx,%r12d
   26c23:	41 01 c0             	add    %eax,%r8d
   26c26:	45 39 fd             	cmp    %r15d,%r13d
   26c29:	75 9d                	jne    26bc8 <gouraud_deob_draw_triangle+0x14d8>
   26c2b:	e9 b5 ed ff ff       	jmp    259e5 <gouraud_deob_draw_triangle+0x2f5>
   26c30:	41 39 cb             	cmp    %ecx,%r11d
   26c33:	0f 84 77 01 00 00    	je     26db0 <gouraud_deob_draw_triangle+0x16c0>
   26c39:	89 ca                	mov    %ecx,%edx
   26c3b:	44 29 da             	sub    %r11d,%edx
   26c3e:	89 d1                	mov    %edx,%ecx
   26c40:	83 e9 01             	sub    $0x1,%ecx
   26c43:	0f 88 7b 03 00 00    	js     26fc4 <gouraud_deob_draw_triangle+0x18d4>
   26c49:	c1 e2 09             	shl    $0x9,%edx
   26c4c:	89 7c 24 28          	mov    %edi,0x28(%rsp)
   26c50:	41 89 e8             	mov    %ebp,%r8d
   26c53:	45 89 f5             	mov    %r14d,%r13d
   26c56:	89 44 24 34          	mov    %eax,0x34(%rsp)
   26c5a:	44 8d 14 02          	lea    (%rdx,%rax,1),%r10d
   26c5e:	41 89 db             	mov    %ebx,%r11d
   26c61:	89 c6                	mov    %eax,%esi
   26c63:	89 4c 24 3c          	mov    %ecx,0x3c(%rsp)
   26c67:	44 89 d3             	mov    %r10d,%ebx
   26c6a:	89 6c 24 2c          	mov    %ebp,0x2c(%rsp)
   26c6e:	8b 6c 24 0c          	mov    0xc(%rsp),%ebp
   26c72:	44 89 74 24 38       	mov    %r14d,0x38(%rsp)
   26c77:	44 8b 74 24 18       	mov    0x18(%rsp),%r14d
   26c7c:	44 89 7c 24 30       	mov    %r15d,0x30(%rsp)
   26c81:	45 89 c7             	mov    %r8d,%r15d
   26c84:	0f 1f 40 00          	nopl   0x0(%rax)
   26c88:	44 89 e9             	mov    %r13d,%ecx
   26c8b:	44 89 e2             	mov    %r12d,%edx
   26c8e:	44 89 e8             	mov    %r13d,%eax
   26c91:	c1 f9 0e             	sar    $0xe,%ecx
   26c94:	c1 fa 0e             	sar    $0xe,%edx
   26c97:	44 09 e0             	or     %r12d,%eax
   26c9a:	78 41                	js     26cdd <gouraud_deob_draw_triangle+0x15ed>
   26c9c:	81 fa 00 02 00 00    	cmp    $0x200,%edx
   26ca2:	40 0f 9f c7          	setg   %dil
   26ca6:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
   26cac:	41 0f 9f c1          	setg   %r9b
   26cb0:	44 08 cf             	or     %r9b,%dil
   26cb3:	75 28                	jne    26cdd <gouraud_deob_draw_triangle+0x15ed>
   26cb5:	39 d1                	cmp    %edx,%ecx
   26cb7:	7e 24                	jle    26cdd <gouraud_deob_draw_triangle+0x15ed>
   26cb9:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
   26cbe:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
   26cc3:	45 89 f8             	mov    %r15d,%r8d
   26cc6:	89 74 24 18          	mov    %esi,0x18(%rsp)
   26cca:	44 89 5c 24 24       	mov    %r11d,0x24(%rsp)
   26ccf:	e8 5c e9 ff ff       	call   25630 <gouraud_deob_draw_scanline.part.0>
   26cd4:	44 8b 5c 24 24       	mov    0x24(%rsp),%r11d
   26cd9:	8b 74 24 18          	mov    0x18(%rsp),%esi
   26cdd:	81 c6 00 02 00 00    	add    $0x200,%esi
   26ce3:	41 01 ed             	add    %ebp,%r13d
   26ce6:	45 01 f4             	add    %r14d,%r12d
   26ce9:	45 01 df             	add    %r11d,%r15d
   26cec:	39 f3                	cmp    %esi,%ebx
   26cee:	75 98                	jne    26c88 <gouraud_deob_draw_triangle+0x1598>
   26cf0:	44 8b 6c 24 0c       	mov    0xc(%rsp),%r13d
   26cf5:	8b 4c 24 3c          	mov    0x3c(%rsp),%ecx
   26cf9:	44 8b 74 24 38       	mov    0x38(%rsp),%r14d
   26cfe:	8b 6c 24 2c          	mov    0x2c(%rsp),%ebp
   26d02:	44 89 ea             	mov    %r13d,%edx
   26d05:	8b 7c 24 28          	mov    0x28(%rsp),%edi
   26d09:	8b 44 24 34          	mov    0x34(%rsp),%eax
   26d0d:	0f af d1             	imul   %ecx,%edx
   26d10:	45 01 ee             	add    %r13d,%r14d
   26d13:	44 01 dd             	add    %r11d,%ebp
   26d16:	44 8b 7c 24 30       	mov    0x30(%rsp),%r15d
   26d1b:	44 8d 67 ff          	lea    -0x1(%rdi),%r12d
   26d1f:	8b 5c 24 20          	mov    0x20(%rsp),%ebx
   26d23:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
   26d28:	41 01 d6             	add    %edx,%r14d
   26d2b:	44 89 da             	mov    %r11d,%edx
   26d2e:	0f af d1             	imul   %ecx,%edx
   26d31:	c1 e1 09             	shl    $0x9,%ecx
   26d34:	8d 84 08 00 02 00 00 	lea    0x200(%rax,%rcx,1),%eax
   26d3b:	01 d5                	add    %edx,%ebp
   26d3d:	41 89 e8             	mov    %ebp,%r8d
   26d40:	44 89 dd             	mov    %r11d,%ebp
   26d43:	0f 1f 44 00 00       	nopl   0x0(%rax,%rax,1)
   26d48:	44 89 f1             	mov    %r14d,%ecx
   26d4b:	44 89 fa             	mov    %r15d,%edx
   26d4e:	44 89 f6             	mov    %r14d,%esi
   26d51:	c1 f9 0e             	sar    $0xe,%ecx
   26d54:	c1 fa 0e             	sar    $0xe,%edx
   26d57:	44 09 fe             	or     %r15d,%esi
   26d5a:	78 3b                	js     26d97 <gouraud_deob_draw_triangle+0x16a7>
   26d5c:	81 fa 00 02 00 00    	cmp    $0x200,%edx
   26d62:	40 0f 9f c6          	setg   %sil
   26d66:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
   26d6c:	41 0f 9f c1          	setg   %r9b
   26d70:	44 08 ce             	or     %r9b,%sil
   26d73:	75 22                	jne    26d97 <gouraud_deob_draw_triangle+0x16a7>
   26d75:	39 d1                	cmp    %edx,%ecx
   26d77:	7e 1e                	jle    26d97 <gouraud_deob_draw_triangle+0x16a7>
   26d79:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
   26d7e:	89 c6                	mov    %eax,%esi
   26d80:	44 89 44 24 18       	mov    %r8d,0x18(%rsp)
   26d85:	89 44 24 0c          	mov    %eax,0xc(%rsp)
   26d89:	e8 a2 e8 ff ff       	call   25630 <gouraud_deob_draw_scanline.part.0>
   26d8e:	44 8b 44 24 18       	mov    0x18(%rsp),%r8d
   26d93:	8b 44 24 0c          	mov    0xc(%rsp),%eax
   26d97:	45 01 ee             	add    %r13d,%r14d
   26d9a:	41 01 df             	add    %ebx,%r15d
   26d9d:	41 01 e8             	add    %ebp,%r8d
   26da0:	05 00 02 00 00       	add    $0x200,%eax
   26da5:	41 83 ec 01          	sub    $0x1,%r12d
   26da9:	73 9d                	jae    26d48 <gouraud_deob_draw_triangle+0x1658>
   26dab:	e9 35 ec ff ff       	jmp    259e5 <gouraud_deob_draw_triangle+0x2f5>
   26db0:	8b 54 24 20          	mov    0x20(%rsp),%edx
   26db4:	39 54 24 0c          	cmp    %edx,0xc(%rsp)
   26db8:	0f 8e 7b fe ff ff    	jle    26c39 <gouraud_deob_draw_triangle+0x1549>
   26dbe:	e9 9c eb ff ff       	jmp    2595f <gouraud_deob_draw_triangle+0x26f>
   26dc3:	0f 1f 44 00 00       	nopl   0x0(%rax,%rax,1)
   26dc8:	41 39 ca             	cmp    %ecx,%r10d
   26dcb:	0f 84 76 01 00 00    	je     26f47 <gouraud_deob_draw_triangle+0x1857>
   26dd1:	89 c8                	mov    %ecx,%eax
   26dd3:	44 29 d0             	sub    %r10d,%eax
   26dd6:	89 c1                	mov    %eax,%ecx
   26dd8:	83 e9 01             	sub    $0x1,%ecx
   26ddb:	0f 88 de 01 00 00    	js     26fbf <gouraud_deob_draw_triangle+0x18cf>
   26de1:	44 89 7c 24 30       	mov    %r15d,0x30(%rsp)
   26de6:	c1 e0 09             	shl    $0x9,%eax
   26de9:	45 89 e5             	mov    %r12d,%r13d
   26dec:	41 89 da             	mov    %ebx,%r10d
   26def:	89 4c 24 34          	mov    %ecx,0x34(%rsp)
   26df3:	8b 5c 24 18          	mov    0x18(%rsp),%ebx
   26df7:	46 8d 1c 38          	lea    (%rax,%r15,1),%r11d
   26dfb:	89 7c 24 38          	mov    %edi,0x38(%rsp)
   26dff:	89 74 24 3c          	mov    %esi,0x3c(%rsp)
   26e03:	44 89 64 24 2c       	mov    %r12d,0x2c(%rsp)
   26e08:	44 8b 64 24 20       	mov    0x20(%rsp),%r12d
   26e0d:	44 89 74 24 28       	mov    %r14d,0x28(%rsp)
   26e12:	41 89 fe             	mov    %edi,%r14d
   26e15:	0f 1f 00             	nopl   (%rax)
   26e18:	44 89 e9             	mov    %r13d,%ecx
   26e1b:	89 ea                	mov    %ebp,%edx
   26e1d:	44 89 e8             	mov    %r13d,%eax
   26e20:	c1 f9 0e             	sar    $0xe,%ecx
   26e23:	c1 fa 0e             	sar    $0xe,%edx
   26e26:	09 e8                	or     %ebp,%eax
   26e28:	78 46                	js     26e70 <gouraud_deob_draw_triangle+0x1780>
   26e2a:	81 fa 00 02 00 00    	cmp    $0x200,%edx
   26e30:	40 0f 9f c7          	setg   %dil
   26e34:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
   26e3a:	41 0f 9f c1          	setg   %r9b
   26e3e:	44 08 cf             	or     %r9b,%dil
   26e41:	75 2d                	jne    26e70 <gouraud_deob_draw_triangle+0x1780>
   26e43:	39 d1                	cmp    %edx,%ecx
   26e45:	7e 29                	jle    26e70 <gouraud_deob_draw_triangle+0x1780>
   26e47:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
   26e4c:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
   26e51:	45 89 f0             	mov    %r14d,%r8d
   26e54:	44 89 fe             	mov    %r15d,%esi
   26e57:	44 89 54 24 24       	mov    %r10d,0x24(%rsp)
   26e5c:	44 89 5c 24 20       	mov    %r11d,0x20(%rsp)
   26e61:	e8 ca e7 ff ff       	call   25630 <gouraud_deob_draw_scanline.part.0>
   26e66:	44 8b 54 24 24       	mov    0x24(%rsp),%r10d
   26e6b:	44 8b 5c 24 20       	mov    0x20(%rsp),%r11d
   26e70:	41 81 c7 00 02 00 00 	add    $0x200,%r15d
   26e77:	41 01 dd             	add    %ebx,%r13d
   26e7a:	44 01 e5             	add    %r12d,%ebp
   26e7d:	45 01 d6             	add    %r10d,%r14d
   26e80:	45 39 df             	cmp    %r11d,%r15d
   26e83:	75 93                	jne    26e18 <gouraud_deob_draw_triangle+0x1728>
   26e85:	8b 4c 24 34          	mov    0x34(%rsp),%ecx
   26e89:	44 89 d2             	mov    %r10d,%edx
   26e8c:	8b 7c 24 38          	mov    0x38(%rsp),%edi
   26e90:	44 8b 64 24 2c       	mov    0x2c(%rsp),%r12d
   26e95:	44 8b 7c 24 30       	mov    0x30(%rsp),%r15d
   26e9a:	0f af d1             	imul   %ecx,%edx
   26e9d:	42 8d 04 17          	lea    (%rdi,%r10,1),%eax
   26ea1:	8b 74 24 3c          	mov    0x3c(%rsp),%esi
   26ea5:	44 8b 74 24 28       	mov    0x28(%rsp),%r14d
   26eaa:	44 8b 6c 24 0c       	mov    0xc(%rsp),%r13d
   26eaf:	8d 6e ff             	lea    -0x1(%rsi),%ebp
   26eb2:	8d 3c 02             	lea    (%rdx,%rax,1),%edi
   26eb5:	8b 54 24 18          	mov    0x18(%rsp),%edx
   26eb9:	41 89 f8             	mov    %edi,%r8d
   26ebc:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
   26ec1:	89 d0                	mov    %edx,%eax
   26ec3:	41 01 d4             	add    %edx,%r12d
   26ec6:	89 d3                	mov    %edx,%ebx
   26ec8:	0f af c1             	imul   %ecx,%eax
   26ecb:	c1 e1 09             	shl    $0x9,%ecx
   26ece:	45 8d bc 0f 00 02 00 	lea    0x200(%r15,%rcx,1),%r15d
   26ed5:	00 
   26ed6:	41 01 c4             	add    %eax,%r12d
   26ed9:	44 89 f8             	mov    %r15d,%eax
   26edc:	45 89 d7             	mov    %r10d,%r15d
   26edf:	90                   	nop
   26ee0:	44 89 e1             	mov    %r12d,%ecx
   26ee3:	44 89 f2             	mov    %r14d,%edx
   26ee6:	44 89 e6             	mov    %r12d,%esi
   26ee9:	c1 f9 0e             	sar    $0xe,%ecx
   26eec:	c1 fa 0e             	sar    $0xe,%edx
   26eef:	44 09 f6             	or     %r14d,%esi
   26ef2:	78 3b                	js     26f2f <gouraud_deob_draw_triangle+0x183f>
   26ef4:	81 fa 00 02 00 00    	cmp    $0x200,%edx
   26efa:	40 0f 9f c6          	setg   %sil
   26efe:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
   26f04:	41 0f 9f c1          	setg   %r9b
   26f08:	44 08 ce             	or     %r9b,%sil
   26f0b:	75 22                	jne    26f2f <gouraud_deob_draw_triangle+0x183f>
   26f0d:	39 d1                	cmp    %edx,%ecx
   26f0f:	7e 1e                	jle    26f2f <gouraud_deob_draw_triangle+0x183f>
   26f11:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
   26f16:	89 c6                	mov    %eax,%esi
   26f18:	44 89 44 24 18       	mov    %r8d,0x18(%rsp)
   26f1d:	89 44 24 0c          	mov    %eax,0xc(%rsp)
   26f21:	e8 0a e7 ff ff       	call   25630 <gouraud_deob_draw_scanline.part.0>
   26f26:	44 8b 44 24 18       	mov    0x18(%rsp),%r8d
   26f2b:	8b 44 24 0c          	mov    0xc(%rsp),%eax
   26f2f:	41 01 dc             	add    %ebx,%r12d
   26f32:	45 01 ee             	add    %r13d,%r14d
   26f35:	45 01 f8             	add    %r15d,%r8d
   26f38:	05 00 02 00 00       	add    $0x200,%eax
   26f3d:	83 ed 01             	sub    $0x1,%ebp
   26f40:	73 9e                	jae    26ee0 <gouraud_deob_draw_triangle+0x17f0>
   26f42:	e9 9e ea ff ff       	jmp    259e5 <gouraud_deob_draw_triangle+0x2f5>
   26f47:	8b 54 24 18          	mov    0x18(%rsp),%edx
   26f4b:	39 54 24 0c          	cmp    %edx,0xc(%rsp)
   26f4f:	0f 8d 7c fe ff ff    	jge    26dd1 <gouraud_deob_draw_triangle+0x16e1>
   26f55:	e9 71 ec ff ff       	jmp    25bcb <gouraud_deob_draw_triangle+0x4db>
   26f5a:	66 0f 1f 44 00 00    	nopw   0x0(%rax,%rax,1)
   26f60:	83 ef 01             	sub    $0x1,%edi
   26f63:	41 89 fc             	mov    %edi,%r12d
   26f66:	0f 88 79 ea ff ff    	js     259e5 <gouraud_deob_draw_triangle+0x2f5>
   26f6c:	41 89 e8             	mov    %ebp,%r8d
   26f6f:	44 8b 6c 24 0c       	mov    0xc(%rsp),%r13d
   26f74:	89 dd                	mov    %ebx,%ebp
   26f76:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
   26f7b:	8b 5c 24 20          	mov    0x20(%rsp),%ebx
   26f7f:	e9 c4 fd ff ff       	jmp    26d48 <gouraud_deob_draw_triangle+0x1658>
   26f84:	45 85 d2             	test   %r10d,%r10d
   26f87:	0f 8e 58 ea ff ff    	jle    259e5 <gouraud_deob_draw_triangle+0x2f5>
   26f8d:	e9 17 fc ff ff       	jmp    26ba9 <gouraud_deob_draw_triangle+0x14b9>
   26f92:	66 0f 1f 44 00 00    	nopw   0x0(%rax,%rax,1)
   26f98:	83 ee 01             	sub    $0x1,%esi
   26f9b:	89 f5                	mov    %esi,%ebp
   26f9d:	0f 88 42 ea ff ff    	js     259e5 <gouraud_deob_draw_triangle+0x2f5>
   26fa3:	44 89 f8             	mov    %r15d,%eax
   26fa6:	41 89 f8             	mov    %edi,%r8d
   26fa9:	41 89 df             	mov    %ebx,%r15d
   26fac:	44 8b 6c 24 0c       	mov    0xc(%rsp),%r13d
   26fb1:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
   26fb6:	8b 5c 24 18          	mov    0x18(%rsp),%ebx
   26fba:	e9 21 ff ff ff       	jmp    26ee0 <gouraud_deob_draw_triangle+0x17f0>
   26fbf:	8d 6e ff             	lea    -0x1(%rsi),%ebp
   26fc2:	eb df                	jmp    26fa3 <gouraud_deob_draw_triangle+0x18b3>
   26fc4:	44 8d 67 ff          	lea    -0x1(%rdi),%r12d
   26fc8:	eb a2                	jmp    26f6c <gouraud_deob_draw_triangle+0x187c>

Disassembly of section .fini:
