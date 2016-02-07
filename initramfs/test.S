	.text
	.global start
start:
	call dummy_function
	xorq %rax, %rax
	movq $msg, %rdi
	movq size, %rsi
	int $0x80
1:	jmp 1b

dummy_function:
	ret

msg:
	.ascii "Hello, World! From Userspace.\n"
size:
	.quad . - msg
