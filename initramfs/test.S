	.text
	.global start
start:
	xorq %rax, %rax
	int $0x80
1:	jmp 1b
