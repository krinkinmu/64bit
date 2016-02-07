	.text
	.global syscall
syscall:
	movq %rdi, %rax
	movq %rsi, %rdi
	movq %rdx, %rsi
	movq %rcx, %rdx
	movq %r8, %rcx
	movq %r9, %r8
	int $0x80
	ret
