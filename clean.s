	.file	"test_while_simple"
	.text
	.globl	main                            # -- Begin function main
	.p2align	4
	.type	main,@function
main:                                   # @main
	.cfi_startproc
# %bb.0:                                # %entry
	pushq	%rax
	.cfi_def_cfa_offset 16
	movq	$0, (%rsp)
	cmpq	$2, (%rsp)
	jg	.LBB0_3
	.p2align	4
.LBB0_2:                                # %whilebody
                                        # =>This Inner Loop Header: Depth=1
	movq	(%rsp), %rsi
	movl	$.L__unnamed_1, %edi
	xorl	%eax, %eax
	callq	printf@PLT
	incq	(%rsp)
	movl	$.L__unnamed_2, %edi
	xorl	%eax, %eax
	callq	printf@PLT
	cmpq	$2, (%rsp)
	jle	.LBB0_2
.LBB0_3:                                # %whilecont
	movl	$.L__unnamed_3, %edi
	xorl	%eax, %eax
	callq	printf@PLT
	movl	$.L__unnamed_4, %edi
	xorl	%eax, %eax
	callq	printf@PLT
	xorl	%eax, %eax
	popq	%rcx
	.cfi_def_cfa_offset 8
	retq
.Lfunc_end0:
	.size	main, .Lfunc_end0-main
	.cfi_endproc
                                        # -- End function
	.type	.L__unnamed_5,@object           # @0
	.section	.rodata.str1.1,"aMS",@progbits,1
.L__unnamed_5:
	.asciz	"__main__"
	.size	.L__unnamed_5, 9

	.type	__name__,@object                # @__name__
	.data
	.p2align	3, 0x0
__name__:
	.quad	.L__unnamed_5
	.size	__name__, 8

	.type	__arg__,@object                 # @__arg__
	.local	__arg__
	.comm	__arg__,24,16
	.type	.L__unnamed_1,@object           # @1
	.section	.rodata.str1.1,"aMS",@progbits,1
.L__unnamed_1:
	.asciz	"%lld"
	.size	.L__unnamed_1, 5

	.type	.L__unnamed_2,@object           # @2
.L__unnamed_2:
	.asciz	" next "
	.size	.L__unnamed_2, 7

	.type	.L__unnamed_3,@object           # @3
.L__unnamed_3:
	.zero	1
	.size	.L__unnamed_3, 1

	.type	.L__unnamed_4,@object           # @4
.L__unnamed_4:
	.asciz	"\n"
	.size	.L__unnamed_4, 2

	.section	".note.GNU-stack","",@progbits
