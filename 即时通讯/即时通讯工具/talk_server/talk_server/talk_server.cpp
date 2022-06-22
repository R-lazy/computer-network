#include "stdafx.h"
#include "stdafx.h"
#include <stdio.h>  
#include<iostream>
#include <string>
#include<cstring>
using namespace std;
#include <winsock2.h>  

#pragma comment(lib,"ws2_32.lib")  
#define BUF_SIZE 1024

typedef struct test{
	SOCKET sclient;
}Test;
DWORD WINAPI FunSend(LPVOID lpParamter)
{
		Test *pt=(Test*)lpParamter;
		string sLine;
		getline(cin,sLine);
		string data=sLine;
		const char * sendData;
		sendData = data.c_str();   //字符串转化为指针 
		

		send(pt->sclient, sendData, strlen(sendData), 0);
		Sleep(5000);
		//send()用来将数据由指定的socket传给对方主机
    return 0;
}
//
DWORD WINAPI FunRev(LPVOID lpParamter){
		char buffer[255];
		Test *pt=(Test*)lpParamter;
		int ret=recv(pt->sclient, buffer, BUF_SIZE, 0);  
		if(ret>0){
			buffer[ret]='\0';
			printf("client:%s\n",buffer);
		}
		Sleep(5000);
		return 0;
}

int _tmain(int argc, _TCHAR* argv[])
{
	    //初始化WSA  加载套接字库
    WORD sockVersion = MAKEWORD(2,2);  
    WSADATA wsaData;  
    if(WSAStartup(sockVersion, &wsaData)!=0)  
    {  
        return 0;  
    }  
  
    //创建套接字  
    SOCKET slisten = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);  
    if(slisten == INVALID_SOCKET)  
    {  
        printf("socket error !");  
        return 0;  
    }  
  
    //绑定IP和端口  绑定套接字
    sockaddr_in sin;  
	memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;  
    sin.sin_port = htons(8888);  
    sin.sin_addr.S_un.S_addr = INADDR_ANY;   
    if(bind(slisten, (LPSOCKADDR)&sin, sizeof(sin)) == SOCKET_ERROR)  
    {  
        printf("bind error !");  
    }  
  
    //开始监听  将套接字设为监听模式
    if(listen(slisten, 5) == SOCKET_ERROR)  
    {  
        printf("listen error !");  
        return 0;  
    }  
	
    //循环接收数据  接受请求后 返回一个新的对应的套接字
    SOCKET sClient;  
    sockaddr_in remoteAddr;  
    int nAddrlen = sizeof(remoteAddr);  
    //char revData[255];   
    //while (true)  {  
      while(true){  
		printf("wait connectin...\n local ip %s",inet_ntoa(sin.sin_addr));  
        sClient = accept(slisten, (SOCKADDR *)&remoteAddr, &nAddrlen);  
        if(sClient == INVALID_SOCKET)  
        {  
            printf("accept error !");  
            //return 1;  
        }  
        //printf("client connect ok ip：%s \r\n", inet_ntoa(remoteAddr.sin_addr));  
		printf("this is server client connect ok \n"); 
		break;
	  }

		Test t;
		t.sclient=sClient;
		while(true){
		HANDLE hThreadSend = CreateThread(NULL, 0, FunSend, (LPVOID)&t, 0, NULL);
		HANDLE hThreadRev = CreateThread(NULL, 0, FunRev, (LPVOID)&t, 0, NULL);
		CloseHandle(hThreadSend);
		CloseHandle(hThreadRev);
		}
	closesocket(sClient);
    closesocket(slisten);  
    WSACleanup();  
	return 0;
}

