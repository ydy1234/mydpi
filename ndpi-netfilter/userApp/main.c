#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <fcntl.h>
//#include <sqlite3.h>
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

	//sprintf(ccmd,"\n%s %s %s %ld\n",name,src,dst,tbyte);
	sprintf(ccmd,"%s %s %s\n",name,src,dst);
}

void extractDataIP(char* msg)
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
	end=strstr(msg,":");
	len=strlen(start)-strlen(end);
	strncpy(src,start,len);
	src[len]='\0';
	printf("src %s\n",src);
	//find dest
	start=strstr(msg,"dst ");
	start+=4;
	end=strstr(start,":");
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

	//sprintf(ccmd,"\n%s %s %s %ld\n",name,src,dst,tbyte);
	sprintf(ccmd,"%s %s %s\n",name,src,dst);
}

int CheckAndUpdate(FILE* fp,char *src, char *dst,double size)
{ 
  	char tmp[1024];
	char dd[1024];
  while(fgets(tmp,1024,fp)!=NULL)
  {
    //printf("读取的数据 %s\n",tmp);
    if(strstr(tmp,src)!=NULL&&strstr(tmp,dst)!=NULL)
    {
	   printf("origin:%s\n",tmp);
       char* sz=strrchr(tmp,' ');

	   long tsize=atol(sz+1);
	    printf("Num1=%s,tsize=%d\n",sz+1,tsize);
	   size+=tsize;
	   sprintf(dd,"%ld",size);
	   int len=strlen(dd);
	   printf("Num2=%s,tsize=%d\n",dd,tsize);
	   strcpy(sz+1,dd);
	   printf("After:%s\n",tmp);
	   return 1;
	}
  }
  return 0;
}
int CheckAndUpdate2(FILE* fp,char *msg)
{ 
  char tmp[1024];
  while(fgets(tmp,1024,fp)!=NULL)
  {
     if(strcmp(tmp,msg)==0)
	 	return 1; 
  }
  return 0;
}

void Insert(FILE* fp,char* msg)
{
    
	fseek(fp, 0, SEEK_END);
	fwrite(msg, strlen(msg), 1, fp);
	//fscanf(fp,"\n%s %s %s %ld\n",name,src,dst,size);
}
void CopyToFile(FILE* src,FILE* dst)
{
  char tmp[1024];
  char dtmp[1024];
  char dd[4096][100];
  char ddn[4000][100];
  int flag=0;
  int cnt=0;
  int ncnt=0;
  int i;
  dst=fopen("dpiFinal.txt","a+");
  while(fgets(dtmp,1024,dst)!=NULL)
  {
    strcpy(dd[cnt++],dtmp);	
  }
  fclose(dst);
  while(fgets(tmp,1024,src)!=NULL)
  {
    flag=0;
    for(i=0;i<4096;i++)
    {
      if(strcmp(tmp,dd[i])==0)
      {
        flag=1;
        break;
	  }
	}
	if(flag==0)
	{
	  strcpy(dd[cnt++],tmp);
	  strcpy(ddn[ncnt++],tmp);
	}
  }
  dst=fopen("dpiFinal.txt","a+");
  for(i=0;i<ncnt;i++)
  {
    fseek(dst, 0, SEEK_END);
	fwrite(ddn[i], strlen(ddn[i]), 1, dst);
  }
  fclose(dst);
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
	FILE* fp=fopen("dpiResult.txt","a+");
	FILE* fp2;//=fopen("dpiFinal.txt","a+");
	if(fp==NULL)
	{
       printf("file open failed\n");
	   return -1;
	}
	//assert(fp!=NULL);
	
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

		if(cnt%4000==0)
		{
		  fclose(fp);
		  fp=fopen("dpiResult.txt","a+");
		  CopyToFile(fp,fp2);
          fclose(fp);
		  fp=fopen("dpiResult.txt","w+");
		  
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
			  extractDataIP(result);
	          if(CheckAndUpdate2(fp,ccmd)==0)
	          {
	            Insert(fp,ccmd);
	          }
			}
			printf("recvmsg:ret=%d, [%s]\n", ret,NLMSG_DATA(nlhdr));
		}
		cnt++;
	}

    fclose(fp);
	return 0;
}
