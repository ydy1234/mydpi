#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <fcntl.h>
#include <string.h>
#include <sqlite3.h>
//#include "constants.h"

#define NETLINK_TEST 30

#define MAX_PAYLOAD 1024
char result[MAX_PAYLOAD];
char ccmd[MAX_PAYLOAD];
int g_fd;

//typedef struct PP{
  char name[20];
  char src[30];
  char dst[30];
  double tbyte;
//} pp;
int record=0;
int rc;
char *zErrMsg;

void extractData(char* msg)
{
	//find protocol name
	char* start;
	char* end;
	int len=0;
	int i;
	start=strstr(msg,"Proto ");
	start+=6;
	end=strstr(msg,"( src");
	end-=1;
	len=strlen(start)-strlen(end);
	strncpy(name,start,len);
	name[len]='\0';
	printf("name %s\n",name);

	//find source
	start=strstr(msg,"src");
	start+=4;
	end=strstr(msg,"--->");
	len=strlen(start)-strlen(end);
	strncpy(src,start,len);
	src[len]='\0';
	printf("src %s\n",src);
	//find dest
	start=strstr(msg,"dst ");
	start+=4;
	end=strstr(msg,",ipsize");
	len=strlen(start)-strlen(end);
	strncpy(dst,start,len);
	dst[len]='\0';
	printf("dst %s\n",dst);
	//find size
	start=strstr(msg,"ipsize=");
	end=strstr(start,")");
	start+=7;
	len=strlen(start)-strlen(end);
	tbyte=0;
	for(i=0;i<len;i++)
	{
	  int tmp=*(start+i)-'0';
	  tbyte=tmp+tbyte*10;
	  printf("tmp=%d databyte=%lf\n",tmp,tbyte);
	}
	printf("%s,len=%d\n",start,len);
}
static int
callback(void *unused, int argc, char **argv, char **azColName)
{
    int i;

    for (i = 0; i < argc; i++) {
        printf("%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
    }
    printf("\n");
    return (0);
}


static int
sql_execute(sqlite3 *db, char *sqlCmd, char *opString)
{
    int ret;
    char *errMsg = NULL;

    ret = sqlite3_exec(db, sqlCmd, callback, 0, &errMsg);

    if (ret != SQLITE_OK) {
        fprintf(stdout, "%s failed: %s\n", opString, errMsg);
        sqlite3_free(errMsg);
    } else {
        fprintf(stdout, "%s succeded\n", opString);
    }
    return (ret);
}

static int
sqldb_view(sqlite3 *db)
{
    char *sqlCmd;

    sqlCmd = "SELECT * FROM DPIRESULT"; 
    sql_execute(db, sqlCmd, "Select all");

    return (0);
}
static int
sqldb_find(sqlite3 *db,char* src,char* dst)
{
   char sqlCmd[1024];

    //sqlCmd = "SELECT * FROM DPIRESULT WHERE SRC";
	snprintf(sqlCmd, sizeof (sqlCmd), 
             "DELETE from DPIRESULT WHERE SRC = %s AND DST =%s", src, dst);
    sql_execute(db, sqlCmd, "Select all");

    return (0);
}

static int
sqldb_delete_data(sqlite3 *db, int id)
{
    char sqlCmd[1024];

    snprintf(sqlCmd, sizeof (sqlCmd), 
             "DELETE from DPIRESULT where ID = %d", id);
    sql_execute(db, sqlCmd, "Data delete");

    return (0);
}

static int
sqldb_update_age(sqlite3 *db, int id, int size)
{
    char sqlCmd[1024];

    snprintf(sqlCmd, sizeof (sqlCmd), 
             "UPDATE DPIRESULT set SIZE = %d where ID = %d",
             size, id);
    sql_execute(db, sqlCmd, "Data update");

    return (0);
}

static int
sqldb_insert(sqlite3 *db, int id, char *name, char* src, char *dst,double size)
{
    char sqlCmd[1024];

    snprintf(sqlCmd, sizeof (sqlCmd), 
             "INSERT INTO DPIRESULT (ID, NAME, SRC, DST, SIZE) " \
             "VALUES (%d, \'%s\', \'%s\', \'%s\',%lf);", id, name, src, dst,size);
    sql_execute(db, sqlCmd, "Data insert");

    return (0);
}

static int
sqldb_create(sqlite3 *db)
{
    char *sqlCmd;

    sqlCmd = "CREATE TABLE DPIRESULT(" \
             "ID       INT PRIMARY KEY NOT NULL," \
             "NAME     TEXT            NOT NULL," \
             "SRC      TEXT             NOT NULL," \
             "DST      TEXT             NOT NULL," \
             "SIZE     REAL);";
    sql_execute(db, sqlCmd, "Create DB");

    return (0);
}

static sqlite3 *
sqldb_open(char *filename)
{
    int ret;
    sqlite3 *db = NULL;
    ret = sqlite3_open(filename, &db);

    if (ret) {
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
        exit(0);
    } else {
        fprintf(stderr, "Opened database successfully\n");
    }
    return db;
}

int main(int argc, char *argv[]) {
	int ret = 0;
	int flags;
	
	//int dblen;
	//sqlite3 *db=NULL;
	//db=sqldb_open("dpi.db");
	//sqldb_create(db);
   // if(db==NULL)
   // {
   //   printf("db open faile\n");
	//}
	 
	g_fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_TEST);
	if (0 > g_fd) {
		perror("create socket error");
		return -1;
	}
	     //设置socket为非阻塞模式  
    flags = fcntl(g_fd, F_GETFL, 0);  
    fcntl(g_fd, F_SETFL, flags|O_NONBLOCK);  
	// 设置源地址
	struct sockaddr_nl src_addr;
	src_addr.nl_family = AF_NETLINK;
	src_addr.nl_pad = 0;
	src_addr.nl_pid = getpid();
	src_addr.nl_groups = 0;
	ret = bind(g_fd, (struct sockaddr *)&src_addr, sizeof(src_addr));
	if (-1 == ret) {
		perror("bind");
		return -1;
	}

	// 设置目的地址
	struct sockaddr_nl dst_addr;

	// 设置消息头
	struct nlmsghdr *nlhdr = (struct nlmsghdr *)malloc(NLMSG_SPACE(MAX_PAYLOAD));

	struct iovec iov;

	struct msghdr msg;
	int cnt=0;
	while (1) {
		dst_addr.nl_family = AF_NETLINK;
		dst_addr.nl_pad = 0;
		dst_addr.nl_pid = 0;
		dst_addr.nl_groups = 0;

		nlhdr->nlmsg_len = NLMSG_SPACE(MAX_PAYLOAD);
		nlhdr->nlmsg_pid = getpid();
		nlhdr->nlmsg_flags = 0;
		nlhdr->nlmsg_type = 0;

		iov.iov_base = (void *)nlhdr;
		iov.iov_len = nlhdr->nlmsg_len;

		memset(&msg, 0, sizeof(msg));
		msg.msg_name = (void *)&dst_addr;
		msg.msg_namelen = sizeof(dst_addr);
		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;

		if(cnt%1000==0)
		{
		strcpy(NLMSG_DATA(nlhdr), "test");
		
		ret = sendmsg(g_fd, &msg, 0);
		if (-1 == ret) {
			perror("sendmsg");
			return -1;
		} else {
			printf("sendmsg: [%s]\n", NLMSG_DATA(nlhdr));
		}
		}

		memset(nlhdr, 0, NLMSG_SPACE(MAX_PAYLOAD));
		ret = recvmsg(g_fd, &msg, 0);
		//printf("YDY %d\n",ret);
		if (ret<0)
		{
			//printf("YDY recvmsg failed\n");
			perror("recvmsg");
			//return -1;
		} 
		else 
		{
		    strncpy(result,NLMSG_DATA(nlhdr),NLMSG_SPACE(MAX_PAYLOAD));
			printf("result=%s\n",result);
			if(strstr(result,"127.0")==NULL)
		    {
			  extractData(result);
	          
			}
			printf("recvmsg:ret=%d, [%s]\n", ret,NLMSG_DATA(nlhdr));
		}
		cnt++;
	}

	return 0;
}
