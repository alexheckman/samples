#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ctype.h>
#include <unistd.h>
#include <linux/loop.h>
#include <blkid/blkid.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

static int fsno;
static char* filesystems[128];

static int available_filesystems()
{
    const char* files[] = {"/etc/filesystems", "/proc/filesystems"};
    const int files_len = sizeof(files)/sizeof(char*);
    const int line_size = 255;
    char *line = (char*)malloc(sizeof(char) * line_size);

    fsno = 0;
    int i = 0;
    for (; i < files_len; i++) {
        FILE *f = fopen(files[i], "r");
        if (!f) continue;

        while(fgets(line, line_size, f)) {
            char *pos = strstr(line, "nodev");
            if(!pos) {
                //if (line[0] != '#' && line[0] != '*' && isalpha(line[0])) {
                char *l = line;
                if (l[0] != '#' && l[0] != '*') {
                    //front trim
                    while (l++)
                        if (*l == '\n') {
                            *l = 0;
                            break;
                        }
                    l = line;
                    while(isspace(*l)) l++;
                    if (isalpha(*l)) {
                        if (filesystems[fsno])
                            free(filesystems[fsno]);
                        filesystems[fsno++] = strdup(l);
                    }
                }
            }
        }

        free(line);
        fclose(f);
        break; //if we reach this point with one of the filesystems file it's enough
    }
}

static int fs_type(const char* file, const char** value)
{
    const char* attr = "TYPE";
    blkid_probe pr; //this is actually a pointer

    pr = blkid_new_probe_from_filename(file);
    if (!pr)
        return -1;

    if (blkid_do_probe(pr) == -1)
        return -1;
    if (blkid_probe_has_value(pr, attr))
        if (blkid_probe_lookup_value(pr, attr, value, NULL) == -1)
            return -1;

    blkid_free_probe(pr);
    return 0;
}

static int _mount_loop_device(const char* source, const char* target)
{
    static const char* loop_control = "/dev/loop-control";
    static const char* fstype = "iso9660";
    int loopctlfd, loopfd, fd;

    loopctlfd = open(loop_control, O_RDWR);
    if (loopctlfd == -1) {
        printf("Could not open loop control %s, reason: %s.\n", loop_control, strerror(errno));
        return -1;
    }

    int freedevno = ioctl(loopctlfd, LOOP_CTL_GET_FREE);
    if (freedevno == -1) {
        perror("ioctl");
        close(loopctlfd);
        return -1;
    }

    printf("First free loop device is %d.\n", freedevno);
    close(loopctlfd);

    char loopname[1024];
    memset(loopname, '\0', 1024);
    sprintf(loopname, "/dev/loop%d", freedevno);
    loopfd = open(loopname, O_RDWR);
    if (loopfd == -1) {
        printf("Could not open loop device %s, reason: %s.\n", loopname, strerror(errno));
        return -1;
    }

    fd = open(source, O_RDWR);
    if (fd == -1) {
        printf("Could not open file %s, reason: %s.\n", source, strerror(errno));
        goto error;
    }

    if (ioctl(loopfd, LOOP_SET_FD, fd) == -1) {
        perror("ioctl");
        goto error1;
    }
    
    close(fd);

    struct loop_info64 linfo;
    memset(&linfo, 0, sizeof(struct loop_info64));
    linfo.lo_flags = LO_FLAGS_AUTOCLEAR;
    strcpy(linfo.lo_file_name, source);

    if (ioctl(loopfd, LOOP_SET_STATUS64, &linfo) == -1) {
        perror("ioctl");
        goto error;
    }

    int ret = mount(loopname, target, fstype, 0, NULL);
    if (ret == -1) {
        if (errno == EACCES) { //try mounting RDONLY, iso is okay to be mounted RO
            if (mount(loopname, target, fstype, MS_RDONLY, NULL) == -1) {
                perror("mount");
                goto error;
            }
        } else {
            perror("mount");
            goto error;
        }
    }

    close(loopfd);
    return 0;

error1:
    close(fd);
error:
    close(loopfd);
    return -1;
}

static int _do_mount(const char* source, const char* target, const char* fstype, unsigned long mountflags)
{
    if (!strcmp(fstype, "iso9660")) {
        return _mount_loop_device(source, target);
    } else {
        int ret = mount(source, target, fstype, mountflags, NULL);
        if (ret == -1) {
            perror("mount");
            return -1;
        }
        return 0;
    }
}

static int do_mount(const char* source, const char* target, unsigned long mountflags)
{
    const char* type = NULL;
    if (fs_type(source, &type) == -1)
        return -1;

    if (!type && !filesystems[0]) {
        printf("Could not find any available filesystems.\n");
        return 0;
    }

    if (type) {
        return _do_mount(source, target, type, mountflags);
    } else {
        available_filesystems();
        int i = 0;
        for (; i < fsno; i++) {
            printf("testing %s\n", filesystems[i]);
            int ret = _do_mount(source, target, filesystems[i], mountflags);
            if (!ret)
                return 0;
        }
    }

    return -1;
}

int main(int argc, char* argv[])
{
    available_filesystems();

    if (argc != 3) {
        printf("Usage: mnt </path/to/file> </path/to/mountpoint>\n");
        return 1;
    }

    int ret = 0;
    ret = do_mount(argv[1], argv[2], 0);

    //getc(stdin);
    return ret;
}
