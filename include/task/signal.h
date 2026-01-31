#pragma once

#include <stdint.h>
#include <arch/x86_64/idt.h>
#include <sys/syscall.h>
#include <sys/signal.h>
#include <task/process.h>

void signal_init_process(process_t * proc);
void signal_reset_exec(process_t * proc);
void signal_copy_on_fork(process_t* child, const process_t* parent);
int signal_send_pid(int pid, int sig);
bool signal_deliver_after_syscall(struct syscall_regs* regs, const uint64_t* ret);
bool signal_deliver_after_interrupt(struct interrupt_frame* frame);