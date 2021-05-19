#pragma once

#include <Windows.h>
#include <stdio.h>

#include <fltUser.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "fltLib.lib")

#define COMMPORTNAME L"\\Encrypt-hkx3upper"
#define MESSAGE_SIZE 1024

typedef struct EPT_MESSAGE_HEADER
{
	int Command;
	int Length;
}EPT_MESSAGE_HEADER, *PEPT_MESSAGE_HEADER;

//��չ���� , ��Ӣ�ģ��ָ����� , ��Ӣ�ģ����� ���磺txt,docx������count�м�¼����
typedef struct EPT_PROCESS_RULES
{
	char TargetProcessName[MAX_PATH];
	char TargetExtension[100];
	int count;
	UCHAR Hash[32];

}EPT_PROCESS_RULES, * PEPT_PROCESS_RULES;