#include "constval.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fuse.h>
#include <zlib.h>
#include <time.h>
#include <pthread.h>
static fhhfs_magic_head* magic_head;
static FILE* block_file;
byte* fhhfs_read_file(unsigned long long block_id, unsigned long long count, void* process_buffer);
static unsigned long long fhhfs_get_next_node(unsigned long long prev_id, void* buffer)
{
    //首先，查找node分配表.
    unsigned long long* buf = buffer;
    unsigned long long entries_per_node = magic_head->node_size/sizeof(unsigned long long);
    unsigned long long node_count = prev_id / entries_per_node;
    fseek(block_file,(magic_head->main_node_table_entry+node_count)*magic_head->node_size,SEEK_SET);
    fread(buffer,magic_head->node_size,1,block_file);
    //读取数据。
    unsigned long long ret = buf[prev_id%entries_per_node];
    return ret;
}
static int fhhfs_get_nth_node_id(unsigned long long begin,unsigned long long count,void* buffer)
{
    for(unsigned long long c=0;c<count;c++) {
        begin = fhhfs_get_next_node(begin,buffer);
    }
    return begin;
}
int write_node_id(unsigned long long node,unsigned long long value,void* process_buffer)
{
    unsigned long long* table_buffer = process_buffer;
    unsigned long long entries_per_node = magic_head->node_size/sizeof(unsigned long long);
    unsigned long long node_count = node / entries_per_node;
    //write into main table.
    fseek(block_file,(magic_head->main_node_table_entry+node_count)*magic_head->node_size,SEEK_SET);
    fread(process_buffer,magic_head->node_size,1,block_file);
    table_buffer[node%entries_per_node] = value;
    fseek(block_file,(magic_head->main_node_table_entry+node_count)*magic_head->node_size,SEEK_SET);
    fwrite(process_buffer,magic_head->node_size,1,block_file);
    //write into back table.
    fseek(block_file,(magic_head->back_node_table_entry+node_count)*magic_head->node_size,SEEK_SET);
    fread(process_buffer,magic_head->node_size,1,block_file);
    table_buffer[node%entries_per_node] = value;
    fseek(block_file,(magic_head->back_node_table_entry+node_count)*magic_head->node_size,SEEK_SET);
    fwrite(process_buffer,magic_head->node_size,1,block_file);

    fflush(block_file);
    return SUCCESS;
}
//定义分配新节点的函数。
//当占用率小于30%时，首个节点采取随机分配。
//当占用率大于30%时，首个节点采取顺序分配。
static int allocate_node(unsigned long long* dest,int n,void* process_buffer,unsigned long long prev_id)
{
    //如果剩余空间不足，那么直接退出。
    if(magic_head->node_used+n>magic_head->node_total)
    {
        return ERR_EXCEEDED;
    }
    unsigned long long tmp=prev_id;
    //Tune #1:Try to make existing file countinuous.
    if((magic_head->node_used*1.0/magic_head->node_total>0.3)||prev_id)
    {
        //顺序分配。
        while(fhhfs_get_next_node(++tmp,process_buffer)!=0) {
            if(tmp >= magic_head->node_total-1)
            {
                tmp = 2;
            }
        }
    }
    else
    {  
        //随机分配。
        while(fhhfs_get_next_node((tmp = rand()*1.0/RAND_MAX*magic_head->node_total,tmp),process_buffer)!=0);
    }
    dest[0]=tmp;
    for(int i=1;i<n;i++)
    {
        while(fhhfs_get_next_node(++tmp,process_buffer)!=0)
        {
            if(tmp >= magic_head->node_total-1)
            {
                tmp = 2;
            }
        }
        dest[i]=tmp;
    }
    if(n>1)
    {
        for(int i=0;i<n-1;i++)
        {
            write_node_id(dest[i],(dest[i+1]),process_buffer);
        }
        write_node_id(dest[n-1],1,process_buffer);
    }
    else
    {
        write_node_id(dest[0],1,process_buffer);
    }
    fseek(block_file,0,SEEK_SET);
    magic_head->node_used += n;
    fwrite(magic_head,sizeof(fhhfs_magic_head),1,block_file);
    unsigned crc_value = crc32(CRC_MAGIC_NUMBER,(byte*)magic_head,sizeof(fhhfs_magic_head));
    fwrite(&crc_value,sizeof(crc_value),1,block_file);
    fflush(block_file);
    return SUCCESS;
}
unsigned long long get_node_id_by_filename(unsigned long long current_dir,char* filename,void* process_buffer)
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
    short node_size = magic_head->node_size;
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
    }
    return total_byte;
}
int fhhfs_write_file(unsigned long long orig_node_id,file_header* new_file,void* process_buffer)
{
    //由于传入了文件的格式，我们可以放心地读取文件头中关于长度的定义。
    unsigned long long node_count = (((new_file)->filesize)+sizeof(file_header)-1) / magic_head->node_size+1;
    unsigned long long node_id[node_count+1];
    //如果原来没有传入node id的话，我们生成一个。
    if(!orig_node_id)
    {
        unsigned long long tmp[1];
        allocate_node(tmp,1,process_buffer,0);
        orig_node_id = tmp[0];
    }
    node_id[0] = orig_node_id;
    unsigned long long pointer = 0;
    //如果原来节点分配表中有节点记录的话，那么复用这些信息。
    while((node_id[pointer]!=1||pointer==0)&&pointer<node_count)
    {
        node_id[pointer+1] = fhhfs_get_next_node(node_id[pointer],process_buffer);
        ++pointer;
    };
    if(pointer<node_count)
    {
        //如果不够，那么就多分配几个。
        allocate_node(node_id+pointer,node_count-pointer,process_buffer,node_id[pointer-1]);
        //刚分配出来的node连上前一个。
        if(pointer) {
            write_node_id(node_id[pointer-1],node_id[pointer],process_buffer);
        }
    }
    else
    {
        if(node_id[pointer]!=1)
        {
            //如果超了，那么就把原先的置0.
            unsigned long long tmp_node = node_id[pointer];
            unsigned long long tmp_node_next = tmp_node;
            while(tmp_node && tmp_node!=1)
            {
                tmp_node_next = fhhfs_get_next_node(tmp_node,process_buffer);
                write_node_id(tmp_node,0,process_buffer);
                tmp_node = tmp_node_next;
            }
            node_id[pointer] = 1;
        }
    }
    //然后是相对简单的写文件环节。
    pointer = 0;
    for(;pointer<node_count;pointer++)
    {
        fseek(block_file,node_id[pointer]*magic_head->node_size,SEEK_SET);
        unsigned long long offset = pointer*magic_head->node_size;
        unsigned long long size = ((pointer==node_count-1)?(((((file_header*)new_file)->filesize)+sizeof(file_header))%magic_head->node_size):magic_head->node_size);

        fwrite(((void*)new_file+offset),size,1,block_file);
    }
    return SUCCESS;
}
int insert_dir_entry(unsigned long long dir_id,unsigned long long node_id,const char* filename,void* process_buffer)
{
    byte* file_data = fhhfs_read_file(dir_id,0,process_buffer);
    file_header* header = (file_header*) file_data;
    //计算是否需要申请新的块。
    int free_size = magic_head->node_size-(magic_head->node_size%(header->filesize+sizeof(file_header)));
    if(free_size < sizeof(node_id)+strlen(filename))
    {
        file_data = realloc(file_data,(header->filesize
                                      +sizeof(file_header)
                                      +sizeof(node_id)
                                      +strlen(filename)-1)/magic_head->node_size
                                      +1
                                      );
    }
    int offset = header->filesize+sizeof(file_header);
    memcpy(file_data+offset,&node_id,sizeof(unsigned long long));
    offset += sizeof(unsigned long long);
    sprintf((char*)(file_data+offset),"%s",filename);
    header->filesize += sizeof(node_id)+strlen(filename)+1;
    fhhfs_write_file(dir_id,header,process_buffer);
    free(file_data);
    return SUCCESS;
}
int remove_dir_entry(unsigned long long dir_id,const char* filename,void* process_buffer)
{
    byte* dir_data = fhhfs_read_file(dir_id,0,process_buffer);
    unsigned long long length = ((file_header*)dir_data)->filesize;
    unsigned long long offset = sizeof(file_header);
    while(offset<length+sizeof(file_header))
    {
        if(!strcmp((char*)(dir_data+offset+sizeof(unsigned long long)),filename))
        {
            printf("%s!\n",(char*)(dir_data+offset+sizeof(unsigned long long)));
            unsigned long long filename_length = strlen((char*)(dir_data+offset+sizeof(unsigned long long)));
            unsigned long long remaining = (length+sizeof(file_header))-offset-sizeof(unsigned long long)-filename_length-1;
            memmove(dir_data+offset,dir_data+offset+sizeof(unsigned long long)+filename_length+1,remaining);
            ((file_header*)dir_data)->filesize -= (filename_length +1+sizeof(unsigned long long));
            fhhfs_write_file(dir_id,(file_header*)dir_data,process_buffer);
            free(dir_data);
            return SUCCESS;
        }
        else
        {
            printf("Not %s...\n",(char*)(dir_data+offset+sizeof(unsigned long long)));
            unsigned long long filename_length = strlen((char*)(dir_data+offset+sizeof(unsigned long long)));
            offset = offset+filename_length+sizeof(unsigned long long)+1;
        }
    }
    free(dir_data);
    //由于我们这里需要返回节点，ERR_NOTFOUND与根目录节点冲突。因此，我们返回ERR_GENERIC（-1）。
    return ERR_NOTFOUND;
}
static unsigned long long get_node_id_by_full_path(const char* path,PathResolveMethod method,void* process_buffer) {
    char temp_command[PATH_MAX];
    strcpy(temp_command,path);
    char* dir_name;
    char* tmp = temp_command;
    //如果第一个就是/的话，我们直接回滚到开始。
    unsigned long long curr_dir = 1;
    if(temp_command[1]=='\0')
    {
        return curr_dir;
    }

    dir_name = strtok(tmp,"/");
    unsigned long long new_id = get_node_id_by_filename(curr_dir,dir_name,process_buffer);        
    if(new_id==-1)
    {
        if(method==RESOLVE_SELF|| strtok(NULL,"/")!=NULL) {
            return ERR_NOTFOUND;
        }
        else {
            return curr_dir;
        }
    }
    curr_dir = new_id;
    while((dir_name = strtok(NULL,"/"))!=NULL)
    {
        unsigned long long new_id = get_node_id_by_filename(curr_dir,dir_name,process_buffer);        
        if(new_id==-1)
        {
            if(method==RESOLVE_SELF || strtok(NULL,"/")!=NULL) {
                return ERR_NOTFOUND;
            }
            else {
                return curr_dir;
            }
        }
        curr_dir = new_id;
    }
    //已经到当前目录。
    return curr_dir;
}
static int fhhfs_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi)
{
    void* working_buffer = malloc(magic_head->node_size);
    int result=SUCCESS;
    unsigned long long node_id = get_node_id_by_full_path(path,RESOLVE_SELF,working_buffer);
    if(node_id!=ERR_GENERIC) {
        byte* dir_data = fhhfs_read_file(node_id,0,working_buffer);
        if(dir_data==NULL) {
            goto readdir_fail;
        }
        unsigned long long length = ((file_header*)dir_data)->filesize;
        unsigned long long offset = sizeof(file_header);
        unsigned long long node_id;
        while(result==SUCCESS&&offset<length+sizeof(file_header))
        {
            node_id = *(unsigned long long*)(dir_data+offset);
            fi->fh = node_id;
            result=filler(buf, (char*)(dir_data+offset+sizeof(unsigned long long)), NULL, 0);
            unsigned long filename_length = strlen((char*)(dir_data+offset+sizeof(unsigned long long)));
            offset = offset+filename_length+sizeof(unsigned long long)+1;
        }
        free(dir_data);

    }
    
    else {
        result = ERR_NOTFOUND;
    }
readdir_fail:
    free(working_buffer);
    return result;
}

