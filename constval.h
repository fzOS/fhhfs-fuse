/*
    文件系统的公用变量定义。
    Copyleft (c) Igniculus, 2019, All rights reserved.
*/
#ifndef CONSTVAL
#define CONSTVAL

typedef unsigned char byte;

#define CRC_MAGIC_NUMBER 189050311

#define FUSE_USE_VERSION 26
#define _FILE_OFFSET_BITS 64

//平台相关定义。
#ifdef __linux__
#include <linux/limits.h>
#endif
#include <errno.h>

//定义文件系统的"Magic Struct"。
//该Magic Struct从分区的0磁道2扇区(+1024K)处开始。
typedef struct fhhfs_magic_head
{
    char magic_id[7]; //"fhhfs!" + '\0',0x00~0x06
    byte version:7; //文件系统版本，0x07
    byte dirty_mark:1; //"文件系统脏"标记，当文件系统未被正确卸载时，此标记为1，与version共用一个字节
    unsigned long long last_mount_timestamp; //上一次挂载时间戳，0x08~0x0F
    unsigned long long node_total; //文件系统中总共的节点个数，0x10~0x17
    unsigned long long node_used; //文件系统中已被使用的节点个数，0x18~0x1F
    unsigned long long main_node_table_entry; //文件系统的主节点分配表位置。
    unsigned long long back_node_table_entry; //文件系统的备份节点分配表位置。
    short node_size; //文件系统中的块大小，0x20~0x21
    char label[30]; //文件系统卷标
    char end_sign[2]; //结束标志,(55AA)，0x22~0x23
}  __attribute__((packed))  fhhfs_magic_head;
/*
    定义一个基本的文件节点的物理结构格式。
    在fhhfs中，文件节点只负责维护block间的引用关系(链表)。
    当next为0时，该节点没有后继节点。
    当next具有实际值时，在读取数据时将会自动跳转至下一节点，并将数据附在当前节点的数据之后。
*/
typedef struct node
{
    // unsigned long long next; //隐式链表已被废弃。 
    byte data[2048];
}  __attribute__((packed)) node;
/*
    定义一个基本文件节点的逻辑结构格式。
    在fhhfs中，逻辑结构独立于物理结构。
*/
typedef struct file_header
{
    unsigned long long create_timestamp;
    unsigned long long modify_timestamp;
    unsigned long long open_timestamp;
    unsigned int file_type:4; //文件类型，0:普通文件,1:目录文件,2:块设备,3:软链接,其他：还没想好
    unsigned int owner_priv:4; //拥有者的权限+Set UID
    unsigned int group_priv:4; //用户所在的群组的权限+Set GID
    unsigned int other_priv:4; //其他用户的权限+Sticky
    unsigned int user_id;
    unsigned int group_id;
    unsigned long long filesize;
} __attribute__((packed)) file_header;

/*
    然后，我们定义一些常用的错误代码。
    在fhhfs-sh中，这些错误代码将会被使用。
 */
#define SUCCESS 0
#define ERR_NOTFOUND -2 //没有发现文件
#define ERR_READONLY -3 //没有写入权限
#define ERR_PERMDENI -4 //操作权限拒绝
#define ERR_EXCEEDED -5 //数据超出限制
#define ERR_GENERIC  -1 //其他通用错误

/*
  节点表的数据结构定义无法使用typedef。
  使用静态链表存储分配的node。
  定义：
  节点数值为0->未使用的节点。
  节点数值为1->无后继节点。(不可能有节点的后继节点是根目录节点……)
  节点数值为其他值->后继结点。
*/
#define NODE_UNUSED 0
#define NODE_EOF    1


/*
    按照lhb要求的文件打开的结构体。
    目前，貌似只需要首块号、存储文件的位置，以及文件头。
*/
typedef struct {
    unsigned long long first_block;
    unsigned long long offset;
    file_header header;
} opened_file;

typedef enum {
    RESOLVE_SELF=0,
    RESOLVE_PARENT=1
} PathResolveMethod;
#endif
