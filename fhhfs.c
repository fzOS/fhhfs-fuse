#include "constval.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fuse.h>
#include <zlib.h>
#include <time.h>

static fhhfs_magic_head* magic_head;
static FILE* block_file;
byte* fhhfs_read_file(unsigned long long block_id, unsigned long long count, void* process_buffer);
static int fhhfs_get_next_node(int prev_id, void* buffer)
{
    //首先，查找node分配表.
    unsigned long long* buf = buffer;
    unsigned long long dest_node_page = prev_id / magic_head->node_size;
    fseek(block_file,(magic_head->main_node_table_entry+dest_node_page)*magic_head->node_size,SEEK_SET);
    fread(buffer,magic_head->node_size,1,block_file);
    //读取数据。
    return buf[prev_id%(magic_head->node_size/sizeof(unsigned long long))];
}
unsigned long long get_node_id_by_filename(int current_dir,char* filename,void* process_buffer)
{
    byte* dir_data = fhhfs_read_file(current_dir,0,process_buffer);
    unsigned long long length = ((file_header*)dir_data)->filesize;
    unsigned long long offset = sizeof(file_header);
    unsigned long long node_id;
    while(offset<length+sizeof(file_header))
    {
        node_id = *(unsigned long long*)(dir_data+offset);
        if(!strcmp((char*)(dir_data+offset+sizeof(unsigned long long)),filename))
        {
            free(dir_data);
            return node_id;
        }
        else
        {
            unsigned long filename_length = strlen((char*)(dir_data+offset+sizeof(unsigned long long)));
            offset = offset+filename_length+sizeof(unsigned long long)+1;
        }
    }
    free(dir_data);
    //由于我们这里需要返回节点，ERR_NOTFOUND与根目录节点冲突。因此，我们返回ERR_GENERIC（-1）。
    return ERR_GENERIC;
}
byte* fhhfs_read_file(unsigned long long block_id, unsigned long long count, void* process_buffer)
{
    if(count==0)
    {
        count = -1;
    }
    //由于至少有一个node，因此我们可以放心地读取1个。
    byte buffer[magic_head->node_size];
    fseek(block_file,block_id*magic_head->node_size,SEEK_SET);
    fread(buffer,magic_head->node_size,1,block_file);
    //读取node中关于文件大小的记载。
    unsigned long long size = ((file_header*)((node*)&buffer)->data)->filesize;
    //计算每个node的大小。
    short node_size = sizeof(((node*)buffer)->data);
    //计算总共需要读取几个node。
    unsigned long long node_count = (size+sizeof(file_header))/node_size +1;
    node_count = (node_count>count) ? count : node_count;
    byte* total_byte = malloc(node_count*node_size);
    for(int i=0;i<node_count;i++)
    {
        fseek(block_file,block_id*magic_head->node_size,SEEK_SET);
        fread(buffer,magic_head->node_size,1,block_file);
        memcpy(total_byte+i*node_size,((node*)&buffer)->data,node_size);
        block_id = fhhfs_get_next_node(block_id,process_buffer);
        //printf("The next node is:%llu\n",block_id);
    }
    return total_byte;
}

static unsigned long long get_node_id_by_full_path(const char* path,void* process_buffer) {
    char temp_command[PATH_MAX];
    strcpy(temp_command,path);
    char* dir_name;
    file_header* header;
    char* tmp = temp_command;
    int uid,gid,owner_priv,group_priv,other_priv;
    //如果第一个就是/的话，我们直接回滚到开始。
    int curr_dir = 1;
    if(temp_command[0]=='/')
    {
        curr_dir = 1;
        temp_command[1]='\0';
        tmp += 1;
    }
    while((dir_name = strtok(tmp,"/"))!=NULL)
    {
        unsigned long long new_id = get_node_id_by_filename(curr_dir,dir_name,process_buffer);
        if(new_id==-1)
        {
            return ERR_NOTFOUND;
        }
        header = (file_header*)fhhfs_read_file(new_id,1,process_buffer);
        if(header->file_type!=1) //这TMD根本不是个文件夹！
        {
            free(header);
            return ERR_GENERIC;
        }
        curr_dir = new_id;
        uid = header->user_id;
        gid = header->group_id;
        owner_priv = header->owner_priv;
        group_priv = header->group_priv;
        other_priv = header->group_priv;
        if(strcmp(dir_name,"."))
        {
            if(!strcmp(dir_name,".."))
            {
                unsigned long last_slash = strlen(path);
                if(last_slash!=1)
                {
                    --last_slash;
                }
                while(path[--last_slash]!='/');
                temp_command[last_slash+1]='\0';
            }
            else
            {
                strcat(temp_command,dir_name);
                strcat(temp_command,"/");
            }
            
        }
        tmp = NULL;
        free(header);
    }
    return curr_dir;
}
static int fhhfs_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi)
{
    void* working_buffer = malloc(magic_head->node_size);
    //printf( "fhhfs:tfs_readdir path : %s ", path);
    unsigned long long node_id = get_node_id_by_full_path(path,working_buffer);
    if(node_id!=ERR_GENERIC) {
        byte* dir_data = fhhfs_read_file(node_id,0,working_buffer);
        unsigned long long length = ((file_header*)dir_data)->filesize;
        unsigned long long offset = sizeof(file_header);
        unsigned long long node_id;
        while(offset<length+sizeof(file_header))
        {
            node_id = *(unsigned long long*)(dir_data+offset);
            filler(buf, (char*)(dir_data+offset+sizeof(unsigned long long)), NULL, 0);
            unsigned long filename_length = strlen((char*)(dir_data+offset+sizeof(unsigned long long)));
            offset = offset+filename_length+sizeof(unsigned long long)+1;
        }
        free(dir_data);

    }
    return ERR_GENERIC;
    //return filler(buf, "Hello-world", NULL, 0);
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