static int fhhfs_getattr(const char* path, struct stat *stbuf)
{
    void* buffer = malloc(magic_head->node_size);
    unsigned long long node_id=get_node_id_by_full_path(path,RESOLVE_SELF,buffer);
    if(node_id!=ERR_NOTFOUND) {
        file_header* dir_data = (file_header*)fhhfs_read_file(node_id,1,buffer);
        if(dir_data==NULL) {
            free(buffer);
            errno = ENOENT;
            return ERR_NOTFOUND;
        }
        stbuf->st_gid = dir_data->group_id;
        stbuf->st_uid = dir_data->user_id;
        stbuf->st_size = dir_data->filesize;
        stbuf->st_mode = ((dir_data->owner_priv)<<6) | ((dir_data->group_priv)<<3) | ((dir_data->other_priv));
        switch (dir_data->file_type) {
            case 0:{stbuf->st_mode|=__S_IFREG;break;}
            case 1:{stbuf->st_mode|=__S_IFDIR;break;}
            case 2:{stbuf->st_mode|=__S_IFBLK;break;}
            case 3:{stbuf->st_mode|=__S_IFLNK;break;}
        }
        stbuf->st_ctime = dir_data->create_timestamp;
        stbuf->st_mtime= dir_data->modify_timestamp;
        stbuf->st_atime = dir_data->open_timestamp;
        free(dir_data);
    }
    else {
        free(buffer);
        errno = ENOENT;
        return ERR_NOTFOUND;
    }
    free(buffer);
    return SUCCESS;
}

static void fhhfs_umount(void* data)
{
    fseek(block_file,0,SEEK_SET);
    magic_head->dirty_mark = 0;
    fwrite(magic_head,sizeof(fhhfs_magic_head),1,block_file);
    unsigned crc_value = crc32(CRC_MAGIC_NUMBER,(byte*)magic_head,sizeof(fhhfs_magic_head));
    fwrite(&crc_value,sizeof(crc_value),1,block_file);
    fflush(block_file);
    fclose(block_file);
    free(magic_head);
}

static int fhhfs_open(const char * filepath, struct fuse_file_info * fi)
{
    void* buffer = malloc(magic_head->node_size);
    unsigned long long node_id = get_node_id_by_full_path(filepath,RESOLVE_SELF,buffer);
    fi->fh = node_id;
    free(buffer);
    return SUCCESS;
}
static int fhhfs_read(const char * path, char * buf, size_t size, off_t off,struct fuse_file_info * fi)
{
    void* buffer = malloc(magic_head->node_size);
    //Get file header.
    fseek(block_file,fi->fh*magic_head->node_size,SEEK_SET);
    fread(buffer,magic_head->node_size,1,block_file);
    if(((file_header*)buffer)->filesize<size+off) {
        //Assume that off will never be greater than filesize.
        size = ((file_header*)buffer)->filesize - off;
    }
    //Calculate how many block do we need to read.
    off += sizeof(file_header);
    off_t offset_in_previous_block = off % magic_head->node_size;
    unsigned long long begin_nth_node = off / magic_head->node_size;;
    off_t offset_in_last_block = 0;
    unsigned long long middle_node_count = 0;
    if(size>(magic_head->node_size-offset_in_previous_block)) {
        //Calculate how many total blocks should be read.
        middle_node_count = (size - (magic_head->node_size - offset_in_previous_block)) / magic_head->node_size;
        //Calculate how many bytes remaining in last block.
        offset_in_last_block = size
                               -(magic_head->node_size-offset_in_previous_block)
                                -(magic_head->node_size*middle_node_count);
    }
    unsigned long long pointer = 0;
    unsigned long long current_node = fhhfs_get_nth_node_id(fi->fh,begin_nth_node,buffer);
    //Read！
    fseek(block_file,current_node*magic_head->node_size,SEEK_SET);
    fread(buffer,magic_head->node_size,1,block_file);
    memcpy(buf,buffer+offset_in_previous_block,(magic_head->node_size - offset_in_previous_block));
    pointer+=(magic_head->node_size - offset_in_previous_block);
    for(unsigned long long i=0;i<middle_node_count;i++) {
        current_node = fhhfs_get_next_node(current_node,buffer);
        fseek(block_file,current_node*magic_head->node_size,SEEK_SET);
        fread(buffer,magic_head->node_size,1,block_file);
        memcpy(buf+pointer,buffer,magic_head->node_size);
        pointer += magic_head->node_size;
    }
    if(offset_in_last_block) {
        current_node = fhhfs_get_next_node(current_node,buffer);
        fseek(block_file,current_node*magic_head->node_size,SEEK_SET);
        fread(buffer,magic_head->node_size,1,block_file);
        memcpy(buf+pointer,buffer,offset_in_last_block);
        pointer += offset_in_last_block;
    }
    free(buffer);
    return size;
}
static int fhhfs_write(const char * filename, const char * buf, size_t size, off_t off,struct fuse_file_info * fi)
{
    void* buffer = malloc(magic_head->node_size);
    //Get file header.
    fseek(block_file,fi->fh*magic_head->node_size,SEEK_SET);
    fread(buffer,magic_head->node_size,1,block_file);
    if(((file_header*)buffer)->filesize<size+off) {
        unsigned long long allocated_node_count = ((((file_header*)buffer)->filesize+sizeof(file_header)-1)/ magic_head->node_size)+1;
        ((file_header*)buffer)->filesize = size+off;
        fseek(block_file,fi->fh*magic_head->node_size,SEEK_SET);
        fwrite(buffer,magic_head->node_size,1,block_file);
        //We need to allocate more nodes.
        unsigned long long new_node_count = ((size+off+sizeof(file_header)-1)/ magic_head->node_size)+1;
        unsigned long long last_node_number = fhhfs_get_nth_node_id(fi->fh,allocated_node_count-1,buffer);
        if(new_node_count-allocated_node_count) {
            unsigned long long dest[new_node_count-allocated_node_count];
            allocate_node(dest,new_node_count-allocated_node_count,buffer,last_node_number);
            write_node_id(last_node_number,dest[0],buffer);
        }
    }
    off += sizeof(file_header);
    off_t offset_in_previous_block = off % magic_head->node_size;
    unsigned long long begin_nth_node = off / magic_head->node_size;
    unsigned long long middle_node_count=0;
    off_t offset_in_last_block=0;
    if(size > (magic_head->node_size - offset_in_previous_block)) {
        //Calculate how many total blocks should be read.
        middle_node_count = (size - (magic_head->node_size - offset_in_previous_block)) / magic_head->node_size;
        //Calculate how many bytes remaining in last block.
        offset_in_last_block = size
                                    -(magic_head->node_size-offset_in_previous_block)
                                    -(magic_head->node_size*middle_node_count);
    }
    unsigned long long pointer = 0;
    unsigned long long current_node = fhhfs_get_nth_node_id(fi->fh,begin_nth_node,buffer);
    //Write！
    fseek(block_file,current_node*magic_head->node_size,SEEK_SET);
    fread(buffer,magic_head->node_size,1,block_file);
    memcpy(buffer+offset_in_previous_block,buf,(magic_head->node_size - offset_in_previous_block));
    fseek(block_file,current_node*magic_head->node_size,SEEK_SET);
    fwrite(buffer,magic_head->node_size,1,block_file);
    pointer+=(magic_head->node_size - offset_in_previous_block);
    for(unsigned long long i=0;i<middle_node_count;i++) {
        current_node = fhhfs_get_next_node(current_node,buffer);
        fseek(block_file,current_node*magic_head->node_size,SEEK_SET);
        fread(buffer,magic_head->node_size,1,block_file);
        fseek(block_file,current_node*magic_head->node_size,SEEK_SET);
        memcpy(buffer,buf+pointer,magic_head->node_size);
        fwrite(buffer,magic_head->node_size,1,block_file);
        pointer += magic_head->node_size;
    }
    if(offset_in_last_block) {
        current_node = fhhfs_get_next_node(current_node,buffer);
        fseek(block_file,current_node*magic_head->node_size,SEEK_SET);
        fread(buffer,magic_head->node_size,1,block_file);
        fseek(block_file,current_node*magic_head->node_size,SEEK_SET);
        memcpy(buffer,buf+pointer,offset_in_last_block);
        fwrite(buffer,magic_head->node_size,1,block_file);
        pointer += offset_in_last_block;
    }
    fflush(block_file);
    free(buffer);
    return size;
}
static int fhhfs_create(const char * path, mode_t mode, struct fuse_file_info * fi)
{
    int ret=SUCCESS;
    void* buffer = malloc(magic_head->node_size);
    unsigned long long parent_node_id = get_node_id_by_full_path(path,RESOLVE_PARENT,buffer);
    //找出文件名。
    const char* real_filename=&path[strlen(path)];
    while(*(real_filename-1)!='/')
    {
        real_filename--;
        if(real_filename==path) {
            break;
        }
    }
    if(parent_node_id!=ERR_NOTFOUND) {
        unsigned long long node_buffer[2];
        if(allocate_node(node_buffer,1,buffer,0)!=ERR_EXCEEDED) {
            insert_dir_entry(parent_node_id,node_buffer[0],real_filename,buffer);
            unsigned long long timestamp = time(NULL);
            file_header h = {
                .create_timestamp = timestamp,
                .modify_timestamp = timestamp,
                .open_timestamp   = timestamp,
                .file_type        = 0, 
                .owner_priv       = 06, // rw-
                .group_priv       = 04, // r--
                .other_priv       = 04, // r--
                .user_id          = getuid(),
                .group_id         = getgid(),
                .filesize         = 0
            };
            fi->fh = node_buffer[0];
            fhhfs_write_file(fi->fh,&h,buffer);
        }
        else {
            ret = ERR_EXCEEDED;
        }

    }
    else {
        ret = ERR_NOTFOUND;
    }
    free(buffer);
    return ret;
}
static int fhhfs_statfs(const char *path, struct statvfs * fs)
{
    fs->f_blocks = magic_head->node_total;
    fs->f_bsize  = magic_head->node_size;
    fs->f_bavail = magic_head->node_total - magic_head->node_used; 
    return SUCCESS;
}
static int fhhfs_unlink(const char* filepath)
{
    int ret=SUCCESS;
    void* buffer = malloc(magic_head->node_size);
    unsigned long long node_id = get_node_id_by_full_path(filepath,RESOLVE_SELF,buffer);
    unsigned long long parent_node_id = 1;//FIXME:get_node_id_by_full_path(filepath,RESOLVE_PARENT,buffer);
    //Find filename。
    const char* real_filename=&filepath[strlen(filepath)];
    while(*(real_filename-1)!='/')
    {
        real_filename--;
        if(real_filename==filepath) {
            break;
        }
    }
    printf("%lld,%s\n",parent_node_id,real_filename);
#if 1
    unsigned long long node_count=0;
    if((ret = remove_dir_entry(parent_node_id,real_filename,buffer),ret)==SUCCESS) {
        unsigned long long temp_id;
        while(node_id!=1) {
            temp_id = fhhfs_get_next_node(node_id,buffer);
            (void)node_id;
            (void)temp_id;
            write_node_id(node_id,0,buffer);
            node_id = temp_id;
            node_count++;
        }
        magic_head->node_used -= node_count;
        fseek(block_file,0,SEEK_SET);
        fwrite(magic_head,sizeof(fhhfs_magic_head),1,block_file);
        unsigned crc_value = crc32(CRC_MAGIC_NUMBER,(byte*)magic_head,sizeof(fhhfs_magic_head));
        fwrite(&crc_value,sizeof(crc_value),1,block_file);
        fflush(block_file);
    }
#endif
    free(buffer);
    return ret;
}
static struct fuse_operations tfs_ops = {
    .readdir = fhhfs_readdir,
    .getattr = fhhfs_getattr,
    .destroy = fhhfs_umount,
    .open    = fhhfs_open,
    .read    = fhhfs_read,
    .write   = fhhfs_write,
    .create  = fhhfs_create,
    .statfs  = fhhfs_statfs,
    .unlink  = fhhfs_unlink
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
    printf("Node Table Entry:%lld\n",magic_head->main_node_table_entry);
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
    char* args_to_fuse[] = {argv[0],argv[2],"-f","-s","-o","fsname=fhhfs,big_writes"};
    ret = fuse_main(sizeof(args_to_fuse)/sizeof(char*), args_to_fuse, &tfs_ops, NULL);
    return ret;
}
