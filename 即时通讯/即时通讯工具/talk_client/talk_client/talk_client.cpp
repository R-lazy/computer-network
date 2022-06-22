#include "stdafx.h"

#include<WINSOCK2.H>
#include<STDIO.H>
#include<iostream>
#include<cstring>
#include <string>
using namespace std;
#pragma comment(lib, "ws2_32.lib")

#define BUF_SIZE 1024

typedef struct test{//定义一个结构体，用来给线程传参数
	SOCKET sclient;
}Test;
DWORD WINAPI FunSend(LPVOID lpParamter)//发送数据的线程
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
DWORD WINAPI FunRev(LPVOID lpParamter){//接受数据的线程
		char buffer[255];
		Test *pt=(Test*)lpParamter;
		int ret=recv(pt->sclient, buffer, BUF_SIZE, 0);  
		if(ret>0){
			buffer[ret]='\0';
			printf("server:%s\n",buffer);
		}
		Sleep(5000);
		return 0;
}

int _tmain(int argc, _TCHAR* argv[])
{
	WORD sockVersion = MAKEWORD(2, 2);//使用socket的固定写法
	WSADATA data;
	if(WSAStartup(sockVersion, &data)!=0)
	{
		return 0;
	}
	//while(true){
		SOCKET sclient = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);//新建一个SOCKET，后边是配置的参数，包括用什么协议等
		if(sclient == INVALID_SOCKET)
		{
			printf("invalid socket!");
			return 0;
		}
		
		sockaddr_in serAddr;//设置一个套接字，套接字用来填充SOCKET的属性，包括IP、端口等
		serAddr.sin_family = AF_INET;
		serAddr.sin_port = htons(8888);
		//serAddr.sin_port = htons(2049);
		serAddr.sin_addr.S_un.S_addr = inet_addr("127.0.0.1");
		//serAddr.sin_addr.S_un.S_addr = inet_addr("192.32.7.1");
		if(connect(sclient, (sockaddr *)&serAddr, sizeof(serAddr)) == SOCKET_ERROR)
		{  //连接失败 
			printf("connect error !");
			closesocket(sclient);
			return 0;
		}
		else{
			printf("Hellow\n");
			
		}


		//将sclient这个sock传入线程
		Test t;
		t.sclient=sclient;
		while(true){
		HANDLE hThreadSend = CreateThread(NULL, 0, FunSend, (LPVOID)&t, 0, NULL);//启动两个收发线程
		HANDLE hThreadRev = CreateThread(NULL, 0, FunRev, (LPVOID)&t, 0, NULL);
		


		CloseHandle(hThreadSend);//关闭线程
		CloseHandle(hThreadRev);
		}	
	closesocket(sclient);//关闭socket
	
	WSACleanup();
	
	return 0;
}

