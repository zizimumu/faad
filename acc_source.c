#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdarg.h>
#include <signal.h> 


#define MAX_FILE_BUF 4096


static void faad_fprintf(FILE *stream, const char *fmt, ...)
{
    va_list ap;


    {
        va_start(ap, fmt);

        vfprintf(stream, fmt, ap);

        va_end(ap);
    }
}

void add_noise(unsigned char *buf,int buf_sz,int cnt)
{
	int i;
	int off;

	for(i=0;i<cnt;i++)
	{
		off = rand()%buf_sz;
		buf[off] = rand()%255 + 1;
	}
}


void test(int n,struct siginfo *siginfo,void *myact)  
{  
		faad_fprintf(stderr,"acc source exit\n",n);/** 打印出信号值 **/ 
         faad_fprintf(stderr,"signal number:%d\n",n);/** 打印出信号值 **/  
         faad_fprintf(stderr,"siginfo signo:%d\n",siginfo->si_signo); /** siginfo结构里保存的信号值 **/  
         faad_fprintf(stderr,"siginfo errno:%d\n",siginfo->si_errno); /** 打印出错误代码 **/  
         faad_fprintf(stderr,"siginfo code:%d\n",siginfo->si_code);   /**　打印出出错原因 **/  
    exit(0);  
} 


int main(int argc, char **argv)
{
	int fd,ret;
	unsigned int len,offset,count=0,read_cnt = 1;
	char *file,*buf;
	int fd_std;
	
	if(argc != 2){
		printf("err param,use %s input_file\n",argv[0]);
		return -1;
	}



	struct sigaction act;  
	sigemptyset(&act.sa_mask);   /** 清空阻塞信号 **/  
	act.sa_flags=SA_SIGINFO;     /** 设置SA_SIGINFO 表示传递附加信息到触发函数 **/  
	act.sa_sigaction=test;  
	if(sigaction(SIGPIPE,&act,NULL) < 0)  
	{  
		 printf("install signal error\n");  
	} 


	file = argv[1];
	fd = open(file,O_RDWR);
	if(fd < 0 ){
		perror("open input file err\n");
		return -1;
	}



	len = lseek(fd,0,SEEK_END);
	lseek(fd,0,SEEK_SET);

	faad_fprintf(stderr,"open file %s,size %d ,ver %d\n",file,len,4);

	srand(2017);
	buf = malloc(MAX_FILE_BUF);
	fd_std = fileno(stdout);

	lseek(fd,55,SEEK_SET);

	while(1){
		
		ret = read(fd,buf,MAX_FILE_BUF);
		if(ret < 0){
			faad_fprintf(stderr,"read file err\n");
			return -1;
		}
		else if(ret < MAX_FILE_BUF || ( (read_cnt % 250) == 0)){

			//return 0;
			read_cnt++;
			offset = rand() % len;
			lseek(fd,offset,SEEK_SET);
			faad_fprintf(stderr,"\n\nfile end,start again at %d\n\n",offset);
		}

		

		if((count % 2) == 0)
			add_noise(buf,MAX_FILE_BUF,500);

		count++;//

		if(ret > 0)
			write(fd_std,buf,ret);
	}
	
	close(fd);
	free(buf);

	faad_fprintf(stderr,"done\n");

	return 0;
}


