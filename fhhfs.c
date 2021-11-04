#include "constval.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fuse.h>
#include <zlib.h>
#include <time.h>

static fhhfs_magic_head* magic_head;
static FILE* block_file;
static int fhhfs_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi)
{
    printf( "fhhfs:tfs_readdir path : %s ", path);
 
    return filler(buf, "Hello-world", NULL, 0);
}

static int fhhfs_getattr(const char* path, struct stat *stbuf)
{
    printf("fhhfs:tfs_getattr path : %s ", path);
    if(strcmp(path, "/") == 0)
        stbuf->st_mode = 0755 | S_IFDIR;
    else
        stbuf->st_mode = 0644 | S_IFREG;
    return 0;
}

static void fhhfs_umount(void* data)
{
    fseek(block_file,0,SEEK_SET);
    magic_head->dirty_mark = 0;
    fwrite(magic_head,sizeof(fhhfs_magic_head),1,block_file);
    unsigned crc_value = crc32(CRC_MAGIC_NUMBER,(byte*)magic_head,sizeof(fhhfs_magic_head));
    fwrite(&crc_value,sizeof(crc_value),1,block_file);
    fflush(block_file);
}


static struct fuse_operations tfs_ops = {
   .readdir = fhhfs_readdir,
   .getattr = fhhfs_getattr,
   .destroy = fhhfs_umount
};
void fhhfs_mount(void)
{
    //检查文件系统Magic Struct。
    magic_head = malloc(sizeof(fhhfs_magic_head));
    fseek(block_file,0,SEEK_SET);
    fread(magic_head,sizeof(fhhfs_magic_head),1,block_file);
    //如果magic_id不正确，则该块设备不是fhhfs格式。
    if(strcmp(magic_head->magic_id,"fhhfs!"))
    {
        fprintf(stderr,"Not a fhhfs block file.Stop.\n");
        exit(-1);
    }
    //通过计算CRC32，检查magic_head是否正确。
    unsigned crc_val;
    fread(&crc_val,sizeof(unsigned),1,block_file);
    if(crc_val!=crc32(CRC_MAGIC_NUMBER,(byte*)magic_head,sizeof(fhhfs_magic_head)))
    {
        fprintf(stderr,"Block file is corrupted.Run fsck.\n");
        exit(-1);
    }
    if(magic_head->dirty_mark)
    {
        fprintf(stderr,"Warning:This block file is dirty.Please unmount first next time.\n");
    }
    printf("This block file has %lld nodes with the size of %d bytes each.\n"
          ,magic_head->node_total
          ,magic_head->node_size
    );
    //读取卷标。 
    printf("The label of this partition is:%s\n",magic_head->label);

    //将Dirty Mark写入。
    fseek(block_file,0,SEEK_SET);
    magic_head->dirty_mark = 1;
    magic_head->last_mount_timestamp = time(NULL);
    fwrite(magic_head,sizeof(fhhfs_magic_head),1,block_file);
    unsigned crc_value = crc32(CRC_MAGIC_NUMBER,(byte*)magic_head,sizeof(fhhfs_magic_head));
    fwrite(&crc_value,sizeof(crc_value),1,block_file);
    fflush(block_file);
}

int main(int argc, char *argv[])
{
    int ret = 0;
    if(argc != 3) {
        fprintf(stderr,"Usage:%s block mountpoint\n",argv[0]);
        exit(-1);
    }
    block_file =  fopen(argv[1],"rb+");
    if(block_file==NULL) {
        fprintf(stderr,"%s:block %s cannot be opened.\n",argv[0],argv[1]);
        exit(-1);
    }
    fhhfs_mount();
    char* args_to_fuse[2] = {argv[0],argv[2]};
    ret = fuse_main(2, args_to_fuse, &tfs_ops, NULL);
    return ret;
}