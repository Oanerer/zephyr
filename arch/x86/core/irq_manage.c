/*
 * Copyright (c) 2010-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Interrupt support for IA-32 arch
 *
 * INTERNAL
 * The _idt_base_address symbol is used to determine the base address of the IDT.
 * (It is generated by the linker script, and doesn't correspond to an actual
 * global variable.)
 */

#include <kernel.h>
#include <arch/cpu.h>
#include <kernel_structs.h>
#include <misc/__assert.h>
#include <misc/printk.h>
#include <irq.h>

extern void _SpuriousIntHandler(void *);
extern void _SpuriousIntNoErrCodeHandler(void *);

/*
 * These 'dummy' variables are used in nanoArchInit() to force the inclusion of
 * the spurious interrupt handlers. They *must* be declared in a module other
 * than the one they are used in to get around garbage collection issues and
 * warnings issued some compilers that they aren't used. Therefore care must
 * be taken if they are to be moved. See kernel_structs.h for more information.
 */
void *_dummy_spurious_interrupt;
void *_dummy_exception_vector_stub;

/*
 * Place the addresses of the spurious interrupt handlers into the intList
 * section. The genIdt tool can then populate any unused vectors with
 * these routines.
 */
void *__attribute__((section(".spurIsr"))) MK_ISR_NAME(_SpuriousIntHandler) =
	&_SpuriousIntHandler;
void *__attribute__((section(".spurNoErrIsr")))
	MK_ISR_NAME(_SpuriousIntNoErrCodeHandler) =
		&_SpuriousIntNoErrCodeHandler;

