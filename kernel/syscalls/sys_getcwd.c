#include <syscall_common.h>

#include <lib/string.h>
#include <status.h>

int sys_getcwd(char *buf, size_t size)
{
    if (!buf || size == 0)
        return -EINVAL;
    const char *cwd = (current_process && current_process->cwd[0]) ? current_process->cwd : "/";

    const size_t len = strlen(cwd);
    if (len + 1 > size)
        return -EINVAL;
    if (!user_ptr_write_ok(buf, len + 1, "sys_getcwd"))
        return -EFAULT;
    memcpy(buf, cwd, len + 1);

    return ALL_OK;
}
