#include <types.h>
#include <limits.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <kern/fcntl.h> // O_RDONLY
#include <vfs.h> //
#include <lib.h>
#include <syscall.h>
#include <mips/trapframe.h> //
#include <current.h>
#include <proc.h>
#include <synch.h>
#include <thread.h>
#include <addrspace.h>
#include <copyinout.h>
#include "opt-A2.h"

void enter_forked_process_for_threadfork(void *data1, unsigned long data2);

/* this implementation of sys__exit does not do anything with the exit code */
/* this needs to be fixed to get exit() and waitpid() working properly */

void sys__exit(int exitcode) {
    
    DEBUG(DB_SYSCALL,"sys_exit(): exitcode - %d\n",exitcode);
    
    
    struct addrspace *as;
    struct proc *p = curproc;
    
    
    /* for now, just include this to keep the compiler from complaining about
     an unused variable */

    
    KASSERT(curproc->p_addrspace != NULL);
    as_deactivate();
    /*
     * clear p_addrspace before calling as_destroy. Otherwise if
     * as_destroy sleeps (which is quite possible) when we
     * come back we'll be calling as_activate on a
     * half-destroyed address space. This tends to be
     * messily fatal.
     */
    as = curproc_setas(NULL);
    as_destroy(as);
    
    /* detach this thread from its process */
    /* note: curproc cannot be used after this call */
    proc_remthread(curthread);
    
    
    
#if OPT_A2
    p -> p_exitcode = _MKWAIT_EXIT(exitcode);
    p -> exitable = true;
    
    lock_acquire(p->p_waitpid_lk);
    cv_broadcast(p->p_waitpid_cv, p->p_waitpid_lk);
    lock_release(p->p_waitpid_lk);
#else
    (void)exitcode;
#endif
    
    proc_destroy(p);
    
    thread_exit();
    /* thread_exit() does not return */
    panic("sys_exit: unexpected return from thread_exit() \n");

}


/* stub handler for getpid() system call                */
int
sys_getpid(pid_t *retval)
{
    /* for now, this is just a stub that always returns a PID of 1 */
    /* you need to fix this to make it work properly */
#if OPT_A2
    KASSERT(curproc != NULL);
    spinlock_acquire(&curproc->p_lock);
    *retval = curproc->p_pid;
    spinlock_release(&curproc->p_lock);
#else
    *retval = 1;
#endif
    
    return(0);
}

/* stub handler for waitpid() system call                */
/*
 * parent must receive the child's exit code
 */
int
sys_waitpid(pid_t pid,
            userptr_t status,
            int options,
            pid_t *retval)
{
    int exitstatus;
    int result;
    
    /* this is just a stub implementation that always reports an
     exit status of 0, regardless of the actual exit status of
     the specified process.
     In fact, this will return 0 even if the specified process
     is still running, and even if it never existed in the first place.
     Fix this!
     */
    
    if (options != 0) {
        return(EINVAL);
    }
    
#if OPT_A2
    struct proc *p = curproc;
    
    if (status == NULL) {
        DEBUG(DB_SYSCALL, "Syscall waitpid error: non-existent process of pid (%d)\n" , pid);
        return EFAULT;
    }
    
    if (pid < 0 || pid > PID_MAX || proc_get_by_pid(pid) == NULL) {
        DEBUG(DB_SYSCALL, "Syscall waitpid error: non-existent process of pid (%d)\n" , pid);
        return ESRCH;
    }
    
    if (!if_procchild(p, pid))
    {
        DEBUG(DB_SYSCALL, "Syscall waitpid error: target %d not a child of process %p\n", pid, p);
        return(ECHILD);
    }
    // Error check is clear, proceed.
    
    struct proc *waitproc = proc_get_by_pid(pid);
    
    if (!waitproc->exitable) {
        lock_acquire(waitproc->p_waitpid_lk);
        while (!waitproc->exitable)
        {
            cv_wait(waitproc->p_waitpid_cv, waitproc->p_waitpid_lk);
        }
        lock_release(waitproc->p_waitpid_lk);
    }
    
    exitstatus = waitproc->p_exitcode;
#else
    /* for now, just pretend the exitstatus is 0 */
    exitstatus = 0;
#endif
    
    result = copyout((void *)&exitstatus,status,sizeof(int)); // return 0 if succeeds
    if (result) {
        return(result);
    }
    
    *retval = pid;
    return(0);
}


#if OPT_A2
int
sys_fork(struct trapframe *tf, pid_t *retval)
{
    KASSERT(curproc != NULL);
    
    if (get_proc_count() == PID_MAX) {
        return ENPROC;
    }
    
    struct proc *childproc = proc_create_runprogram(curproc->p_name);
    KASSERT (childproc->p_pid > 0);
    
    // when proc_create_runprogram returns NULL,
    if (childproc == NULL) {
        return ENOMEM;
    }

    struct addrspace *childas = kmalloc(sizeof(struct addrspace));
    if (childas == NULL) {
        proc_destroy(childproc);
        return ENOMEM;
    }
    
    int errno = as_copy(curproc->p_addrspace, &childas);
    if (errno) {
        kfree(childas);
        proc_destroy(childproc);
        return errno;
    }
    childproc->p_addrspace = childas;
    
    struct trapframe *childtf = kmalloc(sizeof(struct trapframe));
    if (childtf == NULL) {
        kfree(childtf);
        as_destroy(childas);
        proc_destroy(childproc);
        return ENOMEM;
    }

    memcpy(childtf, tf, sizeof(struct trapframe));

    childproc->p_pproc = curproc;
    
    int result = procarray_add(&curproc->p_children, childproc, NULL);
    if (result){
        kfree(childtf);
        as_destroy(childas);
        proc_destroy(childproc);
        return result;
    }

    void **data = kmalloc(2*sizeof(void *));
    data[0] = (void *)childtf;
    data[1] = (void *)childas;
    
    errno = thread_fork(curproc->p_name, childproc, &enter_forked_process_for_threadfork, data, 0);
    if (errno) {
        kfree(childtf);
        as_destroy(childas);
        proc_destroy(childproc);
        return ENOMEM;
    }
    
    *retval = childproc->p_pid;
    
    return(0);
}

void enter_forked_process_for_threadfork(void *data1, unsigned long data2) {
    (void)data2;
    struct trapframe *childtf = ((void **)data1)[0];
    struct addrspace *childas = ((void **)data1)[1];
    KASSERT(childtf != NULL);
    KASSERT(childas != NULL);
    
    /* Switch to child as and activate it. */
    curproc_setas(childas);
    as_activate();    
    
    enter_forked_process(childtf);
}
#endif

