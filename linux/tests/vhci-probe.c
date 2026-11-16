#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <poll.h>
int main(void){
  int fd=open("/dev/vhci",O_RDWR|O_NONBLOCK); if(fd<0){perror("open");return 1;}
  unsigned char req[2]={0xff,0x00}; /* HCI_VENDOR_PKT, HCI_PRIMARY */
  int w=write(fd,req,2); printf("write=%d errno=%s\n",w,strerror(errno));
  struct pollfd p={fd,POLLIN,0}; int pr=poll(&p,1,3000); printf("poll=%d rev=%x\n",pr,p.revents);
  unsigned char b[64]; int r=read(fd,b,sizeof b); printf("read=%d errno=%s:",r,strerror(errno)); for(int i=0;i<r;i++)printf(" %02x",b[i]); printf("\n");
  return 0;}
