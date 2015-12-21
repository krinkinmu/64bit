	.code64
	.text
	.global switch_threads, start_thread
	.extern thread_entry

start_thread:
	popq %rdi
	popq %rsi
	jmp thread_entry
1:	jmp 1b

switch_threads:
	pushq %rbp
	pushq %rbx
	pushq %r12
	pushq %r13
	pushq %r14
	pushq %r15
	pushfq

	movq %rsp, (%rdi)
	movq %rsi, %rsp

	popfq
	popq %r15
	popq %r14
	popq %r13
	popq %r12
	popq %rbx
	popq %rbp

	ret
