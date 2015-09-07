// first OpenVG program
// Anthony Starks (ajstarks@gmail.com)
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <sys/types.h>
#include "VG/openvg.h"
#include "VG/vgu.h"
//#include "fontinfo.h"
#include "shapes.h"

#include <netdb.h>
#include <netinet/in.h>
#include <string.h>
#include <time.h>

void changemode(int);
int  kbhit(void);
short keyPressed(char);
short keyReleased(char);

short old_sr = 0;
short sr = 0;


int main(int argc, char *argv[]) {
	int width=512, height=512;
	char s[3];

	int sockfd, portno =2222, n;
	struct sockaddr_in serv_addr;
	struct hostent *server;

	char buffer[256], ch;

	int k, i=0;
	char coord[4];
	float x,y;

	clock_t time = 0;

	changemode(1);	// used for kbhit()

	init(&width, &height);				   // Graphics initialization

	Start(width, height);				   // Start the picture
	Background(44,77,232);				   // Black background

	Fill(0,0,0, 1);				   // Big blue marble
	Circle(width / 2, height / 2, width/2);			   // The "world"
	End();						   // End the picture



	// create socket
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd<0) 
	{	perror("ERROR opening socket");	exit (1);	}
	server=gethostbyname(argv[1]);
	if (server==NULL) 
	{	perror("ERROR no such host");	exit (0);	}
	memset ((void *) &serv_addr, '\0', sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	memcpy ((void *) &serv_addr.sin_addr.s_addr, (void *)server->h_addr, server->h_length);
	serv_addr.sin_port = htons(portno);
	if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0)
	{	perror("ERROR connecting");	exit (1);	}

	//bzero(buffer, 256);
	
	do
	{
		time = clock();
		//Start(width, height);				   // Start the picture
		Stroke(0,255,0, 1); StrokeWidth(1);
		Fill(0,0,0,0.1);				   // Big blue marble
		Rect((width-512)/2, (height-512)/2, 512, 512);
		while ((float)CLOCKS_PER_SEC/(float)(clock() - time) > 120.0) 
		{
			n = recv(sockfd, buffer, 5, MSG_PEEK);
			if (n<0)
			{	perror("ERROR receiving from socket");	exit (1);	
			}
		
			if (n>=5)
			{	
				n = read(sockfd, buffer, 1);
				if (buffer[0]==0)
					i++;
				else
					i=0;

				if (i==2)
{
i=0;
				n = read(sockfd, buffer, 4);
				if (n<0)
				{
					perror("ERROR reading from socket");
					exit (1);
				}
//				if (n!=5)
//					perror("ERROR did not read 5 bytes");
		
				for (k=0;k<4;k++)
					coord[k] = buffer[k] & 0x3f;
				x = ((((coord[0] | (coord[1]<<6)) / 2) + 256) % 512) +(width-512)/2;
				y = (512 - ((((coord[2] | (coord[3]<<6)) / 2) + 256) % 512)) +(height-512)/2;
		
				Line(x,y,x+1,y+1);
		
//				printf("X = %f, Y = %f\n",x,y);
}
			}
		};
	
//		printf("------------------------\n");
		End();

		if (kbhit())
		{
			old_sr = sr;
			ch = getchar();
			sr = keyPressed(ch);
		}
		else
		{
			sr=0;
		}
		if (old_sr != sr)
		{
			buffer[0] = (sr & 0xF);
			buffer[1] = ((sr & 0xF00) >> 4);
			// s.write((sr & 0xF) | ((sr & 0xF00) >> 4));
			n = write(sockfd, buffer, 2);
		}
		

	} while (s[0]!='x');
//*/ End();	fgets(s, 2, stdin);

	finish();					   // Graphics cleanup
	changemode(0);	// used for kbhit()
	exit(0);
}


short keyPressed(char key)
{
//  old_sr = sr;
  switch (key)
  {
	  case '1':    sr |= 0x800;    break;
	  case '2':    sr |= 0x400;    break;
	  case '3':    sr |= 0x200;    break;
	  case '4':    sr |= 0x100;    break;
	  case '5':    sr |= 0x80;    break;
	  case '6':    sr |= 0x40;    break;
	  case '7':    sr |= 0x20;    break;
	  case '8':    sr |= 0x10;    break;
	  case '9':    sr |= 0x8;    break;
	  case '0':    sr |= 0x4;    break;
	  case '-':    sr |= 0x2;    break;
	  case '=':    sr |= 0x1;    break;
	  default:    break;
  }
/*  if (old_sr != sr)
  {
    s.write((sr & 0xF) | ((sr & 0xF00) >> 4));
  }
*/
  return sr;
}

short keyReleased(char key)
{
//  old_sr = sr;
  switch (key)
  {
	  case '1':    sr &= ~0x800;    break;
	  case '2':    sr &= ~0x400;    break;
	  case '3':    sr &= ~0x200;    break;
	  case '4':    sr &= ~0x100;    break;
	  case '5':    sr &= ~0x80;    break;
	  case '6':    sr &= ~0x40;    break;
	  case '7':    sr &= ~0x20;    break;
	  case '8':    sr &= ~0x10;    break;
	  case '9':    sr &= ~0x8;    break;
	  case '0':    sr &= ~0x4;    break;
	  case '-':    sr &= ~0x2;    break;
	  case '=':    sr &= ~0x1;    break;
	  default:    break;
  }
/*  if (old_sr != sr)
  {
    s.write((sr & 0xF) | ((sr & 0xF00) >> 4));
  }
*/
  return sr;
}

void changemode(int dir)
{
  static struct termios oldt, newt;
 
  if ( dir == 1 )
  {
    tcgetattr( STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~( ICANON | ECHO );
    tcsetattr( STDIN_FILENO, TCSANOW, &newt);
  }
  else
    tcsetattr( STDIN_FILENO, TCSANOW, &oldt);
}
 
int kbhit (void)
{
  struct timeval tv;
  fd_set rdfs;
 
  tv.tv_sec = 0;
  tv.tv_usec = 0;
 
  FD_ZERO(&rdfs);
  FD_SET (STDIN_FILENO, &rdfs);
 
  select(STDIN_FILENO+1, &rdfs, NULL, NULL, &tv);
  return FD_ISSET(STDIN_FILENO, &rdfs);
 
}
