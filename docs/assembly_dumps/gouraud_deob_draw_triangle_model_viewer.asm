
build_release/model_viewer:     file format elf64-x86-64


Disassembly of section .init:

Disassembly of section .plt:

Disassembly of section .plt.got:

Disassembly of section .plt.sec:

Disassembly of section .text:

0000000000029ec0 <gouraud_deob_draw_triangle>:
   29ec0:	f3 0f 1e fa          	endbr64
   29ec4:	41 57                	push   %r15
   29ec6:	41 89 f3             	mov    %esi,%r11d
   29ec9:	41 89 d2             	mov    %edx,%r10d
   29ecc:	41 56                	push   %r14
   29ece:	41 55                	push   %r13
   29ed0:	41 89 cd             	mov    %ecx,%r13d
   29ed3:	29 f1                	sub    %esi,%ecx
   29ed5:	41 54                	push   %r12
   29ed7:	55                   	push   %rbp
   29ed8:	53                   	push   %rbx
   29ed9:	44 89 cb             	mov    %r9d,%ebx
   29edc:	44 29 c3             	sub    %r8d,%ebx
   29edf:	48 83 ec 48          	sub    $0x48,%rsp
   29ee3:	44 8b b4 24 80 00 00 	mov    0x80(%rsp),%r14d
   29eea:	00 
   29eeb:	48 89 7c 24 10       	mov    %rdi,0x10(%rsp)
   29ef0:	89 d7                	mov    %edx,%edi
   29ef2:	29 f7                	sub    %esi,%edi
   29ef4:	44 89 f5             	mov    %r14d,%ebp
   29ef7:	44 29 c5             	sub    %r8d,%ebp
   29efa:	44 39 ea             	cmp    %r13d,%edx
   29efd:	0f 84 e5 02 00 00    	je     2a1e8 <gouraud_deob_draw_triangle+0x328>
   29f03:	44 89 f0             	mov    %r14d,%eax
   29f06:	44 89 ee             	mov    %r13d,%esi
   29f09:	44 29 c8             	sub    %r9d,%eax
   29f0c:	29 d6                	sub    %edx,%esi
   29f0e:	c1 e0 0e             	shl    $0xe,%eax
   29f11:	99                   	cltd
   29f12:	f7 fe                	idiv   %esi
   29f14:	89 44 24 20          	mov    %eax,0x20(%rsp)
   29f18:	45 39 da             	cmp    %r11d,%r10d
   29f1b:	0f 84 a7 02 00 00    	je     2a1c8 <gouraud_deob_draw_triangle+0x308>
   29f21:	89 d8                	mov    %ebx,%eax
   29f23:	c7 44 24 0c 00 00 00 	movl   $0x0,0xc(%rsp)
   29f2a:	00 
   29f2b:	c1 e0 0e             	shl    $0xe,%eax
   29f2e:	99                   	cltd
   29f2f:	f7 ff                	idiv   %edi
   29f31:	89 44 24 18          	mov    %eax,0x18(%rsp)
   29f35:	45 39 eb             	cmp    %r13d,%r11d
   29f38:	0f 85 92 02 00 00    	jne    2a1d0 <gouraud_deob_draw_triangle+0x310>
   29f3e:	89 de                	mov    %ebx,%esi
   29f40:	89 f8                	mov    %edi,%eax
   29f42:	0f af f1             	imul   %ecx,%esi
   29f45:	0f af c5             	imul   %ebp,%eax
   29f48:	29 c6                	sub    %eax,%esi
   29f4a:	0f 84 65 02 00 00    	je     2a1b5 <gouraud_deob_draw_triangle+0x2f5>
   29f50:	44 8b a4 24 90 00 00 	mov    0x90(%rsp),%r12d
   29f57:	00 
   29f58:	44 8b bc 24 98 00 00 	mov    0x98(%rsp),%r15d
   29f5f:	00 
   29f60:	44 2b a4 24 88 00 00 	sub    0x88(%rsp),%r12d
   29f67:	00 
   29f68:	44 2b bc 24 88 00 00 	sub    0x88(%rsp),%r15d
   29f6f:	00 
   29f70:	41 0f af cc          	imul   %r12d,%ecx
   29f74:	41 0f af ff          	imul   %r15d,%edi
   29f78:	41 0f af df          	imul   %r15d,%ebx
   29f7c:	44 0f af e5          	imul   %ebp,%r12d
   29f80:	29 f9                	sub    %edi,%ecx
   29f82:	c1 e1 08             	shl    $0x8,%ecx
   29f85:	89 c8                	mov    %ecx,%eax
   29f87:	44 29 e3             	sub    %r12d,%ebx
   29f8a:	99                   	cltd
   29f8b:	c1 e3 08             	shl    $0x8,%ebx
   29f8e:	f7 fe                	idiv   %esi
   29f90:	89 44 24 1c          	mov    %eax,0x1c(%rsp)
   29f94:	89 d8                	mov    %ebx,%eax
   29f96:	99                   	cltd
   29f97:	f7 fe                	idiv   %esi
   29f99:	45 39 ea             	cmp    %r13d,%r10d
   29f9c:	89 c3                	mov    %eax,%ebx
   29f9e:	44 89 e8             	mov    %r13d,%eax
   29fa1:	41 0f 4e c2          	cmovle %r10d,%eax
   29fa5:	41 39 c3             	cmp    %eax,%r11d
   29fa8:	0f 8f 62 02 00 00    	jg     2a210 <gouraud_deob_draw_triangle+0x350>
   29fae:	41 81 fb 7f 01 00 00 	cmp    $0x17f,%r11d
   29fb5:	0f 8f fa 01 00 00    	jg     2a1b5 <gouraud_deob_draw_triangle+0x2f5>
   29fbb:	b8 80 01 00 00       	mov    $0x180,%eax
   29fc0:	8b 54 24 1c          	mov    0x1c(%rsp),%edx
   29fc4:	8b ac 24 88 00 00 00 	mov    0x88(%rsp),%ebp
   29fcb:	41 39 c2             	cmp    %eax,%r10d
   29fce:	89 c1                	mov    %eax,%ecx
   29fd0:	89 c7                	mov    %eax,%edi
   29fd2:	41 0f 4e ca          	cmovle %r10d,%ecx
   29fd6:	41 39 c5             	cmp    %eax,%r13d
   29fd9:	89 d0                	mov    %edx,%eax
   29fdb:	41 0f 4e fd          	cmovle %r13d,%edi
   29fdf:	c1 e5 08             	shl    $0x8,%ebp
   29fe2:	41 0f af c0          	imul   %r8d,%eax
   29fe6:	41 c1 e0 0e          	shl    $0xe,%r8d
   29fea:	45 89 c4             	mov    %r8d,%r12d
   29fed:	29 c5                	sub    %eax,%ebp
   29fef:	01 d5                	add    %edx,%ebp
   29ff1:	39 cf                	cmp    %ecx,%edi
   29ff3:	0f 8e 57 06 00 00    	jle    2a650 <gouraud_deob_draw_triangle+0x790>
   29ff9:	45 85 db             	test   %r11d,%r11d
   29ffc:	0f 88 9e 0b 00 00    	js     2aba0 <gouraud_deob_draw_triangle+0xce0>
   2a002:	44 89 d8             	mov    %r11d,%eax
   2a005:	45 89 c6             	mov    %r8d,%r14d
   2a008:	c1 e0 09             	shl    $0x9,%eax
   2a00b:	8b 54 24 18          	mov    0x18(%rsp),%edx
   2a00f:	41 c1 e1 0e          	shl    $0xe,%r9d
   2a013:	39 54 24 0c          	cmp    %edx,0xc(%rsp)
   2a017:	45 89 cf             	mov    %r9d,%r15d
   2a01a:	0f 9d c2             	setge  %dl
   2a01d:	45 85 d2             	test   %r10d,%r10d
   2a020:	0f 88 9a 0d 00 00    	js     2adc0 <gouraud_deob_draw_triangle+0xf00>
   2a026:	29 cf                	sub    %ecx,%edi
   2a028:	41 39 cb             	cmp    %ecx,%r11d
   2a02b:	0f 84 4f 15 00 00    	je     2b580 <gouraud_deob_draw_triangle+0x16c0>
   2a031:	84 d2                	test   %dl,%dl
   2a033:	0f 85 c7 13 00 00    	jne    2b400 <gouraud_deob_draw_triangle+0x1540>
   2a039:	44 29 d9             	sub    %r11d,%ecx
   2a03c:	41 89 ca             	mov    %ecx,%r10d
   2a03f:	83 e9 01             	sub    $0x1,%ecx
   2a042:	0f 88 e7 00 00 00    	js     2a12f <gouraud_deob_draw_triangle+0x26f>
   2a048:	41 c1 e2 09          	shl    $0x9,%r10d
   2a04c:	89 7c 24 28          	mov    %edi,0x28(%rsp)
   2a050:	41 89 e8             	mov    %ebp,%r8d
   2a053:	45 89 f5             	mov    %r14d,%r13d
   2a056:	44 89 4c 24 30       	mov    %r9d,0x30(%rsp)
   2a05b:	41 01 c2             	add    %eax,%r10d
   2a05e:	41 89 db             	mov    %ebx,%r11d
   2a061:	89 c6                	mov    %eax,%esi
   2a063:	89 44 24 34          	mov    %eax,0x34(%rsp)
   2a067:	45 89 c7             	mov    %r8d,%r15d
   2a06a:	44 89 d3             	mov    %r10d,%ebx
   2a06d:	89 4c 24 3c          	mov    %ecx,0x3c(%rsp)
   2a071:	89 6c 24 2c          	mov    %ebp,0x2c(%rsp)
   2a075:	8b 6c 24 0c          	mov    0xc(%rsp),%ebp
   2a079:	44 89 74 24 38       	mov    %r14d,0x38(%rsp)
   2a07e:	44 8b 74 24 18       	mov    0x18(%rsp),%r14d
   2a083:	0f 1f 44 00 00       	nopl   0x0(%rax,%rax,1)
   2a088:	44 89 e1             	mov    %r12d,%ecx
   2a08b:	44 89 ea             	mov    %r13d,%edx
   2a08e:	44 89 e0             	mov    %r12d,%eax
   2a091:	c1 f9 0e             	sar    $0xe,%ecx
   2a094:	c1 fa 0e             	sar    $0xe,%edx
   2a097:	44 09 e8             	or     %r13d,%eax
   2a09a:	78 41                	js     2a0dd <gouraud_deob_draw_triangle+0x21d>
   2a09c:	81 fa 00 02 00 00    	cmp    $0x200,%edx
   2a0a2:	40 0f 9f c7          	setg   %dil
   2a0a6:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
   2a0ac:	41 0f 9f c1          	setg   %r9b
   2a0b0:	44 08 cf             	or     %r9b,%dil
   2a0b3:	75 28                	jne    2a0dd <gouraud_deob_draw_triangle+0x21d>
   2a0b5:	39 d1                	cmp    %edx,%ecx
   2a0b7:	7e 24                	jle    2a0dd <gouraud_deob_draw_triangle+0x21d>
   2a0b9:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
   2a0be:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
   2a0c3:	45 89 f8             	mov    %r15d,%r8d
   2a0c6:	89 74 24 18          	mov    %esi,0x18(%rsp)
   2a0ca:	44 89 5c 24 24       	mov    %r11d,0x24(%rsp)
   2a0cf:	e8 2c fd ff ff       	call   29e00 <gouraud_deob_draw_scanline.part.0>
   2a0d4:	44 8b 5c 24 24       	mov    0x24(%rsp),%r11d
   2a0d9:	8b 74 24 18          	mov    0x18(%rsp),%esi
   2a0dd:	81 c6 00 02 00 00    	add    $0x200,%esi
   2a0e3:	41 01 ed             	add    %ebp,%r13d
   2a0e6:	45 01 f4             	add    %r14d,%r12d
   2a0e9:	45 01 df             	add    %r11d,%r15d
   2a0ec:	39 f3                	cmp    %esi,%ebx
   2a0ee:	75 98                	jne    2a088 <gouraud_deob_draw_triangle+0x1c8>
   2a0f0:	8b 4c 24 3c          	mov    0x3c(%rsp),%ecx
   2a0f4:	8b 74 24 0c          	mov    0xc(%rsp),%esi
   2a0f8:	44 89 da             	mov    %r11d,%edx
   2a0fb:	44 89 db             	mov    %r11d,%ebx
   2a0fe:	44 8b 74 24 38       	mov    0x38(%rsp),%r14d
   2a103:	8b 6c 24 2c          	mov    0x2c(%rsp),%ebp
   2a107:	0f af d1             	imul   %ecx,%edx
   2a10a:	8b 44 24 34          	mov    0x34(%rsp),%eax
   2a10e:	8b 7c 24 28          	mov    0x28(%rsp),%edi
   2a112:	41 01 f6             	add    %esi,%r14d
   2a115:	0f af f1             	imul   %ecx,%esi
   2a118:	44 01 dd             	add    %r11d,%ebp
   2a11b:	c1 e1 09             	shl    $0x9,%ecx
   2a11e:	44 8b 7c 24 30       	mov    0x30(%rsp),%r15d
   2a123:	8d 84 08 00 02 00 00 	lea    0x200(%rax,%rcx,1),%eax
   2a12a:	01 d5                	add    %edx,%ebp
   2a12c:	41 01 f6             	add    %esi,%r14d
   2a12f:	c1 e7 09             	shl    $0x9,%edi
   2a132:	41 89 e8             	mov    %ebp,%r8d
   2a135:	41 89 c5             	mov    %eax,%r13d
   2a138:	8b 6c 24 20          	mov    0x20(%rsp),%ebp
   2a13c:	41 89 fc             	mov    %edi,%r12d
   2a13f:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
   2a144:	41 01 c4             	add    %eax,%r12d
   2a147:	89 d8                	mov    %ebx,%eax
   2a149:	8b 5c 24 0c          	mov    0xc(%rsp),%ebx
   2a14d:	0f 1f 00             	nopl   (%rax)
   2a150:	44 89 f9             	mov    %r15d,%ecx
   2a153:	44 89 f2             	mov    %r14d,%edx
   2a156:	44 89 fe             	mov    %r15d,%esi
   2a159:	c1 f9 0e             	sar    $0xe,%ecx
   2a15c:	c1 fa 0e             	sar    $0xe,%edx
   2a15f:	44 09 f6             	or     %r14d,%esi
   2a162:	78 3c                	js     2a1a0 <gouraud_deob_draw_triangle+0x2e0>
   2a164:	81 fa 00 02 00 00    	cmp    $0x200,%edx
   2a16a:	40 0f 9f c6          	setg   %sil
   2a16e:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
   2a174:	41 0f 9f c1          	setg   %r9b
   2a178:	44 08 ce             	or     %r9b,%sil
   2a17b:	75 23                	jne    2a1a0 <gouraud_deob_draw_triangle+0x2e0>
   2a17d:	39 d1                	cmp    %edx,%ecx
   2a17f:	7e 1f                	jle    2a1a0 <gouraud_deob_draw_triangle+0x2e0>
   2a181:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
   2a186:	44 89 ee             	mov    %r13d,%esi
   2a189:	89 44 24 18          	mov    %eax,0x18(%rsp)
   2a18d:	44 89 44 24 0c       	mov    %r8d,0xc(%rsp)
   2a192:	e8 69 fc ff ff       	call   29e00 <gouraud_deob_draw_scanline.part.0>
   2a197:	8b 44 24 18          	mov    0x18(%rsp),%eax
   2a19b:	44 8b 44 24 0c       	mov    0xc(%rsp),%r8d
   2a1a0:	41 81 c5 00 02 00 00 	add    $0x200,%r13d
   2a1a7:	41 01 de             	add    %ebx,%r14d
   2a1aa:	41 01 ef             	add    %ebp,%r15d
   2a1ad:	41 01 c0             	add    %eax,%r8d
   2a1b0:	45 39 ec             	cmp    %r13d,%r12d
   2a1b3:	75 9b                	jne    2a150 <gouraud_deob_draw_triangle+0x290>
   2a1b5:	48 83 c4 48          	add    $0x48,%rsp
   2a1b9:	5b                   	pop    %rbx
   2a1ba:	5d                   	pop    %rbp
   2a1bb:	41 5c                	pop    %r12
   2a1bd:	41 5d                	pop    %r13
   2a1bf:	41 5e                	pop    %r14
   2a1c1:	41 5f                	pop    %r15
   2a1c3:	c3                   	ret
   2a1c4:	0f 1f 40 00          	nopl   0x0(%rax)
   2a1c8:	c7 44 24 18 00 00 00 	movl   $0x0,0x18(%rsp)
   2a1cf:	00 
   2a1d0:	89 e8                	mov    %ebp,%eax
   2a1d2:	c1 e0 0e             	shl    $0xe,%eax
   2a1d5:	99                   	cltd
   2a1d6:	f7 f9                	idiv   %ecx
   2a1d8:	89 44 24 0c          	mov    %eax,0xc(%rsp)
   2a1dc:	e9 5d fd ff ff       	jmp    29f3e <gouraud_deob_draw_triangle+0x7e>
   2a1e1:	0f 1f 80 00 00 00 00 	nopl   0x0(%rax)
   2a1e8:	39 f2                	cmp    %esi,%edx
   2a1ea:	0f 84 40 04 00 00    	je     2a630 <gouraud_deob_draw_triangle+0x770>
   2a1f0:	89 d8                	mov    %ebx,%eax
   2a1f2:	c7 44 24 20 00 00 00 	movl   $0x0,0x20(%rsp)
   2a1f9:	00 
   2a1fa:	c1 e0 0e             	shl    $0xe,%eax
   2a1fd:	99                   	cltd
   2a1fe:	f7 ff                	idiv   %edi
   2a200:	89 44 24 18          	mov    %eax,0x18(%rsp)
   2a204:	eb ca                	jmp    2a1d0 <gouraud_deob_draw_triangle+0x310>
   2a206:	66 2e 0f 1f 84 00 00 	cs nopw 0x0(%rax,%rax,1)
   2a20d:	00 00 00 
   2a210:	45 39 ea             	cmp    %r13d,%r10d
   2a213:	0f 8f 17 02 00 00    	jg     2a430 <gouraud_deob_draw_triangle+0x570>
   2a219:	41 81 fa 7f 01 00 00 	cmp    $0x17f,%r10d
   2a220:	7f 93                	jg     2a1b5 <gouraud_deob_draw_triangle+0x2f5>
   2a222:	b8 80 01 00 00       	mov    $0x180,%eax
   2a227:	8b 7c 24 1c          	mov    0x1c(%rsp),%edi
   2a22b:	41 39 c5             	cmp    %eax,%r13d
   2a22e:	41 0f 4e c5          	cmovle %r13d,%eax
   2a232:	89 fa                	mov    %edi,%edx
   2a234:	89 c1                	mov    %eax,%ecx
   2a236:	b8 80 01 00 00       	mov    $0x180,%eax
   2a23b:	41 39 c3             	cmp    %eax,%r11d
   2a23e:	41 0f 4e c3          	cmovle %r11d,%eax
   2a242:	41 0f af d1          	imul   %r9d,%edx
   2a246:	41 c1 e1 0e          	shl    $0xe,%r9d
   2a24a:	44 89 cd             	mov    %r9d,%ebp
   2a24d:	89 c6                	mov    %eax,%esi
   2a24f:	8b 84 24 90 00 00 00 	mov    0x90(%rsp),%eax
   2a256:	c1 e0 08             	shl    $0x8,%eax
   2a259:	29 d0                	sub    %edx,%eax
   2a25b:	01 c7                	add    %eax,%edi
   2a25d:	39 f1                	cmp    %esi,%ecx
   2a25f:	0f 8d bb 05 00 00    	jge    2a820 <gouraud_deob_draw_triangle+0x960>
   2a265:	45 85 d2             	test   %r10d,%r10d
   2a268:	0f 88 62 0d 00 00    	js     2afd0 <gouraud_deob_draw_triangle+0x1110>
   2a26e:	45 89 d7             	mov    %r10d,%r15d
   2a271:	45 89 cc             	mov    %r9d,%r12d
   2a274:	41 c1 e7 09          	shl    $0x9,%r15d
   2a278:	8b 54 24 18          	mov    0x18(%rsp),%edx
   2a27c:	41 c1 e6 0e          	shl    $0xe,%r14d
   2a280:	39 54 24 20          	cmp    %edx,0x20(%rsp)
   2a284:	0f 9e c0             	setle  %al
   2a287:	45 85 ed             	test   %r13d,%r13d
   2a28a:	0f 88 98 0b 00 00    	js     2ae28 <gouraud_deob_draw_triangle+0xf68>
   2a290:	29 ce                	sub    %ecx,%esi
   2a292:	41 39 ca             	cmp    %ecx,%r10d
   2a295:	0f 84 7c 14 00 00    	je     2b717 <gouraud_deob_draw_triangle+0x1857>
   2a29b:	84 c0                	test   %al,%al
   2a29d:	0f 85 f5 12 00 00    	jne    2b598 <gouraud_deob_draw_triangle+0x16d8>
   2a2a3:	89 c8                	mov    %ecx,%eax
   2a2a5:	44 29 d0             	sub    %r10d,%eax
   2a2a8:	89 c1                	mov    %eax,%ecx
   2a2aa:	83 e9 01             	sub    $0x1,%ecx
   2a2ad:	0f 88 e8 00 00 00    	js     2a39b <gouraud_deob_draw_triangle+0x4db>
   2a2b3:	c1 e0 09             	shl    $0x9,%eax
   2a2b6:	44 89 7c 24 30       	mov    %r15d,0x30(%rsp)
   2a2bb:	45 89 e5             	mov    %r12d,%r13d
   2a2be:	89 4c 24 34          	mov    %ecx,0x34(%rsp)
   2a2c2:	46 8d 14 38          	lea    (%rax,%r15,1),%r10d
   2a2c6:	89 d8                	mov    %ebx,%eax
   2a2c8:	8b 5c 24 18          	mov    0x18(%rsp),%ebx
   2a2cc:	89 7c 24 38          	mov    %edi,0x38(%rsp)
   2a2d0:	89 74 24 3c          	mov    %esi,0x3c(%rsp)
   2a2d4:	44 89 64 24 2c       	mov    %r12d,0x2c(%rsp)
   2a2d9:	44 8b 64 24 20       	mov    0x20(%rsp),%r12d
   2a2de:	44 89 74 24 28       	mov    %r14d,0x28(%rsp)
   2a2e3:	41 89 fe             	mov    %edi,%r14d
   2a2e6:	66 2e 0f 1f 84 00 00 	cs nopw 0x0(%rax,%rax,1)
   2a2ed:	00 00 00 
   2a2f0:	89 e9                	mov    %ebp,%ecx
   2a2f2:	44 89 ea             	mov    %r13d,%edx
   2a2f5:	89 ef                	mov    %ebp,%edi
   2a2f7:	c1 f9 0e             	sar    $0xe,%ecx
   2a2fa:	c1 fa 0e             	sar    $0xe,%edx
   2a2fd:	44 09 ef             	or     %r13d,%edi
   2a300:	78 44                	js     2a346 <gouraud_deob_draw_triangle+0x486>
   2a302:	81 fa 00 02 00 00    	cmp    $0x200,%edx
   2a308:	40 0f 9f c7          	setg   %dil
   2a30c:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
   2a312:	41 0f 9f c1          	setg   %r9b
   2a316:	44 08 cf             	or     %r9b,%dil
   2a319:	75 2b                	jne    2a346 <gouraud_deob_draw_triangle+0x486>
   2a31b:	39 d1                	cmp    %edx,%ecx
   2a31d:	7e 27                	jle    2a346 <gouraud_deob_draw_triangle+0x486>
   2a31f:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
   2a324:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
   2a329:	45 89 f0             	mov    %r14d,%r8d
   2a32c:	44 89 fe             	mov    %r15d,%esi
   2a32f:	89 44 24 24          	mov    %eax,0x24(%rsp)
   2a333:	44 89 54 24 20       	mov    %r10d,0x20(%rsp)
   2a338:	e8 c3 fa ff ff       	call   29e00 <gouraud_deob_draw_scanline.part.0>
   2a33d:	8b 44 24 24          	mov    0x24(%rsp),%eax
   2a341:	44 8b 54 24 20       	mov    0x20(%rsp),%r10d
   2a346:	41 81 c7 00 02 00 00 	add    $0x200,%r15d
   2a34d:	41 01 dd             	add    %ebx,%r13d
   2a350:	44 01 e5             	add    %r12d,%ebp
   2a353:	41 01 c6             	add    %eax,%r14d
   2a356:	45 39 d7             	cmp    %r10d,%r15d
   2a359:	75 95                	jne    2a2f0 <gouraud_deob_draw_triangle+0x430>
   2a35b:	8b 4c 24 34          	mov    0x34(%rsp),%ecx
   2a35f:	89 c3                	mov    %eax,%ebx
   2a361:	44 8b 64 24 2c       	mov    0x2c(%rsp),%r12d
   2a366:	8b 44 24 18          	mov    0x18(%rsp),%eax
   2a36a:	89 da                	mov    %ebx,%edx
   2a36c:	8b 7c 24 38          	mov    0x38(%rsp),%edi
   2a370:	0f af d1             	imul   %ecx,%edx
   2a373:	44 8b 7c 24 30       	mov    0x30(%rsp),%r15d
   2a378:	44 8b 74 24 28       	mov    0x28(%rsp),%r14d
   2a37d:	41 01 c4             	add    %eax,%r12d
   2a380:	0f af c1             	imul   %ecx,%eax
   2a383:	8b 74 24 3c          	mov    0x3c(%rsp),%esi
   2a387:	c1 e1 09             	shl    $0x9,%ecx
   2a38a:	45 8d bc 0f 00 02 00 	lea    0x200(%r15,%rcx,1),%r15d
   2a391:	00 
   2a392:	41 01 c4             	add    %eax,%r12d
   2a395:	8d 04 1f             	lea    (%rdi,%rbx,1),%eax
   2a398:	8d 3c 02             	lea    (%rdx,%rax,1),%edi
   2a39b:	c1 e6 09             	shl    $0x9,%esi
   2a39e:	41 89 f8             	mov    %edi,%r8d
   2a3a1:	89 d8                	mov    %ebx,%eax
   2a3a3:	44 8b 6c 24 0c       	mov    0xc(%rsp),%r13d
   2a3a8:	89 f5                	mov    %esi,%ebp
   2a3aa:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
   2a3af:	8b 5c 24 18          	mov    0x18(%rsp),%ebx
   2a3b3:	44 01 fd             	add    %r15d,%ebp
   2a3b6:	66 2e 0f 1f 84 00 00 	cs nopw 0x0(%rax,%rax,1)
   2a3bd:	00 00 00 
   2a3c0:	44 89 f1             	mov    %r14d,%ecx
   2a3c3:	44 89 e2             	mov    %r12d,%edx
   2a3c6:	44 89 f6             	mov    %r14d,%esi
   2a3c9:	c1 f9 0e             	sar    $0xe,%ecx
   2a3cc:	c1 fa 0e             	sar    $0xe,%edx
   2a3cf:	44 09 e6             	or     %r12d,%esi
   2a3d2:	78 3c                	js     2a410 <gouraud_deob_draw_triangle+0x550>
   2a3d4:	81 fa 00 02 00 00    	cmp    $0x200,%edx
   2a3da:	40 0f 9f c6          	setg   %sil
   2a3de:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
   2a3e4:	41 0f 9f c1          	setg   %r9b
   2a3e8:	44 08 ce             	or     %r9b,%sil
   2a3eb:	75 23                	jne    2a410 <gouraud_deob_draw_triangle+0x550>
   2a3ed:	39 d1                	cmp    %edx,%ecx
   2a3ef:	7e 1f                	jle    2a410 <gouraud_deob_draw_triangle+0x550>
   2a3f1:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
   2a3f6:	44 89 fe             	mov    %r15d,%esi
   2a3f9:	89 44 24 18          	mov    %eax,0x18(%rsp)
   2a3fd:	44 89 44 24 0c       	mov    %r8d,0xc(%rsp)
   2a402:	e8 f9 f9 ff ff       	call   29e00 <gouraud_deob_draw_scanline.part.0>
   2a407:	8b 44 24 18          	mov    0x18(%rsp),%eax
   2a40b:	44 8b 44 24 0c       	mov    0xc(%rsp),%r8d
   2a410:	41 81 c7 00 02 00 00 	add    $0x200,%r15d
   2a417:	41 01 dc             	add    %ebx,%r12d
   2a41a:	45 01 ee             	add    %r13d,%r14d
   2a41d:	41 01 c0             	add    %eax,%r8d
   2a420:	41 39 ef             	cmp    %ebp,%r15d
   2a423:	75 9b                	jne    2a3c0 <gouraud_deob_draw_triangle+0x500>
   2a425:	e9 8b fd ff ff       	jmp    2a1b5 <gouraud_deob_draw_triangle+0x2f5>
   2a42a:	66 0f 1f 44 00 00    	nopw   0x0(%rax,%rax,1)
   2a430:	41 81 fd 7f 01 00 00 	cmp    $0x17f,%r13d
   2a437:	0f 8f 78 fd ff ff    	jg     2a1b5 <gouraud_deob_draw_triangle+0x2f5>
   2a43d:	b8 80 01 00 00       	mov    $0x180,%eax
   2a442:	8b 7c 24 1c          	mov    0x1c(%rsp),%edi
   2a446:	41 39 c3             	cmp    %eax,%r11d
   2a449:	41 0f 4e c3          	cmovle %r11d,%eax
   2a44d:	89 fa                	mov    %edi,%edx
   2a44f:	89 c1                	mov    %eax,%ecx
   2a451:	b8 80 01 00 00       	mov    $0x180,%eax
   2a456:	41 39 c2             	cmp    %eax,%r10d
   2a459:	41 0f 4e c2          	cmovle %r10d,%eax
   2a45d:	41 0f af d6          	imul   %r14d,%edx
   2a461:	41 c1 e6 0e          	shl    $0xe,%r14d
   2a465:	89 c6                	mov    %eax,%esi
   2a467:	8b 84 24 98 00 00 00 	mov    0x98(%rsp),%eax
   2a46e:	c1 e0 08             	shl    $0x8,%eax
   2a471:	29 d0                	sub    %edx,%eax
   2a473:	01 c7                	add    %eax,%edi
   2a475:	39 ce                	cmp    %ecx,%esi
   2a477:	0f 8e 63 05 00 00    	jle    2a9e0 <gouraud_deob_draw_triangle+0xb20>
   2a47d:	45 85 ed             	test   %r13d,%r13d
   2a480:	0f 88 ea 0a 00 00    	js     2af70 <gouraud_deob_draw_triangle+0x10b0>
   2a486:	45 89 ef             	mov    %r13d,%r15d
   2a489:	44 89 f5             	mov    %r14d,%ebp
   2a48c:	41 c1 e7 09          	shl    $0x9,%r15d
   2a490:	41 c1 e0 0e          	shl    $0xe,%r8d
   2a494:	45 89 c4             	mov    %r8d,%r12d
   2a497:	45 85 db             	test   %r11d,%r11d
   2a49a:	0f 88 a0 0a 00 00    	js     2af40 <gouraud_deob_draw_triangle+0x1080>
   2a4a0:	89 c8                	mov    %ecx,%eax
   2a4a2:	8b 54 24 0c          	mov    0xc(%rsp),%edx
   2a4a6:	44 8b 44 24 20       	mov    0x20(%rsp),%r8d
   2a4ab:	29 ce                	sub    %ecx,%esi
   2a4ad:	44 29 e8             	sub    %r13d,%eax
   2a4b0:	8d 48 ff             	lea    -0x1(%rax),%ecx
   2a4b3:	44 39 c2             	cmp    %r8d,%edx
   2a4b6:	0f 8e d7 0d 00 00    	jle    2b293 <gouraud_deob_draw_triangle+0x13d3>
   2a4bc:	85 c9                	test   %ecx,%ecx
   2a4be:	0f 88 df 00 00 00    	js     2a5a3 <gouraud_deob_draw_triangle+0x6e3>
   2a4c4:	89 7c 24 34          	mov    %edi,0x34(%rsp)
   2a4c8:	45 89 fa             	mov    %r15d,%r10d
   2a4cb:	41 89 ed             	mov    %ebp,%r13d
   2a4ce:	41 89 db             	mov    %ebx,%r11d
   2a4d1:	89 4c 24 3c          	mov    %ecx,0x3c(%rsp)
   2a4d5:	89 d3                	mov    %edx,%ebx
   2a4d7:	89 6c 24 28          	mov    %ebp,0x28(%rsp)
   2a4db:	89 cd                	mov    %ecx,%ebp
   2a4dd:	44 89 64 24 2c       	mov    %r12d,0x2c(%rsp)
   2a4e2:	45 89 c4             	mov    %r8d,%r12d
   2a4e5:	44 89 7c 24 30       	mov    %r15d,0x30(%rsp)
   2a4ea:	41 89 ff             	mov    %edi,%r15d
   2a4ed:	89 74 24 38          	mov    %esi,0x38(%rsp)
   2a4f1:	44 89 d6             	mov    %r10d,%esi
   2a4f4:	0f 1f 40 00          	nopl   0x0(%rax)
   2a4f8:	44 89 f1             	mov    %r14d,%ecx
   2a4fb:	44 89 ea             	mov    %r13d,%edx
   2a4fe:	44 89 f0             	mov    %r14d,%eax
   2a501:	c1 f9 0e             	sar    $0xe,%ecx
   2a504:	c1 fa 0e             	sar    $0xe,%edx
   2a507:	44 09 e8             	or     %r13d,%eax
   2a50a:	78 41                	js     2a54d <gouraud_deob_draw_triangle+0x68d>
   2a50c:	81 fa 00 02 00 00    	cmp    $0x200,%edx
   2a512:	40 0f 9f c7          	setg   %dil
   2a516:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
   2a51c:	41 0f 9f c1          	setg   %r9b
   2a520:	44 08 cf             	or     %r9b,%dil
   2a523:	75 28                	jne    2a54d <gouraud_deob_draw_triangle+0x68d>
   2a525:	39 d1                	cmp    %edx,%ecx
   2a527:	7e 24                	jle    2a54d <gouraud_deob_draw_triangle+0x68d>
   2a529:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
   2a52e:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
   2a533:	45 89 f8             	mov    %r15d,%r8d
   2a536:	89 74 24 0c          	mov    %esi,0xc(%rsp)
   2a53a:	44 89 5c 24 24       	mov    %r11d,0x24(%rsp)
   2a53f:	e8 bc f8 ff ff       	call   29e00 <gouraud_deob_draw_scanline.part.0>
   2a544:	44 8b 5c 24 24       	mov    0x24(%rsp),%r11d
   2a549:	8b 74 24 0c          	mov    0xc(%rsp),%esi
   2a54d:	45 01 e5             	add    %r12d,%r13d
   2a550:	41 01 de             	add    %ebx,%r14d
   2a553:	45 01 df             	add    %r11d,%r15d
   2a556:	81 c6 00 02 00 00    	add    $0x200,%esi
   2a55c:	83 ed 01             	sub    $0x1,%ebp
   2a55f:	73 97                	jae    2a4f8 <gouraud_deob_draw_triangle+0x638>
   2a561:	8b 4c 24 3c          	mov    0x3c(%rsp),%ecx
   2a565:	8b 44 24 20          	mov    0x20(%rsp),%eax
   2a569:	44 89 da             	mov    %r11d,%edx
   2a56c:	44 89 db             	mov    %r11d,%ebx
   2a56f:	8b 6c 24 28          	mov    0x28(%rsp),%ebp
   2a573:	8b 7c 24 34          	mov    0x34(%rsp),%edi
   2a577:	0f af d1             	imul   %ecx,%edx
   2a57a:	44 8b 7c 24 30       	mov    0x30(%rsp),%r15d
   2a57f:	44 8b 64 24 2c       	mov    0x2c(%rsp),%r12d
   2a584:	01 c5                	add    %eax,%ebp
   2a586:	0f af c1             	imul   %ecx,%eax
   2a589:	8b 74 24 38          	mov    0x38(%rsp),%esi
   2a58d:	01 c5                	add    %eax,%ebp
   2a58f:	42 8d 04 1f          	lea    (%rdi,%r11,1),%eax
   2a593:	8d 3c 02             	lea    (%rdx,%rax,1),%edi
   2a596:	89 c8                	mov    %ecx,%eax
   2a598:	c1 e0 09             	shl    $0x9,%eax
   2a59b:	45 8d bc 07 00 02 00 	lea    0x200(%r15,%rax,1),%r15d
   2a5a2:	00 
   2a5a3:	c1 e6 09             	shl    $0x9,%esi
   2a5a6:	41 89 f8             	mov    %edi,%r8d
   2a5a9:	89 d8                	mov    %ebx,%eax
   2a5ab:	44 8b 74 24 20       	mov    0x20(%rsp),%r14d
   2a5b0:	41 89 f5             	mov    %esi,%r13d
   2a5b3:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
   2a5b8:	8b 5c 24 18          	mov    0x18(%rsp),%ebx
   2a5bc:	45 01 fd             	add    %r15d,%r13d
   2a5bf:	90                   	nop
   2a5c0:	44 89 e1             	mov    %r12d,%ecx
   2a5c3:	89 ea                	mov    %ebp,%edx
   2a5c5:	44 89 e6             	mov    %r12d,%esi
   2a5c8:	c1 f9 0e             	sar    $0xe,%ecx
   2a5cb:	c1 fa 0e             	sar    $0xe,%edx
   2a5ce:	09 ee                	or     %ebp,%esi
   2a5d0:	78 3c                	js     2a60e <gouraud_deob_draw_triangle+0x74e>
   2a5d2:	81 fa 00 02 00 00    	cmp    $0x200,%edx
   2a5d8:	40 0f 9f c6          	setg   %sil
   2a5dc:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
   2a5e2:	41 0f 9f c1          	setg   %r9b
   2a5e6:	44 08 ce             	or     %r9b,%sil
   2a5e9:	75 23                	jne    2a60e <gouraud_deob_draw_triangle+0x74e>
   2a5eb:	39 d1                	cmp    %edx,%ecx
   2a5ed:	7e 1f                	jle    2a60e <gouraud_deob_draw_triangle+0x74e>
   2a5ef:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
   2a5f4:	44 89 fe             	mov    %r15d,%esi
   2a5f7:	89 44 24 18          	mov    %eax,0x18(%rsp)
   2a5fb:	44 89 44 24 0c       	mov    %r8d,0xc(%rsp)
   2a600:	e8 fb f7 ff ff       	call   29e00 <gouraud_deob_draw_scanline.part.0>
   2a605:	8b 44 24 18          	mov    0x18(%rsp),%eax
   2a609:	44 8b 44 24 0c       	mov    0xc(%rsp),%r8d
   2a60e:	41 81 c7 00 02 00 00 	add    $0x200,%r15d
   2a615:	44 01 f5             	add    %r14d,%ebp
   2a618:	41 01 dc             	add    %ebx,%r12d
   2a61b:	41 01 c0             	add    %eax,%r8d
   2a61e:	45 39 fd             	cmp    %r15d,%r13d
   2a621:	75 9d                	jne    2a5c0 <gouraud_deob_draw_triangle+0x700>
   2a623:	e9 8d fb ff ff       	jmp    2a1b5 <gouraud_deob_draw_triangle+0x2f5>
   2a628:	0f 1f 84 00 00 00 00 	nopl   0x0(%rax,%rax,1)
   2a62f:	00 
   2a630:	c7 44 24 18 00 00 00 	movl   $0x0,0x18(%rsp)
   2a637:	00 
   2a638:	c7 44 24 20 00 00 00 	movl   $0x0,0x20(%rsp)
   2a63f:	00 
   2a640:	c7 44 24 0c 00 00 00 	movl   $0x0,0xc(%rsp)
   2a647:	00 
   2a648:	e9 f1 f8 ff ff       	jmp    29f3e <gouraud_deob_draw_triangle+0x7e>
   2a64d:	0f 1f 00             	nopl   (%rax)
   2a650:	45 85 db             	test   %r11d,%r11d
   2a653:	0f 88 77 05 00 00    	js     2abd0 <gouraud_deob_draw_triangle+0xd10>
   2a659:	44 89 d8             	mov    %r11d,%eax
   2a65c:	8b 54 24 18          	mov    0x18(%rsp),%edx
   2a660:	41 c1 e6 0e          	shl    $0xe,%r14d
   2a664:	45 89 c7             	mov    %r8d,%r15d
   2a667:	c1 e0 09             	shl    $0x9,%eax
   2a66a:	39 54 24 0c          	cmp    %edx,0xc(%rsp)
   2a66e:	0f 9c c2             	setl   %dl
   2a671:	45 85 ed             	test   %r13d,%r13d
   2a674:	0f 88 86 07 00 00    	js     2ae00 <gouraud_deob_draw_triangle+0xf40>
   2a67a:	29 f9                	sub    %edi,%ecx
   2a67c:	44 39 df             	cmp    %r11d,%edi
   2a67f:	0f 84 ac 05 00 00    	je     2ac31 <gouraud_deob_draw_triangle+0xd71>
   2a685:	84 d2                	test   %dl,%dl
   2a687:	0f 84 9f 05 00 00    	je     2ac2c <gouraud_deob_draw_triangle+0xd6c>
   2a68d:	44 29 df             	sub    %r11d,%edi
   2a690:	41 89 fa             	mov    %edi,%r10d
   2a693:	83 ef 01             	sub    $0x1,%edi
   2a696:	0f 88 ea 00 00 00    	js     2a786 <gouraud_deob_draw_triangle+0x8c6>
   2a69c:	89 44 24 30          	mov    %eax,0x30(%rsp)
   2a6a0:	41 89 e8             	mov    %ebp,%r8d
   2a6a3:	41 c1 e2 09          	shl    $0x9,%r10d
   2a6a7:	41 89 db             	mov    %ebx,%r11d
   2a6aa:	89 7c 24 34          	mov    %edi,0x34(%rsp)
   2a6ae:	8b 5c 24 18          	mov    0x18(%rsp),%ebx
   2a6b2:	45 89 fd             	mov    %r15d,%r13d
   2a6b5:	41 01 c2             	add    %eax,%r10d
   2a6b8:	89 4c 24 38          	mov    %ecx,0x38(%rsp)
   2a6bc:	89 6c 24 2c          	mov    %ebp,0x2c(%rsp)
   2a6c0:	8b 6c 24 0c          	mov    0xc(%rsp),%ebp
   2a6c4:	44 89 74 24 28       	mov    %r14d,0x28(%rsp)
   2a6c9:	45 89 c6             	mov    %r8d,%r14d
   2a6cc:	44 89 7c 24 3c       	mov    %r15d,0x3c(%rsp)
   2a6d1:	41 89 c7             	mov    %eax,%r15d
   2a6d4:	0f 1f 40 00          	nopl   0x0(%rax)
   2a6d8:	44 89 e9             	mov    %r13d,%ecx
   2a6db:	44 89 e2             	mov    %r12d,%edx
   2a6de:	44 89 e8             	mov    %r13d,%eax
   2a6e1:	c1 f9 0e             	sar    $0xe,%ecx
   2a6e4:	c1 fa 0e             	sar    $0xe,%edx
   2a6e7:	44 09 e0             	or     %r12d,%eax
   2a6ea:	78 46                	js     2a732 <gouraud_deob_draw_triangle+0x872>
   2a6ec:	81 fa 00 02 00 00    	cmp    $0x200,%edx
   2a6f2:	40 0f 9f c7          	setg   %dil
   2a6f6:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
   2a6fc:	41 0f 9f c1          	setg   %r9b
   2a700:	44 08 cf             	or     %r9b,%dil
   2a703:	75 2d                	jne    2a732 <gouraud_deob_draw_triangle+0x872>
   2a705:	39 d1                	cmp    %edx,%ecx
   2a707:	7e 29                	jle    2a732 <gouraud_deob_draw_triangle+0x872>
   2a709:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
   2a70e:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
   2a713:	45 89 f0             	mov    %r14d,%r8d
   2a716:	44 89 fe             	mov    %r15d,%esi
   2a719:	44 89 5c 24 24       	mov    %r11d,0x24(%rsp)
   2a71e:	44 89 54 24 0c       	mov    %r10d,0xc(%rsp)
   2a723:	e8 d8 f6 ff ff       	call   29e00 <gouraud_deob_draw_scanline.part.0>
   2a728:	44 8b 5c 24 24       	mov    0x24(%rsp),%r11d
   2a72d:	44 8b 54 24 0c       	mov    0xc(%rsp),%r10d
   2a732:	41 81 c7 00 02 00 00 	add    $0x200,%r15d
   2a739:	41 01 ec             	add    %ebp,%r12d
   2a73c:	41 01 dd             	add    %ebx,%r13d
   2a73f:	45 01 de             	add    %r11d,%r14d
   2a742:	45 39 d7             	cmp    %r10d,%r15d
   2a745:	75 91                	jne    2a6d8 <gouraud_deob_draw_triangle+0x818>
   2a747:	8b 7c 24 34          	mov    0x34(%rsp),%edi
   2a74b:	8b 74 24 18          	mov    0x18(%rsp),%esi
   2a74f:	44 89 da             	mov    %r11d,%edx
   2a752:	44 89 db             	mov    %r11d,%ebx
   2a755:	44 8b 7c 24 3c       	mov    0x3c(%rsp),%r15d
   2a75a:	8b 6c 24 2c          	mov    0x2c(%rsp),%ebp
   2a75e:	0f af d7             	imul   %edi,%edx
   2a761:	8b 44 24 30          	mov    0x30(%rsp),%eax
   2a765:	44 8b 74 24 28       	mov    0x28(%rsp),%r14d
   2a76a:	41 01 f7             	add    %esi,%r15d
   2a76d:	0f af f7             	imul   %edi,%esi
   2a770:	44 01 dd             	add    %r11d,%ebp
   2a773:	c1 e7 09             	shl    $0x9,%edi
   2a776:	8b 4c 24 38          	mov    0x38(%rsp),%ecx
   2a77a:	8d 84 38 00 02 00 00 	lea    0x200(%rax,%rdi,1),%eax
   2a781:	01 d5                	add    %edx,%ebp
   2a783:	41 01 f7             	add    %esi,%r15d
   2a786:	85 c9                	test   %ecx,%ecx
   2a788:	0f 8e 27 fa ff ff    	jle    2a1b5 <gouraud_deob_draw_triangle+0x2f5>
   2a78e:	c1 e1 09             	shl    $0x9,%ecx
   2a791:	41 89 e8             	mov    %ebp,%r8d
   2a794:	41 89 c5             	mov    %eax,%r13d
   2a797:	8b 6c 24 18          	mov    0x18(%rsp),%ebp
   2a79b:	41 89 cc             	mov    %ecx,%r12d
   2a79e:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
   2a7a3:	41 01 c4             	add    %eax,%r12d
   2a7a6:	89 d8                	mov    %ebx,%eax
   2a7a8:	8b 5c 24 20          	mov    0x20(%rsp),%ebx
   2a7ac:	0f 1f 40 00          	nopl   0x0(%rax)
   2a7b0:	44 89 f9             	mov    %r15d,%ecx
   2a7b3:	44 89 f2             	mov    %r14d,%edx
   2a7b6:	44 89 fe             	mov    %r15d,%esi
   2a7b9:	c1 f9 0e             	sar    $0xe,%ecx
   2a7bc:	c1 fa 0e             	sar    $0xe,%edx
   2a7bf:	44 09 f6             	or     %r14d,%esi
   2a7c2:	78 3c                	js     2a800 <gouraud_deob_draw_triangle+0x940>
   2a7c4:	81 fa 00 02 00 00    	cmp    $0x200,%edx
   2a7ca:	40 0f 9f c6          	setg   %sil
   2a7ce:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
   2a7d4:	41 0f 9f c1          	setg   %r9b
   2a7d8:	44 08 ce             	or     %r9b,%sil
   2a7db:	75 23                	jne    2a800 <gouraud_deob_draw_triangle+0x940>
   2a7dd:	39 d1                	cmp    %edx,%ecx
   2a7df:	7e 1f                	jle    2a800 <gouraud_deob_draw_triangle+0x940>
   2a7e1:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
   2a7e6:	44 89 ee             	mov    %r13d,%esi
   2a7e9:	89 44 24 18          	mov    %eax,0x18(%rsp)
   2a7ed:	44 89 44 24 0c       	mov    %r8d,0xc(%rsp)
   2a7f2:	e8 09 f6 ff ff       	call   29e00 <gouraud_deob_draw_scanline.part.0>
   2a7f7:	8b 44 24 18          	mov    0x18(%rsp),%eax
   2a7fb:	44 8b 44 24 0c       	mov    0xc(%rsp),%r8d
   2a800:	41 81 c5 00 02 00 00 	add    $0x200,%r13d
   2a807:	41 01 de             	add    %ebx,%r14d
   2a80a:	41 01 ef             	add    %ebp,%r15d
   2a80d:	41 01 c0             	add    %eax,%r8d
   2a810:	45 39 ec             	cmp    %r13d,%r12d
   2a813:	75 9b                	jne    2a7b0 <gouraud_deob_draw_triangle+0x8f0>
   2a815:	e9 9b f9 ff ff       	jmp    2a1b5 <gouraud_deob_draw_triangle+0x2f5>
   2a81a:	66 0f 1f 44 00 00    	nopw   0x0(%rax,%rax,1)
   2a820:	45 85 d2             	test   %r10d,%r10d
   2a823:	0f 88 77 07 00 00    	js     2afa0 <gouraud_deob_draw_triangle+0x10e0>
   2a829:	45 89 d6             	mov    %r10d,%r14d
   2a82c:	45 89 cd             	mov    %r9d,%r13d
   2a82f:	41 c1 e6 09          	shl    $0x9,%r14d
   2a833:	41 c1 e0 0e          	shl    $0xe,%r8d
   2a837:	45 89 c4             	mov    %r8d,%r12d
   2a83a:	45 85 db             	test   %r11d,%r11d
   2a83d:	0f 88 bd 07 00 00    	js     2b000 <gouraud_deob_draw_triangle+0x1140>
   2a843:	89 f0                	mov    %esi,%eax
   2a845:	29 f1                	sub    %esi,%ecx
   2a847:	44 29 d0             	sub    %r10d,%eax
   2a84a:	44 8b 54 24 18       	mov    0x18(%rsp),%r10d
   2a84f:	8d 70 ff             	lea    -0x1(%rax),%esi
   2a852:	44 39 54 24 20       	cmp    %r10d,0x20(%rsp)
   2a857:	0f 8e 4d 08 00 00    	jle    2b0aa <gouraud_deob_draw_triangle+0x11ea>
   2a85d:	85 f6                	test   %esi,%esi
   2a85f:	0f 88 e8 00 00 00    	js     2a94d <gouraud_deob_draw_triangle+0xa8d>
   2a865:	44 89 44 24 28       	mov    %r8d,0x28(%rsp)
   2a86a:	45 89 f2             	mov    %r14d,%r10d
   2a86d:	45 89 ef             	mov    %r13d,%r15d
   2a870:	41 89 db             	mov    %ebx,%r11d
   2a873:	89 4c 24 2c          	mov    %ecx,0x2c(%rsp)
   2a877:	8b 5c 24 20          	mov    0x20(%rsp),%ebx
   2a87b:	41 89 f4             	mov    %esi,%r12d
   2a87e:	89 7c 24 38          	mov    %edi,0x38(%rsp)
   2a882:	44 89 6c 24 34       	mov    %r13d,0x34(%rsp)
   2a887:	44 8b 6c 24 18       	mov    0x18(%rsp),%r13d
   2a88c:	44 89 74 24 30       	mov    %r14d,0x30(%rsp)
   2a891:	41 89 fe             	mov    %edi,%r14d
   2a894:	89 74 24 3c          	mov    %esi,0x3c(%rsp)
   2a898:	44 89 d6             	mov    %r10d,%esi
   2a89b:	0f 1f 44 00 00       	nopl   0x0(%rax,%rax,1)
   2a8a0:	44 89 f9             	mov    %r15d,%ecx
   2a8a3:	89 ea                	mov    %ebp,%edx
   2a8a5:	44 89 f8             	mov    %r15d,%eax
   2a8a8:	c1 f9 0e             	sar    $0xe,%ecx
   2a8ab:	c1 fa 0e             	sar    $0xe,%edx
   2a8ae:	09 e8                	or     %ebp,%eax
   2a8b0:	78 41                	js     2a8f3 <gouraud_deob_draw_triangle+0xa33>
   2a8b2:	81 fa 00 02 00 00    	cmp    $0x200,%edx
   2a8b8:	40 0f 9f c7          	setg   %dil
   2a8bc:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
   2a8c2:	41 0f 9f c1          	setg   %r9b
   2a8c6:	44 08 cf             	or     %r9b,%dil
   2a8c9:	75 28                	jne    2a8f3 <gouraud_deob_draw_triangle+0xa33>
   2a8cb:	39 d1                	cmp    %edx,%ecx
   2a8cd:	7e 24                	jle    2a8f3 <gouraud_deob_draw_triangle+0xa33>
   2a8cf:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
   2a8d4:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
   2a8d9:	45 89 f0             	mov    %r14d,%r8d
   2a8dc:	89 74 24 18          	mov    %esi,0x18(%rsp)
   2a8e0:	44 89 5c 24 24       	mov    %r11d,0x24(%rsp)
   2a8e5:	e8 16 f5 ff ff       	call   29e00 <gouraud_deob_draw_scanline.part.0>
   2a8ea:	44 8b 5c 24 24       	mov    0x24(%rsp),%r11d
   2a8ef:	8b 74 24 18          	mov    0x18(%rsp),%esi
   2a8f3:	44 01 ed             	add    %r13d,%ebp
   2a8f6:	41 01 df             	add    %ebx,%r15d
   2a8f9:	45 01 de             	add    %r11d,%r14d
   2a8fc:	81 c6 00 02 00 00    	add    $0x200,%esi
   2a902:	41 83 ec 01          	sub    $0x1,%r12d
   2a906:	73 98                	jae    2a8a0 <gouraud_deob_draw_triangle+0x9e0>
   2a908:	8b 74 24 3c          	mov    0x3c(%rsp),%esi
   2a90c:	8b 44 24 20          	mov    0x20(%rsp),%eax
   2a910:	44 89 da             	mov    %r11d,%edx
   2a913:	44 89 db             	mov    %r11d,%ebx
   2a916:	44 8b 6c 24 34       	mov    0x34(%rsp),%r13d
   2a91b:	8b 7c 24 38          	mov    0x38(%rsp),%edi
   2a91f:	0f af d6             	imul   %esi,%edx
   2a922:	44 8b 74 24 30       	mov    0x30(%rsp),%r14d
   2a927:	44 8b 64 24 28       	mov    0x28(%rsp),%r12d
   2a92c:	41 01 c5             	add    %eax,%r13d
   2a92f:	0f af c6             	imul   %esi,%eax
   2a932:	8b 4c 24 2c          	mov    0x2c(%rsp),%ecx
   2a936:	41 01 c5             	add    %eax,%r13d
   2a939:	42 8d 04 1f          	lea    (%rdi,%r11,1),%eax
   2a93d:	8d 3c 02             	lea    (%rdx,%rax,1),%edi
   2a940:	89 f0                	mov    %esi,%eax
   2a942:	c1 e0 09             	shl    $0x9,%eax
   2a945:	45 8d b4 06 00 02 00 	lea    0x200(%r14,%rax,1),%r14d
   2a94c:	00 
   2a94d:	85 c9                	test   %ecx,%ecx
   2a94f:	0f 8e 60 f8 ff ff    	jle    2a1b5 <gouraud_deob_draw_triangle+0x2f5>
   2a955:	c1 e1 09             	shl    $0x9,%ecx
   2a958:	41 89 f8             	mov    %edi,%r8d
   2a95b:	89 d8                	mov    %ebx,%eax
   2a95d:	44 8b 7c 24 0c       	mov    0xc(%rsp),%r15d
   2a962:	89 cd                	mov    %ecx,%ebp
   2a964:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
   2a969:	8b 5c 24 20          	mov    0x20(%rsp),%ebx
   2a96d:	44 01 f5             	add    %r14d,%ebp
   2a970:	44 89 e9             	mov    %r13d,%ecx
   2a973:	44 89 e2             	mov    %r12d,%edx
   2a976:	44 89 ee             	mov    %r13d,%esi
   2a979:	c1 f9 0e             	sar    $0xe,%ecx
   2a97c:	c1 fa 0e             	sar    $0xe,%edx
   2a97f:	44 09 e6             	or     %r12d,%esi
   2a982:	78 3c                	js     2a9c0 <gouraud_deob_draw_triangle+0xb00>
   2a984:	81 fa 00 02 00 00    	cmp    $0x200,%edx
   2a98a:	40 0f 9f c6          	setg   %sil
   2a98e:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
   2a994:	41 0f 9f c1          	setg   %r9b
   2a998:	44 08 ce             	or     %r9b,%sil
   2a99b:	75 23                	jne    2a9c0 <gouraud_deob_draw_triangle+0xb00>
   2a99d:	39 d1                	cmp    %edx,%ecx
   2a99f:	7e 1f                	jle    2a9c0 <gouraud_deob_draw_triangle+0xb00>
   2a9a1:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
   2a9a6:	44 89 f6             	mov    %r14d,%esi
   2a9a9:	89 44 24 18          	mov    %eax,0x18(%rsp)
   2a9ad:	44 89 44 24 0c       	mov    %r8d,0xc(%rsp)
   2a9b2:	e8 49 f4 ff ff       	call   29e00 <gouraud_deob_draw_scanline.part.0>
   2a9b7:	8b 44 24 18          	mov    0x18(%rsp),%eax
   2a9bb:	44 8b 44 24 0c       	mov    0xc(%rsp),%r8d
   2a9c0:	41 81 c6 00 02 00 00 	add    $0x200,%r14d
   2a9c7:	45 01 fc             	add    %r15d,%r12d
   2a9ca:	41 01 dd             	add    %ebx,%r13d
   2a9cd:	41 01 c0             	add    %eax,%r8d
   2a9d0:	41 39 ee             	cmp    %ebp,%r14d
   2a9d3:	75 9b                	jne    2a970 <gouraud_deob_draw_triangle+0xab0>
   2a9d5:	e9 db f7 ff ff       	jmp    2a1b5 <gouraud_deob_draw_triangle+0x2f5>
   2a9da:	66 0f 1f 44 00 00    	nopw   0x0(%rax,%rax,1)
   2a9e0:	45 85 ed             	test   %r13d,%r13d
   2a9e3:	0f 88 27 05 00 00    	js     2af10 <gouraud_deob_draw_triangle+0x1050>
   2a9e9:	45 89 ef             	mov    %r13d,%r15d
   2a9ec:	44 89 f5             	mov    %r14d,%ebp
   2a9ef:	41 c1 e7 09          	shl    $0x9,%r15d
   2a9f3:	41 c1 e1 0e          	shl    $0xe,%r9d
   2a9f7:	45 89 cc             	mov    %r9d,%r12d
   2a9fa:	45 85 d2             	test   %r10d,%r10d
   2a9fd:	0f 88 5d 04 00 00    	js     2ae60 <gouraud_deob_draw_triangle+0xfa0>
   2aa03:	29 f1                	sub    %esi,%ecx
   2aa05:	44 29 ee             	sub    %r13d,%esi
   2aa08:	8b 54 24 20          	mov    0x20(%rsp),%edx
   2aa0c:	44 8d 46 ff          	lea    -0x1(%rsi),%r8d
   2aa10:	39 54 24 0c          	cmp    %edx,0xc(%rsp)
   2aa14:	0f 8e 86 07 00 00    	jle    2b1a0 <gouraud_deob_draw_triangle+0x12e0>
   2aa1a:	45 85 c0             	test   %r8d,%r8d
   2aa1d:	0f 88 e5 00 00 00    	js     2ab08 <gouraud_deob_draw_triangle+0xc48>
   2aa23:	44 89 4c 24 28       	mov    %r9d,0x28(%rsp)
   2aa28:	41 89 ed             	mov    %ebp,%r13d
   2aa2b:	41 89 db             	mov    %ebx,%r11d
   2aa2e:	8b 5c 24 0c          	mov    0xc(%rsp),%ebx
   2aa32:	89 4c 24 2c          	mov    %ecx,0x2c(%rsp)
   2aa36:	44 89 fe             	mov    %r15d,%esi
   2aa39:	45 89 c4             	mov    %r8d,%r12d
   2aa3c:	89 7c 24 38          	mov    %edi,0x38(%rsp)
   2aa40:	44 89 44 24 3c       	mov    %r8d,0x3c(%rsp)
   2aa45:	89 6c 24 34          	mov    %ebp,0x34(%rsp)
   2aa49:	8b 6c 24 20          	mov    0x20(%rsp),%ebp
   2aa4d:	44 89 7c 24 30       	mov    %r15d,0x30(%rsp)
   2aa52:	41 89 ff             	mov    %edi,%r15d
   2aa55:	0f 1f 00             	nopl   (%rax)
   2aa58:	44 89 e9             	mov    %r13d,%ecx
   2aa5b:	44 89 f2             	mov    %r14d,%edx
   2aa5e:	44 89 e8             	mov    %r13d,%eax
   2aa61:	c1 f9 0e             	sar    $0xe,%ecx
   2aa64:	c1 fa 0e             	sar    $0xe,%edx
   2aa67:	44 09 f0             	or     %r14d,%eax
   2aa6a:	78 41                	js     2aaad <gouraud_deob_draw_triangle+0xbed>
   2aa6c:	81 fa 00 02 00 00    	cmp    $0x200,%edx
   2aa72:	40 0f 9f c7          	setg   %dil
   2aa76:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
   2aa7c:	41 0f 9f c1          	setg   %r9b
   2aa80:	44 08 cf             	or     %r9b,%dil
   2aa83:	75 28                	jne    2aaad <gouraud_deob_draw_triangle+0xbed>
   2aa85:	39 d1                	cmp    %edx,%ecx
   2aa87:	7e 24                	jle    2aaad <gouraud_deob_draw_triangle+0xbed>
   2aa89:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
   2aa8e:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
   2aa93:	45 89 f8             	mov    %r15d,%r8d
   2aa96:	89 74 24 20          	mov    %esi,0x20(%rsp)
   2aa9a:	44 89 5c 24 24       	mov    %r11d,0x24(%rsp)
   2aa9f:	e8 5c f3 ff ff       	call   29e00 <gouraud_deob_draw_scanline.part.0>
   2aaa4:	44 8b 5c 24 24       	mov    0x24(%rsp),%r11d
   2aaa9:	8b 74 24 20          	mov    0x20(%rsp),%esi
   2aaad:	41 01 ee             	add    %ebp,%r14d
   2aab0:	41 01 dd             	add    %ebx,%r13d
   2aab3:	45 01 df             	add    %r11d,%r15d
   2aab6:	81 c6 00 02 00 00    	add    $0x200,%esi
   2aabc:	41 83 ec 01          	sub    $0x1,%r12d
   2aac0:	73 96                	jae    2aa58 <gouraud_deob_draw_triangle+0xb98>
   2aac2:	44 8b 44 24 3c       	mov    0x3c(%rsp),%r8d
   2aac7:	8b 44 24 0c          	mov    0xc(%rsp),%eax
   2aacb:	44 89 da             	mov    %r11d,%edx
   2aace:	44 89 db             	mov    %r11d,%ebx
   2aad1:	8b 6c 24 34          	mov    0x34(%rsp),%ebp
   2aad5:	8b 7c 24 38          	mov    0x38(%rsp),%edi
   2aad9:	41 0f af d0          	imul   %r8d,%edx
   2aadd:	44 8b 7c 24 30       	mov    0x30(%rsp),%r15d
   2aae2:	44 8b 64 24 28       	mov    0x28(%rsp),%r12d
   2aae7:	01 c5                	add    %eax,%ebp
   2aae9:	41 0f af c0          	imul   %r8d,%eax
   2aaed:	8b 4c 24 2c          	mov    0x2c(%rsp),%ecx
   2aaf1:	01 c5                	add    %eax,%ebp
   2aaf3:	42 8d 04 1f          	lea    (%rdi,%r11,1),%eax
   2aaf7:	8d 3c 02             	lea    (%rdx,%rax,1),%edi
   2aafa:	44 89 c0             	mov    %r8d,%eax
   2aafd:	c1 e0 09             	shl    $0x9,%eax
   2ab00:	45 8d bc 07 00 02 00 	lea    0x200(%r15,%rax,1),%r15d
   2ab07:	00 
   2ab08:	85 c9                	test   %ecx,%ecx
   2ab0a:	0f 8e a5 f6 ff ff    	jle    2a1b5 <gouraud_deob_draw_triangle+0x2f5>
   2ab10:	c1 e1 09             	shl    $0x9,%ecx
   2ab13:	41 89 f8             	mov    %edi,%r8d
   2ab16:	89 d8                	mov    %ebx,%eax
   2ab18:	44 8b 74 24 0c       	mov    0xc(%rsp),%r14d
   2ab1d:	41 89 cd             	mov    %ecx,%r13d
   2ab20:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
   2ab25:	8b 5c 24 18          	mov    0x18(%rsp),%ebx
   2ab29:	45 01 fd             	add    %r15d,%r13d
   2ab2c:	0f 1f 40 00          	nopl   0x0(%rax)
   2ab30:	89 e9                	mov    %ebp,%ecx
   2ab32:	44 89 e2             	mov    %r12d,%edx
   2ab35:	89 ee                	mov    %ebp,%esi
   2ab37:	c1 f9 0e             	sar    $0xe,%ecx
   2ab3a:	c1 fa 0e             	sar    $0xe,%edx
   2ab3d:	44 09 e6             	or     %r12d,%esi
   2ab40:	78 3c                	js     2ab7e <gouraud_deob_draw_triangle+0xcbe>
   2ab42:	81 fa 00 02 00 00    	cmp    $0x200,%edx
   2ab48:	40 0f 9f c6          	setg   %sil
   2ab4c:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
   2ab52:	41 0f 9f c1          	setg   %r9b
   2ab56:	44 08 ce             	or     %r9b,%sil
   2ab59:	75 23                	jne    2ab7e <gouraud_deob_draw_triangle+0xcbe>
   2ab5b:	39 d1                	cmp    %edx,%ecx
   2ab5d:	7e 1f                	jle    2ab7e <gouraud_deob_draw_triangle+0xcbe>
   2ab5f:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
   2ab64:	44 89 fe             	mov    %r15d,%esi
   2ab67:	89 44 24 18          	mov    %eax,0x18(%rsp)
   2ab6b:	44 89 44 24 0c       	mov    %r8d,0xc(%rsp)
   2ab70:	e8 8b f2 ff ff       	call   29e00 <gouraud_deob_draw_scanline.part.0>
   2ab75:	8b 44 24 18          	mov    0x18(%rsp),%eax
   2ab79:	44 8b 44 24 0c       	mov    0xc(%rsp),%r8d
   2ab7e:	41 81 c7 00 02 00 00 	add    $0x200,%r15d
   2ab85:	41 01 dc             	add    %ebx,%r12d
   2ab88:	44 01 f5             	add    %r14d,%ebp
   2ab8b:	41 01 c0             	add    %eax,%r8d
   2ab8e:	45 39 fd             	cmp    %r15d,%r13d
   2ab91:	75 9d                	jne    2ab30 <gouraud_deob_draw_triangle+0xc70>
   2ab93:	e9 1d f6 ff ff       	jmp    2a1b5 <gouraud_deob_draw_triangle+0x2f5>
   2ab98:	0f 1f 84 00 00 00 00 	nopl   0x0(%rax,%rax,1)
   2ab9f:	00 
   2aba0:	8b 44 24 0c          	mov    0xc(%rsp),%eax
   2aba4:	45 89 c6             	mov    %r8d,%r14d
   2aba7:	41 0f af c3          	imul   %r11d,%eax
   2abab:	41 29 c6             	sub    %eax,%r14d
   2abae:	8b 44 24 18          	mov    0x18(%rsp),%eax
   2abb2:	41 0f af c3          	imul   %r11d,%eax
   2abb6:	44 0f af db          	imul   %ebx,%r11d
   2abba:	41 29 c4             	sub    %eax,%r12d
   2abbd:	31 c0                	xor    %eax,%eax
   2abbf:	44 29 dd             	sub    %r11d,%ebp
   2abc2:	45 31 db             	xor    %r11d,%r11d
   2abc5:	e9 41 f4 ff ff       	jmp    2a00b <gouraud_deob_draw_triangle+0x14b>
   2abca:	66 0f 1f 44 00 00    	nopw   0x0(%rax,%rax,1)
   2abd0:	8b 74 24 0c          	mov    0xc(%rsp),%esi
   2abd4:	44 8b 54 24 18       	mov    0x18(%rsp),%r10d
   2abd9:	44 89 c0             	mov    %r8d,%eax
   2abdc:	45 89 c7             	mov    %r8d,%r15d
   2abdf:	41 c1 e6 0e          	shl    $0xe,%r14d
   2abe3:	89 f2                	mov    %esi,%edx
   2abe5:	41 0f af d3          	imul   %r11d,%edx
   2abe9:	29 d0                	sub    %edx,%eax
   2abeb:	44 89 d2             	mov    %r10d,%edx
   2abee:	41 0f af d3          	imul   %r11d,%edx
   2abf2:	44 0f af db          	imul   %ebx,%r11d
   2abf6:	41 29 d7             	sub    %edx,%r15d
   2abf9:	44 29 dd             	sub    %r11d,%ebp
   2abfc:	45 85 ed             	test   %r13d,%r13d
   2abff:	78 17                	js     2ac18 <gouraud_deob_draw_triangle+0xd58>
   2ac01:	44 39 d6             	cmp    %r10d,%esi
   2ac04:	41 89 c4             	mov    %eax,%r12d
   2ac07:	0f 9c c2             	setl   %dl
   2ac0a:	31 c0                	xor    %eax,%eax
   2ac0c:	45 31 db             	xor    %r11d,%r11d
   2ac0f:	e9 66 fa ff ff       	jmp    2a67a <gouraud_deob_draw_triangle+0x7ba>
   2ac14:	0f 1f 40 00          	nopl   0x0(%rax)
   2ac18:	8b 54 24 20          	mov    0x20(%rsp),%edx
   2ac1c:	41 89 c4             	mov    %eax,%r12d
   2ac1f:	45 31 db             	xor    %r11d,%r11d
   2ac22:	31 c0                	xor    %eax,%eax
   2ac24:	0f af d7             	imul   %edi,%edx
   2ac27:	41 29 d6             	sub    %edx,%r14d
   2ac2a:	31 ff                	xor    %edi,%edi
   2ac2c:	44 39 df             	cmp    %r11d,%edi
   2ac2f:	75 0e                	jne    2ac3f <gouraud_deob_draw_triangle+0xd7f>
   2ac31:	8b 54 24 18          	mov    0x18(%rsp),%edx
   2ac35:	39 54 24 20          	cmp    %edx,0x20(%rsp)
   2ac39:	0f 8f 47 fb ff ff    	jg     2a786 <gouraud_deob_draw_triangle+0x8c6>
   2ac3f:	44 29 df             	sub    %r11d,%edi
   2ac42:	89 fa                	mov    %edi,%edx
   2ac44:	83 ef 01             	sub    $0x1,%edi
   2ac47:	0f 88 e9 00 00 00    	js     2ad36 <gouraud_deob_draw_triangle+0xe76>
   2ac4d:	89 44 24 30          	mov    %eax,0x30(%rsp)
   2ac51:	41 89 e8             	mov    %ebp,%r8d
   2ac54:	c1 e2 09             	shl    $0x9,%edx
   2ac57:	41 89 db             	mov    %ebx,%r11d
   2ac5a:	89 7c 24 34          	mov    %edi,0x34(%rsp)
   2ac5e:	8b 5c 24 18          	mov    0x18(%rsp),%ebx
   2ac62:	45 89 fd             	mov    %r15d,%r13d
   2ac65:	44 8d 14 02          	lea    (%rdx,%rax,1),%r10d
   2ac69:	89 4c 24 38          	mov    %ecx,0x38(%rsp)
   2ac6d:	89 6c 24 2c          	mov    %ebp,0x2c(%rsp)
   2ac71:	8b 6c 24 0c          	mov    0xc(%rsp),%ebp
   2ac75:	44 89 74 24 28       	mov    %r14d,0x28(%rsp)
   2ac7a:	45 89 c6             	mov    %r8d,%r14d
   2ac7d:	44 89 7c 24 3c       	mov    %r15d,0x3c(%rsp)
   2ac82:	41 89 c7             	mov    %eax,%r15d
   2ac85:	0f 1f 00             	nopl   (%rax)
   2ac88:	44 89 e1             	mov    %r12d,%ecx
   2ac8b:	44 89 ea             	mov    %r13d,%edx
   2ac8e:	44 89 e0             	mov    %r12d,%eax
   2ac91:	c1 f9 0e             	sar    $0xe,%ecx
   2ac94:	c1 fa 0e             	sar    $0xe,%edx
   2ac97:	44 09 e8             	or     %r13d,%eax
   2ac9a:	78 46                	js     2ace2 <gouraud_deob_draw_triangle+0xe22>
   2ac9c:	81 fa 00 02 00 00    	cmp    $0x200,%edx
   2aca2:	40 0f 9f c7          	setg   %dil
   2aca6:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
   2acac:	41 0f 9f c1          	setg   %r9b
   2acb0:	44 08 cf             	or     %r9b,%dil
   2acb3:	75 2d                	jne    2ace2 <gouraud_deob_draw_triangle+0xe22>
   2acb5:	39 d1                	cmp    %edx,%ecx
   2acb7:	7e 29                	jle    2ace2 <gouraud_deob_draw_triangle+0xe22>
   2acb9:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
   2acbe:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
   2acc3:	45 89 f0             	mov    %r14d,%r8d
   2acc6:	44 89 fe             	mov    %r15d,%esi
   2acc9:	44 89 5c 24 24       	mov    %r11d,0x24(%rsp)
   2acce:	44 89 54 24 0c       	mov    %r10d,0xc(%rsp)
   2acd3:	e8 28 f1 ff ff       	call   29e00 <gouraud_deob_draw_scanline.part.0>
   2acd8:	44 8b 5c 24 24       	mov    0x24(%rsp),%r11d
   2acdd:	44 8b 54 24 0c       	mov    0xc(%rsp),%r10d
   2ace2:	41 81 c7 00 02 00 00 	add    $0x200,%r15d
   2ace9:	41 01 ec             	add    %ebp,%r12d
   2acec:	41 01 dd             	add    %ebx,%r13d
   2acef:	45 01 de             	add    %r11d,%r14d
   2acf2:	45 39 d7             	cmp    %r10d,%r15d
   2acf5:	75 91                	jne    2ac88 <gouraud_deob_draw_triangle+0xdc8>
   2acf7:	8b 7c 24 34          	mov    0x34(%rsp),%edi
   2acfb:	8b 74 24 18          	mov    0x18(%rsp),%esi
   2acff:	44 89 da             	mov    %r11d,%edx
   2ad02:	44 89 db             	mov    %r11d,%ebx
   2ad05:	44 8b 7c 24 3c       	mov    0x3c(%rsp),%r15d
   2ad0a:	8b 6c 24 2c          	mov    0x2c(%rsp),%ebp
   2ad0e:	0f af d7             	imul   %edi,%edx
   2ad11:	8b 44 24 30          	mov    0x30(%rsp),%eax
   2ad15:	44 8b 74 24 28       	mov    0x28(%rsp),%r14d
   2ad1a:	41 01 f7             	add    %esi,%r15d
   2ad1d:	0f af f7             	imul   %edi,%esi
   2ad20:	44 01 dd             	add    %r11d,%ebp
   2ad23:	c1 e7 09             	shl    $0x9,%edi
   2ad26:	8b 4c 24 38          	mov    0x38(%rsp),%ecx
   2ad2a:	8d 84 38 00 02 00 00 	lea    0x200(%rax,%rdi,1),%eax
   2ad31:	01 d5                	add    %edx,%ebp
   2ad33:	41 01 f7             	add    %esi,%r15d
   2ad36:	83 e9 01             	sub    $0x1,%ecx
   2ad39:	41 89 cc             	mov    %ecx,%r12d
   2ad3c:	0f 88 73 f4 ff ff    	js     2a1b5 <gouraud_deob_draw_triangle+0x2f5>
   2ad42:	41 89 e8             	mov    %ebp,%r8d
   2ad45:	44 8b 6c 24 20       	mov    0x20(%rsp),%r13d
   2ad4a:	89 dd                	mov    %ebx,%ebp
   2ad4c:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
   2ad51:	8b 5c 24 18          	mov    0x18(%rsp),%ebx
   2ad55:	0f 1f 00             	nopl   (%rax)
   2ad58:	44 89 f1             	mov    %r14d,%ecx
   2ad5b:	44 89 fa             	mov    %r15d,%edx
   2ad5e:	44 89 f6             	mov    %r14d,%esi
   2ad61:	c1 f9 0e             	sar    $0xe,%ecx
   2ad64:	c1 fa 0e             	sar    $0xe,%edx
   2ad67:	44 09 fe             	or     %r15d,%esi
   2ad6a:	78 3b                	js     2ada7 <gouraud_deob_draw_triangle+0xee7>
   2ad6c:	81 fa 00 02 00 00    	cmp    $0x200,%edx
   2ad72:	40 0f 9f c6          	setg   %sil
   2ad76:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
   2ad7c:	41 0f 9f c1          	setg   %r9b
   2ad80:	44 08 ce             	or     %r9b,%sil
   2ad83:	75 22                	jne    2ada7 <gouraud_deob_draw_triangle+0xee7>
   2ad85:	39 d1                	cmp    %edx,%ecx
   2ad87:	7e 1e                	jle    2ada7 <gouraud_deob_draw_triangle+0xee7>
   2ad89:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
   2ad8e:	89 c6                	mov    %eax,%esi
   2ad90:	44 89 44 24 18       	mov    %r8d,0x18(%rsp)
   2ad95:	89 44 24 0c          	mov    %eax,0xc(%rsp)
   2ad99:	e8 62 f0 ff ff       	call   29e00 <gouraud_deob_draw_scanline.part.0>
   2ad9e:	44 8b 44 24 18       	mov    0x18(%rsp),%r8d
   2ada3:	8b 44 24 0c          	mov    0xc(%rsp),%eax
   2ada7:	45 01 ee             	add    %r13d,%r14d
   2adaa:	41 01 df             	add    %ebx,%r15d
   2adad:	41 01 e8             	add    %ebp,%r8d
   2adb0:	05 00 02 00 00       	add    $0x200,%eax
   2adb5:	41 83 ec 01          	sub    $0x1,%r12d
   2adb9:	73 9d                	jae    2ad58 <gouraud_deob_draw_triangle+0xe98>
   2adbb:	e9 f5 f3 ff ff       	jmp    2a1b5 <gouraud_deob_draw_triangle+0x2f5>
   2adc0:	8b 74 24 20          	mov    0x20(%rsp),%esi
   2adc4:	0f af ce             	imul   %esi,%ecx
   2adc7:	41 29 cf             	sub    %ecx,%r15d
   2adca:	45 85 db             	test   %r11d,%r11d
   2adcd:	74 04                	je     2add3 <gouraud_deob_draw_triangle+0xf13>
   2adcf:	84 d2                	test   %dl,%dl
   2add1:	74 17                	je     2adea <gouraud_deob_draw_triangle+0xf2a>
   2add3:	8b 54 24 20          	mov    0x20(%rsp),%edx
   2add7:	39 54 24 0c          	cmp    %edx,0xc(%rsp)
   2addb:	0f 8e 4f 09 00 00    	jle    2b730 <gouraud_deob_draw_triangle+0x1870>
   2ade1:	45 85 db             	test   %r11d,%r11d
   2ade4:	0f 85 46 09 00 00    	jne    2b730 <gouraud_deob_draw_triangle+0x1870>
   2adea:	45 85 ed             	test   %r13d,%r13d
   2aded:	0f 8e c2 f3 ff ff    	jle    2a1b5 <gouraud_deob_draw_triangle+0x2f5>
   2adf3:	e9 37 f3 ff ff       	jmp    2a12f <gouraud_deob_draw_triangle+0x26f>
   2adf8:	0f 1f 84 00 00 00 00 	nopl   0x0(%rax,%rax,1)
   2adff:	00 
   2ae00:	8b 74 24 20          	mov    0x20(%rsp),%esi
   2ae04:	0f af f7             	imul   %edi,%esi
   2ae07:	41 29 f6             	sub    %esi,%r14d
   2ae0a:	45 85 db             	test   %r11d,%r11d
   2ae0d:	0f 84 17 fe ff ff    	je     2ac2a <gouraud_deob_draw_triangle+0xd6a>
   2ae13:	84 d2                	test   %dl,%dl
   2ae15:	0f 85 6b f9 ff ff    	jne    2a786 <gouraud_deob_draw_triangle+0x8c6>
   2ae1b:	31 ff                	xor    %edi,%edi
   2ae1d:	e9 0a fe ff ff       	jmp    2ac2c <gouraud_deob_draw_triangle+0xd6c>
   2ae22:	66 0f 1f 44 00 00    	nopw   0x0(%rax,%rax,1)
   2ae28:	8b 54 24 0c          	mov    0xc(%rsp),%edx
   2ae2c:	0f af d1             	imul   %ecx,%edx
   2ae2f:	41 29 d6             	sub    %edx,%r14d
   2ae32:	45 85 d2             	test   %r10d,%r10d
   2ae35:	74 04                	je     2ae3b <gouraud_deob_draw_triangle+0xf7b>
   2ae37:	84 c0                	test   %al,%al
   2ae39:	74 17                	je     2ae52 <gouraud_deob_draw_triangle+0xf92>
   2ae3b:	8b 54 24 18          	mov    0x18(%rsp),%edx
   2ae3f:	39 54 24 0c          	cmp    %edx,0xc(%rsp)
   2ae43:	0f 8d 1f 09 00 00    	jge    2b768 <gouraud_deob_draw_triangle+0x18a8>
   2ae49:	45 85 d2             	test   %r10d,%r10d
   2ae4c:	0f 85 16 09 00 00    	jne    2b768 <gouraud_deob_draw_triangle+0x18a8>
   2ae52:	45 85 db             	test   %r11d,%r11d
   2ae55:	0f 8e 5a f3 ff ff    	jle    2a1b5 <gouraud_deob_draw_triangle+0x2f5>
   2ae5b:	e9 3b f5 ff ff       	jmp    2a39b <gouraud_deob_draw_triangle+0x4db>
   2ae60:	8b 44 24 18          	mov    0x18(%rsp),%eax
   2ae64:	0f af c6             	imul   %esi,%eax
   2ae67:	8b 74 24 20          	mov    0x20(%rsp),%esi
   2ae6b:	41 29 c4             	sub    %eax,%r12d
   2ae6e:	39 74 24 0c          	cmp    %esi,0xc(%rsp)
   2ae72:	0f 8f 90 fc ff ff    	jg     2ab08 <gouraud_deob_draw_triangle+0xc48>
   2ae78:	85 c9                	test   %ecx,%ecx
   2ae7a:	0f 8e 35 f3 ff ff    	jle    2a1b5 <gouraud_deob_draw_triangle+0x2f5>
   2ae80:	c1 e1 09             	shl    $0x9,%ecx
   2ae83:	41 89 f8             	mov    %edi,%r8d
   2ae86:	89 d8                	mov    %ebx,%eax
   2ae88:	44 8b 74 24 0c       	mov    0xc(%rsp),%r14d
   2ae8d:	41 89 cd             	mov    %ecx,%r13d
   2ae90:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
   2ae95:	8b 5c 24 18          	mov    0x18(%rsp),%ebx
   2ae99:	45 01 fd             	add    %r15d,%r13d
   2ae9c:	0f 1f 40 00          	nopl   0x0(%rax)
   2aea0:	44 89 e1             	mov    %r12d,%ecx
   2aea3:	89 ea                	mov    %ebp,%edx
   2aea5:	44 89 e6             	mov    %r12d,%esi
   2aea8:	c1 f9 0e             	sar    $0xe,%ecx
   2aeab:	c1 fa 0e             	sar    $0xe,%edx
   2aeae:	09 ee                	or     %ebp,%esi
   2aeb0:	78 3c                	js     2aeee <gouraud_deob_draw_triangle+0x102e>
   2aeb2:	81 fa 00 02 00 00    	cmp    $0x200,%edx
   2aeb8:	40 0f 9f c6          	setg   %sil
   2aebc:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
   2aec2:	41 0f 9f c1          	setg   %r9b
   2aec6:	44 08 ce             	or     %r9b,%sil
   2aec9:	75 23                	jne    2aeee <gouraud_deob_draw_triangle+0x102e>
   2aecb:	39 d1                	cmp    %edx,%ecx
   2aecd:	7e 1f                	jle    2aeee <gouraud_deob_draw_triangle+0x102e>
   2aecf:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
   2aed4:	44 89 fe             	mov    %r15d,%esi
   2aed7:	89 44 24 18          	mov    %eax,0x18(%rsp)
   2aedb:	44 89 44 24 0c       	mov    %r8d,0xc(%rsp)
   2aee0:	e8 1b ef ff ff       	call   29e00 <gouraud_deob_draw_scanline.part.0>
   2aee5:	8b 44 24 18          	mov    0x18(%rsp),%eax
   2aee9:	44 8b 44 24 0c       	mov    0xc(%rsp),%r8d
   2aeee:	41 81 c7 00 02 00 00 	add    $0x200,%r15d
   2aef5:	41 01 dc             	add    %ebx,%r12d
   2aef8:	44 01 f5             	add    %r14d,%ebp
   2aefb:	41 01 c0             	add    %eax,%r8d
   2aefe:	45 39 fd             	cmp    %r15d,%r13d
   2af01:	75 9d                	jne    2aea0 <gouraud_deob_draw_triangle+0xfe0>
   2af03:	e9 ad f2 ff ff       	jmp    2a1b5 <gouraud_deob_draw_triangle+0x2f5>
   2af08:	0f 1f 84 00 00 00 00 	nopl   0x0(%rax,%rax,1)
   2af0f:	00 
   2af10:	8b 44 24 20          	mov    0x20(%rsp),%eax
   2af14:	8b 54 24 0c          	mov    0xc(%rsp),%edx
   2af18:	44 89 f5             	mov    %r14d,%ebp
   2af1b:	45 31 ff             	xor    %r15d,%r15d
   2af1e:	41 0f af c5          	imul   %r13d,%eax
   2af22:	41 0f af d5          	imul   %r13d,%edx
   2af26:	44 0f af eb          	imul   %ebx,%r13d
   2af2a:	41 29 c6             	sub    %eax,%r14d
   2af2d:	29 d5                	sub    %edx,%ebp
   2af2f:	44 29 ef             	sub    %r13d,%edi
   2af32:	45 31 ed             	xor    %r13d,%r13d
   2af35:	e9 b9 fa ff ff       	jmp    2a9f3 <gouraud_deob_draw_triangle+0xb33>
   2af3a:	66 0f 1f 44 00 00    	nopw   0x0(%rax,%rax,1)
   2af40:	8b 44 24 18          	mov    0x18(%rsp),%eax
   2af44:	8b 54 24 20          	mov    0x20(%rsp),%edx
   2af48:	0f af c1             	imul   %ecx,%eax
   2af4b:	41 29 c4             	sub    %eax,%r12d
   2af4e:	39 54 24 0c          	cmp    %edx,0xc(%rsp)
   2af52:	0f 8e fc 07 00 00    	jle    2b754 <gouraud_deob_draw_triangle+0x1894>
   2af58:	45 85 d2             	test   %r10d,%r10d
   2af5b:	0f 8e 54 f2 ff ff    	jle    2a1b5 <gouraud_deob_draw_triangle+0x2f5>
   2af61:	e9 3d f6 ff ff       	jmp    2a5a3 <gouraud_deob_draw_triangle+0x6e3>
   2af66:	66 2e 0f 1f 84 00 00 	cs nopw 0x0(%rax,%rax,1)
   2af6d:	00 00 00 
   2af70:	8b 44 24 20          	mov    0x20(%rsp),%eax
   2af74:	44 89 f5             	mov    %r14d,%ebp
   2af77:	45 31 ff             	xor    %r15d,%r15d
   2af7a:	41 0f af c5          	imul   %r13d,%eax
   2af7e:	29 c5                	sub    %eax,%ebp
   2af80:	8b 44 24 0c          	mov    0xc(%rsp),%eax
   2af84:	41 0f af c5          	imul   %r13d,%eax
   2af88:	44 0f af eb          	imul   %ebx,%r13d
   2af8c:	41 29 c6             	sub    %eax,%r14d
   2af8f:	44 29 ef             	sub    %r13d,%edi
   2af92:	45 31 ed             	xor    %r13d,%r13d
   2af95:	e9 f6 f4 ff ff       	jmp    2a490 <gouraud_deob_draw_triangle+0x5d0>
   2af9a:	66 0f 1f 44 00 00    	nopw   0x0(%rax,%rax,1)
   2afa0:	8b 44 24 18          	mov    0x18(%rsp),%eax
   2afa4:	8b 54 24 20          	mov    0x20(%rsp),%edx
   2afa8:	45 89 cd             	mov    %r9d,%r13d
   2afab:	45 31 f6             	xor    %r14d,%r14d
   2afae:	41 0f af c2          	imul   %r10d,%eax
   2afb2:	41 0f af d2          	imul   %r10d,%edx
   2afb6:	44 0f af d3          	imul   %ebx,%r10d
   2afba:	29 c5                	sub    %eax,%ebp
   2afbc:	41 29 d5             	sub    %edx,%r13d
   2afbf:	44 29 d7             	sub    %r10d,%edi
   2afc2:	45 31 d2             	xor    %r10d,%r10d
   2afc5:	e9 69 f8 ff ff       	jmp    2a833 <gouraud_deob_draw_triangle+0x973>
   2afca:	66 0f 1f 44 00 00    	nopw   0x0(%rax,%rax,1)
   2afd0:	8b 44 24 18          	mov    0x18(%rsp),%eax
   2afd4:	45 89 cc             	mov    %r9d,%r12d
   2afd7:	45 31 ff             	xor    %r15d,%r15d
   2afda:	41 0f af c2          	imul   %r10d,%eax
   2afde:	41 29 c4             	sub    %eax,%r12d
   2afe1:	8b 44 24 20          	mov    0x20(%rsp),%eax
   2afe5:	41 0f af c2          	imul   %r10d,%eax
   2afe9:	44 0f af d3          	imul   %ebx,%r10d
   2afed:	29 c5                	sub    %eax,%ebp
   2afef:	44 29 d7             	sub    %r10d,%edi
   2aff2:	45 31 d2             	xor    %r10d,%r10d
   2aff5:	e9 7e f2 ff ff       	jmp    2a278 <gouraud_deob_draw_triangle+0x3b8>
   2affa:	66 0f 1f 44 00 00    	nopw   0x0(%rax,%rax,1)
   2b000:	8b 44 24 0c          	mov    0xc(%rsp),%eax
   2b004:	0f af c6             	imul   %esi,%eax
   2b007:	8b 74 24 18          	mov    0x18(%rsp),%esi
   2b00b:	41 29 c4             	sub    %eax,%r12d
   2b00e:	39 74 24 20          	cmp    %esi,0x20(%rsp)
   2b012:	0f 8f 35 f9 ff ff    	jg     2a94d <gouraud_deob_draw_triangle+0xa8d>
   2b018:	85 c9                	test   %ecx,%ecx
   2b01a:	0f 8e 95 f1 ff ff    	jle    2a1b5 <gouraud_deob_draw_triangle+0x2f5>
   2b020:	c1 e1 09             	shl    $0x9,%ecx
   2b023:	41 89 f8             	mov    %edi,%r8d
   2b026:	89 d8                	mov    %ebx,%eax
   2b028:	44 8b 7c 24 0c       	mov    0xc(%rsp),%r15d
   2b02d:	89 cd                	mov    %ecx,%ebp
   2b02f:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
   2b034:	8b 5c 24 20          	mov    0x20(%rsp),%ebx
   2b038:	44 01 f5             	add    %r14d,%ebp
   2b03b:	0f 1f 44 00 00       	nopl   0x0(%rax,%rax,1)
   2b040:	44 89 e1             	mov    %r12d,%ecx
   2b043:	44 89 ea             	mov    %r13d,%edx
   2b046:	44 89 e6             	mov    %r12d,%esi
   2b049:	c1 f9 0e             	sar    $0xe,%ecx
   2b04c:	c1 fa 0e             	sar    $0xe,%edx
   2b04f:	44 09 ee             	or     %r13d,%esi
   2b052:	78 3c                	js     2b090 <gouraud_deob_draw_triangle+0x11d0>
   2b054:	81 fa 00 02 00 00    	cmp    $0x200,%edx
   2b05a:	40 0f 9f c6          	setg   %sil
   2b05e:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
   2b064:	41 0f 9f c1          	setg   %r9b
   2b068:	44 08 ce             	or     %r9b,%sil
   2b06b:	75 23                	jne    2b090 <gouraud_deob_draw_triangle+0x11d0>
   2b06d:	39 d1                	cmp    %edx,%ecx
   2b06f:	7e 1f                	jle    2b090 <gouraud_deob_draw_triangle+0x11d0>
   2b071:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
   2b076:	44 89 f6             	mov    %r14d,%esi
   2b079:	89 44 24 18          	mov    %eax,0x18(%rsp)
   2b07d:	44 89 44 24 0c       	mov    %r8d,0xc(%rsp)
   2b082:	e8 79 ed ff ff       	call   29e00 <gouraud_deob_draw_scanline.part.0>
   2b087:	8b 44 24 18          	mov    0x18(%rsp),%eax
   2b08b:	44 8b 44 24 0c       	mov    0xc(%rsp),%r8d
   2b090:	41 81 c6 00 02 00 00 	add    $0x200,%r14d
   2b097:	45 01 fc             	add    %r15d,%r12d
   2b09a:	41 01 dd             	add    %ebx,%r13d
   2b09d:	41 01 c0             	add    %eax,%r8d
   2b0a0:	44 39 f5             	cmp    %r14d,%ebp
   2b0a3:	75 9b                	jne    2b040 <gouraud_deob_draw_triangle+0x1180>
   2b0a5:	e9 0b f1 ff ff       	jmp    2a1b5 <gouraud_deob_draw_triangle+0x2f5>
   2b0aa:	85 c0                	test   %eax,%eax
   2b0ac:	0f 8e 66 ff ff ff    	jle    2b018 <gouraud_deob_draw_triangle+0x1158>
   2b0b2:	44 89 44 24 28       	mov    %r8d,0x28(%rsp)
   2b0b7:	45 89 f2             	mov    %r14d,%r10d
   2b0ba:	45 89 ef             	mov    %r13d,%r15d
   2b0bd:	41 89 db             	mov    %ebx,%r11d
   2b0c0:	89 4c 24 2c          	mov    %ecx,0x2c(%rsp)
   2b0c4:	8b 5c 24 20          	mov    0x20(%rsp),%ebx
   2b0c8:	41 89 f4             	mov    %esi,%r12d
   2b0cb:	89 7c 24 38          	mov    %edi,0x38(%rsp)
   2b0cf:	44 89 6c 24 34       	mov    %r13d,0x34(%rsp)
   2b0d4:	44 8b 6c 24 18       	mov    0x18(%rsp),%r13d
   2b0d9:	44 89 74 24 30       	mov    %r14d,0x30(%rsp)
   2b0de:	41 89 fe             	mov    %edi,%r14d
   2b0e1:	89 74 24 3c          	mov    %esi,0x3c(%rsp)
   2b0e5:	44 89 d6             	mov    %r10d,%esi
   2b0e8:	0f 1f 84 00 00 00 00 	nopl   0x0(%rax,%rax,1)
   2b0ef:	00 
   2b0f0:	89 e9                	mov    %ebp,%ecx
   2b0f2:	44 89 fa             	mov    %r15d,%edx
   2b0f5:	89 e8                	mov    %ebp,%eax
   2b0f7:	c1 f9 0e             	sar    $0xe,%ecx
   2b0fa:	c1 fa 0e             	sar    $0xe,%edx
   2b0fd:	44 09 f8             	or     %r15d,%eax
   2b100:	78 41                	js     2b143 <gouraud_deob_draw_triangle+0x1283>
   2b102:	81 fa 00 02 00 00    	cmp    $0x200,%edx
   2b108:	40 0f 9f c7          	setg   %dil
   2b10c:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
   2b112:	41 0f 9f c1          	setg   %r9b
   2b116:	44 08 cf             	or     %r9b,%dil
   2b119:	75 28                	jne    2b143 <gouraud_deob_draw_triangle+0x1283>
   2b11b:	39 d1                	cmp    %edx,%ecx
   2b11d:	7e 24                	jle    2b143 <gouraud_deob_draw_triangle+0x1283>
   2b11f:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
   2b124:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
   2b129:	45 89 f0             	mov    %r14d,%r8d
   2b12c:	89 74 24 18          	mov    %esi,0x18(%rsp)
   2b130:	44 89 5c 24 24       	mov    %r11d,0x24(%rsp)
   2b135:	e8 c6 ec ff ff       	call   29e00 <gouraud_deob_draw_scanline.part.0>
   2b13a:	44 8b 5c 24 24       	mov    0x24(%rsp),%r11d
   2b13f:	8b 74 24 18          	mov    0x18(%rsp),%esi
   2b143:	44 01 ed             	add    %r13d,%ebp
   2b146:	41 01 df             	add    %ebx,%r15d
   2b149:	45 01 de             	add    %r11d,%r14d
   2b14c:	81 c6 00 02 00 00    	add    $0x200,%esi
   2b152:	41 83 ec 01          	sub    $0x1,%r12d
   2b156:	73 98                	jae    2b0f0 <gouraud_deob_draw_triangle+0x1230>
   2b158:	8b 74 24 3c          	mov    0x3c(%rsp),%esi
   2b15c:	8b 44 24 20          	mov    0x20(%rsp),%eax
   2b160:	44 89 da             	mov    %r11d,%edx
   2b163:	44 89 db             	mov    %r11d,%ebx
   2b166:	44 8b 6c 24 34       	mov    0x34(%rsp),%r13d
   2b16b:	8b 7c 24 38          	mov    0x38(%rsp),%edi
   2b16f:	0f af d6             	imul   %esi,%edx
   2b172:	44 8b 74 24 30       	mov    0x30(%rsp),%r14d
   2b177:	44 8b 64 24 28       	mov    0x28(%rsp),%r12d
   2b17c:	41 01 c5             	add    %eax,%r13d
   2b17f:	0f af c6             	imul   %esi,%eax
   2b182:	c1 e6 09             	shl    $0x9,%esi
   2b185:	8b 4c 24 2c          	mov    0x2c(%rsp),%ecx
   2b189:	45 8d b4 36 00 02 00 	lea    0x200(%r14,%rsi,1),%r14d
   2b190:	00 
   2b191:	41 01 c5             	add    %eax,%r13d
   2b194:	42 8d 04 1f          	lea    (%rdi,%r11,1),%eax
   2b198:	8d 3c 02             	lea    (%rdx,%rax,1),%edi
   2b19b:	e9 78 fe ff ff       	jmp    2b018 <gouraud_deob_draw_triangle+0x1158>
   2b1a0:	85 f6                	test   %esi,%esi
   2b1a2:	0f 8e d0 fc ff ff    	jle    2ae78 <gouraud_deob_draw_triangle+0xfb8>
   2b1a8:	44 89 4c 24 28       	mov    %r9d,0x28(%rsp)
   2b1ad:	41 89 ed             	mov    %ebp,%r13d
   2b1b0:	41 89 db             	mov    %ebx,%r11d
   2b1b3:	8b 5c 24 0c          	mov    0xc(%rsp),%ebx
   2b1b7:	89 4c 24 2c          	mov    %ecx,0x2c(%rsp)
   2b1bb:	44 89 fe             	mov    %r15d,%esi
   2b1be:	45 89 c4             	mov    %r8d,%r12d
   2b1c1:	89 7c 24 38          	mov    %edi,0x38(%rsp)
   2b1c5:	44 89 44 24 3c       	mov    %r8d,0x3c(%rsp)
   2b1ca:	89 6c 24 34          	mov    %ebp,0x34(%rsp)
   2b1ce:	8b 6c 24 20          	mov    0x20(%rsp),%ebp
   2b1d2:	44 89 7c 24 30       	mov    %r15d,0x30(%rsp)
   2b1d7:	41 89 ff             	mov    %edi,%r15d
   2b1da:	66 0f 1f 44 00 00    	nopw   0x0(%rax,%rax,1)
   2b1e0:	44 89 f1             	mov    %r14d,%ecx
   2b1e3:	44 89 ea             	mov    %r13d,%edx
   2b1e6:	44 89 f0             	mov    %r14d,%eax
   2b1e9:	c1 f9 0e             	sar    $0xe,%ecx
   2b1ec:	c1 fa 0e             	sar    $0xe,%edx
   2b1ef:	44 09 e8             	or     %r13d,%eax
   2b1f2:	78 41                	js     2b235 <gouraud_deob_draw_triangle+0x1375>
   2b1f4:	81 fa 00 02 00 00    	cmp    $0x200,%edx
   2b1fa:	40 0f 9f c7          	setg   %dil
   2b1fe:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
   2b204:	41 0f 9f c1          	setg   %r9b
   2b208:	44 08 cf             	or     %r9b,%dil
   2b20b:	75 28                	jne    2b235 <gouraud_deob_draw_triangle+0x1375>
   2b20d:	39 d1                	cmp    %edx,%ecx
   2b20f:	7e 24                	jle    2b235 <gouraud_deob_draw_triangle+0x1375>
   2b211:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
   2b216:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
   2b21b:	45 89 f8             	mov    %r15d,%r8d
   2b21e:	89 74 24 20          	mov    %esi,0x20(%rsp)
   2b222:	44 89 5c 24 24       	mov    %r11d,0x24(%rsp)
   2b227:	e8 d4 eb ff ff       	call   29e00 <gouraud_deob_draw_scanline.part.0>
   2b22c:	44 8b 5c 24 24       	mov    0x24(%rsp),%r11d
   2b231:	8b 74 24 20          	mov    0x20(%rsp),%esi
   2b235:	41 01 ee             	add    %ebp,%r14d
   2b238:	41 01 dd             	add    %ebx,%r13d
   2b23b:	45 01 df             	add    %r11d,%r15d
   2b23e:	81 c6 00 02 00 00    	add    $0x200,%esi
   2b244:	41 83 ec 01          	sub    $0x1,%r12d
   2b248:	73 96                	jae    2b1e0 <gouraud_deob_draw_triangle+0x1320>
   2b24a:	44 8b 44 24 3c       	mov    0x3c(%rsp),%r8d
   2b24f:	8b 44 24 0c          	mov    0xc(%rsp),%eax
   2b253:	44 89 da             	mov    %r11d,%edx
   2b256:	44 89 db             	mov    %r11d,%ebx
   2b259:	8b 6c 24 34          	mov    0x34(%rsp),%ebp
   2b25d:	8b 7c 24 38          	mov    0x38(%rsp),%edi
   2b261:	41 0f af d0          	imul   %r8d,%edx
   2b265:	44 8b 7c 24 30       	mov    0x30(%rsp),%r15d
   2b26a:	44 8b 64 24 28       	mov    0x28(%rsp),%r12d
   2b26f:	01 c5                	add    %eax,%ebp
   2b271:	41 0f af c0          	imul   %r8d,%eax
   2b275:	41 c1 e0 09          	shl    $0x9,%r8d
   2b279:	8b 4c 24 2c          	mov    0x2c(%rsp),%ecx
   2b27d:	47 8d bc 07 00 02 00 	lea    0x200(%r15,%r8,1),%r15d
   2b284:	00 
   2b285:	01 c5                	add    %eax,%ebp
   2b287:	42 8d 04 1f          	lea    (%rdi,%r11,1),%eax
   2b28b:	8d 3c 02             	lea    (%rdx,%rax,1),%edi
   2b28e:	e9 e5 fb ff ff       	jmp    2ae78 <gouraud_deob_draw_triangle+0xfb8>
   2b293:	85 c0                	test   %eax,%eax
   2b295:	0f 8e de 00 00 00    	jle    2b379 <gouraud_deob_draw_triangle+0x14b9>
   2b29b:	89 7c 24 34          	mov    %edi,0x34(%rsp)
   2b29f:	45 89 fa             	mov    %r15d,%r10d
   2b2a2:	41 89 db             	mov    %ebx,%r11d
   2b2a5:	8b 5c 24 0c          	mov    0xc(%rsp),%ebx
   2b2a9:	89 4c 24 3c          	mov    %ecx,0x3c(%rsp)
   2b2ad:	41 89 ed             	mov    %ebp,%r13d
   2b2b0:	44 89 64 24 2c       	mov    %r12d,0x2c(%rsp)
   2b2b5:	44 8b 64 24 20       	mov    0x20(%rsp),%r12d
   2b2ba:	89 6c 24 28          	mov    %ebp,0x28(%rsp)
   2b2be:	89 cd                	mov    %ecx,%ebp
   2b2c0:	44 89 7c 24 30       	mov    %r15d,0x30(%rsp)
   2b2c5:	41 89 ff             	mov    %edi,%r15d
   2b2c8:	89 74 24 38          	mov    %esi,0x38(%rsp)
   2b2cc:	44 89 d6             	mov    %r10d,%esi
   2b2cf:	90                   	nop
   2b2d0:	44 89 e9             	mov    %r13d,%ecx
   2b2d3:	44 89 f2             	mov    %r14d,%edx
   2b2d6:	44 89 e8             	mov    %r13d,%eax
   2b2d9:	c1 f9 0e             	sar    $0xe,%ecx
   2b2dc:	c1 fa 0e             	sar    $0xe,%edx
   2b2df:	44 09 f0             	or     %r14d,%eax
   2b2e2:	78 41                	js     2b325 <gouraud_deob_draw_triangle+0x1465>
   2b2e4:	81 fa 00 02 00 00    	cmp    $0x200,%edx
   2b2ea:	40 0f 9f c7          	setg   %dil
   2b2ee:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
   2b2f4:	41 0f 9f c1          	setg   %r9b
   2b2f8:	44 08 cf             	or     %r9b,%dil
   2b2fb:	75 28                	jne    2b325 <gouraud_deob_draw_triangle+0x1465>
   2b2fd:	39 d1                	cmp    %edx,%ecx
   2b2ff:	7e 24                	jle    2b325 <gouraud_deob_draw_triangle+0x1465>
   2b301:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
   2b306:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
   2b30b:	45 89 f8             	mov    %r15d,%r8d
   2b30e:	89 74 24 0c          	mov    %esi,0xc(%rsp)
   2b312:	44 89 5c 24 24       	mov    %r11d,0x24(%rsp)
   2b317:	e8 e4 ea ff ff       	call   29e00 <gouraud_deob_draw_scanline.part.0>
   2b31c:	44 8b 5c 24 24       	mov    0x24(%rsp),%r11d
   2b321:	8b 74 24 0c          	mov    0xc(%rsp),%esi
   2b325:	45 01 e5             	add    %r12d,%r13d
   2b328:	41 01 de             	add    %ebx,%r14d
   2b32b:	45 01 df             	add    %r11d,%r15d
   2b32e:	81 c6 00 02 00 00    	add    $0x200,%esi
   2b334:	83 ed 01             	sub    $0x1,%ebp
   2b337:	73 97                	jae    2b2d0 <gouraud_deob_draw_triangle+0x1410>
   2b339:	8b 4c 24 3c          	mov    0x3c(%rsp),%ecx
   2b33d:	8b 44 24 20          	mov    0x20(%rsp),%eax
   2b341:	44 89 da             	mov    %r11d,%edx
   2b344:	44 89 db             	mov    %r11d,%ebx
   2b347:	8b 6c 24 28          	mov    0x28(%rsp),%ebp
   2b34b:	8b 7c 24 34          	mov    0x34(%rsp),%edi
   2b34f:	0f af d1             	imul   %ecx,%edx
   2b352:	44 8b 7c 24 30       	mov    0x30(%rsp),%r15d
   2b357:	44 8b 64 24 2c       	mov    0x2c(%rsp),%r12d
   2b35c:	01 c5                	add    %eax,%ebp
   2b35e:	0f af c1             	imul   %ecx,%eax
   2b361:	8b 74 24 38          	mov    0x38(%rsp),%esi
   2b365:	c1 e1 09             	shl    $0x9,%ecx
   2b368:	45 8d bc 0f 00 02 00 	lea    0x200(%r15,%rcx,1),%r15d
   2b36f:	00 
   2b370:	01 c5                	add    %eax,%ebp
   2b372:	42 8d 04 1f          	lea    (%rdi,%r11,1),%eax
   2b376:	8d 3c 02             	lea    (%rdx,%rax,1),%edi
   2b379:	c1 e6 09             	shl    $0x9,%esi
   2b37c:	41 89 f8             	mov    %edi,%r8d
   2b37f:	89 d8                	mov    %ebx,%eax
   2b381:	44 8b 74 24 20       	mov    0x20(%rsp),%r14d
   2b386:	41 89 f5             	mov    %esi,%r13d
   2b389:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
   2b38e:	8b 5c 24 18          	mov    0x18(%rsp),%ebx
   2b392:	45 01 fd             	add    %r15d,%r13d
   2b395:	0f 1f 00             	nopl   (%rax)
   2b398:	89 e9                	mov    %ebp,%ecx
   2b39a:	44 89 e2             	mov    %r12d,%edx
   2b39d:	89 ee                	mov    %ebp,%esi
   2b39f:	c1 f9 0e             	sar    $0xe,%ecx
   2b3a2:	c1 fa 0e             	sar    $0xe,%edx
   2b3a5:	44 09 e6             	or     %r12d,%esi
   2b3a8:	78 3c                	js     2b3e6 <gouraud_deob_draw_triangle+0x1526>
   2b3aa:	81 fa 00 02 00 00    	cmp    $0x200,%edx
   2b3b0:	40 0f 9f c6          	setg   %sil
   2b3b4:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
   2b3ba:	41 0f 9f c1          	setg   %r9b
   2b3be:	44 08 ce             	or     %r9b,%sil
   2b3c1:	75 23                	jne    2b3e6 <gouraud_deob_draw_triangle+0x1526>
   2b3c3:	39 d1                	cmp    %edx,%ecx
   2b3c5:	7e 1f                	jle    2b3e6 <gouraud_deob_draw_triangle+0x1526>
   2b3c7:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
   2b3cc:	44 89 fe             	mov    %r15d,%esi
   2b3cf:	89 44 24 18          	mov    %eax,0x18(%rsp)
   2b3d3:	44 89 44 24 0c       	mov    %r8d,0xc(%rsp)
   2b3d8:	e8 23 ea ff ff       	call   29e00 <gouraud_deob_draw_scanline.part.0>
   2b3dd:	8b 44 24 18          	mov    0x18(%rsp),%eax
   2b3e1:	44 8b 44 24 0c       	mov    0xc(%rsp),%r8d
   2b3e6:	41 81 c7 00 02 00 00 	add    $0x200,%r15d
   2b3ed:	44 01 f5             	add    %r14d,%ebp
   2b3f0:	41 01 dc             	add    %ebx,%r12d
   2b3f3:	41 01 c0             	add    %eax,%r8d
   2b3f6:	45 39 fd             	cmp    %r15d,%r13d
   2b3f9:	75 9d                	jne    2b398 <gouraud_deob_draw_triangle+0x14d8>
   2b3fb:	e9 b5 ed ff ff       	jmp    2a1b5 <gouraud_deob_draw_triangle+0x2f5>
   2b400:	41 39 cb             	cmp    %ecx,%r11d
   2b403:	0f 84 77 01 00 00    	je     2b580 <gouraud_deob_draw_triangle+0x16c0>
   2b409:	89 ca                	mov    %ecx,%edx
   2b40b:	44 29 da             	sub    %r11d,%edx
   2b40e:	89 d1                	mov    %edx,%ecx
   2b410:	83 e9 01             	sub    $0x1,%ecx
   2b413:	0f 88 7b 03 00 00    	js     2b794 <gouraud_deob_draw_triangle+0x18d4>
   2b419:	c1 e2 09             	shl    $0x9,%edx
   2b41c:	89 7c 24 28          	mov    %edi,0x28(%rsp)
   2b420:	41 89 e8             	mov    %ebp,%r8d
   2b423:	45 89 f5             	mov    %r14d,%r13d
   2b426:	89 44 24 34          	mov    %eax,0x34(%rsp)
   2b42a:	44 8d 14 02          	lea    (%rdx,%rax,1),%r10d
   2b42e:	41 89 db             	mov    %ebx,%r11d
   2b431:	89 c6                	mov    %eax,%esi
   2b433:	89 4c 24 3c          	mov    %ecx,0x3c(%rsp)
   2b437:	44 89 d3             	mov    %r10d,%ebx
   2b43a:	89 6c 24 2c          	mov    %ebp,0x2c(%rsp)
   2b43e:	8b 6c 24 0c          	mov    0xc(%rsp),%ebp
   2b442:	44 89 74 24 38       	mov    %r14d,0x38(%rsp)
   2b447:	44 8b 74 24 18       	mov    0x18(%rsp),%r14d
   2b44c:	44 89 7c 24 30       	mov    %r15d,0x30(%rsp)
   2b451:	45 89 c7             	mov    %r8d,%r15d
   2b454:	0f 1f 40 00          	nopl   0x0(%rax)
   2b458:	44 89 e9             	mov    %r13d,%ecx
   2b45b:	44 89 e2             	mov    %r12d,%edx
   2b45e:	44 89 e8             	mov    %r13d,%eax
   2b461:	c1 f9 0e             	sar    $0xe,%ecx
   2b464:	c1 fa 0e             	sar    $0xe,%edx
   2b467:	44 09 e0             	or     %r12d,%eax
   2b46a:	78 41                	js     2b4ad <gouraud_deob_draw_triangle+0x15ed>
   2b46c:	81 fa 00 02 00 00    	cmp    $0x200,%edx
   2b472:	40 0f 9f c7          	setg   %dil
   2b476:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
   2b47c:	41 0f 9f c1          	setg   %r9b
   2b480:	44 08 cf             	or     %r9b,%dil
   2b483:	75 28                	jne    2b4ad <gouraud_deob_draw_triangle+0x15ed>
   2b485:	39 d1                	cmp    %edx,%ecx
   2b487:	7e 24                	jle    2b4ad <gouraud_deob_draw_triangle+0x15ed>
   2b489:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
   2b48e:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
   2b493:	45 89 f8             	mov    %r15d,%r8d
   2b496:	89 74 24 18          	mov    %esi,0x18(%rsp)
   2b49a:	44 89 5c 24 24       	mov    %r11d,0x24(%rsp)
   2b49f:	e8 5c e9 ff ff       	call   29e00 <gouraud_deob_draw_scanline.part.0>
   2b4a4:	44 8b 5c 24 24       	mov    0x24(%rsp),%r11d
   2b4a9:	8b 74 24 18          	mov    0x18(%rsp),%esi
   2b4ad:	81 c6 00 02 00 00    	add    $0x200,%esi
   2b4b3:	41 01 ed             	add    %ebp,%r13d
   2b4b6:	45 01 f4             	add    %r14d,%r12d
   2b4b9:	45 01 df             	add    %r11d,%r15d
   2b4bc:	39 f3                	cmp    %esi,%ebx
   2b4be:	75 98                	jne    2b458 <gouraud_deob_draw_triangle+0x1598>
   2b4c0:	44 8b 6c 24 0c       	mov    0xc(%rsp),%r13d
   2b4c5:	8b 4c 24 3c          	mov    0x3c(%rsp),%ecx
   2b4c9:	44 8b 74 24 38       	mov    0x38(%rsp),%r14d
   2b4ce:	8b 6c 24 2c          	mov    0x2c(%rsp),%ebp
   2b4d2:	44 89 ea             	mov    %r13d,%edx
   2b4d5:	8b 7c 24 28          	mov    0x28(%rsp),%edi
   2b4d9:	8b 44 24 34          	mov    0x34(%rsp),%eax
   2b4dd:	0f af d1             	imul   %ecx,%edx
   2b4e0:	45 01 ee             	add    %r13d,%r14d
   2b4e3:	44 01 dd             	add    %r11d,%ebp
   2b4e6:	44 8b 7c 24 30       	mov    0x30(%rsp),%r15d
   2b4eb:	44 8d 67 ff          	lea    -0x1(%rdi),%r12d
   2b4ef:	8b 5c 24 20          	mov    0x20(%rsp),%ebx
   2b4f3:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
   2b4f8:	41 01 d6             	add    %edx,%r14d
   2b4fb:	44 89 da             	mov    %r11d,%edx
   2b4fe:	0f af d1             	imul   %ecx,%edx
   2b501:	c1 e1 09             	shl    $0x9,%ecx
   2b504:	8d 84 08 00 02 00 00 	lea    0x200(%rax,%rcx,1),%eax
   2b50b:	01 d5                	add    %edx,%ebp
   2b50d:	41 89 e8             	mov    %ebp,%r8d
   2b510:	44 89 dd             	mov    %r11d,%ebp
   2b513:	0f 1f 44 00 00       	nopl   0x0(%rax,%rax,1)
   2b518:	44 89 f1             	mov    %r14d,%ecx
   2b51b:	44 89 fa             	mov    %r15d,%edx
   2b51e:	44 89 f6             	mov    %r14d,%esi
   2b521:	c1 f9 0e             	sar    $0xe,%ecx
   2b524:	c1 fa 0e             	sar    $0xe,%edx
   2b527:	44 09 fe             	or     %r15d,%esi
   2b52a:	78 3b                	js     2b567 <gouraud_deob_draw_triangle+0x16a7>
   2b52c:	81 fa 00 02 00 00    	cmp    $0x200,%edx
   2b532:	40 0f 9f c6          	setg   %sil
   2b536:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
   2b53c:	41 0f 9f c1          	setg   %r9b
   2b540:	44 08 ce             	or     %r9b,%sil
   2b543:	75 22                	jne    2b567 <gouraud_deob_draw_triangle+0x16a7>
   2b545:	39 d1                	cmp    %edx,%ecx
   2b547:	7e 1e                	jle    2b567 <gouraud_deob_draw_triangle+0x16a7>
   2b549:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
   2b54e:	89 c6                	mov    %eax,%esi
   2b550:	44 89 44 24 18       	mov    %r8d,0x18(%rsp)
   2b555:	89 44 24 0c          	mov    %eax,0xc(%rsp)
   2b559:	e8 a2 e8 ff ff       	call   29e00 <gouraud_deob_draw_scanline.part.0>
   2b55e:	44 8b 44 24 18       	mov    0x18(%rsp),%r8d
   2b563:	8b 44 24 0c          	mov    0xc(%rsp),%eax
   2b567:	45 01 ee             	add    %r13d,%r14d
   2b56a:	41 01 df             	add    %ebx,%r15d
   2b56d:	41 01 e8             	add    %ebp,%r8d
   2b570:	05 00 02 00 00       	add    $0x200,%eax
   2b575:	41 83 ec 01          	sub    $0x1,%r12d
   2b579:	73 9d                	jae    2b518 <gouraud_deob_draw_triangle+0x1658>
   2b57b:	e9 35 ec ff ff       	jmp    2a1b5 <gouraud_deob_draw_triangle+0x2f5>
   2b580:	8b 54 24 20          	mov    0x20(%rsp),%edx
   2b584:	39 54 24 0c          	cmp    %edx,0xc(%rsp)
   2b588:	0f 8e 7b fe ff ff    	jle    2b409 <gouraud_deob_draw_triangle+0x1549>
   2b58e:	e9 9c eb ff ff       	jmp    2a12f <gouraud_deob_draw_triangle+0x26f>
   2b593:	0f 1f 44 00 00       	nopl   0x0(%rax,%rax,1)
   2b598:	41 39 ca             	cmp    %ecx,%r10d
   2b59b:	0f 84 76 01 00 00    	je     2b717 <gouraud_deob_draw_triangle+0x1857>
   2b5a1:	89 c8                	mov    %ecx,%eax
   2b5a3:	44 29 d0             	sub    %r10d,%eax
   2b5a6:	89 c1                	mov    %eax,%ecx
   2b5a8:	83 e9 01             	sub    $0x1,%ecx
   2b5ab:	0f 88 de 01 00 00    	js     2b78f <gouraud_deob_draw_triangle+0x18cf>
   2b5b1:	44 89 7c 24 30       	mov    %r15d,0x30(%rsp)
   2b5b6:	c1 e0 09             	shl    $0x9,%eax
   2b5b9:	45 89 e5             	mov    %r12d,%r13d
   2b5bc:	41 89 da             	mov    %ebx,%r10d
   2b5bf:	89 4c 24 34          	mov    %ecx,0x34(%rsp)
   2b5c3:	8b 5c 24 18          	mov    0x18(%rsp),%ebx
   2b5c7:	46 8d 1c 38          	lea    (%rax,%r15,1),%r11d
   2b5cb:	89 7c 24 38          	mov    %edi,0x38(%rsp)
   2b5cf:	89 74 24 3c          	mov    %esi,0x3c(%rsp)
   2b5d3:	44 89 64 24 2c       	mov    %r12d,0x2c(%rsp)
   2b5d8:	44 8b 64 24 20       	mov    0x20(%rsp),%r12d
   2b5dd:	44 89 74 24 28       	mov    %r14d,0x28(%rsp)
   2b5e2:	41 89 fe             	mov    %edi,%r14d
   2b5e5:	0f 1f 00             	nopl   (%rax)
   2b5e8:	44 89 e9             	mov    %r13d,%ecx
   2b5eb:	89 ea                	mov    %ebp,%edx
   2b5ed:	44 89 e8             	mov    %r13d,%eax
   2b5f0:	c1 f9 0e             	sar    $0xe,%ecx
   2b5f3:	c1 fa 0e             	sar    $0xe,%edx
   2b5f6:	09 e8                	or     %ebp,%eax
   2b5f8:	78 46                	js     2b640 <gouraud_deob_draw_triangle+0x1780>
   2b5fa:	81 fa 00 02 00 00    	cmp    $0x200,%edx
   2b600:	40 0f 9f c7          	setg   %dil
   2b604:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
   2b60a:	41 0f 9f c1          	setg   %r9b
   2b60e:	44 08 cf             	or     %r9b,%dil
   2b611:	75 2d                	jne    2b640 <gouraud_deob_draw_triangle+0x1780>
   2b613:	39 d1                	cmp    %edx,%ecx
   2b615:	7e 29                	jle    2b640 <gouraud_deob_draw_triangle+0x1780>
   2b617:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
   2b61c:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
   2b621:	45 89 f0             	mov    %r14d,%r8d
   2b624:	44 89 fe             	mov    %r15d,%esi
   2b627:	44 89 54 24 24       	mov    %r10d,0x24(%rsp)
   2b62c:	44 89 5c 24 20       	mov    %r11d,0x20(%rsp)
   2b631:	e8 ca e7 ff ff       	call   29e00 <gouraud_deob_draw_scanline.part.0>
   2b636:	44 8b 54 24 24       	mov    0x24(%rsp),%r10d
   2b63b:	44 8b 5c 24 20       	mov    0x20(%rsp),%r11d
   2b640:	41 81 c7 00 02 00 00 	add    $0x200,%r15d
   2b647:	41 01 dd             	add    %ebx,%r13d
   2b64a:	44 01 e5             	add    %r12d,%ebp
   2b64d:	45 01 d6             	add    %r10d,%r14d
   2b650:	45 39 df             	cmp    %r11d,%r15d
   2b653:	75 93                	jne    2b5e8 <gouraud_deob_draw_triangle+0x1728>
   2b655:	8b 4c 24 34          	mov    0x34(%rsp),%ecx
   2b659:	44 89 d2             	mov    %r10d,%edx
   2b65c:	8b 7c 24 38          	mov    0x38(%rsp),%edi
   2b660:	44 8b 64 24 2c       	mov    0x2c(%rsp),%r12d
   2b665:	44 8b 7c 24 30       	mov    0x30(%rsp),%r15d
   2b66a:	0f af d1             	imul   %ecx,%edx
   2b66d:	42 8d 04 17          	lea    (%rdi,%r10,1),%eax
   2b671:	8b 74 24 3c          	mov    0x3c(%rsp),%esi
   2b675:	44 8b 74 24 28       	mov    0x28(%rsp),%r14d
   2b67a:	44 8b 6c 24 0c       	mov    0xc(%rsp),%r13d
   2b67f:	8d 6e ff             	lea    -0x1(%rsi),%ebp
   2b682:	8d 3c 02             	lea    (%rdx,%rax,1),%edi
   2b685:	8b 54 24 18          	mov    0x18(%rsp),%edx
   2b689:	41 89 f8             	mov    %edi,%r8d
   2b68c:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
   2b691:	89 d0                	mov    %edx,%eax
   2b693:	41 01 d4             	add    %edx,%r12d
   2b696:	89 d3                	mov    %edx,%ebx
   2b698:	0f af c1             	imul   %ecx,%eax
   2b69b:	c1 e1 09             	shl    $0x9,%ecx
   2b69e:	45 8d bc 0f 00 02 00 	lea    0x200(%r15,%rcx,1),%r15d
   2b6a5:	00 
   2b6a6:	41 01 c4             	add    %eax,%r12d
   2b6a9:	44 89 f8             	mov    %r15d,%eax
   2b6ac:	45 89 d7             	mov    %r10d,%r15d
   2b6af:	90                   	nop
   2b6b0:	44 89 e1             	mov    %r12d,%ecx
   2b6b3:	44 89 f2             	mov    %r14d,%edx
   2b6b6:	44 89 e6             	mov    %r12d,%esi
   2b6b9:	c1 f9 0e             	sar    $0xe,%ecx
   2b6bc:	c1 fa 0e             	sar    $0xe,%edx
   2b6bf:	44 09 f6             	or     %r14d,%esi
   2b6c2:	78 3b                	js     2b6ff <gouraud_deob_draw_triangle+0x183f>
   2b6c4:	81 fa 00 02 00 00    	cmp    $0x200,%edx
   2b6ca:	40 0f 9f c6          	setg   %sil
   2b6ce:	81 f9 00 02 00 00    	cmp    $0x200,%ecx
   2b6d4:	41 0f 9f c1          	setg   %r9b
   2b6d8:	44 08 ce             	or     %r9b,%sil
   2b6db:	75 22                	jne    2b6ff <gouraud_deob_draw_triangle+0x183f>
   2b6dd:	39 d1                	cmp    %edx,%ecx
   2b6df:	7e 1e                	jle    2b6ff <gouraud_deob_draw_triangle+0x183f>
   2b6e1:	44 8b 4c 24 1c       	mov    0x1c(%rsp),%r9d
   2b6e6:	89 c6                	mov    %eax,%esi
   2b6e8:	44 89 44 24 18       	mov    %r8d,0x18(%rsp)
   2b6ed:	89 44 24 0c          	mov    %eax,0xc(%rsp)
   2b6f1:	e8 0a e7 ff ff       	call   29e00 <gouraud_deob_draw_scanline.part.0>
   2b6f6:	44 8b 44 24 18       	mov    0x18(%rsp),%r8d
   2b6fb:	8b 44 24 0c          	mov    0xc(%rsp),%eax
   2b6ff:	41 01 dc             	add    %ebx,%r12d
   2b702:	45 01 ee             	add    %r13d,%r14d
   2b705:	45 01 f8             	add    %r15d,%r8d
   2b708:	05 00 02 00 00       	add    $0x200,%eax
   2b70d:	83 ed 01             	sub    $0x1,%ebp
   2b710:	73 9e                	jae    2b6b0 <gouraud_deob_draw_triangle+0x17f0>
   2b712:	e9 9e ea ff ff       	jmp    2a1b5 <gouraud_deob_draw_triangle+0x2f5>
   2b717:	8b 54 24 18          	mov    0x18(%rsp),%edx
   2b71b:	39 54 24 0c          	cmp    %edx,0xc(%rsp)
   2b71f:	0f 8d 7c fe ff ff    	jge    2b5a1 <gouraud_deob_draw_triangle+0x16e1>
   2b725:	e9 71 ec ff ff       	jmp    2a39b <gouraud_deob_draw_triangle+0x4db>
   2b72a:	66 0f 1f 44 00 00    	nopw   0x0(%rax,%rax,1)
   2b730:	83 ef 01             	sub    $0x1,%edi
   2b733:	41 89 fc             	mov    %edi,%r12d
   2b736:	0f 88 79 ea ff ff    	js     2a1b5 <gouraud_deob_draw_triangle+0x2f5>
   2b73c:	41 89 e8             	mov    %ebp,%r8d
   2b73f:	44 8b 6c 24 0c       	mov    0xc(%rsp),%r13d
   2b744:	89 dd                	mov    %ebx,%ebp
   2b746:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
   2b74b:	8b 5c 24 20          	mov    0x20(%rsp),%ebx
   2b74f:	e9 c4 fd ff ff       	jmp    2b518 <gouraud_deob_draw_triangle+0x1658>
   2b754:	45 85 d2             	test   %r10d,%r10d
   2b757:	0f 8e 58 ea ff ff    	jle    2a1b5 <gouraud_deob_draw_triangle+0x2f5>
   2b75d:	e9 17 fc ff ff       	jmp    2b379 <gouraud_deob_draw_triangle+0x14b9>
   2b762:	66 0f 1f 44 00 00    	nopw   0x0(%rax,%rax,1)
   2b768:	83 ee 01             	sub    $0x1,%esi
   2b76b:	89 f5                	mov    %esi,%ebp
   2b76d:	0f 88 42 ea ff ff    	js     2a1b5 <gouraud_deob_draw_triangle+0x2f5>
   2b773:	44 89 f8             	mov    %r15d,%eax
   2b776:	41 89 f8             	mov    %edi,%r8d
   2b779:	41 89 df             	mov    %ebx,%r15d
   2b77c:	44 8b 6c 24 0c       	mov    0xc(%rsp),%r13d
   2b781:	48 8b 7c 24 10       	mov    0x10(%rsp),%rdi
   2b786:	8b 5c 24 18          	mov    0x18(%rsp),%ebx
   2b78a:	e9 21 ff ff ff       	jmp    2b6b0 <gouraud_deob_draw_triangle+0x17f0>
   2b78f:	8d 6e ff             	lea    -0x1(%rsi),%ebp
   2b792:	eb df                	jmp    2b773 <gouraud_deob_draw_triangle+0x18b3>
   2b794:	44 8d 67 ff          	lea    -0x1(%rdi),%r12d
   2b798:	eb a2                	jmp    2b73c <gouraud_deob_draw_triangle+0x187c>

Disassembly of section .fini:
