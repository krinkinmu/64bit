#define MB_HEADER_MAGIC 0x1BADB002
#define MB_HEADER_FLAGS (1 << 16)
#define MB_HEADER_CKSUM -(MB_HEADER_MAGIC + MB_HEADER_FLAGS)

	.section .header, "a"
	.global start32
	.extern main

mboot_header:
	.align 4
	.long MB_HEADER_MAGIC
	.long MB_HEADER_FLAGS
	.long MB_HEADER_CKSUM
	.long mboot_header
	.long text_begin
	.long bss_begin
	.long bss_end
	.long start32

	.text
	.code32
	.align 4
gdt:
        .quad 0x0000000000000000
        .quad 0x00cf9a000000ffff
        .quad 0x00cf92000000ffff
        .quad 0x00a09a0000000000
        .quad 0x00a0920000000000
gdt_ptr:
	.word (gdt_ptr - gdt - 1)
	.quad gdt

start32:
	movl $stack_top, %esp

	pushl $0
	popf

	call videomem_clrscr

	pushl $zeroing_bss
	call videomem_puts

	xorl %eax, %eax
	movl bss_begin, %edi
	movl bss_end, %ecx
	subl %edi, %ecx
	shrl $2, %ecx
	rep
	stosl

	call check_long_mode
	call setup_identity_mapping

	pushl $enter_long_mode
	call videomem_puts
	addl $4, %esp

	movl $0xC0000080, %ecx
	rdmsr
	orl $(1 << 8), %eax
	wrmsr
	
	pushl $enable_paging
	call videomem_puts
	addl $4, %esp

	movl %cr0, %eax
	orl $(1 << 31), %eax
	movl %eax, %cr0

	pushl $enable_64bit_gdt
	call videomem_puts
	addl $4, %esp

	lgdt gdt_ptr
	ljmp $0x18, $start64

	.code64
start64:
	movw $0x20, %ax
	movw %ax, %ds
	movw %ax, %es
	movw %ax, %fs
	movw %ax, %gs
	movw %ax, %ss

	call main

	movl $0xb8000, %edi
	movq $0x0720072007200720, %rax
	movl $500, %ecx
	rep stosq
1:
	jmp 1b

/**
 * Utlities:
 *   - fail function - prints messages and halts
 *   - check_long_mode function - checks weather long mode available or not
 *                                and fails if it's not available
 *   - setup_identity_mapping - setup identity mapping for lower 2Mb of physical
 *                              memory, it loads cr3 with pml4 adddress and
 *                              sets PAE bit in cr4, but doesn't enable paging.
 */

#define PTE_PRESENT (1 << 0)
#define PTE_WRITE   (1 << 1)
#define PTE_LARGE   (1 << 7)
#define CR4_PAE     (1 << 5)

	.code32
setup_identity_mapping:
	pushl $setup_mapping
	call videomem_puts
	addl $4, %esp

	lea pdtp, %eax
	orl $(PTE_PRESENT | PTE_WRITE), %eax
	movl %eax, pml4

	lea pd, %eax
	orl $(PTE_PRESENT | PTE_WRITE), %eax
	movl %eax, pdtp

	movl $(PTE_LARGE | PTE_PRESENT | PTE_WRITE), %eax
	movl %eax, pd

	movl %cr4, %eax
	orl $CR4_PAE, %eax
	movl %eax, %cr4

	lea pml4, %eax
	movl %eax, %cr3
	ret

check_long_mode:
	subl $4, %esp

	movl $checking_cpuid, (%esp)
	call videomem_puts

	pushf
	pushf
	xorl $(1 << 21), (%esp)
	popf
	pushf
	popl %eax
	xor (%esp), %eax
	popf
	test $(1 << 21), %eax
	jne 1f

	movl $cpuid_not_available, (%esp)
	call fail
1:
	movl $checking_x86_64, (%esp)
	call videomem_puts

	movl $0x80000000, %eax
	cpuid
	cmpl $0x80000001, %eax
	jnb 1f

	movl $no_extended_cpuid, (%esp)
	call fail

1:
	movl $0x80000001, %eax
	cpuid
	test $(1 << 29), %edx
	jnz 1f

	movl $x86_64_not_available, (%esp)
	call fail

1:
	addl $4, %esp
	ret


fail:
	addl $4, %esp
	call videomem_puts
halt:	jmp halt


zeroing_bss:
	.asciz "Zeroing bss...\n"
enable_64bit_gdt:
	.asciz "Enable bootstrap 64 bit GDT...\n"
checking_cpuid:
	.asciz "Checking CPUID instruction...\n"
cpuid_not_available:
	.asciz "CPUID instruction is not available!\n"
checking_x86_64:
	.asciz "Checking long mode...\n"
no_extended_cpuid:
	.asciz "There is no extended CPUID function available!\n"
x86_64_not_available:
	.asciz "Long mode is not available!\n"
setup_mapping:
	.asciz "Setup identity mapping for lower 2Mb physical addresses..."
enter_long_mode:
	.asciz "Enter long mode...\n"
enable_paging:
	.asciz "Enable paging...\n"


	.bss
	.align 0x1000
pml4:
	.space 0x1000
pdtp:
	.space 0x1000
pd:
	.space 0x1000

	.space 0x1000
stack_top:
