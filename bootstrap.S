#define MB_HEADER_MAGIC 0x1BADB002
#define MB_HEADER_FLAGS ((1 << 16) | (1 << 1))
#define MB_HEADER_CKSUM -(MB_HEADER_MAGIC + MB_HEADER_FLAGS)
#define PAGE_SIZE       0x1000

	.section .bootstrap, "ax"
	.code32
	.global start32
	.extern main

	.align 16
start32:
	jmp 1f

	.align 16
mboot_header:
	.long MB_HEADER_MAGIC
	.long MB_HEADER_FLAGS
	.long MB_HEADER_CKSUM
	.long mboot_header
	.long text_phys_begin
	.long data_phys_end
	.long bss_phys_end
	.long start32

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

	.global cmdline, mmap, mmap_len
cmdline:
	.quad 0
mmap:
	.quad 0
mmap_len:
	.quad 0

	.space 0x100
bootstrap_stack_top:

1:
	movl $bootstrap_stack_top, %esp
	pushl %eax
	call videomem_clrscr
	popl %eax
	cmpl $0x2badb002, %eax
	je 1f

	pushl $not_multiboot
	jmp fail

1:
	movl (%ebx), %edi
	test $(1 << 6), %edi
	jne 1f

	pushl $no_memory_map
	jmp fail

1:
	movl 44(%ebx), %eax
	movl %eax, mmap_len

	movl 48(%ebx), %eax
	movl %eax, mmap

	test $(1 << 2), %edi
	je 1f

	movl 16(%ebx), %eax
	movl %eax, cmdline

1:
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

/**
 * Utlities:
 *   - fail function - prints messages and halts
 *   - check_long_mode function - checks weather long mode available or not
 *                                and fails if it's not available
 *   - setup_identity_mapping - setup identity mapping for lower 1G of physical
 *                              memory, it loads cr3 with pml4 adddress and
 *                              sets PAE bit in cr4, but doesn't enable paging.
 */

	.code32

#define PTE_PRESENT (1 << 0)
#define PTE_WRITE   (1 << 1)
#define PTE_LARGE   (1 << 7)
#define CR4_PAE     (1 << 5)

setup_identity_mapping:
	pushl $setup_mapping
	call videomem_puts
	addl $4, %esp

	movl $(bss_phys_begin + PAGE_SIZE), %eax
	orl $(PTE_PRESENT | PTE_WRITE), %eax

	movl $bss_phys_begin, %edx
	movl %eax, (%edx)
	movl %eax, 4088(%edx)

	movl $(bss_phys_begin + 2 * PAGE_SIZE), %eax
	orl $(PTE_PRESENT | PTE_WRITE), %eax

	movl $(bss_phys_begin + PAGE_SIZE), %edx
	movl %eax, (%edx)
	movl %eax, 4080(%edx)

	movl $(PTE_LARGE | PTE_PRESENT | PTE_WRITE), %eax
	movl $(bss_phys_begin + 2 * PAGE_SIZE), %edx
	movl $512, %ecx
1:
	movl %eax, (%edx)
	addl $0x200000, %eax
	addl $8, %edx
	loop 1b

	movl %cr4, %eax
	orl $CR4_PAE, %eax
	movl %eax, %cr4

	movl $bss_phys_begin, %eax
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
	jmp fail
1:
	movl $checking_x86_64, (%esp)
	call videomem_puts

	movl $0x80000000, %eax
	cpuid
	cmpl $0x80000001, %eax
	jnb 1f

	movl $no_extended_cpuid, (%esp)
	jmp fail

1:
	movl $0x80000001, %eax
	cpuid
	test $(1 << 29), %edx
	jnz 1f

	movl $x86_64_not_available, (%esp)
	jmp fail

1:
	addl $4, %esp
	ret

fail:
	call videomem_puts
halt:	jmp halt


not_multiboot:
	.asciz "Not a multiboot compatible bootloader!\n";
no_memory_map:
	.asciz "No memory map provided!\n";
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
	.asciz "Setup identity mapping for lower 1G physical addresses...\n"
enter_long_mode:
	.asciz "Enter long mode...\n"
enable_paging:
	.asciz "Enable paging...\n"

	.code64
start64:
	movw $0x20, %ax
	movw %ax, %ds
	movw %ax, %es
	movw %ax, %fs
	movw %ax, %gs
	movw %ax, %ss

	movabsq $stack_top, %rax
	movq %rax, %rsp

	movabsq $bss_virt_begin, %rax
	movq $0, (%rax)
	addq $PAGE_SIZE, %rax;
	movq $0, (%rax);

	cld
	movabsq $main, %rax
	call *%rax

	cli
1:
	hlt
	jmp 1b

	.bss
	.align 0x1000
	.space 0x2000
stack_top:
