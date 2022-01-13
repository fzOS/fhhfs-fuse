#include "constval.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <zlib.h>
#include <time.h>
//定义我们自己的print函数，在输出前加入调用程序名称。
#define print(x...) printf("mkfs.fhhfs: " x)
FILE* block_file;
//定义临时变量ch，用于从键盘接受用户数据。
unsigned char ch;
int main(int argc,const char* argv[])
{
    srand(time(NULL));
    if(argc == 2)
    {
        block_file = fopen(argv[1],"rb+");
        print("Using custom block file.\n");
    }
    else
    {
        printf("usage:%s inputfile.\n",argv[0]);
        return EXIT_FAILURE;
    }
    if(block_file==NULL)
    {
        print("Unable to open block file.Exiting.\n");
        goto exit;
    }
    //再次提醒用户确认操作。
    printf("WARNING:FORMATTING A BLOCK FILE CANNOT BE UNDONE.\nARE YOU SURE?(y/N):");
    ch = getchar();
    if(ch!='y'&&ch!='Y')
    {
        printf("Abort.\n");
        goto out;
    }
    //检测块文件的大小。
    fseek(block_file, 0, SEEK_END);
    long size = ftell(block_file);
    rewind(block_file);
    print("The size of block file is %ld bytes.\n",size);
    //将块文件大小转化为Block数量。
    long node_count = size / 2048; //文件系统Magic所占用的Block为 block 0。
    print("Under the block size of 2048 bytes,the partition will have %ld nodes.\n",node_count);
    char tmp[30]; //为用户输入文件名
    print("Please input the label of volume (30 characters max):");
    scanf("%s",tmp);
    print("Writing file system magic data.\n");
    fseek(block_file,0, SEEK_SET);
    //没想到还是向FAT屈服了…………
    //生成节点分配表的长度。
    unsigned long long node_table_length = ((node_count - 1) * sizeof(unsigned long long) / 2048)+1;
    print("The node table will cover %llu blocks.\n",node_table_length);
    //生成两张节点分配表的位置。
    unsigned long long main_node_table_entry,back_node_table_entry;
    while((main_node_table_entry = rand()*1.0/RAND_MAX*node_count,main_node_table_entry)==0
         ||main_node_table_entry == 1
         ||main_node_table_entry + node_table_length > node_count
           );
    while((back_node_table_entry = rand()*1.0/RAND_MAX*node_count,main_node_table_entry)==0
         ||back_node_table_entry == 1
         ||back_node_table_entry < main_node_table_entry + node_table_length //两张表重叠可不行
         ||back_node_table_entry + node_table_length > node_count
         );
    print("The positions of node tables' entries are:%llu and %llu\n",main_node_table_entry,back_node_table_entry);
    fhhfs_magic_head magic_head = 
    {
        .magic_id              = "fhhfs!",
        .version               = 0,
        .last_mount_timestamp  = 0L,
        .node_total            = node_count,
        .node_used             = 2+node_table_length*2, //文件系统自己也是要恰饭的#(滑稽),再加上一个根目录数据和两张节点分配表。
        .node_size             = 2048,
        .main_node_table_entry = main_node_table_entry,
        .back_node_table_entry = back_node_table_entry,
        .end_sign              = {0x55,0xAA},
    };
    strcpy(magic_head.label,tmp);
    fwrite(&magic_head,sizeof(magic_head),1,block_file);
    //将文件系统的Magic Head计算CRC32，值放入紧跟的4字节。
    unsigned crc_value = crc32(CRC_MAGIC_NUMBER,(byte*)&magic_head,sizeof(magic_head));
    fwrite(&crc_value,sizeof(crc_value),1,block_file);
    ch = 0;
    fwrite(&ch,1,1536-ftell(block_file),block_file);
    //在513字节开始再存储一份分区表，作为备份。
    fwrite(&magic_head,sizeof(magic_head),1,block_file);
    fwrite(&crc_value,sizeof(crc_value),1,block_file);
    fwrite(&ch,1,2048-ftell(block_file),block_file);
    print("Creating root node.\n");
    fseek(block_file,2048, SEEK_SET);
    //根目录数据从第1block开始。
    node root_node;
    memset(&root_node, 0, sizeof(node));
    unsigned long long timestamp = time(NULL);
    //生成根目录数据。
    file_header root_file_header = 
    {
        .create_timestamp = timestamp,
        .modify_timestamp = timestamp,
        .open_timestamp   = timestamp,
        .file_type        = 1,  //根目录必然是个目录……
        .owner_priv       = 07, // rwx
        .group_priv       = 05, // r-x
        .other_priv       = 05, // r-x
        .user_id          = 0,  //root
        .group_id         = 0,    //wheel
        .filesize         = 21, //.和.. 
    };
    memcpy(root_node.data,&root_file_header,sizeof(root_file_header));
    unsigned long long root_node_id = 1;
    int offset = sizeof(root_file_header);
    memcpy(root_node.data+offset,&root_node_id,sizeof(root_node_id));
    offset+=sizeof(root_node_id);
    sprintf((char*)root_node.data+offset,".");//当前目录。
    offset+=2;
    memcpy(root_node.data+offset,&root_node_id,sizeof(root_node_id));
    offset+=sizeof(root_node_id);
    sprintf((char*)root_node.data+offset,"..");//上级目录。根目录的上级就是自己。
    //将数据写入文件系统。
    fwrite(&root_node,sizeof(root_node),1,block_file);
    fflush(block_file);
    print("Creating node table nodes.\n");
    //Empty node table.
    fseek(block_file,main_node_table_entry*magic_head.node_size,SEEK_SET);
    unsigned long long* i = calloc(1,2048);
    for(unsigned long long j=0;j<node_table_length;j++) {
        fwrite(i,2048,1,block_file);
    }
    fseek(block_file,back_node_table_entry*magic_head.node_size,SEEK_SET);
    for(unsigned long long j=0;j<node_table_length;j++) {
        fwrite(i,2048,1,block_file);
    }
    //Write root data in main table.
    i[0] = i[1] = NODE_EOF;
    fseek(block_file,main_node_table_entry*magic_head.node_size,SEEK_SET);
    fwrite(i,sizeof(unsigned long long),2,block_file);
    //Write main table nodes into main table.
    fseek(block_file,(main_node_table_entry)*(magic_head.node_size+sizeof(unsigned long long)),SEEK_SET);
    i[0] = main_node_table_entry +1;
    for(unsigned long long j=0;j<node_table_length-1;j++) {
        fwrite(i,sizeof(unsigned long long),1,block_file);
        i[0]++;
    }
    i[0] = NODE_EOF;
    fwrite(i,sizeof(unsigned long long),1,block_file);
    //Write back table nodes into main table.
    fseek(block_file,main_node_table_entry*magic_head.node_size+back_node_table_entry*sizeof(unsigned long long),SEEK_SET);
    i[0] = back_node_table_entry +1;
    for(unsigned long long j=0;j<node_table_length-1;j++) {
        fwrite(i,sizeof(unsigned long long),1,block_file);
        i[0]++;
    }
    i[0] = NODE_EOF;
    fwrite(i,sizeof(unsigned long long),1,block_file);
    //Write root data in back table.
    fseek(block_file,back_node_table_entry*magic_head.node_size,SEEK_SET);
    fwrite(i,sizeof(unsigned long long),2,block_file);
    //Write back table nodes into back table.
    fseek(block_file,(back_node_table_entry)*(magic_head.node_size+sizeof(unsigned long long)),SEEK_SET);
    i[0] = back_node_table_entry +1;
    for(unsigned long long j=0;j<node_table_length-1;j++) {
        fwrite(i,sizeof(unsigned long long),1,block_file);
        i[0]++;
    }
    i[0] = NODE_EOF;
    fwrite(i,sizeof(unsigned long long),1,block_file);
    //Write main table nodes into back table.
    fseek(block_file,back_node_table_entry*magic_head.node_size+main_node_table_entry*sizeof(unsigned long long),SEEK_SET);
    i[0] = back_node_table_entry +1;
    for(unsigned long long j=0;j<node_table_length-1;j++) {
        fwrite(i,sizeof(unsigned long long),1,block_file);
        i[0]++;
    }
    i[0] = NODE_EOF;
    free(i);
    fwrite(&i,sizeof(unsigned long long),1,block_file);
out:fflush(block_file);
    fclose(block_file);
exit:print("Done.\n");
}
