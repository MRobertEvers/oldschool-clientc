
build_release/model_viewer:     file format elf64-x86-64


Disassembly of section .init:

Disassembly of section .plt:

Disassembly of section .plt.got:

Disassembly of section .plt.sec:

Disassembly of section .text:

00000000000298f0 <raster_gouraud_s4>:
   298f0:	f3 0f 1e fa          	endbr64
   298f4:	41 57                	push   %r15
   298f6:	45 89 c3             	mov    %r8d,%r11d
   298f9:	41 56                	push   %r14
   298fb:	41 55                	push   %r13
   298fd:	41 54                	push   %r12
   298ff:	41 89 cc             	mov    %ecx,%r12d
   29902:	55                   	push   %rbp
   29903:	89 f5                	mov    %esi,%ebp
   29905:	44 89 ce             	mov    %r9d,%esi
   29908:	45 29 e3             	sub    %r12d,%r11d
   2990b:	53                   	push   %rbx
   2990c:	89 f3                	mov    %esi,%ebx
   2990e:	29 cb                	sub    %ecx,%ebx
   29910:	89 d8                	mov    %ebx,%eax
   29912:	48 83 ec 38          	sub    $0x38,%rsp
   29916:	44 8b 4c 24 70       	mov    0x70(%rsp),%r9d
   2991b:	8b 8c 24 80 00 00 00 	mov    0x80(%rsp),%ecx
   29922:	89 54 24 1c          	mov    %edx,0x1c(%rsp)
   29926:	44 8b 54 24 78       	mov    0x78(%rsp),%r10d
   2992b:	44 29 c9             	sub    %r9d,%ecx
   2992e:	45 29 ca             	sub    %r9d,%r10d
   29931:	89 ca                	mov    %ecx,%edx
   29933:	41 0f af d3          	imul   %r11d,%edx
   29937:	41 0f af c2          	imul   %r10d,%eax
   2993b:	29 c2                	sub    %eax,%edx
   2993d:	0f 84 eb 02 00 00    	je     29c2e <raster_gouraud_s4+0x33e>
   29943:	8b 84 24 90 00 00 00 	mov    0x90(%rsp),%eax
   2994a:	2b 84 24 88 00 00 00 	sub    0x88(%rsp),%eax
   29951:	41 89 d7             	mov    %edx,%r15d
   29954:	44 8b b4 24 98 00 00 	mov    0x98(%rsp),%r14d
   2995b:	00 
   2995c:	89 44 24 08          	mov    %eax,0x8(%rsp)
   29960:	44 2b b4 24 88 00 00 	sub    0x88(%rsp),%r14d
   29967:	00 
   29968:	44 3b 4c 24 78       	cmp    0x78(%rsp),%r9d
   2996d:	0f 8e cd 02 00 00    	jle    29c40 <raster_gouraud_s4+0x350>
   29973:	44 39 8c 24 80 00 00 	cmp    %r9d,0x80(%rsp)
   2997a:	00 
   2997b:	0f 8c 37 03 00 00    	jl     29cb8 <raster_gouraud_s4+0x3c8>
   29981:	44 29 c6             	sub    %r8d,%esi
   29984:	8b 94 24 80 00 00 00 	mov    0x80(%rsp),%edx
   2998b:	8b 84 24 80 00 00 00 	mov    0x80(%rsp),%eax
   29992:	41 89 cd             	mov    %ecx,%r13d
   29995:	89 74 24 20          	mov    %esi,0x20(%rsp)
   29999:	8b b4 24 88 00 00 00 	mov    0x88(%rsp),%esi
   299a0:	2b 44 24 78          	sub    0x78(%rsp),%eax
   299a4:	44 89 8c 24 80 00 00 	mov    %r9d,0x80(%rsp)
   299ab:	00 
   299ac:	89 b4 24 98 00 00 00 	mov    %esi,0x98(%rsp)
   299b3:	8b b4 24 90 00 00 00 	mov    0x90(%rsp),%esi
   299ba:	89 44 24 0c          	mov    %eax,0xc(%rsp)
   299be:	44 8b 4c 24 78       	mov    0x78(%rsp),%r9d
   299c3:	89 d8                	mov    %ebx,%eax
   299c5:	89 b4 24 88 00 00 00 	mov    %esi,0x88(%rsp)
   299cc:	44 89 e6             	mov    %r12d,%esi
   299cf:	45 89 c4             	mov    %r8d,%r12d
   299d2:	89 54 24 78          	mov    %edx,0x78(%rsp)
   299d6:	44 39 8c 24 80 00 00 	cmp    %r9d,0x80(%rsp)
   299dd:	00 
   299de:	0f 8d 2c 03 00 00    	jge    29d10 <raster_gouraud_s4+0x420>
   299e4:	44 89 e2             	mov    %r12d,%edx
   299e7:	45 31 c0             	xor    %r8d,%r8d
   299ea:	29 f2                	sub    %esi,%edx
   299ec:	89 54 24 18          	mov    %edx,0x18(%rsp)
   299f0:	44 89 ca             	mov    %r9d,%edx
   299f3:	2b 94 24 80 00 00 00 	sub    0x80(%rsp),%edx
   299fa:	89 54 24 14          	mov    %edx,0x14(%rsp)
   299fe:	45 85 ed             	test   %r13d,%r13d
   29a01:	7e 1c                	jle    29a1f <raster_gouraud_s4+0x12f>
   29a03:	c1 e0 08             	shl    $0x8,%eax
   29a06:	99                   	cltd
   29a07:	41 f7 fd             	idiv   %r13d
   29a0a:	41 89 c0             	mov    %eax,%r8d
   29a0d:	44 8b 6c 24 14       	mov    0x14(%rsp),%r13d
   29a12:	c7 44 24 10 00 00 00 	movl   $0x0,0x10(%rsp)
   29a19:	00 
   29a1a:	45 85 ed             	test   %r13d,%r13d
   29a1d:	7e 10                	jle    29a2f <raster_gouraud_s4+0x13f>
   29a1f:	8b 44 24 18          	mov    0x18(%rsp),%eax
   29a23:	c1 e0 08             	shl    $0x8,%eax
   29a26:	99                   	cltd
   29a27:	f7 7c 24 14          	idivl  0x14(%rsp)
   29a2b:	89 44 24 10          	mov    %eax,0x10(%rsp)
   29a2f:	8b 54 24 0c          	mov    0xc(%rsp),%edx
   29a33:	c7 44 24 18 00 00 00 	movl   $0x0,0x18(%rsp)
   29a3a:	00 
   29a3b:	85 d2                	test   %edx,%edx
   29a3d:	7e 12                	jle    29a51 <raster_gouraud_s4+0x161>
   29a3f:	8b 44 24 20          	mov    0x20(%rsp),%eax
   29a43:	41 89 d5             	mov    %edx,%r13d
   29a46:	c1 e0 08             	shl    $0x8,%eax
   29a49:	99                   	cltd
   29a4a:	41 f7 fd             	idiv   %r13d
   29a4d:	89 44 24 18          	mov    %eax,0x18(%rsp)
   29a51:	8b 44 24 08          	mov    0x8(%rsp),%eax
   29a55:	45 0f af d6          	imul   %r14d,%r10d
   29a59:	41 89 f5             	mov    %esi,%r13d
   29a5c:	41 c1 e4 08          	shl    $0x8,%r12d
   29a60:	45 0f af de          	imul   %r14d,%r11d
   29a64:	44 8b b4 24 98 00 00 	mov    0x98(%rsp),%r14d
   29a6b:	00 
   29a6c:	41 c1 e5 08          	shl    $0x8,%r13d
   29a70:	0f af c1             	imul   %ecx,%eax
   29a73:	41 c1 e6 08          	shl    $0x8,%r14d
   29a77:	44 29 d0             	sub    %r10d,%eax
   29a7a:	44 8b 94 24 80 00 00 	mov    0x80(%rsp),%r10d
   29a81:	00 
   29a82:	c1 e0 08             	shl    $0x8,%eax
   29a85:	99                   	cltd
   29a86:	41 f7 ff             	idiv   %r15d
   29a89:	8b 54 24 08          	mov    0x8(%rsp),%edx
   29a8d:	0f af d3             	imul   %ebx,%edx
   29a90:	89 44 24 0c          	mov    %eax,0xc(%rsp)
   29a94:	44 89 d8             	mov    %r11d,%eax
   29a97:	29 d0                	sub    %edx,%eax
   29a99:	c1 e0 08             	shl    $0x8,%eax
   29a9c:	99                   	cltd
   29a9d:	41 f7 ff             	idiv   %r15d
   29aa0:	89 44 24 08          	mov    %eax,0x8(%rsp)
   29aa4:	8b 44 24 0c          	mov    0xc(%rsp),%eax
   29aa8:	0f af f0             	imul   %eax,%esi
   29aab:	41 01 c6             	add    %eax,%r14d
   29aae:	41 29 f6             	sub    %esi,%r14d
   29ab1:	45 85 d2             	test   %r10d,%r10d
   29ab4:	0f 88 ee 02 00 00    	js     29da8 <raster_gouraud_s4+0x4b8>
   29aba:	8b 84 24 80 00 00 00 	mov    0x80(%rsp),%eax
   29ac1:	45 89 eb             	mov    %r13d,%r11d
   29ac4:	0f af c5             	imul   %ebp,%eax
   29ac7:	45 85 c9             	test   %r9d,%r9d
   29aca:	0f 88 b0 02 00 00    	js     29d80 <raster_gouraud_s4+0x490>
   29ad0:	8b 5c 24 1c          	mov    0x1c(%rsp),%ebx
   29ad4:	8d 53 ff             	lea    -0x1(%rbx),%edx
   29ad7:	44 39 cb             	cmp    %r9d,%ebx
   29ada:	44 0f 4e ca          	cmovle %edx,%r9d
   29ade:	44 39 8c 24 80 00 00 	cmp    %r9d,0x80(%rsp)
   29ae5:	00 
   29ae6:	0f 8d fd 02 00 00    	jge    29de9 <raster_gouraud_s4+0x4f9>
   29aec:	8b 9c 24 80 00 00 00 	mov    0x80(%rsp),%ebx
   29af3:	44 89 44 24 14       	mov    %r8d,0x14(%rsp)
   29af8:	44 89 f2             	mov    %r14d,%edx
   29afb:	45 89 ef             	mov    %r13d,%r15d
   29afe:	44 89 5c 24 20       	mov    %r11d,0x20(%rsp)
   29b03:	41 89 d5             	mov    %edx,%r13d
   29b06:	89 44 24 28          	mov    %eax,0x28(%rsp)
   29b0a:	44 89 4c 24 70       	mov    %r9d,0x70(%rsp)
   29b0f:	44 89 64 24 24       	mov    %r12d,0x24(%rsp)
   29b14:	41 89 dc             	mov    %ebx,%r12d
   29b17:	44 89 db             	mov    %r11d,%ebx
   29b1a:	44 89 74 24 2c       	mov    %r14d,0x2c(%rsp)
   29b1f:	41 89 c6             	mov    %eax,%r14d
   29b22:	66 0f 1f 44 00 00    	nopw   0x0(%rax,%rax,1)
   29b28:	48 83 ec 08          	sub    $0x8,%rsp
   29b2c:	89 d9                	mov    %ebx,%ecx
   29b2e:	45 89 f8             	mov    %r15d,%r8d
   29b31:	44 89 f6             	mov    %r14d,%esi
   29b34:	8b 44 24 14          	mov    0x14(%rsp),%eax
   29b38:	c1 f9 08             	sar    $0x8,%ecx
   29b3b:	45 89 e9             	mov    %r13d,%r9d
   29b3e:	41 c1 f8 08          	sar    $0x8,%r8d
   29b42:	89 ea                	mov    %ebp,%edx
   29b44:	41 01 ee             	add    %ebp,%r14d
   29b47:	41 83 c4 01          	add    $0x1,%r12d
   29b4b:	50                   	push   %rax
   29b4c:	e8 6f f4 ff ff       	call   28fc0 <draw_scanline_gouraud_s4>
   29b51:	8b 44 24 24          	mov    0x24(%rsp),%eax
   29b55:	01 c3                	add    %eax,%ebx
   29b57:	8b 44 24 20          	mov    0x20(%rsp),%eax
   29b5b:	41 01 c7             	add    %eax,%r15d
   29b5e:	8b 44 24 18          	mov    0x18(%rsp),%eax
   29b62:	59                   	pop    %rcx
   29b63:	5e                   	pop    %rsi
   29b64:	41 01 c5             	add    %eax,%r13d
   29b67:	44 39 64 24 70       	cmp    %r12d,0x70(%rsp)
   29b6c:	75 ba                	jne    29b28 <raster_gouraud_s4+0x238>
   29b6e:	44 89 e3             	mov    %r12d,%ebx
   29b71:	89 ee                	mov    %ebp,%esi
   29b73:	8b 44 24 28          	mov    0x28(%rsp),%eax
   29b77:	44 8b 44 24 14       	mov    0x14(%rsp),%r8d
   29b7c:	89 da                	mov    %ebx,%edx
   29b7e:	44 8b 5c 24 20       	mov    0x20(%rsp),%r11d
   29b83:	44 8b 74 24 2c       	mov    0x2c(%rsp),%r14d
   29b88:	2b 94 24 80 00 00 00 	sub    0x80(%rsp),%edx
   29b8f:	01 e8                	add    %ebp,%eax
   29b91:	44 8b 64 24 24       	mov    0x24(%rsp),%r12d
   29b96:	83 ea 01             	sub    $0x1,%edx
   29b99:	45 01 c3             	add    %r8d,%r11d
   29b9c:	0f af f2             	imul   %edx,%esi
   29b9f:	01 f0                	add    %esi,%eax
   29ba1:	89 d6                	mov    %edx,%esi
   29ba3:	41 0f af f0          	imul   %r8d,%esi
   29ba7:	41 01 f3             	add    %esi,%r11d
   29baa:	8b 74 24 08          	mov    0x8(%rsp),%esi
   29bae:	0f af d6             	imul   %esi,%edx
   29bb1:	41 01 f6             	add    %esi,%r14d
   29bb4:	41 01 d6             	add    %edx,%r14d
   29bb7:	3b 5c 24 78          	cmp    0x78(%rsp),%ebx
   29bbb:	7d 71                	jge    29c2e <raster_gouraud_s4+0x33e>
   29bbd:	8b 74 24 1c          	mov    0x1c(%rsp),%esi
   29bc1:	3b 74 24 78          	cmp    0x78(%rsp),%esi
   29bc5:	7f 0b                	jg     29bd2 <raster_gouraud_s4+0x2e2>
   29bc7:	83 ee 01             	sub    $0x1,%esi
   29bca:	89 74 24 78          	mov    %esi,0x78(%rsp)
   29bce:	39 de                	cmp    %ebx,%esi
   29bd0:	7e 5c                	jle    29c2e <raster_gouraud_s4+0x33e>
   29bd2:	44 89 44 24 10       	mov    %r8d,0x10(%rsp)
   29bd7:	45 89 f5             	mov    %r14d,%r13d
   29bda:	41 89 c7             	mov    %eax,%r15d
   29bdd:	41 89 de             	mov    %ebx,%r14d
   29be0:	44 89 db             	mov    %r11d,%ebx
   29be3:	0f 1f 44 00 00       	nopl   0x0(%rax,%rax,1)
   29be8:	48 83 ec 08          	sub    $0x8,%rsp
   29bec:	89 d9                	mov    %ebx,%ecx
   29bee:	45 89 e0             	mov    %r12d,%r8d
   29bf1:	45 89 e9             	mov    %r13d,%r9d
   29bf4:	8b 44 24 14          	mov    0x14(%rsp),%eax
   29bf8:	89 ea                	mov    %ebp,%edx
   29bfa:	44 89 fe             	mov    %r15d,%esi
   29bfd:	c1 f9 08             	sar    $0x8,%ecx
   29c00:	41 c1 f8 08          	sar    $0x8,%r8d
   29c04:	41 01 ef             	add    %ebp,%r15d
   29c07:	41 83 c6 01          	add    $0x1,%r14d
   29c0b:	50                   	push   %rax
   29c0c:	e8 af f3 ff ff       	call   28fc0 <draw_scanline_gouraud_s4>
   29c11:	8b 44 24 20          	mov    0x20(%rsp),%eax
   29c15:	01 c3                	add    %eax,%ebx
   29c17:	8b 44 24 28          	mov    0x28(%rsp),%eax
   29c1b:	41 01 c4             	add    %eax,%r12d
   29c1e:	8b 44 24 18          	mov    0x18(%rsp),%eax
   29c22:	41 01 c5             	add    %eax,%r13d
   29c25:	58                   	pop    %rax
   29c26:	5a                   	pop    %rdx
   29c27:	44 3b 74 24 78       	cmp    0x78(%rsp),%r14d
   29c2c:	75 ba                	jne    29be8 <raster_gouraud_s4+0x2f8>
   29c2e:	48 83 c4 38          	add    $0x38,%rsp
   29c32:	5b                   	pop    %rbx
   29c33:	5d                   	pop    %rbp
   29c34:	41 5c                	pop    %r12
   29c36:	41 5d                	pop    %r13
   29c38:	41 5e                	pop    %r14
   29c3a:	41 5f                	pop    %r15
   29c3c:	c3                   	ret
   29c3d:	0f 1f 00             	nopl   (%rax)
   29c40:	8b 44 24 78          	mov    0x78(%rsp),%eax
   29c44:	39 84 24 80 00 00 00 	cmp    %eax,0x80(%rsp)
   29c4b:	0f 8c 1f 01 00 00    	jl     29d70 <raster_gouraud_s4+0x480>
   29c51:	8b 94 24 80 00 00 00 	mov    0x80(%rsp),%edx
   29c58:	44 29 c6             	sub    %r8d,%esi
   29c5b:	44 89 54 24 14       	mov    %r10d,0x14(%rsp)
   29c60:	41 89 cd             	mov    %ecx,%r13d
   29c63:	8b 84 24 80 00 00 00 	mov    0x80(%rsp),%eax
   29c6a:	2b 44 24 78          	sub    0x78(%rsp),%eax
   29c6e:	89 74 24 20          	mov    %esi,0x20(%rsp)
   29c72:	44 89 8c 24 80 00 00 	mov    %r9d,0x80(%rsp)
   29c79:	00 
   29c7a:	8b b4 24 88 00 00 00 	mov    0x88(%rsp),%esi
   29c81:	44 8b 4c 24 78       	mov    0x78(%rsp),%r9d
   29c86:	44 89 5c 24 18       	mov    %r11d,0x18(%rsp)
   29c8b:	89 54 24 78          	mov    %edx,0x78(%rsp)
   29c8f:	89 44 24 0c          	mov    %eax,0xc(%rsp)
   29c93:	89 d8                	mov    %ebx,%eax
   29c95:	89 b4 24 98 00 00 00 	mov    %esi,0x98(%rsp)
   29c9c:	44 89 e6             	mov    %r12d,%esi
   29c9f:	45 89 c4             	mov    %r8d,%r12d
   29ca2:	45 31 c0             	xor    %r8d,%r8d
   29ca5:	45 85 ed             	test   %r13d,%r13d
   29ca8:	0f 8f 55 fd ff ff    	jg     29a03 <raster_gouraud_s4+0x113>
   29cae:	e9 5a fd ff ff       	jmp    29a0d <raster_gouraud_s4+0x11d>
   29cb3:	0f 1f 44 00 00       	nopl   0x0(%rax,%rax,1)
   29cb8:	44 89 e0             	mov    %r12d,%eax
   29cbb:	44 29 c0             	sub    %r8d,%eax
   29cbe:	89 44 24 20          	mov    %eax,0x20(%rsp)
   29cc2:	44 89 c8             	mov    %r9d,%eax
   29cc5:	2b 44 24 78          	sub    0x78(%rsp),%eax
   29cc9:	89 44 24 0c          	mov    %eax,0xc(%rsp)
   29ccd:	8b 84 24 90 00 00 00 	mov    0x90(%rsp),%eax
   29cd4:	89 84 24 88 00 00 00 	mov    %eax,0x88(%rsp)
   29cdb:	44 89 c8             	mov    %r9d,%eax
   29cde:	44 8b 4c 24 78       	mov    0x78(%rsp),%r9d
   29ce3:	89 44 24 78          	mov    %eax,0x78(%rsp)
   29ce7:	44 89 e0             	mov    %r12d,%eax
   29cea:	45 89 c4             	mov    %r8d,%r12d
   29ced:	41 89 c0             	mov    %eax,%r8d
   29cf0:	44 89 c0             	mov    %r8d,%eax
   29cf3:	44 8b 6c 24 78       	mov    0x78(%rsp),%r13d
   29cf8:	44 2b ac 24 80 00 00 	sub    0x80(%rsp),%r13d
   29cff:	00 
   29d00:	29 f0                	sub    %esi,%eax
   29d02:	44 39 8c 24 80 00 00 	cmp    %r9d,0x80(%rsp)
   29d09:	00 
   29d0a:	0f 8c d4 fc ff ff    	jl     299e4 <raster_gouraud_s4+0xf4>
   29d10:	89 f2                	mov    %esi,%edx
   29d12:	44 29 e2             	sub    %r12d,%edx
   29d15:	89 54 24 18          	mov    %edx,0x18(%rsp)
   29d19:	8b 94 24 80 00 00 00 	mov    0x80(%rsp),%edx
   29d20:	44 29 ca             	sub    %r9d,%edx
   29d23:	89 54 24 14          	mov    %edx,0x14(%rsp)
   29d27:	44 89 ea             	mov    %r13d,%edx
   29d2a:	44 8b 6c 24 0c       	mov    0xc(%rsp),%r13d
   29d2f:	89 54 24 0c          	mov    %edx,0xc(%rsp)
   29d33:	89 c2                	mov    %eax,%edx
   29d35:	8b 44 24 20          	mov    0x20(%rsp),%eax
   29d39:	89 54 24 20          	mov    %edx,0x20(%rsp)
   29d3d:	8b 94 24 88 00 00 00 	mov    0x88(%rsp),%edx
   29d44:	89 94 24 98 00 00 00 	mov    %edx,0x98(%rsp)
   29d4b:	8b 94 24 80 00 00 00 	mov    0x80(%rsp),%edx
   29d52:	44 89 8c 24 80 00 00 	mov    %r9d,0x80(%rsp)
   29d59:	00 
   29d5a:	41 89 d1             	mov    %edx,%r9d
   29d5d:	89 f2                	mov    %esi,%edx
   29d5f:	44 89 e6             	mov    %r12d,%esi
   29d62:	41 89 d4             	mov    %edx,%r12d
   29d65:	e9 38 ff ff ff       	jmp    29ca2 <raster_gouraud_s4+0x3b2>
   29d6a:	66 0f 1f 44 00 00    	nopw   0x0(%rax,%rax,1)
   29d70:	44 89 54 24 0c       	mov    %r10d,0xc(%rsp)
   29d75:	44 89 5c 24 20       	mov    %r11d,0x20(%rsp)
   29d7a:	e9 71 ff ff ff       	jmp    29cf0 <raster_gouraud_s4+0x400>
   29d7f:	90                   	nop
   29d80:	8b 54 24 18          	mov    0x18(%rsp),%edx
   29d84:	8b 74 24 1c          	mov    0x1c(%rsp),%esi
   29d88:	bb 01 00 00 00       	mov    $0x1,%ebx
   29d8d:	41 0f af d1          	imul   %r9d,%edx
   29d91:	41 29 d4             	sub    %edx,%r12d
   29d94:	85 f6                	test   %esi,%esi
   29d96:	0f 4e de             	cmovle %esi,%ebx
   29d99:	83 eb 01             	sub    $0x1,%ebx
   29d9c:	e9 16 fe ff ff       	jmp    29bb7 <raster_gouraud_s4+0x2c7>
   29da1:	0f 1f 80 00 00 00 00 	nopl   0x0(%rax)
   29da8:	8b 84 24 80 00 00 00 	mov    0x80(%rsp),%eax
   29daf:	45 89 eb             	mov    %r13d,%r11d
   29db2:	41 0f af c0          	imul   %r8d,%eax
   29db6:	41 29 c3             	sub    %eax,%r11d
   29db9:	8b 44 24 10          	mov    0x10(%rsp),%eax
   29dbd:	0f af 84 24 80 00 00 	imul   0x80(%rsp),%eax
   29dc4:	00 
   29dc5:	41 29 c5             	sub    %eax,%r13d
   29dc8:	8b 44 24 08          	mov    0x8(%rsp),%eax
   29dcc:	0f af 84 24 80 00 00 	imul   0x80(%rsp),%eax
   29dd3:	00 
   29dd4:	c7 84 24 80 00 00 00 	movl   $0x0,0x80(%rsp)
   29ddb:	00 00 00 00 
   29ddf:	41 29 c6             	sub    %eax,%r14d
   29de2:	31 c0                	xor    %eax,%eax
   29de4:	e9 de fc ff ff       	jmp    29ac7 <raster_gouraud_s4+0x1d7>
   29de9:	44 89 cb             	mov    %r9d,%ebx
   29dec:	e9 c6 fd ff ff       	jmp    29bb7 <raster_gouraud_s4+0x2c7>

Disassembly of section .fini:
